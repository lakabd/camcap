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

#define APP_VERBOSITY false

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
    int ret = 0;

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Init display
    display_config conf;
    conf.testing_display = true;
    Display disp(conf, APP_VERBOSITY);
    
    printf("[MAIN] Initialize display...\n");
    ret = disp.initialize();
    if(!ret){
        printf("[MAIN] Error on display initialize() !\n");
        return -1;
    }

    // Init Polls
    struct pollfd fds;
    fds.fd = disp.get_fd();
    fds.events = POLLIN; // Wake up when VSync/Flip event happens

    // Scanout
    printf("[MAIN] Starting loop (Press Ctrl+C to exit)...\n");

    while(running){
        ret = poll(&fds, 1, -1); // Wait indefinitely for an event

        if(ret > 0){
            if (fds.revents & POLLIN) {
                if(!disp.handleEvent()){
                    printf("[MAIN] Error on display handleEvent() !\n");
                    return -1;
                }
            }
            if(!disp.flipPending()){
                if(!disp.scanout(0)){
                    printf("[MAIN] Error on display scanout() !\n");
                    return -1;
                }
            }
        }
    }

    printf("[MAIN] Exiting...\n");
    return 0;
}