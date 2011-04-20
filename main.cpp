#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <tr1/cstdint>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <cuetools/cuefile.h>
#include <unicode/utf8.h>

#include "decode.hpp"
#include "encode.hpp"
#include "errors.hpp"
#include "replaygain.hpp"
#include "transcode.hpp"

const char *prog;

namespace {

template <typename In>
void		create_dirs(In begin, In end, const std::string *)
		    throw (flacsplit::Unix_error);
std::string	escape_cue_string(const std::string &);
bool		extension(const std::string &, std::string &, std::string &);
FILE		*find_file(const std::string &, std::string &);
std::string	frametime(uint32_t);
void		get_cue_extra(const std::string &, std::string &out_genre,
		    std::string &out_date) throw (flacsplit::Unix_error);
void		make_album_path(const flacsplit::Music_info &album,
		    std::vector<std::string> &, std::string &);
void		make_track_name(const flacsplit::Music_info &track,
		    std::string &);
bool		once(const std::string &, const std::string *);
void		sanitize(const std::string &, std::string &);
void		split_path(const std::string &, std::string &, std::string &);
void		usage(const boost::program_options::options_description &);

class Cuetools_cd {
public:
	Cuetools_cd() : _cd(0) {}
	Cuetools_cd(const Cd *cd) : _cd(const_cast<Cd *>(cd)) {}
	~Cuetools_cd()
	{	if (_cd) cd_delete(_cd); }
	Cuetools_cd &operator=(const Cd *cd)
	{
		if (_cd) cd_delete(_cd);
		_cd = const_cast<Cd *>(cd);
		return *this;
	}

