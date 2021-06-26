#pragma once

#include "inc/pps.h"
#include <algorithm>
#include <future>


//-------------------------------------------------------------------------------------------------
// An optimized "staging buffer" used to implement Host-To-Device and Device-To-Host copies.
// Some GPUs may not be able to directly access host memory, and in these cases we need to
// stage the copy through a pinned staging buffer.  For example, the CopyHostToDevice
// uses the CPU to copy to a pinned "staging buffer", and then use the GPU DMA engine to copy
// from the staging buffer to the final destination.  The copy is broken into buffer-sized chunks
// to limit the size of the buffer and also to provide better performance by overlapping the CPU copies
// with the DMA copies.
//
// PinInPlace is another algorithm which pins the host memory "in-place", and copies it with the DMA
// engine.  This routine is under development.
//
// Staging buffer provides thread-safe access via a mutex.
struct UnpinnedCopyEngine {

    enum CopyMode {ChooseBest=0, UsePinInPlace=1, UseStaging=2, UseMemcpy=3} ; 

    static const int _max_buffers = 4;

    UnpinnedCopyEngine(device_t hsaAgent,hsa_agent_t cpuAgent, size_t bufferSize, int numBuffers, 
                       bool isLargeBar, int thresholdH2D_directStaging, int thresholdH2D_stagingPinInPlace, int thresholdD2H) ;
    ~UnpinnedCopyEngine();

    // Use hueristic to choose best copy algorithm 
    void CopyHostToDevice(CopyMode copyMode, void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);
    void CopyDeviceToHost(CopyMode copyMode, void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);


    // Specific H2D copy algorithm implementations:
    void CopyHostToDeviceStaging(void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);
    void CopyHostToDevicePinInPlace(void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);
    void CopyHostToDeviceMemcpy(void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);


    // Specific D2H copy algorithm implementations:
    void CopyDeviceToHostStaging(void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);
    void CopyDeviceToHostPinInPlace(void* dst, const void* src, size_t sizeBytes, const signal_t *waitFor);


    // P2P Copy implementation:
    void CopyPeerToPeer(void* dst, device_t dstAgent, const void* src, hsa_agent_t srcAgent, size_t sizeBytes, const signal_t *waitFor);

private:
    bool IsLockedPointer(const void *ptr);

private:
    device_t     _hsaAgent;
    device_t     _cpuAgent;
    size_t          _bufferSize;  // Size of the buffers.
    int             _numBuffers;

    // True if system supports large-bar and thus can benefit from CPU directly performing copy operation.
    bool            _isLargeBar;

    char            *_pinnedStagingBuffer[_max_buffers];
    signal_t     _completionSignal[_max_buffers];
    signal_t     _completionSignal2[_max_buffers]; // P2P needs another set of signals.
    std::mutex       _copyLock;    // provide thread-safe access
    size_t              _hipH2DTransferThresholdDirectOrStaging;
    size_t              _hipH2DTransferThresholdStagingOrPininplace;
    size_t              _hipD2HTransferThreshold;
};

