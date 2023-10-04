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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>

#include <FLAC++/decoder.h>
#include <sndfile.h>

#include "decode.hpp"
#include "errors.hpp"

namespace {

const unsigned FRAMES_PER_SEC = 75;

enum flacsplit::file_format	get_file_format(FILE *);
bool				same_file(FILE *, FILE *);

class Flac_decoder :
    public FLAC::Decoder::File,
    public flacsplit::Basic_decoder {
public:
	struct Flac_decode_error : flacsplit::Decode_error {
		Flac_decode_error(const std::string &msg) : _msg(msg) {}

		const char *what() const noexcept override {
			return _msg.c_str();
		}

		std::string _msg;
	};

	//! Note that this takes ownership of the file (or rather, the FLAC
	//! library takes ownership).
	//! \throw Flac_decode_error
	Flac_decoder(FILE *);

	//! \throw Flac_decode_error
	void next_frame(struct flacsplit::Frame &) override;

	void seek(uint64_t sample) override {
		seek_absolute(sample);
	}

	unsigned sample_rate() const override {
		return get_sample_rate();
	}

	uint64_t total_samples() const override {
		return get_total_samples();
	}

protected:
	FLAC__StreamDecoderWriteStatus write_callback(
	    const FLAC__Frame *, const FLAC__int32 *const *) override;

	FLAC__StreamDecoderSeekStatus seek_callback(FLAC__uint64) override;

	FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64 *) override;

	FLAC__StreamDecoderLengthStatus length_callback(FLAC__uint64 *)
	    override;

	bool eof_callback() override {
		return feof(_fp);
	}

	void error_callback(FLAC__StreamDecoderErrorStatus status) override {
		_last_status = FLAC__StreamDecoderErrorStatusString[status];
	}

private:
	FILE				*_fp;
	const FLAC__Frame		*_last_frame;
	std::unique_ptr<const FLAC__int32 *>
					_last_buffer;
	const char			*_last_status;
	bool				_frame_retrieved;
};

class Wave_decoder : public flacsplit::Basic_decoder {
public:
	struct Wave_decode_error : flacsplit::Decode_error {
		Wave_decode_error(const char *msg) : _msg(msg) {}

		const char *what() const noexcept override {
			return _msg.c_str();
		}

		std::string _msg;
	};

	//! \throw flacsplit::Sndfile_error
	Wave_decoder(const std::string &, FILE *);

	virtual ~Wave_decoder() noexcept {
		close_quiet(_file);
	}

	//! \throw Wave_decode_error
	void next_frame(struct flacsplit::Frame &) override;

	//! \throw Wave_decode_error
	void seek(uint64_t sample) override {
		if (sf_seek(_file, frame, SF_SEEK_SET) == -1)
			throw_traced(flacsplit::Sndfile_error(
			    "sf_seek error", sf_error(_file)
			));
	}

	unsigned sample_rate() const override {
		return _info.samplerate;
	}

	uint64_t total_samples() const override {
		// 31 bits is big enough for 2-channel 48 kHz for 6.21 hours
		return _info.frames;
	}

private:
	static void close_quiet(SNDFILE *file) noexcept;

	std::unique_ptr<int32_t>	_samples;
	std::unique_ptr<int32_t>	_transp;
	std::unique_ptr<int32_t *>	_transp_ptrs;
	SNDFILE		*_file;
	SF_INFO		_info;
	sf_count_t	_samples_len;
};

