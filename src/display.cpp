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
    if(!m_config.testing_display){
        if(m_config.buf_fourcc.length() != 4){
            log.fatal("Display buffer format must be a 4-character string (e.g., XR24, NV12, ...)");
        }
        if(m_config.buf_width == 0 || m_config.buf_height == 0 || m_config.buf_stride == 0 || m_config.buf_stride < m_config.buf_width){
            log.fatal("Display buffer dimensions invalid");
        }
    }
    else{
        m_config.buf_fourcc = "XR24"; // XRGB8888;
        // Display mode is used for dimensions
    }

    try {
        // Open DRM device
        const char* drmDevices[] = {"/dev/dri/card0", "/dev/dri/card1"};
        for(const char* device : drmDevices){
            m_drmFd = open(device, O_RDWR | O_CLOEXEC);
            if(m_drmFd >= 0){
                // Check if device has resources (connectors, crtcs)
                if(getRessources() && m_drmRes){
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

        // Validate GBM format
        std::string& fourcc = m_config.buf_fourcc;
        m_gbm_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING; // Defaults to Display + GPU
        m_gbm_format = __gbm_fourcc_code(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
        if(gbm_device_is_format_supported(m_gbmDev, m_gbm_format, m_gbm_flags) == 0){
            log.fatal("Specified format " + fourcc + " is NOT supported");
        }
        
    } catch (...) {
        if(m_gbmDev) gbm_device_destroy(m_gbmDev);
        if(m_drmRes) drmModeFreeResources(m_drmRes);
        if(m_drmFd >= 0) close(m_drmFd);
        throw;
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

bool Display::findPlane()
{
    Logger& log = m_logger;
    int crtc_index = -1;

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
                            m_planeId = plane->plane_id;
                            m_drmPlane = plane;
                            log.info("Found Primary DRM plane ID : %d", m_planeId);
                        }
                        drmModeFreeProperty(prop);
                    }
                    if(m_planeId) break;
                }
                drmModeFreeObjectProperties(props);
            }
        }
        if(m_planeId) break;
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(planeRes);

    if(!m_planeId){
        log.error("findPlane: No Primary plane found !");
        return false;
    }

    if(log.get_verbose()){
        print_drmModePlane(m_drmPlane);
    }
    
    return true;
}

