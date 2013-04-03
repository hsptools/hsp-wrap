#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  FILE *outf, *inf;
  char c[40];
  int i;

  outf = fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");

  if (!outf || !inf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; stdiowrap_fgets(c, sizeof(c), inf) != NULL; ++i) {
    sleep(1);  
    fputs(c, outf);
  }

  stdiowrap_fclose(inf);
  fclose(outf);

  fprintf(stderr, "Wrote %d lines\n. Done.", i);
  return EXIT_SUCCESS;
}
