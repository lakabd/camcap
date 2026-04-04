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
#include <queue>

#include "helpers.hpp"
#include "display.hpp"
#include "capture.hpp"

#define CAPTURE_VERBOSITY false
#define DISPLAY_VERBOSITY true

#define ISP_MAINPATH    "/dev/video11"

static std::atomic<bool> running(true);
int main_ret = 0;

void signalHandler(int signal)
{
    if(signal == SIGINT){
        running = false;
        main_ret = 0;
    }
}

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;
    __u32 current_buf_index = 0;
    int previous_buf_index = -1;
    std::queue<__u32> display_queue; // list of buffers ready to scanout
    bool zero_frame_drop = false;
    std::array<int, DRM_MAX_PLANES_PER_FRAME> dma_fds;

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Init capture
    capture_config cam_conf;
    cam_conf.buf.fourcc = "NM21";
    cam_conf.buf.width  = 1920;
    cam_conf.buf.height = 1080;
    cam_conf.buf_count = 5;
    Capture cap(ISP_MAINPATH, cam_conf, CAPTURE_VERBOSITY);

    printf("[MAIN] Initialize camera...\n");
    if(!cap.initialize()){
        printf("[MAIN] Error on capture initialize() !\n");
        return -1;
    }

    // Init display
    display_config disp_conf;
    disp_conf.cam_buf = cap.get_config().buf;
    disp_conf.testing_display = false;
    Display disp(disp_conf, DISPLAY_VERBOSITY);
    
    printf("[MAIN] Initialize display...\n");
    if(!disp.initialize()){
        printf("[MAIN] Error on display initialize() !\n");
        return -1;
    }

    // Init Polls
    struct pollfd fds[2];
    fds[0].fd = cap.get_fd();
    fds[0].events = POLLIN; // Wake up on buffer ready
    fds[1].fd = disp.get_fd();
    fds[1].events = POLLIN; // Wake up when VSync/Flip event happens

    // Start camera
    if(!cap.start()){
        printf("[MAIN] Error on capture start() !\n");
        return -1;
    }

    // Scanout
    printf("[MAIN] Starting loop (Press Ctrl+C to exit)...\n");

    // Throughput optimized loop
    auto zerodrop_loop = [&](){
        while(running){
            int poll_result = poll(fds, 2, 5000); // Wait for an event

            if(poll_result > 0){
                // Capture ready
                if(fds[0].revents & POLLIN){
                    // Dequeue buffer
                    DequeueStatus s = cap.dequeueBuffer(&current_buf_index);
                    if(s == DequeueStatus::Error){
                        printf("[MAIN] Error on capture dequeueBuffer() !\n");
                        main_ret = -1;
                        break;
                    }
                    else if(s == DequeueStatus::Success){
                        // Fill display queue
                        display_queue.push(current_buf_index);
                    }
                }
                // Display ready
                if(fds[1].revents & POLLIN){
                    // Queue back previously displayed buffer
                    if(previous_buf_index >= 0){
                        if(!cap.queueBuffer(static_cast<__u32>(previous_buf_index))){
                            printf("[MAIN] Error on capture queueBuffer() !\n");
                            main_ret = -1;
                            break;
                        }
                        previous_buf_index = -1;
                    }
                    // Update pageflip status
                    if(!disp.handleEvent()){
                        printf("[MAIN] Error on display handleEvent() !\n");
                        main_ret = -1;
                        break;
                    }
                }
                // Scanout
                if(!display_queue.empty() && !disp.flipPending()){
                    current_buf_index = display_queue.front();
                    if(!cap.get_buffer_dmafds(current_buf_index, dma_fds)){
                        printf("[MAIN] Error on capture get_buffer_dmafd() !\n");
                        main_ret = -1;
                        break;
                    }
                    if(!disp.scanout(dma_fds)){
                        printf("[MAIN] Error on display scanout() !\n");
                        main_ret = -1;
                        break;
                    }
                    display_queue.pop(); // Pop display queue
                    previous_buf_index = current_buf_index;
                }
            }
            else if(poll_result == 0){
                printf("[MAIN] poll timeout!\n");
                continue;
            }
            else {
                if(errno != EINTR){
                    printf("[MAIN] poll failure: %s !\n", strerror(errno));
                    main_ret = -1;
                }
                break;
            }
        }
    };

    // Latency optimized loop
    auto realtime_loop = [&](){
        while(running){
            int poll_result = poll(fds, 2, 5000); // Wait for an event

            if(poll_result > 0){
                // Display ready
                if(fds[1].revents & POLLIN){
                    // Queue back previously displayed buffer
                    if(previous_buf_index >= 0){
                        if(!cap.queueBuffer(static_cast<__u32>(previous_buf_index))){
                            printf("[MAIN] Error on capture queueBuffer() !\n");
                            main_ret = -1;
                            break;
                        }
                        previous_buf_index = -1;
                    }
                    // Update pageflip status
                    if(!disp.handleEvent()){
                        printf("[MAIN] Error on display handleEvent() !\n");
                        main_ret = -1;
                        break;
                    }
                }

                // Capture ready
                if(fds[0].revents & POLLIN){
                    // Dequeue buffer
                    DequeueStatus s = cap.dequeueBuffer(&current_buf_index);
                    if(s == DequeueStatus::Error){
                        printf("[MAIN] Error on capture dequeueBuffer() !\n");
                        main_ret = -1;
                        break;
                    }
                    // Scanout
                    if(!disp.flipPending()){
                        if(!cap.get_buffer_dmafds(current_buf_index, dma_fds)){
                            printf("[MAIN] Error on capture get_buffer_dmafd() !\n");
                            main_ret = -1;
                            break;
                        }
                        if(!disp.scanout(dma_fds)){
                            printf("[MAIN] Error on display scanout() !\n");
                            main_ret = -1;
                            break;
                        }
                        previous_buf_index = current_buf_index;
                    }
                    else {
                        printf("[MAIN] Dropping buffer index %d\n", current_buf_index);
                        if(!cap.queueBuffer(static_cast<__u32>(current_buf_index))){
                            printf("[MAIN] Error on capture queueBuffer() !\n");
                            main_ret = -1;
                            break;
                        }
                    }
                }
            }
            else if(poll_result == 0){
                printf("[MAIN] poll timeout!\n");
                continue;
            }
            else {
                if(errno != EINTR){
                    printf("[MAIN] poll failure: %s !\n", strerror(errno));
                    main_ret = -1;
                }
                break;
            }
        }
    };

    // Main loop
    if(zero_frame_drop)
        zerodrop_loop();
    else
        realtime_loop();

    printf("[MAIN] Exiting...\n");

    // Stop Capture
    cap.stop();

    return main_ret;
}