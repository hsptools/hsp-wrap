#ifndef FSTREAMWRAP_H
#define FSTREAMWRAP_H

#include <istream>
#include <ostream>

#ifndef STDIOWRAP_C
#ifdef STDIOWRAP_AUTO
#define std::ifstream    stdiowrap::ifstream
#define ifstream         stdiowrap::ifstream
#define std::ofstream    stdiowrap::ofstream
#define ofstream         stdiowrap::ofstream
#define std::fstream     stdiowrap::fstream
#define fstream          stdiowrap::fstream
#endif
#endif

namespace stdiowrap {

// C++ class implementing the filebuf interface
class filebuf : public std::filebuf
{
 public:
  static const size_t BUFF_SIZE = 1024;

  filebuf();
  filebuf(FILE *h);
  ~filebuf();

  // These will associate/disassociate a stdiowrap handle with the stdstreamwrap object
  int      open(const char *fn, const char *mode);
  filebuf *open(const char *fn, std::ios_base::openmode m);
  filebuf *close();

	bool     is_open();
  
 protected:
  virtual int_type overflow(int_type c);
  virtual int_type sync();
  virtual int_type underflow();
  
 private:
  // Our handle
  FILE *m_handle;
  char  m_inBuff[BUFF_SIZE];
  char  m_outBuff[BUFF_SIZE];
};


class ifstream : public std::istream
{
	public:
  ifstream();
  explicit ifstream(const char *fn, std::ios_base::openmode m = std::ios_base::in);

  void open(const char *fn, std::ios_base::openmode m = std::ios_base::in);

	std::filebuf *rdbuf() const;
	bool is_open();

  // TODO: wchar functions

	private:
	stdiowrap::filebuf buf;
};


class ofstream : public std::ostream
{
	public:
  ofstream();
  explicit ofstream(const char *fn, std::ios_base::openmode m = std::ios_base::out);

  void open(const char *fn, std::ios_base::openmode m = std::ios_base::out);

	std::filebuf *rdbuf() const;
	bool is_open();

  // TODO: wchar functions

	private:
	stdiowrap::filebuf buf;
};


class fstream : public std::iostream
{
	public:
  fstream();
  explicit fstream(const char *fn, std::ios_base::openmode m = (std::ios_base::in | std::ios_base::out));

  void open(const char *fn, std::ios_base::openmode m = (std::ios_base::in | std::ios_base::out));

	std::filebuf *rdbuf() const;
	bool is_open();

  // TODO: wchar functions

	private:
	stdiowrap::filebuf buf;
};

}

#endif
