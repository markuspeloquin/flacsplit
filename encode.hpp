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
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#ifndef ENCODE_HPP
#define ENCODE_HPP

#include <boost/scoped_ptr.hpp>

#include "errors.hpp"
#include "transcode.hpp"

namespace flacsplit {

struct Encode_error : std::exception {
	Encode_error() : std::exception() {}
};

class Basic_encoder {
public:
	Basic_encoder() {}
	virtual ~Basic_encoder() {}
	virtual void add_frame(const struct Frame &) throw (Encode_error) = 0;
	virtual void set_meta(const Music_info &) = 0;
	virtual bool finish() = 0;
};

class Encoder {
public:
	struct Bad_format : std::exception {
		virtual ~Bad_format() throw () {}
		virtual const char *what() const throw ()
		{	return "bad format"; }
	};

	Encoder(FILE *, const Music_info &track, enum file_format=FF_FLAC)
	    throw (Bad_format);

	~Encoder() {}

	void add_frame(const struct Frame &frame) throw (Encode_error)
	{
		_encoder->add_frame(frame);
	}

	bool finish()
	{
		return _encoder->finish();
	}

private:
	boost::scoped_ptr<Basic_encoder>	_encoder;
};

}

#endif
