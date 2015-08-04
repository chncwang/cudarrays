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

#pragma once
#ifndef CUDARRAYS_DETAIL_DYNARRAY_BASE_HPP_
#define CUDARRAYS_DETAIL_DYNARRAY_BASE_HPP_

#include "../../common.hpp"
#include "../../dim_manager.hpp"
#include "../../host.hpp"
#include "../../storage.hpp"

#include "indexing.hpp"

namespace cudarrays {

template <typename T, unsigned Dims>
class dynarray_base {
public:
    using  dim_manager_type = dim_manager<T, Dims>;
    dynarray_base(const extents<Dims> &extents,
                  const align_t &align) :
        dimManager_(extents, align)
    {
    }

    virtual ~dynarray_base()
    {
    }

    inline __host__ __device__
    const dim_manager_type &
    get_dim_manager() const
    {
        return dimManager_;
    }

    virtual void set_current_gpu(unsigned /*idx*/) {}
    virtual void to_device(host_storage &host) = 0;
    virtual void to_host(host_storage &host) = 0;

private:
    dim_manager_type dimManager_;
};

template <typename T, storage_tag StorageImpl, typename StorageTraits>
class dynarray_storage;

}

#endif

/* vim:set backspace=2 tabstop=4 shiftwidth=4 textwidth=120 foldmethod=marker expandtab: */
