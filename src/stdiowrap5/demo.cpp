#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <iostream>

#include "streambufwrap.h"
#include "stdiowrap.h"

using namespace std;

int main (int argc, char *argv[])
{
  // Check command line
  if( argc < 3 ) {
    cout << "Usage:\n\tdemo <input> <output>\n";
    return 1;
  }

  // Open the input and output "files"
  cout << "Opening " << argv[1] << " and " << argv[2] << endl;
  FILE *in_handle  = stdiowrap_fopen(argv[1], "r");
  FILE *out_handle = stdiowrap_fopen(argv[2], "w");
  if( !in_handle || !out_handle ) {
    cout << "Failed to open input/output files." << endl;
    return 2;
  }

  // Turn them into C++ streams
	stdiowrap::streambuf in_stdsw(in_handle);
	stdiowrap::streambuf out_stdsw(out_handle);
  istream is(&in_stdsw);
  ostream os(&out_stdsw);

  // Just dump our input to output
  char buf[1024];
  int  count;
  do {
    is.getline(buf, 1024);
    count = is.gcount();
    cout << "Read " << count << " bytes: " << buf << endl;
    if( count ) {
      os << buf << endl;
    }
  } while( count );
  os.flush();

  // Return success
  return 0;
}
