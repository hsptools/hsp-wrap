#include <stdio.h>
#include "zutils.h"


int
main(int argc, char **argv) {
  FILE          *f;
  unsigned long  total=0;
  int            i,blks;

  // Check command line args
  if (argc < 2) {
    fprintf(stderr,"zstat usage:\n\t zstat <infile0> [infile_1] ... [infile_n]\n");
    return 1;
  }
  
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
  return 0;
}
