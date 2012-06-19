// I'll keep the comments below just as evidence that there are no
// copyright issues with me using or modifying this.  It was released
// into the public domain.         ~Aaron Vose. 

/* zpipe.c: example of proper use of zlib's inflate() and deflate()
   Not copyrighted -- provided to the public domain
   Version 1.4  11 December 2005  Mark Adler */

/* Version history:
   1.0  30 Oct 2004  First version
   1.1   8 Nov 2004  Add void casting for unused return values
                     Use switch statement for inflate() return values
   1.2   9 Nov 2004  Add assertions to document zlib guarantees
   1.3   6 Apr 2005  Remove incorrect assertion in inf()
   1.4  11 Dec 2005  Add hack to avoid MSDOS end-of-line conversions
                     Avoid some compiler warnings for input and output buffers
   1.AV              Hacked to pieces by Aaron Vose; mostly to add input
                     support from files, and to allow the extraction of
		     concatenated compressed blocks.
*/

#include <stdio.h>
#include "zutils.h"


int
main(int argc, char **argv) {
  FILE          *f;
  unsigned long  blocks=0;
  int            i,blks;


  // Check command line args
  if (argc < 2) {
    fprintf(stderr,"zextract usage:\n\t zextract <infile0> [infile_1] ... [infile_n]\n");
    return 1;
  }
  
  // Extract all input files and concatenate to stdout
  for (i=1; i<argc; i++) {
    if (!(f = fopen(argv[i],"r"))) {
      fprintf(stderr, "zextract: could not open \"%s\".  Skipping.\n", argv[i]);
      continue;
    }
    // Decompress the file
    if (zutil_inf(stdout, f, &blks) != Z_OK) {
      fprintf(stderr, "zextract: error while extracting \"%s\".  Skipping.\n", argv[i]);
    }
    // Close file, update counts
    fclose(f);
    blocks += blks;
  }

  // Success
  fprintf(stderr,"zextract: extracted %lu blocks from %d files.\n",blocks,argc-1);
  return 0;
}
