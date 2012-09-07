#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv) {
  FILE     *f;

  int       c, printing;
  char      ch, lastch;
  long      seqs, samples;
  double    ratio, acc;

  // Check command line args
  if (argc < 2) {
    fprintf(stderr,"samplefasta usage:\n\t samplefasta <fasta> <numsamples>\n");
    return 1;
  }

  sscanf(argv[2], "%ld", &samples);

  if (!(f = fopen(argv[1],"r"))) {
    fprintf(stderr, "samplefasta: could not open \"%s\".  Exiting.\n", argv[2]);
    exit(-1);
  }

  // Count seqs
  seqs = 0;
  lastch = '\n';
  while ((c = fgetc(f)) != EOF) {
    ch = (char)c & 0xFF;

    if (ch == '>' && lastch == '\n') {
      seqs++;
    }

    lastch = ch;
  }

  // Back to beginning
  fseek(f, 0, SEEK_SET);
  ratio = ((double)samples)/seqs;

  // Print seqs
  acc      = 1.0;
  lastch   = '\n';
  printing = 0;
  while ((c = fgetc(f)) != EOF) {
    ch = (char)c & 0xFF;

    if (ch == '>' && lastch == '\n') {
      if (acc >= 1.0) {
	printing = 1;
	acc -= 1.0;
      } else {
	printing = 0;
      }
      acc += ratio;
    }

    if (printing) {
      fputc(ch, stdout);
    }

    lastch = ch;
  }

  fclose(f);

  return 0;
}
