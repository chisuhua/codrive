#include "utils/lang/lang.h"
#include "inc/csq_accelerator.h"
#include "inc/csq_pointer.h"
#include "inc/csq.h"
#include "inc/csq_cmd.h"
#include "inc/csq_queue.h"
#include "inc/csq_device.h"

using namespace csq;

//-------------------------------------------

static unsigned extractBits(unsigned v, unsigned pos, unsigned w)
{
    return (v >> pos) & ((1 << w) - 1);
};


namespace csq {

void DlaQueue::printAsyncOps(std::ostream &s )
{
    signal_value_t oldv=0;
    s << *this << " : " << asyncOps.size() << " op entries\n";
    for (unsigned int i=0; i<asyncOps.size(); i++) {
        const std::shared_ptr<Cmd> &op = asyncOps[i];
        s << "index:" << std::setw(4) << i ;
        if (op != nullptr) {
            s << " op#"<< op->getSeqNum() ;
            signal_t signal = * (static_cast<signal_t*> (op->getNativeHandle()));
            signal_value_t v = 0;
            if (signal.handle) {
                v = hsa_signal_load_scacquire(signal);
            }
            s  << " " << getHcCommandKindString(op->getCommandKind());
            // TODO - replace with virtual function
            if (op->getCommandKind() == hcCommandMarker) {
                auto b = static_cast<CmdBarrier*> (op.get());
                s << " acq=" << extractBits(b->header, HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCACQUIRE_FENCE_SCOPE);
                s << ",rel=" << extractBits(b->header, HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCRELEASE_FENCE_SCOPE);
            } else if (op->getCommandKind() == hcCommandKernel) {
                auto d = static_cast<CmdDispatch*> (op.get());
                s << " acq=" << extractBits(d->getAql().header, HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCACQUIRE_FENCE_SCOPE);
                s << ",rel=" << extractBits(d->getAql().header, HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCRELEASE_FENCE_SCOPE);
            }
            s  << " completion=0x" << std::hex << signal.handle << std::dec <<",value=" << v;

            if (v != oldv) {
                s << " <--TRANSITION";
                oldv = v;
            }
        } else {
            s << " op <nullptr>";
        }
        s  << "\n";

    }
}

// Save the command and type
// TODO - can convert to reference?
void DlaQueue::pushAsyncOp(std::shared_ptr<Cmd> op) {

    op->setSeqNumFromQueue();
/*
    DBOUT(DB_CMD, "  pushing " << *op << " completion_signal="<< std::hex  << ((signal_t*)op->getNativeHandle())->handle << std::dec
                << "  commandKind=" << getHcCommandKindString(op->getCommandKind())
                << " "
                << (op->getCommandKind() == hcCommandKernel ? ((static_cast<HSADispatch*> (op.get()))->getKernelName()) : "")  // change to getLongKernelName() for mangled name
                << std::endl);
*/


    if (!drainingQueue_ && (asyncOps.size() >= MAX_INFLIGHT_COMMANDS_PER_QUEUE-1)) {
        // DBOUT(DB_WAIT, "*** Hit max inflight ops asyncOps.size=" << asyncOps.size() << ". " << op << " force sync\n");
        // DBOUT(DB_RESOURCE, "*** Hit max inflight ops asyncOps.size=" << asyncOps.size() << ". " << op << " force sync\n");

        drainingQueue_ = true;

        wait();
    }
    op->asyncOpsIndex(asyncOps.size());
    youngestCommandKind = op->getCommandKind();
    asyncOps.push_back(std::move(op));

    drainingQueue_ = false;
/*
    if (DBFLAG(DB_QUEUE)) {
        printAsyncOps(std::cerr);
    }
    */
}



// Check upcoming command that will be sent to this queue against the youngest async op
// in the queue to detect if any command dependency is required.
//
// The function returns nullptr if no dependency is required. For example, back-to-back commands
// of same type are often implicitly synchronized so no dependency is required.
//
// Also different modes and optimizations can control when dependencies are added.
// TODO - return reference if possible to avoid shared ptr overhead.
/*
std::shared_ptr<AsyncOp> detectStreamDeps(hcCommandKind newCommandKind, AsyncOp *kNewOp) {

    const auto newOp = static_cast<const Cmd*> (kNewOp);

    assert (newCommandKind != hcCommandInvalid);

    if (!asyncOps.empty() && asyncOps.back().get()!=nullptr) {
        assert (youngestCommandKind != hcCommandInvalid);

        // Ensure we have not already added the op we are checking into asyncOps,
        // that must be done after we check for deps.
        if (newOp && (newOp == asyncOps.back().get())) {
            throw runtime_exception("enqueued op before checking dependencies!", 0);
        }

        bool needDep = false;
        if  (newCommandKind != youngestCommandKind) {
            needDep = true;
        };


        if (((newCommandKind == hcCommandKernel) && (youngestCommandKind == hcCommandMarker)) ||
            ((newCommandKind == hcCommandMarker) && (youngestCommandKind == hcCommandKernel))) {

            // No dependency required since Marker and Kernel share same queue and are ordered by AQL barrier bit.
            needDep = false;
        } else if (isCopyCommand(newCommandKind) && isCopyCommand(youngestCommandKind)) {
            assert (newOp);
            auto cmd_copy = static_cast<const CmdCopy*> (newOp);
            auto youngestCopyOp = static_cast<const CmdCopy*> (asyncOps.back().get());
            if (cmd_copy->getCopyDevice() != youngestCopyOp->getCopyDevice()) {
                // This covers cases where two copies are back-to-back in the queue but use different copy engines.
                // In this case there is no implicit dependency between the ops so we need to add one
                // here.
                needDep = true;
                DBOUT(DB_CMD2, "Set NeedDep for " << newOp << "(different copy engines) " );
            }
            if (FORCE_SIGNAL_DEP_BETWEEN_COPIES) {
                DBOUT(DB_CMD2, "Set NeedDep for " << newOp << "(FORCE_SIGNAL_DEP_BETWEEN_COPIES) " );
                needDep = true;
            }
        }

        if (needDep) {
            return asyncOps.back();
        }
    }

    return nullptr;
}
*/


void DlaQueue::waitForStreamDeps (Cmd *newOp) {
    std::shared_ptr<AsyncOp> depOp = detectStreamDeps(newOp->getCommandKind(), newOp);
    if (depOp != nullptr) {
        EnqueueMarkerWithDependency(1, &depOp, HCC_OPT_FLUSH ? no_scope : system_scope);
    }
}


int DlaQueue::getPendingAsyncOps() {
    int count = 0;
    for (unsigned int i = 0; i < asyncOps.size(); ++i) {
        auto &asyncOp = asyncOps[i];

        if (asyncOp != nullptr) {
            signal_t signal = *(static_cast <signal_t*> (asyncOp->getNativeHandle()));
            if (signal.handle) {
                signal_value_t v = hsa_signal_load_scacquire(signal);
                if (v != 0) {
                    ++count;
                }
            } else {
                ++count;
            }
        }
    }
    return count;
}


bool DlaQueue::isEmpty() {
    // Have to walk asyncOps since it can contain null pointers (if event is waited on and removed)
    // Also not all commands contain signals.

    bool isEmpty = true;

    const auto& oldest = find_if(
                asyncOps.crbegin(), asyncOps.crend(), [](const std::shared_ptr<Cmd> &asyncOp) { return asyncOp != nullptr; });


    if (oldest != asyncOps.crend()) {
        signal_t signal = *(static_cast <signal_t*> ((*oldest)->getNativeHandle()));
        if (signal.handle) {
            signal_value_t v = hsa_signal_load_scacquire(signal);
            if (v != 0) {
                isEmpty=false;
            }
        } else {
            // oldest has no signal - enqueue a new one:
            auto marker = EnqueueMarker(system_scope);
            // DBOUTL(DB_CMD2, "Inside HSAQueue::isEmpty and queue contained only no-signal ops, enqueued marker " << marker << " into " << *this);
            isEmpty=false;
        }
    }

    return isEmpty;
};




// Must retain this exact function signature here even though mode not used since virtual interface in
// runtime depends on this signature.
void DlaQueue::wait(hcWaitMode mode) {
    // wait on all previous async operations to complete
    // Go in reverse order (from youngest to oldest).
    // Ensures younger ops have chance to complete before older ops reclaim their resources
    //


    if (HCC_OPT_FLUSH && nextSyncNeedsSysRelease()) {

        // In the loop below, this will be the first op waited on
        auto marker = EnqueueMarker(system_scope);

        // DBOUT(DB_CMD2, " Sys-release needed, enqueued marker into " << *this << " to release written data " << marker<<"\n");

    }
/*
    DBOUT(DB_WAIT, *this << " wait, contents:\n");
    if (DBFLAG(DB_WAIT)) {
        printAsyncOps(std::cerr);
    }
*/



    bool foundFirstValidOp = false;

    for (int i = asyncOps.size()-1; i >= 0;  i--) {
        if (asyncOps[i] != nullptr) {
            auto asyncOp = asyncOps[i];
            if (!foundFirstValidOp) {
                signal_t sig =  *(static_cast <signal_t*> (asyncOp->getNativeHandle()));
                assert(sig.handle != 0);
                foundFirstValidOp = true;
            }
            // wait on valid futures only
            std::shared_future<void>* future = asyncOp->getFuture();
            if (future && future->valid()) {
                future->wait();
            }
        }
    }
    // clear async operations table
    asyncOps.clear();
}


/*
    void DlaQueue::releaseToSystemIfNeeded()
    {
        if (HCC_OPT_FLUSH && nextSyncNeedsSysRelease()) {
            // In the loop below, this will be the first op waited on
            auto marker= EnqueueMarker(csq::system_scope);

            DBOUT(DB_CMD2, " In waitForDependentAsyncOps, sys-release needed: enqueued marker to release written data " << marker<<"\n");
        };
    }
*/

/*
    // wait for dependent async operations to complete
    void DlaQueue::waitForDependentAsyncOps(void* buffer) {
        auto&& dependentAsyncOpVector = bufferKernelMap[buffer];
        for (int i = 0; i < dependentAsyncOpVector.size(); ++i) {
          auto dependentAsyncOp = dependentAsyncOpVector[i];
          if (!dependentAsyncOp.expired()) {
            auto dependentAsyncOpPointer = dependentAsyncOp.lock();
            // wait on valid futures only
            std::shared_future<void>* future = dependentAsyncOpPointer->getFuture();
            if (future->valid()) {
              future->wait();
            }
          }
        }
        dependentAsyncOpVector.clear();
    }
*/

/*
    void sync_copy(void* dst, device_t dst_agent,
                   const void* src, device_t src_agent,
                   size_t size) {

      if (DBFLAG(DB_COPY)) {
        dumpHSAAgentInfo(src_agent, "sync_copy source agent");
        dumpHSAAgentInfo(dst_agent, "sync_copy destination agent");
      }

      status_t status;
      hsa_signal_store_relaxed(sync_copy_signal, 1);
      status = hsa_amd_memory_async_copy(dst, dst_agent,
                                          src, src_agent,
                                          size, 0, nullptr, sync_copy_signal);
      STATUS_CHECK(status, __LINE__);
      hsa_signal_wait_scacquire(sync_copy_signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
      return;
    }
*/

/*
    void read(void* device, void* dst, size_t count, size_t offset) override {
        waitForDependentAsyncOps(device);
        releaseToSystemIfNeeded();

        // do read
        if (dst != device) {
            if (!getDev()->is_unified()) {
                DBOUT(DB_COPY, "read(" << device << "," << dst << "," << count << "," << offset
                                << "): use HSA memory copy\n");
                status_t status = SUCCESS;
                // Make sure host memory is accessible to gpu
                // FIXME: host memory is allocated through OS allocator, if not, correct it.
                // dst--host buffer might be allocated through either OS allocator or hsa allocator.
                // Things become complicated, we may need some query API to query the pointer info, i.e.
                // allocator info. Same as write.
                device_t* agent = static_cast<hsa_agent_t*>(getHSAAgent());
                void* va = nullptr;
                status = hsa_amd_memory_lock(dst, count, agent, 1, &va);
                // TODO: If host buffer is not allocated through OS allocator, so far, lock
                // API will return nullptr to va, this is not specified in the spec, but will use it to
                // check if host buffer is allocated by hsa allocator
                if(va == NULL || status != SUCCESS)
                {
                    status = hsa_amd_agents_allow_access(1, agent, NULL, dst);
                    STATUS_CHECK(status, __LINE__);
                    va = dst;
                }

                sync_copy(va, *static_cast<device_t*>(getHostAgent()),  (char*)device + offset, *static_cast<hsa_agent_t*>(getHSAAgent()), count);

                // Unlock the host memory
                status = hsa_amd_memory_unlock(dst);
            } else {
                DBOUT(DB_COPY, "read(" << device << "," << dst << "," << count << "," << offset
                                << "): use host memory copy\n");
                memmove(dst, (char*)device + offset, count);
            }
        }
    }
*/

/*
    void write(void* device, const void* src, size_t count, size_t offset, bool blocking) override {
        waitForDependentAsyncOps(device);
        releaseToSystemIfNeeded(); // may not be needed.

        // do write
        if (src != device) {
            if (!getDev()->is_unified()) {
                DBOUT(DB_COPY, "write(" << device << "," << src << "," << count << "," << offset
                                << "," << blocking << "): use HSA memory copy\n");
                status_t status = SUCCESS;
                // Make sure host memory is accessible to gpu
                // FIXME: host memory is allocated through OS allocator, if not, correct it.
                device_t* agent = static_cast<hsa_agent_t*>(getHSAAgent());
                const void* va = nullptr;
                status = hsa_amd_memory_lock(const_cast<void*>(src), count, agent, 1, (void**)&va);

                if(va == NULL || status != SUCCESS)
                {
                    status = hsa_amd_agents_allow_access(1, agent, NULL, src);
                    STATUS_CHECK(status, __LINE__);
                    va = src;
                }
                sync_copy(((char*)device) + offset,  *agent, va,    *static_cast<device_t*>(getHostAgent()), count);

                STATUS_CHECK(status, __LINE__);
                // Unlock the host memory
                status = hsa_amd_memory_unlock(const_cast<void*>(src));
            } else {
                DBOUT(DB_COPY, "write(" << device << "," << src << "," << count << "," << offset
                                << "," << blocking << "): use host memory copy\n");
                memmove((char*)device + offset, src, count);
            }
        }
    }
*/

/*
    //FIXME: this API doesn't work in the P2P world because we don't who the source agent is!!!
    void copy(void* src, void* dst, size_t count, size_t src_offset, size_t dst_offset, bool blocking) override {
        waitForDependentAsyncOps(dst);
        waitForDependentAsyncOps(src);
        releaseToSystemIfNeeded();

        // do copy
        if (src != dst) {
            if (!getDev()->is_unified()) {
                DBOUT(DB_COPY, "copy(" << src << "," << dst << "," << count << "," << src_offset
                               << "," << dst_offset << "," << blocking << "): use HSA memory copy\n");
                status_t status = SUCCESS;
                // FIXME: aftre p2p enabled, if this function is not expected to copy between two buffers from different device, then, delete allow_access API call.
                device_t* agent = static_cast<hsa_agent_t*>(getHSAAgent());
                status = hsa_amd_agents_allow_access(1, agent, NULL, src);
                STATUS_CHECK(status, __LINE__);
                status = hsa_memory_copy((char*)dst + dst_offset, (char*)src + src_offset, count);
                STATUS_CHECK(status, __LINE__);
            } else {
                DBOUT(DB_COPY, "copy(" << src << "," << dst << "," << count << "," << src_offset
                               << "," << dst_offset << "," << blocking << "): use host memory copy\n");
                memmove((char*)dst + dst_offset, (char*)src + src_offset, count);
            }
        }
    }
*/
/*
    void* map(void* device, size_t count, size_t offset, bool modify) override {
        if (DBFLAG(DB_COPY)) {
            dumpHSAAgentInfo(*static_cast<device_t*>(getHSAAgent()), "map(...)");
        }
        waitForDependentAsyncOps(device);
        releaseToSystemIfNeeded();

        // do map
        // as HSA runtime doesn't have map/unmap facility at this moment,
        // we explicitly allocate a host memory buffer in this case
        if (!getDev()->is_unified()) {
            if (DBFLAG(DB_COPY)) {
                DBWSTREAM << getDev()->get_path();
                DBSTREAM << ": map( <device> " << device << ", <count> " << count << ", <offset> " << offset
                         << ", <modify> " << modify << "): use HSA memory map\n";
            }
            status_t status = SUCCESS;
            // allocate a host buffer
            // TODO: for safety, we copy to host, but we can map device memory to host through hsa_amd_agents_allow_access
            // withouth copying data.  (Note: CPU only has WC access to data, which has very poor read perf)
            void* data = nullptr;
            hsa_amd_memory_pool_t* am_host_region = static_cast<hsa_amd_memory_pool_t*>(getHSAAMHostRegion());
            status = hsa_amd_memory_pool_allocate(*am_host_region, count, 0, &data);
            STATUS_CHECK(status, __LINE__);
            if (data != nullptr) {
              // copy data from device buffer to host buffer
              device_t* agent = static_cast<hsa_agent_t*>(getHSAAgent());
              status = hsa_amd_agents_allow_access(1, agent, NULL, data);
              STATUS_CHECK(status, __LINE__);
              sync_copy(data, *static_cast<device_t*>(getHostAgent()), ((char*)device) + offset, *agent, count);
            } else {
              throw Kalmar::runtime_exception("host buffer allocation failed!", 0);
            }
            return data;
        } else {
            if (DBFLAG(DB_COPY)) {
              DBWSTREAM << getDev()->get_path();
              DBSTREAM << ": map( <device> " << device << ", <count> " << count << ", <offset> " << offset
                       << ", <modify> " << modify << "): use host memory map\n";
            }
            // for host memory we simply return the pointer plus offset
            return (char*)device + offset;
        }
    }

    void unmap(void* device, void* addr, size_t count, size_t offset, bool modify) override {
        // do unmap

        // as HSA runtime doesn't have map/unmap facility at this moment,
        // we free the host memory buffer allocated in map()
        if (!getDev()->is_unified()) {
            if (DBFLAG(DB_COPY)) {
                DBWSTREAM << getDev()->get_path();
                DBSTREAM << ": unmap( <device> " << device << ", <addr> " << addr << ", <count> " << count
                         << ", <offset> " << offset << ", <modify> " << modify << "): use HSA memory unmap\n";
            }
            if (modify) {
                // copy data from host buffer to device buffer
                status_t status = SUCCESS;

                device_t* agent = static_cast<hsa_agent_t*>(getHSAAgent());
                sync_copy(((char*)device) + offset, *agent, addr, *static_cast<device_t*>(getHostAgent()), count);
            }

            // deallocate the host buffer
            hsa_amd_memory_pool_free(addr);
        } else {
            if (DBFLAG(DB_COPY)) {
                DBWSTREAM << getDev()->get_path();
                DBSTREAM << ": unmap( <device> " << device << ", <addr> " << addr << ", <count> " << count
                         << ", <offset> " << offset << ", <modify> " << modify <<"): use host memory unmap\n";
            }
            // for host memory there's nothing to be done
        }
    }
*/
/*
    void Push(void *kernel, int idx, void *device, bool modify) override {
        PushArgImpl(kernel, idx, sizeof(void*), &device);

        // register the buffer with the kernel
        // when the buffer may be read/written by the kernel
        // the buffer is not registered if it's only read by the kernel
        if (modify) {
          kernelBufferMap[kernel].push_back(device);
        }
    }
*/

// enqueue a barrier packet
std::shared_ptr<AsyncOp> DlaQueue::EnqueueMarker(memory_scope release_scope) {

    status_t status = SUCCESS;

    // create shared_ptr instance
    std::shared_ptr<CmdBarrier> barrier = std::make_shared<CmdBarrier>(this, 0, nullptr);
    // associate the barrier with this queue
    pushAsyncOp(barrier);

    // enqueue the barrier
    status = barrier.get()->enqueueAsync(release_scope);
    STATUS_CHECK(status, __LINE__);

    return barrier;
}


// enqueue a barrier packet with multiple prior dependencies
// The marker will wait for all specified input dependencies to resolve and
// also for all older commands in the queue to execute, and then will
// signal completion by decrementing the associated signal.
//
// depOps specifies the other ops that this marker will depend on.  These
// can be in any queue on any GPU .
//
// fenceScope specifies the scope of the acquire and release fence that will be
// applied after the marker executes.  See csq::memory_scope
std::shared_ptr<AsyncOp> DlaQueue::EnqueueMarkerWithDependency(int count,
        std::shared_ptr <AsyncOp> *depOps,
        memory_scope fenceScope) {

    status_t status = SUCCESS;

    if ((count >= 0) && (count <= HSA_BARRIER_DEP_SIGNAL_CNT)) {

        // create shared_ptr instance
        std::shared_ptr<CmdBarrier> barrier = std::make_shared<CmdBarrier>(this, count, depOps);
        // associate the barrier with this queue
        pushAsyncOp(barrier);

        for (int i=0; i<count; i++) {
            auto depOp = barrier->depAsyncOps[i];
            if (depOp != nullptr) {
                auto depHSAQueue = static_cast<DlaQueue *> (depOp->getQueue());
                // Same accelerator:
                // Inherit system-acquire and system-release bits op we are dependent on.
                //   - barriers
                //
                // _nextSyncNeedsSysRelease is set when a queue executes a kernel.
                // It indicates the queue needs to execute a release-to-system
                // before host can see the data - this is important for kernels which write
                // non-coherent zero-copy host memory.
                // If creating a dependency on a queue which needs_system_release, copy that
                // state here.   If the host then waits on the freshly created marker,
                // runtime will issue a system-release fence.
                if (depOp->barrierNextKernelNeedsSysAcquire()) {
                    setNextKernelNeedsSysAcquire(true);
                }
                if (depOp->barrierNextSyncNeedsSysRelease()) {
                    setNextSyncNeedsSysRelease(true);
                }
                if (HCC_FORCE_CROSS_QUEUE_FLUSH & 0x1) {
                    if (!depOp->barrierNextKernelNeedsSysAcquire()) {
                    }
                    setNextKernelNeedsSysAcquire(true);
                }
                if (HCC_FORCE_CROSS_QUEUE_FLUSH & 0x2) {
                    if (!depOp->barrierNextSyncNeedsSysRelease()) {
                    }
                    setNextSyncNeedsSysRelease(true);
                }

                if (depHSAQueue->getDlaDev() != this->getDlaDev()) {
                    // Cross-accelerator dependency case.
                    // This requires system-scope acquire
                    // TODO - only needed if these are peer GPUs, could optimize with an extra check
                    // DBOUT(DB_WAIT, "  Adding cross-accelerator system-scope acquire\n");
                    barrier->acquire_scope (system_scope);
                }

            } else {
                break;
            }
        }

        // enqueue the barrier
        status = barrier.get()->enqueueAsync(fenceScope);
        STATUS_CHECK(status, __LINE__);
        return barrier;
    } else {
        // throw an exception
        throw utils::Error("Incorrect number of dependent signals passed to EnqueueMarkerWithDependency");
    }
}

// synchronous copy
void DlaQueue::copy(const void *src, void *dst, size_t size_bytes)  {
    // DBOUT(DB_COPY, "HSAQueue::copy(" << src << ", " << dst << ", " << size_bytes << ")\n");
    // wait for all previous async commands in this queue to finish
    this->wait();

    // create a CmdCopy instance
    CmdCopy* copyCommand = new CmdCopy(this, src, dst, size_bytes);

    // synchronously do copy
    copyCommand->syncCopy();

    delete(copyCommand);
}

// remove finished async operation from waiting list
void DlaQueue::removeAsyncOp(Cmd* asyncOp) {
    uint32_t targetIndex = asyncOp->asyncOpsIndex();

    // Make sure the opindex is still valid.
    // If the queue is destroyed first it may not exist in asyncops anymore so no need to destroy.
    if (targetIndex < asyncOps.size() &&
        asyncOp == asyncOps[targetIndex].get()) {

        // All older ops are known to be done and we can reclaim their resources here:
        // Both execute_in_order and execute_any_order flags always remove ops in-order at the end of the pipe.
        // Note if not found above targetIndex=-1 and we skip the loop:
        for (int i = targetIndex; i>=0; i--) {
            AsyncOp *op = asyncOps[i].get();
            if (op) {
                asyncOps[i].reset();

    #if CHECK_OLDER_COMPLETE
                // opportunistically update status for any ops we encounter along the way:
                signal_t signal =  *(static_cast<signal_t*> (op->getNativeHandle()));

                // v<0 : no signal, v==0 signal and done, v>0 : signal and not done:
                signal_value_t v = -1;
                if (signal.handle)
                    v = hsa_signal_load_scacquire(signal);
                assert (v <=0);
    #endif

            } else {
                // The queue is retired in-order, and ops only inserted at "top", and ops can only be removed at two defined points:
                //   - Draining the entire queue in HSAQueue::wait() - this calls asyncOps.clear()
                //   - Events in the middle of the queue can be removed, but will call this function which removes all older ops.
                //   So once we remove the asyncOps, there is no way for an older async op to be come non-null and we can stop search here:

                break; // stop searching if we find null, there cannot be any more valid pointers below.
            }
        }
    }


    // GC for finished kernels
    if (asyncOps.size() > ASYNCOPS_VECTOR_GC_SIZE) {
        // DBOUTL(DB_RESOURCE, "asyncOps size=" << asyncOps.size() << " exceeds collection size, compacting");
        asyncOps.erase(std::remove(asyncOps.begin(), asyncOps.end(), nullptr),
                     asyncOps.end());
    }
}


std::ostream& operator<<(std::ostream& os, const DlaQueue & hav)
{
    auto device = static_cast<DlaDevice*>(hav.getDev());
    os << "queue#" << device->accSeqNum << "." << hav.queueSeqNum;
    return os;
}


DlaQueue::DlaQueue(Device* pDev, device_t agent, execute_order order, queue_priority priority) :
    Queue(pDev, queuing_mode_automatic, order, priority),
    rocrQueue(nullptr),
    asyncOps(), drainingQueue_(false),
    valid(true), _nextSyncNeedsSysRelease(false), _nextKernelNeedsSysAcquire(false), bufferKernelMap(), kernelBufferMap()
{
    {
        // Protect the HSA queue we can steal it.
        // DBOUT(DB_LOCK, " ptr:" << this << " create lock_guard...\n");

        std::lock_guard<std::recursive_mutex> l(this->qmutex);

        auto device = static_cast<DlaDevice*>(this->getDev());
        device->createOrstealRocrQueue(this, priority);
    }


    youngestCommandKind = hcCommandInvalid;

    status_t status= hsa_signal_create(1, 1, &agent, &sync_copy_signal);
    STATUS_CHECK(status, __LINE__);
}


void DlaQueue::dispose()  {
    status_t status;

    // DBOUT(DB_INIT, "HSAQueue::dispose() " << this << "in\n");
    {
        // DBOUT(DB_LOCK, " ptr:" << this << " dispose lock_guard...\n");

        DlaDevice* device = static_cast<DlaDevice*>(getDev());

        // NOTE: needs to acquire rocrQueuesMutex and then the qumtex in this
        // sequence in order to avoid potential deadlock with other threads
        // executing createOrstealRocrQueue at the same time
        std::lock_guard<std::mutex> rl(device->rocrQueuesMutex);
        std::lock_guard<std::recursive_mutex> l(this->qmutex);

        // wait on all existing kernel dispatches and barriers to complete
        wait();

        this->valid = false;

        // clear bufferKernelMap
        for (auto iter = bufferKernelMap.begin(); iter != bufferKernelMap.end(); ++iter) {
           iter->second.clear();
        }
        bufferKernelMap.clear();

        // clear kernelBufferMap
        for (auto iter = kernelBufferMap.begin(); iter != kernelBufferMap.end(); ++iter) {
            iter->second.clear();
        }
        kernelBufferMap.clear();

        if (this->rocrQueue != nullptr) {
            device->removeRocrQueue(rocrQueue);
            rocrQueue = nullptr;
        }
    }

    status = hsa_signal_destroy(sync_copy_signal);

    STATUS_CHECK(status, __LINE__);

    // DBOUT(DB_INIT, "HSAQueue::dispose() " << this <<  " out\n");
}

DlaDevice * DlaQueue::getDlaDev() const {
    return static_cast<DlaDevice*>(this->getDev());
};

queue_t *DlaQueue::acquireLockedRocrQueue() {
    // DBOUT(DB_LOCK, " ptr:" << this << " lock...\n");
    this->qmutex.lock();
    if (this->rocrQueue == nullptr) {
        auto device = static_cast<DlaDevice*>(this->getDev());
        device->createOrstealRocrQueue(this);
    }

    // DBOUT (DB_QUEUE, "acquireLockedRocrQueue returned hwQueue=" << this->rocrQueue->_hwQueue << "\n");
    assert (this->rocrQueue->_hwQueue != 0);
    return this->rocrQueue->_hwQueue;
}

void DlaQueue::releaseLockedRocrQueue()
{

    // DBOUT(DB_LOCK, " ptr:" << this << " unlock...\n");
    this->qmutex.unlock();
}

void* DlaQueue::getDlaAgent()  {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getAgent()));
}

