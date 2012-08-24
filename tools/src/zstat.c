#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zutils/zutils.h"


int
main(int argc, char **argv) {
  FILE          *f;
  unsigned long  total=0;
  int            i,blks;
  long           bsz;

  // Check command line args
  if (argc < 2) {
    fprintf(stderr,"zstat usage:\n\t zstat [-x] <infile0> [infile_1] ... [infile_n]\n");
    return 1;
  }

  if (!strcmp(argv[1], "-x")) {
    // Extended stats for one file
    if (!(f = fopen(argv[2],"r"))) {
      fprintf(stderr, "zstat: could not open \"%s\".  Exiting.\n", argv[2]);
      exit(-1);
    }
    // Display lengths
    while (zutil_blk_iter(f, &bsz)) {
      printf("%ld\n", bsz);
    }
    // Close file
    fclose(f);
  } else {
    // Stats for each file
    for (i=1; i<argc; i++) {
      if (!(f = fopen(argv[i],"r"))) {
        fprintf(stderr, "zstat: could not open \"%s\".  Skipping.\n", argv[i]);
        continue;
      }
      // Count blocks
      if (zutil_blk_cnt(f, &blks) != Z_OK) {
        fprintf(stderr, "zstat: error while reading \"%s\".  Skipping.\n", argv[i]);
      } else {
        printf("%s\t%d\n", argv[i], blks);
      }
      // Close file, update counts
      fclose(f);
      total += blks;
    }

    // Print total
    printf("%lu\n", total);
  }

  return 0;
}
