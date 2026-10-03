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
#include <poll.h>

#include "logger.hpp"
#include "helpers.hpp"

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
    capture_config m_config{};
    __u32 m_memory_type{};
    __u32 m_num_planes{};
    bool m_is_mp_device{false};

    Logger m_logger;
    bool m_initialized{false};
    bool m_stream_is_on{false};

    std::jthread m_worker;
    std::atomic<bool> m_healthy{true};
    moodycamel::BlockingReaderWriterQueue<std::shared_ptr<uint32_t>> m_display_queue{FRAME_QUEUE_SIZE};

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
    bool dequeueBuffer(__u32 *out_buf_index);
    bool queueBuffer(__u32 in_buf_index);

    // Streaming
    bool streamOn();
    bool streamOff();

    // Helpers
    bool saveOneFrame(__u32 buf_index, const std::string& path);

    // Worker
    void workerLoop(std::stop_token st);

public:
    Capture(const std::string& device, capture_config& conf, bool verbose);
    ~Capture();

    const capture_config& get_config() const {
        return m_config;
    }

    auto get_queue(){
        return &m_display_queue;
    }

    bool is_healthy() const {
        return m_healthy.load(); 
    }

    bool initialize(); // Initialize Capture
    bool start(); // Start streaming
    bool stop(); // Stop streaming
};