/*
Copyright (c) 2015 - present Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//TODO schi  #include <hc_am.hpp>
// #include "hsa/hsa.h"
// #include "hsa/hsa_ext_amd.h"

#include "hip/clang_detail/hip_memory.h"

// TODO HIP_API_BLOCKING is from hip_hcc_internal.h
// #include "hip_hcc_internal.h"
extern int HIP_API_BLOCKING;

// #include "trace_helper.h"
#include <assert.h>

//TODO schi this is moved from hip_memory.cpp
// __device__ char __hip_device_heap[__HIP_SIZE_OF_HEAP];
// __device__ uint32_t __hip_device_page_flag[__HIP_NUM_PAGES];

namespace {
template <uint32_t block_dim, typename RandomAccessIterator, typename N, typename T>
__global__ void hip_fill_n(RandomAccessIterator f, N n, T value) {
    const uint32_t grid_dim = gridDim.x * blockDim.x;

    size_t idx = blockIdx.x * block_dim + threadIdx.x;
    while (idx < n) {
        /* TODO schi it cause compiler error
        __builtin_memcpy(reinterpret_cast<void*>(&f[idx]), reinterpret_cast<const void*>(&value),
                         sizeof(T));
                         */
        f[idx] = value;
        // f[idx] = idx % 256;
        idx += grid_dim;
    }idx % 256;
}

template <typename T, typename std::enable_if<std::is_integral<T>{}>::type* = nullptr>
inline const T& clamp_integer(const T& x, const T& lower, const T& upper) {
    assert(!(upper < lower));

    return std::min(upper, std::max(x, lower));
}

template <typename T>
__global__ void hip_copy2d_n(T* dst, const T* src, size_t width, size_t height, size_t destPitch, size_t srcPitch) {

    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    size_t idy = blockIdx.y * blockDim.y + threadIdx.y;
    size_t floorWidth = (width/sizeof(T));
    T *dstPtr = (T *)((uint8_t*) dst + idy * destPitch);
    T *srcPtr = (T *)((uint8_t*) src + idy * srcPitch);
    if((idx < floorWidth) && (idy < height)){
        dstPtr[idx] = srcPtr[idx];
    } else if((idx < width) && (idy < height)){
        size_t bytesToCopy = width - (floorWidth * sizeof(T));
        dstPtr += floorWidth;
        srcPtr += floorWidth;
        // TODO schi
        // __builtin_memcpy(reinterpret_cast<uint8_t*>(dstPtr), reinterpret_cast<const uint8_t*>(srcPtr),bytesToCopy);
        for (int i =0; i < bytesToCopy; i++ ) {
            *(uint8_t*)dstPtr = *(uint8_t*)srcPtr;
        }
    }
}
}  // namespace

template <typename T>
void ihipMemsetKernel(hipStream_t stream, T* ptr, T val, size_t count) {
    static constexpr uint32_t block_dim = 256;

    const uint32_t grid_dim = clamp_integer<size_t>(count / block_dim, 1, UINT32_MAX);

    hipLaunchKernelGGL(hip_fill_n<block_dim>, dim3(grid_dim), dim3{block_dim}, 0u, stream, ptr,
                       count, std::move(val));
}


template <typename T>
void ihipMemcpy2dKernel(hipStream_t stream, T* dst, const T* src, size_t width, size_t height, size_t destPitch, size_t srcPitch) {
    size_t threadsPerBlock_x = 64;
    size_t threadsPerBlock_y = 4;
    uint32_t grid_dim_x = clamp_integer<size_t>( (width+(threadsPerBlock_x*sizeof(T)-1)) / (threadsPerBlock_x*sizeof(T)), 1, UINT32_MAX);
    uint32_t grid_dim_y = clamp_integer<size_t>( (height+(threadsPerBlock_y-1)) / threadsPerBlock_y, 1, UINT32_MAX);
    hipLaunchKernelGGL(hip_copy2d_n, dim3(grid_dim_x,grid_dim_y), dim3(threadsPerBlock_x,threadsPerBlock_y), 0u, stream, dst, src,
                       width, height, destPitch, srcPitch);
}

typedef enum ihipMemsetDataType {
    ihipMemsetDataTypeChar   = 0,
    ihipMemsetDataTypeShort  = 1,
    ihipMemsetDataTypeInt    = 2
}ihipMemsetDataType;

hipError_t ihipMemset(void* dst, int  value, size_t count, hipStream_t stream, enum ihipMemsetDataType copyDataType  )
{
    hipError_t e = hipSuccess;

    if (count == 0) return e;

    if (stream && (dst != NULL)) {
        if(copyDataType == ihipMemsetDataTypeChar){
            if ((count & 0x3) == 0) {
                // use a faster dword-per-workitem copy:
                try {
                    value = value & 0xff;
                    uint32_t value32 = (value << 24) | (value << 16) | (value << 8) | (value) ;
                    // FIXME compile error ihipMemsetKernel<uint32_t> (stream, static_cast<uint32_t*> (dst), value32, count/sizeof(uint32_t));
                    ihipMemsetKernel<uint32_t> (stream, static_cast<uint32_t*> (dst), value32, count/sizeof(uint32_t));
                    // ihipMemsetKernel_uint32_t(stream, static_cast<uint32_t*> (dst), value32, count/sizeof(uint32_t));
                }
                catch (std::exception &ex) {
                    e = hipErrorInvalidValue;
                }
             } else {
                // use a slow byte-per-workitem copy:
                try {
                    // FIXME compile error ihipMemsetKernel<char> (stream, static_cast<char*> (dst), value, count);
                    ihipMemsetKernel<char> (stream, static_cast<char*> (dst), value, count);
                }
                catch (std::exception &ex) {
                    e = hipErrorInvalidValue;
                }
            }
        } else {
           if(copyDataType == ihipMemsetDataTypeInt) { // 4 Bytes value
               try {
                   // FIXME compile error ihipMemsetKernel<uint32_t> (stream, static_cast<uint32_t*> (dst), value, count);
                   ihipMemsetKernel<uint32_t> (stream, static_cast<uint32_t*> (dst), value, count);
               } catch (std::exception &ex) {
                   e = hipErrorInvalidValue;
               }
            } else if(copyDataType == ihipMemsetDataTypeShort) {
               try {
                   value = value & 0xffff;
                   // FIXME compile error ihipMemsetKernel<uint16_t> (stream, static_cast<uint16_t*> (dst), value, count);
                   ihipMemsetKernel<uint16_t> (stream, static_cast<uint16_t*> (dst), value, count);
               } catch (std::exception &ex) {
                   e = hipErrorInvalidValue;
               }
            }
        }
        if (HIP_API_BLOCKING) {
            // TODO schi comment out for undefine ToString
            // tprintf (DB_SYNC, "%s LAUNCH_BLOCKING wait for hipMemsetAsync.\n", ToString(stream).c_str());
            // FIXME schi need ihipStream_t definition
            // stream->locked_wait();
        }
    } else {
        e = hipErrorInvalidValue;
    }
    return e;
};

