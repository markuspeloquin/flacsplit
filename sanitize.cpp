/* Copyright (c) 2011--2012, Markus Peloquin <markus@cs.wisc.edu>
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

#include <cstdint>
#include <stdexcept>
#include <vector>

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

	// Sigur Ros exception
	if (str == "( )")
		return "Untitled";

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

	return res;
}
