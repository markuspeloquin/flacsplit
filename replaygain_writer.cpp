#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <boost/algorithm/string/predicate.hpp>

#include <FLAC++/metadata.h>

#include "replaygain_writer.hpp"

namespace flacsplit {
	class Metadata_editor;
}

namespace {

inline flacsplit::Metadata_editor *
as_editor(FLAC__IOHandle handle) {
	return reinterpret_cast<flacsplit::Metadata_editor *>(handle);
}

} // end anon

static int		metadata_editor_close(FLAC__IOHandle);
static int		metadata_editor_eof(FLAC__IOHandle);
static size_t		metadata_editor_read(void *, size_t, size_t,
			    FLAC__IOHandle);
static int		metadata_editor_seek(FLAC__IOHandle, FLAC__int64, int);
static FLAC__int64	metadata_editor_tell(FLAC__IOHandle);
static size_t		metadata_editor_write(const void *, size_t, size_t,
			    FLAC__IOHandle);


namespace flacsplit {

class Metadata_editor {
public:
	Metadata_editor(FILE *fp) :
		_callbacks(make_callbacks()),
		_fp(fp)
	{
		_chain.read(this, _callbacks);
	}

	virtual ~Metadata_editor() {}

	std::unique_ptr<FLAC::Metadata::Iterator> iterator() {
		auto iter = std::make_unique<FLAC::Metadata::Iterator>();
		iter->init(_chain);
		return iter;
	}

	bool check_if_tempfile_needed(bool use_padding) const {
		return const_cast<FLAC::Metadata::Chain &>(
		    _chain).check_if_tempfile_needed(use_padding);
	}

	FLAC::Metadata::Chain::Status status() {
		return _chain.status();
	}

	bool valid() const {
		return _chain.is_valid();
	}

	bool write(bool use_padding) {
		return _chain.write(use_padding, this, _callbacks);
	}

	virtual size_t read_callback(uint8_t *buf, size_t size, size_t nmemb) {
		return fread(buf, size, nmemb, _fp);
	}

	virtual size_t write_callback(const uint8_t *buf, size_t size,
	    size_t nmemb) {
		return fwrite(buf, size, nmemb, _fp);
	}

	virtual int seek_callback(off_t offset, int whence) {
		long loffset = offset;
		if (loffset != offset)
			throw std::runtime_error("bad narrow cast");

		return fseek(_fp, offset, whence);
	}

	virtual off_t tell_callback() const {
		return ftell(const_cast<FILE *>(_fp));
	}

	virtual int eof_callback() const {
		return feof(const_cast<FILE *>(_fp));
	}

	virtual int close_callback() {
		return 0;
	}

private:
	static ::FLAC__IOCallbacks make_callbacks() {
		::FLAC__IOCallbacks callbacks;
		callbacks.close = metadata_editor_close;
		callbacks.eof = metadata_editor_eof;
		callbacks.read = metadata_editor_read;
		callbacks.seek = metadata_editor_seek;
		callbacks.tell = metadata_editor_tell;
		callbacks.write = metadata_editor_write;
		return callbacks;
	}

	::FLAC__IOCallbacks	_callbacks;
	FLAC::Metadata::Chain	_chain;
	FILE			*_fp;
};

class Replaygain_writer_impl : public Metadata_editor {
public:
	Replaygain_writer_impl(FILE *fp) : Metadata_editor(fp) {}
	virtual ~Replaygain_writer_impl() {}

	void add_replaygain(const flacsplit::Replaygain_stats &);
	void save();

private:
	FLAC::Metadata::VorbisComment *find_comment();
};

} // end flacsplit



static int
metadata_editor_close(FLAC__IOHandle handle) {
	//std::cerr << "close()\n";

	return as_editor(handle)->close_callback();
}

static int
metadata_editor_eof(FLAC__IOHandle handle) {
	//std::cerr << "eof()\n";

	return as_editor(handle)->eof_callback();
}

static size_t
metadata_editor_read(void *ptr, size_t size, size_t nmemb,
    FLAC__IOHandle handle) {
	//std::cerr << "read(buf," << size << ',' << nmemb << ")\n";

	uint8_t *buf = reinterpret_cast<uint8_t *>(ptr);
	return as_editor(handle)->read_callback(buf, size, nmemb);
}

