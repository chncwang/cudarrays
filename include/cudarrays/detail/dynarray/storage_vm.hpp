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
#ifndef CUDARRAYS_DETAIL_DYNARRAY_STORAGE_VM_HPP_
#define CUDARRAYS_DETAIL_DYNARRAY_STORAGE_VM_HPP_

#include "../../detail/utils/log.hpp"
#include "../../utils.hpp"

#include "base.hpp"

using namespace utils;

namespace cudarrays {

template <unsigned Dims>
class page_allocator {
    using extents_type = std::array<array_size_t, Dims>;

public:
    struct page_stats {
        unsigned gpu;

        array_size_t local;
        array_size_t remote;

        page_stats() :
            gpu(0),
            local(0),
            remote(0)
        {}

        array_size_t get_total() const
        {
            return local + remote;
        }

        double get_imbalance_ratio() const
        {
            return double(remote) / double(get_total());
        }
    };

    page_allocator(unsigned gpus,
                   const std::array<array_size_t, Dims> &elems,
                   const std::array<array_size_t, Dims> &elems_align,
                   const std::array<array_size_t, Dims> &elems_local,
                   const std::array<unsigned, Dims> &arrayDimToGpus,
                   array_size_t granularity) :
        gpus_(gpus),
        dims_(elems),
        dimsAlign_(elems_align),
        dimsLocal_(elems_local),
        arrayDimToGpus_(arrayDimToGpus),
        granularity_(granularity)
    {
        fill(idx_, array_index_t(0));

        nelems_ = accumulate(elems, 1, std::multiplies<array_size_t>());

        for (unsigned dim = 0; dim < Dims; ++dim) {
            mayOverflow_[dim] = elems_align[dim] % elems_local[dim] != 0;
        }
    }


    std::pair<bool, page_stats>
    advance()
    {
        bool done = false;
        array_size_t inc = granularity_;

        std::vector<array_size_t> localStats(gpus_, 0); // gpu -> local_elems

        if (idx_[0] == dimsAlign_[0]) return std::pair<bool, page_stats>(true, page_stats());

        do {
            std::array<unsigned, Dims> tileId;

            for (unsigned dim = 0; dim < Dims; ++dim) {
                tileId[dim] = idx_[dim] / dimsLocal_[dim];
            }
            unsigned gpu = 0;
            for (unsigned dim = 0; dim < Dims; ++dim) {
                gpu += tileId[dim] * arrayDimToGpus_[dim];
            }

            // Compute increment until the next tile in the lowest-order dimension
            array_index_t off = array_index_t(dimsLocal_[Dims - 1]) -
                                array_index_t(idx_[Dims - 1] % dimsLocal_[Dims - 1]);
            // Check if array dimensions are not divisible by local tile dimensions
            if (//mayOverflow_[Dims - 1] &&
                idx_[Dims - 1] + off >= dimsAlign_[Dims - 1])
                off = dimsAlign_[Dims - 1] - idx_[Dims - 1];

            if (array_index_t(inc) < off) { // We are done
                idx_[Dims - 1] += inc;

                localStats[gpu] += inc;

                inc = 0;
            } else {
                if (idx_[Dims - 1] + off == dimsAlign_[Dims - 1]) {
                    if (Dims > 1) {
                        idx_[Dims - 1] = 0;
                        // End of a dimension propagate the carry
                        for (int dim = int(Dims) - 2; dim >= 0; --dim) {
                            ++idx_[dim];

                            if (idx_[dim] < dimsAlign_[dim]) {
                                break; // We don't have to propagate the carry further
                            } else if (dim > 0 && idx_[dim] == dimsAlign_[dim]) {
                                // Set index to 0 unless for the highest-order dimension
                                idx_[dim] = 0;
                            }
                        }
                        if (idx_[0] == dimsAlign_[0]) {
                            done = true;
                            localStats[gpu] += off;
                            break;
                        }
                    } else {
                        done = true;
                        idx_[Dims - 1] += off;
                    }
                } else {
                    // Next tile within the dimension
                    idx_[Dims - 1] += off;
                }
                inc -= off;

                localStats[gpu] += off;
            }
            if (idx_[0] == dimsAlign_[0]) break;
        } while (inc > 0);

        // Implement some way to balance allocations on 50% imbalance
        unsigned mgpu = 0;
        array_size_t sum = 0;
        for (unsigned gpu = 0; gpu < gpus_; ++gpu) {
            if (localStats[gpu] > localStats[mgpu]) mgpu = gpu;

            sum += localStats[gpu];
        }

        page_stats page;
        page.gpu    = mgpu;
        page.local  = localStats[mgpu];
        page.remote = sum - localStats[mgpu];

        pageStats_.push_back(page);

        return std::pair<bool, page_stats>(done, page);
    }

