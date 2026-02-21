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
#include "helpers.hpp"

// DRM Event callback
void eventCb(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *user_data);

struct display_config {
    buffer_t cam_buf;
    buffer_t gpu_buf;
    bool testing_display; // test dimensions: display mode settings & test format: XR24
};

typedef struct {
    unsigned int count;
    unsigned int sec;
    unsigned int usec;
    bool flip_pending;
} frame_info_t;

class Display {
private:
    int m_drmFd{-1};
    drmModeRes *m_drmRes{nullptr};
    drmModeConnector *m_drmConnector{nullptr};
    drmModeEncoder *m_drmEncoder{nullptr};
    drmModeCrtc *m_drmCrtc{nullptr};
    drmModeModeInfo m_modeSettings{}; // Holds display preferred mode
    uint32_t m_modePropFb_id{0}; // FB_ID DRM Mode property for atomicUpdate().
    drmModePlane *m_drmPrimaryPlane{nullptr};
    uint32_t m_connectorId{0};
    uint32_t m_crtcId{0};
    uint32_t m_primaryPlaneId{0};

    struct gbm_device *m_gbmDev{nullptr};
    uint32_t m_gbm_flags{0};
    uint32_t m_gpu_format{0};
    uint32_t m_cam_format{0};
    uint32_t m_testPatern_FbId{0};
    uint32_t m_splashscreen_FbId{0};

    drmEventContext m_drm_evctx{};
    frame_info_t m_frame{};
    
    display_config m_config{};
    Logger m_logger;
    bool m_display_initialized{false};

    bool getResources();
    bool findConnector();
    bool findEncoder();
    bool findCrtc();
    bool findPlane();
    bool createTestPattern();
    bool loadSplachScreen();
    bool atomicModeSet();
    bool atomicUpdate(uint32_t fbId);

    // Camera buffer
    uint32_t createFbFromFd(int buf_fd);

    // GPU buffer
    struct gbm_bo* importGbmBoFromFD(int buf_fd);
    uint32_t createFbFromGbmBo(struct gbm_bo *bo);

public:
    Display(display_config& conf, bool verbose);
    ~Display();

    int get_fd(){
        return m_drmFd;
    }

    bool flipPending(){
        return m_frame.flip_pending;
    }

    bool initialize(); // Initialize the display
    bool scanout(int buf_fd); // Scanout buf_fd. Non-blocking call
    bool handleEvent(); // Handle DRM events e.g., page flip
};