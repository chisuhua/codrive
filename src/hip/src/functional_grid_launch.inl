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

#include "hip/clang_detail/program_state.hpp"

// TODO schi change from hip_runtime_api to hip_runtime
// #include "hip/hip_runtime_api.h"
#include "hip/hip_runtime.h"

// Internal header, do not percolate upwards.
#include "hip_hcc_internal.h"

// TODO schi #include "hc.hpp"
#include "inc/csq_accelerator.h"

#include "trace_helper.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <iostream>

using namespace hc;
using namespace std;

namespace hip_impl
{
    hsa_agent_t target_agent(hipStream_t stream)
    {
        if (stream) {
            return *static_cast<hsa_agent_t*>(
                stream->locked_getAv()->get_hsa_agent());
        }
        else if (
            ihipGetTlsDefaultCtx() && ihipGetTlsDefaultCtx()->getDevice()) {
            return ihipGetDevice(
                ihipGetTlsDefaultCtx()->getDevice()->_deviceId)->_hsaAgent;
        }
        else {
            csq::accelerator acc; 
            return *static_cast<hsa_agent_t*>(
                // TODO schi accelerator{}.get_default_view().get_hsa_agent());
                acc.get_default_view().get_hsa_agent());
        }
    }
}
