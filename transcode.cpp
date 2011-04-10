#include <cuetools/cuefile.h>

#include "transcode.hpp"

flacsplit::Music_info::Music_info(const Cdtext *cdtext0) :
	_parent(0),
	_track(0)
{
	Cdtext *cdtext = const_cast<Cdtext *>(cdtext0);

	const char *album = cdtext_get(PTI_TITLE, cdtext);
	if (album) _title = album;

	const char *artist = cdtext_get(PTI_PERFORMER, cdtext);
	if (artist) _artist = artist;

	const char *genre = cdtext_get(PTI_GENRE, cdtext);
	if (genre) _genre = genre;
}

flacsplit::Music_info::Music_info(const Cdtext *cdtext0,
    const Music_info &parent, uint8_t track) :
	_parent(&parent),
	_track(track)
{
	Cdtext *cdtext = const_cast<Cdtext *>(cdtext0);

	const char *artist = cdtext_get(PTI_PERFORMER, cdtext);
	if (artist && artist != parent._artist) _artist = artist;

	const char *genre = cdtext_get(PTI_GENRE, cdtext);
	if (genre && genre != parent._genre) _genre = genre;

	const char *title = cdtext_get(PTI_TITLE, cdtext);
	if (title) _title = title;
};
