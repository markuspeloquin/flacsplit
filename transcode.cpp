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

#include <iostream>

#include <cuetools/cuefile.h>
#include <unicode/utf8.h>

#include "transcode.hpp"

flacsplit::Music_info::Music_info(const Cdtext *cdtext0) :
	_parent(0),
	_track(0)
{
	Cdtext *cdtext = const_cast<Cdtext *>(cdtext0);

	const char *album = cdtext_get(PTI_TITLE, cdtext);
	if (album) _title = iso8859_to_utf8(album);

	const char *artist = cdtext_get(PTI_PERFORMER, cdtext);
	if (artist) _artist = iso8859_to_utf8(artist);

	const char *genre = cdtext_get(PTI_GENRE, cdtext);
	if (genre) _genre = iso8859_to_utf8(genre);
}

flacsplit::Music_info::Music_info(const Cdtext *cdtext0,
    const Music_info &parent, uint8_t track) :
	_parent(&parent),
	_track(track)
{
	Cdtext *cdtext = const_cast<Cdtext *>(cdtext0);

	const char *artist_cstr = cdtext_get(PTI_PERFORMER, cdtext);
	if (artist_cstr) {
		std::string artist = iso8859_to_utf8(artist_cstr);
		if (artist != parent._artist) _artist = artist;
	}

	const char *genre_cstr = cdtext_get(PTI_GENRE, cdtext);
	if (genre_cstr) {
		std::string genre = iso8859_to_utf8(genre_cstr);
		if (genre != parent._genre) _genre = genre;
	}

	const char *title = cdtext_get(PTI_TITLE, cdtext);
	if (title) _title = iso8859_to_utf8(title);
}

std::string
flacsplit::iso8859_to_utf8(const std::string &str)
{
	const char	*s = str.c_str();
	int32_t		length = str.size();
	int32_t		i = 0;
	bool		iso8859_1 = false;

	while (i < length) {
		int32_t	c;
		U8_NEXT(s, i, length, c);
		if (c < 0) {
			iso8859_1 = true;
			break;
		}
	}
	if (!iso8859_1)
		return str;

	// maximum bytes needed is 2 (+1 for null, so 3); U8_APPEND_UNSAFE
	// needs 4 bytes in the worst case, so 4 is used to avoid warnings
	std::string	res;
	char		buf[4];
	for (i = 0; i < length; i++) {
		// encode single character
		size_t len = 0;
		// c must be unsigned; conversion to unsigned always defined
		uint32_t c = static_cast<uint8_t>(str[i]);
		U8_APPEND_UNSAFE(buf, len, c);
		buf[len] = '\0';
		res += buf;
	}

	return res;
}
