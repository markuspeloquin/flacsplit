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

#include <boost/scoped_array.hpp>

#include <FLAC++/decoder.h>
#include <sox.h>

#include "decode.hpp"

namespace {

const unsigned FRAMES_PER_SEC = 75;

enum flacsplit::file_format
		get_file_format(FILE *);
bool		same_file(FILE *, FILE *) throw (flacsplit::Unix_error);

class Sox_init {
public:
	Sox_init() : _valid(false) {}
	~Sox_init()
	{
		if (_valid)
			sox_quit();
	}

	static void init() throw (flacsplit::Sox_error)
	{
		if (!_instance._valid)
			_instance.do_init();
	}

private:
	Sox_init(const Sox_init &) {}
	void operator=(const Sox_init &) {}

	void do_init() throw (flacsplit::Sox_error)
	{
		_valid = true;
		if (sox_init() != SOX_SUCCESS)
			_valid = false;
		//if (_valid && sox_format_init() != SOX_SUCCESS) {
		//	sox_quit();
		//	_valid = false;
		//}

		if (!_valid)
			throw flacsplit::Sox_error("sox_init() error");
	}

	static Sox_init _instance;

	bool _valid;
};

class Flac_decoder :
    public FLAC::Decoder::File,
    public flacsplit::Basic_decoder {
public:
	struct Flac_decode_error : flacsplit::Decode_error {
		Flac_decode_error(const std::string &msg) :
			flacsplit::Decode_error(),
			_msg(msg)
		{}
		virtual ~Flac_decode_error() throw () {}
		virtual const char *what() const throw ()
		{	return _msg.c_str(); }
		std::string _msg;
	};

	Flac_decoder(FILE *fp) throw (Flac_decode_error);
	virtual ~Flac_decoder() {}

	virtual void next_frame(struct flacsplit::Frame &)
	    throw (Flac_decode_error);

	virtual void seek(uint64_t sample) throw ()
	{
		seek_absolute(sample);
	}

	virtual unsigned sample_rate() const
	{
		return get_sample_rate();
	}

	virtual uint64_t total_samples() const
	{
		return get_total_samples();
	}

protected:
	virtual FLAC__StreamDecoderWriteStatus write_callback(
	    const FLAC__Frame *, const FLAC__int32 *const *);

	virtual FLAC__StreamDecoderSeekStatus seek_callback(FLAC__uint64);

	virtual FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64 *);

	virtual FLAC__StreamDecoderLengthStatus length_callback(
	    FLAC__uint64 *);

	virtual bool eof_callback()
	{
		return feof(_fp);
	}

	virtual void error_callback(FLAC__StreamDecoderErrorStatus status)
	{
		_last_status = FLAC__StreamDecoderErrorStatusString[status];
	}

private:
	FILE				*_fp;
	const FLAC__Frame		*_last_frame;
	boost::scoped_array<const FLAC__int32 *>
					_last_buffer;
	const char			*_last_status;
	bool				_frame_retrieved;
};

class Wave_decoder : public flacsplit::Basic_decoder {
public:
	struct Wave_decode_error : flacsplit::Decode_error {
		Wave_decode_error(const char *msg) :
			flacsplit::Decode_error(),
			_msg(msg)
		{}
		virtual ~Wave_decode_error() throw () {}
		virtual const char *what() const throw ()
		{	return _msg.c_str(); }
		std::string _msg;
	};

	Wave_decoder(const std::string &, FILE *)
	    throw (flacsplit::Sox_error);
	virtual ~Wave_decoder()
	{
		sox_close(_fmt);
	}

	virtual void next_frame(struct flacsplit::Frame &)
	    throw (Wave_decode_error);

	virtual void seek(uint64_t sample) throw (Wave_decode_error)
	{
		sample *= _fmt->signal.channels;
		if (sox_seek(_fmt, sample, SOX_SEEK_SET) != SOX_SUCCESS)
			throw Wave_decode_error("sox_seek() error");
	}

	virtual unsigned sample_rate() const
	{
		return _fmt->signal.rate;
	}

	virtual uint64_t total_samples() const
	{
		// a 32-bit size_t (sox_signalinfo_t::length) is big enough
		// for 2-channel 44.1 kHz for 13.5 hours
		return _fmt->signal.length / _fmt->signal.channels;
	}

private:
	boost::scoped_array<sox_sample_t>	_samples;
	boost::scoped_array<int32_t>		_transp;
	boost::scoped_array<int32_t *>		_transp_ptrs;
	sox_format_t	*_fmt;
	size_t		_samples_len;
};

Sox_init Sox_init::_instance;

