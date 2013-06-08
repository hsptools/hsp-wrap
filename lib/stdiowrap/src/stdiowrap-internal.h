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
  FILE   *stream;    // FILE pointer that is mapped
  char   *name;      // Path of the file
  char   *data;      // Pointer to memory holding contents of file
  size_t  size;      // Size of the data segment in memory
  size_t  tsize;     // Size of the container holding the data segment
  size_t *psize;     // Pointer to back-end's size so it can be updated
  char   *pos;       // Current cursor position in data segment

  off_t   offset;    // Offset of current data-segment from beginning of file

  int     is_stream; // Is this a stream (can we poll for more data)?
};

struct WFILEDES {
  int     fd;       // Virtual file descriptor
  char   *data;     // mmaped data
  int     ref_cnt;  // Reference count (how many mmaps are open)
  int     fte_idx;
  struct WFILEDES *next; // Next file descriptor
};


static void   parse_env (void *dest, char *name, const char *format);
static void   init_SHM ();
static struct file_table_entry *find_file_entry (const char *name, int *idx);

static int    fill_WFILE_data_SHM (struct WFILE *wf);
static void   free_WFILE_data_SHM (struct WFILE *wf);
static struct WFILE *new_WFILE (const char *fn);
static void   destroy_WFILE (struct WFILE *wf);
static int    include_WFILE (struct WFILE *wf);
static void   exclude_WFILE (struct WFILE *wf);
static struct WFILE *find_WFILE (FILE *f);

struct WFILEDES *find_WFILEDES_path (const char *path);
struct WFILEDES *find_WFILEDES_fd (int filedes);

static void   set_status (enum process_state st);
static enum   process_cmd get_command ();
static void   update_wfiles_from_flush (struct WFILE *exclude);
static int    wait_eod (struct WFILE *wf);
static int    wait_nospace (struct WFILE *wf);


#endif // STDIOWRAP_INTERNAL_H__
