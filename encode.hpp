#ifndef ENCODE_HPP
#define ENCODE_HPP

#include <cstdint>
#include <memory>

#include "errors.hpp"
#include "transcode.hpp"

namespace flacsplit {

struct Encode_error : std::exception {};

class Basic_encoder {
public:
	Basic_encoder() {}

	virtual ~Basic_encoder() {}

	//! \throw Encode_error
	virtual void add_frame(const struct Frame &) = 0;

	virtual bool finish() = 0;
};

class Encoder : public Basic_encoder {
public:
	struct Bad_format : std::exception {
		const char *what() const noexcept override {
			return "bad format";
		}
	};

	//! \throw Bad_format
	Encoder(
	    FILE *fp,
	    const Music_info &track,
	    int64_t total_samples,
	    int32_t sample_rate,
	    file_format=file_format::FLAC);

	//! \throw Encode_error
	void add_frame(const struct Frame &frame) override {
		_encoder->add_frame(frame);
	}

	bool finish() override {
		return _encoder->finish();
	}

private:
	std::unique_ptr<Basic_encoder>	_encoder;
};

}

#endif
