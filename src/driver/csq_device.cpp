#include "utils/lang/lang.h"
#include "inc/csq.h"
#include "inc/csq_cmd.h"
#include "inc/csq_device.h"
#include "inc/csq_context.h"
#include "inc/kalmar_aligned_alloc.h"


static utils::Debug debug;
#define DBOUTL(db_flag, msg)  debug << msg << "\n";

namespace csq {
pool_iterator::pool_iterator()
{
    _kernarg_memory_pool.handle=(uint64_t)-1;
    _finegrained_system_memory_pool.handle=(uint64_t)-1;
    _local_memory_pool.handle=(uint64_t)-1;
    _coarsegrained_system_memory_pool.handle=(uint64_t)-1;

    _found_kernarg_memory_pool = false;
    _found_finegrained_system_memory_pool = false;
    _found_local_memory_pool = false;
    _found_coarsegrained_system_memory_pool = false;

    _local_memory_pool_size = 0;
}

DlaDevice::DlaDevice(device_t a, hsa_agent_t host, int x_accSeqNum) : 
                                Device(),
                               agent(a), max_tile_static_size(0),
                               queue_size(0), queues(), queues_mutex(),
                               rocrQueues(/*empty*/), rocrQueuesMutex(),
                               ri(),
                               useCoarseGrainedRegion(false),
                               kernargPool(), kernargPoolFlag(), kernargCursor(0), kernargPoolMutex(),
                               path(), description(), hostAgent(host),
                               versionMajor(0), versionMinor(0), accSeqNum(x_accSeqNum), queueSeqNums(0) {
                                // Device(access_type_read_write),
                                // Device(access_type_read_write),
                               // executables(), comment out 
                               // profile(hcAgentProfileNone),
                               // programs(),
    // DBOUT(DB_INIT, "HSADevice::HSADevice()\n");

    status_t status = SUCCESS;

    /// set up path and description
    /// and version information
    {
        char name[64] {0};
        node = 0;
        status = hsa_device_get_info(agent, HSA_AGENT_INFO_NAME, name);
        STATUS_CHECK(status, __LINE__);
        status = hsa_device_get_info(agent, HSA_AGENT_INFO_NODE, &node);
        STATUS_CHECK(status, __LINE__);

        wchar_t path_wchar[128] {0};
        wchar_t description_wchar[128] {0};
        swprintf(path_wchar, 128, L"%s%u", name, node);
        swprintf(description_wchar, 128, L"iluvatar HCS Agent %s, Node %u", name, node);

        path = std::wstring(path_wchar);
        description = std::wstring(description_wchar);

        /*
        if (DBFLAG(DB_INIT)) {
          DBWSTREAM << L"Path: " << path << L"\n";
          DBWSTREAM << L"Description: " << description << L"\n";
        }
        */

        status = hsa_device_get_info(agent, HSA_AGENT_INFO_VERSION_MAJOR, &versionMajor);
        STATUS_CHECK(status, __LINE__);
        status = hsa_device_get_info(agent, HSA_AGENT_INFO_VERSION_MINOR, &versionMinor);
        STATUS_CHECK(status, __LINE__);

        // DBOUT(DB_INIT,"  Version Major: " << versionMajor << " Minor: " << versionMinor << "\n");
    }


    {
        /// Set the queue size to use when creating hsa queues:
        this->queue_size = 0;
        status = hsa_device_get_info(agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &this->queue_size);
        STATUS_CHECK(status, __LINE__);

        // MAX_INFLIGHT_COMMANDS_PER_QUEUE throttles the number of commands that can be in the queue, so no reason
        // to allocate a huge HSA queue - size it to it is large enough to handle the inflight commands.
        this->queue_size = 2*MAX_INFLIGHT_COMMANDS_PER_QUEUE;

        // Check that the queue size is valid, these assumptions are used in hsa_queue_create.
        assert (__builtin_popcount(MAX_INFLIGHT_COMMANDS_PER_QUEUE) == 1); // make sure this is power of 2.
    }

    // TODO we don't support profileing for now 
    // status = hsa_amd_profiling_async_copy_enable(1);
    status = hsa_amd_profiling_async_copy_enable(0);
    STATUS_CHECK(status, __LINE__);



    /// Iterate over memory pool of the device and its host
    status = hsa_amd_agent_iterate_memory_pools(agent, DlaDevice::find_group_memory, &max_tile_static_size);
    STATUS_CHECK(status, __LINE__);

    status = hsa_amd_agent_iterate_memory_pools(agent, &DlaDevice::get_memory_pools, &ri);
    STATUS_CHECK(status, __LINE__);

    status = hsa_amd_agent_iterate_memory_pools(hostAgent, DlaDevice::get_host_pools, &ri);
    STATUS_CHECK(status, __LINE__);

    /// after iterating memory regions, set if we can use coarse grained regions
    bool result = false;
    if (hasHSACoarsegrainedRegion()) {
        result = true;
        // environment variable HCC_HSA_USEHOSTMEMORY may be used to change
        // the default behavior
        char* hsa_behavior = getenv("HCC_HSA_USEHOSTMEMORY");
        if (hsa_behavior != nullptr) {
            if (std::string("ON") == hsa_behavior) {
                result = false;
            }
        }
    }
    useCoarseGrainedRegion = result;

    /// pre-allocate a pool of kernarg buffers in case:
    /// - kernarg region is available
    /// - compile-time macro KERNARG_POOL_SIZE is larger than 0
#if KERNARG_POOL_SIZE > 0
    growKernargBuffer();
#endif

    // Setup AM pool.
    ri._am_memory_pool = (ri._found_local_memory_pool)
                             ? ri._local_memory_pool
                             : ri._finegrained_system_memory_pool;

    ri._am_host_memory_pool = (ri._found_coarsegrained_system_memory_pool)
                                  ? ri._coarsegrained_system_memory_pool
                                  : ri._finegrained_system_memory_pool;

    ri._am_host_coherent_memory_pool = (ri._found_finegrained_system_memory_pool)
                                  ? ri._finegrained_system_memory_pool
                                  : ri._coarsegrained_system_memory_pool;

    /// Query the maximum number of work-items in a workgroup
    status = hsa_device_get_info(agent, HSA_AGENT_INFO_WORKGROUP_MAX_SIZE, &workgroup_max_size);
    STATUS_CHECK(status, __LINE__);

    /// Query the maximum number of work-items in each dimension of a workgroup
    status = hsa_device_get_info(agent, HSA_AGENT_INFO_WORKGROUP_MAX_DIM, &workgroup_max_dim);

    STATUS_CHECK(status, __LINE__);

    /// Get ISA associated with the agent
    // new hcc code remove it: status = hsa_device_get_info(agent, HSA_AGENT_INFO_ISA, &agentISA);
    // STATUS_CHECK(status, __LINE__);

    /// Get the profile of the agent
    profile_t agentProfile;
    status = hsa_device_get_info(agent, HSA_AGENT_INFO_PROFILE, &agentProfile);
    STATUS_CHECK(status, __LINE__);

    /*
    if (agentProfile == HSA_PROFILE_BASE) {
        profile = hcAgentProfileBase;
    } else if (agentProfile == HSA_PROFILE_FULL) {
        profile = hcAgentProfileFull;
    }
    */

    //---
    this->copy_mode = static_cast<UnpinnedCopyEngine::CopyMode> (HCC_UNPINNED_COPY_MODE);
    //Provide an environment variable to select the mode used to perform the copy operaton
    switch (this->copy_mode) {
        case UnpinnedCopyEngine::ChooseBest:    //0
        case UnpinnedCopyEngine::UsePinInPlace: //1
        case UnpinnedCopyEngine::UseStaging:    //2
        case UnpinnedCopyEngine::UseMemcpy:     //3
            break;
        default:
            this->copy_mode = UnpinnedCopyEngine::ChooseBest;
    };

    HCC_H2D_STAGING_THRESHOLD    *= 1024;
    HCC_H2D_PININPLACE_THRESHOLD *= 1024;
    HCC_D2H_PININPLACE_THRESHOLD *= 1024;

    static const size_t stagingSize = HCC_STAGING_BUFFER_SIZE * 1024;

    // FIXME: Disable optimizated data copies on large bar system for now due to stability issues
    //this->cpu_accessible_am = hasAccess(hostAgent, ri._am_memory_pool);
    this->cpu_accessible_am = false;

    // hostPool not used
    // hsa_amd_memory_pool_t hostPool = (getHSAAMHostRegion());
    
    copy_engine[0] = new UnpinnedCopyEngine(agent, hostAgent, stagingSize, 2/*staging buffers*/,
                                            this->cpu_accessible_am,
                                            HCC_H2D_STAGING_THRESHOLD,
                                            HCC_H2D_PININPLACE_THRESHOLD,
                                            HCC_D2H_PININPLACE_THRESHOLD);

    copy_engine[1] = new UnpinnedCopyEngine(agent, hostAgent, stagingSize, 2/*staging Buffers*/,
                                            this->cpu_accessible_am,
                                            HCC_H2D_STAGING_THRESHOLD,
                                            HCC_H2D_PININPLACE_THRESHOLD,
                                            HCC_D2H_PININPLACE_THRESHOLD);

/*
    if (HCC_CHECK_COPY && !this->cpu_accessible_am) {
        throw Kalmar::runtime_exception("HCC_CHECK_COPY can only be used on machines where accelerator memory is visible to CPU (ie large-bar systems)", 0);
    }
*/
    // TODO it will hang, since DlaContext contructor call new Device, but DlaContext don't finish contrution
    // move to csq_context.cpp after new DlaDevice
    // ctx.agentToDeviceMap_.insert(std::pair<uint64_t, DlaDevice*> (agent.handle, this));
    // ((DlaContext*)getContext())->agentToDeviceMap_.insert(std::pair<uint64_t, DlaDevice*> (agent.handle, this));

}

void* DlaDevice::getDlaAgent() {
    return static_cast<void*>(&getAgent());
}

// Creates or steals a rocrQueue and returns it in theif->rocrQueue

void DlaDevice::createOrstealRocrQueue(DlaQueue *thief, queue_priority priority) {
    RocrQueue *foundRQ = nullptr;

    this->rocrQueuesMutex.lock();

    // Allocate a new queue when we are below the HCC_MAX_QUEUES limit
    auto rqSize = rocrQueues[0].size()+rocrQueues[1].size()+rocrQueues[2].size();
    if (rqSize < HCC_MAX_QUEUES) {
        foundRQ = new RocrQueue(agent, this->queue_size, thief, priority);
        rocrQueues[priority].push_back(foundRQ);
        // DBOUT(DB_QUEUE, "Create new rocrQueue=" << foundRQ << " for thief=" << thief << "\n")
    }

    this->rocrQueuesMutex.unlock();

    if (foundRQ != nullptr)
        return;

    // Steal an unused queue when we reaches the limit
    while (!foundRQ) {

        this->rocrQueuesMutex.lock();

        // First make a pass to see if we can find an unused queue
        for (auto rq : rocrQueues[priority]) {
            if (rq->_hccQueue == nullptr) {
                // DBOUT(DB_QUEUE, "Found unused rocrQueue=" << rq << " for thief=" << thief << ".  hwQueue=" << rq->_hwQueue << "\n")
                foundRQ = rq;
                // update the queue pointers to indicate the theft
                foundRQ->assignHccQueue(thief);
                break;
            }
        }

        this->rocrQueuesMutex.unlock();

        if (foundRQ != nullptr) {
            break; // while !foundRQ
        }

        this->rocrQueuesMutex.lock();

        // Second pass, try steal from a ROCR queue associated with an HCC queue, but with no active tasks

        for (auto rq : rocrQueues[priority]) {
            if (rq->_hccQueue != thief)  {
                auto victimHccQueue = rq->_hccQueue;
                // victimHccQueue==nullptr should be detected by above loop.
                std::lock_guard<std::recursive_mutex> l(victimHccQueue->qmutex);
                if (victimHccQueue->isEmpty()) {
                    // DBOUT(DB_LOCK, " ptr:" << this << " lock_guard...\n");

                    assert (victimHccQueue->rocrQueue == rq);  // ensure the link is consistent.
                    victimHccQueue->rocrQueue = nullptr;
                    foundRQ = rq;
                    // update the queue pointers to indicate the theft:
                    foundRQ->assignHccQueue(thief);
                    // DBOUT(DB_QUEUE, "Stole existing rocrQueue=" << rq << " from victimHccQueue=" << victimHccQueue << " to hccQueue=" << thief << "\n")
                    break; // for
                }
            }
        }

        this->rocrQueuesMutex.unlock();

        if (foundRQ != nullptr) {
            break; // while !foundRQ
        }
    }
};


// NOTE: removeRocrQueue should only be called from HSAQueue::dispose
// since there's an assumption on a specific locking sequence
void DlaDevice::removeRocrQueue(RocrQueue *rocrQueue) {

    // queues already locked:
    size_t hccSize = queues.size();

    // rocrQueuesMutex has already been acquired in HSAQueue::dispose

    // a perf optimization to keep the HSA queue if we have more HCC queues that might want it.
    // This defers expensive queue deallocation if an hccQueue that holds an hwQueue is destroyed -
    // keep the hwqueue around until the number of hccQueues drops below the number of hwQueues
    // we have already allocated.
    // TODO: Do we want to track HCC queues independently for each priority?
    // auto rqSize = rocrQueues.size();
    auto rqSize = rocrQueues[0].size()+rocrQueues[1].size()+rocrQueues[2].size();
    if (hccSize < rqSize) {
        queue_priority priority = rocrQueue->_priority;
        auto iter = std::find(rocrQueues[priority].begin(), rocrQueues[priority].end(), rocrQueue);
        assert(iter != rocrQueues[priority].end());
        // Remove the pointer from the list:
        rocrQueues[priority].erase(iter);
/*
        auto iter = std::find(rocrQueues.begin(), rocrQueues.end(), rocrQueue);
        assert(iter != rocrQueues.end());
        // Remove the pointer from the list:
        rocrQueues.erase(iter);
*/
        // DBOUT(DB_QUEUE, "removeRocrQueue-hard: rocrQueue=" << rocrQueue << " hccQueues/rocrQueues=" << hccSize << "/" << rqSize << "\n")
        delete rocrQueue; // this will delete the HSA HW queue.
    }
    else {
        // DBOUT(DB_QUEUE, "removeRocrQueue-soft: rocrQueue=" << rocrQueue << " keep hwQUeue, set _hccQueue link to nullptr"
        //                                                   << " hccQueues/rocrQueues=" << hccSize << "/" << rqSize << "\n");
        rocrQueue->_hccQueue = nullptr; // mark it as available.
    }
};



DlaDevice::~DlaDevice() {
    // DBOUT(DB_INIT, "DlaDevice::~DlaDevice() in\n");

    // release all queues
    queues_mutex.lock();

    for (auto queue_iterator : queues) {
        if (!queue_iterator.expired()) {
            auto queue = queue_iterator.lock();
            queue->dispose();
        }
    }

    queues.clear();
    queues_mutex.unlock();

    // deallocate kernarg buffers in the pool
#if KERNARG_POOL_SIZE > 0
    kernargPoolMutex.lock();

    status_t status = SUCCESS;

    // kernargPool is allocated in batch, KERNARG_POOL_SIZE for each
    // allocation. it is therefore be released also in batch.
    for (unsigned int i = 0; i < kernargPool.size() / KERNARG_POOL_SIZE; ++i) {
        hsa_amd_memory_pool_free(kernargPool[i * KERNARG_POOL_SIZE]);
        STATUS_CHECK(status, __LINE__);
    }

    kernargPool.clear();
    kernargPoolFlag.clear();

    kernargPoolMutex.unlock();
#endif

    // release all data in programs
    /*
    for (auto kernel_iterator : programs) {
        delete kernel_iterator.second;
    }
    programs.clear();
    */

    // release executable
    /*
    for (auto executable_iterator : executables) {
        delete executable_iterator.second;
    }
    executables.clear();
    */

    for (int i=0; i<2; i++) {
        if (copy_engine[i]) {
            delete copy_engine[i];
            copy_engine[i] = NULL;
        }
    }
    // DBOUT(DB_INIT, "HSADevice::~HSADevice() out\n");
}
/*
    bool is_emulated() const override { return false; }
    uint32_t get_version() const { return ((static_cast<unsigned int>(versionMajor) << 16) | versionMinor); }

    bool has_cpu_accessible_am() const override { return cpu_accessible_am; }
*/

