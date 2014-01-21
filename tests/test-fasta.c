#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "stdiowrap/stdiowrap.h"

int
main(int argc, char **argv)
{
  int c, i, j;
  FILE *outf, *inf;

  struct timespec ts = {.tv_sec=0, .tv_nsec=0};

  srand(time(NULL) + getpid());

  outf = stdiowrap_fopen(argv[1], "w");
  inf  = stdiowrap_fopen(argv[2], "r");

  if (!outf || !inf) {
    fprintf(stderr, "Could not open file(s)\n");
    exit(EXIT_FAILURE);
  }

  // j = 0 on new sequence (accept the '>' character)
  j=0;
  for (i=0; (c = stdiowrap_fgetc(inf)) != EOF; ++i) {
    if (j>0 && c=='>') {
      // New sequence (pretend to process)
      stdiowrap_ungetc(c, inf);
      ts.tv_sec  = 10 + rand()%5;
      ts.tv_nsec = rand()%1000000;
      nanosleep(&ts, NULL);
      stdiowrap_fprintf(outf, "\nDone with sequence of %d bytes.\n", j);
      printf("\nDone with sequence of %d bytes.\n", j);
      j=0;
    } else {
      stdiowrap_fputc(c, outf);
      putchar(c);
      j++;
    }
  }

  // Pretend to process last sequence
  nanosleep(&ts, NULL);

  stdiowrap_fclose(inf);
  stdiowrap_fclose(outf);

  fprintf(stderr, "Wrote %d bytes\n. Done.", i);
  printf("Wrote %d bytes\n. Done.", i);
  return EXIT_SUCCESS;
}
