#define _GNU_SOURCE 500
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cliutils/version.h"
#include "ioutils/ioutils.h"
#include "strutils/strutils.h"

#define PACKAGE_NAME "HSP Merge BLAST XML"
#define AUTHORS "Paul Giblock"
#define VERSION "0.1.0"

// Input filename
char *input_opt   = 0;
// Output filename
char *output_opt  = 0;
// Force overwrite
int force_flag    = 0;

// Name of program (mergeblastxml)
static char *program_name;

enum states {
  STATE_HEADER,         // Printing first header
  STATE_ITERATION,      // In an iteration
  STATE_WAIT_ITERATION  // Waiting for next iteration
};


static int merge_xml (FILE *out, FILE *in);

static void
print_usage ()
{
  printf("Usage: %s [OPTION]... FILENAME\n", program_name);
  puts("\
HSP Merge BLAST XML can be used to merge a file containing multiple\n\
BLAST XML documents into a file containing only a single BLAST XML\n\
document.  This allows the output to be compatible with that of a \n\
single BLAST run.  This tool is compatible with XML generate by both\n\
blastall and blastpgp.\n\n\
Options:\n\
  -f, --force         forcibly overwrite output file if it already exists\n\
  -i, --input=FILE    read input from FILE instead of standard input\n\
  -o, --output=FILE   write to output file instead of standard output\n\
      --help          display this help and exit\n\
      --version       display version information and exit\n\n\
Report bugs to <brekapal@utk.edu>\
");
}


int
main (int argc, char **argv)
{
  FILE *in, *out;
  int   iters, c;

  program_name = argv[0];

  while (1) {
    static struct option long_options[] =
    {
      {"input",         required_argument, 0, 'i'},
      {"output",        required_argument, 0, 'o'},
      {"force",         no_argument,       0, 'f'},
      {"help",          no_argument,       0, '#'},
      {"version",       no_argument,       0, '^'},
      {0, 0, 0, 0}
    };

    int option_index = 0;

    c = getopt_long(argc, argv, "i:o:f",
                    long_options, &option_index);

    // Detect the end of the options.
    if (c == -1) {
      break;
    }

    switch (c) {
    case 'i':
      input_opt = optarg;
      break;

    case 'o':
      output_opt = optarg;
      break;

    case 'f':
      force_flag = 1;
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
  if (optind != argc) {
    error(EXIT_FAILURE, 0, "Too many parameters");
  }

  // Open input file (or stdin)
  if (input_opt) {
    if (!(in = fopen(input_opt, "r"))) {
      error(EXIT_FAILURE, errno, "%s: Could not open file", input_opt);
    }
  } else {
    in = stdin;
  }

  // Open output file (or stdout)
  if (output_opt) {
    out = fdopen(ioutil_open_w(output_opt, force_flag, 0), "w");
  } else {
    out = stdout;
  }

  // Do the algorithm
  iters = merge_xml(out, in);

  // Done
  fclose(out);
  fclose(in);

  if (!iters) {
    fputs("Warning: did not find any iterations.\n", stderr);
  }

  return EXIT_SUCCESS;
}


static int
merge_xml (FILE *out, FILE *in) {
  // Initial state
  enum states q    = STATE_HEADER;
  int         iter = 1;
  char       *l    = 0;
  size_t      sz   = 0;
  size_t      len  = 0;
  // Temp
  char       *pos;

  while ((len = getline(&l, &sz, in)) != -1) {
    switch (q) {
    case STATE_HEADER:
      if (strstr(l, "<Iteration>")) {
        // Switch to iteration state
        q = STATE_ITERATION;
      }
      // Write the line no matter what
      fwrite(l, 1, len, out); 
      break;

    case STATE_ITERATION:
      if (strstr(l, "</Iteration>")) {
        // Increment iter ID
        ++iter;
        // Found the end of the iteration. Wait for next.
        q = STATE_WAIT_ITERATION;
      }

      if ((pos = strstr(l, "<Iteration_iter-num>"))) {
        // Found iteration number, replace with new sequence
        // Print indentation
        fwrite(l, 1, pos-l, out);
        // Rewrite iternum
        fprintf(out, "<Iteration_iter-num>%d</Iteration_iter-num>\n", iter);
      } else {
        // Otherwise, a normal iteration line: print
        fwrite(l, 1, len, out);
      }
      break;

    case STATE_WAIT_ITERATION:
      // Waiting to see if we get another document
      if (strstr(l, "<Iteration>")) {
        // Awesome, open the iteration
        fwrite(l, 1, len, out); 
        // Switch to iteration state
        q = STATE_ITERATION;
      }
      break;
    }
  }

  // Close the document
  fputs("  </BlastOutput_iterations>\n</BlastOutput>\n", out);
  // Number of iters is nextId - 1
  return iter - 1;
}
