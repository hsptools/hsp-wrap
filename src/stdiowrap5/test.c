#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "stdiowrap.h"

int main(int argc, char **argv)
{
  FILE *f;
  char  buf[128];
  int   rd;
  long  start;

  
  // Check command line
  if( argc != 2 ) {
    fprintf(stderr,"Usage:\n\ttest <input_file>\n");
    exit(1);
  }
  
  // Open infile
  if( !(f=stdiowrap_fopen(argv[1], "r")) ) {
    fprintf(stderr,"test: failed to open input file: \"%s\"\n",argv[1]);
    exit(1);
  }

  // Record start
  start = stdiowrap_ftell(f);

  // Read input file and dump to stdout
  while( (rd=stdiowrap_fread(buf,1,sizeof(buf)-1,f)) == sizeof(buf)-1 ) {
    buf[sizeof(buf)-1] = '\0';
    printf("%s", buf);
  }
  if( rd ) {
    buf[rd] = '\0';
    printf("%s", buf);
  }
  
  // Rewind the file
  stdiowrap_fseek(f,start,SEEK_SET);

  // Read input file and dump to stdout (again)
  while( (rd=stdiowrap_fread(buf,1,sizeof(buf)-1,f)) == sizeof(buf)-1 ) {
    buf[sizeof(buf)-1] = '\0';
    printf("%s", buf);
  }
  if( rd ) {
    buf[rd] = '\0';
    printf("%s", buf);
  }

  // Close infile
  stdiowrap_fclose(f);

  // Return success
  return 0;
}
