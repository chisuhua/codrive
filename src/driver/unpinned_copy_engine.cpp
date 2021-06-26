
#include <cassert>
#include <atomic>
#include <string.h>
#include <vector>
// #include <hc.hpp>
// #include <hc_am.hpp>

// #include <hsa/hsa_ext_amd.h>

#include "utils/lang/debug.h"
#include "inc/pps_ext.h"
#include "inc/unpinned_copy_engine.h"
// #include "hc_rt_debug.h"

using std::vector;

void errorCheck(status_t hsa_error_code, int line_num, std::string str)
{
    if ((hsa_error_code != SUCCESS) && (hsa_error_code != HSA_STATUS_INFO_BREAK))
    {
        printf("HSA reported error!\n In file: %s\nAt line: %d\n", str.c_str(), line_num);
    }
}

// #define ErrorCheck(x) errorCheck(x, __LINE__, __FILENAME__)
#define ErrorCheck(x) errorCheck(x, __LINE__, __FILE__)

static status_t findGlobalPool(hsa_amd_memory_pool_t pool, void *data)
{
    if (NULL == data)
    {
        return ERROR_INVALID_ARGUMENT;
    }

    // status_t err;
    hsa_amd_segment_t segment;
    uint32_t flag;
    ErrorCheck(hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment));

    ErrorCheck(hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flag));
    if ((HSA_AMD_SEGMENT_GLOBAL == segment) &&
        (flag & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED))
    {
        *((hsa_amd_memory_pool_t *)data) = pool;
    }
    return SUCCESS;
}

static status_t find_gpu(device_t agent, void *data)
{
    // status_t status;
    hsa_device_type_t device_type;
    std::vector<device_t> *pAgents = nullptr;

    if (data == nullptr)
    {
        return ERROR_INVALID_ARGUMENT;
    }
    else
    {
        pAgents = static_cast<std::vector<device_t> *>(data);
    }

    status_t stat = hsa_device_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
    if (stat != SUCCESS)
    {
        return stat;
    }

    if (device_type == HSA_DEVICE_TYPE_GPU)
    {
        pAgents->push_back(agent);
    }

    return SUCCESS;
}

//-------------------------------------------------------------------------------------------------
UnpinnedCopyEngine::UnpinnedCopyEngine(device_t hsaAgent, hsa_agent_t cpuAgent, size_t bufferSize, int numBuffers,
                                       bool isLargeBar, int thresholdH2DDirectStaging,
                                       int thresholdH2DStagingPinInPlace, int thresholdD2H) : _hsaAgent(hsaAgent),
                                                                                              _cpuAgent(cpuAgent),
                                                                                              _bufferSize(bufferSize),
                                                                                              _numBuffers(numBuffers > _max_buffers ? _max_buffers : numBuffers),
                                                                                              _isLargeBar(isLargeBar),
                                                                                              _hipH2DTransferThresholdDirectOrStaging(thresholdH2DDirectStaging),
                                                                                              _hipH2DTransferThresholdStagingOrPininplace(thresholdH2DStagingPinInPlace),
                                                                                              _hipD2HTransferThreshold(thresholdD2H)
{
    hsa_amd_memory_pool_t sys_pool;
    status_t err = hsa_amd_agent_iterate_memory_pools(_cpuAgent, findGlobalPool, &sys_pool);

    // Generate a packed C-style array of agents, for use below with hsa_amd_agents_allow_access
    // TODO - should this include the CPU agents as well?
    std::vector<device_t> agents;
    err = hsa_iterate_agents(&find_gpu, &agents);
    ErrorCheck(err);
    device_t *agentBlock = new hsa_agent_t[agents.size()];
    unsigned int i = 0;
    for (auto iter = agents.begin(); iter != agents.end(); iter++)
    {
        agentBlock[i++] = *iter;
        assert(i <= agents.size());
    };

    ErrorCheck(err);
    for (int i = 0; i < _numBuffers; i++)
    {
        // TODO - experiment with alignment here.
        err = hsa_amd_memory_pool_allocate(sys_pool, _bufferSize, 0, (void **)(&_pinnedStagingBuffer[i]));
        ErrorCheck(err);

        if ((err != SUCCESS) || (_pinnedStagingBuffer[i] == NULL))
        {
            // THROW_ERROR(hipErrorMemoryAllocation, err);
        }

        // Allow access from every agent:
        // This is used in peer-to-peer copies, since we use the buffers to copy from different agents.
        // TODO - may want to review this algorithm for NUMA locality - it might be faster to use staging buffer closer to devices?
        err = hsa_amd_agents_allow_access(agents.size(), agentBlock, NULL, _pinnedStagingBuffer[i]);
        ErrorCheck(err);

        hsa_signal_create(0, 0, NULL, &_completionSignal[i]);
        hsa_signal_create(0, 0, NULL, &_completionSignal2[i]);
    }
};

