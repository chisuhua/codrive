#include "utils/lang/error.h"
#include "inc/pps_ext.h"
#include "inc/csq_cmd.h"
#include "inc/csq_context.h"
#include "inc/csq_device.h"
#include "inc/csq_pointer.h"
#include "utils/lang/debug.h"

static utils::Debug debug;
#define DBOUTL(db_flag, msg)  debug << msg << "\n";
#define DBOUT(db_flag, msg)  debug << msg << "\n";

unsigned extractBits(unsigned v, unsigned pos, unsigned w)
{
    return (v >> pos) & ((1 << w) - 1);
};


using namespace csq;

DlaContext* getDlaContext()
{
    return dynamic_cast<DlaContext*>(getContext());
}


const char* getHSAErrorString(status_t s) {
    const char* case_string;
    switch(s) {
        CASE_STRING(ERROR);
        CASE_STRING(ERROR_INVALID_ARGUMENT);
        CASE_STRING(ERROR_INVALID_QUEUE_CREATION);
        CASE_STRING(ERROR_INVALID_ALLOCATION);
        CASE_STRING(ERROR_INVALID_AGENT);
        CASE_STRING(ERROR_INVALID_REGION);
        CASE_STRING(ERROR_INVALID_SIGNAL);
        CASE_STRING(ERROR_INVALID_QUEUE);
        CASE_STRING(ERROR_OUT_OF_RESOURCES);
        CASE_STRING(ERROR_INVALID_PACKET_FORMAT);
        CASE_STRING(ERROR_RESOURCE_FREE);
        CASE_STRING(ERROR_NOT_INITIALIZED);
        CASE_STRING(ERROR_REFCOUNT_OVERFLOW);
        CASE_STRING(ERROR_INCOMPATIBLE_ARGUMENTS);
        CASE_STRING(ERROR_INVALID_INDEX);
        CASE_STRING(ERROR_INVALID_ISA);
        CASE_STRING(ERROR_INVALID_ISA_NAME);
        CASE_STRING(ERROR_INVALID_CODE_OBJECT);
        CASE_STRING(ERROR_INVALID_EXECUTABLE);
        CASE_STRING(ERROR_FROZEN_EXECUTABLE);
        CASE_STRING(ERROR_INVALID_SYMBOL_NAME);
        CASE_STRING(ERROR_VARIABLE_ALREADY_DEFINED);
        CASE_STRING(ERROR_VARIABLE_UNDEFINED);
        CASE_STRING(ERROR_EXCEPTION);
        default: case_string = "Unknown Error Code";
    };
    return case_string;
};

