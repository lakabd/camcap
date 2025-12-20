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


Display::Display(bool verbose)
    : m_drmFd(-1), m_drmRes(nullptr), m_drmConnector(nullptr), m_drmEncoder(nullptr), m_drmCrtc(nullptr), m_connectorId(0),
    m_gbmDev(nullptr), m_bo(nullptr), m_gbmSurface(nullptr), m_logger("display", verbose)
{
    Logger& log = m_logger;

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

    // Create GBM device
    m_gbmDev = gbm_create_device(m_drmFd);
    if(!m_gbmDev){
        log.fatal("Failed to create GBM device");
    }
    log.info("GBM backend: %s", gbm_device_get_backend_name(m_gbmDev));
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
        if(m_drmConnector->connection == DRM_MODE_CONNECTED && m_drmConnector->count_modes > 0){
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

    return true;
}

bool Display::scanout()
{
    //TODO
    return true;
}

Display::~Display()
{
    Logger& log = m_logger;
    log.status("Quitting...");

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
        (m_drmFd);
    }
}