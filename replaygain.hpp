#ifndef REPLAYGAIN_HPP
#define REPLAYGAIN_HPP

#include <string>
#include <vector>

#include "errors.hpp"

namespace flacsplit {

template <typename In>
inline bool	add_replay_gain(In, In) throw (Unix_error);
bool		add_replay_gain(const std::vector<std::string> &)
		    throw (Unix_error);

}

template <typename In>
inline bool
flacsplit::add_replay_gain(In first, In last) throw (Unix_error)
{
	return add_replay_gain(std::vector<std::string>(first, last));
}

#endif
