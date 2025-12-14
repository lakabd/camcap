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

#include "helpers.hpp"
#include "display.hpp"

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

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);

    // Init display
    Display disp(true);
    printf("[MAIN] Initialize display...\n");
    disp.initialize();

    // Scanout
    printf("[MAIN] Starting loop (Press Ctrl+C to exit)...\n");
    while(running){
        disp.scanout();
    }

    printf("[MAIN] Exiting...\n");
    return 0;
}