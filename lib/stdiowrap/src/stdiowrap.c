#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/time.h>

// Include local header
#define STDIOWRAP_C
#include "stdiowrap/stdiowrap.h"
#include "stdiowrap-internal.h"

//#define trace_fn(fmt, ...) fprintf(stderr, "worker %d: %s(" fmt ")\n", wid, __func__, __VA_ARGS__)
#define trace_fn(fmt, ...) 

////////////////////////////////////////////////////////////////////////////////
// Internal State
////////////////////////////////////////////////////////////////////////////////

// SHM list SHM
static struct process_control *ps_ctl = NULL;

// Worker ID of this process
pid_t hspwrap_pid;
wid_t wid;

// The next FILE pointer to use for open
static void *Next_FILE = ((void*)1);

// The "included" WFILEs (names are searchable)
static struct WFILE **IncludedWFILEs  = NULL;
static int     nIncludedWFILEs = 0;

// File Descriptor emulation
static const int fd_check_val   = (((unsigned)-1)>>1) ^ (((unsigned)-1)>>2);
static const int fd_check_mask  = ~(((unsigned)-1)>>2);
//static int fd_next              = (((unsigned)-1)>>1) ^ (((unsigned)-1)>>2);
static int fd_next              = 10;
static struct WFILEDES *fd_list = NULL;

////////////////////////////////////////////////////////////////////////////////
// SHM-specific routines
////////////////////////////////////////////////////////////////////////////////

// TODO: Move to util lib
static void
parse_env (void *dest, char *name, const char *format)
{
  char *ev;
  if (!(ev = getenv(name))) {
    fprintf(stderr, "stdiowrap: Failed to read %s env var.\n", name);
    exit(1);
  }
  if (sscanf(ev, format, dest) != 1) {
    fprintf(stderr, "stdiowrap: Failed to parse %s env var.\n", name);
    exit(1);
  }
}

// TODO: Move to util lib
static void
init_SHM ()
{
  int   fd;

  if (!ps_ctl) {
    char  shmname[256];

    parse_env(&hspwrap_pid, PID_ENVVAR, "%d");
    parse_env(&wid, WORKER_ID_ENVVAR, "%" SCN_WID);

    snprintf(shmname, 256, "/hspwrap.%d.%s", hspwrap_pid, PS_CTL_SHM_NAME);
    fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (fd == -1) {
      fprintf(stderr, "stdiowrap: Failed to open process control SHM (%s): %s\n", shmname, strerror(errno));
      exit(1);
    }
    
    // The file descriptors for the SHMs will already be available to
    // the current process, as it is the child of the process that
    // created the SHMs.  We only need to open the SHM.  Note that
    // our parent already marked the SHMs for removal, so they will
    // cleaned up for us later.
    /*

    // Attach the SHM
    ps_ctl = shmat(fd, NULL, 0);
    if (ps_ctl == ((void *) -1)) {
      fprintf(stderr, "stdiowrap: Failed to attach index SHM.\n");
    }
    */

    // Attach the SHM
    struct stat st;
    fstat(fd, &st);
    ps_ctl = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, 
                      MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
                      fd, 0);
    if (ps_ctl == MAP_FAILED) {
      fprintf(stderr, "stdiowrap: Failed to map process control SHM (%s): %s\n", shmname, strerror(errno));
      exit(1);
    }
    shm_unlink(shmname);
  }
}


static struct file_table_entry *
find_file_entry (const char *name, int *idx)
{
  int i;

  // Attach list SHM if needed
  init_SHM();

  // Find the file
  for (i=0; i < ps_ctl->ft.nfiles; i++) {
    struct file_table_entry *f = ps_ctl->ft.file + i;

    // FIXME SEGFAULT?!?!? FIXME
    if (!strcmp(f->name, name) && (f->wid == wid || f->wid == -1)) {
      if (idx) {
	*idx = i;
      }
      return f;
    }
  }

  return NULL;
}


