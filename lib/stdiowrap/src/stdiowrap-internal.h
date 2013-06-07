#ifndef STDIOWRAP_INTERNAL_H__
#define STDIOWRAP_INTERNAL_H__

#include <stdio.h>
#include <stdarg.h>

#include <hsp/process_control.h>

////////////////////////////////////////////////////////////////////////////////
// Internal to stdiowrap.c
////////////////////////////////////////////////////////////////////////////////


/**
 * Structure to hold file/stream information
 */
struct WFILE {
  FILE          *stream;  // FILE pointer that is mapped
  char          *name;    // Path of the file
  unsigned char *data;    // Pointer to memory holding contents of file
  size_t         size;    // Size of the data segment in memory
  size_t         tsize;   // Size of the container holding the data segment
  size_t        *psize;   // Pointer to back-end's size so it can be updated
  unsigned char *pos;     // Current cursor position in data segment

  off_t          offset;  // Offset of current data-segment from beginning of file

  int            is_stream; // Is this a stream (can we poll for more data)?
};


static struct WFILE *new_WFILE (const char *fn);
static void   destroy_WFILE (struct WFILE *wf);
static int    include_WFILE (struct WFILE *wf);
static void   exclude_WFILE (struct WFILE *wf);
static struct WFILE *find_WFILE (FILE *f);

#endif // STDIOWRAP_INTERNAL_H__
