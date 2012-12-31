#ifndef STDIOWRAP_INTERNAL_H__
#define STDIOWRAP_INTERNAL_H__

#include <semaphore.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
// Internal to stdiowrap.c
////////////////////////////////////////////////////////////////////////////////

// Structure to hold file/stream information
typedef struct {
  FILE          *stream;  // FILE pointer that is mapped
  char          *name;    // Path of the file
  unsigned char *data;    // Pointer to memory holding contents of file
  size_t         size;    // Size of the data segment in memory
  size_t         tsize;   // Size of the container holding the data segment
  size_t        *psize;   // Pointer to back-end's size so it can be updated
  unsigned char *pos;     // Current cursor position in data segment
} WFILE;

#ifdef BACKEND_SHM
// Holds info on one SHM <-> file mapping

typedef uint16_t wid_t;

/**
 * File description for a virtual file
 */
typedef struct {
  // Public
  size_t shm_size;            // Size of the shm (needed for detach)
  int    shm_fd;              // File descriptor of the shm

  wid_t  wid;                 // Worker ID owning this file, -1 if shared TODO: N:M mapping
  size_t size;                // Size of the actual file data within the SHM
  char   name[MAX_FILE_PATH]; // Virtual name (path) of the file 
  // Private (TODO: Move outside of file_table)
  char  *shm;                 // Pointer to shared data
} file_table_entry_t;

/**
 * Table of all available virtual files
 */
typedef struct {
  int                nfiles;
  file_table_entry_t file[MAX_DB_FILES];
} file_table_t;

/**
 * Shared process control structure.  Layout of SHM.
 */
typedef struct {
  int   nprocesses;               // Number of processes
  sem_t sem_empty;                // Signal exhausted data to parent process
  sem_t sem_avail[MAX_PROCESSES]; // Signal available data to child processes

  file_table_t ft;                // File descriptors
} process_control_t;

#endif

static WFILE *new_WFILE(const char *fn);
static void   destroy_WFILE(WFILE *wf);
static void   include_WFILE(WFILE *wf);
static void   exclude_WFILE(WFILE *wf);
static WFILE *find_WFILE(FILE *f);

#endif // STDIOWRAP_INTERNAL_H__