static int
fill_WFILE_data_SHM (struct WFILE *wf)
{
  struct file_table_entry *f;
  int idx;

  f = find_file_entry(wf->name, &idx);
  if (f) {
    char  shmname[256];

    //fprintf(stderr, "stdiowrap: virtualize file %d %s %s %d %d\n", idx, f->name, wf->name, f->wid, wid);

    snprintf(shmname, 256, "/hspwrap.%d.%d", hspwrap_pid, idx);
    int fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    
    // Attach the shared memory segment
    wf->data = mmap(NULL, f->shm_size, PROT_READ | PROT_WRITE, 
		    MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
		    fd, 0);
    if (wf->data == MAP_FAILED) {
      fprintf(stderr, "stdiowrap: Failed to attach SHM.\n");
      fflush(stderr);
	    exit(1);
    }
    wf->size  = f->size;
    wf->tsize = f->shm_size;
    wf->psize = &(f->size);
    wf->is_stream = (f->wid != -1);

    shm_unlink(shmname);

    // We are done; return
    return 0;
  } else {
    // The SHM was not found
    fprintf(stderr, "stdiowrap: could not virtualize file %s %d\n", wf->name, wid);
    return -1;
  }
}


static void
free_WFILE_data_SHM (struct WFILE *wf)
{
  // The parent process should be responsible for all
  // SHM cleaning ( we just attach/detach ).
  if (wf->data) {
    munmap(wf->data, wf->tsize);
  }
}


////////////////////////////////////////////////////////////////////////////////
// Internal Static Functions
////////////////////////////////////////////////////////////////////////////////

static struct WFILE *
new_WFILE (const char *fn)
{
  struct WFILE *wf;
  const char *name=NULL;
  int    rv;

  /*
  char  *saveptr, *files, *n;
  int    rv, i, qidx;
  char   buf[512];
  // Map filename to proper SHM name
  if (!strncmp(fn,":DB:", 4)) {
    fprintf(stderr,"new 2\n");
    // FIXME DEPRECATED: Blast-specific, just make use pass in fullpath+prefix
    sprintf(buf, "%s/%s", getenv("MCW_DB_FULL_PATH"), getenv("MCW_DB_PREFIX"));
    name = buf;
  } else if ( !(strncmp(fn, ":IN:", 4)) ) {
    fprintf(stderr,"new 3\n");
    // Old-school input file
    sprintf(buf,":MCW:W%d:IN0",atoi(getenv(WORKER_ID_ENVVAR)));
    name = buf;
  } else if (sscanf(fn, ":IN%d:", &qidx) == 1) {
    fprintf(stderr,"new 4\n");
    // Input files..
    sprintf(buf,":MCW:W%d:IN%d",atoi(getenv(WORKER_ID_ENVVAR)), qidx);
    name = buf;
  } else {
    fprintf(stderr,"new 5\n");
    // Looking for filename in mapped outputs
    // TODO: Cache list
    files = strdup(getenv("MCW_O_FILES"));
    for ( n = strtok_r(files, ":", &saveptr), i = 0;
	 n;
	 n = strtok_r(NULL, ":", &saveptr), ++i ) {

      // Match (fn is output file)
      if (!strcmp(n, fn)) {
	sprintf(buf,":MCW:W%d:OUT%d",atoi(getenv(WORKER_ID_ENVVAR)), i);
	name = buf;
	break;
      }
    }
    free(files);
  }
  */
  name = fn;

  // Config files, etc. ?
  /*if (!name) {
    printf("stdiowrap: Unknown file: %s\n", fn);
    name = fn;
  }
  */
	
  // Malloc a new WFILE
  if (!(wf=malloc(sizeof(struct WFILE)))) {
    fprintf(stderr, "stdiowrap: new_WFILE: failed to allocate WFILE.\n");
    exit(1);
  }

  // Init WFILE object
  memset(wf, 0, sizeof(struct WFILE));
  wf->stream = Next_FILE++;

  // Copy file path
  if (!(wf->name = strdup(name))) {
    fprintf(stderr, "stdiowrap: new_WFILE: failed to allocate WFILE name.\n");
    destroy_WFILE(wf);
    errno = ENOMEM;
    return NULL;
  }

  // Fill in data segment 
  rv = fill_WFILE_data_SHM(wf);
  if (rv < 0) {
    fprintf(stderr, "stdiowrap: new_WFILE: failed to fill WFILE.\n");
    destroy_WFILE(wf);
    errno = ENOENT;
    return NULL;
  }

  // Set cursor
  wf->pos    = wf->data;
  wf->offset = 0;

  // Return the new WFILE object
  return wf;
}