//---
UnpinnedCopyEngine::~UnpinnedCopyEngine()
{
    for (int i = 0; i < _numBuffers; i++)
    {
        if (_pinnedStagingBuffer[i])
        {
            hsa_amd_memory_pool_free(_pinnedStagingBuffer[i]);
            _pinnedStagingBuffer[i] = NULL;
        }
        hsa_signal_destroy(_completionSignal[i]);
        hsa_signal_destroy(_completionSignal2[i]);
    }
}

//---
//Copies sizeBytes from src to dst, using either a copy to a staging buffer or a staged pin-in-place strategy
//IN: dst - dest pointer - must be accessible from host CPU.
//IN: src - src pointer for copy.  Must be accessible from agent this buffer is associated with (via _hsaAgent)
//IN: waitFor - hsaSignal to wait for - the copy will begin only when the specified dependency is resolved.  May be NULL indicating no dependency.
void UnpinnedCopyEngine::CopyHostToDevicePinInPlace(void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    // Make sure we wait for the dependent signal to complete before entering the critical section
    // to avoid potential dead lock
    {
        std::lock_guard<std::mutex> l(_copyLock);
        // DBOUTL(DB_COPY2, __func__ << DBPARM(dst) << "," << DBPARM(src) << "," << DBPARM(sizeBytes))

        const char *srcp = static_cast<const char *>(src);
        char *dstp = static_cast<char *>(dst);

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_store_screlease(_completionSignal[i], 0);
        }

        if (sizeBytes >= UINT64_MAX / 2)
        {
            // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
        }
        int bufferIndex = 0;

        size_t theseBytes = sizeBytes;
        //tprintf (DB_COPY2, "H2D: waiting... on completion signal handle=%lu\n", _completionSignal[bufferIndex].handle);
        //hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);

        //void * masked_srcp = (void*) ((uintptr_t)srcp & (uintptr_t)(~0x3f)) ; // TODO
        void *locked_srcp;
        //status_t hsa_status = hsa_amd_memory_lock(masked_srcp, theseBytes, &_hsaAgent, 1, &locked_srcp);
        status_t hsa_status = hsa_amd_memory_lock(const_cast<char *>(srcp), theseBytes, &_hsaAgent, 1, &locked_srcp);
        //tprintf (DB_COPY2, "H2D: bytesRemaining=%zu: pin-in-place:%p+%zu bufferIndex[%d]\n", bytesRemaining, srcp, theseBytes, bufferIndex);
        //printf ("status=%x srcp=%p, masked_srcp=%p, locked_srcp=%p\n", hsa_status, srcp, masked_srcp, locked_srcp);

        if (hsa_status != SUCCESS)
        {
            // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
        }

        hsa_signal_store_screlease(_completionSignal[bufferIndex], 1);

        hsa_status = hsa_amd_memory_async_copy(dstp, _hsaAgent, locked_srcp, _hsaAgent, theseBytes,
                                               0, nullptr, _completionSignal[bufferIndex]);
        //tprintf (DB_COPY2, "H2D: bytesRemaining=%zu: async_copy %zu bytes %p to %p status=%x\n", bytesRemaining, theseBytes, _pinnedStagingBuffer[bufferIndex], dstp, hsa_status);

        if (hsa_status != SUCCESS)
        {
            // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
        }
        // DBOUTL(DB_COPY2, "H2D: waiting... on completion signal handle=" << _completionSignal[bufferIndex].handle);
        hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
        hsa_amd_memory_unlock(const_cast<char *>(srcp));
    }
}

