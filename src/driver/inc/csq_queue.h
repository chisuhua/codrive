#pragma once
#include "inc/csq.h"
// #include "inc/csq_cmd.h"
// #include "inc/csq_device.h"
#include <map>
#include "inc/csq_accelerator.h"

// Chicken bits:
/*
int HCC_SERIALIZE_KERNEL = 0;
int HCC_SERIALIZE_COPY = 0;
int HCC_FORCE_COMPLETION_FUTURE = 0;
int HCC_FORCE_CROSS_QUEUE_FLUSH=0;
*/


namespace csq {
class Cmd;
class DlaQueue;
class DlaDevice;
class completion_future;

//-----


// Small wrapper around the hsa hardware queue (ie returned from hsa_queue_create(...).
// This allows us to see which accelerator_view owns the hsa queue, and
// also tracks the state of the cu mask, profiling, priority of the HW queue.
// Rocr queues are shared by the allocated DlaQueues.  When an DlaQueue steals
// a rocrQueue, we ensure that the hw queue has the desired cu_mask and other state.
//
// DlaQueue is the implementation of accelerator_view for HSA back-and.  DlaQueue
// points to RocrQueue, or to nullptr if the DlaQueue is not currently attached to a RocrQueue.
struct RocrQueue {
    static void callbackQueue(status_t status, queue_t* queue, void *data) {
        // STATUS_CHECK(status, __LINE__);
    }

    RocrQueue(device_t agent, size_t queue_size, DlaQueue *hccQueue, queue_priority priority)
        : _priority(priority)
    {
        // Map queue_priority to hsa_amd_queue_priority_t
        /* TODO schi don't need priority now 
        hsa_amd_queue_priority_t queue_priority;
        switch (priority) {
            case priority_low:
                queue_priority = HSA_AMD_QUEUE_PRIORITY_LOW;
                break;
            case priority_high:
                queue_priority = HSA_AMD_QUEUE_PRIORITY_HIGH;
                break;
            case priority_normal:
            default:
                queue_priority = HSA_AMD_QUEUE_PRIORITY_NORMAL;
                break;
        }
        */

        assert(queue_size != 0);

        /// Create a queue using the maximum size.
        // status_t status = hsa_queue_create(agent, queue_size, HSA_QUEUE_TYPE_SINGLE, callbackQueue, NULL, UINT32_MAX, UINT32_MAX, &_hwQueue);
        hsa_queue_create(agent, queue_size, HSA_QUEUE_TYPE_SINGLE, callbackQueue, NULL, UINT32_MAX, UINT32_MAX, &_hwQueue);
        // DBOUT(DB_QUEUE, "  " <<  __func__ << ": created an HSA command queue: " << _hwQueue << "\n");

        // STATUS_CHECK(status, __LINE__);
 
        // Set queue priority
        // TODO schi we don't need priority for now 
        // status = hsa_amd_queue_set_priority(_hwQueue, queue_priority);
        // DBOUT(DB_QUEUE, "  " <<  __func__ << ": set priority for HSA command queue: " << _hwQueue << " to " << queue_priority << "\n");

        // TODO - should we provide a mechanism to conditionally enable profiling as a performance optimization?
        // status = hsa_amd_profiling_set_profiler_enabled(_hwQueue, 1);

        // Create the links between the queues:
        assignHccQueue(hccQueue);
    }

    ~RocrQueue() {

        // DBOUT(DB_QUEUE, "  " <<  __func__ << ": destroy an HSA command queue: " << _hwQueue << "\n");

        // status_t status = hsa_queue_destroy(_hwQueue);
        hsa_queue_destroy(_hwQueue);
        _hwQueue = 0;
        // STATUS_CHECK(status, __LINE__);
    };

    void assignHccQueue(DlaQueue *hccQueue);

    status_t setCuMask(DlaQueue *hccQueue);


    queue_t *_hwQueue; // Pointer to the HSA queue this entry tracks.

    DlaQueue *_hccQueue;  // Pointe to the HCC "HSA" queue which is assigned to use the rocrQueue

    // std::vector<uint32_t> cu_arrays;