static void
destroy_WFILE (struct WFILE *wf)
{
  // Exclude just in case
  exclude_WFILE(wf);

  // Destroy data region
  free_WFILE_data_SHM(wf);

  // Free name memory
  if (wf->name) {
    free(wf->name);
  }

  // Free the WFILE object itself
  free(wf);
}


static int
include_WFILE (struct WFILE *wf)
{
  // Make room for new WFILE in searchable list
  if (!(IncludedWFILEs=realloc(IncludedWFILEs, (nIncludedWFILEs + 1) * sizeof(struct WFILE *)))) {
    fprintf(stderr, "stdiowrap: include_WFILE: failed to grow IncludedWFILEs.\n");
    exit(1);
  }

  // Register / Include WFILE
  IncludedWFILEs[nIncludedWFILEs] = wf;
  return nIncludedWFILEs++;
}


static void
exclude_WFILE (struct WFILE *wf)
{
  int i;
   
  // Search all WFILEs and return if found
  for (i=0; i < nIncludedWFILEs; i++) {
    if (IncludedWFILEs[i] == wf) {
      // We found a match, remove it from the list
      IncludedWFILEs[i] = IncludedWFILEs[--nIncludedWFILEs];
      return;
    }
  }
}


static struct WFILE *
find_WFILE (FILE *f)
{
  int i;

  // Search all WFILEs and return if found
  for (i=0; i < nIncludedWFILEs; i++) {
    if (IncludedWFILEs[i]->stream == f) {
      return IncludedWFILEs[i];
    }
  }

  // Return not found
  return NULL;
}


static void
set_status (enum process_state st)
{
  ps_ctl->process_state[wid] = st;
  ps_ctl->process_cmd[wid]   = NO_CMD;
}


static enum process_cmd
get_command ()
{
  return ps_ctl->process_cmd[wid];
}


static void
update_wfiles_from_flush (struct WFILE *exclude)
{
  struct WFILE *wf;
  int i;  
  for (i=0; i < nIncludedWFILEs; ++i) {
    wf = IncludedWFILEs[i];
    // Check for flush
    if (wf != exclude && wf->size > 0 && *wf->psize == 0) {
      wf->offset += wf->size;
      wf->size    = 0;
      wf->pos     = wf->data;
    }
  }
}


static int
wait_eod (struct WFILE *wf)
{
  enum process_cmd cmd;
  int ret;

  // Not a stream, don't even try!
  if (!wf->is_stream) {
    return 0;
  }

  // Request service
  pthread_mutex_lock(&ps_ctl->lock);
  set_status(EOD);
  pthread_cond_signal(&ps_ctl->need_service);

  while (get_command() == NO_CMD) {
    pthread_cond_wait(&ps_ctl->process_ready[wid], &ps_ctl->lock);
  }

  cmd = get_command();
  switch (cmd) {
  case RUN:
    // More data, update WFILE and continue
    wf->offset += wf->size;
    wf->size    = *wf->psize;
    wf->pos     = wf->data;
    // Also, check output files to see if the size has been reduced (files are flushed)
    update_wfiles_from_flush(wf);

    ret = 1;
    break;

  case QUIT:
    ret = 0;
    break;

  case SUSPEND:
  case RESTORE:
    fprintf(stderr, "stdiowrap: Suspend/Restore is not yet implemented\n");
    exit(1);
    break;
  default:
    fprintf(stderr, "stdiowrap: Unknown command from controller (%d). Exiting\n", cmd);
    exit(1);
    break;
  }

  pthread_mutex_unlock(&ps_ctl->lock);
  return ret;
}


