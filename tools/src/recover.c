#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <ftw.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cliutils/version.h"
#include "ioutils/ioutils.h"

#define PACKAGE_NAME "HSP Recover"
#define AUTHORS "Paul Giblock"
#define VERSION "1.0.0"

// Initial size of the history buffer, in entries
#define INITIAL_HISTORY_SIZE 1024

// Flag set by '--verbose'
static int   verbose_flag;
// Flag set by '--dry-run', do we only collect stats
static int   dry_run_flag;
// Flag set by '--stat', display statistics
static int   stat_flag;
// Flag set by '--append', append results to output files, if exist
static int   append_flag;
// Flag set by '--force', overwrite output files, if exist
static int   force_flag;
static int   success_flag;
// Directory containing log files
static char *directory_opt;
// Prefix for output files
static char *prefix_opt;

// Name of program (recover)
static char *program_name;

// Block statuses, indexed by Block-id
char  *history;
// Size of the history buffer
size_t history_size;
// Maximum block-id encountered so far
unsigned int max_block_id;


static void
print_usage ()
{
  printf("Usage: %s [OPTION]... INPUT_FILE\n", program_name);
  puts("\
HSP Recover aids in the resumption of a failed or incomplete job by creating\n\
a new input file appropriate for resubmission.  At the very least, a directory\n\
containing log files and the the original compressed input file is required.\n\n\
Options:\n\
  -a, --append        append to output files if they already exist\n\
  -d, --directory=DIR the directory to use when searching for log files\n\
  -n, --dry-run       don't actually write output files; just print them\n\
  -f, --force         forcibly overwrite output files if they already exist\n\
  -p, --prefix=PREFIX path prefix for output files\n\
  -s, --stat          show stats after processing files\n\
  -v, --verbose       verbosely list files processed\n\
      --help          display this help and exit\n\
      --version       output version information and exit\n\n\
Report bugs to <brekapal@utk.edu>\
");
}

// Die with error about log line

static void
die_line (int errnum, const char *fpath, unsigned int line_number)
{
  error(EXIT_FAILURE, errnum, "%s: %u: improperly formatted log file",
        fpath, line_number);
}

// Read a single log file

static int
read_log (const char *fpath)
{
  FILE *log_file;
  char *line;
  unsigned int line_number;
  size_t line_buffer_size;

  if (verbose_flag) {
    printf("Found logfile: %s\n", fpath);
  }

  log_file = fopen(fpath, "rt");
  if (!log_file) {
    error (EXIT_FAILURE, errno, "%s: open failed", fpath);
  }

  line_number = 0;
  line = NULL;
  line_buffer_size = 0;
  do {
    ssize_t  line_length;
    unsigned int block_id;

    // Increment line number, and check for roll-over
    ++line_number;
    if (line_number == 0) {
      error (EXIT_FAILURE, 0, "%s: too many checksum lines", fpath);
    }

    line_length = getline (&line, &line_buffer_size, log_file);
    if (line_length <= 0)
      break;

    // Ignore comment lines, which begin with a '#' character
    if (line[0] == '#') {
      continue;
    }

    // Parse line while checking for malformed entries
    if (line[0] == 'S') {
      if (sscanf(line+1, "\t%u", &block_id) != 1) {
	die_line (errno, fpath, line_number);
      }
    } else if (line[0] == 'E') {
      if (sscanf(line+1, "%*s\t%u", &block_id) != 1) {
	die_line (errno, fpath, line_number);
      }
    } else {
      die_line (0, fpath, line_number);
    }

    // Not a comment and not a malformed line, store it

    // Block-ID is out of range, resize array
    if (block_id >= history_size) {
      history = realloc(history, history_size*2);
      memset(history+history_size, 0, history_size);
      history_size*=2;
    }

    history[block_id] = line[0];

    // Record maximum block size
    if (block_id > max_block_id) {
      max_block_id = block_id;
    }
  } while (!feof(log_file));

  // Done with log
  free(line);
  fclose(log_file);
  return 0;
}

// Visitor for entries in directory structure, read any log files

static int
peek_dir (const char *fpath, const struct stat *sb,
          int tflag, struct FTW *ftwbuf)
{
  if (tflag == FTW_F && strcmp(fpath+ftwbuf->base, "log") == 0) {
    read_log(fpath);
  }
  return 0;
}


////////

