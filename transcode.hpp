#ifndef TRANSCODE_HPP
#define TRANSCODE_HPP

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

struct Cdtext;

namespace flacsplit {

enum class file_format { UNKNOWN, WAVE, FLAC };

struct Frame {
	// If bits_per_sample is 16, data will consist of values in
	// [-32768, 32768).
	const int32_t *const	*data;
	int	bits_per_sample;
	int	channels;
	int64_t	samples;
	int32_t	rate;
};

class Music_info {
	Music_info();

public:
	Music_info(const Cdtext *cdtext);

	Music_info(const Cdtext *cdtext, const Music_info &parent,
	    uint8_t track);

	static std::shared_ptr<Music_info> create_hidden(
	    const Music_info &parent);

	const std::string &album() const{
		return _parent ?
		    _parent->_title :
		    _title;
	}

	const std::string &album_artist() const{
		// album: return artist
		// track: return parent artist if different, else ""
		return !_parent ? _artist :
		    _artist.empty() ? _artist : _parent->_artist;
	}

	const std::string &artist() const{
		// album: return artist
		// track: return own artist if non-empty, else parent artist
		return _artist.empty() && _parent ?
		    _parent->_artist :
		    _artist;
	}

	const std::string &date() const{
		return _date.empty() && _parent ?
		    _parent->_date :
		    _date;
	}

	void date(const std::string &date){
		_date = date;
	}

	const std::string &genre() const{
		return _genre.empty() && _parent ?
		    _parent->_genre :
		    _genre;
	}

	void genre(const std::string &genre){
		_genre = genre;
	}

	const std::string &title() const{
		return _title;
	}

	uint8_t track() const {
		return _track;
	}

private:
	const Music_info	*_parent;
	std::string		_artist;
	std::string		_date;
	std::string		_genre;
	std::string		_title;
	uint8_t			_track;
};

std::string	iso8859_to_utf8(const std::string &);

}

#endif