static int
wait_nospace (struct WFILE *wf)
{
  enum process_cmd cmd;
  int ret;

  // Request service
  pthread_mutex_lock(&ps_ctl->lock);
  set_status(NOSPACE);
  pthread_cond_signal(&ps_ctl->need_service);

  while (get_command() == NO_CMD) {
    pthread_cond_wait(&ps_ctl->process_ready[wid], &ps_ctl->lock);
  }

  cmd = get_command();
  switch (cmd) {
  case RUN:
    // More space, update WFILE and continue
    wf->offset += wf->size;
    wf->size    = *wf->psize;
    wf->pos     = wf->data;
    ret = 1;
    break;

  case QUIT:
    ret = 0;
    break;

  case SUSPEND:
  case RESTORE:
    fprintf(stderr, "stdiowrap: Suspend/Restore is not yet implemented\n");
    exit(1);
    break;
  default:
    fprintf(stderr, "stdiowrap: Unknown command from controller (%d). Exiting\n", cmd);
    exit(1);
    break;
  }

  pthread_mutex_unlock(&ps_ctl->lock);
  return ret;
}


////////////////////////////////////////////////////////////////////////////////
// External Exported Functions
////////////////////////////////////////////////////////////////////////////////


#define MAP_WF(w,s)      struct WFILE *w = find_WFILE((s)); if (!(w)) { errno = EINVAL; return; }
#define MAP_WF_E(w,s,e)  struct WFILE *w = find_WFILE((s)); if (!(w)) { errno = EINVAL; return (e); }


extern FILE *
stdiowrap_fopen (const char *path, const char *mode)
{
  trace_fn("'%s', '%s'", path, mode);
  struct WFILE *wf = new_WFILE(path);

  // Check for creation error
  if (!wf) {
    // errno will fall through from new_WFILE()
    return NULL;
  }

  // Assume cursor positioned at start
  wf->pos    = wf->data;
  wf->offset = 0;

  // Register this as a valid mapping
  include_WFILE(wf);

  // Return the WFILE's stream handle pointer
  return wf->stream;
}


extern int
stdiowrap_fclose (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, 0);

  // Unregister and destroy the wrapped object
  exclude_WFILE(wf);
  destroy_WFILE(wf);
  return 0;
}


extern size_t
stdiowrap_fread (void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  trace_fn("%p, %zu, %zu, %p", ptr, size, nmemb, stream);
  MAP_WF_E(wf, stream, 0);

  char   *ubound = wf->data + wf->size;	// upper bound
  size_t  avail  = ubound - wf->pos;	// bytes available
  size_t  remain = size * nmemb;
  size_t  read   = 0;

  while (remain > avail) {
    // Wants too much, copy everything we have
    memcpy(ptr, wf->pos, avail);
    ptr    += avail;
    read   += avail;
    remain -= avail;

    // Wait for data
    if (wait_eod(wf)) {
      // Update locals
      ubound = wf->data + wf->size;
      avail  = ubound - wf->pos;
    } else {
      // Update locals
      ubound = wf->data;
      avail = 0;
      break;
    }
  }

  // Copy last bit of data
  if (avail > remain) {
    memcpy(ptr, wf->pos, remain);
    // Update locals
    read  += remain;
    remain = 0;
  }

  // Figure out how many members we really got, adjust cursor accordingly
  nmemb = read / size;
  wf->pos += (size * nmemb) - read;

  // Return item count
  return nmemb;
}


extern char *
stdiowrap_fgets (char *s, int size, FILE *stream)
{
  trace_fn("%p, %d, %p", s, size, stream);
  MAP_WF_E(wf, stream, NULL);
  char ch;
  char *p = s;

  // Quick sanity check on size
  if (size <= 1) {
    return NULL;
  }
  
  // Copy from wf until newline, EOF, or size limit
  while (--size) {
    // End of buffer, fetch more
    if (wf->pos == (wf->data + wf->size)) {
      if (!wait_eod(wf)) {
	break;
      }
    }
    ch = (char)(*(wf->pos++));
    *(p++) = ch; 
    if (ch == '\n') {
      break;
    }
  }

  // Check for EOF without reading case
  if (p == s) {
    return NULL;
  }

  // Terminate and return a pointer to the read string
  *p = '\0';
  return s;
}


extern int
stdiowrap_fgetc (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, EOF);
  unsigned char *ubound = wf->data + wf->size;	// upper bound

  // Make sure there is a character to be read
  if (wf->pos >= ubound) {
    if (!wait_eod(wf)) {
      // no more data
      return EOF;
    }
  }

  // Return read char
  return (int)(*(wf->pos++));
}


