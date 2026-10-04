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
#include <cstdio>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

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
    int exit_code = 0;

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Command line parsing
    std::string device;                 // -d (mandatory)
    unsigned int width = 1920;          // -w
    unsigned int height = 1080;         // -h
    std::string fourcc = "NV12";        // -f
    bool verbosity_all  = false;        // -v: verbosity
    bool verbosity_cap  = false;
    bool verbosity_disp = false;

    CLI::App app{"Camera Capture Utility"};

    app.footer( "Examples:\n"
        "  Capture a NV12 stream @ 1280x720:  camcap -d /dev/video11 -w 1280 -h 720 -f NV12\n"
    );

    app.set_help_flag("--help", "Print this help message and exit");

    app.add_option("-d,--device", device, "V4L2 video device to use (e.g. /dev/video11)");
    auto *opt_width = app.add_option("-w,--width", width, "Source width in pixels (e.g. 1280)")
        ->check(CLI::PositiveNumber);
    auto *opt_height = app.add_option("-h,--height", height, "Source height in pixels (e.g. 720)")
        ->check(CLI::PositiveNumber);
    auto *opt_fourcc = app.add_option("-f,--fourcc", fourcc, "V4L2 pixel format fourcc name (e.g. NV12)")
        ->check([](const std::string& s){
            return (!s.empty() && s.length() == 4) ? "" : std::string("must be 4 characters (e.g. NV12)");
        }, "FOURCC");
    app.add_flag("-v", verbosity_all, "Verbose logging");
    app.add_flag("--vcap", verbosity_cap, "");
    app.add_flag("--vdisp", verbosity_disp, "");

    CLI11_PARSE(app, argc, argv);

    // -d is mandatory
    if(device.empty()){
        printf("[MAIN] Error: you must at least specify the device to use (e.g. -d /dev/video1)\n\n");
        std::cout << app.help();
        return -1;
    }

    // Notify when defaults are used
    if(opt_fourcc->count() == 0)
        printf("[MAIN] -f not specified, using default format: %s\n", fourcc.c_str());
    if(opt_width->count() == 0)
        printf("[MAIN] -w not specified, using default width: %u\n", width);
    if(opt_height->count() == 0)
        printf("[MAIN] -h not specified, using default height: %u\n", height);

    // Set verbosity: -v = both, --vcap = capture only, --vdisp = display only
    bool cap_verbose  = verbosity_all || verbosity_cap;
    bool disp_verbose = verbosity_all || verbosity_disp;

    // Init capture
    capture_config cam_conf;
    cam_conf.buf.fourcc = fourcc;
    cam_conf.buf.width  = width;
    cam_conf.buf.height = height;
    cam_conf.buf_count = 5;
    Capture cap(device, cam_conf, cap_verbose);

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
    Display disp(disp_conf, disp_verbose);
    
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