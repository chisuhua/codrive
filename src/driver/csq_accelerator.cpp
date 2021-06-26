#include "inc/csq_accelerator.h"

namespace csq
{

// ------------------------------------------------------------------------
// accelerator
// ------------------------------------------------------------------------

    /**
     * Returns a std::vector of accelerator objects (in no specific
     * order) representing all accelerators that are available, including
     * reference accelerators and WARP accelerators if available.
     *
     * @return A vector of accelerators.
     */
    std::vector<accelerator> accelerator::get_all() 
    {
        auto Devices = csq::getContext()->getDevices();
        std::vector<accelerator> ret;
        for(auto&& i : Devices)
          ret.push_back(i);
        return ret;
    }
    /**
     * Sets the default accelerator to the device path identified by the "path"
     * argument. See the constructor accelerator(const std::wstring& path)
     * for a description of the allowable path strings.
     *
     * This establishes a process-wide default accelerator and influences all
     * subsequent operations that might use a default accelerator.
     *
     * @param[in] path The device path of the default accelerator.
     * @return A Boolean flag indicating whether the default was set. If the
     *         default has already been set for this process, this value will be
     *         false, and the function will have no effect.
     */
    bool accelerator::set_default(const std::wstring& path) 
    {
        return csq::getContext()->set_default(path);
    }

    /**
     * Creates and returns a new accelerator view on the accelerator with the
     * supplied queuing mode.
     *
     * @param[in] qmode The queuing mode of the accelerator_view to be created.
     *                  See "Queuing Mode". The default value would be
     *                  queueing_mdoe_automatic if not specified.
     */
    accelerator_view accelerator::create_view(csq::execute_order order, csq::queuing_mode mode, queue_priority priority ) {
        auto pQueue = pDev->createQueue(order, priority);
        pQueue->set_mode(mode);
        return pQueue;
    }



// ------------------------------------------------------------------------
// member function implementations
// ------------------------------------------------------------------------

accelerator
accelerator_view::get_accelerator() const { return pQueue->getDev(); }

completion_future accelerator_view::create_marker(csq::memory_scope scope) const {
    std::shared_ptr<csq::AsyncOp> deps[1]; 
    // If necessary create an explicit dependency on previous command
    // This is necessary for example if copy command is followed by marker - we need the marker to wait for the copy to complete.
    std::shared_ptr<csq::AsyncOp> depOp = pQueue->detectStreamDeps(csq::hcCommandMarker, nullptr);

    int cnt = 0;
    if (depOp) {
        deps[cnt++] = depOp; // retrieve async op associated with completion_future
    }

    return completion_future(pQueue->EnqueueMarkerWithDependency(cnt, deps, scope));
}

unsigned int accelerator_view::get_version() const { return get_accelerator().get_version(); }

completion_future accelerator_view::create_blocking_marker(completion_future& dependent_future, csq::memory_scope scope) const {
    std::shared_ptr<csq::AsyncOp> deps[2]; 

    // If necessary create an explicit dependency on previous command
    // This is necessary for example if copy command is followed by marker - we need the marker to wait for the copy to complete.
    std::shared_ptr<csq::AsyncOp> depOp = pQueue->detectStreamDeps(csq::hcCommandMarker, nullptr);

    int cnt = 0;
    if (depOp) {
        deps[cnt++] = depOp; // retrieve async op associated with completion_future
    }

    if (dependent_future.__asyncOp) {
        deps[cnt++] = dependent_future.__asyncOp; // retrieve async op associated with completion_future
    } 
    
    return completion_future(pQueue->EnqueueMarkerWithDependency(cnt, deps, scope));
}
/*
template<typename InputIterator>
completion_future
accelerator_view::create_blocking_marker(InputIterator first, InputIterator last, csq::memory_scope scope) const {
    std::shared_ptr<csq::AsyncOp> deps[5]; // array of 5 pointers to the native handle of async ops. 5 is the max supported by barrier packet
    completion_future lastMarker;


    // If necessary create an explicit dependency on previous command
    // This is necessary for example if copy command is followed by marker - we need the marker to wait for the copy to complete.
    std::shared_ptr<csq::AsyncOp> depOp = pQueue->detectStreamDeps(hcCommandMarker, nullptr);

    int cnt = 0;
    if (depOp) {
        deps[cnt++] = depOp; // retrieve async op associated with completion_future
    }


    // loop through signals and group into sections of 5
    // every 5 signals goes into one barrier packet
    // since HC sets the barrier bit in each AND barrier packet, we know
    // the barriers will execute in-order
    for (auto iter = first; iter != last; ++iter) {
        if (iter->__asyncOp) {
            deps[cnt++] = iter->__asyncOp; // retrieve async op associated with completion_future
            if (cnt == 5) {
                lastMarker = completion_future(pQueue->EnqueueMarkerWithDependency(cnt, deps, csq::no_scope));
                cnt = 0;
            }
        }
    }

    if (cnt) {
        lastMarker = completion_future(pQueue->EnqueueMarkerWithDependency(cnt, deps, scope));
    }

    return lastMarker;
}
*/

completion_future
accelerator_view::create_blocking_marker(std::initializer_list<completion_future> dependent_future_list, csq::memory_scope scope) const {
    return create_blocking_marker(dependent_future_list.begin(), dependent_future_list.end(), scope);
}


void accelerator_view::copy_ext(const void *src, void *dst, size_t size_bytes, hcCommandKind copyDir, const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, const accelerator *copyAcc, bool forceUnpinnedCopy) {
    pQueue->copy_ext(src, dst, size_bytes, copyDir, srcInfo, dstInfo, copyAcc ? copyAcc->pDev : nullptr, forceUnpinnedCopy);
};

void accelerator_view::copy_ext(const void *src, void *dst, size_t size_bytes, hcCommandKind copyDir, const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, bool forceHostCopyEngine) {
    pQueue->copy_ext(src, dst, size_bytes, copyDir, srcInfo, dstInfo, forceHostCopyEngine);
};

completion_future
accelerator_view::copy_async(const void *src, void *dst, size_t size_bytes) {
    return completion_future(pQueue->EnqueueAsyncCopy(src, dst, size_bytes));
}

completion_future
accelerator_view::copy_async_ext(const void *src, void *dst, size_t size_bytes,
                             csq::hcCommandKind copyDir, 
                             const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, 
                             const accelerator *copyAcc)
{
    return completion_future(pQueue->EnqueueAsyncCopyExt(src, dst, size_bytes, copyDir, srcInfo, dstInfo, copyAcc ? copyAcc->pDev : nullptr));
};


} // namespace csq