extern int
stdiowrap_getc (FILE *stream)
{
  trace_fn("%p", stream);
  return stdiowrap_fgetc(stream);
}


extern int
stdiowrap_fscanf (FILE *stream, const char *format, ...)
{
  trace_fn("%p, '%s', ...", stream, format);
  MAP_WF_E(wf, stream, -1);

  // !!av: This is a sub, prob needs to be filled in
  printf("stdiowrap_fscanf(%s): stub\n", wf->name);
  
  return 0;
  
/*
  va_list ap;
  int     rv;
  // Use vsscanf()
  va_start(ap, format);
  rv = vsscanf(((char*)wf->pos), format, ap);
  va_end(ap);

  // Return the conversion count
  return rv;
*/
}


extern int
stdiowrap_ungetc (int c, FILE *stream)
{
  trace_fn("%d, %p", c, stream);
  MAP_WF_E(wf, stream, EOF);

  // Make sure we can position the cursor back one char
  if (wf->pos == wf->data || (wf->pos - 1) >= (wf->data + wf->size)) {
    return EOF;
  }

  // Move the cursor position back one char
  wf->pos--;

  // Fill in "new" value
  *(wf->pos) = (unsigned char)c;

  // Return the new char
  return (int)(*(wf->pos));
}


extern int
stdiowrap_fseek (FILE *stream, long offset, int whence)
{
  trace_fn("%p, %ld, %d", stream, offset, whence);
  MAP_WF_E(wf, stream, -1);

  // File out what we are relative to
  switch (whence) {
  case SEEK_SET:
    // Relative to the start
    wf->pos = wf->data+offset;
    break;
  case SEEK_CUR:
    // Relative to current cursor postion
    wf->pos += offset;
    break;
  case SEEK_END:
    // Relative to end of stream
    wf->pos = wf->data + wf->size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  // Return success
  return 0;
}


extern int
stdiowrap_fseeko (FILE *stream, off_t offset, int whence)
{
  trace_fn("%p, %ld, %d", stream, offset, whence);
  MAP_WF_E(wf, stream, -1);

  // File out what we are relative to
  switch (whence) {
  case SEEK_SET:
    // Relative to the start
    wf->pos = wf->data+offset;
    break;
  case SEEK_CUR:
    // Relative to current cursor postion
    wf->pos += offset;
    break;
  case SEEK_END:
    // Relative to end of stream
    wf->pos = wf->data + wf->size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  // Return success
  return 0;
}


extern long
stdiowrap_ftell (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return (long)( wf->offset + wf->pos - wf->data );
}


extern off_t
stdiowrap_ftello (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return (off_t)( wf->offset + wf->pos - wf->data );
}


extern void
stdiowrap_rewind (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF(wf, stream);

  // Reset the cursor pointer to the start
  wf->pos = wf->data;
}


extern int
stdiowrap_feof (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, 0);

  // Check the cursor pointer for bounds
  if (wf->pos >= wf->data+wf->size) {
    return 1;
  } else {
    return 0;
  }
}


extern int
stdiowrap_fflush (FILE *stream)
{
  trace_fn("%p", stream);
  MAP_WF_E(wf, stream, EOF);

  // All operations happen to memory, so
  // no flush is needed.
  return 0;
}


extern size_t
stdiowrap_fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  trace_fn("%p, %zu, %zu, %p", ptr, size, nmemb, stream);
  MAP_WF_E(wf, stream, 0);

  // Check bounds
  if ((wf->pos + size) > (wf->data + wf->tsize)) {
    // No room for even one element
    errno = ENOSPC;
    return 0;
  }
  if ((wf->pos + size * nmemb) > (wf->data + wf->tsize)) {
    // Wants too many elements; trim.
    nmemb = ((wf->data + wf->tsize) - wf->pos) / size;
  }

  // Copy into requested buffer
  memcpy(wf->pos, ptr, nmemb * size);

  // Advance cursor
  wf->pos += nmemb * size;

  // Advance record of data segment size
  wf->size += nmemb * size;

  // Tell the SHM about the increased filled portion as well
  *(wf->psize) = wf->size;

  // Return item count
  return nmemb;
}


