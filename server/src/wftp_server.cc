/*
 * $File: wftp_server.cc
 * $Date: Mon Dec 16 12:23:32 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#include "wftp_server.hh"
#include "common.hh"
#include "socket.hh"
#include "cmdparser.hh"

#include <climits>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <map>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

class WFTPServer::ClientHandler {
	bool m_pasv_mode = false;
	WFTPServer &m_server;
	CMDParser m_parser;
	std::shared_ptr<SocketBase> m_ctrl;
	ServerSocket m_data_srv;
	std::string m_working_dir = "/";

	class ClientExit { };
	class FinishHandling { };

	// FEAT
	void do_feat() {
		m_parser.send("211", "NoFeature");
	}

	// PWD
	void do_pwd() {
		m_parser.send("257", "\"" + m_working_dir + "\"");
	}

	// PASV
	void do_pasv() {
		m_pasv_mode = true;
		auto addr = m_ctrl->local_addr();
		auto port = m_data_srv.local_port();
		m_parser.send("227", ssprintf(
					"Entering Passive Mode (%s,%d,%d).",
					SocketBase::format_addr(addr, ',').c_str(),
					port >> 8, port & 0xFF));
	}

	// QUIT
	void do_quit() {
		throw ClientExit();
	}

	// USER
	void do_user() {
		m_parser.send("230", "any user is welcome");
	}

	// PASS
	void do_pass() {
		m_parser.send("230", "any password is usable");
	}

	// SYST
	void do_syst() {
		m_parser.send("215", "UNIX Type: L8");
	}

	// LIST
	void do_list() {
		auto data_conn = get_data_conn();
		m_parser.send("150", "Hohoho, directory listing is coming!");

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
			execl("/bin/sh", "sh", "-c",
					ssprintf("ls --color=always -alh %s",
						get_realpath(".").c_str()).c_str(), nullptr);
			printf("failed to exec ls: %m\n");
			exit(-1);
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
				data_conn->send_crlf(buf, size);
		}
		close(pipefd[0]);

		data_conn->close();
		m_parser.send("226", "finished listing");
	}

	std::string get_realpath(const char *fname) {
		char *rst;
		if (fname[0] == '/')
			rst = realpath((m_server.m_rootdir + fname).c_str(), nullptr);
		else
			rst = realpath((m_server.m_rootdir + m_working_dir + fname)
					.c_str(), nullptr);
		if (!rst)
			return m_server.m_rootdir;
		std::string ret(rst);
		free(rst);
		return ret;
	}

	std::shared_ptr<SocketBase> get_data_conn() {
		wftp_log("waiting data conn");
		auto rst = m_data_srv.accept();
		rst->enable_timeout();
		wftp_log("data conn peer: %s", rst->get_peerinfo());
		return rst;
	}

	void handle_cmd() {
		typedef void (ClientHandler::*handler_ptr_t)();
		static const std::map<std::string, handler_ptr_t> HANDLER_MAP = {
			{"FEAT", &ClientHandler::do_feat},
			{"PWD", &ClientHandler::do_pwd},
			{"PASV", &ClientHandler::do_pasv},
			{"QUIT", &ClientHandler::do_quit},
			{"USER", &ClientHandler::do_user},
			{"PASS", &ClientHandler::do_pass},
			{"SYST", &ClientHandler::do_syst},
			{"LIST", &ClientHandler::do_list},
		};
		auto cmd = m_parser.recv();
		wftp_log("got command from %s: %s", get_peerinfo(), cmd.cmd.c_str());
		auto hdl = HANDLER_MAP.find(cmd.cmd);
		if (hdl != HANDLER_MAP.end())
			(this->*(hdl->second))();
		else {
			wftp_log("unknown command: %s", cmd.cmd.c_str());
			m_parser.send("502",
					ssprintf("command %s unimplemented", cmd.cmd.c_str()));
		}
	}

	public:
		ClientHandler(WFTPServer &server,
				const std::shared_ptr<SocketBase> &socket):
			m_server(server), m_parser(socket), m_ctrl(socket),
			m_data_srv(0)
		{
			m_ctrl->enable_timeout();
			m_data_srv.enable_timeout();
		}

		void run() {
			try {
				m_parser.send("220", WFTP_NAME);
				for (; ;) {
					try {
						handle_cmd();
					} catch (FinishHandling) {
					}
				}
			} catch (ClientExit) {
			}
		}

		const char *get_peerinfo() const {
			return m_ctrl->get_peerinfo();
		}
};

WFTPServer::WFTPServer() {
	set_rootdir(".");
}

void WFTPServer::set_rootdir(const char *dir) {
	auto p = realpath(dir, nullptr);
	if (!p)
		throw WFTPError("failed to get realpath for `%s': %m",
				dir);
	m_rootdir.assign(p);
	free(p);
}

void WFTPServer::worker_thread(ClientHandler *client) {
	try {
		try {
			client->run();
			wftp_log("client %s exited", client->get_peerinfo());
		} catch (std::exception &exc) {
			wftp_log("client %s exit due to exception: %s",
					client->get_peerinfo(), exc.what());
		}
		delete client;
	}
	catch (std::exception &exc) {
		wftp_log("unexpected exception: %s", exc.what());
	} catch (...) {
		wftp_log("totally unexpected exception ...");
	}
}

void WFTPServer::serve_forever() {
	ServerSocket socket(m_port);
	wftp_log("listening on %s:%d ...",
			SocketBase::format_addr(socket.local_addr()).c_str(),
			socket.local_port());
	for (; ;) {
		ClientHandler *client = new ClientHandler(*this, socket.accept());
		wftp_log("connected by %s", client->get_peerinfo());
		std::thread sub(worker_thread, client);
		sub.detach();
	}
}


// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}
