#include <fstream>
#include <iostream>
#include <streambuf>

#include "stdiowrap/fstream.h"

namespace stdiowrap {

ifstream::ifstream ()
: std::istream(), buf()
{
  rdbuf(&buf);
}


ifstream::ifstream (const char *fn, std::ios_base::openmode m)
: std::istream(), buf()
{
  rdbuf(&buf);
  open(fn, m);
}


void
ifstream::open (const char *fn, std::ios_base::openmode m) {
  buf.open(fn, m);
  // TODO: set failbit
}


void
ifstream::close () {
  buf.close();
  // TODO: set failbit
}


bool
ifstream::is_open () {
  return buf.is_open();
}

} // namespace stdiowrap

