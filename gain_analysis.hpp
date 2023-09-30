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

#include "errors.hpp"
#include "gain_analysis.h"

namespace flacsplit {
namespace replaygain {

/** A sample of a Replaygain calculation */
class Sample {
public:
	/** Real initialization comes from Analyzer::pop(). */
	Sample() : _dirty(true) {}

	/** How much to adjust by
	 *
	 * The result is undefined unless initialized with
	 * <code>Analyzer::pop()</code>.
	 *
	 * \return	The adjustment
	 * \throw Not_enough_samples	...
	 */
	double adjustment() const {
		double	v;
		if (_dirty) {
			v = replaygain_adjustment(&_value);
			const_cast<Sample *>(this)->_cached = v;
			const_cast<Sample *>(this)->_dirty = false;
		} else
			v = _cached;
		if (v == GAIN_NOT_ENOUGH_SAMPLES)
			throw_traced(Not_enough_samples());
		return v;
	}

	double peak() const {
		return replaygain_peak(&_value);
	}

private:
	friend class Analyzer;
	friend class Sample_accum;

	struct replaygain_value	_value;
	double			_cached;
	bool			_dirty;
};

/** An accumulation of a number of samples */
class Sample_accum {
public:
	/** Construct and initialize to zero */
	Sample_accum() {
		reset();
	}

	/** Reset the sum to zero */
	void reset() {
		_dirty = true;
		memset(&_sum, 0, sizeof(_sum));
	}

	/** Combine result of one sample with another
	 *
	 * \param value	The value to add
	 */
	void add(const Sample &value) {
		*this += value;
	}

	/** Combine result of one sample with another
	 *
	 * \param value	The value to add
	 */
	Sample_accum &operator+=(const Sample &value) {
		replaygain_accum(&_sum, &value._value);
		return *this;
	}

	/** How much to adjust by
	 *
	 * The result is undefined unless initialized with
	 * <code>Analyzer::pop()</code>.
	 *
	 * \return	The adjustment
	 * \throw Not_enough_samples	...
	 */
	double adjustment() const {
		double	v;
		if (_dirty) {
			v = replaygain_adjustment(&_sum);
			const_cast<Sample_accum *>(this)->_cached = v;
			const_cast<Sample_accum *>(this)->_dirty = false;
		} else
			v = _cached;
		if (v == GAIN_NOT_ENOUGH_SAMPLES)
			throw_traced(Not_enough_samples());
		return v;
	}

	double peak() const {
		return replaygain_peak(&_sum);
	}

private:
	struct replaygain_value	_sum;
	double			_cached;
	bool			_dirty;
};

/** An analyzing context */
class Analyzer {
public:
	/** Construct the analyzer object
	 *
	 * \param samplefreq	The input sample frequency
	 * \throw Bad_samplefreq
	 */
	Analyzer(unsigned long freq) :
		_ctx(nullptr)
	{
		enum replaygain_status	status;
		_ctx = replaygain_alloc(freq, &status);
		switch (status) {
		case REPLAYGAIN_ERR_MEM:
			flacsplit::throw_traced(std::bad_alloc());
		case REPLAYGAIN_ERR_SAMPLEFREQ:
			throw_traced(Bad_samplefreq());
		case REPLAYGAIN_OK:
			break;
		case REPLAYGAIN_ERROR:
		default:
			flacsplit::throw_traced(std::runtime_error(
			    "invalid status"
			));
		}
	}

	~Analyzer() {
		replaygain_free(_ctx);
	}

	/** Reset the sampling frequency
	 *
	 * \param freq	    The frequency to reset to
	 * \retval false    Bad sample frequency
	 */
	bool reset_sample_frequency(unsigned long freq) {
		return replaygain_reset_frequency(_ctx, freq) ==
		    REPLAYGAIN_OK;
	}

	/** Accumulate samples into a calculation
	 *
	 * The range of the samples should be is [-32767.0,32767.0].
	 *
	 * \param left_samples	Samples for the left (or mono) channel
	 * \param right_samples	Samples for the right channel; ignored for
	 *	single-channel
	 * \param num_samples	Number of samples
	 * \param num_channels	Number of channels
	 * \retval false	Bad number of channels or some exceptional
	 *	event
	 */
	bool add(const double *left_samples, const double *right_samples,
	    size_t num_samples, unsigned num_channels) {
		enum replaygain_status	status;

		status = replaygain_analyze(_ctx, left_samples,
		    right_samples, num_samples, num_channels);
		return status == REPLAYGAIN_OK;
	}

	/** Return current calculation, reset context
	 *
	 * \param[out] out	The accumulated Replaygain value
	 */
	void pop(Sample &out) {
		replaygain_pop(_ctx, &out._value);
	}

private:
	// no copying
	Analyzer(const Analyzer &) {}
	void operator=(const Analyzer &) {}

	struct replaygain_ctx	*_ctx;
};

}
}

#endif
