#ifndef STDIOWRAP_STDIOWRAP_H__
#define STDIOWRAP_STDIOWRAP_H__


////////////////////////////////////////////////////////////////////////////////
// Shared
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdarg.h>

////////////////////////////////////////////////////////////////////////////////
// Interface (external to stdiowrap.c)
////////////////////////////////////////////////////////////////////////////////
#ifndef STDIOWRAP_C
#  ifdef STDIOWRAP_AUTO
#    define fopen(a,b)       stdiowrap_fopen((a),(b))
#    define fclose(a)        stdiowrap_fclose((a))
#    define fseek(a,b,c)     stdiowrap_fseek((a),(b),(c))
#    define fseeko(a,b,c)    stdiowrap_fseeko((a),(b),(c))
#    define ftell(a)         stdiowrap_ftell((a))
#    define ftello(a)        stdiowrap_ftello((a))
#    define rewind(a)        stdiowrap_rewind((a))
#    define feof(a)          stdiowrap_feof((a))
#    define fread(a,b,c,d)   stdiowrap_fread((a),(b),(c),(d))
#    define fgets(a,b,c)     stdiowrap_fgets((a),(b),(c))
#    define fgetc(a)         stdiowrap_fgetc((a))
#    define ungetc(a,b)      stdiowrap_ungetc((a),(b))
#    define fscanf(...)      stdiowrap_fscanf(__VA_ARGS__)
#    ifdef getc
#      undef getc
#    endif
#    define getc(a)          stdiowrap_getc((a))
#    define fflush(a)        stdiowrap_fflush((a))
#    define fwrite(a,b,c,d)  stdiowrap_fwrite((a),(b),(c),(d))
#    define fputs(a,b)       stdiowrap_fputs((a),(b))
#    define fputc(a,b)       stdiowrap_fputc((a),(b))
#    define fprintf(...)     stdiowrap_fprintf(__VA_ARGS__)
#    ifdef putc
#      undef putc
#    endif
#    define putc(a,b)        stdiowrap_putc((a),(b))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Open and Close
FILE   *stdiowrap_fopen  (const char *path, const char *mode);
int     stdiowrap_fclose (FILE *stream);

// Cursor positioning / File status
int     stdiowrap_fseek  (FILE *stream, long offset, int whence);
int     stdiowrap_fseeko (FILE *stream, long offset, int whence);
long    stdiowrap_ftell  (FILE *stream);
long    stdiowrap_ftello (FILE *stream);
void    stdiowrap_rewind (FILE *stream);
int     stdiowrap_feof   (FILE *stream);

// Read
size_t  stdiowrap_fread  (void *ptr, size_t size, size_t nmemb, FILE *stream);
char   *stdiowrap_fgets  (char *s, int size, FILE *stream);
int     stdiowrap_fgetc  (FILE *stream);
int     stdiowrap_getc   (FILE *stream);
int     stdiowrap_ungetc (int c, FILE *stream);
int     stdiowrap_fscanf (FILE *stream, const char *format, ...);

// Write
int     stdiowrap_fflush  (FILE *stream);
size_t  stdiowrap_fwrite  (const void *ptr, size_t size, size_t nmemb, FILE *stream);
int     stdiowrap_fputs   (const char *ptr, FILE *stream);
int     stdiowrap_fputc   (int c, FILE *stream);
int     stdiowrap_putc    (int c, FILE *stream);
int     stdiowrap_fprintf (FILE *stream, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // STDIOWRAP_STDIOWRAP_H__