// Copy using simple memcpy.  Only works on large-bar systems.
void UnpinnedCopyEngine::CopyHostToDeviceMemcpy(void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    // DBOUTL (DB_COPY2, __func__ << DBPARM(dst) << "," << DBPARM(src) << "," << DBPARM(sizeBytes))
    if (!_isLargeBar)
    {
        // THROW_ERROR (hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
    }

    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
    memcpy(dst, src, sizeBytes);
    std::atomic_thread_fence(std::memory_order_release);
};

void UnpinnedCopyEngine::CopyHostToDevice(UnpinnedCopyEngine::CopyMode copyMode, void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    bool isLocked = false;
    if ((copyMode == ChooseBest) || (copyMode == UsePinInPlace))
    {
        isLocked = IsLockedPointer(src);
    }
    if (copyMode == ChooseBest)
    {
        if (_isLargeBar && (sizeBytes < _hipH2DTransferThresholdDirectOrStaging))
        {
            copyMode = UseMemcpy;
        }
        else if ((sizeBytes > _hipH2DTransferThresholdStagingOrPininplace) && (!isLocked))
        {
            copyMode = UsePinInPlace;
        }
        else
        {
            copyMode = UseStaging;
        }
    }

    if (copyMode == UseMemcpy)
    {
        CopyHostToDeviceMemcpy(dst, src, sizeBytes, waitFor);
    }
    else if ((copyMode == UsePinInPlace) && (!isLocked))
    {
        CopyHostToDevicePinInPlace(dst, src, sizeBytes, waitFor);
    }
    else if (copyMode == UseStaging)
    {
        CopyHostToDeviceStaging(dst, src, sizeBytes, waitFor);
    }
    else
    {
        // Unknown copy mode.
        // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
    }
}

//---
//Copies sizeBytes from src to dst, using either a copy to a staging buffer or a staged pin-in-place strategy
//IN: dst - dest pointer - must be accessible from host CPU.
//IN: src - src pointer for copy.  Must be accessible from agent this buffer is associated with (via _hsaAgent)
//IN: waitFor - hsaSignal to wait for - the copy will begin only when the specified dependency is resolved.  May be NULL indicating no dependency.
void UnpinnedCopyEngine::CopyHostToDeviceStaging(void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    // Make sure we wait for the dependent signal to complete before entering the critical section
    // to avoid potential dead lock
    {
        std::lock_guard<std::mutex> l(_copyLock);
        // DBOUTL (DB_COPY2, __func__ << DBPARM(dst) << "," << DBPARM(src) << "," << DBPARM(sizeBytes))

        const char *srcp = static_cast<const char *>(src);
        char *dstp = static_cast<char *>(dst);

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_store_screlease(_completionSignal[i], 0);
        }

        if (sizeBytes >= UINT64_MAX / 2)
        {
            // THROW_ERROR (hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
        }

        int bufferIndex = 0;
        for (int64_t bytesRemaining = sizeBytes; bytesRemaining > 0; bytesRemaining -= _bufferSize)
        {

            size_t theseBytes = ((uint64_t)bytesRemaining > _bufferSize) ? _bufferSize : bytesRemaining;

            // DBOUTL (DB_COPY2,  "H2D: waiting... on completion signal handle=" << _completionSignal[bufferIndex].handle);
            hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);

            // DBOUTL (DB_COPY2, "H2D: bytesRemaining=" << bytesRemaining << ": copy " << theseBytes << " bytes "
            //        << static_cast<const void*>(srcp) << " to stagingBuf[" << bufferIndex << "]:" << static_cast<void*>(_pinnedStagingBuffer[bufferIndex]));
            // TODO - use uncached memcpy, someday.
            memcpy(_pinnedStagingBuffer[bufferIndex], srcp, theseBytes);

            hsa_signal_store_screlease(_completionSignal[bufferIndex], 1);
            status_t hsa_status = hsa_amd_memory_async_copy(dstp, _hsaAgent, _pinnedStagingBuffer[bufferIndex], _hsaAgent, theseBytes, 0, nullptr, _completionSignal[bufferIndex]);
            // DBOUTL (DB_COPY2, "H2D: bytesRemaining=" << bytesRemaining << ": async_copy " << theseBytes << " bytes "
            //        << static_cast<void*>(_pinnedStagingBuffer[bufferIndex]) << " to " << static_cast<void*>(dstp) << " status=" << hsa_status);
            if (hsa_status != SUCCESS)
            {
                // THROW_ERROR (hipErrorRuntimeMemory, hsa_status);
            }

            srcp += theseBytes;
            dstp += theseBytes;
            if (++bufferIndex >= _numBuffers)
            {
                bufferIndex = 0;
            }
        }

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_wait_scacquire(_completionSignal[i], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
        }
    }
}

