#define _XOPEN_SOURCE 500
#include <errno.h>
#include <error.h>
#include <ftw.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "strutils.h"
#include "version.h"
#include "zutils.h"

#define PACKAGE_NAME "HSP Gather"
#define AUTHORS "Paul Giblock"
#define VERSION "1.0.0"

// Filename of output files (query)
static char *fn_base;
// Flag set by '--ignore-errors'
static int   ignore_errors_flag;
// Directory containing result files
static char *directory_opt;
// Name of output file
static char *output_opt;

// Name of program (recover)
static char *program_name;

static void
print_usage ()
{
  printf("Usage: gather %s [OPTION]... FILENAME\n", program_name);
  puts("\
HSP Gather is the standard way to coalesce the output results of a\n\
completed HSP job.  The resulting output files with the name FILENAME are\n\
decompressed and concatenated to standard output, unless the -o option\n\
is specified.\n\n\
Options:\n\
  -d, --directory=DIR the directory to use when searching for result files\n\
  -i, --ignore-errors print a warning instead of failing if errors occur\n\
  -o, --output=FILE   write to output file instead of standard output\n\
      --help          display this help and exit\n\
      --version       display version information and exit\n\n\
Report bugs to <pgiblock@utk.edu>\
");
}


static int
print_file (const char *fpath)
{
  FILE *f;
  int blks;

  if (!(f = fopen(fpath, "r"))) {
    if (ignore_errors_flag) {
      error(0, errno, "%s: open failed, skipping", fpath);
      return 0;
    } else {
      error(EXIT_FAILURE, errno, "%s: open failed", fpath);
    }
  }

  // Decompress the file
  if (zutil_inf(stdout, f, &blks) != Z_OK) {
    if (ignore_errors_flag) {
      error(0, errno, "%s: extraction failed, skipping", fpath);
      return 0;
    } else {
      error(EXIT_FAILURE, errno, "%s: extraction failed", fpath);
    }
  }
  return 1;
}


static int
peek_dir(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
  if (tflag == FTW_F && str_ends_with(fpath+ftwbuf->base, fn_base)) {
    print_file(fpath);
  }
  return 0;
}


int
main(int argc, char **argv)
{
  int c;
  struct stat st;

  program_name = argv[0];

  // Option defaults
  directory_opt = ".";

  while (1) {
    static struct option long_options[] =
    {
      {"directory",     required_argument, 0, 'd'},
      {"output",        required_argument, 0, 'o'},
      {"ignore-errors", no_argument,       0, 'i'},
      {"help",          no_argument,       0, '#'},
      {"version",       no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "d:o:i",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'd':
	directory_opt = optarg;
	break;

      case 'o':
	output_opt = optarg;
	break;

      case 'i':
	ignore_errors_flag = 1;
	break;

      case '?':
	printf("Try '%s --help' for more information.\n", program_name);
	exit(EXIT_FAILURE);
	break;

      case '#':
	print_usage();
	exit(EXIT_SUCCESS);
	break;

      case_GETOPT_VERSION;

      default:
	abort();
    }
  }

  // Verify number of parameters
  if (optind == argc) {
    error(EXIT_FAILURE, 0, "Missing input filename");
  } else if (optind != argc-1) {
    error(EXIT_FAILURE, 0, "Too many parameters");
  }

  // Path to compressed file
  fn_base = argv[optind];

  // Directory exists?
  if (!stat(directory_opt, &st)) {
    if (S_ISDIR(st.st_mode)) {
      // Do it
      nftw(directory_opt, peek_dir, 100, 0);
      return EXIT_SUCCESS;
    }
  }  

  // If we got here, finding the directory failed
  error(EXIT_FAILURE, 0, "%s: directory doesn't exist", directory_opt);
  return EXIT_FAILURE;
}
