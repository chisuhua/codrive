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

//! HIP = Heterogeneous-compute Interface for Portability
//!
//! Define a extremely thin runtime layer that allows source code to be compiled unmodified
//! through either AMD HCC or NVCC.   Key features tend to be in the spirit
//! and terminology of CUDA, but with a portable path to other accelerators as well:
//
//! Both paths support rich C++ features including classes, templates, lambdas, etc.
//! Runtime API is C
//! Memory management is based on pure pointers and resembles malloc/free/copy.
//
//! hip_runtime.h     : includes everything in hip_api.h, plus math builtins and kernel launch
//! macros. hip_runtime_api.h : Defines HIP API.  This is a C header file and does not use any C++
//! features.

#ifndef HIP_INCLUDE_HIP_HIP_RUNTIME_H
#define HIP_INCLUDE_HIP_HIP_RUNTIME_H

// Some standard header files, these are included by hc.hpp and so want to make them avail on both
// paths to provide a consistent include env and avoid "missing symbol" errors that only appears
// on NVCC path:
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if __cplusplus > 199711L
#include <thread>
#endif

// #define __hcc_workweek__ 18800
// #define __HIP__

#define __host__

typedef struct dim3 {
    uint32_t x;  ///< x
    uint32_t y;  ///< y
    uint32_t z;  ///< z
#ifdef __cplusplus
    dim3(uint32_t _x = 1, uint32_t _y = 1, uint32_t _z = 1) : x(_x), y(_y), z(_z){};
#endif
} dim3;

#define __device__
#define __CUDA__
// #include "cuda_open/cuda_open.h"
#include "cuda_open/vector_types.h"

// #include <hip/clang_detail/hip_runtime.h>
// copy some from hip/clang_detail/hip_runtime.h
extern int HIP_TRACE_API;


// #include <hip/hip_common.h>
// 
// #include <hip/clang_detail/hip_runtime.h>

// #if defined(__HIP_PLATFORM_HCC__) && !defined(__HIP_PLATFORM_NVCC__)
// #include <hip/hcc_detail/hip_runtime.h>
// #elif defined(__HIP_PLATFORM_NVCC__) && !defined(__HIP_PLATFORM_HCC__)
// #include <hip/nvcc_detail/hip_runtime.h>
// #else
// #error("Must define exactly one of __HIP_PLATFORM_HCC__ or __HIP_PLATFORM_NVCC__");
// #endif

// Implementation of malloc and free device functions.
// HIP heap is implemented as a global array with fixed size. Users may define
// __HIP_SIZE_OF_PAGE and __HIP_NUM_PAGES to have a larger heap.


// Size of page in bytes.
#ifndef __HIP_SIZE_OF_PAGE
#define __HIP_SIZE_OF_PAGE 64
#endif

// Total number of pages
#ifndef __HIP_NUM_PAGES
#define __HIP_NUM_PAGES (16 * 64 * 64)
#endif

#define __HIP_SIZE_OF_HEAP (__HIP_NUM_PAGES * __HIP_SIZE_OF_PAGE)

/*
typedef enum hipMemoryType {
    hipMemoryTypeHost,    ///< Memory is physically located on host
    hipMemoryTypeDevice,  ///< Memory is physically located on device. (see deviceId for specific
                          ///< device)
    hipMemoryTypeArray,  ///< Array memory, physically located on device. (see deviceId for specific
                         ///< device)
    hipMemoryTypeUnified  ///< Not used currently
}hipMemoryType;
*/

// this file is from cuda_open include directory
#include "hip/hip_host_runtime_api.h"
// copy from hcc_detail/hip_runtime_api.h
// hipError_t hipFuncGetAttributes(hipFuncAttributes* attr, const void* func);

// #include <hip/hip_runtime_api.h>
// #include <hip/clang_detail/hip_vector_types.h>
// #define __device__
// #define __CUDA__
// #include "cuda_open/cuda_open.h"
// #include "cuda_open/vector_types.h"

#endif
