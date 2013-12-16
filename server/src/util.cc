/*
 * $File: util.cc
 * $Date: Mon Dec 16 17:17:41 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */


#include "util.hh"
#include "common.hh"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

void capture_subproc_output(
		std::function<void(const void*, size_t)> on_recv_data,
		std::function<void()> setup_child) {

	int pipefd[2];
	if (pipe(pipefd))
		throw WFTPError("pipe: %m");

	pid_t chpid = fork();
	if (chpid < 0)
		throw WFTPError("fork: %m");
	if (!chpid) {
		// in child
		close(pipefd[0]);
		if (dup2(pipefd[1], fileno(stdout)) < 0)
			throw WFTPError("dup2 stdout: %m");
		if (dup2(pipefd[1], fileno(stderr)) < 0)
			throw WFTPError("dup2 stderr: %m");
		setup_child();
		exit(0);
	}

	close(pipefd[1]);
	char buf[1024];
	for (; ;) {
		auto size = read(pipefd[0], buf, sizeof(buf));
		if (size <= 0) {
			auto w = waitpid(chpid, nullptr, 0);
			if (w == -1)
				throw WFTPError("waitpid %d: %m", chpid);
			if (w == chpid)
				break;
		}
		else
			on_recv_data(buf, size);
	}
	close(pipefd[0]);
}

std::string get_filesize(const char *fpath, bool *successful) {
	struct stat stat;
	if (::stat(fpath, &stat)) {
		if (successful)
			*successful = false;
		return ssprintf("failed to get file size: %m");
	}
	std::ostringstream oss;
	oss << stat.st_size;
	if (successful)
		*successful = true;
	return oss.str();
}

bool isdir(const char *fpath) {
	struct stat stat;
	if (::stat(fpath, &stat)) 
		return false;
	return S_ISDIR(stat.st_mode);
}

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}
