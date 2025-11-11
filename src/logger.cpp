/*
 * Copyright (c) 2025 Abderrahim LAKBIR
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include "logger.hpp"

Logger::Logger(const std::string& name, bool verbose)
    : m_name(name), m_verbose(verbose)
{
}

Logger::~Logger()
{
}

void Logger::set_verbose(bool verbose)
{
    m_verbose = verbose;
}

bool Logger::get_verbose()
{
    return m_verbose;
}

void Logger::info(const char* fmt, ...) const
{
    if (!m_verbose)
        return;

    va_list args;
    va_start(args, fmt);

    printf("[%s] Info: ", m_name.c_str());
    vprintf(fmt, args);
    printf("\n");

    va_end(args);
}

void Logger::error(const char* fmt, ...) const
{
    va_list args;
    va_start(args, fmt);
    
    fprintf(stderr, "[%s] Error: ", m_name.c_str());
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);
}

void Logger::warning(const char* fmt, ...) const
{
    va_list args;
    va_start(args, fmt);
    
    fprintf(stderr, "[%s] Warning: ", m_name.c_str());
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);
}

[[noreturn]] void Logger::fatal(const std::string& msg) const
{
    fprintf(stderr, "[%s] Fatal: ", m_name.c_str());
    // Juts need to terminate: always throw runtime_error
    throw std::runtime_error(msg);
}