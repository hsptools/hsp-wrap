#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cliutils/version.h"
#include "ioutils/ioutils.h"

#define PACKAGE_NAME "HSP Sample FASTA"
#define AUTHORS "Paul Giblock"
#define VERSION "1.0.0"

enum state_t {
  IN_HEADER,
  IN_SEQUENCE
};

// Samples (sequence count)
long samples_opt  = 0;
// Minimum sequence length (in AAs)
int min_bound_opt = 1;
// Maximum sequence length (in AAs)
int max_bound_opt = INT_MAX;
// Input filename
char *input_opt   = 0;
// Output filename
char *output_opt  = 0;
// Other filename
char *other_opt  = 0;
// Force overwrite
int force_flag    = 1;

// Name of program (samplefasta)
static char *program_name;


static void
print_usage ()
{
  printf("Usage: %s [OPTION]... FILENAME\n", program_name);
  puts("\
HSP Sample FASTA is used to prepare a FASTA as a subset of another FASTA\n\
file.  This is primarily useful when preparing inputs for benchmarking\n\
Sequences are sampled with uniform distribution across the input file.\n\
There is also support to restrict the length of samples which are sequenced.\n\
Any sequences which exceed this limit are not considered while sampling.\n\n\
Options:\n\
  -s, --samples=SAMPLES   the number of samples to select\n\
  -m, --min-length=AAS    the minimum sequence length to accept\n\
  -M, --max-length=AAS    the maximum sequence length to accept\n\
  -o, --output=FILE       write to output file instead of standard output\n\
  -O, --other-output=FILE write to output file instead of standard output\n\
      --help              display this help and exit\n\
      --version           display version information and exit\n\n\
Report bugs to <brekapal@utk.edu>\
");
}

int
in_range (unsigned int val, unsigned int min_val, unsigned int max_val)
{
  return val >= min_val && val <= max_val;
}


void
write_skip_list (unsigned char **skip_list, long *skip_list_len, long pos, unsigned char val)
{
  if (pos >= *skip_list_len) {
    *skip_list_len *= 4;
    *skip_list = realloc(*skip_list, *skip_list_len * sizeof(unsigned char));
  }
  (*skip_list)[pos] = val;
}


int
parse_int (char *str, char *name)
{
  int val;
  if (sscanf(optarg, "%d", &val) != 1) {
    error(EXIT_FAILURE, 0, "Invalid format for %s", name);
  }
  if (val <= 0) {
    error(EXIT_FAILURE, 0, "Option %s must be a positive integer", name);
  }
  return val;
}


int
main (int argc, char **argv)
{
  FILE          *in, *out, *other_out;

  int            c, printing;
  unsigned int   naa, nbytes;
  char           ch, lastch;
  long           seqs, total_seqs, skip_list_len, i;
  double         ratio, acc;
  enum state_t   q;
  unsigned char *skip_list;

  program_name = argv[0];

  while (1) {
    static struct option long_options[] =
    {
      {"samples",       required_argument, 0, 's'},
      {"min-length",    required_argument, 0, 'm'},
      {"max-length",    required_argument, 0, 'M'},
      {"output",        required_argument, 0, 'o'},
      {"other-output",  required_argument, 0, 'O'},
      {"help",          no_argument,       0, '#'},
      {"version",       no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "s:m:M:o:O:",
	long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
      case 's':
	samples_opt = parse_int(optarg, "samples");
	break;

      case 'm':
	min_bound_opt = parse_int(optarg, "min-length");
	break;

      case 'M':
	max_bound_opt = parse_int(optarg, "max-length");
	break;

      case 'o':
	output_opt = optarg;
	break;

      case 'O':
	other_opt = optarg;
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
  if (!samples_opt) {
    error(EXIT_FAILURE, 0, "Missing number of samples");
  }

  // Path to input file
  input_opt = argv[optind];

  // Open input
  if (!(in = fopen(input_opt, "r"))) {
    error(EXIT_FAILURE, errno, "%s: Could not open file", input_opt);
  }

  // Open output file (or stdout)
  if (output_opt && strcmp(output_opt, "-")) {
    out = fdopen(ioutil_open_w(output_opt, force_flag, 0), "w");
  } else {
    out = stdout;
  }

  // Open other output file (or stdout)
  if (other_opt) {
    if (strcmp(other_opt, "-")) {
      other_out = fdopen(ioutil_open_w(other_opt, force_flag, 0), "w");
    } else {
      other_out = stdout;
    }
  } else {
    other_out = NULL;
  }

  // Don't allow writing to the same file for 'out' and 'other'
  if (out == other_out) {
    fprintf(stderr, "samplefasta: output and other-output must reference different files.\n");
    exit(EXIT_FAILURE);
  }

  // Initial skip_list buffer
  skip_list_len = 1<<10;
  if (!(skip_list = malloc(skip_list_len * sizeof(unsigned char)))) {
    fprintf(stderr, "samplefasta: insufficient memory.  Exiting.\n");
    exit(EXIT_FAILURE);
  }

  // Count seqs
  naa = nbytes = seqs = total_seqs = 0;
  q = IN_HEADER;
  lastch = '\n';
  while ((c = fgetc(in)) != EOF) {
    ch = (char)c & 0xFF;

    if (ch == '>' && lastch == '\n') {
      // Found the beginning of a sequence
      if (naa) {
	// Previous sequence exists, count it
	if (in_range(naa, min_bound_opt, max_bound_opt)) {
	  write_skip_list(&skip_list, &skip_list_len, total_seqs, 1);
	  seqs++;
	} else {
	  write_skip_list(&skip_list, &skip_list_len, total_seqs, 0);
	}
	total_seqs++;
      }
      // Set state to beginning of new sequence
      q   = IN_HEADER;
      naa = nbytes = 0;
    } else if (q == IN_HEADER && ch == '\n') {
      // Done with header, can count AA's now
      q = IN_SEQUENCE;
      naa = 0;
    } else if (q == IN_SEQUENCE && !isspace(ch)) {
      // Non-space character in sequence, count as AA
      naa++;
    }

    nbytes++;
    lastch = ch;
  }

  // Catch last sequence
  if (naa) {
    if (in_range(naa, min_bound_opt, max_bound_opt)) {
      write_skip_list(&skip_list, &skip_list_len, total_seqs, 1);
      seqs++;
    } else {
      write_skip_list(&skip_list, &skip_list_len, total_seqs, 0);
    }
    total_seqs++;
  }

  fprintf(stderr, "Total: %ld, In bounds: %ld\n", total_seqs, seqs);

  // Back to beginning
  fseek(in, 0, SEEK_SET);
  ratio = ((double)samples_opt)/total_seqs;

  // Print seqs
  acc      = 1.0;
  lastch   = '\n';
  printing = 0;
  i        = 0;
  while ((c = fgetc(in)) != EOF) {
    ch = (char)c & 0xFF;

    if (ch == '>' && lastch == '\n') {
      if (acc >= 1.0 && skip_list[i] ) {
	printing = 1;
	acc -= 1.0;
      } else {
	printing = 0;
      }
      acc += ratio;
      i   += 1;
    }

    if (printing) {
      fputc(ch, out);
    } else if (other_out) {
      fputc(ch, other_out);
    }

    lastch = ch;
  }

  fclose(in);

  return 0;
}
