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


////////////////////////////////////////////////////////////////////////////////
// Internal State
////////////////////////////////////////////////////////////////////////////////


// Set default backend
#ifndef BACKEND_MMAP
#  ifndef BACKEND_SHM
#    warn "A backend has not been set; using default: BACKEND_MMAP."
#    define BACKEND_MMAP
#  endif
#endif


// Defines and includes needed for the SHMs
#ifdef BACKEND_SHM
#  include <sys/ipc.h>
#  include <sys/shm.h>
#  include <sys/types.h>
#  include <sys/time.h>
#  define MAX_DB_FILES   (256)
#endif


// Include local header
#define STDIOWRAP_C
#include "stdiowrap/stdiowrap.h"
#include "stdiowrap-internal.h"


// SHM list SHM
#ifdef BACKEND_SHM
static filesizes_t *file_sizes = NULL;
#endif


// The next FILE pointer to use for open
static void *Next_FILE = ((void*)1);


// The "included" WFILEs (names are searchable)
static WFILE **IncludedWFILEs  = NULL;
static int     nIncludedWFILEs = 0;

////////////////////////////////////////////////////////////////////////////////
// Internal Static Functions
////////////////////////////////////////////////////////////////////////////////


#ifdef BACKEND_MMAP
static void fill_WFILE_data_MMAP(WFILE *wf)
{
  struct stat buf;
  int         fd;

  // Get a FD for this file
  if( !(fd=open(wf->name,O_RDONLY)) ) {
    fprintf(stderr,"stdiowrap: fill_WFILE_data_MMAP: open() failed: \"%s\".\n",
	    wf->name);
    exit(1);
  }

  // Find file length
  if( fstat(fd, &buf) ) {
    close(fd);
    fprintf(stderr,"stdiowrap: fill_WFILE_data_MMAP: stat() failed: \"%s\".\n",
	    wf->name);
    exit(1);
  }
  wf->size = buf.st_size;

  // Map and then close
  wf->data = mmap(NULL,wf->size,PROT_READ,MAP_SHARED,fd,0);
  close(fd);
  if( wf->data == MAP_FAILED ) {
    fprintf(stderr,"stdiowrap: fill_WFILE_data_MMAP: mmap() failed: \"%s\".\n",
	    wf->name);
    exit(1);
  }
}


static void free_WFILE_data_MMAP(WFILE *wf)
{
  // Unmap the memory region
  if( munmap(wf->data,wf->size) ) {
    fprintf(stderr,
	    "stdiowrap: free_WFILE_data_MMAP: munmap() failed: \"%s\".\n",
	    wf->name);
    exit(1);
  }
  wf->data = NULL;
  wf->size = 0;
}
#endif


#ifdef BACKEND_SHM
static void init_SHM()
{
  char *ev;
  int   fd;

  if( !file_sizes ) {
    char  shmname[256];

    snprintf(shmname, 256, "/mcw.%s.%s", getenv("MCW_PID"), "file_sizes");
    fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    
    /*
    // The file descriptors for the SHMs will already be available to
    // the current process, as it is the child of the process that
    // created the SHMs.  We only need to open the SHM.  Note that
    // our parent already marked the SHMs for removal, so they will
    // cleaned up for us later.
    if( !(ev=getenv("MCW_FI_SHM_FD")) ) {
      fprintf(stderr,"stdiowrap: Failed to read MCW_FI_SHM_FD env var.\n");
      exit(1);
    }
    if( sscanf(ev,"%d",&fd) != 1 ) {
      fprintf(stderr,"stdiowrap: Failed to parse MCW_FI_SHM_FD env var.\n");
      exit(1);
    }
    */

    // Attach the SHM
    struct stat st;
    fstat(fd, &st);
    file_sizes = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, 
                      MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
                      fd, 0);
    if( file_sizes == MAP_FAILED ) {
      fprintf(stderr,"stdiowrap: Failed to attach index SHM (%d): %s\n", fd, strerror(errno));
      exit(1);
    }
  }

}