Flac_decoder::Flac_decoder(FILE *fp) :
	FLAC::Decoder::File(),
	Basic_decoder(),
	_fp(fp),
	_last_frame(nullptr),
	_last_buffer(),
	_last_status(nullptr),
	_frame_retrieved(false)
{
	FLAC__StreamDecoderInitStatus status;
	if ((status = init(fp)) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
		throw_traced(Flac_decode_error(
		    FLAC__StreamDecoderInitStatusString[status]
		));
	seek(0);
}

void
Flac_decoder::next_frame(struct flacsplit::Frame &frame) {
	// a seek will trigger a call to write_callback(), so don't process
	// the next frame if it hasn't been seen yet here
	if (!_last_frame || _frame_retrieved)
		if (!process_single())
			throw_traced(Flac_decode_error(
			    get_state().as_cstring()
			));
	if (_last_status)
		throw_traced(Flac_decode_error(_last_status));
	frame.data = _last_buffer.get();
	frame.channels = _last_frame->header.channels;
	frame.samples = _last_frame->header.blocksize;
	frame.rate = _last_frame->header.sample_rate;
	_frame_retrieved = true;
}

FLAC__StreamDecoderWriteStatus
Flac_decoder::write_callback(const FLAC__Frame *frame,
    const FLAC__int32 *const *buffer) {
	if (!_last_buffer.get())
		// the number of channels should be constant
		_last_buffer.reset(new const FLAC__int32 *[
		    frame->header.channels]);

	/*
	 * usually, 'buffer' is an array; when there is a short, unaligned,
	 * read, 'buffer' is a temporary heap-allocated buffer that must be
	 * copied
	 */

	_last_frame = frame;
	std::copy(buffer, buffer + frame->header.channels,
	    _last_buffer.get());
	_last_status = nullptr;
	_frame_retrieved = false;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

FLAC__StreamDecoderSeekStatus
Flac_decoder::seek_callback(FLAC__uint64 absolute_byte_offset) {
	long off = absolute_byte_offset;
	if (static_cast<FLAC__uint64>(off) != absolute_byte_offset)
		flacsplit::throw_traced(std::runtime_error("bad offset"));

	return fseek(_fp, off, SEEK_SET) ?
	    FLAC__STREAM_DECODER_SEEK_STATUS_ERROR :
	    FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus
Flac_decoder::tell_callback(FLAC__uint64 *absolute_byte_offset) {
	long off = ftell(_fp);
	if (off < 0)
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = off;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus
Flac_decoder::length_callback(FLAC__uint64 *stream_length) {
	struct stat st;
	if (fstat(fileno(_fp), &st))
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	*stream_length = st.st_size;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

Wave_decoder::Wave_decoder(const std::string &path, FILE *fp) :
	Basic_decoder(),
	_samples(),
	_transp()
{
	_file = sf_open_fd(fileno(fp), SFM_READ, &_info, false);
	if (!_file)
		throw_traced(flacsplit::Sndfile_error(
		    "sf_open_fd failed", sf_error(nullptr)
		));

	try {
		_samples_len = _info.channels * _info.samplerate /
		    FRAMES_PER_SEC;
		_samples.reset(new int32_t[_samples_len]);
		_transp.reset(new int32_t[_samples_len]);
		_transp_ptrs.reset(new int32_t *[_info.channels]);
	} catch (...) {
		close_quiet(_file);
		throw;
	}
}

void
Wave_decoder::close_quiet(SNDFILE *file) noexcept {
		int errnum = sf_close(file);
		if (errnum) {
			// Probably never occurs.
			std::cerr << "failed to close: "
			    << sf_error_number(errnum) << '\n';
		}
}

void
Wave_decoder::next_frame(struct flacsplit::Frame &frame) {
	sf_count_t samples;
	samples = sf_read_int(_file, _samples.get(), _samples_len);
	if (samples < _samples_len) {
		// This may only occur on the final track if this is not an
		// exact number of frames. Perhaps because the data is not from
		// a CD.
		std::cerr << "expected " << _samples_len << " but got " << samples << '\n';
		throw_traced(flacsplit::Sndfile_error(
		    "sf_read error", sf_error(_file)
		));
	}

	frame.data = _transp_ptrs.get();
	frame.channels = _info.channels;
	if (samples % frame.channels)
		flacsplit::throw_traced(std::runtime_error(
		    "bad number of samples"
		));
	frame.samples = samples / frame.channels;
	frame.rate = _info.samplerate;

	// transpose _samples => _transp
	size_t i = 0;
	for (size_t sample = 0; sample < frame.samples; sample++) {
		size_t j = sample;
		for (size_t channel = 0; channel < frame.channels; channel++) {
			_transp.get()[j] = _samples.get()[i];
			i++;
			j += frame.samples;
		}
	}

	// make 2d array to return
	_transp_ptrs.get()[0] = _transp.get();
	for (size_t channel = 1; channel < frame.channels; channel++)
		_transp_ptrs.get()[channel] = _transp_ptrs.get()[channel-1] +
		    frame.samples;
}

flacsplit::file_format
get_file_format(FILE *fp) {
	const char *const RIFF = "RIFF";
	const char *const WAVE = "WAVE";
	const char *const FLAC = "fLaC";

	char buf[12];

	if (!fread(buf, sizeof(buf), 1, fp))
		return flacsplit::file_format::UNKNOWN;
	fseek(fp, -sizeof(buf), SEEK_CUR);

	if (std::equal(buf, buf+4, RIFF) && std::equal(buf+8, buf+12, WAVE))
		return flacsplit::file_format::WAVE;
	if (std::equal(buf, buf+4, FLAC))
		return flacsplit::file_format::FLAC;
	return flacsplit::file_format::UNKNOWN;
}

} // end anon

flacsplit::Decoder::Decoder(const std::string &path, FILE *fp,
    enum file_format format) :
	Basic_decoder(),
	_decoder()
{
	if (format == file_format::UNKNOWN)
		format = get_file_format(fp);
	switch (format) {
	case file_format::UNKNOWN:
		throw throw_traced(Bad_format());
	case file_format::WAVE:
		_decoder.reset(new Wave_decoder(path, fp));
		break;
	case file_format::FLAC:
		_decoder.reset(new Flac_decoder(fp));
	}
}