namespace csq {

const char* getHcCommandKindString( hcCommandKind k) {
    const char* case_string;

    switch(k) {
        CASE_STRING(hcCommandInvalid);
        CASE_STRING(hcMemcpyHostToHost);
        CASE_STRING(hcMemcpyHostToDevice);
        CASE_STRING(hcMemcpyDeviceToHost);
        CASE_STRING(hcMemcpyDeviceToDevice);
        CASE_STRING(hcCommandKernel);
        CASE_STRING(hcCommandMarker);
        default: case_string = "Unknown command type";
    };
    return case_string;
};

// ----------------------------------------------------------------------
// member function implementation of HSADispatch
// ----------------------------------------------------------------------

CmdDispatch::CmdDispatch(DlaDevice* _device, Queue *queue, CslKernel* _kernel, const hsa_kernel_dispatch_packet_t *aql) :
    Cmd(queue, hcCommandKernel),
    device(_device),
    kernel_name(nullptr),
    kernel(_kernel),
    kernargMemory(nullptr),
    isDispatched(false),
    waitMode(HSA_WAIT_STATE_BLOCKED),
    future(nullptr)
{
    if (aql) {
        this->aql = *aql;
    }
    clearArgs();
}

/*
static std::ostream& PrintHeader(std::ostream& os, uint16_t h)
{
    os << "header=" << std::hex << h << "("
    //os << std::hex << "("
       << "type=" << extractBits(h, HSA_PACKET_HEADER_TYPE, HSA_PACKET_HEADER_WIDTH_TYPE)
       << ",barrier=" << extractBits (h, HSA_PACKET_HEADER_BARRIER, HSA_PACKET_HEADER_WIDTH_BARRIER)
       << ",acquire=" << extractBits(h, HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCACQUIRE_FENCE_SCOPE)
       << ",release=" << extractBits(h, HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCRELEASE_FENCE_SCOPE)
       << ")";
    return os;
}
*/

/*
static std::ostream& operator<<(std::ostream& os, const hsa_kernel_dispatch_packet_t &aql)
{
    PrintHeader(os, aql.header);
    os << " setup=" << std::hex <<  aql.setup
       << " grid=[" << std::dec << aql.grid_size_x << "." <<  aql.grid_size_y << "." <<  aql.grid_size_z << "]"
       << " group=[" << std::dec << aql.workgroup_size_x << "." <<  aql.workgroup_size_y << "." <<  aql.workgroup_size_z << "]"
       << " private_seg_size=" <<  aql.private_segment_size
       << " group_seg_size=" <<  aql.group_segment_size
       << " kernel_object=0x" << std::hex <<  aql.kernel_object
       << " kernarg_address=0x" <<  aql.kernarg_address
       << " completion_signal=0x" <<  aql.completion_signal.handle
       << std::dec;
    return os;
}
*/
/*
static std::string rawAql(const hsa_kernel_dispatch_packet_t &aql)
{
    std::stringstream ss;
    const unsigned *aqlBytes = (unsigned*)&aql;
     ss << "    raw_aql=[" << std::hex << std::setfill('0');
     for (unsigned int i=0; i<sizeof(aql)/sizeof(unsigned); i++) {
         ss << " 0x" << std::setw(8) << aqlBytes[i];
     }
     ss << " ]" ;
     return ss.str();
}
*/
/*
static std::ostream& operator<<(std::ostream& os, const hsa_barrier_and_packet_t &aql)
{
    PrintHeader(os, aql.header);
    os << " dep_signal[0]=0x" <<  aql.dep_signal[0].handle
       << " dep_signal[1]=0x" <<  aql.dep_signal[1].handle
       << " dep_signal[2]=0x" <<  aql.dep_signal[2].handle
       << " dep_signal[3]=0x" <<  aql.dep_signal[3].handle
       << " dep_signal[4]=0x" <<  aql.dep_signal[4].handle
       << " completion_signal=0x" <<  aql.completion_signal.handle
       << std::dec;
   return os;
}
*/

//static std::ostream& rawAql(std::ostream& os, const hsa_barrier_and_packet_t &aql)
/*
static std::string rawAql(const hsa_barrier_and_packet_t &aql)
{
    std::stringstream ss;
    const unsigned *aqlBytes = (unsigned*)&aql;
     ss << "    raw_aql=[" << std::hex << std::setfill('0');
     for (unsigned int i=0; i<sizeof(aql)/sizeof(unsigned); i++) {
         ss << " 0x" << std::setw(8) << aqlBytes[i];
     }
     ss << " ]" ;
     return ss.str();
}
*/

/*
static void printKernarg(const void *kernarg_address, int bytesToPrint)
{
    const unsigned int *ck = static_cast<const unsigned int*> (kernarg_address);

    std::stringstream ks;
    ks << "kernarg_address: 0x" << kernarg_address << ", total of " << bytesToPrint << " bytes:";
    for (unsigned int i=0; i<bytesToPrint/sizeof(unsigned int); i++) {
        bool newLine = ((i % 4) ==0);

        if (newLine) {
            ks << "\n      ";
            ks << "0x" << std::setw(16) << std::setfill('0') << &(ck[i]) <<  ": " ;
        }

        ks << "0x" << std::hex << std::setfill('0') << std::setw(8) << ck[i] << "  ";
    };
    ks << "\n";

    DBOUT(DB_KERNARG, ks.str());
}
*/


// dispatch a kernel asynchronously
// -  allocates signal, copies arguments into kernarg buffer, and places aql packet into queue.
status_t CmdDispatch::dispatchKernel(queue_t* lockedHsaQueue, const void *hostKernarg,
                            int hostKernargSize, bool allocSignal) {

    status_t status = SUCCESS;
    if (isDispatched) {
        return ERROR_INVALID_ARGUMENT;
    }


    /*
     * Setup the dispatch information.
     */
    // set dispatch fences
    // The fence bits must be set on entry into this function.
    uint16_t header = aql.header;
    if (hsaQueue()->get_execute_order() == execute_in_order) {
        //std::cout << "barrier bit on\n";
        // set AQL header with barrier bit on if execute in order
        header |= ((HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
                     (1 << HSA_PACKET_HEADER_BARRIER));
    } else {
        //std::cout << "barrier bit off\n";
        // set AQL header with barrier bit off if execute in any order
        header |= (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE);
    }


    // bind kernel arguments
    //printf("hostKernargSize size: %d in bytesn", hostKernargSize);

    if (hostKernargSize > 0) {
        // hsa_amd_memory_pool_t kernarg_region = device->getHSAKernargRegion();
        std::pair<void*, int> ret = device->getKernargBuffer(hostKernargSize);
        kernargMemory = ret.first;
        kernargMemoryIndex = ret.second;
        //std::cerr << "op #" << getSeqNum() << " allocated kernarg cursor=" << kernargMemoryIndex << "\n";

        // as kernarg buffers are fine-grained, we can directly use memcpy
        memcpy(kernargMemory, hostKernarg, hostKernargSize);

        aql.kernarg_address = kernargMemory;
    } else {
        aql.kernarg_address = nullptr;
    }


    // write packet
    uint32_t queueMask = lockedHsaQueue->size - 1;
    // TODO: Need to check if package write is correct.
    uint64_t index = hsa_queue_load_write_index_relaxed(lockedHsaQueue);
    uint64_t nextIndex = index + 1;
    if (nextIndex - hsa_queue_load_read_index_scacquire(lockedHsaQueue) >= lockedHsaQueue->size) {
      // checkHCCRuntimeStatus(Kalmar::HCCRuntimeStatus::HCCRT_STATUS_ERROR_COMMAND_QUEUE_OVERFLOW, __LINE__, lockedHsaQueue);
    }


    hsa_kernel_dispatch_packet_t* q_aql =
        &(((hsa_kernel_dispatch_packet_t*)(lockedHsaQueue->base_address))[index & queueMask]);

    // Copy mostly-finished AQL packet into the queue
    *q_aql = aql;

    // Set some specific fields:
    if (allocSignal) {
        /*
         * Create a signal to wait for the dispatch to finish.
         */
        std::pair<signal_t, int> ret = getDlaContext()->getSignal();
        _signal = ret.first;
        _signalIndex = ret.second;
        q_aql->completion_signal = _signal;
    } else {
        _signal.handle = 0;
        _signalIndex = -1;
    }

    // Lastly copy in the header:
    q_aql->header = header;

    hsa_queue_store_write_index_relaxed(lockedHsaQueue, index + 1);
    // DBOUTL(DB_AQL, " dispatch_aql " << *this << "(hwq=" << lockedHsaQueue << ") kernargs=" << hostKernargSize << " " << *q_aql );
    // DBOUTL(DB_AQL2, rawAql(*q_aql));
/*
    if (DBFLAG(DB_KERNARG)) {
        printKernarg(q_aql->kernarg_address, hostKernargSize);
    }
*/

    // Ring door bell
    hsa_signal_store_relaxed(lockedHsaQueue->doorbell_signal, index);

    isDispatched = true;

    return status;
}



// wait for the kernel to finish execution
status_t CmdDispatch::waitComplete() {
    status_t status = SUCCESS;
    if (!isDispatched)  {
        return ERROR_INVALID_ARGUMENT;
    }

    if (_signal.handle) {
        DBOUT(DB_MISC, "wait for kernel dispatch op#" << *this  << " completion with wait flag: " << waitMode << "  signal="<< std::hex  << _signal.handle << std::dec << "\n");

        // wait for completion
        if (hsa_signal_wait_scacquire(_signal, HSA_SIGNAL_CONDITION_LT, 1, uint64_t(-1), waitMode)!=0) {
            throw utils::Error("Signal wait returned unexpected value\n");
        }

        DBOUT (DB_MISC, "complete!\n");
    } else {
        // Some commands may have null signal - in this case we can't actually
        // track their status so assume they are complete.
        // In practice, apps would need to use another form of synchronization for
        // these such as waiting on a younger command or using a queue sync.
        DBOUT (DB_MISC, "null signal, considered complete\n");
    }


    // unregister this async operation from HSAQueue
    if (this->hsaQueue() != nullptr) {
        this->hsaQueue()->removeAsyncOp(this);
    }

    isDispatched = false;
    return status;
}

status_t CmdDispatch::dispatchKernelWaitComplete() {
    status_t status = SUCCESS;

    if (isDispatched) {
        return ERROR_INVALID_ARGUMENT;
    }

    // WaitComplete dispatches need to ensure all data is released to system scope
    // This ensures the op is trule "complete" before continuing.
    // This WaitComplete path is used for AMP-style dispatches and may merit future review&optimization.
    aql.header =
        ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
        ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);

    {
        // extract queue_t from HSAQueue
        queue_t* rocrQueue = hsaQueue()->acquireLockedRocrQueue();

        // dispatch kernel
        status = dispatchKernel(rocrQueue, arg_vec.data(), arg_vec.size(), true);
        STATUS_CHECK(status, __LINE__);

        hsaQueue()->releaseLockedRocrQueue();
    }

    // wait for completion
    status = waitComplete();
    STATUS_CHECK(status, __LINE__);

    return status;
}


// Flavor used when launching dispatch with args and signal created by HCC
// (As opposed to the dispatch_hsa_kernel path)
status_t CmdDispatch::dispatchKernelAsyncFromOp()
{
    return dispatchKernelAsync(arg_vec.data(), arg_vec.size(), true);
}

status_t CmdDispatch::dispatchKernelAsync(const void *hostKernarg, int hostKernargSize, bool allocSignal) {


    if (HCC_SERIALIZE_KERNEL & 0x1) {
        hsaQueue()->wait();
    }

    status_t status = SUCCESS;


    // If HCC_OPT_FLUSH=1, we are not flushing to system scope after each command.
    // Set the flag so we remember to do so at next queue::wait() call.
    hsaQueue()->setNextSyncNeedsSysRelease(true);

    {
        // extract queue_t from HSAQueue
        queue_t* rocrQueue = hsaQueue()->acquireLockedRocrQueue();

        // dispatch kernel
        status = dispatchKernel(rocrQueue, hostKernarg, hostKernargSize, allocSignal);
        STATUS_CHECK(status, __LINE__);

        hsaQueue()->releaseLockedRocrQueue();
    }


    // dynamically allocate a std::shared_future<void> object
    future = new std::shared_future<void>(std::async(std::launch::deferred, [&] {
        waitComplete();
    }).share());

    if (HCC_SERIALIZE_KERNEL & 0x2) {
        status = waitComplete();
        STATUS_CHECK(status, __LINE__);
    };


    return status;
}

void CmdDispatch::dispose() {
    // status_t status;
    if (kernargMemory != nullptr) {
      //std::cerr << "op#" << getSeqNum() << " releasing kernal arg buffer index=" << kernargMemoryIndex<< "\n";
      device->releaseKernargBuffer(kernargMemory, kernargMemoryIndex);
      kernargMemory = nullptr;
    }

    clearArgs();
    std::vector<uint8_t>().swap(arg_vec);

    if (HCC_PROFILE & HCC_PROFILE_TRACE) {
        uint64_t start = getBeginTimestamp();
        uint64_t end   = getEndTimestamp();
        //std::string kname = kernel ? (kernel->kernelName + "+++" + kernel->shortKernelName) : "hmm";
        //LOG_PROFILE(this, start, end, "kernel", kname.c_str(), std::hex << "kernel="<< kernel << " " << (kernel? kernel->kernelCodeHandle:0x0) << " aql.kernel_object=" << aql.kernel_object << std::dec);
        LOG_PROFILE(this, start, end, "kernel", getKernelName(), "");
    }
    getDlaContext()->releaseSignal(_signal, _signalIndex);

    if (future != nullptr) {
      delete future;
      future = nullptr;
    }
}

uint64_t CmdDispatch::getBeginTimestamp() {
    hsa_amd_profiling_dispatch_time_t time;
    hsa_amd_profiling_get_dispatch_time(_agent, _signal, &time);
    return time.start;
}

uint64_t CmdDispatch::getEndTimestamp() {
    hsa_amd_profiling_dispatch_time_t time;
    hsa_amd_profiling_get_dispatch_time(_agent, _signal, &time);
    return time.end;
}

void CmdDispatch::overrideAcquireFenceIfNeeded()
{
    if (hsaQueue()->nextKernelNeedsSysAcquire())  {
       DBOUT( DB_CMD2, "  kernel AQL packet adding system-scope acquire\n");
       // Pick up system acquire if needed.
       aql.header |= ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) ;
       hsaQueue()->setNextKernelNeedsSysAcquire(false);
    }
}
/*
status_t CmdDispatch::setLaunchConfiguration(const int dims, size_t *globalDims, size_t *localDims, const int dynamicGroupSize) {
    assert((0 < dims) && (dims <= 3));

//    DBOUT(DB_MISC, "static group segment size: " << kernel->static_group_segment_size
//                   << " dynamic group segment size: " << dynamicGroupSize << "\n");

    // Set group dims
    // for each workgroup dimension, make sure it does not exceed the maximum allowable limit
    const uint16_t* workgroup_max_dim = device->getWorkgroupMaxDim();

    unsigned int workgroup_size[3] = { 1, 1, 1};

    // Check whether the user specified a workgroup size
    if (localDims[0] != 0) {
      for (int i = 0; i < dims; i++) {
        // If user specify a group size that exceeds the device limit
        // throw an error
        if (localDims[i] > workgroup_max_dim[i]) {
          std::stringstream msg;
          msg << "The extent of the tile (" << localDims[i] 
              << ") exceeds the device limit (" << workgroup_max_dim[i] << ").";
          throw utils::Error(msg.str().c_str());
        } else if (localDims[i] > globalDims[i]) {
          std::stringstream msg;
          msg << "The extent of the tile (" << localDims[i] 
              << ") exceeds the compute grid extent (" << globalDims[i] << ").";
          throw utils::Error(msg.str().c_str());
        }
        workgroup_size[i] = localDims[i];
      }
    }
    else {

      constexpr unsigned int recommended_flat_workgroup_size = 64;

      // user didn't specify a workgroup size
      if (dims == 1) {
        workgroup_size[0] = recommended_flat_workgroup_size;
      }
      else if (dims == 2) {

        // compute the group size for the 1st dimension
        for (unsigned int i = 1; ; i<<=1) {
          if (i == recommended_flat_workgroup_size
              || i >= globalDims[0]) {
            workgroup_size[0] = 
              std::min(i, static_cast<unsigned int>(globalDims[0]));
            break;
          }
        }

        // compute the group size for the 2nd dimension
        workgroup_size[1] = recommended_flat_workgroup_size / workgroup_size[0];
      }
      else if (dims == 3) {

        // compute the group size for the 1st dimension
        for (unsigned int i = 1; ; i<<=1) {
          if (i == recommended_flat_workgroup_size
              || i >= globalDims[0]) {
            workgroup_size[0] = 
              std::min(i, static_cast<unsigned int>(globalDims[0]));
            break;
          }
        }

        // compute the group size for the 2nd dimension
        for (unsigned int j = 1; ; j<<=1) {
          unsigned int flat_group_size = workgroup_size[0] * j;
          if (flat_group_size > recommended_flat_workgroup_size) {
            workgroup_size[1] = j >> 1;
            break;
          }
          else if (flat_group_size == recommended_flat_workgroup_size
              || j >= globalDims[1]) {
            workgroup_size[1] = 
              std::min(j, static_cast<unsigned int>(globalDims[1]));
            break;
          }
        }

        // compute the group size for the 3rd dimension
        workgroup_size[2] = recommended_flat_workgroup_size / 
                              (workgroup_size[0] * workgroup_size[1]);
      }
    }

    auto kernel = this->kernel;

    auto calculate_kernel_max_flat_workgroup_size = [&] {
      constexpr unsigned int max_num_vgprs_per_work_item = 256;
      constexpr unsigned int num_work_items_per_simd = 64;
      constexpr unsigned int num_simds_per_cu = 4;
      const unsigned int workitem_vgpr_count = std::max((unsigned int)kernel->workitem_vgpr_count, 1u);
      unsigned int max_flat_group_size = (max_num_vgprs_per_work_item / workitem_vgpr_count) 
                                           * num_work_items_per_simd * num_simds_per_cu;
      return max_flat_group_size;
    };

    auto validate_kernel_flat_group_size = [&] {
      const unsigned int actual_flat_group_size = workgroup_size[0] * workgroup_size[1] * workgroup_size[2];
      const unsigned int max_num_work_items_per_cu = calculate_kernel_max_flat_workgroup_size();
      if (actual_flat_group_size > max_num_work_items_per_cu) {
        std::stringstream msg;
        msg << "The number of work items (" << actual_flat_group_size 
            << ") per work group exceeds the limit (" << max_num_work_items_per_cu << ") of kernel "
            << kernel->kernelName << " .";
        throw utils::Error(msg.str().c_str());
      }
    };
    validate_kernel_flat_group_size();

    memset(&aql, 0, sizeof(aql));

    // Copy info from kernel into AQL packet:
    // bind kernel code
    aql.kernel_object = kernel->kernelCodeHandle;

    aql.group_segment_size   = kernel->static_group_segment_size + dynamicGroupSize;
    aql.private_segment_size = kernel->private_segment_size;

    // Set global dims:
    aql.grid_size_x = globalDims[0];
    aql.grid_size_y = (dims > 1 ) ? globalDims[1] : 1;
    aql.grid_size_z = (dims > 2 ) ? globalDims[2] : 1;

    aql.workgroup_size_x = workgroup_size[0];
    aql.workgroup_size_y = workgroup_size[1];
    aql.workgroup_size_z = workgroup_size[2];

    aql.setup = dims << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;

    aql.header = 0;
    if (HCC_OPT_FLUSH) {
        aql.header = ((HSA_FENCE_SCOPE_AGENT) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
                     ((HSA_FENCE_SCOPE_AGENT) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
        overrideAcquireFenceIfNeeded();
    } else {
        aql.header = ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
                     ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
    }

    return SUCCESS;
}
*/

