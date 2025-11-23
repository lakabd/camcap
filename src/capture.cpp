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

Capture::Capture(const std::string& device, capture_config& conf, bool verbose)
    : m_config(conf), m_logger("capture", verbose)
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
    if(conf.width == 0 || conf.height == 0 || 
            conf.mem_type >= MEM_TYPE_MAX || conf.buf_count == 0)
        log.fatal("Capture config not correctly defined. Please check!");
    
    if(conf.fmt_fourcc.length() != 4)
        log.fatal("Format must be a 4-character string (e.g., 'NV12')");


    // Init members
    try{
        m_capture_buf.resize(conf.buf_count);
    } catch(const std::bad_alloc& e){
        log.fatal("Failed to allocate capture buffers");
    }
}

bool Capture::checkDeviceCapabilities()
{
    Logger& log = m_logger;
    struct v4l2_capability caps{};

    // Query Caps
    log.status("Querying device capabilities.");
    if(!xioctl(m_fd, VIDIOC_QUERYCAP, &caps)){
        log.error("VIDIOC_QUERYCAP failed, error getting caps");
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

bool Capture::enumerateFormats(std::vector<std::string>& list)
{
    Logger& log = m_logger;
    struct v4l2_fmtdesc fmtdesc{};
    
    // Set buffer type based on whether device is multi-planar
    fmtdesc.type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    // Clear the list
    list.clear();
    
    // Enumerate all formats
    log.info("Enumerating all supported formats.");
    fmtdesc.index = 0;
    while(ioctl(m_fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0){
        // Fill in format fourcc
        char fourcc[5] = {0};
        fourcc[0] = fmtdesc.pixelformat & 0xFF;
        fourcc[1] = (fmtdesc.pixelformat >> 8) & 0xFF;
        fourcc[2] = (fmtdesc.pixelformat >> 16) & 0xFF;
        fourcc[3] = (fmtdesc.pixelformat >> 24) & 0xFF;
        list.push_back(fourcc);
        
        log.info(". %d: %s - %s %s", fmtdesc.index, 
            list.back().c_str(), fmtdesc.description,
            (fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED) ? " [compressed]" : "");

        fmtdesc.index++;
    }
    
    // Check if smth was found
    if(list.empty()){
        log.error("Error VIDIOC_ENUM_FM: No format found for device");
        return false;
    }
    
    log.info("Total supported formats: %zu", list.size());
    
    return true;
}

bool Capture::checkFormatSize()
{
    Logger& log = m_logger;
    bool found_sizes = false;
    bool requested_size_ok = false;
    std::string& fourcc = m_config.fmt_fourcc;
    unsigned int w = m_config.width;
    unsigned int h = m_config.height;
    struct v4l2_frmsizeenum frmsize{};

     __u32 v4l2_fmt = v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
    frmsize.pixel_format = v4l2_fmt;
    frmsize.index = 0;
    
    log.info("Enumerating frame sizes for requested format: %s", 
        fourcc.c_str());
    
    while(ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0){
        found_sizes = true;
        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){
            // Discrete frame sizes
            log.info("  [%d] Discrete sizeq: %dx%d",
                frmsize.index,
                frmsize.discrete.width,
                frmsize.discrete.height);
            // Exact match required
            if(w == frmsize.discrete.width && h == frmsize.discrete.height)
                requested_size_ok = true;
        }
        else if(frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE){
            // Stepwise frame sizes
            log.info("  [%d] Stepwise (range with step):",
                frmsize.index);
            log.info("      Width:  %d - %d (step %d)",
                frmsize.stepwise.min_width,
                frmsize.stepwise.max_width,
                frmsize.stepwise.step_width);
            log.info("      Height: %d - %d (step %d)",
                frmsize.stepwise.min_height,
                frmsize.stepwise.max_height,
                frmsize.stepwise.step_height);
            // Size must fit within stepwise range and steps
            bool width_ok = (w >= frmsize.stepwise.min_width &&
                             w <= frmsize.stepwise.max_width &&
                             w % frmsize.stepwise.step_width == 0);
            bool height_ok = (h >= frmsize.stepwise.min_height &&
                              h <= frmsize.stepwise.max_height &&
                              h % frmsize.stepwise.step_height == 0);
            
            if(width_ok && height_ok)
                requested_size_ok = true;
        }
        else if(frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS){
            // Continuous frame sizes
            log.info("  [%d] Continuous (any size in range):",
                frmsize.index);
            log.info("      Width:  %d - %d",
                frmsize.stepwise.min_width,
                frmsize.stepwise.max_width);
            log.info("      Height: %d - %d",
                frmsize.stepwise.min_height,
                frmsize.stepwise.max_height);
            // Size must fit within continuous range
            if(w >= frmsize.stepwise.min_width &&
                    w <= frmsize.stepwise.max_width &&
                    h >= frmsize.stepwise.min_height &&
                    h <= frmsize.stepwise.max_height)
                requested_size_ok = true;
        }

        frmsize.index++;
    }
    // If no sizes are found, assume all sizes are supported
    if(!found_sizes){
        log.warning("Warning VIDIOC_ENUM_FRAMESIZES: No frame sizes found for" 
            "format %s", fourcc.c_str());
        return true;
    }

    // Check if requested size was found
    if(!requested_size_ok){
        log.error("Size %dx%d is NOT supported for format %s", w, h,
            fourcc.c_str());
        return false;
    }
    
    log.info("Size %dx%d for format %s OK", w, h, fourcc.c_str());
    
    return true;
}

bool Capture::checkFormat()
{
    Logger& log = m_logger;
    std::vector<std::string> formats_list;
    bool format_found = false;
    std::string& fourcc = m_config.fmt_fourcc;

    log.status("Checking supported formats");

    // Check if requested format is supported
    if(!enumerateFormats(formats_list)){
        log.error("Capture::enumrateFormats Failed !");
        return false;
    }
    for(const auto& fmt : formats_list){
        if(fmt == fourcc){
            format_found = true;
            break;
        }
    }
    if(!format_found){
        log.error("Requested format '%s' is not supported by device", 
            fourcc.c_str());
        return false;
    }
    
    // Check if requested size is supported
    if(!checkFormatSize()){
        log.error("Capture::checkFormatSizes Failed !");
        return false;
    }

    return true;
}

bool Capture::setFormat()
{
    Logger& log = m_logger;
    std::string& fourcc = m_config.fmt_fourcc;
    struct v4l2_format format{};
    
    log.status("Setting requested format");

    // Set requested format and size
    format.type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    __u32 v4l2_fmt = v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

    if(m_is_mp_device){
        format.fmt.pix_mp.pixelformat = v4l2_fmt;
        format.fmt.pix_mp.width = m_config.width;
        format.fmt.pix_mp.height = m_config.height;
    } else {
        log.error("TODO: Capture class doesn't support Non-Planar devices");
        return false;
    }

    if(!xioctl(m_fd, VIDIOC_S_FMT, &format)){
        log.error("VIDIOC_S_FMT failed, error setting format");
        return false;
    }

    // Verify
    if(m_is_mp_device){
        if(format.fmt.pix_mp.pixelformat != v4l2_fmt){
            log.warning("Driver adjusted pixel format from %s to %c%c%c%c",
                fourcc.c_str(),
                format.fmt.pix_mp.pixelformat & 0xFF,
                (format.fmt.pix_mp.pixelformat >> 8) & 0xFF,
                (format.fmt.pix_mp.pixelformat >> 16) & 0xFF,
                (format.fmt.pix_mp.pixelformat >> 24) & 0xFF);
        }
        if(format.fmt.pix_mp.width != m_config.width || 
           format.fmt.pix_mp.height != m_config.height){
            log.warning("Driver adjusted resolution from %dx%d to %dx%d",
                m_config.width, m_config.height,
                format.fmt.pix_mp.width, format.fmt.pix_mp.height);
        }
        log.info("Format set: %dx%d, num_planes=%d",
            format.fmt.pix_mp.width, format.fmt.pix_mp.height,
            format.fmt.pix_mp.num_planes);
    }

    return true;
}

bool Capture::requestBuffers()
{
    Logger& log = m_logger;
    struct v4l2_requestbuffers req{};

    log.status("Requesting capture buffers");

    // Request buffers
    req.count  = m_config.buf_count;
    req.type   = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = (m_config.mem_type == TYPE_DMABUF) ? V4L2_MEMORY_DMABUF :
            V4L2_MEMORY_MMAP;
    if(!xioctl(m_fd, VIDIOC_REQBUFS, &req)){
        log.error("VIDIOC_REQBUFS failed, error requesting buffers");
        return false;
    }

    // Verify
    if(req.count != m_config.buf_count){
        log.warning("Driver adjusted buffer count from %d to %d",
            m_config.buf_count, req.count);
        m_config.buf_count = req.count;
        try{
            m_capture_buf.resize(req.count);
        } catch(const std::bad_alloc& e) {
            log.error("Failed to resize capture buffer vector");
            return false;
        }
    }
    
    log.info("Allocated %d buffers", req.count);
    
    return true;
}

bool Capture::mapBuffers()
{
    Logger& log = m_logger;
    struct v4l2_buffer buf{};
    struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
    
    log.status("Mapping capture buffers: Using %s", (m_config.mem_type == 
            TYPE_DMABUF) ? "DMABUF" : "MMAP" );
    
    // Fill v4l2_buffer struct
    buf.type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (m_config.mem_type == TYPE_DMABUF) ? 
        V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    if(m_is_mp_device){
        buf.m.planes = planes;
        buf.length   = VIDEO_MAX_PLANES;
    }

    // Query and map each requested buffer
    for(unsigned int i = 0; i < m_config.buf_count; i++){
        buf.index  = i;
        
        // Query buffer to get plane information
        if(!xioctl(m_fd, VIDIOC_QUERYBUF, &buf)){
            log.error("VIDIOC_QUERYBUF failed for buffer %d", i);
            return false;
        }
        
        // Map buffer planes
        if(m_is_mp_device){
            log.info(". Buffer %d: (%d plane(s))", i, buf.length);
            
            for(unsigned int p = 0; p < buf.length; p++){
                void* mapped = mmap(NULL, planes[p].length,
                    PROT_READ | PROT_WRITE, MAP_SHARED,
                    m_fd, planes[p].m.mem_offset);
                
                if(mapped == MAP_FAILED){
                    log.error("mmap failed for buffer %d plane %d: %s",
                        i, p, strerror(errno));
                    // Unmap previously mapped buffers
                    for(unsigned int j = 0; j <= i; j++){
                        for(unsigned int k = 0; k < buf.length; k++){
                            if(m_capture_buf[j].plane_addr[k] != nullptr){
                                munmap(m_capture_buf[j].plane_addr[k], 
                                    m_capture_buf[j].plane_size[k]);
                            }
                        }
                    }
                    return false;
                }
                // Save addr and size
                m_capture_buf[i].plane_addr[p] = mapped;
                m_capture_buf[i].plane_size[p] = planes[p].length;
                
                log.info("    Plane %d: addr=%p, size=%u bytes, offset=%u",
                    p, mapped, planes[p].length, planes[p].m.mem_offset);
            }
        } else {
            log.error("TODO: Capture class doesn't support Non-Planar devices");
            return false;
        }
    }
    
    log.info("Successfully mapped %d buffers", m_config.buf_count);

    return true;
}

bool Capture::queueBuffers()
{
    Logger& log = m_logger;
    struct v4l2_buffer buf{};
    struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
    
    log.status("Queuing capture buffers");
    // Fill v4l2_buffer struct
    buf.type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (m_config.mem_type == TYPE_DMABUF) ? 
        V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
    if(m_is_mp_device){
        buf.m.planes = planes;
        buf.length   = VIDEO_MAX_PLANES;
    }
    
    // Queue each buffer
    for(unsigned int i = 0; i < m_config.buf_count; i++){
        buf.index = i;
        
        if(!xioctl(m_fd, VIDIOC_QBUF, &buf)){
            log.error("VIDIOC_QBUF failed for buffer %d", i);
            return false;
        }
        
        log.info(". Buffer %d queued", i);
    }
    
    return true;
}

bool Capture::streamOn()
{
    Logger& log = m_logger;

    // Start streaming
    enum v4l2_buf_type type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if(!xioctl(m_fd, VIDIOC_STREAMON, &type)){
        log.error("VIDIOC_STREAMON failed");
        return false;
    }
    
    log.info("Streaming started successfully");

    return true;
}

bool Capture::start()
{
    Logger& log = m_logger;

    // Check Caps
    if(!checkDeviceCapabilities()){
        log.error("Capture::checkDeviceCapabilities Failed !");
        return false;
    }

    // Check format
    if(!checkFormat()){
        log.error("Capture::checkFormat Failed !");
        return false;
    }

    // Set format
    if(!setFormat()){
        log.error("Capture::setFormat Failed !");
        return false;
    }

    // Request capture buffers
    if(!requestBuffers()){
        log.error("Capture::requestBuffers Failed !");
        return false;
    }

    // Map capture buffers
    if(!mapBuffers()){
        log.error("Capture::mapBuffers Failed !");
        return false;
    }

    // Queue capture buffers
    if(!queueBuffers()){
        log.error("Capture::queueBuffers Failed !");
        return false;
    }

    // Start streaming
    if(!streamOn()){
        log.error("Capture::streamOn Failed !");
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

bool Capture::streamOff()
{
    Logger& log = m_logger;

    // Stop streaming
    enum v4l2_buf_type type = m_is_mp_device ? 
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if(!xioctl(m_fd, VIDIOC_STREAMOFF, &type)){
        log.error("VIDIOC_STREAMOFF failed");
        return false;
    }
    
    log.info("Streaming stopped successfully");

    return true;
}

bool Capture::stop()
{
    Logger& log = m_logger;

    // Stop streaming
    if(!streamOff()){
        log.error("Capture::streamOff Failed !");
        return false;
    }

    log.status("Capture is OFF !");

    return true;
}

Capture::~Capture()
{
    Logger& log = m_logger;
    log.status("Quitting...");
    close(m_fd);
}
