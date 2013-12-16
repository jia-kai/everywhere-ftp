/*
 * $File: common.cc
 * $Date: Mon Dec 16 17:35:44 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#define LOG_TIMEFMT		"%H:%M:%S"
#define LOG_COLOR_START	"\033[31m"
#define LOG_COLOR_END	"\033[0m"

#include "common.hh"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <mutex>

#include <libgen.h>
#include <unistd.h>

#include <sys/time.h>

WFTPError::WFTPError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	PrintfOsstream msg;
	msg.vprintf(fmt, ap);
	va_end(ap);
	m_msg = msg.str();
}

void PrintfOsstream::printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void PrintfOsstream::vprintf(const char *fmt, va_list ap)
{
	int size = 128;
	char *p, *np;

	if ((p = (char*)malloc(size)) == NULL)
	{
		fprintf(stderr, "failed to alloc memory\n");
		exit(EXIT_FAILURE);
	}

	for (; ;)
	{
		va_list aq;
		va_copy(aq, ap);
		int n = vsnprintf(p, size, fmt, aq);
		va_end(aq);

		if (n > -1 && n < size)
		{
			if (m_max_size && n + m_cur_size >= m_max_size)
			{
				if (m_cur_size >= m_max_size)
					break;
				n = m_max_size - m_cur_size;
			}
			this->write(p, n);
			break;
		}
		if (n > -1)		/* glibc 2.1 */
			size = n+1;	/* precisely what is needed */
		else			/* glibc 2.0 */
			size *= 2;	/* twice the old size */

		if ((np = (char*)realloc(p, size)) == NULL)
		{
			fprintf(stderr, "failed to alloc memory\n");
			exit(EXIT_FAILURE);
		} else
			p = np;
	}
	free(p);
}


void __wftp_log__(const char *fpath, const char *func, int line,
		const char *fmt, ...)
{
	static std::mutex mtx;
	std::lock_guard<std::mutex> locker(mtx);

	/*
	FILE *fout = fopen(LOG_FILEPATH, "a");
	if (!fout)
	{
		fprintf(stderr, "failed to open log file: %s (%m)\n", LOG_FILEPATH);
		exit(EXIT_FAILURE);
	}
	*/
	FILE *fout = stderr;

	time_t cur_time;
	time(&cur_time);
	char timestr[64];
	strftime(timestr, sizeof(timestr), LOG_TIMEFMT, localtime(&cur_time));
	
	fprintf(fout, LOG_COLOR_START "[%s@%s:%d %s] " LOG_COLOR_END,
			func, basename(strdupa(fpath)), line, timestr);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(fout, fmt, ap);
	va_end(ap);
	fputc('\n', fout);
	// fclose(fout);
}

std::string ssprintf(const char *fmt, ...) {
	PrintfOsstream pr;
	va_list ap;
	va_start(ap, fmt);
	pr.vprintf(fmt, ap);
	va_end(ap);
	return pr.str();
}

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}