// ----------------------------------------------------------------------
// member function implementation of CmdBarrier
// ----------------------------------------------------------------------


// TODO - remove hsaQueue parm.
status_t CmdBarrier::enqueueAsync(memory_scope fenceScope) {

    if (fenceScope == system_scope) {
        hsaQueue()->setNextSyncNeedsSysRelease(false);
    };

    if (fenceScope > _acquire_scope) {
        DBOUTL( DB_CMD2, "  marker overriding acquireScope(old:" << _acquire_scope << ") to match fenceScope = " << fenceScope);
        _acquire_scope = fenceScope;
    }

    // set acquire scope:
    unsigned fenceBits = 0;

    switch (_acquire_scope) {
        case no_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_NONE) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE);
            break;
        case accelerator_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_AGENT) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE);
            break;
        case system_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE);
            break;
        default:
            STATUS_CHECK(ERROR_INVALID_ARGUMENT, __LINE__);
    }

    switch (fenceScope) {
        case no_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_NONE) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
            break;
        case accelerator_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_AGENT) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
            break;
        case system_scope:
            fenceBits |= ((HSA_FENCE_SCOPE_SYSTEM) << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
            break;
        default:
            STATUS_CHECK(ERROR_INVALID_ARGUMENT, __LINE__);
    };

    if (isDispatched) {
        STATUS_CHECK(ERROR_INVALID_ARGUMENT, __LINE__);
    }

    // Create a signal to wait for the barrier to finish.
    std::pair<signal_t, int> ret = dynamic_cast<DlaContext*>(getContext())->getSignal();
    _signal = ret.first;
    _signalIndex = ret.second;


    // setup header
    header = HSA_PACKET_TYPE_BARRIER_AND << HSA_PACKET_HEADER_TYPE;
