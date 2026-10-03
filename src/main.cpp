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

#include "helpers.hpp"
#include "display.hpp"
#include "capture.hpp"

#define CAPTURE_VERBOSITY false
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

    int exit_code = 0;

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Init capture
    capture_config cam_conf;
    cam_conf.buf.fourcc = "NV12";
    cam_conf.buf.width  = 1280;
    cam_conf.buf.height = 720;
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
    disp_conf.frames_queue = cap.get_queue();
    disp_conf.testing_display = false;
    Display disp(disp_conf, DISPLAY_VERBOSITY);
    
    printf("[MAIN] Initialize display...\n");
    if(!disp.initialize()){
        printf("[MAIN] Error on display initialize() !\n");
        return -1;
    }

    cap.start();
    disp.start();

    printf("[MAIN] Starting loop (Press Ctrl+C to exit)...\n");

    while(running){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(!cap.is_healthy()){
            printf("[MAIN] Fatal: capture node died!\n");
            exit_code = -1;
            break;
        }
        if(!disp.is_healthy()){
            printf("[MAIN] Fatal: display node died!\n");
            exit_code = -1;
            break;
        }
    }

    printf("[MAIN] Exiting...\n");

    disp.stop();
    cap.stop();

    return exit_code;
}