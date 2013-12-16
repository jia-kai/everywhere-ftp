/*
 * $File: socket.hh
 * $Date: Mon Dec 16 17:44:01 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#pragma once

#include "common.hh"
#include <cstdint>
#include <memory>
#include <string>

class SocketBase {
	public:
		typedef unsigned addr_t;

		SocketBase(const SocketBase &) = delete;
		~SocketBase();

		SocketBase& operator = (const SocketBase &) = delete;

		void send(const void *buf, size_t size);
		void recv_fixsize(void *buf, size_t size);
		size_t recv(void *buf, size_t max_size);

		/*!
		 * send text with CRLF linebreaks
		 */
		void send_crlf(const char *msg, size_t size) {
			size_t start = 0;
			for (size_t i = 0; i < size; i ++)
				if (msg[i] == '\n' && (!i || msg[i - 1] != '\r')) {
					send(msg + start, i - start);
					send("\r\n", 2);
					start = i + 1;
				}
			if (start < size)
				send(msg + start, size - start);
		}

		static std::string format_addr(addr_t addr, const char sep = '.') {
			return ssprintf("%d%c%d%c%d%c%d",
					(addr>>24)&0xFF, sep,
					(addr>>16)&0xFF, sep,
					(addr>>8)&0xFF, sep,
					addr&0xFF);
		}


		void close();

		void enable_timeout();

		/*!
		 * return a text description of the peer
		 */
		const char *get_peerinfo() const {
			return m_peerinfo.c_str();
		}

		std::shared_ptr<SocketBase> connect(
				const char *host, const char *service);

		/*!
		 * \brief get local port number
		 */
		int local_port() const
		{ return m_local_port; }

		/*!
		 * \brief get the local address
		 */
		addr_t local_addr() const
		{ return m_local_addr; }

	protected:
		static std::shared_ptr<SocketBase>
			make_from_fd(int fd, const std::string &peerinfo = std::string());

		void set_socket_fd(int fd);

		int get_socket_fd() const {
			return m_fd;
		}

		SocketBase() = default;

	private:
		int m_fd = -1;
		std::string m_peerinfo;

		addr_t m_local_addr;
		int m_local_port;

		SocketBase(int fd);
};

class ServerSocket: public SocketBase {
	public:
		/*!
		 * \brief pass 0 to *port* to use an ephemeral port
		 */
		ServerSocket(uint16_t port, int backlog = 5);

		std::shared_ptr<SocketBase> accept();
};

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

