#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include <boost/scoped_array.hpp>
#include <boost/shared_array.hpp>

#include "replaygain.hpp"

extern const char *prog;

namespace {

// File descriptor RAII object
class scoped_fd {
public:
	scoped_fd(int fd=-1) : _fd(fd) {}
	~scoped_fd()
	{
		close();
	}
	void close()
	{
		if (_fd == -1) return;
		::close(_fd);
		_fd = -1;
	}
	int release()
	{
		int t = _fd;
		_fd = 0;
		return t;
	}
	scoped_fd &operator=(int fd)
	{
		close();
		_fd = fd;
		return *this;
	}
	operator int() const
	{
		return _fd;
	}
private:
	scoped_fd(const scoped_fd &) {}
	void operator=(const scoped_fd &) {}
	int _fd;
};

class shared_fd {
public:
	shared_fd(int fd=-1) :
		_count(fd == -1 ? 0 : new unsigned(1)),
		_fd(fd)
	{}
	shared_fd(const shared_fd &fd) :
		_count(fd._count),
		_fd(fd._fd)
	{
		if (_count) ++*_count;
	}
	shared_fd &operator=(const shared_fd &fd)
	{
		short_close();
		_count = fd._count;
		_fd = fd._fd;
		++*_count;
		return *this;
	}
	shared_fd &operator=(int fd)
	{
		if (_count) {
			if (!--*_count) {
				// closing last reference
				::close(_fd);
				if (fd != -1)
					// reuse the counter
					++*_count;
				else {
					delete _count;
					_count = 0;
				}
			} else if (fd != -1)
				_count = new unsigned(1);
			else
				_count = 0;
		} else if (fd != -1)
			_count = new unsigned(1);

		_fd = fd;
		return *this;
	}
	~shared_fd()
	{
		short_close();
	}
	void close()
	{
		if (short_close()) {
			_count = 0;
			_fd = -1;
		}
	}
	int release()
	{
		int fd = _fd;
		if (_count) {
			if (!--*_count)
				delete _count;
			_count = 0;
			_fd = -1;
		}
		return fd;
	}
	operator int() const
	{
		return _fd;
	}
private:
	bool short_close()
	{
		if (_count) {
			if (!--*_count) {
				delete _count;
				::close(_fd);
			}
			return true;
		}
		return false;
	}
	unsigned	*_count;
	int		_fd;
};

void
add_arg(std::vector<boost::shared_array<char> > &args, const std::string &str)
{
	args.push_back(boost::shared_array<char>(new char[str.size() + 1]));
	memcpy(args.back().get(), str.c_str(), str.size() + 1);
}

void
add_arg(std::vector<boost::shared_array<char> > &args, const char *str)
{
	if (str) {
		size_t len = strlen(str);
		args.push_back(boost::shared_array<char>(new char[len + 1]));
		memcpy(args.back().get(), str, len + 1);
	} else
		args.push_back(boost::shared_array<char>());
}

} // end anon

bool
flacsplit::add_replay_gain(const std::vector<std::string> &paths)
    throw (Unix_error)
{
	typedef std::vector<std::string>::const_iterator path_iterator;

	std::vector<boost::shared_array<char> > args;
	boost::scoped_array<char *> arg_buf;
	pid_t pid;

	add_arg(args, "metaflac");
	add_arg(args, "--add-replay-gain");
	for (size_t i = 0; i < paths.size(); i++)
		add_arg(args, paths[i]);
	add_arg(args, 0);

	arg_buf.reset(new char *[args.size()]);
	for (size_t i = 0; i < args.size(); i++)
		arg_buf[i] = args[i].get();

	// create a pipe to return an error through
	int pipefd[2];
	if (pipe(pipefd) == -1)
		throw Unix_error("making metaflac pipe");
	scoped_fd piperd(pipefd[0]);
	scoped_fd pipewr(pipefd[1]);

	// get current flags on write-end
	int flags;
	if ((flags = fcntl(pipewr, F_GETFD)) < 0)
		throw Unix_error("getting pipe flags");

	// add FD_CLOEXEC to write-end flags
	if (fcntl(pipewr, F_SETFD, flags | FD_CLOEXEC) == -1)
		throw Unix_error("setting FD_CLOEXEC on pipe");

	if ((pid = fork()) == -1)
		throw Unix_error("forking for metaflac");
	else if (!pid) {
		piperd.close();
		execvp(arg_buf[0], arg_buf.get());

		// return errno through the pipe
		int e = errno;
		if (write(pipewr, &e, sizeof(e)) <
		    static_cast<int>(sizeof(e))) {
			std::cerr << prog << ": exec `metaflac' failed: "
			    << strerror(e) << '\n';
		}
		pipewr.close();
		exit(0x7f);
	}
	pipewr.close();

	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		int ret = WEXITSTATUS(status);
		if (ret) {
			if (ret == 0x7f) {
				int e;
				// grab errno off of pipe
				if (read(piperd, &e, sizeof(e)) <
				    static_cast<int>(sizeof(e)))
					throw Unix_error(
					    "exec `metaflac' failed and "
					    "nothing actually works", 0);
				throw Unix_error("exec `metaflac' failed", e);
			}
			std::cerr << prog << ": `metaflac' exited with "
			    << ret << '\n';
			return false;
		}
	} else {
		int sig = WTERMSIG(status);
		std::cerr << prog << ": `metaflac' terminated with signal "
		    << sig << '\n';
		return false;
	}

	return true;
}
