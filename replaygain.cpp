#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <boost/scoped_array.hpp>
#include <boost/shared_array.hpp>

#include "replaygain.hpp"

extern const char *prog;

namespace {

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

}

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

	int pipefd[2];
	if (pipe(pipefd) == -1)
		throw Unix_error("making metaflac pipe");

	if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		throw Unix_error("making metaflac pipe");
	}

	if ((pid = fork()) == -1) {
		int e = errno;
		close(pipefd[0]);
		close(pipefd[1]);
		throw Unix_error("forking for metaflac", e);
	} else if (!pid) {
		close(pipefd[0]);
		execvp(arg_buf[0], arg_buf.get());
		int e = errno;
		if (write(pipefd[1], &e, sizeof(e)) <
		    static_cast<int>(sizeof(e))) {
			std::cerr << prog << ": exec `metaflac' failed: "
			    << strerror(e) << '\n';
		}
		close(pipefd[1]);
		exit(0x7f);
	}
	close(pipefd[1]);

	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		int ret = WEXITSTATUS(status);
		if (ret) {
			if (ret == 0x7f) {
				int e;
				// grab errno off of pipe
				if (read(pipefd[0], &e, sizeof(e)) <
				    static_cast<int>(sizeof(e))) {
					close(pipefd[0]);
					throw Unix_error(
					    "exec `metaflac' failed and "
					    "nothing actually works", 0);
				}
				close(pipefd[0]);
				throw Unix_error("exec `metaflac' failed", e);
			}
			std::cerr << prog << ": `metaflac' exited with "
			    << ret << '\n';
			close(pipefd[0]);
			return false;
		}
	} else {
		int sig = WTERMSIG(status);
		std::cerr << prog << ": `metaflac' terminated with signal "
		    << sig << '\n';
		close(pipefd[0]);
		return false;
	}
	close(pipefd[0]);

	return true;
}
