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

#include <cerrno>
#include <cstring>
#include <sstream>

#include "errors.hpp"

flacsplit::Unix_error::Unix_error(int errnum) {
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
