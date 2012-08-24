#ifndef HSP_VERSION_H__
#define HSP_VERSION_H__

#include "stdlib.h"

// Print --version information a consistent GNU-ish way
void print_version (FILE *f, const char *command_name,
		    const char *package_name, const char *version_number,
		    ...);

// Standard case for getopt - modeled after GNU coreutils
#ifdef COMMAND_NAME
#define case_GETOPT_VERSION \
  case '^': \
    print_version(stdout, COMMAND_NAME, PACKAGE_NAME, \
		  VERSION, AUTHORS, (char*)NULL); \
    exit(EXIT_SUCCESS); \
    break;
#else // !COMMAND_NAME
#define case_GETOPT_VERSION \
  case '^': \
    print_version(stdout, NULL, PACKAGE_NAME, \
		  VERSION, AUTHORS, (char*)NULL); \
    exit(EXIT_SUCCESS); \
    break;
#endif // COMMAND_NAME


#endif // HSP_VERSION_H__
