/*
 * $File: socket.cc
 * $Date: Mon Dec 16 21:11:55 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#define SOCKET_TIMEOUT	100

#define SET_TCP_NODELAY

#include "socket.hh"
#include "common.hh"

#include <cstdio>
#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <alloca.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

SocketBase::SocketBase(int fd) {
	set_socket_fd(fd);
}

SocketBase::~SocketBase() {
	close();
}

void SocketBase::set_socket_fd(int fd) {
	m_fd = fd;

#ifdef SET_TCP_NODELAY
	int flag = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				(const char*)&flag, sizeof(flag)))
		throw WFTPError("failed to setsockopt: %s", strerror(errno));
#endif


	struct sockaddr_in local_addr;
	struct sockaddr* local_addr_ptr = (struct sockaddr*)&local_addr;
	socklen_t addrlen = sizeof(local_addr);
	memset(&local_addr, 0, addrlen);
	if (getsockname(fd, local_addr_ptr, &addrlen))
		throw WFTPError("getsockname: %m");

	m_local_addr = ntohl(local_addr.sin_addr.s_addr);
	m_local_port = ntohs(local_addr.sin_port);
}

std::shared_ptr<SocketBase> SocketBase::make_from_fd(
		int fd, const std::string &peerinfo) {

	auto rst = new SocketBase(fd);
	rst->m_peerinfo = peerinfo;
	return std::shared_ptr<SocketBase>(rst);
}

void SocketBase::close() {
	if (m_fd != -1) {
		if (::close(m_fd))
			wftp_log("failed to close fd %d: %m", m_fd);
		m_fd = -1;
	}
}

void SocketBase::send(const void *buf0, size_t size) {
	if (m_fd < 0)
		throw WFTPError("attempt to write to unbinded socket");
	const char *buf = static_cast<const char *>(buf0);
	while (size) {
		ssize_t s = ::send(m_fd, buf, size, 0);
		if (s < 0)
			throw WFTPError("socket: failed to write: %s", strerror(errno));
		size -= s;
		buf += s;
	}
}

size_t SocketBase::recv(void *buf, size_t max_size) {
	if (is_closed())
		return 0;
	ssize_t s = ::recv(m_fd, buf, max_size, 0);
	if (s < 0)
		throw WFTPError("socket: failed to read: %s", strerror(errno));
	return s;
}

void SocketBase::recv_fixsize(void *buf0, size_t size) {
	if (is_closed())
		throw WFTPError("recv_fixsize from closed socket");
	char *buf = static_cast<char *>(buf0);
	while (size) {
		ssize_t s = ::recv(m_fd, buf, size, 0);
		if (s < 0)
			throw WFTPError("socket: failed to read: %s", strerror(errno));
		size -= s;
		buf += s;
	}
}

bool SocketBase::is_closed() {
	if (m_fd < 0)
		throw WFTPError("attempt to operate on unbound socket");
	fd_set rfd;
	FD_ZERO(&rfd);
	FD_SET(m_fd, &rfd);
	timeval tv;
	memset(&tv, 0, sizeof(tv));
	select(m_fd + 1, &rfd, 0, 0, &tv);
	if (!FD_ISSET(m_fd, &rfd))
		return false;
	int n = 0;
	ioctl(m_fd, FIONREAD, &n);
	return n == 0;
}

std::shared_ptr<SocketBase> SocketBase::connect(
		const char *host, const char *service) {

	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (int s = getaddrinfo(host, service, &hints, &result))
		throw WFTPError("failed to getaddrinfo(%s, %s): %s",
				host, service, gai_strerror(s));

	int fd = -1;
	for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == -1)
			continue;

		if (!::connect(fd, rp->ai_addr, rp->ai_addrlen))
			break;  // return 0, successfully connected
		::close(fd);
	}
	freeaddrinfo(result);
	if (fd == -1)
		throw WFTPError("failed to connect to (%s, %s): %m", host, service);
	return SocketBase::make_from_fd(fd);
}

ServerSocket::ServerSocket(uint16_t port, int backlog) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		throw WFTPError("failed to create socket: %m");

	struct sockaddr_in srv_addr;
	struct sockaddr* srv_addr_ptr = (struct sockaddr*)&srv_addr;
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = INADDR_ANY;
	srv_addr.sin_port = htons(port);

	int optval = 1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
				(const char *) &optval, sizeof(optval)))
		throw WFTPError("setsockopt: %m");

	if (bind(sockfd, srv_addr_ptr, sizeof(srv_addr)) < 0)
		throw WFTPError("faied to bind to %d: %m", int(port));

	if (listen(sockfd, backlog))
		throw WFTPError("listen: %m");

	set_socket_fd(sockfd);
}

void SocketBase::enable_timeout() {
	struct timeval timeout;
	timeout.tv_sec = SOCKET_TIMEOUT;
	timeout.tv_usec = 0;

	if (setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO,
				(const char *) &timeout, sizeof(timeout)))
		throw WFTPError("setsockopt: %m");
	if (setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO,
				(const char *) &timeout, sizeof(timeout)))
		throw WFTPError("setsockopt: %m");
}

std::shared_ptr<SocketBase> ServerSocket::accept() {
	struct sockaddr_in cli_addr;
	socklen_t cli_addr_len = sizeof(cli_addr);
	int clifd;
	for (; ; ) {
		clifd = ::accept(get_socket_fd(),
			(struct sockaddr*)&cli_addr, &cli_addr_len);
		if (clifd == -1) {
			if (errno != EINTR)
				wftp_log("bad client socket fd: %m");
			continue;
		}
		break;
	}

	char hostbuf[NI_MAXHOST] = "?", servbuf[NI_MAXSERV] = "?";
	getnameinfo((struct sockaddr*)&cli_addr, cli_addr_len,
			hostbuf, NI_MAXHOST,
			servbuf, NI_MAXSERV,
			NI_NUMERICHOST | NI_NUMERICSERV);
	return SocketBase::make_from_fd(clifd,
			std::string(hostbuf) + ":" + servbuf);
}

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