void UnpinnedCopyEngine::CopyDeviceToHostPinInPlace(void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    // Make sure we wait for the dependent signal to complete before entering the critical section
    // to avoid potential dead lock
    {
        std::lock_guard<std::mutex> l(_copyLock);

        const char *srcp = static_cast<const char *>(src);
        char *dstp = static_cast<char *>(dst);

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_store_screlease(_completionSignal[i], 0);
        }

        if (sizeBytes >= UINT64_MAX / 2)
        {
            // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
        }
        int bufferIndex = 0;
        size_t theseBytes = sizeBytes;
        void *locked_destp;

        status_t hsa_status = hsa_amd_memory_lock(const_cast<char *>(dstp), theseBytes, &_hsaAgent, 1, &locked_destp);

        if (hsa_status != SUCCESS)
        {
            // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
        }

        hsa_signal_store_screlease(_completionSignal[bufferIndex], 1);

        hsa_status = hsa_amd_memory_async_copy(locked_destp, _hsaAgent, srcp, _hsaAgent, theseBytes, 
                                               0, nullptr, _completionSignal[bufferIndex]);

        if (hsa_status != SUCCESS)
        {
            // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
        }
        // DBOUTL(DB_COPY2, "D2H: waiting... on completion signal handle=\n"
        //                     << _completionSignal[bufferIndex].handle);
        hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
        hsa_amd_memory_unlock(const_cast<char *>(dstp));
    }
}

void UnpinnedCopyEngine::CopyDeviceToHost(CopyMode copyMode, void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    bool isLocked = false;
    if ((copyMode == ChooseBest) || (copyMode == UsePinInPlace))
    {
        isLocked = IsLockedPointer(dst);
    }

    if (copyMode == ChooseBest)
    {
        if (sizeBytes > _hipD2HTransferThreshold && !isLocked)
        {
            copyMode = UsePinInPlace;
        }
        else
        {
            copyMode = UseStaging;
        }
    }

    if (copyMode == UsePinInPlace && !isLocked)
    {
        CopyDeviceToHostPinInPlace(dst, src, sizeBytes, waitFor);
    }
    else if (copyMode == UseStaging)
    {
        CopyDeviceToHostStaging(dst, src, sizeBytes, waitFor);
    }
    else
    {
        // Unknown copy mode.
        // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
    }
}

