#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  FILE *outf, *inf, *dbf, *logf;
  char c[200], d[200];
  int i;

  struct timespec ts = {.tv_sec=0, .tv_nsec=10000000};

  outf = stdiowrap_fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");
  dbf  = stdiowrap_fopen(argv[3], "r");
  logf = NULL;

  // Open optional log file
  if (argc == 5) {
    logf = stdiowrap_fopen(argv[4], "w");
    if (!logf) {
      fprintf(stderr, "Could not open file(s)\n");
      exit(EXIT_FAILURE);
    }
  }

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

    if (logf) {
      stdiowrap_fprintf(logf, "Logged line %d\n", i);
    }
    //nanosleep(&ts, NULL);
  }

  if (logf) {
    stdiowrap_fprintf(logf, "Wrote %d lines.\nDone.\n", i);
    stdiowrap_fclose(logf);
  }
  stdiowrap_fclose(inf);
  stdiowrap_fclose(outf);
  stdiowrap_fclose(dbf);

  return EXIT_SUCCESS;
}
