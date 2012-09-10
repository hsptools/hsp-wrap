#include <fstream>
#include <iostream>
#include <streambuf>

#include "stdiowrap/fstream.hpp"

namespace stdiowrap {

fstream::fstream ()
: std::iostream(), buf()
{
  rdbuf(&buf);
}


fstream::fstream (const char *fn, std::ios_base::openmode m)
: std::iostream(), buf()
{
  rdbuf(&buf);
  open(fn, m);
}


void
fstream::open (const char *fn, std::ios_base::openmode m)
{
  buf.open(fn, m);
  // TODO: set failbit
}


void
fstream::close ()
{
  buf.close();
  // TODO: set failbit
}


bool
fstream::is_open ()
{
  return buf.is_open();
}


} // namespace stdiowrap

