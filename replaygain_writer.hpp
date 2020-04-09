#ifndef FLACSPLIT_REPLAYGAIN_WRITER_HPP
#define FLACSPLIT_REPLAYGAIN_WRITER_HPP

#include <memory>

namespace FLAC {
	namespace Metadata {
		class VorbisComment;
	}
}

namespace flacsplit {

class Replaygain_writer_impl;

class Replaygain_stats {
public:
	double album_gain() const
	{
		return _album_gain;
	}
	void album_gain(double album_gain)
	{
		_album_gain = album_gain;
	}

	double album_peak() const
	{
		return _album_peak;
	}
	void album_peak(double album_peak)
	{
		_album_peak = album_peak;
	}

	double reference_loudness() const
	{
		return 89.0;
	}

	double track_gain() const
	{
		return _track_gain;
	}
	void track_gain(double track_gain)
	{
		_track_gain = track_gain;
	}

	double track_peak() const
	{
		return _track_peak;
	}
	void track_peak(double track_peak)
	{
		_track_peak = track_peak;
	}

private:
	double _album_gain;
	double _album_peak;
	double _track_gain;
	double _track_peak;
};

class Replaygain_writer {
public:
	Replaygain_writer(FILE *);
	~Replaygain_writer();
	void add_replaygain(const flacsplit::Replaygain_stats &);
	bool check_if_tempfile_needed() const;
	void save();

private:
	std::unique_ptr<Replaygain_writer_impl> _impl;
};

void	append_replaygain_tags(FLAC::Metadata::VorbisComment &comment,
	    const Replaygain_stats &gain_stats);

void	delete_replaygain_tags(FLAC::Metadata::VorbisComment &comment);

}

#endif
