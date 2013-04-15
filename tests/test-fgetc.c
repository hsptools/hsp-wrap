#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  int c, i;
  FILE *outf, *inf;

  outf = stdiowrap_fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");

  if (!outf || !inf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; (c = stdiowrap_fgetc(inf)) != EOF; ++i) {
    stdiowrap_fputc(c, outf);
  }

  stdiowrap_fclose(inf);
  stdiowrap_fclose(outf);

  fprintf(stderr, "Wrote %d bytes\n. Done.", i);
  return EXIT_SUCCESS;
}
