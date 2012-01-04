#ifndef STDIOWRAP_H
#define STDIOWRAP_H


////////////////////////////////////////////////////////////////////////////////
// Shared
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdarg.h>



////////////////////////////////////////////////////////////////////////////////
// Interface (external to stdiowrap.c)
////////////////////////////////////////////////////////////////////////////////
#ifndef STDIOWRAP_C
#ifdef STDIOWRAP_AUTO
#define fopen(a,b)       stdiowrap_fopen((a),(b))
#define fclose(a)        stdiowrap_fclose((a))
#define fseek(a,b,c)     stdiowrap_fseek((a),(b),(c))
#define fseeko(a,b,c)    stdiowrap_fseeko((a),(b),(c))
#define ftell(a)         stdiowrap_ftell((a))
#define ftello(a)        stdiowrap_ftello((a))
#define rewind(a)        stdiowrap_rewind((a))
#define feof(a)          stdiowrap_feof((a))
#define fread(a,b,c,d)   stdiowrap_fread((a),(b),(c),(d))
#define fgets(a,b,c)     stdiowrap_fgets((a),(b),(c))
#define fgetc(a)         stdiowrap_fgetc((a))
#define ungetc(a,b)      stdiowrap_ungetc((a),(b))
#define fscanf(...)      stdiowrap_fscanf(__VA_ARGS__)
#ifdef getc
#undef getc
#endif
#define getc(a)          stdiowrap_getc((a))
#define fflush(a)        stdiowrap_fflush((a))
#define fwrite(a,b,c,d)  stdiowrap_fwrite((a),(b),(c),(d))
#define fprintf(...)     stdiowrap_fprintf(__VA_ARGS__)
#endif

// Open and Close
extern FILE*   stdiowrap_fopen(const char *path, const char *mode);
extern int     stdiowrap_fclose(FILE *stream);

// Cursor positioning / File status
extern int     stdiowrap_fseek(FILE *stream, long offset, int whence);
extern int     stdiowrap_fseeko(FILE *stream, /*off_t*/long offset, int whence);
extern long    stdiowrap_ftell(FILE *stream);
extern /*off_t*/long   stdiowrap_ftello(FILE *stream);
extern void    stdiowrap_rewind(FILE *stream);
extern int     stdiowrap_feof(FILE *stream);

// Read
extern size_t  stdiowrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern char*   stdiowrap_fgets(char *s, int size, FILE *stream);
extern int     stdiowrap_fgetc(FILE *stream);
extern int     stdiowrap_getc(FILE *stream);
extern int     stdiowrap_ungetc(int c, FILE *stream);
extern int     stdiowrap_fscanf(FILE *stream, const char *format, ...);

// Write
extern int     stdiowrap_fflush(FILE *stream);
extern size_t  stdiowrap_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int     stdiowrap_fprintf(FILE *stream, const char *format, ...);

#endif



////////////////////////////////////////////////////////////////////////////////
// Internal to stdiowrap.c
////////////////////////////////////////////////////////////////////////////////
#ifdef STDIOWRAP_C
// Structure to hold file/stream information
typedef struct {
  FILE          *stream;  // FILE pointer that is mapped
  char          *name;    // Path of the file
  unsigned char *data;    // Pointer to memory holding contents of file
  size_t         size;    // Size of the data segment in memory
  long           tsize;   // Size of the container holding the data segment
  long          *psize;   // Pointer to back-end's size so it can be updated
  unsigned char *pos;     // Current cursor position in data semgent
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

static WFILE* new_WFILE(const char *fn);
static void   destroy_WFILE(WFILE *wf);
static void   include_WFILE(WFILE *wf);
static void   exclude_WFILE(WFILE *wf);
static WFILE* find_WFILE(FILE *f);
#endif



#endif

