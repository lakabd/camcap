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
    : m_drmFd(-1), m_drmRes(nullptr), m_drmConnector(nullptr), m_drmEncoder(nullptr), m_drmCrtc(nullptr), m_gbmDev(nullptr), m_bo(nullptr), 
    m_gbmSurface(nullptr), m_logger("display", verbose)
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
    log.status("Acquiring card ressources.");

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

bool Display::initialize()
{
    Logger& log = m_logger;

    // Get DRM ressources
    if(!getRessources()){
        log.error("getRessources() failed !");
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
    
    // Distroy DRM ressources
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