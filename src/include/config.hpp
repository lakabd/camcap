/*
 * Copyright (c) 2026 Abderrahim LAKBIR
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

#include <atomic>
#include <array>
#include <linux/videodev2.h>

#define DRM_MAX_PLANES_PER_FRAME  4 // DRM maximum number of Planes/DMA_FDs per frame (MP)
#define NUM_BUFFERS 8
#define FRAME_QUEUE_SIZE NUM_BUFFERS

// Generic buffer type
typedef struct {
    std::string fourcc;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride[VIDEO_MAX_PLANES]{0}; // Array of strides for MP format support. Value in bytes (pitch)
} buffer_t;

// Frame type
typedef struct {
    std::array<int, DRM_MAX_PLANES_PER_FRAME> dma_fds{-1, -1, -1, -1};
    uint32_t v4l2_buf_indx{0};
    uint64_t timestamp{0};
} frame_t;

// This is the global frame db
inline std::array<frame_t, NUM_BUFFERS> g_frames_db;