    void* DlaDevice::create(size_t count, struct rw_info* key) {
        void *data = nullptr;

        if (!is_unified()) {
            // DBOUT(DB_INIT, "create( <count> " << count << ", <key> " << key << "): use HSA memory allocator\n");
            status_t status = SUCCESS;
            auto am_region = getDlaAMRegion();

            status = hsa_amd_memory_pool_allocate(am_region, count, 0, &data);
            STATUS_CHECK(status, __LINE__);

            device_t* agent = static_cast<hsa_agent_t*>(getDlaAgent());
            status = hsa_amd_agents_allow_access(1, agent, NULL, data);
            STATUS_CHECK(status, __LINE__);
        } else {
            // DBOUT(DB_INIT, "create( <count> " << count << ", <key> " << key << "): use host memory allocator\n");
            data = kalmar_aligned_alloc(0x1000, count);
        }
        return data;
    }

    void DlaDevice::release(void *ptr, struct rw_info* key ) {
        status_t status = SUCCESS;
        if (!is_unified()) {
            // DBOUT(DB_INIT, "release(" << ptr << "," << key << "): use HSA memory deallocator\n");
            status = hsa_amd_memory_pool_free(ptr);
            STATUS_CHECK(status, __LINE__);
        } else {
            // DBOUT(DB_INIT, "release(" << ptr << "," << key << "): use host memory deallocator\n");
            kalmar_aligned_free(ptr);
        }
    }
/*
    // calculate MD5 checksum
    std::string kernel_checksum(size_t size, void* source) {
        // FNV-1a hashing, 64-bit version
        const uint64_t FNV_prime = 0x100000001b3;
        const uint64_t FNV_basis = 0xcbf29ce484222325;
        uint64_t hash = FNV_basis;

        const char *str = static_cast<const char *>(source);
        for (auto i = 0; i < size; ++i) {
            hash ^= *str++;
            hash *= FNV_prime;
        }
        return std::to_string(hash);
    }

    void BuildProgram(void* size, void* source) override {
        if (executables.find(kernel_checksum((size_t)size, source)) == executables.end()) {
            size_t kernel_size = (size_t)((void *)size);
            char *kernel_source = (char*)malloc(kernel_size+1);
            memcpy(kernel_source, source, kernel_size);
            kernel_source[kernel_size] = '\0';
            BuildOfflineFinalizedProgramImpl(kernel_source, kernel_size);
            free(kernel_source);
        }
    }

    inline
    std::string get_isa_name_from_triple(std::string triple)
    {
        static hsa_isa_t tmp{};
        static const bool is_old_rocr{
            hsa_isa_from_name(triple.c_str(), &tmp) != SUCCESS};

        if (is_old_rocr) {
            auto tmp {triple.substr(triple.rfind('x') + 1)};
            triple.replace(0, std::string::npos, "AMD:AMDGPU");

            for (auto&& x : tmp) {
                triple.push_back(':');
                triple.push_back(x);
            }
        }

        return triple;
    }

    bool IsCompatibleKernel(void* size, void* source) override {
        using namespace ELFIO;
        using namespace std;

        status_t status;

        // Allocate memory for kernel source
        size_t kernel_size = (size_t)((void *)size);
        char *kernel_source = (char*)malloc(kernel_size+1);
        memcpy(kernel_source, source, kernel_size);
        kernel_source[kernel_size] = '\0';

        // Set up ELF header reader
        elfio reader;
        istringstream kern_stream{string{
            kernel_source,
            kernel_source + kernel_size}};
        reader.load(kern_stream);

        // Get ISA from ELF header
        std::string triple = "amdgcn-amd-amdhsa--gfx";
        unsigned MACH = reader.get_flags() & hc::EF_AMDGPU_MACH;

        switch(MACH) {
            case hc::EF_AMDGPU_MACH_AMDGCN_GFX701 : triple.append("701"); break;
            case hc::EF_AMDGPU_MACH_AMDGCN_GFX803 : triple.append("803"); break;
            case hc::EF_AMDGPU_MACH_AMDGCN_GFX900 : triple.append("900"); break;
            case hc::EF_AMDGPU_MACH_AMDGCN_GFX906 : triple.append("906"); break;
        }

        const auto isa{get_isa_name_from_triple(std::move(triple))};

        // hsa_isa_t co_isa{};
        struct isa_comp_data {
            hsa_isa_t isa_type;
            bool is_compatible;
        } co_data;
        status = hsa_isa_from_name(isa.c_str(), &co_isa);

        STATUS_CHECK(status, __LINE__);

        // Check if the code object is compatible with ISA of the agent
        // bool isCompatible = false;
        co_data.is_compatible = false;
        status =
            hsa_agent_iterate_isas(
                agent,
                [](hsa_isa_t agent_isa, void* data) {
                    isa_comp_data* co_data = static_cast<isa_comp_data*>(data);
                    if (agent_isa == co_data->isa_type) {
                        co_data->is_compatible = true;
                        return HSA_STATUS_INFO_BREAK;
                    }
                    return SUCCESS;
                },
                &co_data);
        // status = hsa_isa_compatible(co_isa, agentISA, &isCompatible);
        STATUS_CHECK(status, __LINE__);

        // release allocated memory
        free(kernel_source);

        // return isCompatible;
        return co_data.is_compatible;
    }

    void* CreateKernel(const char* fun, Kalmar::KalmarQueue *queue) override {
        // try load kernels lazily in case it was not done so at bootstrap
        // due to HCC_LAZYINIT env var
        if (executables.size() == 0) {
          CLAMP::LoadInMemoryProgram(queue);
        }

        std::string str(fun);
        HSAKernel *kernel = programs[str];

        const char *demangled = "<demangle_error>";
        std::string shortName;

        if (!kernel) {
            int demangleStatus = 0;
            int kernelNameFormat = HCC_DB_SYMBOL_FORMAT & 0xf;
            if (kernelNameFormat == 1) {
                shortName = fun; // mangled name
            } else {
#ifndef USE_LIBCXX
                demangled = abi::__cxa_demangle(fun, nullptr, nullptr, &demangleStatus);
#endif
                shortName = demangleStatus ? fun : std::string(demangled);
                try {
                    if (demangleStatus == 0) {

                        if (kernelNameFormat == 2) {
                            shortName = demangled;
                        } else {
                            // kernelNameFormat == 0 or unspecified:

                            // Example: HIP_kernel_functor_name_begin_unnamed_HIP_kernel_functor_name_end_5::__cxxamp_trampoline(unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, float*, long long)"

                            std::string hip_begin_str  ("::HIP_kernel_functor_name_begin_");
                            std::string hip_end_str    ("_HIP_kernel_functor_name_end");
                            int hip_begin = shortName.find(hip_begin_str);
                            int hip_end   = shortName.find(hip_end_str);

                            if ((hip_begin != -1) && (hip_end != -1) && (hip_end > hip_begin)) {
                                // HIP kernel with markers
                                int start_pos = hip_begin + hip_begin_str.length();
                                std::string hipname = shortName.substr(start_pos, hip_end - start_pos) ;
                                DBOUTL(DB_CODE, "hipname=" << hipname);
                                if (hipname == "unnamed") {
                                    shortName = shortName.substr(0, hip_begin);
                                } else {
                                    shortName = hipname;
                                }

                            } else {
                                // PFE not from HIP:

                                // strip off hip launch template wrapper:
                                std::string hipImplString ("void hip_impl::grid_launch_hip_impl_<");
                                int begin = shortName.find(hipImplString);
                                if ((begin != std::string::npos)) {
                                    begin += hipImplString.length() ;
                                } else {
                                    begin = 0;
                                }

                                shortName = shortName.substr(begin);

                                // Strip off any leading return type:
                                begin = shortName.find(" ", 0);
                                if (begin == std::string::npos) {
                                    begin = 0;
                                } else {
                                    begin +=1; // skip the space
                                }
                                shortName = shortName.substr(begin);

                                DBOUTL(DB_CODE, "shortKernel processing demangled non-hip.  beginChar=" << begin << " shortName=" << shortName);
                            }

                        }

                        if (HCC_DB_SYMBOL_FORMAT & 0x10) {
                            // trim everything after first (
                            int begin = shortName.find("(");
                            shortName = shortName.substr(0, begin);
                        }
                    }
                } catch (std::out_of_range& exception) {
                    // Do something sensible if string pattern is not what we expect
                    shortName = fun;
                };
            }
            DBOUT (DB_CODE, "CreateKernel_short=      " << shortName << "\n");
            DBOUT (DB_CODE, "CreateKernel_demangled=  " << demangled << "\n");
            DBOUT (DB_CODE, "CreateKernel_raw=       " << fun << "\n");

            if (executables.size() != 0) {
                for (auto executable_iterator : executables) {
                    HSAExecutable *executable = executable_iterator.second;

                    // Get symbol handle.
                    status_t status;
                    hsa_executable_symbol_t kernelSymbol;
                    status = hsa_executable_get_symbol_by_name(executable->hsaExecutable, fun, const_cast<device_t*>(&agent), &kernelSymbol);
                    if (status == SUCCESS) {
                        // Get code handle.
                        uint64_t kernelCodeHandle;
                        status = hsa_executable_symbol_get_info(kernelSymbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernelCodeHandle);
                        if (status == SUCCESS) {
                            kernel =  new HSAKernel(str, shortName, executable, kernelSymbol, kernelCodeHandle);
                            break;
                        }
                    }
                }
            }

            if (!kernel) {
                const auto it =
                    shared_object_kernels(hc2::program_state()).find(agent);
                if (it != shared_object_kernels(hc2::program_state()).cend()) {
                    const auto k = std::find_if(
                        it->second.cbegin(),
                        it->second.cend(),
                        [&](hsa_executable_symbol_t x) {
                            return hc2::hsa_symbol_name(x) == str;
                        });
                    if (k != it->second.cend()) {
                        uint64_t h = 0u;
                        if (hsa_executable_symbol_get_info(
                                *k, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &h) ==
                            SUCCESS) {

                            kernel = new HSAKernel(str, shortName, nullptr, *k, h);
                        }
                    }
                }
                if (!kernel) {
                    hc::print_backtrace();
                    std::cerr << "HSADevice::CreateKernel(): Unable to create kernel " << shortName << " \n";
                    std::cerr << "  CreateKernel_raw=  " << fun << "\n";
                    std::cerr << "  CreateKernel_demangled=  " << demangled << "\n";

                    if (demangled) {
                        free((void*)demangled); // cxa_dmangle mallocs memory.
                    }
                    abort();
                }
            } else {
                //std::cerr << "HSADevice::CreateKernel(): Created kernel\n";
            }
            programs[str] = kernel;
        }

        // HSADispatch instance will be deleted in:
        // HSAQueue::LaunchKernel()
        // or it will be created as a shared_ptr<KalmarAsyncOp> in:
        // HSAQueue::LaunchKernelAsync()
        HSADispatch *dispatch = new HSADispatch(this, queue, kernel);
        return dispatch;
    }
*/

std::shared_ptr<Queue> DlaDevice::createQueue(execute_order order, queue_priority priority) {
    auto hsaAv = new DlaQueue(this, agent, order, priority);
    std::shared_ptr<Queue> q =  std::shared_ptr<Queue>(hsaAv);
    queues_mutex.lock();
    queues.push_back(q);
    hsaAv->queueSeqNum = this->queueSeqNums++;
    queues_mutex.unlock();
    return q;
}

std::vector< std::shared_ptr<Queue> > DlaDevice::get_all_queues() {
    std::vector< std::shared_ptr<Queue> > result;
    queues_mutex.lock();
    for (auto queue : queues) {
        if (!queue.expired()) {
            result.push_back(queue.lock());
        }
    }
    queues_mutex.unlock();
    return result;
}


bool DlaDevice::is_peer(const Device* other) {
  status_t status;

  if(!hasHSACoarsegrainedRegion())
      return false;

  auto self_pool = getDlaAMRegion();
  hsa_amd_memory_pool_access_t access;

  device_t* agent = static_cast<hsa_agent_t*>( const_cast<Device *> (other)->getDlaAgent());

  //TODO: CPU acclerator will return NULL currently, return false.
  if(nullptr == agent)
      return false;

  // If the agent's node is the same as the current device then
  // it's the same HSA agent and therefore not a peer
  uint32_t node = 0;
  status = hsa_device_get_info(*agent, HSA_AGENT_INFO_NODE, &node);
  if (status != SUCCESS)
    return false;
  if (node == this->node)
    return false;


  status = hsa_amd_agent_memory_pool_get_info(*agent, self_pool, HSA_AMD_AGENT_MEMORY_POOL_INFO_ACCESS, &access);

  if(SUCCESS != status)
      return false;

  if ((HSA_AMD_MEMORY_POOL_ACCESS_ALLOWED_BY_DEFAULT == access) || (HSA_AMD_MEMORY_POOL_ACCESS_DISALLOWED_BY_DEFAULT == access))
      return true;

  return false;
}