    // Track profiling enabled state here. - no need now since all hw queues have profiling enabled.

    // Priority could be tracked here:
    queue_priority _priority;
};



class DlaQueue final : public Queue
{
private:
    friend class DlaDevice;
    friend class RocrQueue;
    friend std::ostream& operator<<(std::ostream& os, const DlaQueue & hav);

    // ROCR queue associated with this DlaQueue instance.
    RocrQueue    *rocrQueue;

    //
    // kernel dispatches and barriers associated with this DlaQueue instance
    //
    // When a kernel k is dispatched, we'll get a AsyncOps f. This vector would hold f.
    // acccelerator_view::wait() would trigger DlaQueue::wait(), and all future objects
    // in the AsyncOp objects will be waited on.
    //
    std::vector< std::shared_ptr<Cmd> > asyncOps;

    bool         drainingQueue_;  // mode that we are draining queue, used to allow barrier ops to be enqueued.


    bool                                            valid;

    // Flag that is set when a kernel command is enqueued without system scope
    // Indicates queue needs a flush at the next queue::wait() call or copy to ensure
    // host data is valid.
    bool                                            _nextSyncNeedsSysRelease;

    // Flag that is set after a copy command is enqueued.
    // The next kernel command issued needs to add a system-scope acquire to
    // pick up any data that may have been written by the copy.
    bool                                            _nextKernelNeedsSysAcquire;


    // Kind of the youngest command in the queue.
    // Used to detect and enforce dependencies between commands.
    // Persists even after the youngest command has been removed.
    hcCommandKind youngestCommandKind;

    //
    // kernelBufferMap and bufferKernelMap forms the dependency graph of
    // kernel / kernel dispatches / buffers
    //
    // For a particular kernel k, kernelBufferMap[k] holds a vector of
    // host buffers used by k. The vector is filled at DlaQueue::Push(),
    // when kernel arguments are prepared.
    //
    // When a kenrel k is to be dispatched, kernelBufferMap[k] will be traversed
    // to figure out if there is any previous kernel dispatch associated for
    // each buffer b used by k.  This is done by checking bufferKernelMap[b].
    // If there are previous kernel dispatches which use b, then we wait on
    // them before dispatch kernel k. bufferKernelMap[b] will be cleared then.
    //
    // After kernel k is dispatched, we'll get a KalmarAsync object f, we then
    // walk through each buffer b used by k and mark the association as:
    // bufferKernelMap[b] = f
    //
    // Finally kernelBufferMap[k] will be cleared.
    //

    // association between buffers and kernel dispatches
    // key: buffer address
    // value: a vector of kernel dispatches
    std::map<void*, std::vector< std::weak_ptr<AsyncOp> > > bufferKernelMap;

    // association between a kernel and buffers used by it
    // key: kernel
    // value: a vector of buffers used by the kernel
    std::map<void*, std::vector<void*> > kernelBufferMap;

    // signal used by sync copy only
    signal_t  sync_copy_signal;

    uint64_t                                      queueSeqNum; // sequence-number of this queue.
    std::recursive_mutex   qmutex;  // Protect this DlaQueue. refer to DlaQueue::qmutex

public:
    DlaQueue(Device* pDev, device_t agent, execute_order order, queue_priority priority) ;

    bool nextKernelNeedsSysAcquire() const { return _nextKernelNeedsSysAcquire; };
    void setNextKernelNeedsSysAcquire(bool r) { _nextKernelNeedsSysAcquire = r; };

    bool nextSyncNeedsSysRelease() const {
      return _nextSyncNeedsSysRelease;
    };
    void setNextSyncNeedsSysRelease(bool r) {
      _nextSyncNeedsSysRelease = r;
    };

    uint64_t getSeqNum() const { return queueSeqNum; };

    DlaDevice * getDlaDev() const;

    void dispose() override;

    ~DlaQueue() {
        if (valid) {
            dispose();
        }
    }

    void printAsyncOps(std::ostream &s = std::cerr);

    // Save the command and type
    // TODO - can convert to reference?
    void pushAsyncOp(std::shared_ptr<Cmd> op);


