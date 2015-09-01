/*
 * CUDArrays is a library for easy multi-GPU program development.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2015 Barcelona Supercomputing Center and
 *                         University of Illinois
 *
 *  Developed by: Javier Cabezas <javier.cabezas@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. */

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstddef>

#include <cuda_runtime.h>

#include "cudarrays/common.hpp"
#include "cudarrays/memory.hpp"
#include "cudarrays/system.hpp"
#include "cudarrays/utils.hpp"

#include "cudarrays/detail/utils/log.hpp"

namespace cudarrays {

static std::atomic_flag initialized = ATOMIC_FLAG_INIT;
static volatile bool initializing = true;

void init_lib()
{
    // Only first thread initializes
    if (initialized.test_and_set()) {
        // Wait for other threads to finish library initialization
        while (initializing);
        return;
    }

    DEBUG("Inizializing CUDArrays");

    // TODO: add checks for valid alignment alues

    if (system::MAX_GPUS.value() != 0)
        DEBUG("- Max GPUS: %zd", system::MAX_GPUS.value());
    else
        DEBUG("- Max GPUS: autodetect");

    cudaError_t err;

    int devices;
    err = cudaGetDeviceCount(&devices);
    ASSERT(err == cudaSuccess);

    if (system::MAX_GPUS.value() == 0)
        system::GPUS = devices;

    for (int d1 = 0; d1 < devices; ++d1) {
        err = cudaSetDevice(d1);
        ASSERT(err == cudaSuccess);
        unsigned peers = 1;
        for (int d2 = 0; d2 < devices; ++d2) {
            if (d1 != d2) {
                int access;
                err = cudaDeviceCanAccessPeer(&access, d1, d2);
                ASSERT(err == cudaSuccess);

                if (access) {
                    err = cudaDeviceEnablePeerAccess(d2, 0);
                    ASSERT(err == cudaSuccess);

                    ++peers;
                }
            }
        }
#if CUDARRAYS_DEBUG_CUDA == 1
        err = cudaSetDevice(d1);
        ASSERT(err == cudaSuccess);
        size_t value;
        err = cudaDeviceGetLimit(&value, cudaLimitStackSize);
        ASSERT(err == cudaSuccess);
        err = cudaDeviceSetLimit(cudaLimitStackSize, value * 4);
        ASSERT(err == cudaSuccess);

        printf("GPU %u: Increasing stack size to %zd\n", d1, value * 2);
#endif

        system::PEER_GPUS = std::max(system::PEER_GPUS, peers);
    }

    handler_sigsegv_overload();

    DEBUG("- GPUS: %zd", system::GPUS);
    DEBUG("- Peer GPUS: %zd", system::PEER_GPUS);

    // Merge information provided by the compiler
    cudarrays_compiler_register_info__();

    initializing = false;
}

//__attribute__((destructor(65535)))
void fini_lib()
{
    handler_sigsegv_restore();
}

}

/* vim:set ft=cpp backspace=2 tabstop=4 shiftwidth=4 textwidth=120 foldmethod=marker expandtab: */
