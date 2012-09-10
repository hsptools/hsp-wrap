#ifndef STDIOWRAP_INTERNAL_H__
#define STDIOWRAP_INTERNAL_H__

#include <stdio.h>
#include <stdarg.h>

////////////////////////////////////////////////////////////////////////////////
// Internal to stdiowrap.c
////////////////////////////////////////////////////////////////////////////////

// Structure to hold file/stream information
typedef struct {
  FILE          *stream;  // FILE pointer that is mapped
  char          *name;    // Path of the file
  unsigned char *data;    // Pointer to memory holding contents of file
  size_t         size;    // Size of the data segment in memory
  long           tsize;   // Size of the container holding the data segment
  long          *psize;   // Pointer to back-end's size so it can be updated
  unsigned char *pos;     // Current cursor position in data segment
} WFILE;

#ifdef BACKEND_SHM
// Holds info on one SHM <-> file mapping
typedef struct {
  char *shm;                    // Non-local (mostly invalid) pointer to shm
  long  shmsize;                // Size of the SHM
  long  size;                   // Size of the data in the SHM holding the file
  int   fd;                     // File descriptor associated with the SHM
  char  name[256];              // Name of the file in the SHM
} filesize_t;

// Holds all SHM info
typedef struct {
  int         nfiles;           // Number of files in SHMs
  filesize_t  fs[MAX_DB_FILES]; // Info on the files in the SHMs
} filesizes_t;
#endif

static WFILE *new_WFILE(const char *fn);
static void   destroy_WFILE(WFILE *wf);
static void   include_WFILE(WFILE *wf);
static void   exclude_WFILE(WFILE *wf);
static WFILE *find_WFILE(FILE *f);

#endif // STDIOWRAP_INTERNAL_H__