static int fill_WFILE_data_SHM(WFILE *wf)
{
  int i;


  // Attach list SHM if needed
  init_SHM();

  // Only fill for files that are listed
  for(i=0; i < file_sizes->nfiles; i++) {
    if( !strcmp(file_sizes->fs[i].name,wf->name) ) {
      char  shmname[256];

      snprintf(shmname, 256, "/mcw.%s.%d", getenv("MCW_PID"), i);
      int fd = shm_open(shmname, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      
      // Attach the shared memory segment
      wf->data = mmap(NULL, file_sizes->fs[i].shmsize, PROT_READ | PROT_WRITE, 
                      MAP_SHARED /*| MAP_LOCKED | MAP_HUGETLB*/,
                      fd, 0);
      if( wf->data == ((void*)-1) ) {
        fprintf(stderr,"stdiowrap: Failed to attach SHM.\n");
	      exit(1);
      }
      wf->size  = file_sizes->fs[i].size;
      wf->tsize = file_sizes->fs[i].shmsize;
      wf->psize = &(file_sizes->fs[i].size);
      // We are done; return
      return 0;
    }
  }

  // The SHM was not found
  return -1;
}


static void free_WFILE_data_SHM(WFILE *wf)
{
  // The parent process should be responsible for all
  // SHM cleaning ( we just attach/detach ).
  if( wf->data ) {
    munmap(wf->data, wf->tsize);
  }
}
#endif


static WFILE* new_WFILE(const char *fn)
{
  WFILE *wf;
  char  *saveptr, *files, *n;
  const char *name=NULL;
  char   buf[512];
  int    rv, i, qidx;

  // Map filename to proper SHM name
  if( !strncmp(fn,":DB:", 4) ) {
    // FIXME DEPRECATED: Blast-specific, just make use pass in fullpath+prefix
    sprintf(buf, "%s/%s", getenv("MCW_DB_FULL_PATH"), getenv("MCW_DB_PREFIX"));
    name = buf;
  } else if ( !(strncmp(fn, ":IN:", 4)) ) {
    // Old-school input file
    sprintf(buf,":MCW:W%d:IN0",atoi(getenv("MCW_WID")));
    name = buf;
  } else if( sscanf(fn, ":IN%d:", &qidx) == 1 ) {
    // Input files..
    sprintf(buf,":MCW:W%d:IN%d",atoi(getenv("MCW_WID")), qidx);
    name = buf;
  } else {
    // Looking for filename in mapped outputs
    // TODO: Cache list
    files = strdup(getenv("MCW_O_FILES"));
    for( n = strtok_r(files, ":", &saveptr), i = 0;
	 n;
	 n = strtok_r(NULL, ":", &saveptr), ++i ) {

      // Match (fn is output file)
      if( !strcmp(n, fn) ) {
	sprintf(buf,":MCW:W%d:OUT%d",atoi(getenv("MCW_WID")), i);
	name = buf;
	break;
      }
    }
    free(files);
  }

  // Config files, etc. ?
  // FIXME: Don't trigger on DB matches
  if (!name) {
    //printf("stdiowrap: Unknown file: %s\n", fn);
    name = fn;
  }
	
  // Malloc a new WFILE
  if( !(wf=malloc(sizeof(WFILE))) ) {
    fprintf(stderr,"stdiowrap: new_WFILE: failed to allocate WFILE.\n");
    exit(1);
  }

  // Init WFILE object
  memset(wf,0,sizeof(WFILE));
  wf->stream = Next_FILE++;

  // Copy file path
  if( !(wf->name = strdup(name)) ) {
    destroy_WFILE(wf);
    errno = ENOMEM;
    return NULL;
  }

  // Fill in data segment 
#ifdef BACKEND_MMAP
  rv = fill_WFILE_data_MMAP(wf);
#endif
#ifdef BACKEND_SHM
  rv = fill_WFILE_data_SHM(wf);
#endif
  if( rv < 0 ) {
    destroy_WFILE(wf);
    errno = ENOENT;
    return NULL;
  }

  // Set cursor
  wf->pos = wf->data;

  // Return the new WFILE object
  return wf;
}


static void destroy_WFILE(WFILE *wf)
{
  // Exclude just in case
  exclude_WFILE(wf);

  // Destroy data region
#ifdef BACKEND_MMAP
  free_WFILE_data_MMAP(wf);
#endif
#ifdef BACKEND_SHM
  free_WFILE_data_SHM(wf);
#endif

  // Free name memory
  if( wf->name ) {
    free(wf->name);
  }

  // Free the WFILE object itself
  free(wf);
}


static void include_WFILE(WFILE *wf)
{
  // Make room for new WFILE in searchable list
  if( !(IncludedWFILEs=realloc(IncludedWFILEs,(nIncludedWFILEs+1)*sizeof(WFILE*))) ) {
    fprintf(stderr,"stdiowrap: include_WFILE: failed to grow IncludedWFILEs.\n");
    exit(1);
  }

  // Register / Include WFILE
  IncludedWFILEs[nIncludedWFILEs] = wf;
  nIncludedWFILEs++;
}


static void exclude_WFILE(WFILE *wf)
{
  int i;
   
  // Search all WFILEs and return if found
  for(i=0; i < nIncludedWFILEs; i++) {
    if( IncludedWFILEs[i] == wf ) {
      // We found a match, remove it from the list
      IncludedWFILEs[i] = IncludedWFILEs[--nIncludedWFILEs];
      return;
    }
  }
}


static WFILE* find_WFILE(FILE *f)
{
  int i;

  // Search all WFILEs and return if found
  for(i=0; i < nIncludedWFILEs; i++) {
    if( IncludedWFILEs[i]->stream == f ) {
      return IncludedWFILEs[i];
    }
  }

  // Return not found
  return NULL;
}


////////////////////////////////////////////////////////////////////////////////
// External Exported Functions
////////////////////////////////////////////////////////////////////////////////


#define MAP_WF(w,s)      WFILE *w = find_WFILE((s)); if( !(w) ) { errno = EINVAL; return; }
#define MAP_WF_E(w,s,e)  WFILE *w = find_WFILE((s)); if( !(w) ) { errno = EINVAL; return (e); }


extern FILE* stdiowrap_fopen(const char *path, const char *mode)
{
  WFILE *wf = new_WFILE(path);

  // Check for creation error
  if( !wf ) {
    // errno will fall through from new_WFILE()
    return NULL;
  }

  // Assume cursor positioned at start
  wf->pos = wf->data;

  // Register this as a valid mapping
  include_WFILE(wf);

  // Return the WFILE's stream handle pointer
  return wf->stream;
}


extern int stdiowrap_fclose(FILE *stream)
{
  MAP_WF_E(wf, stream, 0);

  // Unregister and destroy the wrapped object
  exclude_WFILE(wf);
  destroy_WFILE(wf);
  return 0;
}


extern size_t stdiowrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  MAP_WF_E(wf, stream, 0);

  // Check bounds
  if( (wf->pos+size) > (wf->data+wf->size) ) {
    // No room for even one element
    return 0;
  }
  if( (wf->pos+size*nmemb) > (wf->data+wf->size) ) {
    // Wants too many elements; trim.
    nmemb = ((wf->data+wf->size)-wf->pos) / size;
  }

  // Copy into requested buffer
  memcpy(ptr,wf->pos,nmemb*size);

  // Advance cursor
  wf->pos += nmemb*size;

  // Return item count
  return nmemb;
}


