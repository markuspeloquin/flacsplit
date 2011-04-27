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

#include <cassert>
#include <sstream>

#include <FLAC++/encoder.h>
#include <boost/shared_ptr.hpp>

#include "encode.hpp"

namespace {

class Flac_encoder :
    public FLAC::Encoder::File,
    public flacsplit::Basic_encoder {
public:
	struct Flac_encode_error : flacsplit::Encode_error {
		Flac_encode_error(const std::string &msg) :
			flacsplit::Encode_error(),
			_msg(msg)
		{}
		virtual ~Flac_encode_error() throw () {}
		virtual const char *what() const throw ()
		{	return _msg.c_str(); }
		std::string _msg;
	};

	Flac_encoder(FILE *fp, const flacsplit::Music_info &);
	virtual ~Flac_encoder()
	{
		if (_init)
			finish();
		else
			fclose(_fp);
	}

	virtual void add_frame(const struct flacsplit::Frame &)
	    throw (Flac_encode_error);

	virtual bool finish()
	{
		return FLAC::Encoder::File::finish();
	}

protected:
#if 0
	virtual FLAC__StreamEncoderReadStatus read_callback(FLAC__byte *,
	    size_t *);
#endif

	virtual FLAC__StreamEncoderWriteStatus write_callback(
	    const FLAC__byte *, size_t, unsigned, unsigned);

	virtual FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64);

	virtual FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *);

private:
	void set_meta(const flacsplit::Music_info &);

	FLAC::Metadata::VorbisComment	_tag;

	//std::vector<boost::shared_ptr<FLAC::Metadata::VorbisComment::Entry> >
	//	_entries;
	FILE	*_fp;
	bool	_init;
};

Flac_encoder::Flac_encoder(FILE *fp, const flacsplit::Music_info &track) :
	FLAC::Encoder::File(),
	Basic_encoder(),
	_fp(fp),
	_init(false)
{
	set_compression_level(8);
	set_do_exhaustive_model_search(true);
	set_meta(track);
}

void
Flac_encoder::add_frame(const struct flacsplit::Frame &frame)
    throw (Flac_encode_error)
{
	if (!_init) {
		FLAC__StreamEncoderInitStatus status;

		set_channels(frame.channels);
		set_sample_rate(frame.rate);
		status = init(_fp);
		if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
			throw Flac_encode_error(
			    FLAC__StreamEncoderInitStatusString[status]);
		_init = true;
	}
	if (!process(frame.data, frame.samples))
		throw Flac_encode_error(get_state().as_cstring());
}

void
Flac_encoder::set_meta(const flacsplit::Music_info &track)
{
	using FLAC::Metadata::VorbisComment;

	const std::string &album = track.album();
	const std::string &album_artist = track.album_artist();
	const std::string &artist = track.artist();
	const std::string &date = track.date();
	const std::string &genre = track.genre();
	const std::string &title = track.title();
	uint8_t tracknum = track.track();

	if (!album.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "ALBUM", album.c_str()));
	if (!album_artist.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "ALBUM ARTIST", album_artist.c_str()));
	if (!artist.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "ARTIST", artist.c_str()));
	if (!date.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "DATE", date.c_str()));
	if (!genre.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "GENRE", genre.c_str()));
	if (!title.empty())
		_tag.append_comment(VorbisComment::Entry(
		    "TITLE", title.c_str()));
	if (tracknum) {
		std::ostringstream out;
		out << static_cast<int>(tracknum);
		_tag.append_comment(VorbisComment::Entry(
		    "TRACKNUMBER", out.str().c_str()));
	}


	if (_tag.get_num_comments()) {
		// using the C-style function to avoid stupid libFLAC++ bug
		FLAC__StreamMetadata *meta =
		    const_cast<FLAC__StreamMetadata *>(
		    static_cast<const FLAC__StreamMetadata *>(_tag));
		set_metadata(&meta, 1);
	}
}

#if 0
FLAC__StreamEncoderReadStatus
Flac_encoder::read_callback(FLAC__byte *buffer, size_t *bytes)
{
	//FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE
	//FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM
	//FLAC__STREAM_ENCODER_READ_STATUS_ABORT
	//FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED
	return FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED;
}
#endif

FLAC__StreamEncoderWriteStatus
Flac_encoder::write_callback(const FLAC__byte *buffer, size_t bytes,
    unsigned /*samples*/, unsigned /*current_frame*/)
{
	if (fwrite(buffer, bytes, 1, _fp))
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}

FLAC__StreamEncoderSeekStatus
Flac_encoder::seek_callback(FLAC__uint64 absolute_byte_offset)
{
	long off = absolute_byte_offset;
	assert(static_cast<FLAC__uint64>(off) ==
	    absolute_byte_offset);

	return fseek(_fp, off, SEEK_SET) ?
	    FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR :
	    FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus
Flac_encoder::tell_callback(FLAC__uint64 *absolute_byte_offset)
{
	long off = ftell(_fp);
	if (off < 0)
		return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = off;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

} // end anon

flacsplit::Encoder::Encoder(FILE *fp, const Music_info &track,
    enum file_format format) throw (Bad_format)
{
	if (format != FF_FLAC)
		throw Bad_format();
	_encoder.reset(new Flac_encoder(fp, track));
}
