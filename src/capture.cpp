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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <fstream>

#include "helpers.hpp"
#include "capture.hpp"

Capture::Capture(const std::string& device, capture_config& conf, 
    bool verbose)
    : m_config(conf), m_is_mp_device(false), m_logger("capture", verbose)
{
    Logger& log = m_logger;
    // Check device
    struct stat st;
    if(stat(device.c_str(), &st) < 0){
        log.fatal("Failed to stat device " + device + ": " + strerror(errno));
    }
    if(!S_ISCHR(st.st_mode)){
        log.fatal(device + " is not a character device");
    }
    
    log.status("Opening device %s", device.c_str());
    m_fd = open(device.c_str(), O_RDWR);
    if(m_fd < 0)
        log.fatal("Failed to open device " + device + ": " + strerror(errno));
    
    // Check config
    if(conf.fmt == 0 || conf.width == 0 || conf.height == 0 || 
            conf.mem_type >= MEM_TYPE_MAX || conf.buf_count == 0)
        log.fatal("Capture config not correctly defined. Please check!");

    // Init members
    try{
        m_capture_buf.resize(conf.buf_count);
    } catch(const std::bad_alloc& e){
        log.fatal("Failed to allocate capture buffers");
    }
    CLEAR(m_v4l2_buf);
}

bool Capture::checkDeviceCapabilities()
{
    Logger& log = m_logger;
    struct v4l2_capability caps;
    CLEAR(caps);

    // Query Caps
    log.status("Querying device capabilities.");
    if(!xioctl(m_fd, VIDIOC_QUERYCAP, &caps)){
        log.error("Error getting caps");
        return false;
    }

    // Print device info
    log.info("Device Name: %s", caps.card);
    log.info("Driver Name: %s", caps.driver);
    log.info("Device Bus: %s", (const char*)caps.bus_info);
    log.info("Device Version: %u", (unsigned int)caps.version);
    log.info("Device Caps:");
    if(log.get_verbose())
        print_v4l2_device_caps(caps.capabilities);

    // Check device type
    log.status("Checking device type.");
    if(!(caps.capabilities & V4L2_CAP_STREAMING)){
        log.error("Device %s does not support streaming."
                "Please check the specified device !", caps.card);
        return false;
    }
    if(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE){
        log.info("Device is a multi-planar video capture device");
        m_is_mp_device = true;
    }

    return true;
}

bool Capture::start()
{
    Logger& log = m_logger;

    // Check Caps
    if(!checkDeviceCapabilities()){
        log.error("checkDeviceCapabilities Failed !");
        return false;
    }

    log.status("Capture is ON !");

    return true;
}

bool Capture::saveToFile(const std::string& path)
{
    (void) path;
    
    return true;
}

bool Capture::stop()
{
    Logger& log = m_logger;

    log.status("Capture is OFF !");

    return true;
}

Capture::~Capture()
{
    Logger& log = m_logger;
    log.status("Quitting...");
    close(m_fd);
}