#ifndef AMD_HSA
    // AMD implementation does not require barrier bit on barrier packet and executes a little faster without it set.
    header |= (1 << HSA_PACKET_HEADER_BARRIER);
#endif
    header |= fenceBits;

    {
        queue_t* rocrQueue = hsaQueue()->acquireLockedRocrQueue();

        // Obtain the write index for the command queue
        uint64_t index = hsa_queue_load_write_index_relaxed(rocrQueue);
        const uint32_t queueMask = rocrQueue->size - 1;
        uint64_t nextIndex = index + 1;
        if (nextIndex - hsa_queue_load_read_index_scacquire(rocrQueue) >= rocrQueue->size) {
          // checkHCCRuntimeStatus(Kalmar::HCCRuntimeStatus::HCCRT_STATUS_ERROR_COMMAND_QUEUE_OVERFLOW, __LINE__, rocrQueue);
        }

        // Define the barrier packet to be at the calculated queue index address
        hsa_barrier_and_packet_t* barrier = &(((hsa_barrier_and_packet_t*)(rocrQueue->base_address))[index&queueMask]);
        memset(barrier, 0, sizeof(hsa_barrier_and_packet_t));


        // setup dependent signals
        if ((depCount > 0) && (depCount <= 5)) {
            for (int i = 0; i < depCount; ++i) {
                barrier->dep_signal[i] = *(static_cast <signal_t*> (depAsyncOps[i]->getNativeHandle()));
            }
        }

        barrier->completion_signal = _signal;

        // Set header last:
        barrier->header = header;

        // DBOUTL(DB_AQL, " barrier_aql " << *this << " "<< *barrier );
        // DBOUTL(DB_AQL2, rawAql(*barrier));


        // Increment write index and ring doorbell to dispatch the kernel
        hsa_queue_store_write_index_relaxed(rocrQueue, nextIndex);
        hsa_signal_store_relaxed(rocrQueue->doorbell_signal, index);

        hsaQueue()->releaseLockedRocrQueue();
    }

    isDispatched = true;

    // capture the state of these flags after the barrier executes.
    _barrierNextKernelNeedsSysAcquire = hsaQueue()->nextKernelNeedsSysAcquire();
    _barrierNextSyncNeedsSysRelease   = hsaQueue()->nextSyncNeedsSysRelease();

    // dynamically allocate a std::shared_future<void> object
    future = new std::shared_future<void>(std::async(std::launch::deferred, [&] {
        waitComplete();
    }).share());


    return SUCCESS;
}

// wait for the barrier to complete
status_t CmdBarrier::waitComplete() {
    status_t status = SUCCESS;
    if (!isDispatched)  {
        return ERROR_INVALID_ARGUMENT;
    }

    // DBOUT(DB_WAIT,  "  wait for barrier " << *this << " completion with wait flag: " << waitMode << "  signal="<< std::hex  << _signal.handle << std::dec <<"...\n");

    // Wait on completion signal until the barrier is finished
    hsa_signal_wait_scacquire(_signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, waitMode);


    // unregister this async operation from HSAQueue
    if (this->hsaQueue() != nullptr) {
        this->hsaQueue()->removeAsyncOp(this);
    }

    isDispatched = false;

    return status;
}


static std::string fenceToString(int fenceBits)
{
    switch (fenceBits) {
        case 0: return "none";
        case 1: return "acc";
        case 2: return "sys";
        case 3: return "sys";
        default: return "???";
    };
}

