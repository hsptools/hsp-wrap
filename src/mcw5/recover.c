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

// Initial size of the history buffer, in entries
#define INITIAL_HISTORY_SIZE 1024

// Flag set by '--verbose'
static int   verbose_flag;
// Name of program (recover)
static char *program_name;

// Block statuses, indexed by Block-id
char  *history;
// Size of the history buffer
size_t history_size;
// Maximum block-id encountered so far
int    max_block_id;


static void
print_version ()
{
  puts("HSP Recover 1.0.0");
  puts("Copyright (C) 2012 National Institute for Computational Sciences");
  puts("License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>");
  puts("This is free software: you are free to change and redistribute it.");
  puts("There is NO WARRANTY, to the extent permitted by law.\n");
  puts("Written by Paul Giblock.");
}


static void
print_usage ()
{
  printf("Usage: %s [OPTION]... DIRECTORY\n", program_name);
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
    error (EXIT_FAILURE, errno, "open failed: %s", fpath);
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

  while (1) {
    static struct option long_options[] =
    {
      // These options set a flag.
      {"verbose", no_argument,       &verbose_flag, 1},
      {"brief",   no_argument,       &verbose_flag, 0},
      // These options don't set a flag. We distinguish them by their indices.
      {"add",     no_argument,       0, 'a'},
      {"delete",  required_argument, 0, 'd'},
      {"help",    no_argument,       0, '#'},
      {"version", no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "ad:",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 0:
	// If this option set a flag, do nothing else now.
	if (long_options[option_index].flag != 0) {
	  break;
	}
	printf("option %s", long_options[option_index].name);
	if (optarg)
	  printf(" with arg %s", optarg);
	printf("\n");
	break;

      case 'a':
	puts("option -a\n");
	break;

      case 'd':
	printf("option -d with value `%s'\n", optarg);
	break;

      case '?':
	printf("Try '%s --help' for more information.", program_name);
	break;

      case '#':
	print_usage();
	exit(EXIT_SUCCESS);
	break;

      case '^':
	print_version();
	exit(EXIT_SUCCESS);
	break;

      default:
	abort();
    }
  }

  // Path to compressed file
  char *zfile_path  = argv[optind];
  // Path to directory containing 'log' files
  char *log_dirname = argv[optind+1];

  // The actual compressed blocks (input)
  char  *blocks;
  // Size of the compressed blocks mmap region
  size_t blocks_size;
  // Output files
  FILE *resume_file;
  FILE *failed_file;
  FILE *successful_file;

  // Initial history buffer - resizes later if needed
  history_size = INITIAL_HISTORY_SIZE;
  max_block_id = 0;
  history = malloc(history_size);
  if (!history) {
    error(EXIT_FAILURE, errno, "memory exhausted");
  }
  memset(history, 0, history_size);

  // Open output files
  if (!(resume_file = fopen("recovery-resume.cmol2", "w"))) {
    error (EXIT_FAILURE, errno, "open failed: %s", "recovery-resume.cmol2");
  }
  if (!(failed_file = fopen("recovery-failed.cmol2", "w"))) {
    error (EXIT_FAILURE, errno, "open failed: %s", "recovery-failed.cmol2");
  }
  if (!(successful_file = fopen("recovery-successful.cmol2", "w"))) {
    error (EXIT_FAILURE, errno, "open failed: %s", "recovery-successful.cmol2");
  }

  // Open compressed file
  // TODO: Make utility function for mmaping. 
  {
    int zfile;
    struct stat zfile_st;
   
    zfile = open(zfile_path, O_RDONLY);
    if (zfile < 0) {
      error (EXIT_FAILURE, errno, "open failed: %s", zfile_path);
    }

    if (fstat(zfile, &zfile_st) < 0) {
      close(zfile);
      error (EXIT_FAILURE, errno, "stat failed: %s", zfile_path);
    }

    blocks_size = zfile_st.st_size;
    blocks = (char*)mmap(NULL, blocks_size,
			 PROT_READ, MAP_PRIVATE,
			 zfile, 0);
    close(zfile);

    if (blocks == MAP_FAILED) {
      error (EXIT_FAILURE, errno, "mmap failed: %s", zfile_path);
    }
  }

  // Load log entries
  // TODO --directory option
  nftw(log_dirname, peek_dir, 10, 0);

  // Actually copy blocks around
  {
    char *i, status;
    long block_size;
    unsigned int block_id;

    if (verbose_flag) {
      printf("Copying blocks...\n");
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
	  fwrite(i, 1, block_size, successful_file);
	  break;
	case 'E':
	  fwrite(i, 1, block_size, failed_file);
	  break;
	case 0:
	  fwrite(i, 1, block_size, resume_file);
	  break;
	default:
	  if (isprint(status)) {
	    fprintf(stderr, "Unknown block status: %d (%c)\n", status, status);
	  } else {
	    fprintf(stderr, "Unknown block status: %d\n", status);
	  }
	  abort();
      }
    }

    if (verbose_flag) {
      printf("Done.\n");
    }
  }

  // Cleanup
  fclose(resume_file);
  fclose(failed_file);
  fclose(successful_file);

  munmap(blocks, blocks_size);
  
  free(history);

  // TODO: Display stats

  exit(0);
}