//---
//Copies sizeBytes from src to dst, using either a copy to a staging buffer or a staged pin-in-place strategy
//IN: dst - dest pointer - must be accessible from agent this buffer is associated with (via _hsaAgent).
//IN: src - src pointer for copy.  Must be accessible from host CPU.
//IN: waitFor - hsaSignal to wait for - the copy will begin only when the specified dependency is resolved.  May be NULL indicating no dependency.
void UnpinnedCopyEngine::CopyDeviceToHostStaging(void *dst, const void *src, size_t sizeBytes, const signal_t *waitFor)
{
    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    // Make sure we wait for the dependent signal to complete before entering the critical section
    // to avoid potential dead lock
    {

        std::lock_guard<std::mutex> l(_copyLock);

        const char *srcp0 = static_cast<const char *>(src);
        char *dstp1 = static_cast<char *>(dst);

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_store_screlease(_completionSignal[i], 0);
        }

        if (sizeBytes >= UINT64_MAX / 2)
        {
            // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
        }

        int64_t bytesRemaining0 = sizeBytes; // bytes to copy from dest into staging buffer.
        int64_t bytesRemaining1 = sizeBytes; // bytes to copy from staging buffer into final dest

        while (bytesRemaining1 > 0)
        {
            // First launch the async copies to copy from dest to host
            for (int bufferIndex = 0; (bytesRemaining0 > 0) && (bufferIndex < _numBuffers); bytesRemaining0 -= _bufferSize, bufferIndex++)
            {

                size_t theseBytes = ((uint64_t)bytesRemaining0 > _bufferSize) ? _bufferSize : (uint64_t)bytesRemaining0;

                // DBOUTL(DB_COPY2, "D2H: bytesRemaining0=" << bytesRemaining0 << ": copy " << theseBytes << " bytes "
                //                                         << static_cast<const void *>(srcp0) << " to stagingBuf[" << bufferIndex << "]:" << static_cast<void *>(_pinnedStagingBuffer[bufferIndex]));
                hsa_signal_store_screlease(_completionSignal[bufferIndex], 1);
                status_t hsa_status = hsa_amd_memory_async_copy(_pinnedStagingBuffer[bufferIndex], _hsaAgent, srcp0, _hsaAgent, theseBytes, 
                                                                    0, nullptr, _completionSignal[bufferIndex]);
                if (hsa_status != SUCCESS)
                {
                    // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
                }

                srcp0 += theseBytes;
            }

            // Now unload the staging buffers:
            for (int bufferIndex = 0; (bytesRemaining1 > 0) && (bufferIndex < _numBuffers); bytesRemaining1 -= _bufferSize, bufferIndex++)
            {

                size_t theseBytes = ((uint64_t)bytesRemaining1 > _bufferSize) ? _bufferSize : (uint64_t)bytesRemaining1;

                // DBOUTL(DB_COPY2, "D2H: wait_completion[" << bufferIndex << "] bytesRemaining=" << bytesRemaining1);
                hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);

                // DBOUTL(DB_COPY2, "D2H: bytesRemaining1=" << bytesRemaining1 << ": copy " << theseBytes << " bytes "
                //                                         << " stagingBuf[" << bufferIndex << "]:" << static_cast<void *>(_pinnedStagingBuffer[bufferIndex]) << " to dst " << static_cast<void *>(dstp1));
                memcpy(dstp1, _pinnedStagingBuffer[bufferIndex], theseBytes);

                dstp1 += theseBytes;
            }
        }
    }
}

