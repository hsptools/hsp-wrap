#define _XOPEN_SOURCE 500
#include <errno.h>
#include <error.h>
#include <ftw.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cliutils/version.h"
#include "ioutils/ioutils.h"
#include "strutils/strutils.h"

#define PACKAGE_NAME "HSP Gather"
#define AUTHORS "Paul Giblock"
#define VERSION "1.1.0"

#define IDX_REC_SIZE (2+8+8+8+4)

struct chunk {
  uint16_t stream;
  uint64_t voffset;
  uint64_t poffset;
  uint64_t size;
};


// Filename of output files (query)
static char *fn_base;
// Flag set by '--ignore-errors'
static int   ignore_errors_flag;
// Directory containing result files
static char *directory_opt;
// Flag set by '--force', overwrite output files, if exist
static int   force_flag;
// Name of output file
static char *output_opt;

// Name of program (gather)
static char *program_name;

// Output file
FILE *out;

static void
print_usage ()
{
  printf("Usage: %s [OPTION]... FILENAME\n", program_name);
  puts("\
HSP Gather is the standard way to coalesce the output results of a\n\
completed HSP job.  The resulting output files with the name FILENAME are\n\
decompressed and concatenated to standard output, unless the -o option\n\
is specified.\n\n\
Options:\n\
  -d, --directory=DIR the directory to use when searching for result files\n\
  -f, --force         forcibly overwrite output file if it already exists\n\
  -i, --ignore-errors print a warning instead of failing if errors occur\n\
  -o, --output=FILE   write to output file instead of standard output\n\
      --help          display this help and exit\n\
      --version       display version information and exit\n\n\
Report bugs to <brekapal@utk.edu>\
");
}


static int
chunk_cmp (const void *p1, const void *p2)
{
  const struct chunk *c1, *c2;
  c1 = p1;
  c2 = p2;

  if (c1->stream == c2->stream) {
    return c1->voffset - c2->voffset;
  } else {
    return c1->stream - c2->stream;
  }
}


static void
copy (FILE *dest, FILE *src, size_t size)
{
  char  buf[4096];
  size_t chunk_sz;
  off_t off;

  for (off = 0; off < size; ++off) {
    chunk_sz = size-off;
    if (chunk_sz > sizeof(buf)) {
      chunk_sz = sizeof(buf);
    }

    chunk_sz = fread(buf, 1, chunk_sz, src);
    if (fwrite(buf, 1, chunk_sz, dest) != chunk_sz) {
      error(EXIT_FAILURE, errno, "Could not write to file");
    }

    off += chunk_sz;
  }
}


static int
print_file (const char *fpath)
{
  struct chunk *idx;
  FILE *f, *f_idx;
  char *idx_path;
  int nchunks, i;

  idx_path = malloc(strlen(fpath) + 4 + 1);
  if (idx_path == 0) {
    error(EXIT_FAILURE, errno, "Could not form index filename");
  }
  strcpy(idx_path, fpath);
  strcat(idx_path, ".idx");

  if (!(f = fopen(fpath, "r"))) {
    if (ignore_errors_flag) {
      error(0, errno, "%s: open failed, skipping", fpath);
      return 0;
    } else {
      error(EXIT_FAILURE, errno, "%s: open failed", fpath);
    }
  }

  if (!(f_idx = fopen(idx_path, "r"))) {
    if (ignore_errors_flag) {
      error(0, errno, "%s: open failed, skipping", idx_path);
      free(idx_path);
      return 0;
    } else {
      error(EXIT_FAILURE, errno, "%s: open failed", idx_path);
    }
  }
  free(idx_path);

  // TODO: Error check
  fseek(f_idx, 0, SEEK_END);
  nchunks = ftell(f_idx) / IDX_REC_SIZE;
  rewind(f_idx);
  
  // Read in the entire index
  idx = malloc(nchunks * sizeof(struct chunk));
  for (i=0; i<nchunks; ++i) {
    fread(&idx[i].stream,  sizeof(uint16_t), 1, f_idx);
    fread(&idx[i].voffset, sizeof(uint64_t), 1, f_idx);
    fread(&idx[i].poffset, sizeof(uint64_t), 1, f_idx);
    fread(&idx[i].size,    sizeof(uint64_t), 1, f_idx);
    fseek(f_idx, sizeof(uint32_t), SEEK_CUR);
  }
  // Sort it by (stream,voffset)
  qsort(idx, nchunks, sizeof(struct chunk), chunk_cmp);

#if 0
  printf("READ %d CHUNKS:\n", nchunks);
  for (i=0; i<nchunks; ++i) {
    printf("%hu %llu %llu %llu\n", idx[i].stream, idx[i].voffset, idx[i].poffset, idx[i].size);
  }
#endif

  // Finally, ready to write results
  for (i=0; i<nchunks; ++i) {
    fseek(f, idx[i].poffset, SEEK_SET);
    copy(out, f, idx[i].size);
  }

  free(idx);
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
      {"force",         no_argument,       0, 'f'},
      {"ignore-errors", no_argument,       0, 'i'},
      {"help",          no_argument,       0, '#'},
      {"version",       no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "d:o:fi",
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

  // Verify number of parameters
  if (optind == argc) {
    error(EXIT_FAILURE, 0, "Missing input filename");
  } else if (optind != argc-1) {
    error(EXIT_FAILURE, 0, "Too many parameters");
  }

  // Open output file (or stdout)
  if (output_opt) {
    out = fdopen(ioutil_open_w(output_opt, force_flag, 0), "w");
  } else {
    out = stdout;
  }

  // Path to output file
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
