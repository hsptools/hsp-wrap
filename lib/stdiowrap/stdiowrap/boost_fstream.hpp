#ifndef STDIOWRAP_BOOST_FSTREAM_HPP__
#define STDIOWRAP_BOOST_FSTREAM_HPP__

// Make it easier to inject this source, squash errors if included into C source
#ifdef __cplusplus

#include <fstream>
#include <boost/config.hpp>
#include <boost/filesystem/path.hpp>

#include "stdiowrap/fstream.hpp"

namespace stdiowrap {
namespace boost {

// C++ class implementing the filebuf interface
class filebuf : public stdiowrap::filebuf
{
 public:
  filebuf *open (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
  {
    return stdiowrap::filebuf::open(p.string().c_str(), mode)
      ? this : 0;
  }
};


class ifstream : public stdiowrap::ifstream
{
 public:
  explicit ifstream (const ::boost::filesystem::path &p)
    : stdiowrap::ifstream(p.string().c_str(), std::ios_base::in)
  {}

  ifstream (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
    : stdiowrap::ifstream(p.string().c_str(), mode)
  {}

  void open (const ::boost::filesystem::path &p)
  {
    stdiowrap::ifstream::open(p.string().c_str(), std::ios_base::in);
  }

  void open (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
  {
    stdiowrap::ifstream::open(p.string().c_str(), mode);
  }
};


class ofstream : public stdiowrap::ofstream
{
 public:
  explicit ofstream (const ::boost::filesystem::path &p)
    : stdiowrap::ofstream(p.string().c_str(), std::ios_base::in)
  {}

  ofstream (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
    : stdiowrap::ofstream(p.string().c_str(), mode)
  {}

  void open (const ::boost::filesystem::path &p)
  {
    stdiowrap::ofstream::open(p.string().c_str(), std::ios_base::out);
  }

  void open (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
  {
    stdiowrap::ofstream::open(p.string().c_str(), mode);
  }
};


class fstream : public stdiowrap::fstream
{
 public:
  explicit fstream (const ::boost::filesystem::path &p)
    : stdiowrap::fstream(p.string().c_str(),
                         std::ios_base::in | std::ios_base::out)
  {}

  fstream (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
    : stdiowrap::fstream(p.string().c_str(), mode)
  {}

  void open (const ::boost::filesystem::path &p)
  {
    stdiowrap::fstream::open(p.string().c_str(),
                             std::ios_base::in | std::ios_base::out);
  }

  void open (const ::boost::filesystem::path &p, std::ios_base::openmode mode)
  {
    stdiowrap::fstream::open(p.string().c_str(), mode);
  }
};

} // namespace boost
} // namespace stdiowrap

#else  // __cplusplus
#  warning "stdiowrap/boost_fstream.hpp included in Non-C++ source"
#endif // __cplusplus

#endif // STDIOWRAP_BOOST_FSTREAM_HPP__
