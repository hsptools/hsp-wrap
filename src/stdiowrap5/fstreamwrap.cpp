#include <fstream>
#include <iostream>
#include <streambuf>

#include "fstreamwrap.h"

#define DEBUG 0
#ifdef DEBUG
#define TRACE(x) std::cerr << (x) << std::endl;
#endif

namespace stdiowrap {

ifstream::ifstream()
: std::istream(), buf()
{
	this->rdbuf(&buf);
}

ifstream::ifstream(const char *fn, std::ios_base::openmode m)
: std::istream(), buf()
{
	this->rdbuf(&buf);
	this->open(fn, m);
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
	this->rdbuf(&buf);
	TRACE("ofstream: Constructing ofstream (default)");
}

ofstream::ofstream(const char *fn, std::ios_base::openmode m)
: std::ostream(), buf()
{
	TRACE("ofstream: Constructing ofstream (with file)");
	this->rdbuf(&buf);
	this->open(fn, m);
}

void
ofstream::open(const char *fn, std::ios_base::openmode m) {
	TRACE("ofstream: Opening file");
	buf.open(fn, m);
	// TODO: set failbit
}

void
ofstream::close() {
	TRACE("ofstream: Closing file");
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
	this->rdbuf(&buf);
}

fstream::fstream(const char *fn, std::ios_base::openmode m)
: std::iostream(), buf()
{
	this->rdbuf(&buf);
	this->open(fn, m);
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