/*
void* DlaQueue::getHostAgent() {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getHostAgent()));
}
*/

void* DlaQueue::getDlaAMRegion()  {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getDlaAMRegion()));
}

void* DlaQueue::getDlaKernargRegion() {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getHSAKernargRegion()));
}
void* DlaQueue::getDlaCoherentAMHostRegion() {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getHSACoherentAMHostRegion()));
}
void* DlaQueue::getDlaAMHostRegion() {
    return static_cast<void*>(&(static_cast<DlaDevice*>(getDev())->getHSAAMHostRegion()));
}


void DlaQueue::copy_ext(const void *src, void *dst, size_t size_bytes, csq::hcCommandKind copyDir, const csq::AmPointerInfo &srcPtrInfo, const csq::AmPointerInfo &dstPtrInfo,
              const Device *copyDevice, bool forceUnpinnedCopy) {
    // wait for all previous async commands in this queue to finish
    // TODO - can remove this synchronization, copy is tail-synchronous not required on front end.
    this->wait();

    const DlaDevice *copyDeviceHsa = static_cast<const DlaDevice*> (copyDevice);

    // create a HSACopy instance
    CmdCopy* copyCommand = new CmdCopy(this, src, dst, size_bytes);
    copyCommand->setCommandKind(copyDir);

    // synchronously do copy
    // FIX me, pull from constructor.
    copyCommand->syncCopyExt(copyDir, srcPtrInfo, dstPtrInfo, copyDeviceHsa, forceUnpinnedCopy);

    // TODO - should remove from queue instead?
    delete(copyCommand);
};

