/*
 * $File: cmdparser.hh
 * $Date: Mon Dec 16 21:23:46 2013 +0800
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
			size_t size = 0;
			for (; ; ) {
				if (!m_socket->recv(buf + size, 1))
					throw WFTPError(
							"unexpected EOF when trying to find line break");
				if (buf[size] == '\n')
					break;
				size ++;
			}
			if (size && buf[size - 1] == '\r')
				size --;
			buf[size] = 0;
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
			static thread_local std::string buf;
			if (arg.empty())
				buf = cmd;
			else
				buf = cmd + " " + arg;
			buf.append("\r\n");
			m_socket->send(buf.c_str(), buf.length());
		}
};

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