//---
//Copies sizeBytes from src to dst, using either a copy to a staging buffer or a staged pin-in-place strategy
//IN: dst - dest pointer - must be accessible from agent this buffer is associated with (via _hsaAgent).
//IN: src - src pointer for copy.  Must be accessible from host CPU.
//IN: waitFor - hsaSignal to wait for - the copy will begin only when the specified dependency is resolved.  May be NULL indicating no dependency.
void UnpinnedCopyEngine::CopyPeerToPeer(void *dst, device_t dstAgent, const void *src, hsa_agent_t srcAgent, size_t sizeBytes, const signal_t *waitFor)
{
    if (waitFor)
        hsa_signal_wait_scacquire(*waitFor, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);

    // Make sure we wait for the dependent signal to complete before entering the critical section
    // to avoid potential dead lock
    {

        std::lock_guard<std::mutex> l(_copyLock);

        const char *srcp0 = static_cast<const char *>(src);
        char *dstp1 = static_cast<char *>(dst);

        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_store_screlease(_completionSignal[i], 0);
            hsa_signal_store_screlease(_completionSignal2[i], 0);
        }

        if (sizeBytes >= UINT64_MAX / 2)
        {
            // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
        }

        uint64_t bytesRemaining0 = sizeBytes; // bytes to copy from dest into staging buffer.
        uint64_t bytesRemaining1 = sizeBytes; // bytes to copy from staging buffer into final dest

        // TODO - can we run this all on the GPU, without host sync?

        while (bytesRemaining1 > 0)
        {
            // First launch the async copies to copy from dest to host
            for (int bufferIndex = 0; (bytesRemaining0 > 0) && (bufferIndex < _numBuffers); bytesRemaining0 -= _bufferSize, bufferIndex++)
            {

                size_t theseBytes = (bytesRemaining0 > _bufferSize) ? _bufferSize : bytesRemaining0;

                // Wait to make sure we are not overwriting a buffer before it has been drained:
                hsa_signal_wait_scacquire(_completionSignal2[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);

                // DBOUTL(DB_COPY2, "P2P: bytesRemaining0=" << bytesRemaining0 << ": async_copy " << theseBytes << " bytes "
                //                                         << static_cast<const void *>(srcp0) << " to stagingBuf[" << bufferIndex << "]:" << static_cast<void *>(_pinnedStagingBuffer[bufferIndex]));
                hsa_signal_store_screlease(_completionSignal[bufferIndex], 1);
                // Select CPU-agent here to ensure Runtime picks the H2D blit kernel.  Makes a 5X-10X difference in performance.
                status_t hsa_status = hsa_amd_memory_async_copy(_pinnedStagingBuffer[bufferIndex], _cpuAgent, srcp0, srcAgent, theseBytes, 
                                                                    0, nullptr, _completionSignal[bufferIndex]);
                if (hsa_status != SUCCESS)
                {
                    // THROW_ERROR(hipErrorRuntimeMemory, hsa_status);
                }

                srcp0 += theseBytes;
            }

            // Now unload the staging buffers:
            for (int bufferIndex = 0; (bytesRemaining1 > 0) && (bufferIndex < _numBuffers); bytesRemaining1 -= _bufferSize, bufferIndex++)
            {

                size_t theseBytes = (bytesRemaining1 > _bufferSize) ? _bufferSize : bytesRemaining1;

                // DBOUTL(DB_COPY2, "P2P: wait_completion[" << bufferIndex << "] bytesRemaining=" << bytesRemaining1);

                bool hostWait = 0; // TODO - remove me

                if (hostWait)
                {
                    // Host-side wait, should not be necessary:
                    hsa_signal_wait_scacquire(_completionSignal[bufferIndex], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
                }

                // DBOUTL(DB_COPY2, "P2P: bytesRemaining1=" << bytesRemaining1 << ": copy " << theseBytes << " bytes "
                //                                         << " stagingBuf[" << bufferIndex << "]:" << static_cast<void *>(_pinnedStagingBuffer[bufferIndex]) << " to dst " << static_cast<void *>(dstp1));
                hsa_signal_store_screlease(_completionSignal2[bufferIndex], 1);
                // Select CPU-agent here to ensure Runtime picks the H2D blit kernel.  Makes a 5X-10X difference in performance.
                // status_t hsa_status = hsa_amd_memory_async_copy(dstp1, dstAgent, _pinnedStagingBuffer[bufferIndex], _cpuAgent, theseBytes,
                hsa_amd_memory_async_copy(dstp1, dstAgent, _pinnedStagingBuffer[bufferIndex], _cpuAgent, theseBytes, 
                                          hostWait ? 0 : 1, hostWait ? NULL : &_completionSignal[bufferIndex],
                                          _completionSignal2[bufferIndex]);

                dstp1 += theseBytes;
            }
        }

        // Wait for the staging-buffer to dest copies to complete:
        for (int i = 0; i < _numBuffers; i++)
        {
            hsa_signal_wait_scacquire(_completionSignal2[i], HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
        }
    }
}

bool UnpinnedCopyEngine::IsLockedPointer(const void *ptr)
{
    hsa_amd_pointer_info_t info;
    bool isLocked = false;

    info.size = sizeof(info);
    status_t hsa_status = hsa_amd_pointer_info(const_cast<void *>(ptr), &info, nullptr, nullptr, nullptr);
    if (hsa_status != SUCCESS)
    {
        // THROW_ERROR(hipErrorInvalidValue, ERROR_INVALID_ARGUMENT);
    }

    if ((info.type == HSA_EXT_POINTER_TYPE_HSA) || (info.type == HSA_EXT_POINTER_TYPE_LOCKED))
    {
        isLocked = true;
    }
    // DBOUTL (DB_COPY2, "Unpinned Copy: pointer type =" << info.type << " isLocked=" << isLocked);

    return isLocked;
}