std::shared_ptr<AsyncOp> DlaQueue::EnqueueAsyncCopyExt(const void* src, void* dst, size_t size_bytes,
                                                   hcCommandKind copyDir, const csq::AmPointerInfo &srcPtrInfo, const csq::AmPointerInfo &dstPtrInfo,
                                                   const Device *copyDevice) {


    status_t status = SUCCESS;

    // create shared_ptr instance
    const DlaDevice *copyDeviceHsa = static_cast<const DlaDevice*> (copyDevice);
    std::shared_ptr<CmdCopy> copyCommand = std::make_shared<CmdCopy>(this, src, dst, size_bytes);

    // euqueue the async copy command
    status = copyCommand.get()->enqueueAsyncCopyCommand(copyDeviceHsa, srcPtrInfo, dstPtrInfo);
    STATUS_CHECK(status, __LINE__);

    // associate the async copy command with this queue
    pushAsyncOp(copyCommand);

    return copyCommand;
};


// enqueue an async copy command
std::shared_ptr<AsyncOp> DlaQueue::EnqueueAsyncCopy(const void *src, void *dst, size_t size_bytes)  {
    status_t status = SUCCESS;

    // create shared_ptr instance
    std::shared_ptr<CmdCopy> copyCommand = std::make_shared<CmdCopy>(this, src, dst, size_bytes);

    csq::accelerator acc;
    // std::size_t sizes = 0;
    csq::AmPointerInfo srcPtrInfo(NULL, NULL, NULL, 0, &acc, 0, 0);
    // csq::AmPointerInfo srcPtrInfo((void*)NULL, (void*)NULL, (void*)NULL, sizes, &acc, false, false);
    csq::AmPointerInfo dstPtrInfo(NULL, NULL, NULL, 0, &acc, 0, 0);
    // csq::AmPointerInfo dstPtrInfo((void*)NULL, (void*)NULL, (void*)NULL, 0, &acc, 0, 0);

    bool srcInTracker = (csq::am_memtracker_getinfo(&srcPtrInfo, src) == AM_SUCCESS);
    bool dstInTracker = (csq::am_memtracker_getinfo(&dstPtrInfo, dst) == AM_SUCCESS);

    if (!srcInTracker) {
        throw utils::Error("trying to copy from unpinned src pointer");
    } else if (!dstInTracker) {
        throw utils::Error("trying to copy from unpinned dst pointer");
    };


    // Select optimal copy agent:
    // Prefer source SDMA engine if possible since this is typically the fastest, unless the source data is in host mem.
    //
    // If the src agent cannot see both src and dest pointers, then the async copy will fault.
    // The caller of this function is responsible for avoiding this situation, by examining the
    // host and device allow-access mappings and using a CPU staging copy BEFORE calling
    // this routine.
    const DlaDevice *copyDevice;
    if (srcPtrInfo._isInDeviceMem) {  // D2H or D2D
        copyDevice = static_cast<DlaDevice*>(srcPtrInfo._acc->get_dev_ptr());
    } else if (dstPtrInfo._isInDeviceMem) { // H2D
        copyDevice = static_cast<DlaDevice*>(dstPtrInfo._acc->get_dev_ptr());
    } else {
        copyDevice = nullptr; // H2H
    }

    // enqueue the async copy command
    status = copyCommand.get()->enqueueAsyncCopyCommand(copyDevice, srcPtrInfo, dstPtrInfo);
    STATUS_CHECK(status, __LINE__);

    // associate the async copy command with this queue
    pushAsyncOp(copyCommand);

    return copyCommand;
}


