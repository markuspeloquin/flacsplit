#ifndef ERRORS_HPP
#define ERRORS_HPP

#include <exception>
#include <string>

namespace flacsplit {

struct Bad_format : std::exception {
	virtual ~Bad_format() throw () {}
	virtual const char *what() const throw ()
	{	return "bad format"; }
};

struct Sox_error : std::exception {
	Sox_error(const std::string &msg) :
		std::exception(),
		_msg(msg)
	{}
	virtual ~Sox_error() throw () {}
	virtual const char *what() const throw ()
	{	return _msg.c_str(); }
	std::string _msg;
};

struct Unix_error : std::exception {
	Unix_error(int errnum=-1);
	Unix_error(const std::string &msg, int errnum=-1);
	virtual ~Unix_error() throw () {}
	virtual const char *what() const throw ()
	{	return msg.c_str(); }

	std::string	msg;
	int		errnum;
};

}

#endif