void CmdBarrier::dispose() {
    if ((HCC_PROFILE & HCC_PROFILE_TRACE) && (HCC_PROFILE_VERBOSE & HCC_PROFILE_VERBOSE_BARRIER)) {
        uint64_t start = getBeginTimestamp();
        uint64_t end   = getEndTimestamp();
        int acqBits = extractBits(header, HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCACQUIRE_FENCE_SCOPE);
        int relBits = extractBits(header, HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE, HSA_PACKET_HEADER_WIDTH_SCRELEASE_FENCE_SCOPE);

        std::stringstream depss;
        for (int i=0; i<depCount; i++) {
            if (i==0) {
                depss << " deps=";
            } else {
                depss << ",";
            }
            depss << *depAsyncOps[i];
        };
        LOG_PROFILE(this, start, end, "barrier", "depcnt=" + std::to_string(depCount) + ",acq=" + fenceToString(acqBits) + ",rel=" + fenceToString(relBits), depss.str())
    }
    dynamic_cast<DlaContext*>(getContext())->releaseSignal(_signal, _signalIndex);

    // Release referecne to our dependent ops:
    for (int i=0; i<depCount; i++) {
        depAsyncOps[i] = nullptr;
    }

    if (future != nullptr) {
      delete future;
      future = nullptr;
    }
}

uint64_t CmdBarrier::getBeginTimestamp() {
    hsa_amd_profiling_dispatch_time_t time;
    hsa_amd_profiling_get_dispatch_time(_agent, _signal, &time);
    return time.start;
}

uint64_t CmdBarrier::getEndTimestamp() {
    hsa_amd_profiling_dispatch_time_t time;
    hsa_amd_profiling_get_dispatch_time(_agent, _signal, &time);
    return time.end;
}


// ----------------------------------------------------------------------
// member function implementation of Cmd
// ----------------------------------------------------------------------

CmdOpCoord::CmdOpCoord(DlaQueue *queue) :
        _deviceId(queue->getDev()->get_seqnum()),
        _queueId(queue->getSeqNum())
        {}

Cmd::Cmd(Queue *queue, hcCommandKind commandKind) :
    AsyncOp(queue, commandKind),
    _opCoord(static_cast<DlaQueue*> (queue)),
    _asyncOpsIndex(-1),

    _signalIndex(-1),
    _agent(dynamic_cast<DlaDevice*>(hsaQueue()->getDev())->getAgent())
{
    _signal.handle=0;
    apiStartTick = dynamic_cast<DlaContext*>(getContext())->getSystemTicks();
};

DlaQueue *Cmd::hsaQueue() const 
{ 
    return static_cast<DlaQueue *> (this->getQueue()); 
};

bool Cmd::isReady() {
    bool ready = (hsa_signal_load_scacquire(_signal) == 0);
    if (ready && hsaQueue()) {
        hsaQueue()->removeAsyncOp(this);
    }

    return ready;
}


// ----------------------------------------------------------------------
// member function implementation of CmdCopy
// ----------------------------------------------------------------------
//
// Copy mode will be set later on.
// HSA signals would be waited in HSA_WAIT_STATE_ACTIVE by default for CmdCopy instances
CmdCopy::CmdCopy(Queue *queue, const void* src_, void* dst_, size_t sizeBytes_) : 
    Cmd(queue, hcCommandInvalid),
    isSubmitted(false), isAsync(false), isSingleStepCopy(false), isPeerToPeer(false), 
    future(nullptr), depAsyncOp(nullptr), copyDevice(nullptr), waitMode(HSA_WAIT_STATE_ACTIVE),
    src(src_), dst(dst_),
    sizeBytes(sizeBytes_)
{


    apiStartTick = dynamic_cast<DlaContext*>(getContext())->getSystemTicks();
}

// wait for the async copy to complete
status_t CmdCopy::waitComplete() {
    status_t status = SUCCESS;
    if (!isSubmitted)  {
        return ERROR_INVALID_ARGUMENT;
    }

    // Wait on completion signal until the async copy is finishedS
    /*
    if (DBFLAG(DB_WAIT)) {
        signal_value_t v = -1000;
        if (_signal.handle) {
            hsa_signal_load_scacquire(_signal);
        }
        // DBOUT(DB_WAIT, "  wait for copy op#" << getSeqNum() << " completion with wait flag: " << waitMode << "signal="<< std::hex  << _signal.handle << std::dec <<" currentVal=" << v << "...\n");
    }
    */

    // Wait on completion signal until the async copy is finished
    hsa_signal_wait_scacquire(_signal, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, waitMode);


    // unregister this async operation from HSAQueue
    if (this->hsaQueue() != nullptr) {
        this->hsaQueue()->removeAsyncOp(this);
    }

    isSubmitted = false;

    return status;
}


void checkCopy(const void *s1, const void *s2, size_t sizeBytes)
{
    if (memcmp(s1, s2, sizeBytes) != 0) {
        //throw Kalmar::runtime_exception("HCC_CHECK_COPY mismatch detected", 0);
        throw utils::Error("HCC_CHECK_COPY mismatch detected");
    }
}


