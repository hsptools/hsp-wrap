#include <stdio.h>
#include <stdarg.h>
#include "cliutils/version.h"

void
print_version (FILE *f,
	       const char *command_name, const char *package_name,
	       const char *version_number, ...)
{
  va_list authors;
  char *author;
  int nauthors, i;

  // Command and version information
  if (command_name) {
    fprintf(f, "%s (%s) %s\n", command_name, package_name, version_number);
  } else {
    fprintf(f, "%s %s\n", package_name, version_number);
  }
  
  // Standard copyright
  fputs("\
Copyright (C) 2012 National Institute for Computational Sciences\n\
License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n\
", f);

  // Count Authors
  va_start(authors, version_number);
  for (nauthors=0; va_arg(authors, char *); ++nauthors) ;
  va_end(authors);

  // Now display them
  if (nauthors) {
    fputs("\nWritten by ", f);

    va_start(authors, version_number);
    for (i=0; (author = va_arg(authors, char *)); ++i) {
      if (i == 0) {
	// First author, no comma or "and"
      } else if (i < nauthors-1) {
	// Is there still another author left to go?
	fputs(", ", f);
      } else if (i == 1) {
	// Last of only two authors
	fputs(" and ", f);
      } else {
	// Last author
	fputs(", and ", f);
      }
      fputs(author, f);
    }
    va_end(authors);
    fputc('\n', f);
  }
}
