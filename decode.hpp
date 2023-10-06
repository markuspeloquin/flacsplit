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

#ifndef DECODE_HPP
#define DECODE_HPP

#include <cstdio>
#include <memory>
#include <string>

#include "errors.hpp"
#include "transcode.hpp"

namespace flacsplit {

struct Decode_error : std::exception {};

class Basic_decoder {
public:
	//! \throw DecodeError
	Basic_decoder() noexcept(false) {}

	virtual ~Basic_decoder() noexcept {}

	//! \throw DecodeError
	virtual Frame next_frame(bool allow_short) = 0;

	//! \throw DecodeError
	virtual void seek(int64_t sample) = 0;

	virtual int32_t sample_rate() const = 0;

	virtual int64_t total_samples() const = 0;
};

class Decoder : public Basic_decoder {
public:
	//! \throw Bad_format
	//! \throw Sndfile_error
	Decoder(FILE *, file_format=file_format::UNKNOWN);

	//! \throw DecodeError
	Frame next_frame(bool allow_short) override {
		return _decoder->next_frame(allow_short);
	}

	//! \throw DecodeError
	void seek(int64_t sample) override {
		_decoder->seek(sample);
	}

	//! \throw DecodeError
	void seek_frame(int64_t frame) {
		// sample rates aren't always divisible by 3*5*5 = 75, e.g.
		// 32 kHz, which MP3 supports
		int64_t numer = _decoder->sample_rate() * frame;
		int64_t sample = numer / 75;
		if (sample * 75 != numer)
			throw_traced(std::runtime_error(
			    "frame number doesn't map to a sample number"
			));
		seek(sample);
	}

	int32_t sample_rate() const override {
		return _decoder->sample_rate();
	}

	int64_t total_samples() const override {
		return _decoder->total_samples();
	}

private:
	std::unique_ptr<Basic_decoder>	_decoder;
};

}

#endif
