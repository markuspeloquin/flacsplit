#include <cerrno>
#include <cstring>
#include <sstream>

#include "errors.hpp"

flacsplit::Unix_error::Unix_error(int errnum)
{
	this->errnum = errnum < 0 ? errno : errnum;
	msg = strerror(this->errnum);
}

flacsplit::Unix_error::Unix_error(const std::string &msg, int errnum) :
	msg(msg)
{
	this->errnum = errnum < 0 ? errno : errnum;
	std::ostringstream out;
	out << msg << ": " << strerror(this->errnum);
	this->msg = out.str();
}
