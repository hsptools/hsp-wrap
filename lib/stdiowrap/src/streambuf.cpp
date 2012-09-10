#include <fstream>
#include <iostream>
#include <streambuf>

#include "stdiowrap/streambuf.hpp"
#include "stdiowrap/stdiowrap.h"

#define BAD_HANDLE ((FILE*)-1)

//
// Interface
//


int
stdiowrap::streambuf::open (char *fn, char *mode)
{
  FILE *in_handle;

  if (!(in_handle=stdiowrap_fopen(fn, mode))) {
    return 0;
  }

  m_handle = in_handle;
  return 1; // really want "m_handle" instead
}


void
stdiowrap::streambuf::close ()
{
  if (m_handle > 0) {
    stdiowrap_fclose(m_handle);
  }
  m_handle = BAD_HANDLE;
}


//
// Implementation of Virtual Interface
//

stdiowrap::streambuf::int_type
stdiowrap::streambuf::overflow (stdiowrap::streambuf::int_type c)
{
  char *begin = pbase();
  char *end   = pptr();
  
  // Note, this function may need a lock around it, if you plan on it being
  // called from multiple threads.

  // We save one extra byte at the end of our buffer for storing the "xtra byte".
  // if it is EOF, you may want to do soemthing different with it
  if (traits_type::not_eof(c)) {
    *(end++) = c;
  } else {
    // EOF, you decide what to do. I'm ignoring it
  }

  // Write the data
  stdiowrap_fwrite(begin, 1, end-begin, m_handle);
  
  // Reset the put pointer - we are ready for more.  You don't want to reset it it
  // all the way if you are unable to write everything that is available, I imagine.
  // (minus one to store the "extra byte" in overflow)
  setp(m_outBuff, m_outBuff + BUFF_SIZE - 1);

  return traits_type::not_eof(c);
}


stdiowrap::streambuf::int_type
stdiowrap::streambuf::sync ()
{
  // Flush out our buffer
  int_type ret = overflow(traits_type::eof());
  
  // Flush physical buffer
  stdiowrap_fflush(m_handle);
  
  // A type-neutral way to check for EOF, but you may want to return some combo of
  // the above two calls
  return traits_type::eq_int_type(ret, traits_type::eof()) ? -1 : 0;
}


stdiowrap::streambuf::int_type
stdiowrap::streambuf::underflow ()
{
  // Get as much as possible
  size_t len = stdiowrap_fread(m_inBuff, 1, BUFF_SIZE, m_handle);
  
  // Since the input buffer content is now valid (or is new)
  // the get pointer should be initialized (or reset).
  setg(m_inBuff, m_inBuff, m_inBuff + len);
  
  // Assume nothing means end-of-file, you may want to actually check
  // the last byte, look at feof(), etc...
  if (len == 0) {
    return traits_type::eof();
  } else {
    return traits_type::not_eof(m_inBuff[0]);
  }
}
