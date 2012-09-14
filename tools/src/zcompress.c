#define _XOPEN_SOURCE 500
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "cliutils/version.h"
#include "ioutils/ioutils.h"
#include "zutils/zutils.h"

#define PACKAGE_NAME "HSP ZCompress"
#define AUTHORS "Paul Giblock"
#define VERSION "0.1.0"

// Flag set by '--ignore-errors'
static int   ignore_errors_flag;
// Flag set by '--force', overwrite output files, if exist
static int   force_flag;
// Name of output file
static char *output_opt;

// Name of program (zcompress)
static char *program_name;

// Output file
FILE *out;

static void
print_usage ()
{
  printf("Usage: %s [OPTION]... [FILENAME]...\n", program_name);
  puts("\
HSP ZCompress is a simplified utility for creating a compressed input file\n\
In general, ZCompress will take its input (either standard input or a file)\n\
and compress the data as a single compressed block.  The output is then\n\
written to standard output or a file if specified.  Note, that this will\n\
produce only a single compresed input block and is insufficient for load\n\
balancing.  One either needs to concatenate the output from multiple runs,\n\
or specify multiple input filenames, which will create a block per file.\n\n\
Options:\n\
  -f, --force         forcibly overwrite output file if it already exists\n\
  -i, --ignore-errors print a warning instead of failing if errors occur\n\
  -o, --output=FILE   write to output file instead of standard output\n\
      --help          display this help and exit\n\
      --version       display version information and exit\n\n\
Report bugs to <brekapal@utk.edu>\
");
}


int
main(int argc, char **argv)
{
  FILE       *f;
  int         c;

  program_name = argv[0];

  while (1) {
    static struct option long_options[] =
    {
      {"output",        required_argument, 0, 'o'},
      {"force",         no_argument,       0, 'f'},
      {"help",          no_argument,       0, '#'},
      {"version",       no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "o:f",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'o':
	output_opt = optarg;
	break;

      case 'f':
	force_flag = 1;
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

  // Open output file (or stdout)
  if (output_opt) {
    out = fdopen(ioutil_open_w(output_opt, force_flag, 0), "w");
  } else {
    out = stdout;
  }

  // Act according to number of parameters
  if (optind == argc) {
    zutil_compress_stream(out, stdin, Z_DEFAULT_COMPRESSION);
  } else while (optind != argc) {
    if (!(f = fopen(argv[optind], "r"))) {
      if (ignore_errors_flag) {
	error(0, errno, "%s: open failed, skipping", argv[optind]);
	return 0;
      } else {
	error(EXIT_FAILURE, errno, "%s: open failed", argv[optind]);
      }
    }

    zutil_compress_stream(out, f, Z_DEFAULT_COMPRESSION);
    optind++;
  }

  return EXIT_SUCCESS;
}
