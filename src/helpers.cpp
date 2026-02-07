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

void print_drmModeRes(drmModeRes *res)
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

const char* get_connector_type_name(uint32_t type)
{
    switch(type){ 
        case DRM_MODE_CONNECTOR_Unknown: return "Unknown";
        case DRM_MODE_CONNECTOR_VGA: return "VGA";
        case DRM_MODE_CONNECTOR_DVII: return "DVI-I";
        case DRM_MODE_CONNECTOR_DVID: return "DVI-D";
        case DRM_MODE_CONNECTOR_DVIA: return "DVI-A";
        case DRM_MODE_CONNECTOR_Composite: return "Composite";
        case DRM_MODE_CONNECTOR_SVIDEO: return "S-Video";
        case DRM_MODE_CONNECTOR_LVDS: return "LVDS";
        case DRM_MODE_CONNECTOR_Component: return "Component";
        case DRM_MODE_CONNECTOR_9PinDIN: return "9PinDIN";
        case DRM_MODE_CONNECTOR_DisplayPort: return "DisplayPort";
        case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
        case DRM_MODE_CONNECTOR_HDMIB: return "HDMI-B";
        case DRM_MODE_CONNECTOR_TV: return "TV";
        case DRM_MODE_CONNECTOR_eDP: return "eDP";
        case DRM_MODE_CONNECTOR_VIRTUAL: return "Virtual";
        case DRM_MODE_CONNECTOR_DSI: return "DSI";
        case DRM_MODE_CONNECTOR_DPI: return "DPI";
        case DRM_MODE_CONNECTOR_WRITEBACK: return "Writeback";
        case DRM_MODE_CONNECTOR_SPI: return "SPI";
        case DRM_MODE_CONNECTOR_USB: return "USB";
        default: return "Unknown";
    }
}

void print_drmModeConnector(int drmfd, drmModeConnector *conn)
{
    if(!conn){
        printf("%s: drmModeConnector is NULL\n", __func__);
        return;
    }

    printf("=== DRM Connector ===\n");
    printf("Connector ID: %u\n", conn->connector_id);
    printf("Encoder ID: %u\n", conn->encoder_id);
    printf("Connector Type: %s\n", get_connector_type_name(conn->connector_type));
    
    printf("Connection Status: ");
    switch(conn->connection){
        case DRM_MODE_CONNECTED:
            printf("CONNECTED\n");
            break;
        case DRM_MODE_DISCONNECTED:
            printf("DISCONNECTED\n");
            break;
        case DRM_MODE_UNKNOWNCONNECTION:
            printf("UNKNOWN\n");
            break;
        default:
            printf("%d\n", conn->connection);
            break;
    }
    
    printf("Physical Size: %u x %u mm\n", conn->mmWidth, conn->mmHeight);
    printf("Subpixel: %u\n", conn->subpixel);
    
    printf("\nModes: %d\n", conn->count_modes);
    for(int i = 0; i < conn->count_modes; i++){
        drmModeModeInfoPtr mode = &conn->modes[i];
        printf("  Mode[%d]: %s - %ux%u @%uHz\n", 
               i, mode->name, mode->hdisplay, mode->vdisplay, mode->vrefresh);
    }
    
    printf("\nProperties: %d\n", conn->count_props);
    for(int i = 0; i < conn->count_props; i++){
        drmModePropertyPtr prop = drmModeGetProperty(drmfd, conn->props[i]);
        if(prop){
            printf(" ID=%u -> %s \t= ", conn->props[i], prop->name);
            
            // Print value based on property type
            if(prop->flags & DRM_MODE_PROP_BLOB){
                printf("[blob: %lu]\n", conn->prop_values[i]);
            } 
            else if(prop->flags & DRM_MODE_PROP_ENUM){
                // For enums, find the name
                for(int j = 0; j < prop->count_enums; j++){
                    if(prop->enums[j].value == conn->prop_values[i]){
                        printf("%s\n", prop->enums[j].name);
                        break;
                    }
                }
            }
            else{
                printf("%lu\n", conn->prop_values[i]);
            }
            
            drmModeFreeProperty(prop);
        }
    }
    
    printf("\nEncoders: %d\n", conn->count_encoders);
    for(int i = 0; i < conn->count_encoders; i++){
        printf("  Encoder[%d]: %u\n", i, conn->encoders[i]);
    }
    printf("====================\n");
}