    double get_imbalance_ratio() const
    {
        array_size_t local  = 0;
        array_size_t remote = 0;

        for (const page_stats &page : pageStats_) {
            local  += page.local;
            remote += page.remote;
        }

        return double(remote)/double(local + remote);
    }

private:
    unsigned gpus_;

    extents_type dims_;
    extents_type dimsAlign_;
    extents_type dimsLocal_;

    std::array<unsigned, Dims> arrayDimToGpus_;

    std::array<array_index_t, Dims> idx_;

    array_size_t nelems_;
    array_size_t granularity_;

    std::array<bool, Dims> mayOverflow_;

    std::vector<page_stats> pageStats_;

    CUDARRAYS_TESTED(storage_test, vm_page_allocator1)
    CUDARRAYS_TESTED(storage_test, vm_page_allocator2)
};

template <typename T, unsigned Dims, typename PartConf>
class dynarray_storage<T, Dims, VM, PartConf> :
    public dynarray_base<T, Dims>
{
    using base_storage_type = dynarray_base<T, Dims>;
    using  dim_manager_type = typename base_storage_type::dim_manager_type;
    using      extents_type = typename base_storage_type::extents_type;

    struct storage_host_info {
        unsigned gpus;

        extents_type localDims;
        std::array<unsigned, Dims>     arrayDimToGpus;

        storage_host_info(unsigned _gpus) :
            gpus(_gpus)
        {
        }
    };

public:
    template <unsigned DimsComp>
    __host__ bool
    distribute(const compute_mapping<DimsComp, Dims> &mapping)
    {
        if (!dataDev_) {
            hostInfo_ = new storage_host_info(mapping.comp.procs);

            if (mapping.comp.procs == 1) {
                hostInfo_->localDims = make_array(this->get_dim_manager().sizes_);
                fill(hostInfo_->arrayDimToGpus, 0);
                alloc(mapping.comp.procs);
            } else {
                compute_distribution(mapping);
                // TODO: remove when the allocation is properly done
                alloc(mapping.comp.procs);

                //DEBUG("VM> ALLOC: SHIIIIT: %s", to_string(offsGPU_, Dims - 1).c_str());
            }

            return true;
        }
        return false;
    }

    __host__ bool
    is_distributed()
    {
        return dataDev_ != NULL;
    }

    unsigned get_ngpus() const
    {
        return hostInfo_->gpus;
    }

    __host__
    void to_host()
    {
        unsigned npages;
        npages = div_ceil(this->get_host_storage().size(), config::CUDA_VM_ALIGN);

        T *src = dataDev_ - this->get_dim_manager().get_offset();
        T *dst = this->get_host_storage().get_base_addr();
        for (array_size_t idx  = 0; idx < npages; ++idx) {
            array_size_t bytesChunk = config::CUDA_VM_ALIGN;
            if ((idx + 1) * config::CUDA_VM_ALIGN > this->get_host_storage().size())
                bytesChunk = this->get_host_storage().size() - idx * config::CUDA_VM_ALIGN;

            DEBUG("COPYING TO HOST: %p -> %p (%zd)", &src[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                                     &dst[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                                     size_t(bytesChunk));
            CUDA_CALL(cudaMemcpy(&dst[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                 &src[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                 bytesChunk, cudaMemcpyDeviceToHost));
        }
    }

    __host__
    void to_device()
    {
        unsigned npages;
        npages = div_ceil(this->get_host_storage().size(), config::CUDA_VM_ALIGN);

        T *src = this->get_host_storage().get_base_addr();
        T *dst = dataDev_ - this->get_dim_manager().get_offset();
        for (array_size_t idx  = 0; idx < npages; ++idx) {
            array_size_t bytesChunk = config::CUDA_VM_ALIGN;
            if ((idx + 1) * config::CUDA_VM_ALIGN > this->get_host_storage().size())
                bytesChunk = this->get_host_storage().size() - idx * config::CUDA_VM_ALIGN;

            DEBUG("COPYING TO DEVICE: %p -> %p (%zd)", &src[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                                       &dst[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                                       size_t(bytesChunk));
            CUDA_CALL(cudaMemcpy(&dst[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                 &src[(config::CUDA_VM_ALIGN * idx)/sizeof(T)],
                                 bytesChunk, cudaMemcpyHostToDevice));
        }
    }

    __host__
    dynarray_storage(const extents_type &extents,
                  const align_t &align) :
        base_storage_type(extents, align),
        dataDev_(NULL),
        hostInfo_(NULL)
    {
    }

    __host__
    dynarray_storage(const dynarray_storage &other) :
        dim_manager_type(other),
        dataDev_(other.dataDev_),
        hostInfo_(other.hostInfo_)
    {
    }

    __host__
    virtual ~dynarray_storage()
    {
#ifndef __CUDA_ARCH__
        if (dataDev_ != NULL) {
            unsigned npages = div_ceil(this->get_host_storage().size(), config::CUDA_VM_ALIGN);

            // Update offset
            T *data = dataDev_ - this->get_dim_manager().get_offset();

            // Free data in GPU memory
            for (auto idx : utils::make_range(npages)) {
                CUDA_CALL(cudaFree(&data[config::CUDA_VM_ALIGN_ELEMS<T>() * idx]));
            }
        }

        if (hostInfo_ != NULL) {
            delete hostInfo_;
        }
#endif
    }

    __host__ __device__ inline
    T &access_pos(array_index_t idx1, array_index_t idx2, array_index_t idx3)
    {
        using my_linearizer = linearizer<Dims>;

        array_index_t pos = my_linearizer::access_pos(this->get_dim_manager().get_offs_align(), idx1, idx2, idx3);
        return
#ifndef __CUDA_ARCH__
            this->get_host_storage().get_addr()
#else
            dataDev_
#endif
            [pos];
    }

    __host__ __device__ inline
    const T &access_pos(array_index_t idx1, array_index_t idx2, array_index_t idx3) const
    {
        using my_linearizer = linearizer<Dims>;

        array_index_t pos = my_linearizer::access_pos(this->get_dim_manager().get_offs_align(), idx1, idx2, idx3);
        return
#ifndef __CUDA_ARCH__
            this->get_host_storage().get_addr()
#else
            dataDev_
#endif
            [pos];
    }

private:
    __host__
    void alloc(unsigned gpus)
    {
        using my_allocator = page_allocator<Dims>;

        extents_type elems;
        extents_type elemsAlign;

        utils::copy(this->get_dim_manager().sizes_, elems);
        std::copy(this->get_dim_manager().sizesAlign_, this->get_dim_manager().sizesAlign_ + Dims, elemsAlign.begin());

        my_allocator cursor(gpus,
                            elems,
                            elemsAlign,
                            hostInfo_->localDims,
                            hostInfo_->arrayDimToGpus,
                            config::CUDA_VM_ALIGN_ELEMS<T>());

        char *last = NULL, *curr = NULL;

        // Allocate data in the GPU memory
        for (std::pair<bool, typename my_allocator::page_stats> ret = cursor.advance();
             !ret.first || ret.second.get_total() > 0;
             ret = cursor.advance()) {
            typename my_allocator::page_stats page = ret.second;

            CUDA_CALL(cudaSetDevice(page.gpu));

            CUDA_CALL(cudaMalloc((void **) &curr, config::CUDA_VM_ALIGN));

            DEBUG("VM> ALLOCATING: %zd in %u (%zd)", config::CUDA_VM_ALIGN, page.gpu, size_t(curr) % config::CUDA_VM_ALIGN);

            DEBUG("VM> ALLOCATE: %p in %u (final: %u total: %zd)",
                  curr, page.gpu, unsigned(ret.first), size_t(ret.second.get_total()));

            if (last == NULL) {
                dataDev_ = (T *) curr;
            } else {
                // Check if the driver is allocating contiguous virtual addresses
                assert(last + config::CUDA_VM_ALIGN == curr);
            }
            std::swap(last, curr);
        }

        dataDev_ += this->get_dim_manager().get_offset();
    }

    template <unsigned DimsComp>
    __host__
    void compute_distribution_internal(const compute_mapping<DimsComp, Dims> &mapping)
    {
        std::array<int, Dims> arrayDimToCompDim;
        std::array<unsigned, DimsComp> gpuGrid;

        // Register the mapping
        arrayDimToCompDim = mapping.get_array_to_comp();

        // Count partitioned dimensions in array and computation
        if (mapping.get_array_part_dims() > mapping.comp.get_part_dims())
            FATAL("Not enough partitioned comp dims: %u, to partition %u array dims",
                  mapping.comp.get_part_dims(),
                  mapping.get_array_part_dims());

#if 0
        // Check the minumum number of arrayPartitionGrid
        if ((1 << compPartDims) > mapping.comp.procs)
            FATAL("Not enough GPUs (%u), to partition %u", mapping.comp.procs, arrayPartDims);
#endif
        std::array<array_size_t, Dims> dims = make_array(this->get_dim_manager().sizes_);

        // Distribute the partition uniformly across GPUs
        // 1- Compute GPU grid
        gpuGrid = helper_distribution_get_gpu_grid(mapping.comp);
        // 2- Compute array partitioning grid
        std::array<unsigned, Dims> arrayPartitionGrid = helper_distribution_get_array_grid(gpuGrid, arrayDimToCompDim);
        //hostInfo_->arrayPartitionGrid = arrayPartitionGrid;
        // 3- Compute dimensions of each tile
        std::array<array_size_t, Dims> localDims = helper_distribution_get_local_dims(dims, arrayPartitionGrid);
        hostInfo_->localDims = localDims;
        // 4- Compute the GPU grid offsets (iterate from lowest-order dimension)
        std::array<unsigned, DimsComp> gpuGridOffs = helper_distribution_gpu_get_offs(gpuGrid);
        // 5- Compute the array to GPU mapping needed for the allocation based on the grid offsets
        std::array<unsigned, Dims> arrayDimToGpus = helper_distribution_get_array_dim_to_gpus(gpuGridOffs, arrayDimToCompDim);
        hostInfo_->arrayDimToGpus = arrayDimToGpus;

        DEBUG("VM> BASE INFO");
        DEBUG("VM> - array dims: %s", to_string(dims).c_str());
        DEBUG("VM> - comp  dims: %u", DimsComp);
        DEBUG("VM> - comp -> array: %s", to_string(arrayDimToCompDim).c_str());

        DEBUG("VM> PARTITIONING");
        DEBUG("VM> - gpus: %u", mapping.comp.procs);
        DEBUG("VM> - comp  part: %s (%u)", to_string(mapping.comp.info).c_str(), mapping.comp.get_part_dims());
        DEBUG("VM> - comp  grid: %s", to_string(gpuGrid).c_str());
        DEBUG("VM> - array grid: %s", to_string(arrayPartitionGrid).c_str());
        DEBUG("VM> - local dims: %s", to_string(hostInfo_->localDims).c_str());

        DEBUG("VM> - array grid offsets: %s", to_string(arrayDimToGpus).c_str());
    }

    template <unsigned DimsComp>
    __host__ void
    compute_distribution(const compute_mapping<DimsComp, Dims> &mapping)
    {
        DEBUG("=========================");
        DEBUG("VM> DISTRIBUTE BEGIN");

        if (hostInfo_ != NULL) delete hostInfo_;
        hostInfo_ = new storage_host_info(mapping.comp.procs);

        compute_distribution_internal(mapping);

        DEBUG("VM> DISTRIBUTE END");
        DEBUG("=========================");
    }

    T *dataDev_;

    storage_host_info *hostInfo_;

    CUDARRAYS_TESTED(lib_storage_test, host_vm)
};

}

#endif