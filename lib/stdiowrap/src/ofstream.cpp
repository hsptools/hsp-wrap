#include <fstream>
#include <iostream>
#include <streambuf>

#include "stdiowrap/fstream.h"

namespace stdiowrap {

ofstream::ofstream ()
: std::ostream(), buf()
{
  rdbuf(&buf);
}


ofstream::ofstream (const char *fn, std::ios_base::openmode m)
: std::ostream(), buf()
{
  rdbuf(&buf);
  open(fn, m);
}


void
ofstream::open (const char *fn, std::ios_base::openmode m)
{
  buf.open(fn, m);
  // TODO: set failbit
}


void
ofstream::close ()
{
  buf.close();
  // TODO: set failbit
}


bool
ofstream::is_open () {
  return buf.is_open();
}

} // namespace stdiowrap