// Small wrapper that calls hsa_amd_memory_async_copy.
// HCC knows exactly which copy-engine it wants to perfom the copy and has already made.
status_t CmdCopy::hcc_memory_async_copy(hcCommandKind copyKind, const Device *copyDeviceArg,
                      const AmPointerInfo &dstPtrInfo, const AmPointerInfo &srcPtrInfo, size_t sizeBytes,
                      int depSignalCnt, const signal_t *depSignals,
                      signal_t completion_signal)
{
    this->isSingleStepCopy = true;
    this->copyDevice = copyDeviceArg;
    DlaDevice* dla_device = dynamic_cast<DlaDevice*>(const_cast<Device*>(copyDeviceArg));
    // beautiful...:
    device_t copyAgent = * static_cast<hsa_agent_t*>(dla_device->getDlaAgent());
    status_t status;
    hsa_device_type_t device_type;
    status = hsa_device_get_info(copyAgent, HSA_AGENT_INFO_DEVICE, &device_type);
    if (status != SUCCESS) {
        // throw Kalmar::runtime_exception("invalid copy agent used for hcc_memory_async_copy", status);
        throw utils::Error("invalid copy agent used for hcc_memory_async_copy");
    }
    if (device_type != HSA_DEVICE_TYPE_GPU) {
        // throw Kalmar::runtime_exception("copy agent must be GPU hcc_memory_async_copy", -1);
        throw utils::Error("copy agent must be GPU hcc_memory_async_copy");
    }

    device_t hostAgent = dla_device->getHostAgent();

    /* Determine src and dst pointer passed to ROCR runtime.
     *
     * Pre-condition:
     * - this->dst and this->src must be tracked by AM API implemantation.
     * - dstPtrInfo and srcPtrInfo must be valid.
     */
    void *dstPtr = nullptr;
    void *srcPtr = nullptr;
    uint8_t dir;

    device_t srcAgent, dstAgent;
    switch (copyKind) {
        case hcMemcpyHostToHost:
            srcAgent=hostAgent; dstAgent=hostAgent;

            /* H2H case
             * We expect ROCR runtime to continue use the CPU for host to host
             * copies, and thus must pass host pointers here.
             */
            dstPtr = this->dst;
            srcPtr = const_cast<void*>(this->src);
            // dir = DMA_H2H;
            break;
        case hcMemcpyHostToDevice:
            srcAgent=hostAgent; dstAgent=copyAgent;

            /* H2D case
             * Destination is simply this->dst.
             * Source has to be calculated by adding the offset to the pinned
             * host pointer.
             */
            dstPtr = this->dst;
            srcPtr = reinterpret_cast<unsigned char*>(srcPtrInfo._devicePointer) +
                     (reinterpret_cast<unsigned char*>(const_cast<void*>(this->src)) -
                      reinterpret_cast<unsigned char*>(srcPtrInfo._hostPointer));
            // dir = DMA_H2D;
            break;
        case hcMemcpyDeviceToHost:
            srcAgent=copyAgent; dstAgent=hostAgent;

            /* D2H case
             * Source is simply this->src.
             * Desination has to be calculated by adding the offset to the
             * pinned host pointer.
             */
            dstPtr = reinterpret_cast<unsigned char*>(dstPtrInfo._devicePointer) +
                     (reinterpret_cast<unsigned char*>(this->dst) -
                      reinterpret_cast<unsigned char*>(dstPtrInfo._hostPointer));
            srcPtr = const_cast<void*>(this->src);
            // dir = DMA_D2H;
            break;
        case hcMemcpyDeviceToDevice:
            this->isPeerToPeer = (dstPtrInfo._acc != srcPtrInfo._acc);
            srcAgent=copyAgent; dstAgent=copyAgent;

            /* D2D case
             * Simply pass this->src and this->dst to ROCR runtime.
             */
            dstPtr = this->dst;
            srcPtr = const_cast<void*>(this->src);
            // dir = DMA_D2D;
            break;
        default:
            // throw Kalmar::runtime_exception("bad copyKind in hcc_memory_async_copy", copyKind);
            throw utils::Error("bad copyKind in hcc_memory_async_copy");
    };


    /* ROCR logic to select the copy agent:
     *
     *  Decide which copy agent to use :
     *
     *   1. Pick source agent if src agent is a GPU (regardless of the dst agent).
     *   2. Pick destination agent if src argent is not a GPU, and the dst agent is a GPU.
     *   3. If both src and dst agents are CPUs, launch a CPU thread to perform memcpy. Will wait on host for dependent signals to resolve.
     *
     *    Decide which DMA engine on the copy agent to use :
     *
     *     1.   Use SDMA, if the src agent is a CPU AND dst agent is a GPU.
     *     2.   Use SDMA, if the src agent is a GPU AND dst agent is a CPU.
     *     3.   Launch a Blit kernel if the src agent is a GPU AND dst agent is a GPU.
     */
/*
    DBOUT(DB_AQL, "hsa_amd_memory_async_copy("
                   <<  "dstPtr=" << dstPtr << ",0x" << std::hex << dstAgent.handle
                   << ",srcPtr=" << srcPtr << ",0x" << std::hex << srcAgent.handle
                   << ",sizeBytes=" << std::dec << sizeBytes
                   << ",depSignalCnt=" << depSignalCnt << "," << depSignals << ","
                   << std::hex << completion_signal.handle << "\n" << std::dec);
*/
    status = hsa_amd_memory_async_copy(dstPtr, dstAgent, srcPtr, srcAgent, sizeBytes,
                                       depSignalCnt, depSignals, completion_signal);
    if (status != SUCCESS) {
        // throw Kalmar::runtime_exception("hsa_amd_memory_async_copy error", status);
        throw utils::Error("hsa_amd_memory_async_copy error");
    }



    if (HCC_CHECK_COPY) {
        hsa_signal_wait_scacquire(completion_signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_BLOCKED);
        checkCopy(dstPtr, srcPtr, sizeBytes);
    }

    // Next kernel needs to acquire the result of the copy.
    // This holds true for any copy direction, since host memory can also be cached on this GPU.
    // DBOUT( DB_CMD2, "  copy setNextKernelNeedsSysAcquire(true)\n");
    
    // HSA memory copy requires a system-scope acquire before the next kernel command - set flag here so we remember:
    hsaQueue()->setNextKernelNeedsSysAcquire(true);

    return status;
}



static hcCommandKind resolveMemcpyDirection(bool srcInDeviceMem, bool dstInDeviceMem)
{
    if (!srcInDeviceMem && !dstInDeviceMem) {
        return hcMemcpyHostToHost;
    } else if (!srcInDeviceMem && dstInDeviceMem) {
        return hcMemcpyHostToDevice;
    } else if (srcInDeviceMem && !dstInDeviceMem) {
        return hcMemcpyDeviceToHost;
    } else if (srcInDeviceMem &&  dstInDeviceMem) {
        return hcMemcpyDeviceToDevice;
    } else {
        // Invalid copy copyDir - should never reach here since we cover all 4 possible options above.
        // throw Kalmar::runtime_exception("invalid copy copyDir", 0);
        throw utils::Error("invalid copy copyDir");
    }
}

status_t
CmdCopy::enqueueAsyncCopyCommand(const Device *copyDevice, const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo) {

    status_t status = SUCCESS;


    if (HCC_SERIALIZE_COPY & 0x1) {
        hsaQueue()->wait();
    }

    // Performs an async copy.
    // This routine deals only with "mapped" pointers - see syncCopy for an explanation.

    // enqueue async copy command
    if (isSubmitted) {
        return ERROR_INVALID_ARGUMENT;
    }

    {
        // Create a signal to wait for the async copy command to finish.
        std::pair<signal_t, int> ret = getDlaContext()->getSignal();
        _signal = ret.first;
        _signalIndex = ret.second;


        int depSignalCnt = 0;
        signal_t depSignal = { .handle = 0x0 };
        setCommandKind (resolveMemcpyDirection(srcPtrInfo._isInDeviceMem, dstPtrInfo._isInDeviceMem));

        if (!hsaQueue()->nextSyncNeedsSysRelease()) {
            // DBOUT( DB_CMD2, "  copy launching without adding system release\n");
        }

        auto fenceScope = (hsaQueue()->nextSyncNeedsSysRelease()) ? system_scope : no_scope;

        depAsyncOp = std::static_pointer_cast<Cmd> (hsaQueue()->detectStreamDeps(this->getCommandKind(), this));
        if (depAsyncOp) {
            depSignal = * (static_cast <signal_t*> (depAsyncOp->getNativeHandle()));
        }

        // We need to ensure the copy waits for preceding commands the HCC queue to complete, if those commands exist.
        // The copy has to be set so that it depends on the completion_signal of the youngest command in the queue.
        if (depAsyncOp || fenceScope != no_scope) {
        
            // Normally we can use the input signal to hsa_amd_memory_async_copy to ensure the copy waits for youngest op.
            // However, two cases require special handling:
            //    - the youngest op may not have a completion signal - this is optional for kernel launch commands.
            //    - we may need a system-scope fence. This is true if any kernels have been executed in this queue, or
            //      in streams that we depend on.
            // For both of these cases, we create an additional barrier packet in the source, and attach the desired fence.
            // Then we make the copy depend on the signal written by this command.
            if ((depAsyncOp && depSignal.handle == 0x0) || (fenceScope != no_scope)) {
                // DBOUT( DB_CMD2, "  asyncCopy adding marker for needed dependency or release\n");

                // Set depAsyncOp for use by the async copy below:
                depAsyncOp = std::static_pointer_cast<Cmd> (hsaQueue()->EnqueueMarkerWithDependency(0, nullptr, fenceScope));
                depSignal = * (static_cast <signal_t*> (depAsyncOp->getNativeHandle()));
            };

            depSignalCnt = 1;

            // DBOUT( DB_CMD2, "  asyncCopy sent with dependency on op#" << depAsyncOp->getSeqNum() << " depSignal="<< std::hex  << depSignal.handle << std::dec <<"\n");
        }

// schi TODO        if (DBFLAG(DB_CMD)) {
//            signal_value_t v = hsa_signal_load_scacquire(_signal);
            /*
            DBOUT(DB_CMD,  "  hsa_amd_memory_async_copy launched " << " completionSignal="<< std::hex  << _signal.handle
                      << "  InitSignalValue=" << v << " depSignalCnt=" << depSignalCnt
                      << "  copyAgent=" << copyDevice
                      << "\n");
            */
//        }

        isAsync = true;

        hcc_memory_async_copy(getCommandKind(), copyDevice, dstPtrInfo, srcPtrInfo, sizeBytes, depSignalCnt, depSignalCnt ? &depSignal:NULL, _signal);
    }

    isSubmitted = true;

    STATUS_CHECK(status, __LINE__);

    // dynamically allocate a std::shared_future<void> object
    future = new std::shared_future<void>(std::async(std::launch::deferred, [&] {
        waitComplete();
    }).share());

    if (HCC_SERIALIZE_COPY & 0x2) {
        status = waitComplete();
        STATUS_CHECK(status, __LINE__);
    };

    return status;
}



