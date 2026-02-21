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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm_fourcc.h>

#include "display.hpp"

Display::Display(display_config& conf, bool verbose)
    : m_config(conf), m_logger("display", verbose)
{
    Logger& log = m_logger;

    // Sanity check
    if(!m_config.testing_display){
        if(!validate_user_buffer(m_config.cam_buf)){
            log.fatal("User input: Camera buffer size or format invalid !");
        }
        if(!validate_user_buffer(m_config.gpu_buf)){
            log.fatal("User input: GPU buffer size or format invalid !");
        }
    }
    else {
        // Tests
        m_config.gpu_buf.fourcc = "XR24";
        m_config.cam_buf.fourcc = "NV12";
    }

    try {
        // Open DRM device
        const char* drmDevices[] = {"/dev/dri/card0", "/dev/dri/card1"};
        for(const char* device : drmDevices){
            m_drmFd = open(device, O_RDWR | O_CLOEXEC);
            if(m_drmFd >= 0){
                // Check if device has resources (connectors, crtcs)
                if(getResources() && m_drmRes){
                    if(m_drmRes->count_connectors > 0 && m_drmRes->count_crtcs > 0){
                        log.info("Using DRM device: %s", device);
                        break;
                    }
                    drmModeFreeResources(m_drmRes);
                    m_drmRes = nullptr;
                }
                // Not a suitable device, close and try next
                close(m_drmFd);
                m_drmFd = -1;
            }
        }
        
        if(m_drmFd < 0){
            log.fatal("No suitable DRM device found (must have Connectors and CRTCs)!");
        }
        
        // TODO: Add a check if need to be the DRM master: drmSetMaster()

        // Enable atomic modesetting
        if(drmSetClientCap(m_drmFd, DRM_CLIENT_CAP_ATOMIC, 1) < 0){
            log.fatal("Enabling atomic modesettings failed !");
        }

        // Create GBM device
        m_gbmDev = gbm_create_device(m_drmFd);
        if(!m_gbmDev){
            log.fatal("Failed to create GBM device");
        }
        log.info("GBM backend: %s", gbm_device_get_backend_name(m_gbmDev));

        // Validate GPU format
        std::string& fourcc = m_config.gpu_buf.fourcc;
        m_gbm_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING; // Defaults to Display + GPU
        m_gpu_format = __gbm_fourcc_code(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
        if(gbm_device_is_format_supported(m_gbmDev, m_gpu_format, m_gbm_flags) == 0){
            log.fatal("Specified format " + fourcc + " is NOT supported");
        }

        // Set camera Format
        fourcc = m_config.cam_buf.fourcc;
        m_cam_format = fourcc_code(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

    } catch (...) {
        if(m_gbmDev) gbm_device_destroy(m_gbmDev);
        if(m_drmRes) drmModeFreeResources(m_drmRes);
        if(m_drmFd >= 0) close(m_drmFd);
        throw;
    }
}

bool Display::getResources()
{
    Logger& log = m_logger;
    log.status("Acquiring card ressources...");

    m_drmRes = drmModeGetResources(m_drmFd);
    if(!m_drmRes){
        log.error("drmModeGetResources: failed to get DRM resources");
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
        bool pmode_found = false;
        m_drmConnector = drmModeGetConnector(m_drmFd, m_drmRes->connectors[i]);
        if(m_drmConnector && m_drmConnector->connection == DRM_MODE_CONNECTED && m_drmConnector->count_modes > 0){
            m_connectorId = m_drmConnector->connector_id;
            for(int j = 0; j < m_drmConnector->count_modes; j++){
                if(m_drmConnector->modes[j].type & DRM_MODE_TYPE_PREFERRED){
                    m_modeSettings = m_drmConnector->modes[j];
                    log.info("Found connected display: %dx%d @%dHz (preferred)", m_modeSettings.hdisplay, m_modeSettings.vdisplay, m_modeSettings.vrefresh);
                    pmode_found = true;
                    break;
                }
            }
            if(!pmode_found){
                m_modeSettings = m_drmConnector->modes[0];
                log.info("No preferred mode was found. Using first mode: %dx%d @%dHz", m_modeSettings.hdisplay, m_modeSettings.vdisplay, m_modeSettings.vrefresh);
            }
            // Ok
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
                    break;
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

bool Display::findPlane()
{
    Logger& log = m_logger;
    int crtc_index = -1;
    bool plane_format_ok = false;

    log.status("Finding primary plane...");

    // Planes require a separate get resources call
    drmModePlaneRes *planeRes = drmModeGetPlaneResources(m_drmFd);
    if(!planeRes){
        log.error("drmModeGetPlaneResources: failed to get Plane resources");
        return false;
    }

    // Find our CRTC index for possible_crtcs bitmask
    for(int i = 0; i < m_drmRes->count_crtcs; i++){
        if(m_drmRes->crtcs[i] == m_crtcId){
            crtc_index = i;
            break;
        }
    }
    
    // Find a primary plane
    for(uint32_t i = 0; i < planeRes->count_planes; i++){
        drmModePlane *plane = drmModeGetPlane(m_drmFd, planeRes->planes[i]);
        if(!plane)
            continue;
        
        // Check if plane is compatible with our CRTC
        if(plane->possible_crtcs & (1u << crtc_index)){
            // Check if it's a PRIMARY plane
            drmModeObjectProperties *props = drmModeObjectGetProperties(m_drmFd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
            if(props){
                for(uint32_t j = 0; j < props->count_props; j++){
                    drmModePropertyRes *prop = drmModeGetProperty(m_drmFd, props->props[j]);
                    if(prop){
                        if(strcmp(prop->name, "type") == 0 && props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY){
                            // Validate primary plane against camera format
                            for(uint32_t k = 0; k < plane->count_formats; k++){
                                if(plane->formats[k] == m_cam_format){
                                    plane_format_ok = true;
                                    break;
                                }
                            }
                            // Plane found
                            if(plane_format_ok){
                                m_primaryPlaneId = plane->plane_id;
                                m_drmPrimaryPlane = plane;
                                log.info("Found Primary DRM plane ID : %d", m_primaryPlaneId);
                            }
                        }
                        drmModeFreeProperty(prop);
                    }
                    // TODO: add lookup for an overlay plane for GPU.
                    // TODO: validate overlay plane against GPU format.
                    if(m_primaryPlaneId) break;
                }
                drmModeFreeObjectProperties(props);
            }
        }
        if(m_primaryPlaneId) break;
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planeRes);

    if(!m_primaryPlaneId){
        log.error("findPlane: No Primary plane found !");
        return false;
    }

    if(log.get_verbose()){
        print_drmModePlane(m_drmPrimaryPlane);
    }
    
    return true;
}

bool Display::atomicModeSet()
{
    Logger& log = m_logger;
    int ret;
    uint32_t prop_conn_crtc_id = 0;
    uint32_t prop_plane_crtc_id = 0;
    uint32_t prop_crtc_mode_id = 0, prop_crtc_active = 0;
    uint32_t prop_src_w = 0, prop_src_h = 0, prop_src_x = 0, prop_src_y = 0;
    uint32_t prop_crtc_w = 0, prop_crtc_h = 0, prop_crtc_x = 0, prop_crtc_y = 0;

    log.status("Setting display mode...");

    // Allocate atomic request
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if(!req){
        log.error("drmModeAtomicAlloc: Failed to allocate atomic request");
        return false;
    }

    // Create a blob for the mode
    uint32_t blob_id = 0;
    ret = drmModeCreatePropertyBlob(m_drmFd, &m_modeSettings, sizeof(m_modeSettings), &blob_id);
    if(ret < 0){
        std::cerr << "Failed to create mode blob" << std::endl;
        drmModeAtomicFree(req);
        return false;
    }

    // Connector: Link the connector to the crtc
    prop_conn_crtc_id = get_drmModePropertyId(m_drmFd, m_connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    if(!prop_conn_crtc_id){
        log.error("Failed to find CRTC_ID property on connector");
        goto err;
    }
    drmModeAtomicAddProperty(req, m_connectorId, prop_conn_crtc_id, m_crtcId);

    // CRTC: set the mode and activate
    prop_crtc_mode_id = get_drmModePropertyId(m_drmFd, m_crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    prop_crtc_active  = get_drmModePropertyId(m_drmFd, m_crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    if(!prop_crtc_mode_id || !prop_crtc_active){
        log.error("Failed to find CRTC properties: MODE_ID/ACTIVE");
        goto err;
    }
    drmModeAtomicAddProperty(req, m_crtcId, prop_crtc_mode_id, blob_id);
    drmModeAtomicAddProperty(req, m_crtcId, prop_crtc_active, 1);

    // Plane: attach the splashscreen/testpatern FB to the plane and the plane to the crtc
    m_modePropFb_id = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    prop_plane_crtc_id = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    if(!m_modePropFb_id || !prop_plane_crtc_id){
        log.error("Failed to find Plane properties: FB_ID/CRTC_ID");
        goto err;
    }
    drmModeAtomicAddProperty(req, m_primaryPlaneId, m_modePropFb_id, ((m_config.testing_display) ? m_testPatern_FbId : m_splashscreen_FbId));
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_plane_crtc_id, m_crtcId);

    // Plane: set source coordinates in 16.16 fixed point format
    prop_src_w = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_W");
    prop_src_h = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_H");
    prop_src_x = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_X");
    prop_src_y = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    if(!prop_src_w || !prop_src_h || !prop_src_x || !prop_src_y){
        log.error("Failed to find Plane SRC properties");
        goto err;
    }
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_src_w, ((uint32_t)m_modeSettings.hdisplay) << 16);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_src_h, ((uint32_t)m_modeSettings.vdisplay) << 16);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_src_x, 0);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_src_y, 0);

    // Plane: set destination coordinates in integer
    prop_crtc_w = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    prop_crtc_h = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    prop_crtc_x = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    prop_crtc_y = get_drmModePropertyId(m_drmFd, m_primaryPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    if(!prop_crtc_w || !prop_crtc_h || !prop_crtc_x || !prop_crtc_y){
        log.error("Failed to find Plane CRTC properties");
        goto err;
    }
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_crtc_w, m_modeSettings.hdisplay);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_crtc_h, m_modeSettings.vdisplay);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_crtc_x, 0);
    drmModeAtomicAddProperty(req, m_primaryPlaneId, prop_crtc_y, 0);

    // Setup event context
    m_drm_evctx.version = 2;
    m_drm_evctx.page_flip_handler = eventCb;
    
    // Commit
    ret = drmModeAtomicCommit(m_drmFd, req, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET, &m_frame);
    if(ret < 0){
        log.error("drmModeAtomicCommit: Atomic commit failed: %s", strerror(errno));
    } else {
        m_frame.flip_pending = true;
        log.status("Display is On!");
    }

    drmModeAtomicFree(req);
    drmModeDestroyPropertyBlob(m_drmFd, blob_id);

    return (ret == 0);

err:
    drmModeAtomicFree(req);
    drmModeDestroyPropertyBlob(m_drmFd, blob_id);
    return false;
}

bool Display::initialize()
{
    Logger& log = m_logger;

    // Don't reinit
    if(m_display_initialized)
        return true;

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

    // Find Primary plane
    if(!findPlane()){
        log.error("findPlane() failed !");
        return false;
    }

    // Load Splashscreen or test patern
    if(m_config.testing_display){
        if(!createTestPattern()){
            log.error("createTestPattern() failed!");
            return false;
        }
    }
    else{
        if(!loadSplashScreen()){
            log.error("loadSplashScreen() failed!");
            return false;
        }
    }

    // Display On
    if(!atomicModeSet()){
        log.error("atomicModeSet() failed !");
        return false;
    }

    m_display_initialized = true;

    return true;
}

bool Display::createTestPattern()
{
    Logger& log = m_logger;
    int ret;
    struct drm_mode_create_dumb creq{};
    struct drm_mode_map_dumb mreq{};
    struct drm_gem_close clreq{};
    uint32_t fbId = 0;
    void *map = MAP_FAILED;
    uint32_t *pixels = nullptr;

    log.status("Using test patern (format : XR24)");
    
    // Create Dumb Buffer
    creq.width = m_modeSettings.hdisplay;
    creq.height = m_modeSettings.vdisplay;
    creq.bpp = 32; // XRGB8888
    ret = drmIoctl(m_drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if(ret < 0){
        log.error("DRM_IOCTL_MODE_CREATE_DUMB failed: %s", strerror(errno));
        return false;
    }

    // Create FB
    uint32_t handles[4] = {creq.handle, 0, 0, 0};
    uint32_t pitches[4] = {creq.pitch, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    ret = drmModeAddFB2(m_drmFd, creq.width, creq.height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fbId, 0);
    if(ret < 0){
        log.error("drmModeAddFB2 failed: %s", strerror(errno));
        goto err;
    }

    // Map
    mreq.handle = creq.handle;
    ret = drmIoctl(m_drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if(ret < 0){
        log.error("DRM_IOCTL_MODE_MAP_DUMB failed: %s", strerror(errno));
        goto err;
    }
    map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_drmFd, mreq.offset);
    if(map == MAP_FAILED){
        log.error("mmap failed: %s", strerror(errno));
        goto err;
    }

    // Fill
    pixels = (uint32_t *)map;
    for(uint32_t y = 0; y < creq.height; y++){
        for(uint32_t x = 0; x < creq.width; x++){
            // creq.pitch is in bytes
            pixels[y * (creq.pitch / 4) + x] = 0xFFFF0000; // solid Red
        }
    }

    m_testPatern_FbId = fbId; // FB to display

    // Cleanup
    munmap(map, creq.size);
    clreq.handle = creq.handle;
    drmIoctl(m_drmFd, DRM_IOCTL_GEM_CLOSE, &clreq);

    return true;

err:
    if(fbId)
        drmModeRmFB(m_drmFd, fbId);
    
    struct drm_mode_destroy_dumb dreq{};
    dreq.handle = creq.handle;
    drmIoctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

    return false;
}

bool Display::loadSplashScreen()
{
    Logger& log = m_logger;

    log.status("Loading SplashScreen");

    // TODO

    // Fall back to createTestPattern() for now
    bool ret = createTestPattern();
    m_splashscreen_FbId = m_testPatern_FbId;
    return ret;
}

uint32_t Display::createFbFromGbmBo(struct gbm_bo *bo)
{
    Logger& log = m_logger;
    uint32_t fbId{0};
    int ret;

    // Sanity check
    if(!bo){
        log.error("createFbFromGbmBo: gbm_bo is NULL");
        return 0;
    }

    // We only support XRGB8888 for now
    if(m_gpu_format != GBM_FORMAT_XRGB8888){
        log.error("createFbFromGbmBo: Only supporting XR24 for now.");
        return 0;
    }

    // Create FB
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t bpp = gbm_bo_get_bpp(bo);
    uint32_t depth = bpp - 8; // For XRGB8888
    log.info("Creating framebuffer: %ux%u, format: %#x, handle: %u, stride: %u", width, height, m_gpu_format, handle, stride);

    ret = drmModeAddFB(m_drmFd, width, height, depth, bpp, stride, handle, &fbId);
    if(ret < 0){
        log.error("drmModeAddFB failed: %s", strerror(errno));
        return 0;
    }
    log.info("Created framebuffer: ID %u", fbId);

    return fbId;
}

struct gbm_bo* Display::importGbmBoFromFD(int buf_fd)
{
    Logger& log = m_logger;
    struct gbm_bo *bo{nullptr};

    log.status("Importing GPU buffer.");

    // Sanity check
    if(buf_fd < 0){
        log.error("Provided buf_fd is invalid");
        return nullptr;
    }

    // Import BO
    struct gbm_import_fd_data idata;
    idata.fd = buf_fd;
    idata.width = m_config.gpu_buf.width;
    idata.height = m_config.gpu_buf.height;
    idata.stride = m_config.gpu_buf.stride;
    idata.format = m_gpu_format;
    
    bo = gbm_bo_import(m_gbmDev, GBM_BO_IMPORT_FD, &idata, m_gbm_flags);
    if(!bo){
        log.error("gbm_bo_import failed: cannot import DMA_BUF: %s", strerror(errno));
        return nullptr;
    }

    log.info("Successfully imported DMA_BUF: fd=%d, %ux%u, stride=%u", idata.fd, idata.width, idata.height, idata.stride);

    return bo;
}

uint32_t Display::createFbFromFd(int buf_fd)
{
    Logger& log = m_logger;
    int ret = 0;
    uint32_t fbId = 0;

    log.info("Importing Camera buffer.");

    // Sanity check
    if(buf_fd < 0){
        log.error("Provided buf_fd is invalid");
        return 0;
    }

    // We support only NV12 for now
    if(m_cam_format != DRM_FORMAT_NV12){
        log.error("createFbFromFd: Only supporting NV12 for now.");
        return 0;
    }

    // Create GEM handle
    uint32_t handle;
    ret = drmPrimeFDToHandle(m_drmFd, buf_fd, &handle);
    if(ret < 0){
        log.error("drmPrimeFDToHandle failed: cannot import DMA_BUF: %s", strerror(errno));
        return 0;
    }

    // Create FB
    uint32_t stride = m_config.cam_buf.stride;
    uint32_t height = m_config.cam_buf.height;
    uint32_t width = m_config.cam_buf.width;

    uint32_t handles[4] = {handle, handle, 0, 0};
    uint32_t pitches[4] = {stride, stride, 0, 0};
    uint32_t offsets[4] = {0, stride*height, 0, 0}; // Here assuming UV is packed directly after Y plane

    ret = drmModeAddFB2(m_drmFd, width, height, m_cam_format, handles, pitches, offsets, &fbId, 0);
    if(ret < 0){
        log.error("drmModeAddFB2 failed: %s", strerror(errno));
        goto cleanup;
    }

cleanup:
    struct drm_gem_close clreq{};
    clreq.handle = handle;
    drmIoctl(m_drmFd, DRM_IOCTL_GEM_CLOSE, &clreq);

    return fbId;
}

void eventCb(int fd, unsigned int sequence, unsigned int sec, unsigned int usec, void *user_data)
{
    (void) fd;
    (void) sequence;
    frame_info_t *f = static_cast<frame_info_t*>(user_data);
    float old_t = 0;
    float curr_t = 0;
    float refresh_rate = 0;

    // Calculate FPS
    if(f->count > 0){
        old_t = (float) f->sec + (float) f->usec * 0.000001f;
        curr_t = (float) sec + (float) usec * 0.000001f;
        refresh_rate = (1.0f / (curr_t - old_t));
        f->sec = sec;
        f->usec = usec;
    }

    // Flip complete
    f->flip_pending = false;

    printf("Flip complete for frame %u @%.02fhz\n", f->count++, refresh_rate);
}

bool Display::handleEvent()
{
    Logger& log = m_logger;

    int ret = drmHandleEvent(m_drmFd, &m_drm_evctx);
    if(ret < 0){
        log.error("drmHandleEvent failed: %s", strerror(errno));
        return false;
    }

    return true;
}

bool Display::atomicUpdate(uint32_t cam_fbId) // TODO: add gpu_fbId
{
    Logger& log = m_logger;
    int ret{0};

    // Sanity check
    if(cam_fbId <= 0){
        log.error("cam_fbId not defined");
        return false;
    }
    if(m_modePropFb_id <= 0){
        log.error("m_modePropFb_id is not defined");
        return false;
    }

    // Atomic req
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if(!req){
        log.error("drmModeAtomicAlloc: Failed to allocate atomic request");
        return false;
    }

    // Attach new FB
    drmModeAtomicAddProperty(req, m_primaryPlaneId, m_modePropFb_id, cam_fbId);

    // DRM_MODE_ATOMIC_NONBLOCK: Returns immediately, doesn't wait for VSYNC
    // DRM_MODE_PAGE_FLIP_EVENT: Generates a VBLANK event when the flip completes
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
    
    ret = drmModeAtomicCommit(m_drmFd, req, flags, &m_frame);
    if(ret < 0){
        log.error("drmModeAtomicCommit: Atomic commit failed: %s", strerror(errno));
    } else {
        m_frame.flip_pending = true;
    }

    drmModeAtomicFree(req);

    return (ret == 0) ? true : false;
}

bool Display::scanout(int buf_fd)
{
    Logger& log = m_logger;
    uint32_t fbId{0};
    bool& testing = m_config.testing_display;

    // Sanity check
    if(!m_display_initialized){
        log.error("Display not initialized. Call initialize() first!");
        return false;
    }

    // Import GPU FB
    // TODO

    // Import camera FB
    if(!testing){
        fbId = createFbFromFd(buf_fd);
        if(!fbId){
            log.error("createFbFromFd() failed!");
            return false;
        }
    }

    // Page flip
    if(testing){
        if(!atomicUpdate(m_testPatern_FbId)){
            log.error("atomicUpdate() failed!");
            return false;
        }
    }
    else {
        if(!atomicUpdate(fbId)){
            log.error("atomicUpdate() failed!");
            drmModeRmFB(m_drmFd, fbId);
            return false;
        }
    }

    // Cleanup
    if(fbId){ // TODO: the fbId shall not be removed. We need to implement triple buffering.
        drmModeRmFB(m_drmFd, fbId);
    }

    return true;
}

Display::~Display()
{
    Logger& log = m_logger;
    log.status("Quitting...");

    // Free splash FB
    if(m_splashscreen_FbId > 0){
        drmModeRmFB(m_drmFd, m_splashscreen_FbId);
    }
    // Free test FB
    if(m_testPatern_FbId > 0){
        drmModeRmFB(m_drmFd, m_testPatern_FbId);
    }
    // Free DRM plane
    if(m_drmPrimaryPlane){
        drmModeFreePlane(m_drmPrimaryPlane);
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