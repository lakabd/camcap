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

struct display_config {
    std::string buf_fourcc;
    uint32_t buf_width;
    uint32_t buf_height;
    uint32_t buf_stride;
    int buf_fd; // The DMA_BUF file descriptor associated with the buffer.

    /* Test display.
    * When enabled, above settings are ignored and buf dimensions are set to match display mode & buf_fourcc defaults to XR24.
    */
    bool use_test_patern; 
};

class Display {
private:
    int m_drmFd{-1};
    drmModeRes *m_drmRes{nullptr};
    drmModeConnector *m_drmConnector{nullptr};
    drmModeEncoder *m_drmEncoder{nullptr};
    drmModeCrtc *m_drmCrtc{nullptr};
    drmModeModeInfo m_modeSettings{}; // Holds display preferred mode
    drmModePlane *m_drmPlane{nullptr};
    uint32_t m_connectorId{0};
    uint32_t m_crtcId{0};
    uint32_t m_planeId{0};
    uint32_t m_fbId{0};

    struct gbm_device *m_gbmDev{nullptr};
    uint32_t m_gbm_flags{0};
    struct gbm_bo *m_bo{nullptr};
    uint32_t m_format{0}; // used DRM/GBM format
    
    display_config& m_config;
    Logger m_logger;

    bool getRessources();
    bool findConnector();
    bool findEncoder();
    bool findCrtc();
    bool findPlane();
    bool importGbmBoFromFD();
    bool testPatern();

public:
    Display(display_config& conf, bool verbose);
    ~Display();

    bool initialize();
    bool scanout();
};