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

#pragma once

#include <vector>
#include <linux/videodev2.h>
#include "logger.hpp"

struct capture_buf {
    void* start[VIDEO_MAX_PLANES];
    size_t length[VIDEO_MAX_PLANES];
};

typedef enum {
    TYPE_MMAP=0,
    TYPE_DMABUF,
    MEM_TYPE_MAX
} mem_type_t;

struct capture_config {
    __u32 fmt;
    __u32 width;
    __u32 height;

    mem_type_t mem_type;
    __u32 buf_count;
};

class Capture {
private:
    int m_fd;
    std::vector<capture_buf> m_capture_buf;
    struct v4l2_buffer m_v4l2_buf;
    capture_config& m_config;
    bool m_is_mp_device;
    bool m_plane_count;
    Logger m_logger;

    bool requestBuffers();
    bool mapBuffers();
    bool queueBuffers();
    bool dequeueBuffers();

public:
    Capture(const std::string& device, capture_config& conf, 
        bool verbose);
    ~Capture();

    bool checkCapabilities();
    bool checkFormat();
    bool setFormat();
    bool start();
    bool saveToFile(const std::string& path);
    bool stop();
};