    unsigned int DlaDevice::get_compute_unit_count() {
        device_t agent = getAgent();

        uint32_t compute_unit_count = 0;
        status_t status = hsa_device_get_info(agent, (hsa_agent_info_t)HSA_AMD_AGENT_INFO_COMPUTE_UNIT_COUNT, &compute_unit_count);
        if(status == SUCCESS)
            return compute_unit_count;
        else
            return 0;
    }


    void DlaDevice::releaseKernargBuffer(void* kernargBuffer, int kernargBufferIndex) {
        if ( (KERNARG_POOL_SIZE > 0) && (kernargBufferIndex >= 0) ) {
            kernargPoolMutex.lock();


            // mark the kernarg buffer pointed by kernelBufferIndex as available
            kernargPoolFlag[kernargBufferIndex] = false;

            kernargPoolMutex.unlock();
         } else {
            if (kernargBuffer != nullptr) {
                hsa_amd_memory_pool_free(kernargBuffer);
            }
         }
    }


    void DlaDevice::growKernargBuffer()
    {
        uint8_t * kernargMemory = nullptr;
        // increase kernarg pool on demand by KERNARG_POOL_SIZE
        hsa_amd_memory_pool_t kernarg_region = getHSAKernargRegion();

        status_t status = hsa_amd_memory_pool_allocate(kernarg_region, KERNARG_POOL_SIZE * KERNARG_BUFFER_SIZE, 0, (void**)(&kernargMemory));
        STATUS_CHECK(status, __LINE__);

        status = hsa_amd_agents_allow_access(1, &agent, NULL, kernargMemory);
        STATUS_CHECK(status, __LINE__);

        for (size_t i = 0; i < KERNARG_POOL_SIZE * KERNARG_BUFFER_SIZE; i+=KERNARG_BUFFER_SIZE) {
            kernargPool.push_back(kernargMemory+i);
            kernargPoolFlag.push_back(false);
        };
    }



