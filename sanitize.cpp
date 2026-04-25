#include <cstdint>
#include <stdexcept>
#include <vector>

#include <boost/algorithm/string/trim.hpp>
#include <unicode/utf8.h>

#include "errors.hpp"
#include "sanitize.hpp"

namespace {

const char *LATIN_MAP[] = {
	// latin-1
	"A", "A", "A", "A", "A", "A",
	"AE",
	"C",
	"E", "E", "E", "E",
	"I", "I", "I", "I",
	"DH",
	"N",
	"O", "O", "O", "O", "O",
	nullptr,
	"O",
	"U", "U", "U", "U",
	"Y",
	"th",
	"ss",
	"a", "a", "a", "a", "a", "a",
	"ae",
	"c",
	"e", "e", "e", "e",
	"i", "i", "i", "i",
	"dh",
	"n",
	"o", "o", "o", "o", "o",
	nullptr,
	"o",
	"u", "u", "u", "u",
	"y",
	"th",
	"y",
	// latin extended-A
	"A", "a", "A", "a", "A", "a",
	"C", "c", "C", "c", "C", "c", "C", "c",
	"D", "d", "D", "d",
	"E", "e", "E", "e", "E", "e", "E", "e", "E", "e",
	"G", "g", "G", "g", "G", "g", "G", "g",
	"H", "h", "H", "h",
	"I", "i", "I", "i", "I", "i", "I", "i", "I", "i",
	"IJ", "ij",
	"J", "j",
	"K", "k", "k",
	"L", "l", "L", "l", "L", "l", "L", "l", "L", "l",
	"N", "n", "N", "n", "N", "n", "n", "N", "n",
	"O", "o", "O", "o", "O", "o",
	"OE", "oe",
	"R", "r", "R", "r", "R", "r",
	"S", "s", "S", "s", "S", "s", "S", "s",
	"T", "t", "T", "t", "T", "t",
	"U", "u", "U", "u", "U", "u", "U", "u", "U", "u", "U", "u",
	"W", "w",
	"Y", "y", "Y",
	"Z", "z", "Z", "z", "Z", "z",
	"s"
};
const uint16_t LATIN_MAP_BEGIN = 0xc0;
const uint16_t LATIN_MAP_END = LATIN_MAP_BEGIN +
    sizeof(LATIN_MAP) / sizeof(*LATIN_MAP);

} // end anon namespace

std::string
flacsplit::sanitize(const std::string &str) {
	std::vector<size_t>	guessed;
	std::string		res;
	const char	*s = str.c_str();
	int32_t		length = str.size();
	int32_t		i = 0;

	while (i < length) {
		int32_t	c;
		U8_NEXT(s, i, length, c);
		if (c <= 0)
			throw_traced(std::runtime_error(
			    "not pre-encoded as utf8"
			));

		if (isdigit(c) || c == 0x20 ||
		    (0x41 <= c && c < 0x5b) ||
		    (0x61 <= c && c < 0x7b))
			res += static_cast<char>(c);
		else if (c == 0x09)
			res += ' ';
		else if (LATIN_MAP_BEGIN <= c && c < LATIN_MAP_END) {
			const char *s = LATIN_MAP[c - LATIN_MAP_BEGIN];
			if (s) {
				res += s;
				if (s[0] && isupper(s[1]))
					guessed.push_back(res.size()-1);
			}
		}
		// ignore all else
	}

	// lower-case the letters with guessed cases as best as can
	// be done (e.g. AErin should have been Aerin)
	for (std::vector<size_t>::iterator iter = guessed.begin();
	    iter != guessed.end(); ++iter) {
		size_t i = *iter;
		if (i != res.size() - 1 && !isupper(res[i+1]))
			res[i] = tolower(res[i]);
	}

	boost::trim(res);

	// Sigur Ros exception for '( )'.
	if (str.empty())
		return "Untitled";

	return res;
}