void
DlaQueue::dispatch_hsa_kernel(const hsa_kernel_dispatch_packet_t *aql,
                         const void * args, size_t argSize,
                         csq::completion_future *cf, const char *kernelName)
{
    uint16_t dims = (aql->kernel_ctrl >> HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS) &
                    ((1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_WIDTH_DIMENSIONS) - 1);

    if (dims == 0) {
        throw utils::Error("dispatch_hsa_kernel: must set dims in aql.header");
    }

    uint16_t packetType = (aql->header >> HSA_PACKET_HEADER_TYPE) &
                          ((1 << HSA_PACKET_HEADER_WIDTH_TYPE) - 1);


    if (packetType != HSA_PACKET_TYPE_KERNEL_DISPATCH) {
        throw utils::Error("dispatch_hsa_kernel: must set packetType and fence bits in aql.header");
    }


    DlaDevice* device = static_cast<DlaDevice*>(this->getDev());

    std::shared_ptr<CmdDispatch> sp_dispatch = std::make_shared<CmdDispatch>(device, this/*queue*/, nullptr, aql);
    if (HCC_OPT_FLUSH) {
        sp_dispatch->overrideAcquireFenceIfNeeded();
    }

    CmdDispatch *dispatch = sp_dispatch.get();
    waitForStreamDeps(dispatch);

    pushAsyncOp(sp_dispatch);
    dispatch->setKernelName(kernelName);


    // May be faster to create signals for each dispatch than to use markers.
    // Perhaps could check HSA queue pointers.
    bool needsSignal = true;
    if (HCC_OPT_FLUSH && !HCC_PROFILE && (cf==nullptr) && !HCC_FORCE_COMPLETION_FUTURE && !HCC_SERIALIZE_KERNEL) {
        // Only allocate a signal if the caller requested a completion_future to track status.
        needsSignal = false;
    };

    dispatch->dispatchKernelAsync(args, argSize, needsSignal);


    if (cf) {
        *cf = csq::completion_future(sp_dispatch);
    }
};


void RocrQueue::assignHccQueue(DlaQueue *hccQueue) {
    assert (hccQueue->rocrQueue == nullptr);  // only needy should assign new queue
    hccQueue->rocrQueue = this;
    _hccQueue = hccQueue;
}

};
