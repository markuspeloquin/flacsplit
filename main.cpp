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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cuetools/cuefile.h>
#include <unicode/utf8.h>

#include "decode.hpp"
#include "encode.hpp"
#include "errors.hpp"
#include "gain_analysis.hpp"
#include "replaygain_writer.hpp"
#include "sanitize.hpp"
#include "transcode.hpp"

const char *prog;

namespace flacsplit {
namespace {

class File_handle {
public:
	File_handle() : _fp(nullptr) {}
	File_handle(FILE *fp) : _fp(fp) {}

	File_handle(File_handle &&rhs) {
		std::swap(rhs._fp, _fp);
	}
	File_handle(const File_handle &) = delete;

	~File_handle() {
		if (_fp) fclose(_fp);
	}

	//! \throw flacsplit::Unix_error
	File_handle &operator=(FILE *fp) {
		close();
		_fp = fp;
		return *this;
	}

	File_handle &operator=(File_handle &&rhs) {
		std::swap(rhs._fp, _fp);
		return *this;
	}
	void operator=(const File_handle &) = delete;

	//! \throw flacsplit::Unix_error
	void close() {
		if (_fp) {
			if (fclose(_fp) != 0)
				throw_traced(Unix_error("closing file"));
			_fp = nullptr;
		}
	}

	FILE *release() {
		FILE *fp = _fp;
		_fp = nullptr;
		return fp;
	}

	operator FILE *() {
		return _fp;
	}

	operator bool() const {
		return _fp;
	}

private:
	FILE *_fp;
};

struct options {
	const std::filesystem::path	out_dir;
	bool	hidden_track;
	bool	switch_index;
	bool	use_flac;
};

template <typename In>
void		create_dirs(In begin, In end, const std::filesystem::path &);
std::string	escape_cue_string(const std::string &);
std::pair<File_handle, std::filesystem::path>
		find_file(const std::filesystem::path &, bool use_flac);
std::tuple<std::string, std::string, int64_t>
		get_cue_extra(const std::filesystem::path &);
std::pair<std::vector<std::filesystem::path>, std::filesystem::path>
		make_album_path(const flacsplit::Music_info &album);
std::string	make_track_name(const flacsplit::Music_info &track);
bool		once(const std::filesystem::path &, const struct options *);
void		transform_sample_fmt(const Frame &, double **);
void		usage(const boost::program_options::options_description &);

class Cuetools_cd {
public:
	Cuetools_cd() : _cd(nullptr) {}

	Cuetools_cd(const Cd *cd) : _cd(const_cast<Cd *>(cd)) {}

	~Cuetools_cd() {
		if (_cd) cd_delete(_cd);
	}

	Cuetools_cd &operator=(const Cd *cd) {
		if (_cd) cd_delete(_cd);
		_cd = const_cast<Cd *>(cd);
		return *this;
	}

	operator Cd *() {
		return _cd;
	}

	operator const Cd *() {
		return _cd;
	}

