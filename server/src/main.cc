/*
 * $File: main.cc
 * $Date: Mon Dec 16 19:42:21 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#include "wftp_server.hh"
#include "common.hh"

#include <cstring>
#include <cstdio>

#include <signal.h>

int main(int argc, char **argv) {
	signal(SIGPIPE, SIG_IGN);
	WFTPServer server;
	for (int i = 1; i < argc; i ++) {
		if (!strcmp(argv[i], "-h")) {
			fprintf(stderr, "usage: %s [-h] [-p port] [-d root_dir]\n",
					argv[0]);
			return 0;
		}
		else if (!strcmp(argv[i], "-p")) {
			int port;
			if (i == argc - 1 ||
					sscanf(argv[i + 1], "%d", &port) != 1)
				throw WFTPError("bad port");
			server.set_port(port);
			i ++;
		}
		else if (!strcmp(argv[i], "-d")) {
			if (i == argc - 1)
				throw WFTPError("argument required");
			server.set_rootdir(argv[i + 1]);
			i ++;
		} else
			throw WFTPError("unknown parameter: %s", argv[i]);
	}
	server.serve_forever();
}

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