const char* get_encoder_type_name(uint32_t type)
{
    switch(type){
        case DRM_MODE_ENCODER_NONE: return "None";
        case DRM_MODE_ENCODER_DAC: return "DAC";
        case DRM_MODE_ENCODER_TMDS: return "TMDS";
        case DRM_MODE_ENCODER_LVDS: return "LVDS";
        case DRM_MODE_ENCODER_TVDAC: return "TVDAC";
        case DRM_MODE_ENCODER_VIRTUAL: return "Virtual";
        case DRM_MODE_ENCODER_DSI: return "DSI";
        case DRM_MODE_ENCODER_DPMST: return "DPMST";
        case DRM_MODE_ENCODER_DPI: return "DPI";
        default: return "Unknown";
    }
}

void print_drmModeEncoder(drmModeEncoder *enc)
{
    if(!enc){
        printf("%s: drmModeEncoder is NULL\n", __func__);
        return;
    }

    printf("=== DRM Encoder ===\n");
    printf("Encoder ID: %u\n", enc->encoder_id);
    printf("Encoder Type: %s (%u)\n", get_encoder_type_name(enc->encoder_type), enc->encoder_type);
    printf("Current CRTC ID: %u\n", enc->crtc_id);
    
    printf("Possible CRTCs: 0x%08x (bitmask)\n", enc->possible_crtcs);
    printf("  Compatible CRTC indices: ");
    bool first = true;
    for(int i = 0; i < 32; i++){
        if(enc->possible_crtcs & (1 << i)){
            if(!first){
                printf(", ");
            }
            printf("%d", i);
            first = false;
        }
    }
    printf("\n");
    
    printf("Possible Clones: 0x%08x (bitmask)\n", enc->possible_clones);
    printf("===================\n");
}

void print_drmModeCrtc(drmModeCrtc *crtc)
{
    if(!crtc){
        printf("%s: drmModeCrtc is NULL\n", __func__);
        return;
    }

    printf("=== DRM CRTC ===\n");
    printf("CRTC ID: %u\n", crtc->crtc_id);
    printf("Buffer ID: %u%s\n", crtc->buffer_id, crtc->buffer_id == 0 ? " (disconnected)" : "");
    printf("Position: (%u, %u)\n", crtc->x, crtc->y);
    printf("Size: %u x %u\n", crtc->width, crtc->height);
    printf("Mode Valid: %s\n", crtc->mode_valid ? "Yes" : "No");
    
    if(crtc->mode_valid){
        printf("Current Mode:\n");
        printf("  Name: %s\n", crtc->mode.name);
        printf("  Resolution: %u x %u\n", crtc->mode.hdisplay, crtc->mode.vdisplay);
        printf("  Refresh Rate: %u Hz\n", crtc->mode.vrefresh);
        printf("  Clock: %u kHz\n", crtc->mode.clock);
    }
    
    printf("Gamma Size: %d\n", crtc->gamma_size);
    printf("================\n");
}

void print_drmModePlane(drmModePlane *plane)
{
    if(!plane){
        printf("%s: drmModePlane is NULL\n", __func__);
        return;
    }

    printf("=== DRM Plane ===\n");
    printf("Plane ID: %u\n", plane->plane_id);
    printf("CRTC ID: %u\n", plane->crtc_id);
    printf("FB ID: %u\n", plane->fb_id);
    printf("CRTC Position: (%u, %u)\n", plane->crtc_x, plane->crtc_y);
    printf("Source Position: (%u, %u)\n", plane->x, plane->y);
    printf("Possible CRTCs: 0x%08x\n", plane->possible_crtcs);
    printf("Gamma Size: %u\n", plane->gamma_size);
    printf("Formats count: %u\n", plane->count_formats);
    for(uint32_t i = 0; i < plane->count_formats; i++){
        uint32_t fmt = plane->formats[i];
        printf("  Format[%u]: %c%c%c%c (0x%08x)\n", i,
               (fmt) & 0xFF, (fmt >> 8) & 0xFF, (fmt >> 16) & 0xFF, (fmt >> 24) & 0xFF, fmt);
    }
    printf("=================\n");
}
