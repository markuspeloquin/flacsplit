#ifndef DECODE_HPP
#define DECODE_HPP

#include <cstdio>
#include <string>

#include <boost/scoped_ptr.hpp>

#include "errors.hpp"
#include "transcode.hpp"

namespace flacsplit {

class Sox_init {
public:
	Sox_init() : _valid(false) {}
	~Sox_init();

	static void init() throw (Sox_error)
	{
		if (!_instance._valid)
			_instance.do_init();
	}

private:
	Sox_init(const Sox_init &) {}
	void operator=(const Sox_init &) {}

	void do_init() throw (Sox_error);

	static Sox_init _instance;

	bool _valid;
};

struct Decode_error : std::exception {
	Decode_error() : std::exception() {}
};

class Basic_decoder {
public:
	Basic_decoder() throw (Decode_error) {}
	virtual ~Basic_decoder() {}
	virtual void next_frame(struct Frame &) throw (Decode_error) = 0;
	virtual void seek(uint64_t sample) throw (Decode_error) = 0;
	virtual unsigned sample_rate() const = 0;
	virtual uint64_t total_samples() const = 0;
};

class Decoder {
public:
	Decoder(const std::string &, FILE *, enum file_format=FF_UNKNOWN)
	    throw (Bad_format, Sox_error);

	~Decoder() {}

	void next_frame(struct Frame &frame) throw (Decode_error)
	{
		_decoder->next_frame(frame);
	}

	void seek(uint64_t sample) throw (Decode_error)
	{
		_decoder->seek(sample);
	}

	void seek_frame(uint64_t frame) throw (Decode_error)
	{
		seek(static_cast<uint64_t>(
		    _decoder->sample_rate() * frame / 75. + .5));
	}

	unsigned sample_rate() const
	{
		return _decoder->sample_rate();
	}

	virtual uint64_t total_samples() const
	{
		return _decoder->total_samples();
	}

private:
	boost::scoped_ptr<Basic_decoder>	_decoder;
};

}

#endif
