/*
 * $File: cmdparser.hh
 * $Date: Mon Dec 16 12:11:23 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#pragma once

#include "socket.hh"
#include "common.hh"

#include <cctype>
#include <string>

struct CMDPair {
	std::string cmd, arg;
};

/*!
 * parse ftp command (command, argument)
 */
class CMDParser {
	std::shared_ptr<SocketBase> m_socket;

	public:
		CMDParser(std::shared_ptr<SocketBase> socket):
			m_socket(socket)
		{ }

		CMDPair recv() {
			static thread_local char buf[1024];

			CMDPair rst;
			size_t size = m_socket->recv(buf, sizeof(buf));
			buf[size] = 0;
			while (size && (buf[size - 1] == '\r' || buf[size - 1] == '\n'))
				buf[-- size] = 0;
			rst.cmd.assign(buf);
			for (size_t i = 0; i < size; i ++)
				if (std::isspace(buf[i])) {
					buf[i] = 0;
					rst.cmd.assign(buf);
					rst.arg.assign(buf + i + 1);
					break;
				}
			for (auto &i: rst.cmd)
				i = std::toupper(i);
			return rst;
		}

		void send(const std::string &cmd,
				const std::string &arg = std::string()) {
			if (arg.empty())
				m_socket->send(cmd.c_str(), cmd.size());
			else {
				m_socket->send(cmd.c_str(), cmd.length());
				m_socket->send(" ", 1);
				m_socket->send(arg.c_str(), arg.size());
			}
			m_socket->send("\r\n", 2);
		}
};

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