	operator Cd *()
	{	return _cd; }
	operator const Cd *()
	{	return _cd; }
	operator bool()
	{	return _cd; }

private:
	Cd *_cd;
};

template <typename In>
void
create_dirs(In begin, In end, const std::string *out_dir)
    throw (flacsplit::Unix_error)
{
	std::ostringstream out;
	bool first = true;
	while (begin != end) {
		if (first) {
			if (out_dir && !out_dir->empty()) {
				out << *out_dir;
				if ((*out_dir)[out_dir->size()-1] != '/')
					out << '/';
			}
			out << *begin;
			first = false;
		} else
			out << '/' << *begin;
		++begin;

		struct stat st;
		if (stat(out.str().c_str(), &st) == -1) {
			if (errno != ENOENT) {
				std::ostringstream eout;
				eout << "stat `" << out.str() << "' failed";
				throw flacsplit::Unix_error(eout.str());
			}
		} else if (S_ISDIR(st.st_mode))
			continue;

		if (mkdir(out.str().c_str(),
		    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
			std::ostringstream eout;
			eout << "mkdir `" << out.str() << "' failed";
			throw flacsplit::Unix_error(eout.str());
		}
	}
}

// if str[0] is '\'' or '"', the resulting string will stop at the first
// unterminated corresponding quote mark; a '\\' merely copies the following
// character uninterpreted (and if '\\' is the last character of 'str', it is
// copied itself)
std::string
escape_cue_string(const std::string &str)
{
	if (str.empty()) return "";

	char quote = str[0];
	size_t begin = 0;
	if (quote != '"' && quote != '\'')
		quote = '\0';
	else
		begin = 1;

	std::ostringstream out;
	for (size_t i = begin; i < str.size(); i++)
		if (str[i] == '\\')
			if (i == str.size() - 1)
				out << '\\';
			else
				out << str[++i];
		else if (str[i] == quote)
			break;
		else
			out << str[i];
	return out.str();
}

bool
extension(const std::string &str, std::string &base, std::string &ext)
{
	size_t dot = str.rfind('.');
	if (dot == std::string::npos)
		return false;
	base = str.substr(0, dot);
	ext = str.substr(dot+1);
	return true;
}

FILE *
find_file(const std::string &path, std::string &out_path)
{
	std::string	base;
	std::string	ext;
	std::string	guess;
	FILE		*fp;
	bool		have_ext;

	fp = fopen(path.c_str(), "rb");
	if (fp || errno != ENOENT) {
		out_path = path;
		return fp;
	}

	have_ext = extension(path, base, ext);
	if (have_ext) {
		if (ext != "wav") {
			guess = base + ".wav";
			fp = fopen(guess.c_str(), "rb");
			if (fp || errno != ENOENT) {
				out_path = guess;
				return fp;
			}
		}
		if (ext != "flac") {
			guess = base + ".flac";
			fp = fopen(guess.c_str(), "rb");
			if (fp || errno != ENOENT) {
				out_path = guess;
				return fp;
			}
		}
	} else {
		guess = path + ".wav";
		fp = fopen(guess.c_str(), "rb");
		if (fp || errno != ENOENT) {
			out_path = guess;
			return fp;
		}
		guess = path + ".flac";
		fp = fopen(guess.c_str(), "rb");
		if (fp || errno != ENOENT) {
			out_path = guess;
			return fp;
		}
	}

	assert(errno == ENOENT);
	out_path = path;
	return 0;
}

std::string
frametime(uint32_t frames)
{
	uint16_t seconds = frames / 75;
	frames -= seconds * 75;
	uint16_t minutes = seconds / 60;
	seconds -= minutes * 60;

	std::ostringstream out;
	out << std::setfill('0')
	    << std::setw(2) << minutes << ':'
	    << std::setw(2) << seconds << ':'
	    << std::setw(2) << frames;
	return out.str();
}

void
get_cue_extra(const std::string &path,
    std::string &out_genre, std::string &out_date, unsigned &out_offset)
    throw (flacsplit::Unix_error)
{
	std::ifstream in(path.c_str());
	if (!in) {
		std::ostringstream out;
		out << "opening `" << path << '\'';
		throw flacsplit::Unix_error(out.str());
	}

	const std::string DATE = "REM DATE ";
	const std::string GENRE = "REM GENRE ";
	const std::string OFFSET = "REM OFFSET ";

	std::string line;
	std::string genre;
	std::string date;
	unsigned offset = 0;
	for (;;) {
		std::getline(in, line);
		if (in.bad()) {
			std::ostringstream out;
			out << "reading `" << path << '\'';
			throw flacsplit::Unix_error(out.str());
		} else if (in.eof())
			break;

		if (!line.compare(0, GENRE.size(), GENRE))
			genre = line.substr(GENRE.size(),
			    line.size() - GENRE.size());
		else if (!line.compare(0, DATE.size(), DATE))
			date = line.substr(DATE.size(),
			    line.size() - DATE.size());
		else if (!line.compare(0, OFFSET.size(), OFFSET)) {
			std::string soff = line.substr(OFFSET.size(),
			    line.size() - OFFSET.size());
			soff = escape_cue_string(soff);

			char *endptr;
			errno = 0;
			offset = strtoul(soff.c_str(), &endptr, 0);
			if (errno || *endptr) {
				if (!errno) errno = EINVAL;
				throw flacsplit::Unix_error(
				    "bad offset value");
			}
		}
	}

	out_genre = flacsplit::iso8859_to_utf8(escape_cue_string(genre));
	out_date = flacsplit::iso8859_to_utf8(escape_cue_string(date));
	out_offset = offset;
}

void
make_album_path(const flacsplit::Music_info &album,
    std::vector<std::string> &out_path_vec, std::string &out_path)
{
	std::vector<std::string> path_vec;

	path_vec.push_back(album.artist());
	path_vec.push_back(album.album());
	if (path_vec.back().empty())
		path_vec.back() = "no album";

	sanitize(path_vec[0], path_vec[0]);
	sanitize(path_vec[1], path_vec[1]);

	std::ostringstream pathout;
	pathout << path_vec[0] << '/' << path_vec[1];

	std::swap(path_vec, out_path_vec);
	out_path = pathout.str();
}

void
make_track_name(const flacsplit::Music_info &track, std::string &name)
{
	std::string title;
	sanitize(track.title(), title);

	std::ostringstream nameout;
	nameout << std::setfill('0') << std::setw(2)
	    << static_cast<int>(track.track())
	    << ' ' << title;
	name = nameout.str();
}

void
sanitize(const std::string &str, std::string &out)
{
	std::vector<size_t>	guessed;
	std::string		res;
	const char	*s = str.c_str();
	int32_t		length = str.size();
	int32_t		i = 0;

	// Sigur Ros exception
	if (str == "( )") {
		out = "Untitled";
		return;
	}

	while (i < length) {
		int32_t	c;
		U8_NEXT(s, i, length, c);
		// strings should be pre-encoded in utf8
		assert(c > 0);

		if ((0x30 <= c && c < 0x3a) ||
		    (0x40 <= c && c < 0x5b) ||
		    (0x60 <= c && c < 0x7b) ||
		    c == 0x20)
			res += static_cast<char>(c);
		else if (c == 0x09)			res += ' ';
		else if (0xc0 <= c && c < 0xc6)		res += 'A';
		else if (c == 0xc6) {
			res += "Ae";
			guessed.push_back(res.size()-1);
		} else if (c == 0xc7)			res += 'C';
		else if (0xc8 <= c && c < 0xcc)		res += 'E';
		else if (0xcc <= c && c < 0xd0)		res += 'I';
		else if (c == 0xd0) {
			res += "Dh";
			guessed.push_back(res.size()-1);
		} else if (c == 0xd1)			res += 'N';
		else if (0xd2 <= c && c < 0xd7)		res += 'O';
		else if (0xd9 <= c && c < 0xdd)		res += 'U';
		else if (c == 0xdd)			res += 'Y';
		else if (c == 0xde) {
			res += "Th";
			guessed.push_back(res.size()-1);
		} else if (c == 0xdf)			res += "ss";
		else if (0xe0 <= c && c < 0xe6)		res += 'a';
		else if (c == 0xe6)			res += "ae";
		else if (c == 0xe7)			res += 'c';
		else if (0xe8 <= c && c < 0xec)		res += 'e';
		else if (0xec <= c && c < 0xf0)		res += 'i';
		else if (c == 0xf0)			res += "dh";
		else if (c == 0xf1)			res += 'n';
		else if (0xf2 <= c && c < 0xf7)		res += 'o';
		else if (0xf9 <= c && c < 0xfd)		res += 'u';
		else if (c == 0xfd || c == 0xff)	res += 'y';
		else if (c == 0xfe)			res += "th";
		else if (c == 0x152) {
			res += "Oe";
			guessed.push_back(res.size()-1);
		} else if (c == 0xf8 || c == 0x0153)	res += "oe";
		// ignore all else
	}

	// upper-case the letters with guessed cases as best as can
	// be done (e.g. AeBLAH should have been AEBLAH)
	for (std::vector<size_t>::iterator i = guessed.begin();
	    i != guessed.end(); ++i)
		if (*i < res.size() - 1 && isalpha(res[*i + 1])) {
			if (isupper(res[*i + 1]))
				res[*i] = toupper(res[*i]);
		} else if (*i && isalpha(res[*i - 1]))
			res[*i] = toupper(res[*i]);

	std::swap(out, res);
}

void
split_path(const std::string &path, std::string &dirname,
    std::string &basename)
{
	size_t slash = path.rfind("/");
	if (!slash) {
		dirname = "/";
		basename = path.substr(1);
	} else if (slash == std::string::npos) {
		dirname = "";
		basename = path;
	} else if (slash == path.size()-1)
		// path ends in '/', so strip '/' and try again
		split_path(path.substr(0, slash), dirname, basename);
	else {
		dirname = path.substr(0, slash);
		basename = path.substr(slash + 1);
	}
}

void
usage(const boost::program_options::options_description &desc)
{
	std::cout << "Usage: " << prog << " [OPTIONS...] CUESHEET...\n"
	    << desc;
}

bool
once(const std::string &cue_path, const std::string *out_dir)
{
	using namespace flacsplit;

	std::string cue_dir;
	std::string cue_name;
	split_path(cue_path, cue_dir, cue_name);

	std::string genre;
	std::string date;
	unsigned offset;
	get_cue_extra(cue_path, genre, date, offset);

	int format = CUE;
	Cuetools_cd cd = cf_parse(const_cast<char *>(cue_path.c_str()),
	    &format);
	if (!cd) {
		std::cerr << prog << ": parse failed\n";
		return false;
	}

	Music_info album_info(cd_get_cdtext(cd));
	// technically, cue sheets support GENRE cd-text; they just don't use
	// it; only overwrite if not in cd-text
	if (album_info.genre().empty())
		album_info.genre(genre);
	album_info.date(date);

	std::vector<boost::shared_ptr<Music_info> > track_info;
	std::vector<uint32_t> begin;
	std::vector<uint32_t> end;
	std::vector<uint32_t> pregap;
	unsigned tracks = cd_get_ntrack(cd);
	unsigned tracknum = 1;
	for (unsigned i = 0; i < tracks; i++) {
		Track *track = cd_get_track(cd, i+1);
		if (track_get_mode(track) != MODE_AUDIO) {
			if (i == tracks-1)
				break;
			else
				// this is possible, but I won't handle it
				assert("mixed track types... screw this" &&0);
		}

		track_info.push_back(boost::shared_ptr<Music_info>(
		    new Music_info(track_get_cdtext(track), album_info,
		    offset + tracknum++)));

		begin.push_back(track_get_start(track));
		end.push_back(track_get_length(track));
		if (end.back()) end.back() += begin.back();
		pregap.push_back(track_get_index(track, 1));
	}

	// shift pregaps into preceding tracks
	for (unsigned i = 0; i < tracks; i++) {
		if (i)
			begin[i] += pregap[i];
		if (i != tracks-1)
			end[i] += pregap[i+1];
	}

	std::vector<std::string> dir_components;
	std::string dir_path;
	make_album_path(album_info, dir_components, dir_path);
	create_dirs(dir_components.begin(), dir_components.end(), out_dir);

	// construct base of output pathnames
	if (out_dir) {
		std::ostringstream out;
		if (*out_dir != "") {
			out << *out_dir;
			if ((*out_dir)[out_dir->size()-1] != '/')
				out << '/';
		}
		out << dir_path << '/';
		dir_path = out.str();
	} else
		dir_path += '/';

	std::string path;
	std::string derived_path;
	FILE *fp = 0;
	boost::scoped_ptr<Decoder> decoder;
	bool decoded_any = false; // useless init
	std::vector<std::string> out_paths;

	for (unsigned i = 0; i < tracks; i++) {
		Track *track = cd_get_track(cd, i+1);
		std::string cur_path = cue_dir;
		if (!cur_path.empty())
			cur_path += '/';
		cur_path += track_get_filename(track);

		if (!fp || cur_path != path) {
			// switch file

			path = cur_path;
			fp = find_file(path, derived_path);
			if (!fp) {
				std::cerr << prog << ": open `"
				    << derived_path
				    << "' failed: " << strerror(errno)
				    << '\n';
				return false;
			}

			std::cout << "< " << derived_path << '\n';

			try {
				decoder.reset(new Decoder(derived_path, fp));
			} catch (Bad_format) {
				std::cerr << prog
				    << ": unknown format in file `"
				    << derived_path << "'\n";
				fclose(fp);
				return false;
			}

			decoded_any = false;
		}

		std::string out_name;
		make_track_name(*track_info[i], out_name);
		out_name = dir_path + out_name;
		out_name += ".flac";
		out_paths.push_back(out_name);

		std::cerr << "> " << out_name << '\n';

		FILE *outfp;
		if (!(outfp = fopen(out_name.c_str(), "wb"))) {
			std::ostringstream out;
			out << "open `" << out_name << "' failed";
			throw Unix_error(out.str());
		}

		boost::scoped_ptr<Encoder> encoder;
		try {
			encoder.reset(new Encoder(outfp, *track_info[i]));
		} catch (...) {
			fclose(outfp);
			throw;
		}

		// transcode
		struct Frame frame;
		uint64_t samples = 0;
		uint64_t track_samples = 0;
		decoder->seek_frame(begin[i]);
		do {
			decoder->next_frame(frame);
			if (!decoded_any) {
				// did not know sample rate before, so seek
				// failed; now that a frame was decoded, it
				// should work
				if (begin[i]) {
					decoder->seek_frame(begin[i]);
					decoder->next_frame(frame);
				}
				decoded_any = true;
			}

			if (!track_samples) {
				double		samples;
				if (end[i]) {
					uint64_t frames = end[i] - begin[i];
					samples = frames *
					    decoder->sample_rate() / 75.;
				} else {
					double begin_sample =
					    static_cast<uint64_t>(begin[i]) *
					    decoder->sample_rate() / 75.;
					samples = decoder->total_samples() -
					    begin_sample;
				}
				track_samples = static_cast<uint64_t>(
				    samples + .5);
			}

			uint64_t remaining = track_samples - samples;
			if (remaining < frame.samples)
				frame.samples = remaining;
			samples += frame.samples;

			encoder->add_frame(frame);
		} while (samples < track_samples);

		if (!encoder->finish()) {
			std::cerr << prog << ": finish() failed\n";
			return false;
		}
	}

	std::cout << "> Calculating replay-gain values\n";
	add_replay_gain(out_paths);

	return true;
}

} // end anon

int
main(int argc, char **argv)
{
	namespace po = boost::program_options;

	prog = *argv;

	po::options_description visible_desc("Options");
	visible_desc.add_options()
	    ("help", "show this message")
	    ("outdir,O", po::value<std::string>(),
		"parent directory to output to")
	    ;

	po::options_description hidden_desc;
	hidden_desc.add_options()
	    ("cuefile", po::value<std::vector<std::string> >())
	    ;

	po::positional_options_description pos_desc;
	pos_desc.add("cuefile", -1);

	// combine visible and hidden option groups into 'desc'
	po::options_description desc;
	desc.add(visible_desc).add(hidden_desc);

	// set up the parser
	po::command_line_parser parser(argc, argv);
	parser.options(desc);
	parser.positional(pos_desc);

	// finally parse the arguments, storing into var_map
	po::variables_map var_map;
	try {
		po::store(parser.run(), var_map);
	} catch (const po::unknown_option &e) {
		std::cerr << prog << ": unknown option `"
		    << e.get_option_name() << "'\n";
		return 1;
	}

	if (argc < 2) {
		usage(visible_desc);
		return 1;
	} else if (!var_map["help"].empty()) {
		usage(visible_desc);
		return 0;
	}
	
	std::vector<std::string> cuefiles;
	{
		const po::variable_value &opt = var_map["cuefile"];
		if (opt.empty()) {
			std::cerr << prog << ": missing cue sheets\n";
			usage(visible_desc);
			return 1;
		}
		cuefiles = opt.as<std::vector<std::string> >();
	}

	boost::scoped_ptr<std::string> out_dir;
	{
		const po::variable_value &opt = var_map["outdir"];
		if (!opt.empty())
			out_dir.reset(new std::string(opt.as<std::string>()));
	}

	for (std::vector<std::string>::iterator i = cuefiles.begin();
	    i != cuefiles.end(); ++i)
		if (!once(*i, out_dir.get()))
			return 1;
	return 0;
}
