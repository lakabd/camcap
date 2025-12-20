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

#include <gbm.h>
#include <drm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "logger.hpp"

/**
 * @brief Configuration structure for display settings.
 * 
 * @var display_config::fmt_fourcc
 * The FourCC (Four Character Code) format string that specifies the
 * pixel format for the display (e.g., "YUYV", "MJPG", "RGB3").
 * 
 * @var display_config::display_and_gpu
 * Flag indicating whether the reserverd surface will be used for scanout
 * and rendering i.e., accessible by display unit AND gpu
 */
struct display_config {
    std::string fmt_fourcc;
    bool display_and_gpu;
};

class Display {
private:
    int m_drmFd{-1};
    drmModeRes *m_drmRes{nullptr};
    drmModeConnector *m_drmConnector{nullptr};
    drmModeEncoder *m_drmEncoder{nullptr};
    drmModeCrtc *m_drmCrtc{nullptr};
    drmModeModeInfo m_modeSettings{};
    uint32_t m_connectorId{0};
    uint32_t m_crtcId{0};

    struct gbm_device *m_gbmDev{nullptr};
    struct gbm_bo *m_bo{nullptr};
    struct gbm_surface *m_gbmSurface{nullptr};

    display_config& m_config;
    Logger m_logger;

    bool getRessources();
    bool findConnector();
    bool findEncoder();
    bool findCrtc();

public:
    Display(display_config& conf, bool verbose);
    ~Display();

    bool initialize();
    bool scanout();
};