static int
metadata_editor_seek(FLAC__IOHandle handle, FLAC__int64 offset, int whence) {
	//std::cerr << "seek(" << offset << ',' << whence << ")\n";

	return as_editor(handle)->seek_callback(offset, whence);
}

static FLAC__int64
metadata_editor_tell(FLAC__IOHandle handle) {
	//std::cerr << "tell()\n";

	return as_editor(handle)->tell_callback();
}

static size_t
metadata_editor_write(const void *ptr, size_t size, size_t nmemb,
    FLAC__IOHandle handle) {
	//std::cerr << "write(buf," << size << ',' << nmemb << ")\n";

	const uint8_t *buf = reinterpret_cast<const uint8_t *>(ptr);
	return as_editor(handle)->write_callback(buf, size, nmemb);
}



flacsplit::Replaygain_writer::Replaygain_writer(FILE *fp) :
	_impl(new Replaygain_writer_impl(fp))
{}

flacsplit::Replaygain_writer::~Replaygain_writer() {}

void
flacsplit::Replaygain_writer::add_replaygain(
    const flacsplit::Replaygain_stats &gain_stats) {
	_impl->add_replaygain(gain_stats);
}

bool
flacsplit::Replaygain_writer::check_if_tempfile_needed() const {
	return _impl->check_if_tempfile_needed(true);
}

void
flacsplit::Replaygain_writer::save() {
	_impl->save();
}



FLAC::Metadata::VorbisComment *
flacsplit::Replaygain_writer_impl::find_comment() {
	auto iter = iterator();
	do {
		FLAC::Metadata::VorbisComment *comment =
		    dynamic_cast<FLAC::Metadata::VorbisComment *>(
		    iter->get_block());
		if (comment)
			return comment;
	} while (iter->next());
	return nullptr;
}

inline void
flacsplit::Replaygain_writer_impl::add_replaygain(
    const flacsplit::Replaygain_stats &gain_stats) {
	FLAC::Metadata::VorbisComment *comment = find_comment();
	if (!comment) {
		throw std::runtime_error(
		    "Vorbis comment should have been present");
	}

	append_replaygain_tags(*comment, gain_stats);
}

inline void
flacsplit::Replaygain_writer_impl::save() {
	write(true);
}



void
flacsplit::append_replaygain_tags(FLAC::Metadata::VorbisComment &comment,
    const flacsplit::Replaygain_stats &gain_stats) {
	using FLAC::Metadata::VorbisComment;

	std::ostringstream formatter;
	formatter << std::fixed;

	formatter << std::setprecision(2) << std::showpos
	    << gain_stats.album_gain() << " dB";
	std::string album_gain = formatter.str();

	formatter.str("");
	formatter << std::setprecision(8) << std::noshowpos
	    << gain_stats.album_peak();
	std::string album_peak = formatter.str();

	formatter.str("");
	formatter << std::setprecision(1) << std::noshowpos
	    << gain_stats.reference_loudness() << " dB";
	std::string ref_loudness = formatter.str();

	formatter.str("");
	formatter << std::setprecision(2) << std::showpos
	    << gain_stats.track_gain() << " dB";
	std::string track_gain = formatter.str();

	formatter.str("");
	formatter << std::setprecision(8) << std::noshowpos
	    << gain_stats.track_peak();
	std::string track_peak = formatter.str();

	comment.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_ALBUM_GAIN", album_gain.c_str()));
	comment.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_ALBUM_PEAK", album_peak.c_str()));
	comment.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_REFERENCE_LOUDNESS", ref_loudness.c_str()));
	comment.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_TRACK_GAIN", track_gain.c_str()));
	comment.append_comment(VorbisComment::Entry(
	    "REPLAYGAIN_TRACK_PEAK", track_peak.c_str()));
}

void
flacsplit::delete_replaygain_tags(FLAC::Metadata::VorbisComment &comment) {
	using FLAC::Metadata::VorbisComment;
	// move backwards so the entries don't get shifted on us
	for (unsigned i = comment.get_num_comments(); i != 0;) {
		i--;
		VorbisComment::Entry entry = comment.get_comment(i);
		if (boost::starts_with(entry.get_field_name(), "REPLAYGAIN_"))
			comment.delete_comment(i);
	}
}
