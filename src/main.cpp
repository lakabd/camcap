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
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <poll.h>

#include "helpers.hpp"
#include "display.hpp"
#include "capture.hpp"

#define CAPTURE_VERBOSITY true
#define DISPLAY_VERBOSITY false

#define ISP_MAINPATH    "/dev/video11"

static std::atomic<bool> running(true);

void signalHandler(int signal)
{
    if(signal == SIGINT){
        running = false;
    }
}

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;
    bool ret = 0;

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Init capture
    capture_config cam_conf;
    cam_conf.buf.fourcc = "NV12";
    cam_conf.buf.width  = 1920;
    cam_conf.buf.height = 1080;
    cam_conf.buf_count = 5;
    Capture cap(ISP_MAINPATH, cam_conf, CAPTURE_VERBOSITY);

    printf("[MAIN] Initialize camera...\n");
    ret = cap.initialize();
    if(!ret){
        printf("[MAIN] Error on capture initialize() !\n");
        return -1;
    }

    // Init display
    display_config disp_conf;
    disp_conf.cam_buf = cap.get_config().buf;
    disp_conf.testing_display = false;
    Display disp(disp_conf, DISPLAY_VERBOSITY);
    
    printf("[MAIN] Initialize display...\n");
    ret = disp.initialize();
    if(!ret){
        printf("[MAIN] Error on display initialize() !\n");
        return -1;
    }

    // Init Polls
    struct pollfd fds[2];
    fds[0].fd = disp.get_fd();
    fds[0].events = POLLIN; // Wake up when VSync/Flip event happens
    fds[1].fd = cap.get_fd();
    fds[1].events = POLLIN; // Wake up on buffer ready

    // Start camera
    ret = cap.start();
    if(!ret){
        printf("[MAIN] Error on capture start() !\n");
        return -1;
    }

    // Scanout
    printf("[MAIN] Starting loop (Press Ctrl+C to exit)...\n");

    while(running){
        ret = poll(fds, 2, -1); // Wait indefinitely for an event

        if(ret > 0){
            // Display ready
            if (fds[0].revents & POLLIN) {
                // Event callback
                if(!disp.handleEvent()){
                    printf("[MAIN] Error on display handleEvent() !\n");
                    return -1;
                }
            }
            // Capture ready
            if(fds[1].revents & POLLIN){
                // Dequeue buffer
                __u32 buf_index = 0;
                DequeueStatus s = cap.dequeueBuffer(&buf_index);
                if(s == DequeueStatus::Error){
                    printf("[MAIN] Error on capture dequeueBuffer() !\n");
                    return -1;
                }
                else if(s == DequeueStatus::NoBufferInQueue){
                    continue;
                }
                // save one frame 
                //cap.saveOneFrame(buf_index, "./frame.yuv");

                // If display ready, send buffer
                if(!disp.flipPending()){
                    if(!disp.scanout(cap.get_buffer_dmafd(buf_index))){
                        printf("[MAIN] Error on display scanout() !\n");
                        return -1;
                    }
                }
                // Wait for display to finish. TODO: replace by a pending queue
                usleep(5000);

                // Push buffer back
                ret = cap.queueBuffer(buf_index);
                if(!ret){
                    printf("[MAIN] Error on capture queueBuffer() !\n");
                    return -1;
                }
            }
        }
    }

    printf("[MAIN] Exiting...\n");
    return 0;
}