extern char* stdiowrap_fgets(char *s, int size, FILE *stream)
{
  MAP_WF_E(wf, stream, NULL);
  char *p = s;

  // Quick sanity check on size
  if( size <= 1 ) {
    return NULL;
  }
  
  // Copy from wf until newline, EOF, or size limit
  while( (--size) && (wf->pos < (wf->data+wf->size)) && ((*(p++)=((char)(*(wf->pos++)))) != '\n') );

  // Check for EOF without reading case
  if( p == s ) {
    return NULL;
  }

  // Terminate and return a pointer to the read string
  *p = '\0';
  return s;
}


extern int stdiowrap_fgetc(FILE *stream)
{
  MAP_WF_E(wf, stream, EOF);

  // Make sure there is a character to be read
  if( wf->pos >= (wf->data+wf->size) ) {
    return EOF;
  }

  // Return read char
  return ((int)(*(wf->pos++)));
}


extern int stdiowrap_getc(FILE *stream)
{
  return stdiowrap_fgetc(stream);
}


extern int stdiowrap_fscanf(FILE *stream, const char *format, ...)
{
  MAP_WF_E(wf, stream, -1);

  // !!av: This is a sub, prob needs to be filled in
  printf("stdiowrap_fscanf(%s): stub\n",wf->name);
  
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


extern int stdiowrap_ungetc(int c, FILE *stream)
{
  MAP_WF_E(wf, stream, EOF);

  // Make sure we can position the cursor back one char
  if( (wf->pos == wf->data) || ((wf->pos-1) >= (wf->data+wf->size)) ) {
    return EOF;
  }

  // Move the cursor position back one char
  wf->pos--;

  // Fill in "new" value
  *(wf->pos) = ((unsigned char)c);

  // Return the new char
  return ((int)(*(wf->pos)));
}


extern int stdiowrap_fseek(FILE *stream, long offset, int whence)
{
  MAP_WF_E(wf, stream, -1);

  // File out what we are relative to
  switch(whence) {
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


extern int stdiowrap_fseeko(FILE *stream, off_t offset, int whence)
{
  MAP_WF_E(wf, stream, -1);

  // File out what we are relative to
  switch(whence) {
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


extern long stdiowrap_ftell(FILE *stream)
{
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return ((long)( wf->pos - wf->data ));
}


extern off_t stdiowrap_ftello(FILE *stream)
{
  MAP_WF_E(wf, stream, -1);

  // Return the offset of the cursor from the start
  return ((off_t)( wf->pos - wf->data ));
}


extern void stdiowrap_rewind(FILE *stream)
{
  MAP_WF(wf, stream);

  // Reset the cursor pointer to the start
  wf->pos = wf->data;
}


extern int stdiowrap_feof(FILE *stream)
{
  MAP_WF_E(wf, stream, 0);

  // Check the cursor pointer for bounds
  if( wf->pos >= wf->data+wf->size ) {
    return 1;
  } else {
    return 0;
  }
}


extern int stdiowrap_fflush(FILE *stream)
{
  MAP_WF_E(wf, stream, EOF);

  // All operations happen to memory, so
  // no flush is needed.
  return 0;
}


extern size_t stdiowrap_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  MAP_WF_E(wf, stream, 0);

  // Check bounds
  if( (wf->pos+size) > (wf->data+wf->tsize) ) {
    // No room for even one element
    errno = ENOSPC;
    return 0;
  }
  if( (wf->pos+size*nmemb) > (wf->data+wf->tsize) ) {
    // Wants too many elements; trim.
    nmemb = ((wf->data+wf->tsize)-wf->pos) / size;
  }

  // Copy into requested buffer
  memcpy(wf->pos,ptr,nmemb*size);

  // Advance cursor
  wf->pos += nmemb*size;

  // Advance record of data segment size
  wf->size += nmemb*size;

  // Tell the SHM about the increased filled portion as well
  *(wf->psize) = wf->size;

  // Return item count
  return nmemb;
}


extern int stdiowrap_fputs(const char *s, FILE *stream)
{
  MAP_WF_E(wf, stream, -1);
  const char *p = s;

  // Copy bytes until end of string or no space avail
  while (*p != '\0' && wf->pos < wf->data+wf->tsize) {
    *(wf->pos++) = *(p++);
  }

  // Didn't make it to end of string, space must be limited. Error.
  if (*p != '\0') {
    errno = ENOSPC;
    return EOF;
  }

  // Update sizes
  wf->size += (p-s);
  *(wf->psize) = wf->size;

  // Great success
  return 1;
}


extern int stdiowrap_fputc (int c, FILE *stream)
{
  MAP_WF_E(wf, stream, EOF);

  // Make sure there is room for a character to be written
  if( wf->pos >= (wf->data+wf->tsize) ) {
    errno = ENOSPC;
    return EOF;
  }

  // Write and update sizes
  *(wf->pos++) = (char)c & 0xFF;
  wf->size++;
  *(wf->psize) = wf->size;

  return c;
}


extern int stdiowrap_putc (int c, FILE *stream)
{
  return stdiowrap_fputc(c, stream);
}


extern int stdiowrap_fprintf(FILE *stream, const char *format, ...)
{
  va_list ap;
  int     wc;
  MAP_WF_E(wf, stream, -1);
  
  // Use vsnprintf() to directly write the data into the buffer
  // respecting the max size.
  va_start(ap, format);
  wc = vsnprintf(((char*)wf->pos),
		 ((unsigned long)((wf->data+wf->tsize)-wf->pos)),
		 format, ap);
  va_end(ap);

  // Check bounds
  if( wc >= ((unsigned long)((wf->data+wf->tsize)-wf->pos)) ) {
    // The output was clipped as there wasn't enough room.
    wf->size = wf->tsize;
    wf->pos  = wf->data + wf->size;
    *(wf->psize) = wf->size;
    return wc;
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
