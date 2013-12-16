/*
 * $File: wftp_server.hh
 * $Date: Sun Dec 15 23:32:18 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#include <string>

class WFTPServer {
	int m_port = 21;
	std::string m_rootdir;

	class ClientHandler;
	friend class ClientHandler;

	static void worker_thread(ClientHandler *client);

	public:
		WFTPServer();

		void set_rootdir(const char *dir);

		void set_port(int port) {
			m_port = port;
		}

		void serve_forever();
};

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

