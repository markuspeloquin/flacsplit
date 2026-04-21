#ifndef FLACSPLIT_REPLAYGAIN_WRITER_HPP
#define FLACSPLIT_REPLAYGAIN_WRITER_HPP

#include <memory>

#include "loudness.hpp"

namespace FLAC {
	namespace Metadata {
		class VorbisComment;
	}
}

namespace flacsplit {

class Replaygain_writer_impl;

struct Replaygain_stats {
	double reference_loudness() const {
		return replaygain::EBUR128_REFERENCE;
	}

	double album_gain;
	double album_peak;
	double track_gain;
	double track_peak;
};

class Replaygain_writer {
public:
	Replaygain_writer(FILE *);

	~Replaygain_writer();

	void add_replaygain(flacsplit::Replaygain_stats);

	bool check_if_tempfile_needed() const;

	void save();

private:
	std::unique_ptr<Replaygain_writer_impl> _impl;
};

void	append_replaygain_tags(FLAC::Metadata::VorbisComment &comment,
	    Replaygain_stats);

void	delete_replaygain_tags(FLAC::Metadata::VorbisComment &comment);

}

#endif
