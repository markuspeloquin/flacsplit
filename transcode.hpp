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
	const int32_t *const	*data;
	int	channels;
	int	samples;
	int	rate;
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
