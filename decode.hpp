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

struct Decode_error : std::exception {
	Decode_error() : std::exception() {}
};

class Basic_decoder {
public:
	//! \throw DecodeError
	Basic_decoder() noexcept(false) {}

	virtual ~Basic_decoder() noexcept {}

	//! \throw DecodeError
	virtual void next_frame(struct Frame &) = 0;

	//! \throw DecodeError
	virtual void seek(uint64_t sample) = 0;

	virtual unsigned sample_rate() const = 0;

	virtual uint64_t total_samples() const = 0;
};

class Decoder : public Basic_decoder {
public:
	//! \throw Bad_format
	//! \throw Sox_error
	Decoder(const std::string &, FILE *, enum file_format=FF_UNKNOWN);

	virtual ~Decoder() {}

	//! \throw DecodeError
	void next_frame(struct Frame &frame) override {
		_decoder->next_frame(frame);
	}

	//! \throw DecodeError
	void seek(uint64_t sample) override {
		_decoder->seek(sample);
	}

	//! \throw DecodeError
	void seek_frame(uint64_t frame) {
		// sample rates aren't always divisible by 3*5*5 = 75, e.g.
		// 32 kHz, which MP3 supports

		// seek(round(sample_rate * frame / 75))
		seek((2 * _decoder->sample_rate() * frame + 75) / 150);

		// though maybe floor would be better instead?
		//seek(_decoder->sample_rate() * frame / 75);
	}

	unsigned sample_rate() const override {
		return _decoder->sample_rate();
	}

	uint64_t total_samples() const override {
		return _decoder->total_samples();
	}

private:
	std::unique_ptr<Basic_decoder>	_decoder;
};

}

#endif