	operator bool() {
		return _cd;
	}

private:
	Cd *_cd;
};

//! \throw flacsplit::Unix_error
template <typename In>
void
create_dirs(In begin, In end, const std::filesystem::path &out_dir) {
	std::filesystem::path cur_dir;
	bool first = true;
	while (begin != end) {
		if (first) {
			if (out_dir.empty())
				cur_dir = *begin;
			else
				cur_dir = out_dir / *begin;
			first = false;
		} else
			cur_dir /= *begin;
		++begin;

		struct stat st;
		if (stat(cur_dir.c_str(), &st) == -1) {
			if (errno != ENOENT) {
				std::ostringstream eout;
				eout << "stat " << cur_dir << " failed";
				throw_traced(flacsplit::Unix_error(
				    eout.str()
				));
			}
		} else if (S_ISDIR(st.st_mode))
			continue;

		mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		if (mkdir(cur_dir.c_str(), mode) == -1) {
			std::ostringstream eout;
			eout << "mkdir `" << cur_dir << "' failed";
			throw_traced(flacsplit::Unix_error(eout.str()));
		}
	}
}

// if str[0] is '\'' or '"', the resulting string will stop at the first
// unterminated corresponding quote mark; a '\\' merely copies the following
// character uninterpreted (and if '\\' is the last character of 'str', it is
// copied itself)
std::string
escape_cue_string(const std::string &str) {
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

std::pair<File_handle, std::filesystem::path>
find_file(const std::filesystem::path &src_path, bool use_flac) {
	const char *guesses[] = { ".wav", ".flac" };
	size_t num_guesses = sizeof(guesses) / sizeof(*guesses);
	if (use_flac) std::swap(guesses[0], guesses[1]);

	for (size_t i = 0; i < num_guesses; i++) {
		auto &suf = guesses[i];

		auto guess = src_path;
		guess.replace_extension(suf);
		File_handle fp = fopen(guess.c_str(), "rb");
		if (fp || errno != ENOENT)
			return std::make_pair(std::move(fp), guess);
	}

	if (errno != ENOENT)
		throw_traced(std::runtime_error("bad errno"));
	return std::make_pair(File_handle{}, src_path);
}

#if 0
std::string
frametime(int64_t frames) {
	int64_t seconds = frames / 75;
	frames -= seconds * 75;
	int64_t minutes = seconds / 60;
	seconds -= minutes * 60;

	std::ostringstream out;
	out << std::setfill('0')
	    << std::setw(2) << minutes << ':'
	    << std::setw(2) << seconds << ':'
	    << std::setw(2) << frames;
	return out.str();
}
#endif

//! \throw flacsplit::Unix_error
std::tuple<std::string, std::string, int64_t>
get_cue_extra(const std::filesystem::path &p) {
	std::ifstream in(p.c_str());
	if (!in) {
		std::ostringstream out;
		out << "opening `" << p << '\'';
		throw_traced(flacsplit::Unix_error(out.str()));
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
			out << "reading `" << p << '\'';
			throw_traced(flacsplit::Unix_error(out.str()));
		} else if (in.eof())
			break;

		if (line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);

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
			offset = strtoul(soff.c_str(), &endptr, 10);
			if (errno || *endptr) {
				if (!errno) errno = EINVAL;
				throw_traced(flacsplit::Unix_error(
				    "bad offset value"
				));
			}
		}
	}

