/* Copyright (c) 2011, Markus Peloquin <markus@cs.wisc.edu>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

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
