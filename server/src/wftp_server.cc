/*
 * $File: wftp_server.cc
 * $Date: Mon Dec 16 21:41:01 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#include "wftp_server.hh"
#include "common.hh"
#include "socket.hh"
#include "cmdparser.hh"
#include "util.hh"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <map>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

class WFTPServer::ClientHandler {
	bool m_pasv_mode = false;
	WFTPServer &m_server;
	CMDParser m_parser;
	std::shared_ptr<SocketBase> m_ctrl;
	std::shared_ptr<ServerSocket> m_data_srv;
	std::string m_working_dir = "/";
	CMDPair m_cur_cmd;
	int m_cli_id;
	char m_buf[1024 * 1024];

	class ClientExit { };
	class AbortCurrentFTPCommand { };

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
		m_data_srv = std::make_shared<ServerSocket>(0);
		m_data_srv->enable_timeout();
		auto port = m_data_srv->local_port();
		m_parser.send("227", ssprintf(
					"Entering Passive Mode (%s,%d,%d).",
					SocketBase::format_addr(addr, ',').c_str(),
					port >> 8, port & 0xFF));
	}

	// QUIT
	void do_quit() {
		m_parser.send("221", "Goodbye:)");
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

	// TYPE
	void do_type() {
		m_parser.send("200", "well, I always run in binary mode");
	}

	// LIST and NLST
	void do_list() {
		auto data_conn = get_data_conn("start directory listing");

		const char* opt = m_cur_cmd.cmd == "LIST" ?
			"-al --group-directories-first" : "-a --group-directories-first";
		std::string path = m_cur_cmd.arg;

		while (path[0] == '-') { // ignore all options (sent by chrome)
			for (size_t i = 0; i < path.size(); i ++)
				if (std::isspace(path[i])) {
					path.erase(0, i + 1);
					break;
				}
			if (path[0] == '-') // no space, totally opt
				path = ".";
		}
		if (path.empty())
			path = ".";
		wftp_log("client %s: list dir: %s", get_peerinfo(), path.c_str());
		path = safe_realpath(path);
		std::string ls_cmd = ssprintf("ls %s %s | tail -n +2", opt, path.c_str());

		capture_subproc_output(
			[data_conn](const void *buf, size_t size) {
				data_conn->send_crlf(static_cast<const char*>(buf), size);
			},
			[this, &ls_cmd]() {
				setenv("LC_ALL", "C", 1);
				execl("/bin/sh", "sh", "-c", ls_cmd.c_str(), nullptr);
				printf("failed to exec ls: %m\n");
				exit(-1);
			});

		close_data_conn(data_conn, "finished listing");
	}

	// CWD
	void do_cwd() {
		auto new_dir = safe_realpath(m_cur_cmd.arg);
		if (!isdir(new_dir.c_str())) {
			m_parser.send("550", "failed to chdir");
			return;
		}
		m_working_dir = new_dir;
		m_working_dir.erase(0, m_server.m_rootdir.length() - 1);
		m_parser.send("250", ssprintf("working dir changed to %s",
					m_working_dir.c_str()).c_str());

		wftp_log("client %s: chdir: %s", get_peerinfo(),
				m_working_dir.c_str());
	}

	// SIZE
	void do_size() {
		bool suc;
		std::string size,
			realpath = safe_realpath(m_cur_cmd.arg, &suc);
		if (!suc)
			size = "can not get size of " + m_cur_cmd.arg;
		else
			size = get_filesize(realpath.c_str(), &suc);
		m_parser.send(suc ? "213" : "550", size);
	}

	// RETR
	void do_retr() {
		bool suc;
		auto realpath = safe_realpath(m_cur_cmd.arg, &suc);
		FILE *fin = suc ? fopen(realpath.c_str(), "rb") : nullptr;
		if (!fin) {
			m_parser.send("550", ssprintf("failed to open `%s'", m_cur_cmd.arg.c_str()));
			return;
		}
		AutoCloser _ac(fin);
		auto data_conn = get_data_conn(
				ssprintf("going to transfer %s", m_cur_cmd.arg.c_str()));

		for (; ;) {
			auto size = fread(m_buf, 1, sizeof(m_buf), fin);
			if (size <= 0)
				break;
			data_conn->send(m_buf, size);
		}
		close_data_conn(data_conn, "transfer completed");
	}

	// ALLO
	void do_allo() {
		m_parser.send("202", "ALLO is superfluous");
	}

	// STOR
	void do_stor() {
		bool suc;
		auto realpath = safe_realpath(m_cur_cmd.arg, &suc, true);
		FILE *fout = suc ? fopen(realpath.c_str(), "wb") : nullptr;
		if (!fout) {
			m_parser.send("553", ssprintf("failed to open `%s' for write",
						m_cur_cmd.arg.c_str()));
			return;
		}
		AutoCloser _ac(fout);
		auto data_conn = get_data_conn("OK to transfer");
		off_t tot_size = 0;
		for (; ;) {
			auto size = data_conn->recv(m_buf, sizeof(m_buf));
			if (size <= 0)
				break;
			tot_size += size;
			fwrite(m_buf, 1, size, fout);
		}
		wftp_log("recevied file `%s', size=%llu", realpath.c_str(),
				(unsigned long long)tot_size);
		close_data_conn(data_conn, "transfer complete");
	}

	// DELE and RMD
	void do_remove() {
		bool suc;
		auto realpath = safe_realpath(m_cur_cmd.arg, &suc);
		if (!suc) {
			m_parser.send("550", "file not found");
			return;
		}
		int rst;
		if (m_cur_cmd.cmd == "DELE")
			rst = unlink(realpath.c_str());
		else
			rst = rmdir(realpath.c_str());
		if (rst)
			m_parser.send("550", ssprintf("failed to delete `%s': %m",
						m_cur_cmd.arg.c_str()));
		else
			m_parser.send("250", ssprintf("delete `%s' ok",
						m_cur_cmd.arg.c_str()));
	}

	// MKD
	void do_mkd() {
		bool suc;
		auto realpath = safe_realpath(m_cur_cmd.arg, &suc, true);
		if (!suc) {
			m_parser.send("550", "parent directory not exist");
			return;
		}
		if (mkdir(realpath.c_str(), 0755))
			m_parser.send("550", ssprintf("failed to mkdir `%s': %m",
					realpath.c_str()));
		else
			m_parser.send("257", "mkdir OK");
	}

	void close_data_conn(std::shared_ptr<SocketBase> socket, const char *msg) {
		m_parser.send("226", msg);
		socket->close();
	}

	std::string safe_realpath(const std::string &fpath,
			bool *successful = nullptr, bool allow_nonexist_file = false) {
		if (successful)
			*successful = false;
		auto &rootdir = m_server.m_rootdir;
		std::string dirname, basename, realpath_query;
		if (allow_nonexist_file) {
			basename = fpath;
			for (int i = fpath.length() - 1; i >= 0; i --)
				if (fpath[i] == '/') {
					dirname.assign(fpath.c_str(), i);
					basename.assign(fpath.c_str() + i);
					break;
				}
			realpath_query = dirname;
		} else
			realpath_query = fpath;
		char *rst;
		if (realpath_query[0] == '/')
			rst = realpath((rootdir + realpath_query).c_str(), nullptr);
		else
			rst = realpath((rootdir + m_working_dir + "/" + realpath_query)
					.c_str(), nullptr);
		if (!rst)
			return m_server.m_rootdir;
		std::string ret(rst);
		free(rst);
		if (ret.substr(0, rootdir.length()) != rootdir &&
				ret != rootdir.substr(0, rootdir.length() - 1))
			ret = rootdir;
		else {
			if (successful)
				*successful = true;
			if (allow_nonexist_file) {
				if (ret.back() != '/')
					ret.append("/");
				ret.append(basename);
			}
		}
		return ret;
	}

	std::shared_ptr<SocketBase> get_data_conn(const std::string &msg) {
		if (!m_pasv_mode) {
			m_parser.send("425", "use PASV first");
			throw AbortCurrentFTPCommand();
		}
		auto rst = m_data_srv->accept();
		m_parser.send("125", msg);
		rst->enable_timeout();
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
			{"NLST", &ClientHandler::do_list},
			{"TYPE", &ClientHandler::do_type},
			{"CWD", &ClientHandler::do_cwd},
			{"SIZE", &ClientHandler::do_size},
			{"RETR", &ClientHandler::do_retr},
			{"ALLO", &ClientHandler::do_allo},
			{"STOR", &ClientHandler::do_stor},
			{"DELE", &ClientHandler::do_remove},
			{"RMD", &ClientHandler::do_remove},
			{"MKD", &ClientHandler::do_mkd},
		};
		m_cur_cmd = m_parser.recv();
		wftp_log("got command from %s: %s %s", get_peerinfo(),
				m_cur_cmd.cmd.c_str(), m_cur_cmd.arg.c_str());
		auto hdl = HANDLER_MAP.find(m_cur_cmd.cmd);
		if (hdl != HANDLER_MAP.end())
			(this->*(hdl->second))();
		else {
			wftp_log("unknown command: %s", m_cur_cmd.cmd.c_str());
			m_parser.send("502",
					ssprintf("command %s unimplemented", m_cur_cmd.cmd.c_str()));
		}
	}

	public:
		ClientHandler(WFTPServer &server,
				const std::shared_ptr<SocketBase> &socket,
				int cli_id = -1):
			m_server(server), m_parser(socket), m_ctrl(socket),
			m_cli_id(cli_id)
		{
			m_ctrl->enable_timeout();
			wftp_log("new client: %s [as %s]", m_ctrl->get_peerinfo(),
					get_peerinfo());
		}

		void run() {
			try {
				m_parser.send("220", WFTP_NAME);
				for (; ;) {
					try {
						handle_cmd();
					} catch (AbortCurrentFTPCommand) {
					}
				}
			} catch (ClientExit) {
			}
		}

		const char *get_peerinfo() const {
			if (m_cli_id >= 0) {
				static thread_local char buf[20];
				sprintf(buf, "%d", m_cli_id);
				return buf;
			}
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
	if (m_rootdir.back() != '/')
		m_rootdir.append("/");
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
	wftp_log("listening on %s:%d, rootdir=%s ...",
			SocketBase::format_addr(socket.local_addr()).c_str(),
			socket.local_port(), m_rootdir.c_str());
	for (int cli_id = 0; ; cli_id ++) {
		ClientHandler *client = new ClientHandler(*this, socket.accept(), cli_id);
		std::thread sub(worker_thread, client);
		sub.detach();
	}
}


// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}