	return std::make_tuple(
	    flacsplit::iso8859_to_utf8(escape_cue_string(genre)),
	    flacsplit::iso8859_to_utf8(escape_cue_string(date)),
	    offset
	);
}

std::pair<std::vector<std::filesystem::path>, std::filesystem::path>
make_album_path(const flacsplit::Music_info &album) {
	std::vector<std::filesystem::path> path_vec;

	path_vec.push_back(sanitize(album.artist()));
	auto &album_name = album.album();
	if (album_name.empty())
		path_vec.push_back("no album");
	else
		path_vec.push_back(sanitize(album_name));

	return std::make_pair(path_vec, path_vec[0] / path_vec[1]);
}

std::string
make_track_name(const flacsplit::Music_info &track) {
	std::ostringstream nameout;
	nameout << std::setfill('0') << std::setw(2)
	    << static_cast<int>(track.track())
	    << ' ' << sanitize(track.title());
	return nameout.str();
}

struct track_offset {
	int64_t begin;
	int64_t end;
	int64_t pregap;
	unsigned track_number;
};

bool
once(const std::filesystem::path &cue_path, const struct options *options) {
	using namespace flacsplit;

	auto cue_dir = cue_path.parent_path();
	auto [genre, date, offset] = get_cue_extra(cue_path);

	int fmt = CUE;
	Cuetools_cd cd = cf_parse(const_cast<char *>(cue_path.c_str()), &fmt);
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

	std::vector<std::shared_ptr<Music_info>> track_info;
	std::vector<track_offset> offsets;
	unsigned tracks = cd_get_ntrack(cd);
	for (unsigned i = 0; i < tracks; i++) {
		Track *track = cd_get_track(cd, i+1);
		if (track_get_mode(track) != MODE_AUDIO) {
			if (i == tracks-1) break;

			// this is possible, but I won't handle it
			throw_traced(std::runtime_error(
			    "mixed track types... screw this"
			));
		}

		int32_t begin = track_get_start(track);
		int32_t end = track_get_length(track);
		if (end) end += begin;
		int32_t pregap = track_get_index(track, 1);

		if (!i && pregap && options->hidden_track) {
			// XXX I am calling this track 0, which won't work
			// right if there are multiple disks and this is not
			// on the first; because I don't like the concept of
			// disks
			track_info.push_back(Music_info::create_hidden(
			    album_info));
			offsets.push_back(track_offset{0, pregap, 0, 0});
			begin += pregap;
			pregap = 0;
		}

		track_info.push_back(std::make_shared<Music_info>(
		    track_get_cdtext(track), album_info,
		    offset + i + 1));

		offsets.push_back(track_offset{begin, end, pregap, i+1});
	}

	// shift pregaps into preceding tracks
	if (!options->switch_index)
		for (size_t i = 0; i < offsets.size(); i++) {
			if (i)
				offsets[i].begin += offsets[i].pregap;
			if (i != offsets.size()-1)
				offsets[i].end += offsets[i+1].pregap;
		}

	auto [dir_components, dir_path] = make_album_path(album_info);
	create_dirs(dir_components.begin(), dir_components.end(),
	    options->out_dir);

	// construct base of output pathnames
	if (!options->out_dir.empty())
		dir_path = options->out_dir / dir_path;

	std::filesystem::path src_path;
	std::unique_ptr<Decoder> decoder;
	std::vector<std::filesystem::path> out_paths;

	// for replaygain analysis
	replaygain::Sample_accum		rg_accum;
	std::unique_ptr<replaygain::Analyzer>	rg_analyzer;
	std::unique_ptr<double>			rg_samples;
	double	*double_samples[] = { nullptr, nullptr };
	int	dimens[] = { 0, 0 };

	std::unique_ptr<Replaygain_stats> gain_stats(
	    new Replaygain_stats[offsets.size()]);

	for (size_t i = 0; i < offsets.size(); i++) {
		auto offset = offsets[i];

		unsigned track_number = offset.track_number;
		Track *track = track_number ? cd_get_track(cd, track_number) :
		    cd_get_track(cd, 1);
		std::filesystem::path cur_path = cue_dir;
		cur_path /= track_get_filename(track);

		if (!decoder || cur_path != src_path) {
			// switch file

			src_path = cur_path;
			auto [in_file, derived_path] = find_file(
			    src_path, options->use_flac
			);
			if (!in_file) {
				std::cerr << prog << ": open "
				    << derived_path
				    << " failed: " << strerror(errno)
				    << '\n';
				return false;
			}

			std::cout << "< " << derived_path.c_str() << '\n';

			try {
				decoder.reset(new Decoder(in_file));
				in_file.release();
			} catch (const Bad_format &) {
				std::cerr << prog
				    << ": unknown format in file `"
				    << derived_path << "'\n";
				in_file.close();
				return false;
			}

			int64_t last_track_frame =
			    offsets[offsets.size()-1].begin;
			double last_track_sample = last_track_frame *
			    decoder->sample_rate() / 75.;
			if (decoder->total_samples() <= last_track_sample) {
				std::ostringstream eout;
				eout << "file `" << derived_path
				    << "' does not contain enough samples;"
				    << " expected at least "
				    << last_track_sample
				    << " but found "
				    << decoder->total_samples();
				throw_traced(Not_enough_samples(eout.str()));
			}
		}

		std::filesystem::path out_name = dir_path;
		out_name /= make_track_name(*track_info[i]);
		out_name += ".flac";
		out_paths.push_back(out_name);

		std::cout << "> " << out_name.c_str() << '\n';

		File_handle out_file;
		if (!(out_file = fopen(out_name.c_str(), "wb"))) {
			std::ostringstream out;
			out << "open " << out_name << " failed";
			throw_traced(Unix_error(out.str()));
		}

		std::shared_ptr<Encoder> encoder;

		// transcode
		int64_t samples = 0;
		int64_t track_samples = 0;
		decoder->seek_frame(offset.begin);
		do {
			bool allow_short = offset.end == 0;
			Frame frame = decoder->next_frame(allow_short);
			if (allow_short && !frame.samples)
				break;

			// with FLAC, stream properties like the sample rate
			// aren't known until after the first seek/process
			if (!track_samples) {
				double		samples;
				if (offset.end) {
					int64_t frames = offset.end - offset.begin;
					samples = frames *
					    decoder->sample_rate() / 75.;
				} else {
					double begin_sample = offset.begin *
					    decoder->sample_rate() / 75.;
					if (decoder->total_samples() <=
					    begin_sample) {
						throw_traced(std::runtime_error(
						    "beginning offset isn't "
						    "where it was expected"
						));
					}
					samples = decoder->total_samples() -
					    begin_sample;
				}
				track_samples = static_cast<int64_t>(
				    samples + .5);

				encoder.reset(new Encoder(
				    out_file,
				    *track_info[i],
				    track_samples,
				    frame.rate
				));

				rg_analyzer.reset(new replaygain::Analyzer(
				    decoder->sample_rate()));
			}

			int64_t remaining = track_samples - samples;
			if (remaining < frame.samples)
				frame.samples = remaining;
			samples += frame.samples;

			if (frame.samples > dimens[0] ||
			    frame.channels > dimens[1]) {
				// reset the buffer for conversion to double
				dimens[0] = frame.samples;
				dimens[1] = frame.channels;
				rg_samples.reset(new double[frame.samples *
				    frame.channels]);
				double_samples[0] = rg_samples.get();
				double_samples[1] = double_samples[0] +
				    frame.samples;
			}
			transform_sample_fmt(frame, double_samples);
			rg_analyzer->add(double_samples[0], double_samples[1],
			    frame.samples, frame.channels);

			encoder->add_frame(frame);
		} while (samples < track_samples);

		replaygain::Sample rg_sample;
		rg_analyzer->pop(rg_sample);
		rg_accum += rg_sample;
		gain_stats.get()[i].track_gain(rg_sample.adjustment());
		gain_stats.get()[i].track_peak(rg_sample.peak());

		if (!encoder->finish()) {
			std::cerr << prog << ": finish() failed\n";
			return false;
		}
	}

	double album_gain = rg_accum.adjustment();
	double album_peak = rg_accum.peak();

	for (unsigned i = 0; i < tracks; i++) {
		gain_stats.get()[i].album_gain(album_gain);
		gain_stats.get()[i].album_peak(album_peak);

		// I hate these stupid mode strings; "r+b" = O_RDWR, binary
		File_handle outfp(fopen(out_paths[i].c_str(), "r+b"));
		if (!outfp) {
			std::ostringstream out;
			out << "open " << out_paths[i] << " failed";
			throw_traced(Unix_error(out.str()));
		}

		Replaygain_writer writer(outfp);
		writer.add_replaygain(gain_stats.get()[i]);
		if (writer.check_if_tempfile_needed())
			std::cerr << prog << ": padding exhausted for "
			    << out_paths[i] << ", using temp file\n";
		writer.save();
	}

	return true;
}

void
transform_sample_fmt(const Frame &frame, double **out) {
	int shamt = 16 - frame.bits_per_sample;

	for (int c = 0; c < frame.channels; c++) {
		const int32_t	*channel_in = frame.data[c];
		double		*channel_out = out[c];
		for (int s = 0; s < frame.samples; s++) {
			// Scale to 16-bit.
			double samplef = static_cast<double>(channel_in[s]);
			if (shamt) samplef = ldexp(samplef, shamt);
			channel_out[s] = samplef;
		}
	}
}

void
usage(const boost::program_options::options_description &desc) {
	std::cout << "Usage: " << prog << " [OPTIONS...] CUESHEET...\n"
	    << desc;
}

} // end anon
} // end flacsplit

