/*
 * $File: util.hh
 * $Date: Mon Dec 16 22:18:07 2013 +0800
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
bool isregular(const char *fpath);

// vim: syntax=cpp11.doxygen foldmethod=marker foldmarker=f{{{,f}}}

