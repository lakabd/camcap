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

#include "display.hpp"
#include "helpers.hpp"


Display::Display(display_config& conf, bool verbose)
    : m_config(conf), m_logger("display", verbose)
{
    Logger& log = m_logger;

    // Sanity check
    if(!conf.use_test_patern){
        if(conf.buf_fourcc.length() != 4)
            log.fatal("Display buffer format must be a 4-character string (e.g., 'NV12')");
        if(conf.buf_width == 0 || conf.buf_height == 0 || conf.buf_stride == 0 || conf.buf_stride < conf.buf_width)
            log.fatal("Display buffer dimensions invalid");
    }
    else{
        conf.buf_fourcc = "XR24"; // XRGB8888;
        // Dislay mode is used for dimensions
    }

    // Open DRM device
    const char* drmDevices[] = {"/dev/dri/card0", "/dev/dri/card1", "/dev/dri/renderD128"};
    for(const char* device : drmDevices){
        m_drmFd = open(device, O_RDWR);
        if(m_drmFd >= 0){
            log.info("Using DRM device: %s", device);
            break;
        }
    }
    if(m_drmFd < 0)
        log.fatal("No DRM device found !");

    // TODO: Add a check if need to be the DRM master: drmSetMaster()

    // Create GBM device
    m_gbmDev = gbm_create_device(m_drmFd);
    if(!m_gbmDev){
        log.fatal("Failed to create GBM device");
    }
    log.info("GBM backend: %s", gbm_device_get_backend_name(m_gbmDev));

    // Validate requested format
    std::string& fourcc = conf.buf_fourcc;
    m_gbm_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING; // Defaults to display + GPU
    m_format = __gbm_fourcc_code(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
    if(gbm_device_is_format_supported(m_gbmDev, m_format, m_gbm_flags) == 0){
        log.fatal("Specified format " + fourcc + " is NOT supported");
    }
}

bool Display::getRessources()
{
    Logger& log = m_logger;
    log.status("Acquiring card ressources...");

    m_drmRes = drmModeGetResources(m_drmFd);
    if(!m_drmRes){
        log.error("drmModeGetResources: failed to get DRM ressources");
        return false;
    }

    if(log.get_verbose()){
        print_drmModeRes(m_drmRes);
    }

    return true;
}

bool Display::findConnector()
{
    Logger& log = m_logger;
    log.status("Finding connector...");

    for(int i = 0; i < m_drmRes->count_connectors; i++){
        m_drmConnector = drmModeGetConnector(m_drmFd, m_drmRes->connectors[i]);
        if(m_drmConnector && m_drmConnector->connection == DRM_MODE_CONNECTED && m_drmConnector->count_modes > 0){
            m_connectorId = m_drmConnector->connector_id;
            m_modeSettings = m_drmConnector->modes[0]; // Use first (usually preferred) mode
            log.info("Found connected display: %dx%d @%dHz", m_modeSettings.hdisplay, m_modeSettings.vdisplay, m_modeSettings.vrefresh);
            break;
        }
        drmModeFreeConnector(m_drmConnector);
        m_drmConnector = nullptr;
    }

    if(!m_drmConnector){
        log.error("drmModeGetConnector: No connected display found !");
        return false;
    }

    if(log.get_verbose()){
        print_drmModeConnector(m_drmFd, m_drmConnector);
    }

    return true;
}

bool Display::findEncoder()
{
    Logger& log = m_logger;
    log.status("Finding encoder...");

    // Try to use current encoder if available
    if(m_drmConnector->encoder_id){
        m_drmEncoder = drmModeGetEncoder(m_drmFd, m_drmConnector->encoder_id);
        if(m_drmEncoder)
            log.info("Using connector's current encoder: ID %d", m_drmEncoder->encoder_id);
    }

    // If no current encoder, find a compatible one
    if(!m_drmEncoder){
        for(int i = 0; i < m_drmConnector->count_encoders; i++){
            m_drmEncoder = drmModeGetEncoder(m_drmFd, m_drmConnector->encoders[i]);
            if(m_drmEncoder){
                log.info("Using encoder ID: %d", m_drmEncoder->encoder_id);
                break; 
            }
        }
    }

    if(!m_drmEncoder){
        log.error("drmModeGetEncoder: No encoder was found !");
        return false;
    }

    if(log.get_verbose()){
        print_drmModeEncoder(m_drmEncoder);
    }

    return true;
}

bool Display::findCrtc()
{
    Logger& log = m_logger;
    log.status("Finding crtc...");

    // Try to use current CRTC if available
    if(m_drmEncoder->crtc_id){
        m_drmCrtc = drmModeGetCrtc(m_drmFd, m_drmEncoder->crtc_id);
        if(m_drmCrtc){
            log.info("Using encoder's current CRTC: ID %d", m_drmCrtc->crtc_id);
            m_crtcId = m_drmEncoder->crtc_id;
        }
    }

    // If no current CRTC, find a compatible one from possible_crtcs bitmask
    if(!m_drmCrtc){
        for(int i = 0; i < m_drmRes->count_crtcs; i++){
            if(m_drmEncoder->possible_crtcs & (1 << i)){
                m_crtcId = m_drmRes->crtcs[i];
                m_drmCrtc = drmModeGetCrtc(m_drmFd, m_crtcId);
                if(m_drmCrtc){
                    log.info("Found compatible CRTC: ID %d", m_drmCrtc->crtc_id);
                }
            }
        }
    }

    if(!m_drmCrtc){
        log.error("drmModeGetCrtc: No CRTC was found !");
        return false;
    }

    if(log.get_verbose()){
        print_drmModeCrtc(m_drmCrtc);
    }

    return true;
}

bool Display::initialize()
{
    Logger& log = m_logger;

    // Get DRM ressources
    if(!getRessources()){
        log.error("getRessources() failed !");
        return false;
    }

    // Find connected display
    if(!findConnector()){
        log.error("findConnector() failed !");
        return false;
    }

    // Find encoder
    if(!findEncoder()){
        log.error("findEncoder() failed !");
        return false;
    }

    // Find Crtc
    if(!findCrtc()){
        log.error("findCrtc() failed !");
        return false;
    }

    return true;
}

bool Display::testPatern()
{
    Logger& log = m_logger;

    log.status("Using test patern.");
    
    // Create a test buffer
    m_bo = gbm_bo_create(m_gbmDev, m_modeSettings.hdisplay, m_modeSettings.vdisplay, m_format, m_gbm_flags);
    if(!m_bo){
        log.error("gbm_bo_create: Failed to create test buffer");
        return false;
    }

    // Map and fill with test pattern
    void *map_data;
    uint32_t stride;
    void *ptr = gbm_bo_map(m_bo, 0, 0, m_modeSettings.hdisplay, m_modeSettings.vdisplay, GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    
    if(ptr){
        // Red screen
        uint32_t *pixels = (uint32_t *)ptr;
        for(uint32_t y = 0; y < m_modeSettings.vdisplay; y++){
            for(uint32_t x = 0; x < m_modeSettings.hdisplay; x++){
                pixels[y * (stride/4) + x] = 0xFFFF0000; // Red
            }
        }
        gbm_bo_unmap(m_bo, map_data);
    }
    else {
        log.error("gbm_bo_map: Failed to map test buffer");
        gbm_bo_destroy(m_bo);
        m_bo = nullptr;
        return false;
    }

    return true;
}

bool Display::importGbmBoFromFD()
{
    Logger& log = m_logger;

    log.status("Importing external DMA_BUF.");

    // Sanity check
    if(m_config.buf_fd < 0){
        log.error("Provided buf_fd is invalid");
        return false;
    }

    // Import BO
    struct gbm_import_fd_data import_data;
    import_data.fd = m_config.buf_fd;
    import_data.width = m_config.buf_width;
    import_data.height = m_config.buf_height;
    import_data.stride = m_config.buf_stride;
    import_data.format = m_format;
    
    m_bo = gbm_bo_import(m_gbmDev, GBM_BO_IMPORT_FD, &import_data, m_gbm_flags);
    if(!m_bo){
        log.error("gbm_bo_import failed: cannot import DMA_BUF (errno: %d - %s)", errno, strerror(errno));
        return false;
    }

    log.info("Successfully imported DMA_BUF: fd=%d, %ux%u, stride=%u", m_config.buf_fd, m_config.buf_width, m_config.buf_height, m_config.buf_stride);

    return true;
}

bool Display::scanout()
{
    Logger& log = m_logger;
    int ret = -1;

    // Sanity check
    if(!m_crtcId || !m_connectorId){
        log.error("Display not initialized. Call initialize() first!");
        return false;
    }

    // Get test or real BO
    if(m_config.use_test_patern){
        if(!m_bo && !testPatern()){ // scanout() is called in a loop, we should create test BO only if none.
            log.error("testPatern() failed!");
            return false;
        }
    }
    else{
        // Import BO from user conf
        if(!m_bo && !importGbmBoFromFD()){
            log.error("importGbmBoFromFD() failed!");
            return false;
        }
    }

    // Check if got a BO
    if(!m_bo){
        log.error("GBM bo is empty !");
        return false;
    }

    // Create & display the Framebuffer
    if(m_fbId == 0){
        uint32_t handle = gbm_bo_get_handle(m_bo).u32;
        uint32_t stride = gbm_bo_get_stride(m_bo);
        uint32_t width = gbm_bo_get_width(m_bo);
        uint32_t height = gbm_bo_get_height(m_bo);
        log.info("Creating framebuffer: %ux%u, format: %#x, handle: %u, stride: %u", width, height, m_format, handle, stride);

        // For single-plane formats (XRGB8888, ARGB8888, etc.). TODO: switch to a generic implementation to support MP formats
        uint32_t handles[4] = {handle, 0, 0, 0};
        uint32_t strides[4] = {stride, 0, 0, 0};
        uint32_t offsets[4] = {0, 0, 0, 0};

        ret = drmModeAddFB2(m_drmFd, width, height, m_format, handles, strides, offsets, &m_fbId, 0);
        if(ret){
            log.error("drmModeAddFB2 failed: %d", ret);
            return false;
        }
        log.info("Created framebuffer: ID %u", m_fbId);

        // Now display it
        ret = drmModeSetCrtc(m_drmFd, m_crtcId, m_fbId, 0, 0, &m_connectorId, 1, &m_modeSettings);
        if(ret){
            log.error("drmModeSetCrtc failed: %d", ret);
            drmModeRmFB(m_drmFd, m_fbId);
            m_fbId = 0;
            return false;
        }
    }

    return true;
}

Display::~Display()
{
    Logger& log = m_logger;
    log.status("Quitting...");

    // Free Framebuffer
    if(m_fbId > 0){
        drmModeRmFB(m_drmFd, m_fbId);
    }
    // Free GBM BO
    if(m_bo){
        gbm_bo_destroy(m_bo);
    }
    // Free DRM crtc
    if(m_drmCrtc){
        drmModeFreeCrtc(m_drmCrtc);
    }
    // Free DRM encoder
    if(m_drmEncoder){
        drmModeFreeEncoder(m_drmEncoder);
    }
    // Free DRM connector
    if(m_drmConnector){
        drmModeFreeConnector(m_drmConnector);
    }
    // Free DRM ressources
    if(m_drmRes){
        drmModeFreeResources(m_drmRes);
    }
    // Distroy GBM device
    if(m_gbmDev){
        gbm_device_destroy(m_gbmDev);
    }
    // Close DRM fd
    if(m_drmFd){
        close(m_drmFd);
    }
}