int
main (int argc, char **argv)
{
  int c;
  
  program_name = argv[0];

  // Option defaults
  directory_opt = ".";
  prefix_opt    = "recovery-";

  while (1) {
    static struct option long_options[] =
    {
      {"verbose",   no_argument,      &verbose_flag, 1},
      {"brief",     no_argument,      &verbose_flag, 0},
      {"append",    no_argument,       0, 'a'},
      {"dry-run",   no_argument,       0, 'n'},
      {"force",     no_argument,       0, 'f'},
      {"directory", required_argument, 0, 'd'},
      {"prefix",    required_argument, 0, 'p'},
      {"stat",      no_argument,       0, 's'},
      {"help",      no_argument,       0, '#'},
      {"version",   no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "anfd:p:s",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 0:
	break;

      case 'a':
	append_flag = 1;
	break;

      case 'd':
	directory_opt = optarg;
	break;

      case 'n':
	dry_run_flag = 1;
	break;

      case 'f':
	force_flag = 1;
	break;

      case 'p':
	prefix_opt = optarg;
	break;

      case 's':
	stat_flag = 1;
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
  char *zfile_path  = argv[optind];

  // The actual compressed blocks (input)
  char  *blocks;
  // Size of the compressed blocks mmap region
  off_t  blocks_size;
  // Output files
  int resume_fd;
  int failed_fd;
  int successful_fd;
  // Counts
  unsigned int nresume, nfailed, nsuccessful;

  // Initial history buffer - resizes later if needed
  history_size = INITIAL_HISTORY_SIZE;
  max_block_id = 0;
  history = malloc(history_size);
  if (!history) {
    error(EXIT_FAILURE, errno, "memory exhausted");
  }
  memset(history, 0, history_size);

  // Open output files
  if (!dry_run_flag) {
    size_t fn_buf_size = strlen(prefix_opt) + 20;
    char *fn_buf = malloc(fn_buf_size);

    snprintf(fn_buf, fn_buf_size, "%s-resume", prefix_opt);
    resume_fd = ioutil_open_w(fn_buf, force_flag, append_flag);

    snprintf(fn_buf, fn_buf_size, "%s-failed", prefix_opt);
    failed_fd = ioutil_open_w(fn_buf, force_flag, append_flag);
    // Success file is sort of worthless except for error checking
    if (success_flag) {
      snprintf(fn_buf, fn_buf_size, "%s-success", prefix_opt);
      successful_fd = ioutil_open_w(fn_buf, force_flag, append_flag);
    }

    free(fn_buf);
  }

  // Open compressed file
  blocks = ioutil_mmap_r(zfile_path, &blocks_size);

  // Load log entries
  nftw(directory_opt, peek_dir, 10, 0);

  nresume = nfailed = nsuccessful = 0;

  // Actually copy blocks around
  {
    char *i, status;
    long block_size;
    unsigned int block_id;

    if (verbose_flag) {
      printf("Handling input...\n");
    }
    
    // Now, Read through the compressed input
    for (i = blocks, block_id = 0;
	 i < blocks+blocks_size;
	 i += block_size, ++block_id) {
      // Grab block size, include length header
      block_size = *((long*)i) + sizeof(long);
      
      // Assume block_id out of range is resumable
      if (block_id > max_block_id) {
	status = 0;
      } else {
	status = history[block_id];
      }

      // Write block to appropriate file
      switch (status) {
	case 'S':
	  if (!dry_run_flag && success_flag) {
	    write(successful_fd, i, block_size);
	  }
	  ++nsuccessful;
	  break;
	case 'E':
	  if (!dry_run_flag) write(failed_fd, i, block_size);
	  ++nfailed;
	  break;
	case 0:
	  if (!dry_run_flag) write(resume_fd, i, block_size);
	  ++nresume;
	  break;
	default:
	  fprintf(stderr, "Unknown block status: %d\n", status);
	  abort();
      }
    }

    if (verbose_flag) {
      printf("Done.\n");
    }
  }

  // Cleanup
  if (!dry_run_flag) {
    close(resume_fd);
    close(failed_fd);
    if (success_flag) {
      close(successful_fd);
    }
  }

  munmap(blocks, blocks_size);
  
  free(history);

  // Display stats
  if (dry_run_flag || stat_flag) {
    printf("Successful: %8d\n", nsuccessful);
    printf("Failed:     %8d\n", nfailed);
    printf("Resumable:  %8d\n", nresume);
  }

  exit(0);
}