Flac_decoder::Flac_decoder(FILE *fp) throw (Flac_decode_error) :
	FLAC::Decoder::File(),
	Basic_decoder(),
	_fp(fp),
	_last_frame(0),
	_last_buffer(0),
	_last_status(0),
	_frame_retrieved(false)
{
	FLAC__StreamDecoderInitStatus status;
	if ((status = init(fp)) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
		throw Flac_decode_error(
		    FLAC__StreamDecoderInitStatusString[status]);
}

void
Flac_decoder::next_frame(struct flacsplit::Frame &frame)
    throw (Flac_decode_error)
{
	// a seek will trigger a call to write_callback(), so don't process
	// the next frame if it hasn't been seen yet here
	if (!_last_frame || _frame_retrieved)
		if (!process_single())
			throw Flac_decode_error(get_state().as_cstring());
	if (_last_status)
		throw Flac_decode_error(_last_status);
	frame.data = _last_buffer.get();
	frame.channels = _last_frame->header.channels;
	frame.samples = _last_frame->header.blocksize;
	frame.rate = _last_frame->header.sample_rate;
	_frame_retrieved = true;
}

FLAC__StreamDecoderWriteStatus
Flac_decoder::write_callback(const FLAC__Frame *frame,
    const FLAC__int32 *const *buffer)
{
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
	_last_status = 0;
	_frame_retrieved = false;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

FLAC__StreamDecoderSeekStatus
Flac_decoder::seek_callback(FLAC__uint64 absolute_byte_offset)
{
	long off = absolute_byte_offset;
	assert(static_cast<FLAC__uint64>(off) ==
	    absolute_byte_offset);

	return fseek(_fp, off, SEEK_SET) ?
	    FLAC__STREAM_DECODER_SEEK_STATUS_ERROR :
	    FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus
Flac_decoder::tell_callback(FLAC__uint64 *absolute_byte_offset)
{
	long off = ftell(_fp);
	if (off < 0)
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = off;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus
Flac_decoder::length_callback(FLAC__uint64 *stream_length)
{
	struct stat st;
	if (fstat(fileno(_fp), &st))
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	*stream_length = st.st_size;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

Wave_decoder::Wave_decoder(const std::string &path, FILE *fp)
    throw (flacsplit::Sox_error) :
	Basic_decoder(),
	_samples(),
	_transp()
{
	Sox_init::init();
	if (!(_fmt = sox_open_read(path.c_str(), 0, 0, 0)))
		throw flacsplit::Sox_error("sox_open_read() error");

	try {
		// XXX sox-14.4.0 has sox_format_t::fp declared as void*
		assert(same_file(fp, reinterpret_cast<FILE *>(_fmt->fp)));
		_samples_len = _fmt->signal.channels * _fmt->signal.rate /
		    FRAMES_PER_SEC;
		_samples.reset(new sox_sample_t[_samples_len]);
		_transp.reset(new int32_t[_samples_len]);
		_transp_ptrs.reset(
		    new int32_t *[_fmt->signal.channels]);
	} catch (...) {
		sox_close(_fmt);
		throw;
	}
	fclose(fp);
}

void
Wave_decoder::next_frame(struct flacsplit::Frame &frame)
    throw (Wave_decode_error)
{
	size_t samples;
	samples = sox_read(_fmt, _samples.get(), _samples_len);
	if (samples < _samples_len)
		throw Wave_decode_error("sox_read() error");

	frame.data = _transp_ptrs.get();
	frame.channels = _fmt->signal.channels;
	assert(!(samples % frame.channels));
	frame.samples = samples / frame.channels;
	frame.rate = _fmt->signal.rate;

	// transpose _samples => _transp
	size_t i = 0;
	for (size_t sample = 0; sample < frame.samples; sample++) {
		size_t j = sample;
		for (size_t channel = 0; channel < frame.channels;
		    channel++) {
			SOX_SAMPLE_LOCALS;
			unsigned clips = 0;
			int32_t samp = SOX_SAMPLE_TO_SIGNED_16BIT(
			    _samples[i], clips);
			assert(!clips);
			_transp[j] = samp;
			i++;
			j += frame.samples;
		}
	}

	// make 2d array to return
	_transp_ptrs[0] = _transp.get();
	for (size_t channel = 1; channel < frame.channels; channel++)
		_transp_ptrs[channel] = _transp_ptrs[channel-1] +
		    frame.samples;
}

enum flacsplit::file_format
get_file_format(FILE *fp)
{
	const char *const RIFF = "RIFF";
	const char *const WAVE = "WAVE";
	const char *const FLAC = "fLaC";

	char buf[12];

	if (!fread(buf, sizeof(buf), 1, fp))
		return flacsplit::FF_UNKNOWN;
	fseek(fp, -sizeof(buf), SEEK_CUR);

	if (std::equal(buf, buf+4, RIFF) && std::equal(buf+8, buf+12, WAVE))
		return flacsplit::FF_WAVE;
	if (std::equal(buf, buf+4, FLAC))
		return flacsplit::FF_FLAC;
	return flacsplit::FF_UNKNOWN;
}

/** Check that two C file pointers reference the same underlying file
 * (according to the device and inode).
 * \throws flacsplit::Unix_error if there is a problem calling fstat(2) on an
 *	underlying file descriptor
 * \return whether they are the same
 */
bool
same_file(FILE *a, FILE *b) throw (flacsplit::Unix_error)
{
	struct stat st_a;
	struct stat st_b;

	if (fstat(fileno(a), &st_a))
		throw flacsplit::Unix_error("statting open file");
	if (fstat(fileno(b), &st_b))
		throw flacsplit::Unix_error("statting open file");
	return st_a.st_dev == st_b.st_dev && st_a.st_ino == st_b.st_ino;
}

} // end anon

flacsplit::Decoder::Decoder(const std::string &path, FILE *fp,
    enum file_format format) throw (Bad_format, Sox_error) :
	_decoder()
{
	if (format == FF_UNKNOWN)
		format = get_file_format(fp);
	switch (format) {
	case FF_UNKNOWN:
		throw Bad_format();
	case FF_WAVE:
		_decoder.reset(new Wave_decoder(path, fp));
		break;
	case FF_FLAC:
		_decoder.reset(new Flac_decoder(fp));
		break;
	}
}
