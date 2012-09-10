#include <fstream>
#include <iostream>
#include <streambuf>

#include "stdiowrap/fstream.hpp"
#include "stdiowrap/stdiowrap.h"

#define BAD_HANDLE ((FILE *)-1)

namespace stdiowrap {


/** Convert a std openmode to a mode appropriate for C stdio.
 *  @param m the C++ openmode in which to convert
 *  @return The C stdio mode string, or NULL on error
 */
static const char *
cpp_openmode_to_c (std::ios_base::openmode m)
{
  using namespace std;
  if (m == (ios_base::in))
    return "r";
  else if (m == (ios_base::out | ios_base::trunc))
    return "w";
  else if (m == (ios_base::out))
    return "w";
  else if (m == (ios_base::out | ios_base::app))
    return "a";
  else if (m == (ios_base::in | ios_base::out))
    return "r+";
  else if (m == (ios_base::in | ios_base::out | ios_base::trunc))
    return "w+";
  else if (m == (ios_base::in | ios_base::out | ios_base::app))
    return "a+";
  else
    return NULL;
}


filebuf::filebuf ()
: m_handle(BAD_HANDLE)
{
  // Initialize get and set pointers
  setg(NULL, NULL, NULL);
  setp(m_outBuff, m_outBuff + BUFF_SIZE - 1);
}


filebuf::filebuf (FILE *h)
: m_handle(h)
{
  // Initialize get and set pointers
  setg(NULL, NULL, NULL);
  setp(m_outBuff, m_outBuff + BUFF_SIZE - 1);
}


filebuf::~filebuf ()
{
  // Do any cleanup you may need here (release refcount, etc..)
}


int
filebuf::open (const char *fn, const char *mode)
{
  FILE *in_handle;

  if (!(in_handle = stdiowrap_fopen(fn, mode))) {
    return 0;
  }

  m_handle = in_handle;
  return 1; // really want "m_handle" instead
}


filebuf *
filebuf::open (const char *fn, std::ios_base::openmode m)
{
  const char *mode = cpp_openmode_to_c(m);

  if (mode && this->open(fn, mode)) {
    return this;
  } else {
    return NULL;
  }
}


filebuf *
filebuf::close ()
{
  if (m_handle > 0) {
    this->sync();
    stdiowrap_fclose(m_handle);
  }
  m_handle = BAD_HANDLE;

  // TODO: Actual error checking
  return this;
}


bool
filebuf::is_open ()
{
  return m_handle >= 0;
}


//
// Implementation of Virtual Interface
//

filebuf::int_type
filebuf::overflow (filebuf::int_type c)
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


filebuf::int_type
filebuf::sync ()
{
  // Flush out our buffer
  int_type ret = overflow(traits_type::eof());

  // Flush physical buffer
  stdiowrap_fflush(m_handle);

  // A type-neutral way to check for EOF, but you may want to return some combo of
  // the above two calls
  return traits_type::eq_int_type(ret, traits_type::eof()) ? -1 : 0;
}


filebuf::int_type
filebuf::underflow ()
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

} // namespace stdiowrap
