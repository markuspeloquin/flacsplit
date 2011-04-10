#ifndef ENCODE_HPP
#define ENCODE_HPP

#include <boost/scoped_ptr.hpp>

#include "errors.hpp"
#include "transcode.hpp"

namespace flacsplit {

struct Encode_error : std::exception {
	Encode_error() : std::exception() {}
};

class Basic_encoder {
public:
	Basic_encoder() {}
	virtual ~Basic_encoder() {}
	virtual void add_frame(const struct Frame &) throw (Encode_error) = 0;
	virtual void set_meta(const Music_info &) = 0;
	virtual bool finish() = 0;
};

class Encoder {
public:
	struct Bad_format : std::exception {
		virtual ~Bad_format() throw () {}
		virtual const char *what() const throw ()
		{	return "bad format"; }
	};

	Encoder(FILE *, const Music_info &track, enum file_format=FF_FLAC)
	    throw (Bad_format);

	~Encoder() {}

	void add_frame(const struct Frame &frame) throw (Encode_error)
	{
		_encoder->add_frame(frame);
	}

	bool finish()
	{
		return _encoder->finish();
	}

private:
	boost::scoped_ptr<Basic_encoder>	_encoder;
};

}

#endif
