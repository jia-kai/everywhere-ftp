/*
 * $File: util.hh
 * $Date: Wed Dec 25 15:38:31 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

#pragma once

#include <functional>

/*!
 * execute a function in child process, capture stdout and stderr and call
 * on_recv_data() on caputed data
 */
void capture_subproc_output(
		std::function<void(const void*, size_t)> on_recv_data,
		std::function<void()> setup_child);

/*!
 * get file size as decimal string
 */
std::string get_filesize(const char *fpath, bool *successful = nullptr);

bool isdir(const char *fpath);
bool isregular(const char *fpath, bool allow_nonexist = false);

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