void CmdCopy::dispose() {

    // clear reference counts for dependent ops.
    depAsyncOp = nullptr;


    // HSA signal may not necessarily be allocated by CmdCopy instance
    // only release the signal if it was really allocated (signalIndex >= 0)
    if (_signalIndex >= 0) {
        if (HCC_PROFILE & HCC_PROFILE_TRACE) {
            // uint64_t start = getBeginTimestamp();
            // uint64_t end   = getEndTimestamp();

            // double bw = (double)(sizeBytes)/(end-start) * (1000.0/1024.0) * (1000.0/1024.0);

            // LOG_PROFILE(this, start, end, "copy", getCopyCommandString(),  "\t" << sizeBytes << " bytes;\t" << sizeBytes/1024.0/1024 << " MB;\t" << bw << " GB/s;");
        }
        getDlaContext()->releaseSignal(_signal, _signalIndex);
    } else {
        if (HCC_PROFILE & HCC_PROFILE_TRACE) {
            //uint64_t start = apiStartTick;
            //uint64_t end   = dynamic_cast<DlaContext*>(getContext())->getSystemTicks();
            //double bw = (double)(sizeBytes)/(end-start) * (1000.0/1024.0) * (1000.0/1024.0);
            // LOG_PROFILE(this, start, end, "copyslo", getCopyCommandString(),  "\t" << sizeBytes << " bytes;\t" << sizeBytes/1024.0/1024 << " MB;\t" << bw << " GB/s;");
        }
    }

    if (future != nullptr) {
        delete future;
        future = nullptr;
    }
}

uint64_t CmdCopy::getBeginTimestamp() {
    hsa_amd_profiling_async_copy_time_t time;
    hsa_amd_profiling_get_async_copy_time(_signal, &time);
    return time.start;
}

uint64_t CmdCopy::getEndTimestamp() {
    hsa_amd_profiling_async_copy_time_t time;
    hsa_amd_profiling_get_async_copy_time(_signal, &time);
    return time.end;
}



void CmdCopy::syncCopyExt(hcCommandKind copyDir, const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo, const DlaDevice *copyDevice, bool forceUnpinnedCopy)
{
    bool srcInTracker = (srcPtrInfo._sizeBytes != 0);
    bool dstInTracker = (dstPtrInfo._sizeBytes != 0);

    // DlaDevice* CopyDevice = dynamic_cast<DlaDevice*>(const_cast<Device*>(copy_device));

// TODO - Clean up code below.
    // Copy already called queue.wait() so there are no dependent signals.
    signal_t depSignal;
    int depSignalCnt = 0;


    if ((copyDevice == nullptr) && (copyDir != hcMemcpyHostToHost) && (copyDir != hcMemcpyDeviceToDevice)) {
        // throw Kalmar::runtime_exception("Null copyDevice can only be used with HostToHost or DeviceToDevice copy", -1);
        throw utils::Error("Null copyDevice can only be used with HostToHost or DeviceToDevice copy");
    }


    // DBOUT(DB_COPY, "hcCommandKind: " << getHcCommandKindString(copyDir) << "\n");

    bool useFastCopy = true;
    switch (copyDir) {
        case hcMemcpyHostToDevice:
            if (!srcInTracker || forceUnpinnedCopy) {
                // DBOUT(DB_COPY,"CmdCopy::syncCopyExt(), invoke UnpinnedCopyEngine::CopyHostToDevice()\n");

                copyDevice->copy_engine[0]->CopyHostToDevice(copyDevice->copy_mode, dst, src, sizeBytes, depSignalCnt ? &depSignal : NULL);
                useFastCopy = false;
            }
            break;


        case hcMemcpyDeviceToHost:
            if (!dstInTracker || forceUnpinnedCopy) {
                // DBOUT(DB_COPY,"CmdCopy::syncCopyExt(), invoke UnpinnedCopyEngine::CopyDeviceToHost()\n");
                UnpinnedCopyEngine::CopyMode d2hCopyMode = copyDevice->copy_mode;
                if (d2hCopyMode == UnpinnedCopyEngine::UseMemcpy) {
                    // override since D2H does not support Memcpy
                    d2hCopyMode = UnpinnedCopyEngine::ChooseBest;
                }
                copyDevice->copy_engine[1]->CopyDeviceToHost(d2hCopyMode, dst, src, sizeBytes, depSignalCnt ? &depSignal : NULL);
                useFastCopy = false;
            };
            break;

        case hcMemcpyHostToHost:
            // DBOUT(DB_COPY,"CmdCopy::syncCopyExt(), invoke memcpy\n");
            // Since this is sync copy, we assume here that the GPU has already drained younger commands.

            // This works for both mapped and unmapped memory:
            memcpy(dst, src, sizeBytes);
            useFastCopy = false;
            break;

        case hcMemcpyDeviceToDevice:
            if (forceUnpinnedCopy) {
                // TODO - is this a same-device copy or a P2P?
                device_t dstAgent = * (static_cast<hsa_agent_t*> (dstPtrInfo._acc->get_hsa_agent()));
                device_t srcAgent = * (static_cast<hsa_agent_t*> (srcPtrInfo._acc->get_hsa_agent()));
                // DBOUT(DB_COPY, "CmdCopy::syncCopyExt() P2P copy by engine forcing use of staging buffers.  copyEngine=" << copyDevice << "\n");

                isPeerToPeer = true;

                // TODO, which staging buffer should we use for this to be optimal?
                copyDevice->copy_engine[1]->CopyPeerToPeer(dst, dstAgent, src, srcAgent, sizeBytes, depSignalCnt ? &depSignal : NULL);

                useFastCopy = false;
            }
            break;

        default:
            // throw Kalmar::runtime_exception("unexpected copy type", SUCCESS);
            throw utils::Error("unexpected copy type");

    };


    if (useFastCopy) {
        // Didn't already handle copy with one of special (slow) cases above, use the standard runtime copy path.

        // DBOUT(DB_COPY, "CmdCopy::syncCopyExt(), useFastCopy=1, fetch and init a HSA signal\n");

        // Get a signal and initialize it:
        std::pair<signal_t, int> ret = getDlaContext()->getSignal();
        _signal = ret.first;
        _signalIndex = ret.second;

        hsa_signal_store_relaxed(_signal, 1);

        // DBOUT(DB_CMD, "CmdCopy::syncCopyExt(), invoke hsa_amd_memory_async_copy()\n");

        if (copyDevice == nullptr) {
            // throw Kalmar::runtime_exception("Null copyDevice reached call to hcc_memory_async_copy", -1);
            throw utils::Error("Null copyDevice reached call to hcc_memory_async_copy");
        }


        status_t hsa_status = hcc_memory_async_copy(copyDir, copyDevice, dstPtrInfo, srcPtrInfo, sizeBytes, depSignalCnt, depSignalCnt ? &depSignal:NULL, _signal);

        if (hsa_status == SUCCESS) {
            // DBOUT(DB_COPY, "CmdCopy::syncCopyExt(), wait for completion...");
            hsa_signal_wait_relaxed(_signal, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, waitMode);

            // DBOUT(DB_COPY,"done!\n");
        } else {
            // DBOUT(DB_COPY, "CmdCopy::syncCopyExt(), hsa_amd_memory_async_copy() returns: 0x" << std::hex << hsa_status << std::dec <<"\n");
            // throw Kalmar::runtime_exception("hsa_amd_memory_async_copy error", hsa_status);
            throw utils::Error("hsa_amd_memory_async_copy error");
        }
    }

    if (HCC_CHECK_COPY) {
        checkCopy(dst, src, sizeBytes);
    }
}



