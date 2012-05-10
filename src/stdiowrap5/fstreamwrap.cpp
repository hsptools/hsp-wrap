#include <fstream>
#include <iostream>
#include <streambuf>

#include "fstreamwrap.h"

namespace stdiowrap {

ifstream::ifstream()
: std::istream(), buf()
{
	rdbuf(&buf);
}

ifstream::ifstream(const char *fn, std::ios_base::openmode m)
: std::istream(), buf()
{
	rdbuf(&buf);
	open(fn, m);
}

void
ifstream::open(const char *fn, std::ios_base::openmode m) {
	buf.open(fn, m);
	// TODO: set failbit
}

void
ifstream::close() {
	buf.close();
	// TODO: set failbit
}

bool
ifstream::is_open() {
	return buf.is_open();
}

/////////////////////////////////////////////////////////

ofstream::ofstream()
: std::ostream(), buf()
{
	rdbuf(&buf);
}

ofstream::ofstream(const char *fn, std::ios_base::openmode m)
: std::ostream(), buf()
{
	rdbuf(&buf);
	open(fn, m);
}

void
ofstream::open(const char *fn, std::ios_base::openmode m) {
	buf.open(fn, m);
	// TODO: set failbit
}

void
ofstream::close() {
	buf.close();
	// TODO: set failbit
}

bool
ofstream::is_open() {
	return buf.is_open();
}

/////////////////////////////////////////////////////////

fstream::fstream()
: std::iostream(), buf()
{
	rdbuf(&buf);
}

fstream::fstream(const char *fn, std::ios_base::openmode m)
: std::iostream(), buf()
{
	rdbuf(&buf);
	open(fn, m);
}

void
fstream::open(const char *fn, std::ios_base::openmode m) {
	buf.open(fn, m);
	// TODO: set failbit
}

void
fstream::close() {
	buf.close();
	// TODO: set failbit
}

bool
fstream::is_open() {
	return buf.is_open();
}


} // namespace stdiowrap

