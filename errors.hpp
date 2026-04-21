#ifndef ERRORS_HPP
#define ERRORS_HPP

#include <exception>
#include <sstream>
#include <string>

#include <boost/exception/all.hpp>
#include <boost/stacktrace.hpp>
#include <sndfile.h>

namespace flacsplit {

struct Bad_format : std::exception {
	const char *what() const noexcept override {
		return "bad format";
	}
};

struct Bad_samplefreq : std::exception {
	const char *what() const noexcept override {
		return "bad sample frequency";
	}
};

struct Not_enough_samples : public std::exception {
	Not_enough_samples() :
		_msg("not enough samples to calculate with")
	{}

	Not_enough_samples(const std::string &msg) : _msg(msg) {}

	const char *what() const noexcept override {
		return _msg.c_str();
	}

private:
	std::string _msg;
};

struct Sndfile_error : std::exception {
	Sndfile_error(int errnum) : errnum(errnum) {
		msg = sf_error_number(errnum);
	}

	Sndfile_error(const std::string &msg, int errnum) : errnum(errnum) {
		std::ostringstream out;
		out << msg << ": " << sf_error_number(errnum);
		this->msg = out.str();
	}

	const char *what() const noexcept override {
		return msg.c_str();
	}

	std::string msg;
	int errnum;
};

struct Unix_error : std::exception {
	Unix_error(int errnum=-1);

	Unix_error(const std::string &msg, int errnum=-1);

	const char *what() const noexcept override {
		return msg.c_str();
	}

	std::string	msg;
	int		errnum;
};

typedef boost::error_info<struct tag_stacktrace, boost::stacktrace::stacktrace> traced;

template <class E>
E throw_traced(const E &e) {
	throw boost::enable_error_info(e) << traced(boost::stacktrace::stacktrace());
}

}

#endif