    std::pair<void*, int> DlaDevice::getKernargBuffer(int size) {
        void* ret = nullptr;
        uint32_t cursor = 0;

        // find an available buffer in the pool in case
        // - kernarg pool is available
        // - requested size is smaller than KERNARG_BUFFER_SIZE
        if ( (KERNARG_POOL_SIZE > 0) && (size <= KERNARG_BUFFER_SIZE) ) {
            kernargPoolMutex.lock();
            cursor = kernargCursor;

            if (kernargPoolFlag[cursor] == false) {
                // the cursor is valid, use it
                ret = kernargPool[cursor];

                // set the kernarg buffer as used
                kernargPoolFlag[cursor] = true;

                // simply move the cursor to the next index
                ++kernargCursor;
                if (kernargCursor == kernargPool.size()) kernargCursor = 0;
            } else {
                // the cursor is not valid, sequentially find the next available slot
                bool found = false;

                uint32_t startingCursor = cursor;
                do {
                    ++cursor;
                    if (cursor == kernargPool.size()) cursor = 0;

                    if (kernargPoolFlag[cursor] == false) {
                        // the cursor is valid, use it
                        ret = kernargPool[cursor];

                        // set the kernarg buffer as used
                        kernargPoolFlag[cursor] = true;

                        // simply move the cursor to the next index
                        kernargCursor = cursor + 1;
                        if (kernargCursor == kernargPool.size()) kernargCursor = 0;

                        // break from the loop
                        found = true;
                        break;
                    }
                } while(cursor != startingCursor); // ensure we at most scan the vector once

                if (found == false) {
                    // status_t status = SUCCESS;

                    // increase kernarg pool on demand by KERNARG_POOL_SIZE
                    // hsa_amd_memory_pool_t kernarg_region = getHSAKernargRegion();

                    // keep track of the size of kernarg pool before increasing it
                    uint32_t oldKernargPoolSize = kernargPool.size();
                    uint32_t oldKernargPoolFlagSize = kernargPoolFlag.size();
                    assert(oldKernargPoolSize == oldKernargPoolFlagSize);


                    growKernargBuffer();
                    assert(kernargPool.size() == oldKernargPoolSize + KERNARG_POOL_SIZE);
                    assert(kernargPoolFlag.size() == oldKernargPoolFlagSize + KERNARG_POOL_SIZE);

                    // set return values, after the pool has been increased

                    // use the first item in the newly allocated pool
                    cursor = oldKernargPoolSize;

                    // access the new item through the newly assigned cursor
                    ret = kernargPool[cursor];

                    // mark the item as used
                    kernargPoolFlag[cursor] = true;

                    // simply move the cursor to the next index
                    kernargCursor = cursor + 1;
                    if (kernargCursor == kernargPool.size()) kernargCursor = 0;

                    found = true;
                }

            }

            kernargPoolMutex.unlock();
            memset (ret, 0x00, KERNARG_BUFFER_SIZE);
        } else {
            // allocate new buffers in case:
            // - the kernarg pool is set at compile-time
            // - requested kernarg buffer size is larger than KERNARG_BUFFER_SIZE
            //

            status_t status = SUCCESS;
            hsa_amd_memory_pool_t kernarg_region = getHSAKernargRegion();

            status = hsa_amd_memory_pool_allocate(kernarg_region, size, 0, &ret);
            STATUS_CHECK(status, __LINE__);

            status = hsa_amd_agents_allow_access(1, &agent, NULL, ret);
            STATUS_CHECK(status, __LINE__);

            DBOUTL(DB_RESOURCE, "Allocating non-pool kernarg buffer size=" << size );

            // set cursor value as -1 to notice the buffer would be deallocated
            // instead of recycled back into the pool
            cursor = -1;
            memset (ret, 0x00, size);
        }
        return std::make_pair(ret, cursor);
    }

// TODO schi getSymbolAddress is not implemened, need to move o executable layer, and implement it
//
    void* DlaDevice::getSymbolAddress(const char* symbolName) {
        // status_t status;

        unsigned long* symbol_ptr = nullptr;
        /*
        if (executables.size() != 0) {
            // iterate through all HSA executables
            for (auto executable_iterator : executables) {
                HSAExecutable *executable = executable_iterator.second;

                // get symbol
                hsa_executable_symbol_t symbol;
                status = hsa_executable_get_symbol_by_name(executable->hsaExecutable, symbolName, const_cast<device_t*>(&agent), &symbol);
                //STATUS_CHECK_SYMBOL(status, symbolName, __LINE__);

                if (status == SUCCESS) {
                    // get address of symbol
                    uint64_t symbol_address;
                    status = hsa_executable_symbol_get_info(symbol,
                                                            HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_ADDRESS,
                                                            &symbol_address);
                    STATUS_CHECK(status, __LINE__);

                    symbol_ptr = (unsigned long*)symbol_address;
                    break;
                }
            }
        } else {
            throw Kalmar::runtime_exception("HSA executable NOT built yet!", 0);
        }
        */

        return symbol_ptr;
    }

// FIXME: return values
// TODO: Need more info about hostptr, is it OS allocated buffer or HSA allocator allocated buffer.
// Or it might be the responsibility of caller? Because for OS allocated buffer, we need to call hsa_amd_memory_lock, otherwise, need to call
// hsa_amd_agents_allow_access. Assume it is HSA allocated buffer.

void DlaDevice::memcpySymbol(void* symbolAddr, void* hostptr, size_t count, size_t offset, enum hcCommandKind kind) {
    status_t status;

    if (executables.size() != 0) {
        // copy data
        if (kind == hcMemcpyHostToDevice) {
            // host -> device
            status = hsa_memory_copy(symbolAddr, (char*)hostptr + offset, count);
            STATUS_CHECK(status, __LINE__);
        } else if (kind == hcMemcpyDeviceToHost) {
            // device -> host
            status = hsa_memory_copy(hostptr, (char*)symbolAddr + offset, count);
            STATUS_CHECK(status, __LINE__);
        }
    } else {
        // throw Kalmar::runtime_exception("HSA executable NOT built yet!", 0);
        utils::Error("HSA executable NOT built yet!");
    }
}


// FIXME: return values

void DlaDevice::memcpySymbol(const char* symbolName, void* hostptr, size_t count, size_t offset, enum hcCommandKind kind) {
    if (executables.size() != 0) {
        unsigned long* symbol_ptr = (unsigned long*)getSymbolAddress(symbolName);
        memcpySymbol(symbol_ptr, hostptr, count, offset, kind);
    } else {
        // throw Kalmar::runtime_exception("HSA executable NOT built yet!", 0);
        utils::Error("HSA executable NOT built yet!");
    }
}


/*
void DlaDevice::BuildOfflineFinalizedProgramImpl(void* kernelBuffer, int kernelSize) {
    using namespace ELFIO;
    using namespace std;

    status_t status;

    string index = kernel_checksum((size_t)kernelSize, kernelBuffer);

    // load HSA program if we haven't done so
    if (executables.find(index) == executables.end()) {
        // Create the executable.
        hsa_executable_t hsaExecutable;
        status = hsa_executable_create_alt(
            HSA_PROFILE_FULL,
            HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT,
            nullptr,
            &hsaExecutable);
        STATUS_CHECK(status, __LINE__);

        // Define the global symbol hc::printf_buffer with the actual address
        status = hsa_executable_agent_global_variable_define(hsaExecutable, agent
                                                          , "_ZN2hc13printf_bufferE"
                                                          , hc::printf_buffer_locked_va);
        STATUS_CHECK(status, __LINE__);

        elfio reader;
        istringstream tmp{string{
            static_cast<char*>(kernelBuffer),
            static_cast<char*>(kernelBuffer) + kernelSize}};
        reader.load(tmp);

        elfio self_reader;
        self_reader.load("/proc/self/exe");

        const auto symtab =
            find_section_if(self_reader, [](const ELFIO::section* x) {
                return x->get_type() == SHT_SYMTAB;
        });

        const auto code_object_dynsym =
            find_section_if(reader, [](const ELFIO::section* x) {
                return x->get_type() == SHT_DYNSYM;
        });

        associate_code_object_symbols_with_host_allocation(
            reader,
            self_reader,
            code_object_dynsym,
            symtab,
            agent,
            hsaExecutable);

        auto code_object_reader = load_code_object_and_freeze_executable(
            kernelBuffer, kernelSize, agent, hsaExecutable);

        if (DBFLAG(DB_INIT)) {
            dumpHSAAgentInfo(agent, "Loading code object ");
        }

        // save everything as an HSAExecutable instance
        executables[index] = new HSAExecutable(
            hsaExecutable, code_object_reader);
    }
}
*/
};  // namespace csl