bool Display::atomicModeSet()
{
    Logger& log = m_logger;
    int ret;

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
    uint32_t prop_conn_crtc_id = get_drmModePropertyId(m_drmFd, m_connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    drmModeAtomicAddProperty(req, m_connectorId, prop_conn_crtc_id, m_crtcId);

    // CRTC: set the mode and activate
    uint32_t prop_crtc_mode_id = get_drmModePropertyId(m_drmFd, m_crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    uint32_t prop_crtc_active  = get_drmModePropertyId(m_drmFd, m_crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    drmModeAtomicAddProperty(req, m_crtcId, prop_crtc_mode_id, blob_id);
    drmModeAtomicAddProperty(req, m_crtcId, prop_crtc_active, 1);

    // Plane: attach the splashscreen/testpatern FB to the plane and the plane to the crtc
    m_modePropFb_id = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    uint32_t prop_plane_crtc_id = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    drmModeAtomicAddProperty(req, m_planeId, m_modePropFb_id, ((m_config.testing_display) ? m_testPatern_FbId : m_splashscreen_FbId));
    drmModeAtomicAddProperty(req, m_planeId, prop_plane_crtc_id, m_crtcId);

    // Plane: set source coordinates in 16.16 fixed point format
    uint32_t prop_src_w = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "SRC_W");
    uint32_t prop_src_h = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "SRC_H");
    uint32_t prop_src_x = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "SRC_X");
    uint32_t prop_src_y = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    drmModeAtomicAddProperty(req, m_planeId, prop_src_w, m_modeSettings.hdisplay << 16);
    drmModeAtomicAddProperty(req, m_planeId, prop_src_h, m_modeSettings.vdisplay << 16);
    drmModeAtomicAddProperty(req, m_planeId, prop_src_x, 0);
    drmModeAtomicAddProperty(req, m_planeId, prop_src_y, 0);

    // Plane: set destination coordinates in integer
    uint32_t prop_crtc_w = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    uint32_t prop_crtc_h = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    uint32_t prop_crtc_x = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    uint32_t prop_crtc_y = get_drmModePropertyId(m_drmFd, m_planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    drmModeAtomicAddProperty(req, m_planeId, prop_crtc_w, m_modeSettings.hdisplay);
    drmModeAtomicAddProperty(req, m_planeId, prop_crtc_h, m_modeSettings.vdisplay);
    drmModeAtomicAddProperty(req, m_planeId, prop_crtc_x, 0);
    drmModeAtomicAddProperty(req, m_planeId, prop_crtc_y, 0);

    // Setup event context
    m_drm_evctx.version = 2;
    m_drm_evctx.page_flip_handler = eventCb;
    
    // Commit
    ret = drmModeAtomicCommit(m_drmFd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    if(ret < 0){
        log.error("drmModeAtomicCommit: Atomic commit failed: %s", strerror(errno));
    } else {
        log.status("Display is On!");
    }

    drmModeAtomicFree(req);
    drmModeDestroyPropertyBlob(m_drmFd, blob_id);

    return true;
}

bool Display::initialize()
{
    Logger& log = m_logger;

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
        if(!loadSplachScreen()){
            log.error("loadSplachScreen() failed!");
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
    struct gbm_bo *bo{nullptr};

    log.status("Using test patern");
    
    // Create a test buffer
    bo = gbm_bo_create(m_gbmDev, m_modeSettings.hdisplay, m_modeSettings.vdisplay, m_gbm_format, m_gbm_flags);
    if(!bo){
        log.error("gbm_bo_create: Failed to create test buffer");
        return false;
    }

    // Map and fill with test pattern
    void *map_data;
    uint32_t stride;
    void *ptr = gbm_bo_map(bo, 0, 0, m_modeSettings.hdisplay, m_modeSettings.vdisplay, GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    
    if(ptr){
        // Red screen
        uint32_t *pixels = (uint32_t *)ptr;
        for(uint32_t y = 0; y < m_modeSettings.vdisplay; y++){
            for(uint32_t x = 0; x < m_modeSettings.hdisplay; x++){
                pixels[y * (stride/4) + x] = 0xFFFF0000; // solid Red
            }
        }
        gbm_bo_unmap(bo, map_data);
    }
    else {
        log.error("gbm_bo_map: Failed to map test buffer");
        gbm_bo_destroy(bo);
        return false;
    }

    // Create FB
    m_testPatern_FbId = createFbFromGbmBo(bo);
    if(!m_testPatern_FbId){
        log.error("createFbFromGbmBo: Failed!");
        gbm_bo_destroy(bo);
        return false;
    }

    // The BO is no more needed as we don't intend to modify the buffer afterwards
    gbm_bo_destroy(bo);

    return true;
}

bool Display::loadSplachScreen()
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

    // Sanity check
    if(!bo){
        log.error("createFbFromGbmBo: gbm_bo is NULL");
        return 0;
    }

    // This method is working only for single-plane formats (XRGB8888, ARGB8888, etc.). 
    // TODO: switch to a generic implementation to support MP formats
    if(m_gbm_format != GBM_FORMAT_XRGB8888){
        log.error("createFbFromGbmBo: Only supporting XR24 for now.");
        return 0;
    }

    // Create FB
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    log.info("Creating framebuffer: %ux%u, format: %#x, handle: %u, stride: %u", width, height, m_gbm_format, handle, stride);

    uint32_t handles[4] = {handle, 0, 0, 0};
    uint32_t strides[4] = {stride, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    int ret = drmModeAddFB2(m_drmFd, width, height, m_gbm_format, handles, strides, offsets, &fbId, 0);
    if(ret){
        log.error("drmModeAddFB2 failed: %d", ret);
        return 0;
    }
    log.info("Created framebuffer: ID %u", fbId);

    return fbId;
}

struct gbm_bo* Display::importGbmBoFromFD(int buf_fd)
{
    Logger& log = m_logger;
    struct gbm_bo *bo{nullptr};

    log.status("Importing external DMA_BUF.");

    // Sanity check
    if(buf_fd < 0){
        log.error("Provided buf_fd is invalid");
        return nullptr;
    }

    // Import BO
    struct gbm_import_fd_data import_data;
    import_data.fd = buf_fd;
    import_data.width = m_config.buf_width;
    import_data.height = m_config.buf_height;
    import_data.stride = m_config.buf_stride;
    import_data.format = m_gbm_format;
    
    bo = gbm_bo_import(m_gbmDev, GBM_BO_IMPORT_FD, &import_data, m_gbm_flags);
    if(!bo){
        log.error("gbm_bo_import failed: cannot import DMA_BUF: %s", strerror(errno));
        return nullptr;
    }

    log.info("Successfully imported DMA_BUF: fd=%d, %ux%u, stride=%u", buf_fd, m_config.buf_width, m_config.buf_height, m_config.buf_stride);

    return bo;
}

void eventCb(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *user_data)
{
    (void) fd;
    bool *pending = static_cast<bool*>(user_data);
    // Page flip done
    *pending = false;

    printf("Flip complete! Frame %u / Time: %u,%u", frame, sec, usec);
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

bool Display::atomicUpdate(uint32_t fbId)
{
    Logger& log = m_logger;
    int ret{0};

    // Sanity check
    if(fbId <= 0){
        log.error("fbId not defined");
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
    drmModeAtomicAddProperty(req, m_planeId, m_modePropFb_id, fbId);

    // DRM_MODE_ATOMIC_NONBLOCK: Returns immediately, doesn't wait for VSYNC
    // DRM_MODE_PAGE_FLIP_EVENT: Generates a VBLANK event when the flip completes
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
    
    ret = drmModeAtomicCommit(m_drmFd, req, flags, &m_flip_pending);
    if(ret < 0){
        log.error("drmModeAtomicCommit: Atomic commit failed: %s", strerror(errno));
    }

    drmModeAtomicFree(req);

    return (ret == 0) ? true : false;
}

bool Display::scanout(int buf_fd)
{
    Logger& log = m_logger;
    struct gbm_bo *bo{nullptr};
    uint32_t fbId{0};
    bool& testing = m_config.testing_display;

    // Sanity check
    if(!m_display_initialized){
        log.error("Display not initialized. Call initialize() first!");
        return false;
    }

    // Get GBM bo from fd
    if(!testing){
        bo = importGbmBoFromFD(buf_fd);
        if(!bo){
            log.error("importGbmBoFromFD() failed!");
            return false;
        }
    }
    
    // Get fb from GBM bo
    if(!testing){
        fbId = createFbFromGbmBo(bo);
        if(fbId <= 0){
            log.error("createFbFromGbmBo() failed!");
            gbm_bo_destroy(bo);
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
            gbm_bo_destroy(bo);
            return false;
        }
    }

    // Cleanup
    if(bo){ // TODO: the bo shall not be destroyed. We need to implement triple buffering.
        gbm_bo_destroy(bo);
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
    if(m_drmPlane){
        drmModeFreePlane(m_drmPlane);
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