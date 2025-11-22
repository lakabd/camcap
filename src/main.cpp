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

#include "helpers.hpp"
#include "capture.hpp"

#define ISP_MAINPATH    "/dev/video11"

int main(int argc, char* argv[]){

    (void) argc;
    (void) argv;

    // Init capture
    capture_config conf;
    conf.fmt_fourcc = "NV12";
    conf.width = 1920;
    conf.height = 1080;
    conf.mem_type = TYPE_MMAP;
    conf.buf_count = 5;

    printf("[MAIN] Starting Capture...\n");

    Capture capture(ISP_MAINPATH, conf, true);
    
    if(!capture.start()){
        printf("start Faild !\n");
        return -1;
    }

    printf("[MAIN] Saving one Frame...\n");

    if(!capture.saveToFile("frame.yuv")){
        printf("saveToFile Faild !\n");
        return -1;
    }

    printf("[MAIN] Stoping Capture...\n");

    if(!capture.stop()){
        printf("stop Faild !\n");
        return -1;
    }

    return 0;
}