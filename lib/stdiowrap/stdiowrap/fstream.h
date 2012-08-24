#ifndef STDIOWRAP_FSTREAM_H__
#define STDIOWRAP_FSTREAM_H__

// Make it easier to inject this source, squash errors if included into C source
#ifdef __cplusplus

#include <fstream>

namespace stdiowrap {

// C++ class implementing the filebuf interface
class filebuf : public std::filebuf
{
 public:
  static const size_t BUFF_SIZE = 1024;

  filebuf  ();
  filebuf  (FILE *h);
  ~filebuf ();

  // These will associate/disassociate a stdiowrap handle with the stdstreamwrap object
  int      open  (const char *fn, const char *mode);
  filebuf *open  (const char *fn, std::ios_base::openmode m);
  filebuf *close ();

  bool     is_open ();

 protected:
  virtual int_type overflow (int_type c);
  virtual int_type sync ();
  virtual int_type underflow ();

 private:
  // Our handle
  FILE *m_handle;
  char  m_inBuff[BUFF_SIZE];
  char  m_outBuff[BUFF_SIZE];
};


class ifstream : public std::istream
{
 public:
  ifstream ();
  explicit ifstream (const char *fn, std::ios_base::openmode m = std::ios_base::in);

  void open  (const char *fn, std::ios_base::openmode m = std::ios_base::in);
  void close ();

  bool is_open ();

  // TODO: wchar functions

 private:
  stdiowrap::filebuf buf;
};


class ofstream : public std::ostream
{
 public:
  ofstream ();
  explicit ofstream (const char *fn, std::ios_base::openmode m = std::ios_base::out);

  void open  (const char *fn, std::ios_base::openmode m = std::ios_base::out);
  void close ();

  bool is_open ();

  // TODO: wchar functions

 private:
  stdiowrap::filebuf buf;
};


class fstream : public std::iostream
{
 public:
  fstream ();
  explicit fstream (const char *fn, std::ios_base::openmode m = (std::ios_base::in | std::ios_base::out));

  void open (const char *fn, std::ios_base::openmode m = (std::ios_base::in | std::ios_base::out));
  void close ();

  bool is_open ();

  // TODO: wchar functions

 private:
  stdiowrap::filebuf buf;
};

}


#ifndef STDIOWRAP_C
#  ifdef STDIOWRAP_AUTO
namespace std {
  namespace stdiowrapauto {
    typedef ::stdiowrap::ifstream ifstream;
    typedef ::stdiowrap::ofstream ofstream;
    typedef ::stdiowrap::fstream  fstream;
  }
}

#    define ifstream    stdiowrapauto::ifstream
#    define ofstream    stdiowrapauto::ofstream
#    define fstream     stdiowrapauto::fstream
#  endif
#endif

#else  // __cplusplus
#  warning "fstreamwrap.h included in Non-C++ source"
#endif // __cplusplus

#endif // STDIOWRAP_FSTREAM_H__
