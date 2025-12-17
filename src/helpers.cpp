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

#include <sys/ioctl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <vector>
#include "helpers.hpp"

bool xioctl(int fd, unsigned long req, void *arg)
{
    while (true) {
        if (ioctl(fd, req, arg) == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;  // interrupted by signal, retry
        }
        printf("ioctl error: request %#lx faild with error: %s\n", req, strerror(errno));
        return false;
    }
}

void print_v4l2_device_caps(__u32 caps)
{
    std::vector<const char*> caps_list;
    if(caps & V4L2_CAP_VIDEO_CAPTURE)
        caps_list.push_back("V4L2_CAP_VIDEO_CAPTURE");
    if(caps & V4L2_CAP_VIDEO_OUTPUT)
        caps_list.push_back("V4L2_CAP_VIDEO_OUTPUT");
    if(caps & V4L2_CAP_VIDEO_OVERLAY)
        caps_list.push_back("V4L2_CAP_VIDEO_OVERLAY");
    if(caps & V4L2_CAP_VBI_CAPTURE)
        caps_list.push_back("V4L2_CAP_VBI_CAPTURE");
    if(caps & V4L2_CAP_VBI_OUTPUT)
        caps_list.push_back("V4L2_CAP_VBI_OUTPUT");
    if(caps & V4L2_CAP_SLICED_VBI_CAPTURE)
        caps_list.push_back("V4L2_CAP_SLICED_VBI_CAPTURE");
    if(caps & V4L2_CAP_SLICED_VBI_OUTPUT)
        caps_list.push_back("V4L2_CAP_SLICED_VBI_OUTPUT");
    if(caps & V4L2_CAP_RDS_CAPTURE)
        caps_list.push_back("V4L2_CAP_RDS_CAPTURE");
    if(caps & V4L2_CAP_VIDEO_OUTPUT_OVERLAY)
        caps_list.push_back("V4L2_CAP_VIDEO_OUTPUT_OVERLAY");
    if(caps & V4L2_CAP_HW_FREQ_SEEK)
        caps_list.push_back("V4L2_CAP_HW_FREQ_SEEK");
    if(caps & V4L2_CAP_RDS_OUTPUT)
        caps_list.push_back("V4L2_CAP_RDS_OUTPUT");
    if(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        caps_list.push_back("V4L2_CAP_VIDEO_CAPTURE_MPLANE");
    if(caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE)
        caps_list.push_back("V4L2_CAP_VIDEO_OUTPUT_MPLANE");
    if(caps & V4L2_CAP_VIDEO_M2M_MPLANE)
        caps_list.push_back("V4L2_CAP_VIDEO_M2M_MPLANE");
    if(caps & V4L2_CAP_VIDEO_M2M)
        caps_list.push_back("V4L2_CAP_VIDEO_M2M");
    if(caps & V4L2_CAP_TUNER)
        caps_list.push_back("V4L2_CAP_TUNER");
    if(caps & V4L2_CAP_AUDIO)
        caps_list.push_back("V4L2_CAP_AUDIO");
    if(caps & V4L2_CAP_RADIO)
        caps_list.push_back("V4L2_CAP_RADIO");
    if(caps & V4L2_CAP_MODULATOR)
        caps_list.push_back("V4L2_CAP_MODULATOR");
    if(caps & V4L2_CAP_SDR_CAPTURE)
        caps_list.push_back("V4L2_CAP_SDR_CAPTURE");
    if(caps & V4L2_CAP_EXT_PIX_FORMAT)
        caps_list.push_back("V4L2_CAP_EXT_PIX_FORMAT");
    if(caps & V4L2_CAP_SDR_OUTPUT)
        caps_list.push_back("V4L2_CAP_SDR_OUTPUT");
    if(caps & V4L2_CAP_META_CAPTURE)
        caps_list.push_back("V4L2_CAP_META_CAPTURE");
    if(caps & V4L2_CAP_READWRITE)
        caps_list.push_back("V4L2_CAP_READWRITE");
    if(caps & V4L2_CAP_ASYNCIO)
        caps_list.push_back("V4L2_CAP_ASYNCIO");
    if(caps & V4L2_CAP_STREAMING)
        caps_list.push_back("V4L2_CAP_STREAMING");
    if(caps & V4L2_CAP_META_OUTPUT)
        caps_list.push_back("V4L2_CAP_META_OUTPUT");
    if(caps & V4L2_CAP_TOUCH)
        caps_list.push_back("V4L2_CAP_TOUCH");
    if(caps & V4L2_CAP_IO_MC)
        caps_list.push_back("V4L2_CAP_IO_MC");
    
    // found capabilities
    for(const auto& cap_name : caps_list){
        printf("\t%s\n", cap_name);
    }
}

void print_drmModeRes(drmModeResPtr res)
{
    if(!res){
        printf("%s: drmModeRes is NULL\n", __func__);
        return;
    }

    printf("=== DRM Mode Resources ===\n");
    printf("Framebuffers: %d\n", res->count_fbs);
    for(int i = 0; i < res->count_fbs; i++){
        printf("  FB[%d]: %u\n", i, res->fbs[i]);
    }

    printf("CRTCs: %d\n", res->count_crtcs);
    for(int i = 0; i < res->count_crtcs; i++){
        printf("  CRTC[%d]: %u\n", i, res->crtcs[i]);
    }

    printf("Connectors: %d\n", res->count_connectors);
    for(int i = 0; i < res->count_connectors; i++){
        printf("  Connector[%d]: %u\n", i, res->connectors[i]);
    }

    printf("Encoders: %d\n", res->count_encoders);
    for(int i = 0; i < res->count_encoders; i++){
        printf("  Encoder[%d]: %u\n", i, res->encoders[i]);
    }

    printf("Display size range:\n");
    printf("  Width:  %u - %u\n", res->min_width, res->max_width);
    printf("  Height: %u - %u\n", res->min_height, res->max_height);
    printf("==========================\n");
}
