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

#include <cassert>
#include <iomanip>
#include <sstream>

#include <FLAC++/encoder.h>
#include <boost/algorithm/string/predicate.hpp>
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
	void _append_replaygain_tags(double track_gain, double track_peak,
	    double album_gain, double album_peak);
	void _delete_replaygain_tags();
	virtual void set_meta(const flacsplit::Music_info &track)
	{
		set_meta(track, true);
	}
	void set_meta(const flacsplit::Music_info &, bool);

	FLAC::Metadata::Padding		_padding;
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
Flac_encoder::_append_replaygain_tags(double track_gain, double track_peak,
    double album_gain, double album_peak)
{
	using FLAC::Metadata::VorbisComment;

	std::ostringstream formatter;
	formatter << std::fixed;

	formatter << std::setprecision(2) << track_gain << " dB";
	std::string track_gain_str = formatter.str();

	formatter.str("");
	formatter << std::setprecision(8) << track_peak;
	std::string track_peak_str = formatter.str();

	formatter.str("");
	formatter << std::setprecision(2) << album_gain << " dB";
	std::string album_gain_str = formatter.str();

	formatter.str("");
	formatter << std::setprecision(8) << album_peak;
	std::string album_peak_str = formatter.str();

	_tag.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_REFERENCE_LOUDNESS", "89.0 dB"));
	_tag.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_TRACK_GAIN", track_gain_str.c_str()));
	_tag.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_TRACK_PEAK", track_peak_str.c_str()));
	_tag.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_ALBUM_GAIN", album_gain_str.c_str()));
	_tag.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_ALBUM_PEAK", album_peak_str.c_str()));
}

void
Flac_encoder::_delete_replaygain_tags()
{
	using FLAC::Metadata::VorbisComment;
	// move backwards so the entries don't get shifted on us
	for (unsigned i = _tag.get_num_comments(); i != 0;) {
		i--;
		VorbisComment::Entry entry = _tag.get_comment(i);
		if (boost::starts_with(entry.get_field_name(), "REPLAYGAIN_"))
			_tag.delete_comment(i);
	}
}

void
Flac_encoder::set_meta(const flacsplit::Music_info &track,
    bool add_replaygain_padding)
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

	if (add_replaygain_padding) {
		// use -10 for gain since this gives the field's maximum
		// length
		_append_replaygain_tags(-10.0, 0.0, -10.0, 0.0);
		unsigned pad_length = _tag.get_length();
		_delete_replaygain_tags();
		pad_length -= _tag.get_length();

		// we then must subtract 4 from the pad_length to account for
		// the padding header
		// it is possible that the written gain values are as much as
		// four bytes short, requiring anywhere from 0--4 bytes
		// padding; the minimum size of padding is 4 bytes (the
		// METADATA_BLOCK_HEADER length), so we then add 4 bytes
		//pad_length -= 4; pad_length += 4;
		_padding.set_length(pad_length);
	}

	if (_tag.get_num_comments()) {
		// using the C-style function to avoid stupid libFLAC++ bug
		FLAC__StreamMetadata *meta_comments =
		    const_cast<FLAC__StreamMetadata *>(
		    static_cast<const FLAC__StreamMetadata *>(_tag));

		FLAC__StreamMetadata *meta_padding =
		    const_cast<FLAC__StreamMetadata *>(
		    static_cast<const FLAC__StreamMetadata *>(_padding));

		FLAC__StreamMetadata *meta[] = {meta_comments, meta_padding};

		set_metadata(meta, add_replaygain_padding ? 2 : 1);
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