// Performs a copy, potentially through a staging buffer .
// This routine can take mapped or unmapped src and dst pointers.
//    "Mapped" means the pointers are mapped into the address space of the device associated with this HSAQueue.
//     Mapped memory may be physically located on this device, or pinned in the CPU, or on another device (for P2P access).
//     If the memory is not mapped, it can still be copied usign an intermediate staging buffer approach.
//
//     In some cases (ie for array or array_view) we already know the src or dst are mapped, and the *IsMapped parameters
//     allow communicating that information to this function.  *IsMapped=False indicates the map state is unknown,
//     so the functions uses the memory tracker to determine mapped or unmapped and *IsInDeviceMem
//
// The copies are performed host-synchronously - the routine waits until the copy completes before returning.
void
CmdCopy::syncCopy() {

    // DBOUT(DB_COPY, "CmdCopy::syncCopy(" << hsaQueue() << "), src = " << src << ", dst = " << dst << ", sizeBytes = " << sizeBytes << "\n");

    // The tracker stores information on all device memory allocations and all pinned host memory, for the specified device
    // If the memory is not found in the tracker, then it is assumed to be unpinned host memory.
    bool srcInTracker = false;
    bool srcInDeviceMem = false;
    bool dstInTracker = false;
    bool dstInDeviceMem = false;

    accelerator acc;
    AmPointerInfo srcPtrInfo(NULL, NULL, NULL, 0, &acc, 0, 0);
    AmPointerInfo dstPtrInfo(NULL, NULL, NULL, 0, &acc, 0, 0);

    if (am_memtracker_getinfo(&srcPtrInfo, src) == AM_SUCCESS) {
        srcInTracker = true;
        srcInDeviceMem = (srcPtrInfo._isInDeviceMem);
    }  // Else - srcNotMapped=srcInDeviceMem=false

    if (am_memtracker_getinfo(&dstPtrInfo, dst) == AM_SUCCESS) {
        dstInTracker = true;
        dstInDeviceMem = (dstPtrInfo._isInDeviceMem);
    } // Else - dstNotMapped=dstInDeviceMem=false


    DBOUTL(DB_COPY,  " srcInTracker: " << srcInTracker
                  << " srcInDeviceMem: " << srcInDeviceMem
                  << " dstInTracker: " << dstInTracker
                  << " dstInDeviceMem: " << dstInDeviceMem);

    // Resolve default to a specific Kind so we know which algorithm to use:
    setCommandKind (resolveMemcpyDirection(srcInDeviceMem, dstInDeviceMem));

    DlaDevice *copyDevice;
    if (srcInDeviceMem) {  // D2D, H2D
        copyDevice = static_cast<DlaDevice*> (srcPtrInfo._acc->get_dev_ptr());
    }else if (dstInDeviceMem) {  // D2H
        copyDevice = static_cast<DlaDevice*> (dstPtrInfo._acc->get_dev_ptr());
    } else {
        copyDevice = nullptr;  // H2D
    }

    syncCopyExt(getCommandKind(), srcPtrInfo, dstPtrInfo, copyDevice, false);
};


// ----------------------------------------------------------------------
// extern "C" functions
// ----------------------------------------------------------------------

extern "C" void *GetContextImpl() {
  return dynamic_cast<DlaContext*>(getContext());
}


extern "C" void PushArgImpl(void *ker, int idx, size_t sz, const void *v) {
  //std::cerr << "pushing:" << ker << " of size " << sz << "\n";
  CmdDispatch *dispatch =
      reinterpret_cast<CmdDispatch*>(ker);
  void *val = const_cast<void*>(v);
  switch (sz) {
    case sizeof(double):
      dispatch->pushDoubleArg(*reinterpret_cast<double*>(val));
      break;
    case sizeof(short):
      dispatch->pushShortArg(*reinterpret_cast<short*>(val));
      break;
    case sizeof(int):
      dispatch->pushIntArg(*reinterpret_cast<int*>(val));
      //std::cerr << "(int) value = " << *reinterpret_cast<int*>(val) <<"\n";
      break;
    case sizeof(unsigned char):
      dispatch->pushBooleanArg(*reinterpret_cast<unsigned char*>(val));
      break;
    default:
      assert(0 && "Unsupported kernel argument size");
  }
}


extern "C" void PushArgPtrImpl(void *ker, int idx, size_t sz, const void *v) {
  //std::cerr << "pushing:" << ker << " of size " << sz << "\n";
  CmdDispatch *dispatch =
      reinterpret_cast<CmdDispatch*>(ker);
  void *val = const_cast<void*>(v);
  dispatch->pushPointerArg(val);
}


// op printer
std::ostream& operator<<(std::ostream& os, const Cmd & op)
{
     os << "#" << op.opCoord()._deviceId << "." ;
     os << op.opCoord()._queueId << "." ;
     os << op.getSeqNum();
    return os;
}

// TODO;
// - add common HSAAsyncOp for barrier, etc.  '
//   - store queue, completion signal, other common info.

//   - remove hsaqueeu
}
