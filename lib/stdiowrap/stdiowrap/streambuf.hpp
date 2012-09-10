#ifndef STDIOWRAP_STREAMBUF_HPP__
#define STDIOWRAP_STREAMBUF_HPP__

// Make it easier to inject this source, squash errors if included into C source
#ifdef __cplusplus

#include <fstream>
#include <iostream>
#include <streambuf>


namespace stdiowrap {

// C++ class implementing the streambuf interface
class streambuf : public std::streambuf
{
 public:
  // The streambuf impl works by you providing it with pointers to a buffer.
  // There are three pointers per buffer (2 buffers: in and out)
  // You can read more here: http://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.0/classstd_1_1basic__streambuf.html
  static const size_t BUFF_SIZE = 1024;

  streambuf() :
    m_handle(((FILE*)-1))
    {
      // Initialize get and set pointers
      setg(NULL, NULL, NULL);
      setp(m_outBuff, m_outBuff + BUFF_SIZE - 1);
    }  

  streambuf(FILE *h) :
    m_handle(h)
    {
      // Initialize get and set pointers
      setg(NULL, NULL, NULL);
      setp(m_outBuff, m_outBuff + BUFF_SIZE - 1);
    }
  
  ~streambuf()
    {
      // Do any cleanup you may need hear (release refcount, etc..)
    }

  // These will associate/disassociate a stdiowrap handle with the stdstreamwrap object
  int  open(char *fn, char *mode);
  void close();
  
 protected:
  // "int_type" below is a typedef defined in streambuf.
  // Would make more sense if we were inheriting from basic_streambuf, but that just complicates the matter
  
  virtual int_type overflow(int_type c);
  virtual int_type sync();
  virtual int_type underflow();
  
 private:
  // Our handle
  FILE *m_handle;
  char  m_inBuff[BUFF_SIZE];
  char  m_outBuff[BUFF_SIZE];
};

}

#else  // __cplusplus
#  warning "stdiowrap/streambuf.hpp included in Non-C++ source"
#endif // __cplusplus

#endif // STDIOWRAP_STREAMBUF_HPP__
