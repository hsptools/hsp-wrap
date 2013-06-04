#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
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

    snprintf(shmname, 256, "/mcw.%d.%s", hspwrap_pid, PS_CTL_SHM_NAME);
    fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    
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
      fprintf(stderr,"stdiowrap: Failed to attach index SHM (%s): %s\n", shmname, strerror(errno));
      exit(1);
    }
  }
}


static int
fill_WFILE_data_SHM (struct WFILE *wf)
{
  int i;

  // Attach list SHM if needed
  init_SHM();

  // Only fill for files that are listed
  for (i=0; i < ps_ctl->ft.nfiles; i++) {
    struct file_table_entry *f = ps_ctl->ft.file + i;

    // FIXME SEGFAULT?!?!? FIXME
    if (!strcmp(f->name, wf->name) && f->wid == wid) {
      char  shmname[256];

      //fprintf(stderr, "FILE%d %s %s %d %d\n", i, f->name, wf->name, f->wid, wid);

      snprintf(shmname, 256, "/mcw.%d.%d", hspwrap_pid, i);
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
      // We are done; return
      return 0;
    }
  }

  // The SHM was not found
  return -1;
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
    destroy_WFILE(wf);
    errno = ENOMEM;
    return NULL;
  }

  // Fill in data segment 
  rv = fill_WFILE_data_SHM(wf);
  if (rv < 0) {
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


static void
include_WFILE (struct WFILE *wf)
{
  // Make room for new WFILE in searchable list
  if (!(IncludedWFILEs=realloc(IncludedWFILEs, (nIncludedWFILEs + 1) * sizeof(struct WFILE *)))) {
    fprintf(stderr, "stdiowrap: include_WFILE: failed to grow IncludedWFILEs.\n");
    exit(1);
  }

  // Register / Include WFILE
  IncludedWFILEs[nIncludedWFILEs] = wf;
  nIncludedWFILEs++;
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
  MAP_WF_E(wf, stream, 0);

  // Unregister and destroy the wrapped object
  exclude_WFILE(wf);
  destroy_WFILE(wf);
  return 0;
}


extern size_t
stdiowrap_fread (void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  MAP_WF_E(wf, stream, 0);

  unsigned char *ubound = wf->data + wf->size;	// upper bound
  size_t         avail  = ubound - wf->pos;	// bytes available
  size_t         remain = size * nmemb;
  size_t         read   = 0;

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
  return stdiowrap_fgetc(stream);
}


extern int
stdiowrap_fscanf (FILE *stream, const char *format, ...)
{
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
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return (long)( wf->offset + wf->pos - wf->data );
}


extern off_t
stdiowrap_ftello (FILE *stream)
{
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return (off_t)( wf->offset + wf->pos - wf->data );
}


extern void
stdiowrap_rewind (FILE *stream)
{
  MAP_WF(wf, stream);

  // Reset the cursor pointer to the start
  wf->pos = wf->data;
}


extern int
stdiowrap_feof (FILE *stream)
{
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
  MAP_WF_E(wf, stream, EOF);

  // All operations happen to memory, so
  // no flush is needed.
  return 0;
}


extern size_t
stdiowrap_fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
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
  return stdiowrap_fputc(c, stream);
}


extern int
stdiowrap_fprintf (FILE *stream, const char *format, ...)
{
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

#undef MAP_WF
#undef MAP_WF_E
