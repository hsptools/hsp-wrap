#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ioutils.h"
#include "version.h"

#define PACKAGE_NAME "HSP ZSplit"
#define AUTHORS "Paul Giblock"
#define VERSION "0.1.0"

// Flag set by '--verbose'
static int   verbose_flag;
// Flag set by '--force', overwrite output files, if exist
static int   force_flag;
// Size of output files in block count (max)
static unsigned int blocks_opt;
// Size of output files in bytes (min)
static size_t bytes_opt;
// Prefix for output files
static char *prefix_opt;

// Name of program (recover)
static char *program_name;


static void
print_usage ()
{
  printf("Usage: %s [OPTION]... INPUT_FILE\n", program_name);
  puts("\
HSP ZSplit splits an HSP compressed block file into multiple files. The\n\
split can occur at either block or byte boundaries.  The output files are\n\
all named with a common prefix, given by the --prefix option.\n\n\
Options:\n\
  -b, --blocks=COUNT  output files contain at most COUNT blocks\n\
  -c, --bytes=SIZE    output files are approximately SIZE large\n\
  -f, --force         forcibly overwrite output files if they already exist\n\
  -p, --prefix=PREFIX path prefix for output files\n\
  -v, --verbose       verbosely list files processed\n\
      --help          display this help and exit\n\
      --version       output version information and exit\n\n\
Report bugs to <pgiblock@utk.edu>\
");
}


static long
parse_int (const char *str, const char *var_name)
{
  char *endptr;
  long val;

  errno = 0;
  val = strtol(str, &endptr, 10);

  if (errno) {
    error(EXIT_FAILURE, errno, "%s: invalid integer, %s", var_name, str);
  } else if (endptr == str) {
    error(EXIT_FAILURE, 0, "%s: no digits, %s", var_name, str);
  } else if (*endptr != '\0') {
    error(EXIT_FAILURE, 0, "%s: extraneous characters, %s", var_name, endptr);
  }
  if (val <= 0) {
    error(EXIT_FAILURE, 0, "%s: must be a positive integer", var_name);
  }
  return val;
}

////////

int
main (int argc, char **argv)
{
  int c;
  
  program_name = argv[0];

  // Option defaults
  prefix_opt    = NULL;

  while (1) {
    static struct option long_options[] =
    {
      {"verbose",   no_argument,      &verbose_flag, 1},
      {"brief",     no_argument,      &verbose_flag, 0},
      {"blocks",    required_argument, 0, 'b'},
      {"bytes",     required_argument, 0, 'c'},
      {"force",     no_argument,       0, 'f'},
      {"prefix",    required_argument, 0, 'p'},
      {"help",      no_argument,       0, '#'},
      {"version",   no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "b:c:fp:",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 0:
	break;

      case 'b':
	blocks_opt = parse_int(optarg, "block count");
	break;

      case 'c':
	bytes_opt = parse_int(optarg, "byte count");
	error(EXIT_FAILURE, 0, "--bytes option currently not supported");
	break;

      case 'f':
	force_flag = 1;
	break;

      case 'p':
	prefix_opt = optarg;
	break;

      // TODO: Macroize "invalid option" case
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

  // Required
  if (! (blocks_opt || bytes_opt)
      | (blocks_opt && bytes_opt)) {
    error(EXIT_FAILURE, 0, "Exactly one of --bytes or --blocks is required");
  }

  // Verify number of parameters
  if (optind == argc) {
    error(EXIT_FAILURE, 0, "Missing input filename");
  } else if (optind != argc-1) {
    error(EXIT_FAILURE, 0, "Too many parameters");
  }

  // Path to compressed file
  char *zfile_path  = argv[optind];
  // Default to input filename as prefix
  if (!prefix_opt) {
    prefix_opt = zfile_path;
  }

  // The actual compressed blocks (input)
  char  *blocks;
  // Size of the compressed blocks mmap region
  off_t blocks_size;

  // Open compressed file
  blocks = ioutil_mmap_r(zfile_path, &blocks_size);

  // Actually copy blocks around
  {
    char *i, *start;
    long block_size;
    unsigned int file_id, block_id;
    // Output file
    int output_fd;
    // Output filename
    size_t fn_buf_size = strlen(prefix_opt) + 4;
    char *fn_buf = malloc(fn_buf_size);

    // Outer loop: Loop over files
    i = blocks;
    for (file_id = 0; i < blocks+blocks_size; ++file_id) {
      // Open output file
      snprintf(fn_buf, fn_buf_size, "%s%02d", prefix_opt, file_id);
      if (verbose_flag) {
	puts(fn_buf);
      }
      output_fd = ioutil_open_w(fn_buf, force_flag, 0);

      // Inner loop: Loop over blocks
      for (start = i, block_id = 0;
	   i < blocks+blocks_size && block_id < blocks_opt;
	   i += block_size, ++block_id) {

	// Grab block size, include length header
	block_size = *((long*)i) + sizeof(long);
      }

      // We now have a region [start..i], write it to file
      if (write(output_fd, start, i-start) == -1) {
	error (EXIT_FAILURE, errno, "%s: write failed", fn_buf);
      }

      close(output_fd);
    }

    free(fn_buf);
  }

  // Done
  munmap(blocks, blocks_size);
  exit(0);
}
