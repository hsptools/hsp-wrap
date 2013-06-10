#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  FILE *outf, *inf, *dbf;
  char c[200], d[200];
  int i;

  struct timespec ts = {.tv_sec=0, .tv_nsec=10000000};

  outf = stdiowrap_fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");
  dbf  = stdiowrap_fopen(argv[3], "r");

  if (!outf || !inf || !dbf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  for (i=0; stdiowrap_fgets(c, sizeof(c), inf) != NULL; ++i) {
    if (stdiowrap_fgets(d, sizeof(d), dbf) == 0) {
	stdiowrap_rewind(dbf);
        stdiowrap_fgets(d, sizeof(d), dbf);
    } 
    stdiowrap_fputs(c, outf);
    stdiowrap_fputs(d, outf);

    //nanosleep(&ts, NULL);
  }

  //fprintf(stderr, "Wrote %d lines\n. Done.", i);
  stdiowrap_fclose(inf);
  stdiowrap_fclose(outf);
  stdiowrap_fclose(dbf);

  return EXIT_SUCCESS;
}
