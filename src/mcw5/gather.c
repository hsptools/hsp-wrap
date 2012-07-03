#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>

#include "strutils.h"
#include "zutils.h"

#define NARGS 2

// Filename of output files (query)
char *fn_base;

static void
print_usage(FILE *f) {
  fprintf(f, "\nUsage: gather DIR FILENAME\n\n");
  fprintf(f, "Gather and decompress all results for file FILENAME in mcw job \n"
             "output directory, DIR, and output the results to standard output.\n\n");
  fprintf(f, "Example: gather job-foo out > foo.results\n");
}


static int
print_file(const char *fpath) {
  FILE *f;
  int blks;

  if (!(f = fopen(fpath, "r"))) {
    fprintf(stderr, "gather: could not open \"%s\".  Skipping.\n", fpath);
    return 0;
  }

  // Decompress the file
  if (zutil_inf(stdout, f, &blks) != Z_OK) {
    fprintf(stderr, "gather: error while extracting \"%s\".  Skipping.\n", fpath);
    return 0;
  }
  return 1;
}


static int
peek_dir(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
  if (tflag == FTW_F && str_ends_with(fpath+ftwbuf->base, fn_base)) {
    print_file(fpath);
  }
  return 0;
}


int
main(int argc, char **argv) {
  char *path;

  struct stat st;

  if (argc == NARGS+1) {
    // Path to job directory
    path = argv[1];
    // Filename of output files
    fn_base = argv[2];

    // Directory exists?
    if (!stat(path, &st)) {
      if (S_ISDIR(st.st_mode)) {
        // Do it
        nftw(path, peek_dir, 100, 0);
        return 0;
      }
    }  

    // If we got here, finding the directory failed
    fprintf(stderr, "gather: directory doesn't exist.\n");
    fprintf(stderr, "gather: argument must be an existing output directory.\n");
  } else if (argc < NARGS+1) {
    fprintf(stderr, "gather: too few arguments, %d required\n", NARGS);
  } else {
    fprintf(stderr, "gather: too many arguments, %d required\n", NARGS);
  }

  print_usage(stderr);
  return 100;
}
