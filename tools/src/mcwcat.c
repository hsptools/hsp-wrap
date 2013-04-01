#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"


int
main(int argc, char **argv)
{
  int c, i, j;
  FILE *outf, *inf;

  for (i = 1; i < argc; ++i) {
    fprintf(stderr, "argv[%d] = %s\n", i, argv[i]);
  }
  outf = stdiowrap_fopen(argv[1], "w");
  fprintf(stderr, "outf = %p\n", outf);

  stdiowrap_fputs("======================\n", outf);
  for (i = 1; i < argc; ++i) {
    stdiowrap_fprintf(outf, "argv[%d] = %s\n", i, argv[i]);
  } 
  stdiowrap_fputs("======================\n", outf);
  for (i = 2; i < argc; ++i) {
    inf = stdiowrap_fopen(argv[i], "r");
    fprintf(stderr, "inf = %p\n", inf);
    for (j=0; (c = stdiowrap_fgetc(inf)) != EOF; ++j) {
      fprintf(stderr,".\n");
      stdiowrap_fputc(c, outf);
    }
    stdiowrap_fclose(inf);
    stdiowrap_fputs("-----------\n", outf);
    fprintf(stderr, "File %d. Wrote %d bytes\n", i, j);
  }
  stdiowrap_fputs("======================\n", outf);

  //sleep(10);
  stdiowrap_fclose(outf);

  return EXIT_SUCCESS;
}
