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
#include <map>
#include <poll.h>

#include "logger.hpp"
#include "helpers.hpp"

// DRM Event callback
void eventCb(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *user_data);

struct display_config {
    buffer_t cam_buf;
    buffer_t gpu_buf;
    moodycamel::BlockingReaderWriterQueue<std::shared_ptr<uint32_t>>* frames_queue;
    
    bool testing_display{false}; // test dimensions: display mode settings & test format: XR24

    display_config(){
        frames_queue = nullptr;
        // keep these till Render class is implemented
        gpu_buf.fourcc = "XR24";
        gpu_buf.width = 1; //dummy
        gpu_buf.height = 1; //dummy
        gpu_buf.stride[0] = 1; //dummy
    }
};

typedef struct {
    unsigned int count;
    unsigned int sec;
    unsigned int usec;
    float refresh_rate;
    bool complete{true};
} scanout_status_t;

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

    // Display rectangle
    uint32_t m_src_w{0}, m_src_h{0}; // source rect (FB/camera size)
    uint32_t m_dst_x{0}, m_dst_y{0}, m_dst_w{0}, m_dst_h{0}; // dest rect on CRTC

    struct gbm_device *m_gbmDev{nullptr};
    uint32_t m_gbm_flags{0};
    uint32_t m_gpu_format{0};
    uint32_t m_cam_format{0};
    uint8_t  m_cam_format_nplanes{0};
    bool m_cam_format_packed{false};

    uint32_t m_testPattern_FbId{0};
    uint32_t m_splashscreen_FbId{0};

    drmEventContext m_drm_evctx{};
    scanout_status_t m_scanout_status{};
    bool m_display_is_on{false};
    std::map<int, uint32_t> m_fb_map{}; // <key: dma_fd of plane 0, value: drm framebuffer id>
    
    display_config m_config{};
    Logger m_logger;
    bool m_initialized{false};

    std::jthread m_worker;
    std::atomic<bool> m_healthy{true};

    // Frame lifecycle:
    // Whenever a frame is no more owned by one of these, its framebuffer is recycled back to capture
    std::shared_ptr<uint32_t> m_next_frame;   // waiting to be committed
    std::shared_ptr<uint32_t> m_pending_frame;      // committed, waiting for vblank
    std::shared_ptr<uint32_t> m_on_screen_frame;    // currently displayed

    bool getResources();
    bool findConnector();
    bool findEncoder();
    bool findCrtc();
    bool findPlane();
    bool createTestPattern(uint32_t width, uint32_t height);
    bool loadSplashScreen();
    bool atomicModeSet();
    bool atomicUpdate(uint32_t fbId);
    bool handleEvent(); // Handle DRM events e.g., page flip
    bool scanout(std::array<int, DRM_MAX_PLANES_PER_FRAME>& cam_buf_fds); // Scanout buf_fds. Non-blocking call

    // Camera buffer
    bool createFbFromFd(std::array<int, DRM_MAX_PLANES_PER_FRAME>& buf_fds, uint32_t *out_fbId);

    // GPU buffer
    bool importGbmBoFromFD(int buf_fd, struct gbm_bo **out_bo);
    bool createFbFromGbmBo(struct gbm_bo *bo, uint32_t *out_fbId);

    // Worker
    void workerLoop(std::stop_token st);

public:
    Display(display_config& conf, bool verbose);
    ~Display();

    bool is_healthy() const {
        return m_healthy.load(); 
    }

    bool initialize();
    bool start();
    bool stop();
};