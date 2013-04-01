#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  FILE *outf, *inf;
  char c[40];
  int i;

  //outf = fopen(argv[1], "w");
  inf  = fopen(argv[2], "r");

  if (!outf || !inf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; fgets(c, sizeof(c), inf) != NULL; ++i) {
    //fputs(c, outf);
  }

  fclose(inf);
  //fclose(outf);

  fprintf(stderr, "Wrote %d lines\n. Done.", i);
  return EXIT_SUCCESS;
}
