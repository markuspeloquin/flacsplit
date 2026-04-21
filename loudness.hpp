#ifndef GAIN_ANALYSIS_HPP
#define GAIN_ANALYSIS_HPP

#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <vector>

namespace flacsplit {
namespace replaygain {

const double EBUR128_REFERENCE = -18.0;

struct Ebur128_error : std::exception {
	Ebur128_error(int errnum) : Ebur128_error("ebur128", errnum) {}

	Ebur128_error(const std::string &msg, int errnum) :
		errnum(errnum),
		msg(std::format("{}: {}", msg, message(errnum))) {}

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
