/* Copyright (C) 2010 Markus Peloquin <markus@cs.wisc.edu>
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
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * The rest of gain_analysis code is LGPL. */

/**
 * Pseudo-code to process an album:
 *
 *	double			l_samples[4096];
 *	double			r_samples[4096];
 *	multigain::Sample_accum	sample_accum;
 *
 *	std::unique_ptr<multigain::Analyzer>	rg;
 *
 *	try {
 *		rg.reset(new multigain::Analyzer(44100));
 *	} catch (const multigain::Bad_samplefreq &) {
 *		// handle error
 *	}
 *	for (unsigned i = 1; i <= num_songs; i++) {
 *		multigain::Sample	sample;
 *		size_t			num_samples;
 *
 *		while ((num_samples = getSongSamples(song[i],
 *		    l_samples, r_samples)) > 0) {
 *			rg->analyze_samples(l_samples, r_samples,
 *			    num_samples, 2);
 *		}
 *
 *		rg->pop(sample);
 *		sample_accum.add(sample);
 *
 *		try {
 *			double change = sample.adjustment();
 *			std::cout << "Recommended dB change for song " << i
 *			    << ": " << change << '\n';
 *		} catch (const multigain::Not_enough_samples &) {
 *			// ...
 *		}
 *	}
 *	try {
 *		double change = sample_accum.adjustment();
 *		std::cout << "Recommended dB change for whole album: "
 *		    << change << '\n';
 *	} catch (const multigain::Not_enough_samples &) {
 *		// ...
 *	}
 */

#ifndef GAIN_ANALYSIS_HPP
#define GAIN_ANALYSIS_HPP

#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace flacsplit {
namespace replaygain {

const double EBUR128_REFERENCE = -18.0;

struct Ebur128_error : std::exception {
	Ebur128_error(int errnum) : Ebur128_error("ebur128", errnum) {}

	Ebur128_error(const std::string &msg, int errnum) : errnum(errnum) {
		std::ostringstream out;
		out << msg << ": " << message(errnum);
		this->msg = out.str();
	}

	const char *what() const noexcept override {
		return msg.c_str();
	}

	int errnum;
	std::string msg;

private:
	std::string message(int errnum);
};

/** An analyzing context. One per track. */
class Analyzer {
public:
	/** Construct the analyzer object
	 *
	 * \param samplefreq	The input sample frequency
	 * \throw Ebur128_error
	 */
	Analyzer(unsigned num_channels, unsigned long freq);

	Analyzer(Analyzer &&other) noexcept
		: _internal(other._internal) {
		other._internal = nullptr;
	}

	Analyzer(const Analyzer &) = delete;
	void operator=(const Analyzer &) = delete;

	~Analyzer();

	/** Reset the channels & sampling frequency.
	 *
	 * \param freq	The frequency to reset to
	 * \throws	Ebur128_error
	 */
	void reset(unsigned num_channels, unsigned long freq);

	/** Accumulate samples into a calculation
	 *
	 * The range of the samples should be is [-32767.0,32767.0].
	 *
	 * \param left_samples	Samples for the left (or mono) channel
	 * \param right_samples	Samples for the right channel; pass nullptr
	 *	for single-channel
	 * \param num_samples	Number of samples
	 */
	void add(const double *left_samples, const double *right_samples,
	    size_t num_samples);

	/** Current calculation.
	 *
	 * \retval out	The accumulated Replaygain value
	 * \throws	Ebur128_error
	 */
	double gain();
	double peak();

	/** Get calculation across tracks.
	 *
	 * \retval out	The accumulated Replaygain value
	 * \throws	Ebur128_error
	 */
	static double gain_multiple(std::vector<Analyzer> &);
	static double peak_multiple(std::vector<Analyzer> &);

private:
	class Internal;

	Internal *_internal;
};

}
}

#endif
