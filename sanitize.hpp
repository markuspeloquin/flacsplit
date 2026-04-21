#ifndef FLACSPLIT_SANITIZE_HPP
#define FLACSPLIT_SANITIZE_HPP

#include <string>

namespace flacsplit {

/** Transliterate a string from a Latin-based alphabet to ASCII using
 * common rules.
 * \param str	a UTF8-encoded string with(out) accents
 * \returns	an ASCII transliteration of str */
std::string	sanitize(const std::string &str);

}

#endif