int
main(int argc, char **argv) {
	using namespace flacsplit;

	namespace po = boost::program_options;

	prog = *argv;

	po::options_description visible_desc("Options");
	visible_desc.add_options()
	    ("help", "show this message")
	    ("hidden_track", "interpret initial pregap as a separate track")
	    ("use_flac,f", "split a FLAC instead of WAV if available")
	    ("outdir,O", po::value<std::string>(),
		"parent directory to output to")
	    ("switch_index,i", "use INDEX 00 for splitting instead of 01 "
		"(most CD players seek to INDEX 01 instead of INDEX 00 if "
		"available, but some CDs don't play by those rules)")
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
		cuefiles = opt.as<std::vector<std::string>>();
	}

	std::string out_dir;
	{
		const po::variable_value &opt = var_map["outdir"];
		if (!opt.empty())
			out_dir = opt.as<std::string>();
	}

	bool hidden_track = !var_map["hidden_track"].empty();
	bool switch_index = !var_map["switch_index"].empty();
	bool use_flac = !var_map["use_flac"].empty();

	options opts = {out_dir, hidden_track, switch_index, use_flac};

	for (auto &cuefile : cuefiles) {
		try {
			if (!once(cuefile, &opts))
				return 1;
		} catch (const std::exception &e) {
			std::cerr << prog << ": "  << e.what() << '\n';
			const boost::stacktrace::stacktrace *st =
			    boost::get_error_info<traced>(e);
			if (st)
				std::cerr << *st << '\n';
			return 1;
		}
	}
	return 0;
}