extern int
stdiowrap_fputs (const char *s, FILE *stream)
{
  trace_fn("'%s', %p", s, stream);
  MAP_WF_E(wf, stream, -1);
  const char *p = s;

  while (1) {
    // Copy bytes until end of string or no space avail
    while (*p != '\0' && wf->pos < wf->data + wf->tsize) {
      *(wf->pos++) = *(p++);
    }

    // Didn't make it to end of string, request more space
    if (*p != '\0') {
      if (wait_nospace(wf)) {
        // Got more space, resume
        continue;
      } else {
        // Request denied, error
        // FIXME: is setting to tsize correct now that we have offset?)
        wf->size = wf->tsize;
        *(wf->psize) = wf->size;
        errno = ENOSPC;
        return EOF;
      }
    }
    // Done
    break;
  }

  // Update sizes
  wf->size += (p-s);
  *(wf->psize) = wf->size;

  //fprintf(stderr, "POS: %p SIZE %zu\n", wf->pos, wf->size);

  // Great success
  return 1;
}


extern int
stdiowrap_fputc (int c, FILE *stream)
{
  trace_fn("%c, %p", c, stream);
  MAP_WF_E(wf, stream, EOF);

  // Make sure there is room for a character to be written
  if (wf->pos >= (wf->data + wf->tsize)) {
    // Not enough, request more space
    if (!wait_nospace(wf)) {
      // Request denied, error
      errno = ENOSPC;
      return EOF;
    }
  }

  // Write and update sizes
  *(wf->pos++) = (char)c & 0xFF;
  wf->size++;
  *(wf->psize) = wf->size;

  return c;
}


extern int
stdiowrap_putc (int c, FILE *stream)
{
  trace_fn("%c, %p", c, stream);
  return stdiowrap_fputc(c, stream);
}


extern int
stdiowrap_fprintf (FILE *stream, const char *format, ...)
{
  {
    va_list dbg_ap;
    char dbg_buff[4096];
    va_start(dbg_ap, format);
    vsnprintf(dbg_buff, sizeof(dbg_buff), format, dbg_ap);
    va_end(dbg_ap);

    trace_fn("%p, '%s', <<%s>>", stream, format, dbg_buff);
  }

  va_list ap;
  size_t  sz;
  int     wc;
  MAP_WF_E(wf, stream, -1);

  while (1) {
    // How much room remains?
    sz = (wf->data + wf->tsize) - wf->pos;

    // Use vsnprintf() to directly write the data into the buffer
    // respecting the max size.
    va_start(ap, format);
    wc = vsnprintf((char*)wf->pos, sz, format, ap);
    va_end(ap);

    // Check bounds
    if (wc < sz) {
      break;
    } 
    // Not enough room, request a flush
    if (wait_nospace(wf)) {
      // File has room now, and pointers have been adjusted, retry
      continue;
    } else {
      // Not getting new room for some reason, truncate output
      // FIXME: is setting to tsize correct now that we have offset?)
      wf->size = wf->tsize;
      wf->pos  = wf->data + wf->size;
      *(wf->psize) = wf->size;
      // Return error
      errno = ENOSPC;
      return -1;
    }
  }

  // Increase the recorded size of the data in the SHM
  // and update the cursor position.
  wf->size += wc;
  wf->pos  += wc;
  *(wf->psize) = wf->size;
  
  // Return the write count
  return wc;
}


int
stdiowrap_stat (const char *path, struct stat *buf)
{
  trace_fn("'%s', %p", path, buf);
  struct file_table_entry *f;
  time_t t;
  int idx;

  if (!buf) {
    errno = EFAULT;
    return -1;
  }

  f = find_file_entry(path, &idx);
  if (f) {
    t = time(NULL);
    buf->st_dev     = 1;
    buf->st_ino     = idx;
    buf->st_mode    = S_IFMT | S_IFREG | S_IRWXU;
    buf->st_nlink   = 1;
    buf->st_uid     = getuid();
    buf->st_gid     = getgid();
    buf->st_rdev    = 0;
    buf->st_size    = f->shm_size;
    buf->st_blksize = 512;
    buf->st_blocks  = f->shm_size/512 + 1;
    buf->st_atime   = t;
    buf->st_mtime   = t;
    buf->st_ctime   = t;
    return 0;
  } else {
    // File doesn't exist
    errno = ENOENT;
    return -1;
  }
}

