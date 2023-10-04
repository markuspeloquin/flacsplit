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

#include <FLAC/format.h>
#if FLACPP_API_VERSION_CURRENT <= 8
#	include <cerrno>
#endif
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <FLAC++/encoder.h>

#include "encode.hpp"
#include "replaygain_writer.hpp"

namespace {

// 10 sec at 44.1 kHz; I believe this is the default for flac(1); I don't want
// to adjust it for each sample rate, since the point of the seekpoints is to
// save on I/O, so increasing the sample rate should increase the seekpoints
const unsigned SEEKPOINT_SAMPLES = 441000;

class Flac_encoder :
    public FLAC::Encoder::File,
    public flacsplit::Basic_encoder {
public:
	struct Flac_encode_error : flacsplit::Encode_error {
		Flac_encode_error(const std::string &msg) : _msg(msg) {}

		const char *what() const noexcept override {
			return _msg.c_str();
		}

		std::string _msg;
	};

	Flac_encoder(FILE *fp, const flacsplit::Music_info &, uint64_t=0);

	virtual ~Flac_encoder() {
		if (_init)
			finish();
	}

	//! \throw Flac_encode_error
	void add_frame(const struct flacsplit::Frame &) override;

	bool finish() override {
		bool result = FLAC::Encoder::File::finish();
		_init = false;
		return result;
	}

protected:
	FLAC__StreamEncoderWriteStatus write_callback(
	    const FLAC__byte *, size_t, uint32_t, uint32_t) override;

	FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64) override;

	FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *) override;

private:
	virtual void set_meta(const flacsplit::Music_info &track) {
		set_meta(track, true);
	}
	void set_meta(const flacsplit::Music_info &, bool);

	FLAC__StreamMetadata *cast_metadata(FLAC::Metadata::Prototype &meta) {
		return const_cast<FLAC__StreamMetadata *>(
		    static_cast<const FLAC__StreamMetadata *>(meta));
	}

	std::unique_ptr<FLAC::Metadata::Padding>	_padding;
	std::unique_ptr<FLAC::Metadata::SeekTable>	_seek_table;
	FLAC::Metadata::VorbisComment			_tag;

	//std::vector<std::shared_ptr<FLAC::Metadata::VorbisComment::Entry>>
	//	_entries;
	FILE	*_fp;
	bool	_init;
};

Flac_encoder::Flac_encoder(FILE *fp, const flacsplit::Music_info &track,
    uint64_t total_samples) :
	FLAC::Encoder::File(),
	Basic_encoder(),
	_padding(),
	_seek_table(),
	_tag(),
	_fp(fp),
	_init(false)
{
	set_compression_level(8);
	set_do_exhaustive_model_search(true);

	if (total_samples) {
		_seek_table.reset(new FLAC::Metadata::SeekTable);
		// what braindead developer named these fucking things?
#if FLACPP_API_VERSION_CURRENT <= 8
		if (!FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(
		    cast_metadata(*_seek_table), SEEKPOINT_SAMPLES,
		    total_samples)) {
			throw_traced(flacsplit::Unix_error(ENOMEM));
		}
#else
		_seek_table->template_append_spaced_points_by_samples(
		    SEEKPOINT_SAMPLES, total_samples);
#endif
	}
	set_meta(track);
}

void
Flac_encoder::add_frame(const struct flacsplit::Frame &frame) {
	if (!_init) {
		FLAC__StreamEncoderInitStatus status;

		set_bits_per_sample(frame.bits_per_sample);
		set_channels(frame.channels);
		set_sample_rate(frame.rate);
		status = init();
		if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
			throw_traced(Flac_encode_error(
			    FLAC__StreamEncoderInitStatusString[status]
			));
		}
		_init = true;
	}
	// TODO check that channels/rate/bits_per_sample are unchanged
	if (!process(frame.data, frame.samples))
		throw_traced(Flac_encode_error(get_state().as_cstring()));
}

void
Flac_encoder::set_meta(const flacsplit::Music_info &track,
    bool add_replaygain_padding) {
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
		flacsplit::Replaygain_stats basic_gain_stats;
		basic_gain_stats.album_gain(-10.0);
		basic_gain_stats.album_peak(0.0);
		basic_gain_stats.track_gain(-10.0);
		basic_gain_stats.track_peak(0.0);

		flacsplit::append_replaygain_tags(_tag, basic_gain_stats);
		unsigned pad_length = _tag.get_length();
		flacsplit::delete_replaygain_tags(_tag);
		pad_length -= _tag.get_length();

		// we then must subtract 4 from the pad_length to account for
		// the padding header
		// it is possible that the written gain values are as much as
		// four bytes short, requiring anywhere from 0--4 bytes
		// padding; the minimum size of padding is 4 bytes (the
		// METADATA_BLOCK_HEADER length), so we then add 4 bytes
		//pad_length -= 4; pad_length += 4;
		if (!_padding)
			_padding.reset(new FLAC::Metadata::Padding);
		_padding->set_length(pad_length);
	}

	if (_tag.get_num_comments()) {
		FLAC__StreamMetadata	*meta[3];
		size_t			metalen = 0;

		if (_seek_table)
			meta[metalen++] = cast_metadata(*_seek_table);
		meta[metalen++] = cast_metadata(_tag);
		if (add_replaygain_padding)
			meta[metalen++] = cast_metadata(*_padding);

		// using the C-style function to avoid stupid libFLAC++ bug
		set_metadata(meta, metalen);
	}
}

FLAC__StreamEncoderWriteStatus
Flac_encoder::write_callback(const FLAC__byte *buffer, size_t bytes,
    uint32_t /*samples*/, uint32_t /*current_frame*/) {
	if (fwrite(buffer, bytes, 1, _fp))
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}

FLAC__StreamEncoderSeekStatus
Flac_encoder::seek_callback(FLAC__uint64 absolute_byte_offset) {
	long off = absolute_byte_offset;
	if (static_cast<FLAC__uint64>(off) !=
	    absolute_byte_offset) {
		flacsplit::throw_traced(std::runtime_error("bad offset"));
	}

	if (fseek(_fp, off, SEEK_SET))
		return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
	return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus
Flac_encoder::tell_callback(FLAC__uint64 *absolute_byte_offset) {
	long off = ftell(_fp);
	if (off < 0)
		return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = off;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

} // end anon

flacsplit::Encoder::Encoder(FILE *fp, const Music_info &track,
    uint64_t total_samples, enum file_format format) {
	if (format != file_format::FLAC)
		throw_traced(Bad_format());
	_encoder.reset(new Flac_encoder(fp, track, total_samples));
}
