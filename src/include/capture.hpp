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

#include "helpers.hpp"

enum class DequeueStatus {
    Success,
    NoBufferInQueue,
    Error
};

struct capture_buf {
    int plane_fd[VIDEO_MAX_PLANES]; // For DMA_BUF
    void* plane_addr[VIDEO_MAX_PLANES];
    __u32 plane_length[VIDEO_MAX_PLANES]; // Plane size : frame + padding
    __u32 plane_bytesused[VIDEO_MAX_PLANES]; // Frame size
    bool is_queued;

    capture_buf(){
        for(int i = 0; i < VIDEO_MAX_PLANES; i++){
            plane_fd[i] = -1;
            plane_addr[i] = nullptr;
            plane_length[i] = 0;
            plane_bytesused[i] = 0;
        }
        is_queued = false;
    }
};

struct capture_config {
    buffer_t buf;
    __u32 buf_count;
};

class Capture {
private:
    int m_fd{-1};
    std::vector<capture_buf> m_capture_buf;
    struct v4l2_buffer m_v4l2_buf{};
    capture_config m_config{};
    __u32 m_memory_type{};
    __u32 m_num_planes{};
    bool m_is_mp_device{false};

    Logger m_logger;
    bool m_initialized;
    bool m_stream_is_on{false};

    // Caps
    bool checkDeviceCapabilities();

    // Formats
    bool enumerateFormats(std::vector<std::string>& list);
    bool checkFormat();
    bool checkFormatSize();
    bool setFormat();

    // Buffers
    bool requestBuffers();
    bool prepareBuffers();

    // Streaming
    bool streamOn();
    bool streamOff();

public:
    Capture(const std::string& device, capture_config& conf, bool verbose);
    ~Capture();


    bool queueBuffer(__u32 in_buf_index);
    DequeueStatus dequeueBuffer(__u32 *out_buf_index);

    bool initialize(); // Initialize Capture
    bool start(); // Start streaming
    bool saveOneFrame(__u32 buf_index, const std::string& path);
    bool stop(); // Stop streaming
};