// TODO: Fix these functions (use separate inclusion list for POSIX I/O)

struct WFILEDES *
find_WFILEDES_path (const char *path)
{
  struct WFILEDES *wfd;

  for (wfd = fd_list; wfd; wfd=wfd->next) {
    if (!strcmp(ps_ctl->ft.file[wfd->fte_idx].name, path)) {
      break;
    }
  }
  return wfd;
}


struct WFILEDES *
find_WFILEDES_fd (int fd)
{
  struct WFILEDES *wfd;

  for (wfd = fd_list; wfd; wfd=wfd->next) {
    if (wfd->fd == fd) {
      break;
    }
  }
  return wfd;
}


int
stdiowrap_open (const char *path, int flags)
{
  trace_fn("'%s', %d", path, flags);
  struct file_table_entry *f;
  struct WFILEDES *wfd;
  int idx;

  // First, check if we've opened this file before
  wfd = find_WFILEDES_path(path);
  if (wfd) {
    return wfd->fd;
  }

  // Didn't find it, got to make a new one
  f = find_file_entry(path, &idx);
  if (!f) {
    fprintf(stderr, "stdiowrap: open: %s could not be found\n", path);
    return -1;
  }

  wfd = malloc(sizeof(struct WFILEDES));
  wfd->fd = fd_next++;
  wfd->data = NULL;
  wfd->ref_cnt = 0;
  wfd->fte_idx = idx;
  wfd->next = fd_list;
  fd_list = wfd;

  return wfd->fd;
}
	  

int
stdiowrap_close (int fd)
{
  trace_fn("%d", fd);
  struct WFILEDES *wfd;

  wfd = find_WFILEDES_fd(fd);
  if (!wfd) {
    fprintf(stderr, "stdiowrap: close: couldn't find fd: %d\n", fd);
    errno = EBADF;
    return -1;
  }
  return 0;
}


void *
stdiowrap_mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
  trace_fn("%p, %zu, %d, %d, %d, %lld", addr, len, prot, flags, fd, (long long)off);
  struct WFILEDES *wfd;

  wfd = find_WFILEDES_fd(fd);
  if (!wfd) {
    fprintf(stderr, "stdiowrap: mmap failed: couldn't find WFD\n");
    errno = EBADF;
    return MAP_FAILED;
  }

  if (addr != NULL) {
    fprintf(stderr, "stdiowrap: mmap failed: can't map to specific address\n");
    errno = ENOMEM;
    return MAP_FAILED;
  }

  if (prot & (PROT_WRITE|PROT_EXEC)) {
    fprintf(stderr, "stdiowrap: mmap failed: cannot grant write or exec access\n");
    errno = EACCES;
    return MAP_FAILED;
  }

  struct file_table_entry *fte = &ps_ctl->ft.file[wfd->fte_idx];

  if (fte->shm_size < off + len) {
    fprintf(stderr, "stdiowrap: mmap failed: length and/or offset are out of range\n");
    errno = ENXIO;
    return MAP_FAILED;
  }

  // mmap if not already mapped
  if (wfd->ref_cnt == 0) {
    char shmname[256];
    int  shmfd;
    snprintf(shmname, 256, "/hspwrap.%d.%d", hspwrap_pid, wfd->fte_idx);
    shmfd = shm_open(shmname, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (shmfd == -1) {
      fprintf(stderr, "stdiowrap: mmap failed: couldn't open SHM: %s\n", shmname);
      return MAP_FAILED;
    }
    wfd->data = mmap(NULL, fte->shm_size, PROT_READ, MAP_SHARED, shmfd, 0);
    if (wfd->data == MAP_FAILED) {
      fprintf(stderr, "stdiowrap: mmap failed: couldn't mmap SHM: %s\n", shmname);
      return MAP_FAILED;
    }
    shm_unlink(shmname);
  }

  wfd->ref_cnt++;
  return wfd->data + off;
}


int
stdiowrap_munmap (void *addr, size_t len)
{
  trace_fn("%p, %zu <<STUB>>", addr, len);
  // TODO: implement
  return 0;
}

#undef MAP_WF
#undef MAP_WF_E
