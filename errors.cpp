#include <cerrno>
#include <cstring>
#include <format>

#include "errors.hpp"

flacsplit::Unix_error::Unix_error(int errnum) {
	this->errnum = errnum < 0 ? errno : errnum;
	msg = strerror(this->errnum);
}

flacsplit::Unix_error::Unix_error(const std::string &msg, int errnum) :
	errnum(errnum < 0 ? errno : errnum)
{
	this->msg = std::format("{}: {}", msg, strerror(this->errnum));
}