    // std::shared_ptr<AsyncOp> detectStreamDeps(hcCommandKind newCommandKind, AsyncOp *kNewOp);

    void waitForStreamDeps (Cmd *newOp) ;

    int getPendingAsyncOps() override;

    bool isEmpty() override ;

    // Must retain this exact function signature here even though mode not used since virtual interface in
    // runtime depends on this signature.
    void wait(hcWaitMode mode = hcWaitModeBlocked) override ;

    // I think we should not use below function to launch kernel

    /*
    void LaunchKernel(void *ker, size_t nr_dim, size_t *global, size_t *local) override;
    void LaunchKernelWithDynamicGroupMemory(void *ker, size_t nr_dim, size_t *global, size_t *local, size_t dynamic_group_size) override ;
    std::shared_ptr<KalmarAsyncOp> LaunchKernelAsync(void *ker, size_t nr_dim, size_t *global, size_t *local) override ;
    std::shared_ptr<KalmarAsyncOp> LaunchKernelWithDynamicGroupMemoryAsync(void *ker, size_t nr_dim, size_t *global, size_t *local, size_t dynamic_group_size) override ;
    */

    void releaseToSystemIfNeeded();

    // wait for dependent async operations to complete
    void waitForDependentAsyncOps(void* buffer) ;


    // I don't we should need these, comment out
    /*
    void sync_copy(void* dst, device_t dst_agent,
                   const void* src, device_t src_agent,
                   size_t size);

    void read(void* device, void* dst, size_t count, size_t offset) override ;
    void write(void* device, const void* src, size_t count, size_t offset, bool blocking) override ;
    //FIXME: this API doesn't work in the P2P world because we don't who the source agent is!!!
    void copy(void* src, void* dst, size_t count, size_t src_offset, size_t dst_offset, bool blocking) override ;

    void* map(void* device, size_t count, size_t offset, bool modify) override ;
    void unmap(void* device, void* addr, size_t count, size_t offset, bool modify) override ;

    void Push(void *kernel, int idx, void *device, bool modify) override ;
    */

    void* getDlaQueue() override {
        return static_cast<void*>(rocrQueue);
    }

    queue_t *acquireLockedRocrQueue();

    void releaseLockedRocrQueue();

    // it looks we only need agent and memory_region
    // the agent is return from device which have CSI interface
    void* getDlaAgent() override;
    void* getDlaAMRegion() override;

    // void* getHostAgent() override;

    void* getDlaAMHostRegion() override;
    void* getDlaCoherentAMHostRegion() override;
    void* getDlaKernargRegion() override;
/*
    bool hasHSAInterOp() override {
        return true;
    }
*/
    void dispatch_hsa_kernel(const hsa_kernel_dispatch_packet_t *aql,
                             const void * args, size_t argsize,
                             completion_future *cf, const char *kernelName) override ;


    // enqueue a barrier packet
    std::shared_ptr<AsyncOp> EnqueueMarker(memory_scope release_scope) override ;


    // enqueue a barrier packet with multiple prior dependencies
    // The marker will wait for all specified input dependencies to resolve and
    // also for all older commands in the queue to execute, and then will
    std::shared_ptr<AsyncOp> EnqueueMarkerWithDependency(int count,
            std::shared_ptr <AsyncOp> *depOps,
            memory_scope fenceScope) override ;


    std::shared_ptr<AsyncOp> EnqueueAsyncCopyExt(const void* src, void* dst, size_t size_bytes,
                                                       hcCommandKind copyDir, const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo,
                                                       const Device *copyDevice) override;

    std::shared_ptr<AsyncOp> EnqueueAsyncCopy(const void *src, void *dst, size_t size_bytes) override ;

    // synchronous copy
    void copy(const void *src, void *dst, size_t size_bytes) override ;

    void copy_ext(const void *src, void *dst, size_t size_bytes, hcCommandKind copyDir, const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo,
                  const Device *copyDevice, bool forceUnpinnedCopy) override ;


    // remove finished async operation from waiting list
    void removeAsyncOp(Cmd* asyncOp);
};

} // namespace csl
