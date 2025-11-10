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

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <fstream>

#include "helpers.hpp"
#include "capture.hpp"

Capture::Capture(const std::string& device, capture_config& conf, 
    bool verbose)
    : m_config(conf), m_is_mp_device(false), m_verbose(verbose)
{
    // Check device
    struct stat st;
    if(stat(device.c_str(), &st) < 0){
        throw_error("Failed to stat device " + device + ": " + 
            std::strerror(errno));
    }
    if(!S_ISCHR(st.st_mode)){
        throw_error(device + " is not a character device");
    }
    m_fd = open(device.c_str(), O_RDWR);
    if(m_fd < 0)
        throw_error("Failed to open device " + device + ": " + 
            std::strerror(errno));
    
    // Check config
    if(conf.fmt == 0 || conf.width == 0 || conf.height == 0 || 
            conf.mem_type >= MEM_TYPE_MAX || conf.buf_count == 0)
        throw_error("Capture config not correctly defined. Please check!");

    // Init members
    try{
        m_capture_buf.resize(conf.buf_count);
    } catch(const std::bad_alloc& e){
        throw_error("Failed to allocate capture buffers");
    }
    CLEAR(m_v4l2_buf);
}

Capture::~Capture()
{
    close(m_fd);
}
