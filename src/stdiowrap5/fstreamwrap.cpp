#include <fstream>
#include <iostream>
#include <streambuf>

#include "fstreamwrap.h"

namespace stdiowrap {

ifstream::ifstream()
: std::istream(), buf()
{
}

ifstream::ifstream(const char *fn, std::ios_base::openmode m)
: std::istream(), buf()
{
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

std::filebuf*
ifstream::rdbuf() const {
	return const_cast<stdiowrap::filebuf*>(&buf);
}

bool
ifstream::is_open() {
	return buf.is_open();
}

/////////////////////////////////////////////////////////

ofstream::ofstream()
: std::ostream(), buf()
{
}

ofstream::ofstream(const char *fn, std::ios_base::openmode m)
: std::ostream(), buf()
{
	this->open(fn, m);
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

std::filebuf*
ofstream::rdbuf() const {
	return const_cast<stdiowrap::filebuf*>(&buf);
}

bool
ofstream::is_open() {
	return buf.is_open();
}

/////////////////////////////////////////////////////////

fstream::fstream()
: std::iostream(), buf()
{
}

fstream::fstream(const char *fn, std::ios_base::openmode m)
: std::iostream(), buf()
{
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

std::filebuf*
fstream::rdbuf() const {
	return const_cast<stdiowrap::filebuf*>(&buf);
}

bool
fstream::is_open() {
	return buf.is_open();
}


} // namespace stdiowrap

