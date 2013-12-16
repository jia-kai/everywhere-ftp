/*
 * $File: main.cc
 * $Date: Mon Dec 16 21:58:29 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#include "socket.hh"
#include "cmdparser.hh"

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

class AbortCurCmd { };
class Exit {};

class WFTPClient {
	std::shared_ptr<SocketBase> m_ctrl;
	CMDParser m_parser;
	char m_buf[1024 * 1024];

	/*!
	 * send a command and receive response
	 */
	CMDPair send_cmd(const std::string &cmd) {
		wftp_log("--> %s", cmd.c_str());
		m_ctrl->send((cmd + "\r\n").c_str(), cmd.length() + 2);
		return get_resp();
	}

	CMDPair get_resp() {
		auto cmd = m_parser.recv();
		if (cmd.cmd[0] != '1' && cmd.cmd[0] != '2') {
			wftp_log("bad response: %s %s",
					cmd.cmd.c_str(), cmd.arg.c_str());
			throw AbortCurCmd();
		}
		wftp_log("<-- %s %s", cmd.cmd.c_str(), cmd.arg.c_str());
		return cmd;
	}

	std::shared_ptr<SocketBase> open_pasv_data_conn() {
		auto cmd = send_cmd("PASV");
		int h0, h1, h2, h3, p0, p1;
		if (sscanf(cmd.arg.substr(cmd.arg.rfind(' ') + 1).c_str(),
					"(%d,%d,%d,%d,%d,%d)",
					&h0, &h1, &h2, &h3, &p0, &p1) != 6) {
			wftp_log("bad response for PASV: %s", cmd.arg.c_str());
			throw AbortCurCmd();
		}
		return SocketBase::connect(
				ssprintf("%d.%d.%d.%d", h0, h1, h2, h3).c_str(),
				ssprintf("%d", p0 * 256 + p1).c_str());
	}

	public:
		WFTPClient(std::shared_ptr<SocketBase> socket):
			m_ctrl(socket), m_parser(socket)
		{
			get_resp();
			send_cmd("USER anonymous");
			send_cmd("TYPE I");
		}

		void list(FILE *fout) {
			auto data_conn = open_pasv_data_conn();
			send_cmd("LIST");
			for (;; ) {
				size_t s = data_conn->recv(m_buf, sizeof(m_buf));
				if (s <= 0)
					break;
				fwrite(m_buf, 1, s, fout);
				fflush(fout);
			}
			get_resp();
		}

		void chdir(const std::string &dir) {
			send_cmd("CWD " + dir);
		}

		void rm(const std::string &name) {
			send_cmd("DELE " + name);
		}

		void send_file(const std::string &remote_name, FILE *fin) {
			auto data_conn = open_pasv_data_conn();
			send_cmd("STOR " + remote_name);
			for (; ; ) {
				auto size = fread(m_buf, 1, sizeof(m_buf), fin);
				if (size <= 0)
					break;
				data_conn->send(m_buf, size);
			}
			data_conn->close();
			get_resp();
		}

		void recv_file(const std::string &remote_name, FILE *fout) {
			auto data_conn = open_pasv_data_conn();
			send_cmd("RETR " + remote_name);
			for (; ; ) {
				auto size = data_conn->recv(m_buf, sizeof(m_buf));
				if (size <= 0)
					break;
				if (fwrite(m_buf, 1, size, fout) != size)
					throw WFTPError("failed to write locally: %m");
			}
			data_conn->close();
			get_resp();
		}

		void quit() {
			send_cmd("QUIT");
		}
};

static void read_user_command(std::string &cmd, std::string &arg){
	printf("wftp> ");
	if (!std::getline(std::cin, cmd))
		throw Exit();
	while (!cmd.empty() && std::isspace(cmd[0]))
		cmd.erase(cmd.begin());
	while (!cmd.empty() && std::isspace(cmd.back()))
		cmd.pop_back();
	for (size_t i = 0; i < cmd.length(); i ++)
		if (std::isspace(cmd[i])) {
			arg = cmd.substr(i + 1);
			cmd.erase(i);
		}
	while (!arg.empty() && std::isspace(arg[0]))
		arg.erase(arg.begin());
}

static void interactive_console(WFTPClient &client) {
	for (; ;) {
		std::string cmd, arg;
		try {
			read_user_command(cmd, arg);
			if (cmd == "ls")
				client.list(stdout);
			else if (cmd == "q")
				throw Exit();
			else if (cmd == "cd")
				client.chdir(arg);
			else if (cmd == "rm")
				client.rm(arg);
			else if (cmd == "put") {
				FILE *fin = fopen(arg.c_str(), "rb");
				if (!fin) {
					wftp_log("failed to open `%s': %m",
							arg.c_str());
					throw AbortCurCmd();
				}
				AutoCloser _ac(fin);
				client.send_file(basename(strdupa((arg.c_str()))), fin);
			} else if (cmd == "get") {
				FILE *fout = fopen(arg.c_str(), "wb");
				if (!fout) {
					wftp_log("failed to open `%s': %m",
							arg.c_str());
					throw AbortCurCmd();
				}
				AutoCloser _ac(fout);
				client.recv_file(arg, fout);
			}
		} catch (AbortCurCmd) {
		} catch (Exit) {
			client.quit();
			exit(0);
		}
	}
}

int main (int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
		return -1;
	}
	try {
		WFTPClient client(SocketBase::connect(argv[1], argv[2]));
		interactive_console(client);
	} catch (std::exception &exc) {
		wftp_log("unexpected exception: %s", exc.what());
	}
}

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

