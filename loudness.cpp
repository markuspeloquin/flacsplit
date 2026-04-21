#include <memory>

#include <ebur128.h>

#include "errors.hpp"
#include "loudness.hpp"

namespace flacsplit::replaygain {

class Analyzer::Internal {
public:
	Internal(unsigned num_channels, unsigned long freq) {
		_state = ebur128_init(
		    num_channels, freq, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK
		);
		if (!_state) {
			flacsplit::throw_traced(std::runtime_error(
			    "ebur128_init failed"
			));
		}
	}

	~Internal() {
		ebur128_destroy(&_state);
	}

	ebur128_state *_state;
};

std::string
Ebur128_error::message(int errnum) {
	switch (error(errnum)) {
	case EBUR128_SUCCESS:
		return "success";
	case EBUR128_ERROR_NOMEM:
		return "no memory";
	case EBUR128_ERROR_INVALID_MODE:
		return "invalid mode";
	case EBUR128_ERROR_INVALID_CHANNEL_INDEX:
		return "invalid channel index";
	case EBUR128_ERROR_NO_CHANGE:
		return "no change";
	}
	return "unknown";
}

Analyzer::Analyzer(unsigned num_channels, unsigned long freq) :
	_internal(new Internal(num_channels, freq))
{}

Analyzer::~Analyzer() {
	if (_internal)
		delete _internal;
}

void
Analyzer::reset(unsigned num_channels, unsigned long freq) {
	int err = ebur128_change_parameters(
	    _internal->_state, num_channels, freq
	);
	if (err)
		flacsplit::throw_traced(Ebur128_error(err));
}

void
Analyzer::add(const double *left_samples, const double *right_samples,
    size_t num_samples) {
	if (!right_samples) {
		int err = ebur128_add_frames_double(
		    _internal->_state, left_samples, num_samples
		);
		if (err)
			flacsplit::throw_traced(Ebur128_error(err));
	}
	// Samples need to be interleaved, sadly.
	auto merged = std::make_unique<double[]>(num_samples * 2);
	auto pos = merged.get();
	for (size_t i = 0; i < num_samples; i++) {
		*pos++ = *left_samples++;
		*pos++ = *right_samples++;
	}
	int err = ebur128_add_frames_double(
	    _internal->_state, merged.get(), num_samples
	);
	if (err)
		flacsplit::throw_traced(Ebur128_error(err));
}

double
Analyzer::gain() {
	double result;
	int err = ebur128_loudness_global(_internal->_state, &result);
	if (err)
		flacsplit::throw_traced(Ebur128_error(err));
	return EBUR128_REFERENCE - result;
}

double
Analyzer::peak() {
	double left, right;
	int err;
	if ((err = ebur128_true_peak(_internal->_state, 0, &left)))
		flacsplit::throw_traced(Ebur128_error(err));
	else if ((err = ebur128_true_peak(_internal->_state, 1, &right)))
		flacsplit::throw_traced(Ebur128_error(err));
	return std::max(left, right);
}

double
Analyzer::gain_multiple(std::vector<Analyzer> &vec) {
	auto states = std::make_unique<ebur128_state *[]>(vec.size());
	int end = 0;
	for (auto &state : vec)
		states[end++] = state._internal->_state;
	double result;
	int err = ebur128_loudness_global_multiple(states.get(), end, &result);
	if (err)
		flacsplit::throw_traced(Ebur128_error(err));
	return EBUR128_REFERENCE - result;
}

double
Analyzer::peak_multiple(std::vector<Analyzer> &vec) {
	double result = 0.0;
	for (auto &state : vec)
		result = std::max(result, state.peak());
	return result;
}

}
