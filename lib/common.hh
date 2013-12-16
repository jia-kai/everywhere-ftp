/*
 * $File: common.hh
 * $Date: Mon Dec 16 15:14:44 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#pragma once

#define WFTP_NAME		"WFTP (everyWhereFTP, 0.0.1)"

#include <exception>
#include <string>
#include <sstream>

/*!
 * \brief close the file describer on destruction
 */
struct FDCloser
{
	int fd;
	FDCloser(int f = -1):
		fd(f)
	{}

	void close();

	~FDCloser() {
		close();
	}
};

/*!
 * \brief string stream with C printf-like functions
 */
class PrintfOsstream: public std::ostringstream
{
	size_t m_max_size = 0, m_cur_size = 0;

	public:
		void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
		void vprintf(const char *fmt, va_list ap);

		/*!
		 * \brief set the maximal output string size; extra data would be
		 *		truncated
		 *
		 *	\param size the size in bytes; set to 0 for no limit
		 */
		void set_max_size(size_t size)
		{ m_max_size = size; }
};

std::string ssprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

class WFTPError: public std::exception {
	std::string m_msg;

	public:
		WFTPError(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
		~WFTPError() throw() {}

		const char* what() const throw()
		{ return m_msg.c_str(); }
};

/*
 * print log
 *
 * eg: wftp_log("program started, pid=%d", pid)
 */
#define wftp_log(fmt, ...) __wftp_log__(__FILE__, __func__, __LINE__, \
		fmt, ## __VA_ARGS__)

void __wftp_log__(const char *fpath, const char *func, int line,
		const char *fmt, ...) __attribute__((format(printf, 4, 5)));

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

