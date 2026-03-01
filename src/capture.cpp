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

#include "capture.hpp"

Capture::Capture(const std::string& device, capture_config& conf, bool verbose)
    : m_config(conf), m_logger("capture", verbose)
{
    Logger& log = m_logger;

    // Open device
    log.status("Opening device %s", device.c_str());
    m_fd = open(device.c_str(), O_RDWR | O_CLOEXEC);
    if(m_fd < 0)
        log.fatal("Failed to open device " + device + ": " + strerror(errno));

    try{
        // Check config
        if(!validate_user_buffer(m_config.buf)){
            log.fatal("Capture buffer size or format not correctly defined. Please check!");
        }
        if(m_config.mem_type >= MEM_TYPE_MAX || m_config.buf_count == 0)
            log.fatal("Capture mem_type or buf_count not correctly defined. Please check!");
        
        // Init members
        m_capture_buf.resize(m_config.buf_count);
    } catch (...) {
        close(m_fd);
        throw;
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

    // Use device_caps if available 
    __u32 device_caps = caps.capabilities;
    if(caps.capabilities & V4L2_CAP_DEVICE_CAPS){
        device_caps = caps.device_caps;
    }

    log.info("Device Caps:");
    if(log.get_verbose())
        print_v4l2_device_caps(device_caps);

    // Check device type
    log.status("Checking device type.");
    if(!(device_caps & V4L2_CAP_STREAMING)){
        log.error("Device %s does not support streaming. Please check the specified device !", caps.card);
        return false;
    }
    if(device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE){
        log.info("Device is a multi-planar video capture device");
        m_is_mp_device = true;
    }
    else if(device_caps & V4L2_CAP_VIDEO_CAPTURE){
        // TODO: add support for single planar
        log.error("Device %s is a single plane capture device. Only MPLANE devices are supported.", caps.card);
        return false;
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
        
        log.info(". %d: %s - %s %s", fmtdesc.index, list.back().c_str(), fmtdesc.description, (fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED) ? " [compressed]" : "");

        fmtdesc.index++;
    }
    
    // Check if smth was found
    if(list.empty()){
        log.error("Error VIDIOC_ENUM_FMT: No format found for device");
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
    std::string& fourcc = m_config.buf.fourcc;
    unsigned int w = m_config.buf.width;
    unsigned int h = m_config.buf.height;
    struct v4l2_frmsizeenum frmsize{};
    __u32 min_w, min_h, max_w, max_h, step_w, step_h;
    min_w = min_h = max_w = max_h = step_w = step_h = 0;

     __u32 v4l2_fmt = v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
    frmsize.pixel_format = v4l2_fmt;
    frmsize.index = 0;
    
    log.info("Enumerating frame sizes for requested format: %s", 
        fourcc.c_str());
    
    while(ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0 || !requested_size_ok){
        found_sizes = true;
        min_w = frmsize.stepwise.min_width;
        min_h = frmsize.stepwise.min_height;
        max_w = frmsize.stepwise.max_width;
        max_h = frmsize.stepwise.max_height;
        step_w = frmsize.stepwise.step_width;
        step_h = frmsize.stepwise.step_height;

        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){
            // Discrete frame sizes
            log.info("  [%d] Discrete sizes: %dx%d",
                frmsize.index,
                frmsize.discrete.width,
                frmsize.discrete.height);
            // Exact match required
            if(w == frmsize.discrete.width && h == frmsize.discrete.height)
                requested_size_ok = true;
        }
        else if(frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE){
            // Stepwise frame sizes
            log.info("  [%d] Stepwise (range with step):", frmsize.index);
            log.info("      Width:  %d - %d (step %d)", min_w,  max_w,  step_w);
            log.info("      Height: %d - %d (step %d)", min_h, max_h, step_h);
            // Size must fit within stepwise range and steps
            bool width_ok =  ((w >= min_w)  && (w <= max_w)  && ((w - min_w) % step_w  == 0));
            bool height_ok = ((h >= min_h) && (h <= max_h) && ((h - min_h) % step_h == 0));
            
            if(width_ok && height_ok)
                requested_size_ok = true;
        }
        else if(frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS){
            // Continuous frame sizes
            log.info("  [%d] Continuous (any size in range):", frmsize.index);
            log.info("      Width:  %d - %d", min_w,  max_w);
            log.info("      Height: %d - %d", min_h, max_h);
            // Size must fit within continuous range
            if(w >= min_w && w <= max_w && h >= min_h && h <= max_h)
                requested_size_ok = true;
        }

        frmsize.index++;
    }
    // If no sizes are found, assume all sizes are supported
    if(!found_sizes){
        log.warning("Warning VIDIOC_ENUM_FRAMESIZES: No frame sizes found for format %s", fourcc.c_str());
        return true;
    }

    // Check if requested size was found
    if(!requested_size_ok){
        log.error("Size %dx%d is NOT supported for format %s", w, h, fourcc.c_str());
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
    std::string& fourcc = m_config.buf.fourcc;

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
        log.error("Requested format '%s' is not supported by device", fourcc.c_str());
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
    std::string& fourcc = m_config.buf.fourcc;
    struct v4l2_format format{};
    
    log.status("Setting requested format");

    // Set requested format and size
    format.type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    __u32 v4l2_fmt = v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

    if(m_is_mp_device){
        format.fmt.pix_mp.pixelformat = v4l2_fmt;
        format.fmt.pix_mp.width = m_config.buf.width;
        format.fmt.pix_mp.height = m_config.buf.height;
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
        // Format changed
        if(format.fmt.pix_mp.pixelformat != v4l2_fmt){
            std::string new_fourcc = {
                static_cast<char>(format.fmt.pix_mp.pixelformat & 0xFF),
                static_cast<char>((format.fmt.pix_mp.pixelformat >> 8) & 0xFF),
                static_cast<char>((format.fmt.pix_mp.pixelformat >> 16) & 0xFF),
                static_cast<char>((format.fmt.pix_mp.pixelformat >> 24) & 0xFF)
            };
            log.warning("Driver adjusted pixel format from %s to %s", fourcc.c_str(), new_fourcc.c_str());
            m_config.buf.fourcc = new_fourcc;
        }
        // Size changed
        if(format.fmt.pix_mp.width != m_config.buf.width || 
            format.fmt.pix_mp.height != m_config.buf.height || 
            format.fmt.pix_mp.plane_fmt[0].bytesperline != m_config.buf.stride) // TODO: This is ugly, we need to check num_planes not just plane 0
        {
            log.warning("Driver adjusted resolution from %dx%d (s:%d bytes) to %dx%d (s:%d bytes)", m_config.buf.width, m_config.buf.height, m_config.buf.stride,
                format.fmt.pix_mp.width, format.fmt.pix_mp.height, format.fmt.pix_mp.plane_fmt[0].bytesperline);
            m_config.buf.width = format.fmt.pix_mp.width;
            m_config.buf.height = format.fmt.pix_mp.height;
            m_config.buf.stride = format.fmt.pix_mp.plane_fmt[0].bytesperline;
        }
        
        log.info("Format set: %dx%d, num_planes=%d", format.fmt.pix_mp.width, format.fmt.pix_mp.height, format.fmt.pix_mp.num_planes);
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
    req.type   = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = (m_config.mem_type == TYPE_DMABUF) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    if(!xioctl(m_fd, VIDIOC_REQBUFS, &req)){
        log.error("VIDIOC_REQBUFS failed, error requesting buffers");
        return false;
    }

    // Verify
    if(req.count != m_config.buf_count){
        log.warning("Driver adjusted buffer count from %d to %d", m_config.buf_count, req.count); 
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
    
    log.status("Mapping capture buffers: Using %s", (m_config.mem_type == TYPE_DMABUF) ? "DMABUF" : "MMAP" );
    
    // Fill v4l2_buffer struct
    buf.type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (m_config.mem_type == TYPE_DMABUF) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
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
                void* mapped = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, planes[p].m.mem_offset);
                if(mapped == MAP_FAILED){
                    log.error("mmap failed for buffer %d plane %d: %s", i, p, strerror(errno));
                    // Unmap previously mapped buffers
                    for(unsigned int j = 0; j <= i; j++){
                        for(unsigned int k = 0; k < buf.length; k++){
                            if(m_capture_buf[j].plane_addr[k] != nullptr){
                                munmap(m_capture_buf[j].plane_addr[k], m_capture_buf[j].plane_size[k]);
                            }
                        }
                    }
                    return false;
                }
                // Save addr and size
                m_capture_buf[i].plane_addr[p] = mapped;
                m_capture_buf[i].plane_size[p] = planes[p].length;
                
                log.info("    Plane %d: addr=%p, size=%u bytes, offset=%u", p, mapped, planes[p].length, planes[p].m.mem_offset);
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
    buf.type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (m_config.mem_type == TYPE_DMABUF) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
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
    enum v4l2_buf_type type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
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

bool Capture::saveOneFrame(const std::string& path)
{
    Logger& log = m_logger;
    struct v4l2_buffer buf{};
    struct v4l2_plane planes[VIDEO_MAX_PLANES]{};
    
    log.status("Capturing one frame to %s", path.c_str());

    // Path sanity check
    if(path.empty()){
        log.error("Output path is empty");
        return false;
    }

    // Prepare v4l2_buffer struct
    buf.type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (m_config.mem_type == TYPE_DMABUF) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    
    if(m_is_mp_device){
        buf.m.planes = planes;
        buf.length   = VIDEO_MAX_PLANES;
    }
    
    // Dequeue buffer
    if(!xioctl(m_fd, VIDIOC_DQBUF, &buf)){ // This will block when no buffer is in the driver's outgoing queue.
        log.error("VIDIOC_DQBUF failed");
        return false;
    }
    
    log.info("Dequeued buffer %d", buf.index);
    
    // Open output file
    std::ofstream outfile(path, std::ios::binary);
    if(!outfile.is_open()){
        log.error("Failed to open output file: %s", path.c_str());
        // Important: re-queue buffer before returning
        xioctl(m_fd, VIDIOC_QBUF, &buf);
        return false;
    }
    
    // Write frame data
    if(m_is_mp_device){
        for(unsigned int p = 0; p < buf.length; p++){
            if(planes[p].bytesused > 0){
                outfile.write(static_cast<const char*>(m_capture_buf[buf.index].plane_addr[p]), planes[p].bytesused);
                // Check if write okay
                if(outfile.fail()){
                    log.error("Failed to write plane %d to file: %s", p, strerror(errno));
                    outfile.close();

                    // Re-queue buffer before returning
                    xioctl(m_fd, VIDIOC_QBUF, &buf);
                    return false;
                }
                log.info("Wrote plane %d: %u bytes", p, planes[p].bytesused);
            }
        }
    }
    
    outfile.close();
    log.info("Frame saved to %s", path.c_str());
    
    // Re-queue buffer
    if(!xioctl(m_fd, VIDIOC_QBUF, &buf)){
        log.error("VIDIOC_QBUF failed while re-queuing buffer %d", buf.index);
        return false;
    }
    
    return true;
}

bool Capture::streamOff()
{
    Logger& log = m_logger;

    // Stop streaming
    enum v4l2_buf_type type = m_is_mp_device ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
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

    // Unmap requested buffers
    for(unsigned int i = 0; i < m_config.buf_count; i++){
        for(unsigned int p = 0; p < VIDEO_MAX_PLANES; p++){
            if(m_capture_buf[i].plane_addr[p] != nullptr){
                munmap(m_capture_buf[i].plane_addr[p], m_capture_buf[i].plane_size[p]);
                m_capture_buf[i].plane_addr[p] = nullptr;
            }
        }
    }
    
    // Close device FD
    close(m_fd);
}
