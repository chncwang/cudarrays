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

#include <iostream>

#include <cudarrays/common.hpp>

#include <cudarrays/dynarray.hpp>
#include <cudarrays/launch.hpp>

namespace cudarrays {
void init_lib();
}

#include "matrixadd_kernel.cuh"

using namespace cudarrays;

bool TEST = true;

array_size_t BLOCK_ELEMS = 8;
array_size_t MATRIXADD_ELEMS[3] = { 32, 128, 256 };

template <typename StorageC, typename StorageA = StorageC, typename StorageB = StorageC>
bool
launch_test_matrixadd(compute_conf<3> gpus, std::array<int, 3> infoC,
                                            std::array<int, 3> infoA,
                                            std::array<int, 3> infoB)
{
    if (gpus.procs > config::MAX_GPUS) return false;

    static const array_size_t ELEMS_Z = MATRIXADD_ELEMS[0];
    static const array_size_t ELEMS_Y = MATRIXADD_ELEMS[1];
    static const array_size_t ELEMS_X = MATRIXADD_ELEMS[2];

    using array3D_matrixadd = float [ELEMS_Z][ELEMS_Y][ELEMS_X];
    using my_arrayA = volume<float, layout::rmo, StorageA>;
    using my_arrayB = volume<float, layout::rmo, StorageB>;
    using my_arrayC = volume<float, layout::rmo, StorageC>;

    my_arrayA A({ELEMS_Z, ELEMS_Y, ELEMS_X});
    my_arrayB B({ELEMS_Z, ELEMS_Y, ELEMS_X});
    my_arrayC C({ELEMS_Z, ELEMS_Y, ELEMS_X});

    A.template distribute<3>({gpus, infoA});
    B.template distribute<3>({gpus, infoB});
    C.template distribute<3>({gpus, infoC});

    array3D_matrixadd &A_host = *(array3D_matrixadd *) new float[ELEMS_Z * ELEMS_Y * ELEMS_X];
    array3D_matrixadd &B_host = *(array3D_matrixadd *) new float[ELEMS_Z * ELEMS_Y * ELEMS_X];
    array3D_matrixadd &C_host = *(array3D_matrixadd *) new float[ELEMS_Z * ELEMS_Y * ELEMS_X];

    {
        for (auto i = 0u; i < ELEMS_Z; ++i) {
            for (auto j = 0u; j < ELEMS_Y; ++j) {
                for (auto k = 0u; k < ELEMS_X; ++k) {
                    A(i, j, k)      = float(i * ELEMS_Y * ELEMS_X) + j * ELEMS_X + k;
                    A_host[i][j][k] = float(i * ELEMS_Y * ELEMS_X) + j * ELEMS_X + k;

                    B(i, j, k)      = float(i + 1.f);
                    B_host[i][j][k] = float(i + 1.f);

                    C(i, j, k)      = 0.f;
                    C_host[i][j][k] = 0.f;
                }
            }
        }
    }

    cuda_conf conf{dim3(ELEMS_X / BLOCK_ELEMS,
            ELEMS_Y / BLOCK_ELEMS,
            ELEMS_Z / BLOCK_ELEMS),
              dim3(BLOCK_ELEMS,
                      BLOCK_ELEMS,
                      BLOCK_ELEMS)};

    auto launched = launch_async(matrixadd_kernel<StorageC, StorageA, StorageC>, conf, gpus)(C, A, B);

    if (TEST)
    {
        #pragma omp parallel for
        for (auto i = 0u; i < ELEMS_Z; ++i) {
            for (auto j = 0u; j < ELEMS_Y; ++j) {
                for (auto k = 0u; k < ELEMS_X; ++k) {
                    C_host[i][j][k] = A_host[i][j][k] + B_host[i][j][k];
                }
            }
        }
    }

    bool ok = true;
    if (TEST && launched.get())
    {
        for (auto i = 0u; i < ELEMS_Z; ++i) {
            for (auto j = 0u; j < ELEMS_Y; ++j) {
                for (auto k = 0u; k < ELEMS_X; ++k) {
                    if (C_host[i][j][k] != C(i, j, k)) {
                        std::cout << "C: Position {" << i << ", " << j << ", " << k << "} "
                                                     << C_host[i][j][k]
                                                     << " vs "
                                                     << C(i, j, k) << std::endl;
                        ok = false;
                        abort();
                    }
                }
            }
        }
    }

    delete [] &A_host;
    delete [] &B_host;
    delete [] &C_host;

    return ok;
}

static const int NONE = -1;

template <typename Impl>
void test_conf(unsigned gpus)
{
    bool ok = false;

    ok = launch_test_matrixadd<typename Impl::x>({partition::x, gpus},
                                                 {NONE, NONE, 0},
                                                 {NONE, NONE, 0},
                                                 {NONE, NONE, 0});
    printf("%s X %u: %d\n", Impl::name, gpus, ok);
    ok = launch_test_matrixadd<typename Impl::y>({partition::y, gpus},
                                                 {NONE, 1, NONE},
                                                 {NONE, 1, NONE},
                                                 {NONE, 1, NONE});
    printf("%s Y %u: %d\n", Impl::name, gpus, ok);
    ok = launch_test_matrixadd<typename Impl::z>({partition::z, gpus},
                                                 {2, NONE, NONE},
                                                 {2, NONE, NONE},
                                                 {2, NONE, NONE});
    printf("%s Z %u: %d\n", Impl::name, gpus, ok);

    if (gpus == 1 || gpus >= 4) {
        ok = launch_test_matrixadd<typename Impl::xy>({partition::xy, gpus},
                                                      {NONE, 1, 0},
                                                      {NONE, 1, 0},
                                                      {NONE, 1, 0});
        printf("%s XY %u: %d\n", Impl::name, gpus, ok);
        ok = launch_test_matrixadd<typename Impl::xy>({partition::xy, gpus},
                                                      {NONE, 0, 1},
                                                      {NONE, 0, 1},
                                                      {NONE, 0, 1});
        printf("%s YX %u: %d\n", Impl::name, gpus, ok);
        ok = launch_test_matrixadd<typename Impl::xz>({partition::xz, gpus},
                                                      {2, NONE, 0},
                                                      {2, NONE, 0},
                                                      {2, NONE, 0});
        printf("%s XZ %u: %d\n", Impl::name, gpus, ok);
        ok = launch_test_matrixadd<typename Impl::xz>({partition::xz, gpus},
                                                      {0, NONE, 2},
                                                      {0, NONE, 2},
                                                      {0, NONE, 2});
        printf("%s ZX %u: %d\n", Impl::name, gpus, ok);
        ok = launch_test_matrixadd<typename Impl::yz>({partition::yz, gpus},
                                                      {2, 1, NONE},
                                                      {2, 1, NONE},
                                                      {2, 1, NONE});
        printf("%s YZ %u: %d\n", Impl::name, gpus, ok);
        ok = launch_test_matrixadd<typename Impl::yz>({partition::yz, gpus},
                                                      {1, 2, NONE},
                                                      {1, 2, NONE},
                                                      {1, 2, NONE});
        printf("%s ZY %u: %d\n", Impl::name, gpus, ok);
    }
}

int main()
{
    init_lib();

    for (unsigned gpus : {1, 2, 4}) {
        test_conf<replicate>(gpus);
        test_conf<vm>(gpus);
        test_conf<reshape>(gpus);
#if 0
        test_conf<reshape_cyclic>(gpus);
        test_conf<reshape_block_cyclic>(gpus);
#endif
    }

    return 0;
}

/* vim:set backspace=2 tabstop=4 shiftwidth=4 textwidth=120 foldmethod=marker expandtab: */
