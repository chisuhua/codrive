#include "inc/Runtime.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// #include "inc/shared.h"

// #include "core/inc/hsa_ext_interface.h"

// #include "inc/cpu_agent.h"
#include "inc/Agent.h"

#include "inc/MemoryRegion.h"
// #include "inc/topology.h"
// #include "inc/signal.h"

// #include "inc/interrupt_signal.h"

// #include "core/inc/hsa_ext_amd_impl.h"
// #include "core/inc/hsa_api_trace_int.h"
#include "util/os.h"
#include "inc/Platform.h"
#include "CoStream.h"
#include "inc/Device.h"
#include "inc/Platform.h"
// #include "inc/hsa_ven_amd_aqlprofile.h"

#define HSA_VERSION_MAJOR 1
#define HSA_VERSION_MINOR 1

// const char rocrbuildid[] __attribute__((used)) = "ROCR BUILD ID: " STRING(ROCR_BUILD_ID);

// FIXME
// bool g_use_interrupt_wait = true;
bool g_use_interrupt_wait = false;
int g_debug_level;
int g_zfb_support;

Runtime* Runtime::runtime_singleton_ = NULL;

KernelMutex Runtime::bootstrap_lock_;

static bool loaded = true;

static void init_vars_from_env(void)
{
	char *envvar;
	int debug_level;

	g_debug_level = DEBUG_LEVEL_DEFAULT;

	envvar = getenv("DEBUG_LEVEL");
	if (envvar)
	{
		debug_level = atoi(envvar);
		if (debug_level >= DEBUG_LEVEL_ERR &&
			debug_level <= DEBUG_LEVEL_DEBUG)
			g_debug_level = debug_level;
	}

	/* Check whether to support Zero frame buffer */
	envvar = getenv("HSA_ZFB");
	if (envvar)
		zfb_support = atoi(envvar);
}

class RuntimeCleanup {
public:
    ~RuntimeCleanup()
    {
        if (!Runtime::IsOpen()) {
            delete Runtime::runtime_singleton_;
        }

        loaded = false;
    }
};

static RuntimeCleanup cleanup_at_unload_;

status_t Runtime::Acquire()
{
    // Check to see if HSA has been cleaned up (process exit)
    if (!loaded) return ERROR_OUT_OF_RESOURCES;

    ScopedAcquire<KernelMutex> boot(&bootstrap_lock_);

    if (runtime_singleton_ == NULL) {
        runtime_singleton_ = new Runtime();
    }

    if (runtime_singleton_->ref_count_ == INT32_MAX) {
        return ERROR_REFCOUNT_OVERFLOW;
    }

    runtime_singleton_->ref_count_++;
    MAKE_NAMED_SCOPE_GUARD(refGuard, [&]() { runtime_singleton_->ref_count_--; });

    if (runtime_singleton_->ref_count_ == 1) {
        status_t status = runtime_singleton_->Load();

        if (status != SUCCESS) {
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    refGuard.Dismiss();
    return SUCCESS;
}

status_t Runtime::Release()
{
    // Check to see if HSA has been cleaned up (process exit)
    if (!loaded) return SUCCESS;

    ScopedAcquire<KernelMutex> boot(&bootstrap_lock_);

    if (runtime_singleton_ == nullptr) return ERROR_NOT_INITIALIZED;

    if (runtime_singleton_->ref_count_ == 1) {
        // Release all registered memory, then unload backends
        runtime_singleton_->Unload();
    }

    runtime_singleton_->ref_count_--;

    if (runtime_singleton_->ref_count_ == 0) {
        delete runtime_singleton_;
        runtime_singleton_ = nullptr;
    }

    return SUCCESS;
}

bool Runtime::IsOpen()
{
    return (Runtime::runtime_singleton_ != NULL) && (Runtime::runtime_singleton_->ref_count_ != 0);
}

// Register agent information only.  Must not call anything that may use the registered information
// since those tables are incomplete.
void Runtime::RegisterAgent(core::IAgent* agent)
{
    // Record the agent in the node-to-agent reverse lookup table.
    agents_by_node_[agent->node_id()].push_back(agent);

    // Process agent as a cpu or gpu device.
    if (agent->agent_type() == core::IAgent::AgentType::kCpu) {
        cpu_agents_.push_back(agent);

        // Add cpu regions to the system region list.
        for (const core::IMemoryRegion* region : agent->regions()) {
            if (region->fine_grain()) {
                system_regions_fine_.push_back(region);
            } else {
                system_regions_coarse_.push_back(region);
            }
        }

        assert(system_regions_fine_.size() > 0);

        // Init default fine grain system region allocator using fine grain
        // system region of the first discovered CPU agent.
        if (cpu_agents_.size() == 1) {
            // Might need memory pooling to cover allocation that
            // requires less than 4096 bytes.
            system_allocator_ =
                [&](size_t size, size_t alignment,
                    MemoryRegion::AllocateFlags alloc_flags) -> void* {
                assert(alignment <= 4096);
                void* ptr = NULL;
                return (SUCCESS == Runtime::runtime_singleton_->AllocateMemory(system_regions_fine_[0], size, alloc_flags, &ptr))
                    ? ptr
                    : NULL;
            };

            system_deallocator_ =
                [&](void* ptr) { this->FreeMemory(ptr); };

            // BaseShared::SetAllocateAndFree(system_allocator_, system_deallocator_);
        }
    } else if (agent->agent_type() == core::IAgent::AgentType::kGpu) {
        gpu_agents_.push_back(agent);

        gpu_ids_.push_back(agent->node_id());

        // Assign the first discovered gpu agent as blit agent that will provide
        // DMA operation for hsa_memory_copy.
        if (blit_agent_ == NULL) {
            blit_agent_ = agent;
            // FIXME
#if 0
      // Query the start and end address of the SVM address space in this
      // platform.
      if (reinterpret_cast<GpuDeviceInt*>(blit_agent_)->profile() ==
          HSA_PROFILE_BASE) {
        std::vector<const core::IMemoryRegion*>::const_iterator it =
            std::find_if(blit_agent_->regions().begin(),
                         blit_agent_->regions().end(),
                         [](const core::IMemoryRegion* region) {
              return (
                  reinterpret_cast<const hcs::MemoryRegion*>(region)->IsSvm());
            });

        assert(it != blit_agent_->regions().end());

        const hcs::MemoryRegion* svm_region =
            reinterpret_cast<const hcs::MemoryRegion*>(*it);

        start_svm_address_ =
            static_cast<uintptr_t>(svm_region->GetBaseAddress());
        end_svm_address_ = start_svm_address_ + svm_region->GetPhysicalSize();

        // Bind VM fault handler when we detect the first GPU agent.
        // TODO: validate if it works on APU.
        // TODO schi comment out BindVmFaultHandler();
      } else {
        start_svm_address_ = 0;
        end_svm_address_ = os::GetUserModeVirtualMemoryBase() +
                           os::GetUserModeVirtualMemorySize();
      }
#endif
        }
    }
}

void Runtime::DestroyAgents()
{
    agents_by_node_.clear();

    std::for_each(gpu_agents_.begin(), gpu_agents_.end(), DeleteObject());
    gpu_agents_.clear();

    gpu_ids_.clear();

    std::for_each(cpu_agents_.begin(), cpu_agents_.end(), DeleteObject());
    cpu_agents_.clear();

    blit_agent_ = NULL;

    system_regions_fine_.clear();
    system_regions_coarse_.clear();
}

void Runtime::SetLinkCount(size_t num_nodes)
{
    num_nodes_ = num_nodes;
    link_matrix_.resize(num_nodes * num_nodes);
}

void Runtime::RegisterLinkInfo(uint32_t node_id_from, uint32_t node_id_to,
    uint32_t num_hop,
    hsa_amd_memory_pool_link_info_t& link_info)
{
    const uint32_t idx = GetIndexLinkInfo(node_id_from, node_id_to);
    link_matrix_[idx].num_hop = num_hop;
    link_matrix_[idx].info = link_info;

    // Limit the number of hop to 1 since the runtime does not have enough
    // information to share to the user about each hop.
    link_matrix_[idx].num_hop = std::min(link_matrix_[idx].num_hop, 1U);
}

const LinkInfo Runtime::GetLinkInfo(uint32_t node_id_from,
    uint32_t node_id_to)
{
    return (node_id_from != node_id_to)
        ? link_matrix_[GetIndexLinkInfo(node_id_from, node_id_to)]
        : LinkInfo(); // No link.
}

uint32_t Runtime::GetIndexLinkInfo(uint32_t node_id_from, uint32_t node_id_to)
{
    return ((node_id_from * num_nodes_) + node_id_to);
}

status_t Runtime::IterateAgent(status_t (*callback)(core::IAgent* agent,
                                   void* data),
    void* data)
{
    callback_t<decltype(callback)> call(callback);

    std::vector<core::IAgent*>* agent_lists[2] = { &cpu_agents_, &gpu_agents_ };
    for (std::vector<core::IAgent*>* agent_list : agent_lists) {
        for (size_t i = 0; i < agent_list->size(); ++i) {
            core::IAgent* agent =  agent_list->at(i); // Agent::Handle(agent_list->at(i));
            status_t status = call(agent, data);

            if (status != SUCCESS) {
                return status;
            }
        }
    }

    return SUCCESS;
}

status_t Runtime::AllocateMemory(const core::IMemoryRegion* region, size_t size,
    core::IMemoryRegion::AllocateFlags alloc_flags,
    void** address)
{
    ScopedAcquire<KernelMutex> lock(&memory_lock_);
    status_t status = region->Allocate(size, alloc_flags, address);

    // Track the allocation result so that it could be freed properly.
    if (status == SUCCESS) {
        allocation_map_[*address] = AllocationRegion(region, size);
    }

    return status;
}

status_t Runtime::FreeMemory(void* ptr)
{
    if (ptr == nullptr) {
        return SUCCESS;
    }

    const core::IMemoryRegion* region = nullptr;
    size_t size = 0;
    std::unique_ptr<std::vector<AllocationRegion::notifier_t>> notifiers;

    {
        ScopedAcquire<KernelMutex> lock(&memory_lock_);

        std::map<const void*, AllocationRegion>::iterator it = allocation_map_.find(ptr);

        if (it == allocation_map_.end()) {
            assert(false && "Can't find address in allocation map");
            return ERROR_INVALID_ALLOCATION;
        }
        region = it->second.region;
        size = it->second.size;

        // Imported fragments can't be released with FreeMemory.
        if (region == nullptr) {
            assert(false && "Can't release imported memory with free.");
            return ERROR_INVALID_ARGUMENT;
        }

        notifiers = std::move(it->second.notifiers);

        allocation_map_.erase(it);

        // Fast path to avoid doubling lock ops in the common case (no notifiers).
        if (!notifiers) return region->Free(ptr, size);
    }

    // Notifiers can't run while holding the lock or the callback won't be able to manage memory.
    // The memory triggering the notification has already been removed from the memory map so can't
    // be double released during the callback.
    for (auto& notifier : *notifiers) {
        notifier.callback(notifier.ptr, notifier.user_data);
    }

    // Fragment allocator requires protection.
    ScopedAcquire<KernelMutex> lock(&memory_lock_);
    return region->Free(ptr, size);
}

status_t Runtime::RegisterReleaseNotifier(void* ptr, hsa_amd_deallocation_callback_t callback,
    void* user_data)
{
    ScopedAcquire<KernelMutex> lock(&memory_lock_);
    auto mem = allocation_map_.upper_bound(ptr);
    if (mem != allocation_map_.begin()) {
        mem--;

        // No support for imported fragments yet.
        if (mem->second.region == nullptr) return ERROR_INVALID_ALLOCATION;

        if ((mem->first <= ptr) && (ptr < reinterpret_cast<const uint8_t*>(mem->first) + mem->second.size)) {
            auto& notifiers = mem->second.notifiers;
            if (!notifiers) notifiers.reset(new std::vector<AllocationRegion::notifier_t>);
            AllocationRegion::notifier_t notifier = {
                ptr, callback_t<hsa_amd_deallocation_callback_t>(callback), user_data
            };
            notifiers->push_back(notifier);
            return SUCCESS;
        }
    }
    return ERROR_INVALID_ALLOCATION;
}

status_t Runtime::DeregisterReleaseNotifier(void* ptr,
    hsa_amd_deallocation_callback_t callback)
{
    status_t ret = ERROR_INVALID_ARGUMENT;
    ScopedAcquire<KernelMutex> lock(&memory_lock_);
    auto mem = allocation_map_.upper_bound(ptr);
    if (mem != allocation_map_.begin()) {
        mem--;
        if ((mem->first <= ptr) && (ptr < reinterpret_cast<const uint8_t*>(mem->first) + mem->second.size)) {
            auto& notifiers = mem->second.notifiers;
            if (!notifiers) return ERROR_INVALID_ARGUMENT;
            for (size_t i = 0; i < notifiers->size(); i++) {
                if (((*notifiers)[i].ptr == ptr) && ((*notifiers)[i].callback) == callback) {
                    (*notifiers)[i] = std::move((*notifiers)[notifiers->size() - 1]);
                    notifiers->pop_back();
                    i--;
                    ret = SUCCESS;
                }
            }
        }
    }
    return ret;
}

status_t Runtime::CopyMemory(void* dst, const void* src, size_t size)
{
    void* source = const_cast<void*>(src);
    // Choose agents from pointer info
    bool is_src_system = false;
    bool is_dst_system = false;

    core::IAgent* src_agent;
    core::IAgent* dst_agent;
    // Fetch ownership
    const auto& is_system_mem = [&](void* ptr, core::IAgent*& agent, bool& need_lock) {
        hsa_amd_pointer_info_t info;
        uint32_t count;
        core::IAgent** accessible = nullptr;
        MAKE_SCOPE_GUARD([&]() { free(accessible); });
        info.size = sizeof(info);
        status_t err = PtrInfo(ptr, &info, malloc, &count, accessible);
        if (err != SUCCESS)
            throw co_exception(err, "PtrInfo failed in hsa_memory_copy.");
        ptrdiff_t endPtr = (ptrdiff_t)ptr + size;
        if (info.agentBaseAddress <= ptr && endPtr <= (ptrdiff_t)info.agentBaseAddress + info.sizeInBytes) {
            if (info.agentOwner == nullptr) info.agentOwner = accessible[0];
            agent = info.agentOwner; // core::IAgent::Object(info.agentOwner);
            need_lock = false;
            return agent->agent_type() != core::IAgent::AgentType::kGpu;
        } else {
            need_lock = true;
            agent = cpu_agents_[0];
            return true;
        }
    };
    bool src_lock, dst_lock;
    is_src_system = is_system_mem(source, src_agent, src_lock);
    is_dst_system = is_system_mem(dst, dst_agent, dst_lock);
    // CPU-CPU
    if (is_src_system && is_dst_system) {
        memcpy(dst, source, size);
        return SUCCESS;
    }

    // Same GPU
    if (src_agent->node_id() == dst_agent->node_id()) {
        if (dst_agent->agent_type() == core::IAgent::AgentType::kGpu) {
            return dst_agent->DmaCopy(dst, source, size);
        } else if (src_agent->agent_type() == core::IAgent::AgentType::kGpu) {
            return src_agent->DmaCopy(dst, source, size);
        } else {
            status_t err = ERROR_INVALID_AGENT;
            throw co_exception(err, "non system memory copy can't find valid device agent");
        }
    }

    // GPU-CPU
    // Must ensure that system memory is visible to the GPU during the copy.
    const MemoryRegion* system_region = static_cast<const MemoryRegion*>(system_regions_fine_[0]);

    void* gpuPtr = nullptr;
    const auto& locked_copy = [&](void*& ptr, core::IAgent* locking_agent) {
        void* tmp;
        core::IAgent* agent = locking_agent; // ->public_handle();
        status_t err = system_region->Lock(1, &agent, ptr, size, &tmp);
        if (err != SUCCESS) throw co_exception(err, "Lock failed in hsa_memory_copy.");
        gpuPtr = ptr;
        ptr = tmp;
    };

    MAKE_SCOPE_GUARD([&]() {
        if (gpuPtr != nullptr) system_region->Unlock(gpuPtr);
    });

    //FIXME after lock it will use device address, which dma copy hang

    // FIXME if (src_lock) locked_copy(source, dst_agent);
    // FIXME if (dst_lock) locked_copy(dst, src_agent);
    if (is_src_system) return dst_agent->DmaCopy(dst, source, size);
    if (is_dst_system) return src_agent->DmaCopy(dst, source, size);

    /*
  GPU-GPU - functional support, not a performance path.
  
  This goes through system memory because we have to support copying between non-peer GPUs
  and we can't use P2P pointers even if the GPUs are peers.  Because hsa_amd_agents_allow_access
  reqhuires the caller to specify all allowed agents we can't assume that a peer mapped pointer
  would remain mapped for the duration of the copy.
  */
    void* temp = nullptr;
    system_region->Allocate(size, core::IMemoryRegion::AllocateNoFlags, &temp);
    MAKE_SCOPE_GUARD([&]() { system_region->Free(temp, size); });
    status_t err = src_agent->DmaCopy(temp, source, size);
    if (err == SUCCESS) err = dst_agent->DmaCopy(dst, temp, size);
    return err;
}

#if 0
status_t Runtime::CopyMemory(void* dst, const void* src, size_t size, uint8_t dir) {
  assert(dst != NULL && src != NULL && size != 0);

  bool is_src_system = false;
  bool is_dst_system = false;
  const uintptr_t src_uptr = reinterpret_cast<uintptr_t>(src);
  const uintptr_t dst_uptr = reinterpret_cast<uintptr_t>(dst);


  if ((reinterpret_cast<GpuDeviceInt*>(blit_agent_)->profile() ==
       HSA_PROFILE_FULL)) {
    is_src_system = (src_uptr < end_svm_address_);
    is_dst_system = (dst_uptr < end_svm_address_);
  } else {
    is_src_system =
        ((src_uptr < start_svm_address_) || (src_uptr >= end_svm_address_));
    is_dst_system =
        ((dst_uptr < start_svm_address_) || (dst_uptr >= end_svm_address_));

    if ((is_src_system && !is_dst_system) ||
        (!is_src_system && is_dst_system)) {
      // Use staging buffer or pin if either src or dst is gpuvm and the other
      // is system memory allocated via OS or C/C++ allocator.
      return CopyMemoryHostAlloc(dst, src, size, is_dst_system);
    }
  }

  if (is_src_system && is_dst_system) {
    memmove(dst, src, size);
    return SUCCESS;
  }

  // return blit_agent_->DmaCopy(dst, src, size, dir);
  return blit_agent_->DmaCopy(dst, src, size);
}

status_t Runtime::CopyMemoryHostAlloc(void* dst, const void* src,
                                          size_t size, bool dst_malloc) {
  void* usrptr = (dst_malloc) ? dst : const_cast<void*>(src);
  void* agent_ptr = NULL;

  agent_t blit_agent = core::IAgent::Handle(blit_agent_);

  const hcs::MemoryRegion* system_region =
      reinterpret_cast<const hcs::MemoryRegion*>(system_regions_fine_[0]);
  status_t stat =
      system_region->Lock(1, &blit_agent, usrptr, size, &agent_ptr);

  if (stat != SUCCESS) {
    return stat;
  }

  stat = blit_agent_->DmaCopy((dst_malloc) ? agent_ptr : dst,
                              (dst_malloc) ? src : agent_ptr,
                              size);
                              // (dst_malloc) ? DMA_D2H : DMA_H2D);

  system_region->Unlock(usrptr);

  return stat;
}
#endif

status_t Runtime::CopyMemory(void* dst, core::IAgent& dst_agent,
    const void* src, core::IAgent& src_agent,
    size_t size,
    std::vector<signal_t>& dep_signals,
    signal_t completion_signal)
    // core::ISignal& completion_signal)
{
    const bool dst_gpu = (dst_agent.agent_type() == core::IAgent::AgentType::kGpu);
    const bool src_gpu = (src_agent.agent_type() == core::IAgent::AgentType::kGpu);
    if (dst_gpu || src_gpu) {
        core::IAgent& copy_agent = (src_gpu) ? src_agent : dst_agent;
        return copy_agent.DmaCopy(dst, dst_agent, src, src_agent, size, dep_signals,
            completion_signal);
    }

    // For cpu to cpu, fire and forget a copy thread.
    const bool profiling_enabled = (dst_agent.profiling_enabled() || src_agent.profiling_enabled());
    /*
     * FIXME
    std::thread(
        [](void* dst, const void* src, size_t size,
            std::vector<signal_t> dep_signals,
            signal_t completion_signal, bool profiling_enabled) {
            for (signal_t dep : dep_signals) {
                COSTREAM->WaitRelaxed(dep, signal_condition_t::CONDITION_EQ, 0, UINT64_MAX,
                    wait_state_t::BLOCKED);
            }
            memcpy(dst, src, size);
            COSTREAM->SubRelease(completion_signal);
            // completion_signal->SubRelease(1);
        },
        dst, src, size, dep_signals, &completion_signal, profiling_enabled).detach();
        */

    return SUCCESS;
}

status_t Runtime::FillMemory(void* ptr, uint32_t value, size_t count)
{
    // Choose blit agent from pointer info
    hsa_amd_pointer_info_t info;
    uint32_t agent_count;
    core::IAgent** accessible = nullptr;
    info.size = sizeof(info);
    MAKE_SCOPE_GUARD([&]() { free(accessible); });
    status_t err = PtrInfo(ptr, &info, malloc, &agent_count, accessible);
    if (err != SUCCESS) return err;

    ptrdiff_t endPtr = (ptrdiff_t)ptr + count * sizeof(uint32_t);

    // Check for GPU fill
    // Selects GPU fill for SVM and Locked allocations if a GPU address is given and is mapped.
    if (info.agentBaseAddress <= ptr && endPtr <= (ptrdiff_t)info.agentBaseAddress + info.sizeInBytes) {
        core::IAgent* blit_agent = info.agentOwner;
        if (blit_agent->agent_type() != core::IAgent::AgentType::kGpu) {
            blit_agent = nullptr;
            for (uint32_t i = 0; i < agent_count; i++) {
                if (accessible[i]->agent_type() == core::IAgent::AgentType::kGpu) {
                    blit_agent = accessible[i];
                    break;
                }
            }
        }
        if (blit_agent) return blit_agent->DmaFill(ptr, value, count);
    }

    // Host and unmapped SVM addresses copy via host.
    if (info.hostBaseAddress <= ptr && endPtr <= (ptrdiff_t)info.hostBaseAddress + info.sizeInBytes) {
        // if (info.hostBaseAddress <= ptr && endPtr <= (ptrdiff_t)((ptrdiff_t)info.hostBaseAddress + info.sizeInBytes)) {
        memset(ptr, value, count * sizeof(uint32_t));
        return SUCCESS;
    }

    return ERROR_INVALID_ALLOCATION;
}

status_t Runtime::AllowAccess(uint32_t num_agents,
    const core::IAgent** agents, const void* ptr)
{
    const MemoryRegion* hcs_region = NULL;
    size_t alloc_size = 0;

    {
        ScopedAcquire<KernelMutex> lock(&memory_lock_);

        std::map<const void*, AllocationRegion>::const_iterator it = allocation_map_.find(ptr);

        if (it == allocation_map_.end()) {
            return ERROR;
        }

        hcs_region = reinterpret_cast<const MemoryRegion*>(it->second.region);
        alloc_size = it->second.size;
    }

    return hcs_region->AllowAccess(num_agents, const_cast<core::IAgent**>(agents), ptr, alloc_size);
}

status_t Runtime::GetSystemInfo(hsa_system_info_t attribute, void* value)
{
    switch (attribute) {
    case HSA_SYSTEM_INFO_VERSION_MAJOR:
        *((uint16_t*)value) = HSA_VERSION_MAJOR;
        break;
    case HSA_SYSTEM_INFO_VERSION_MINOR:
        *((uint16_t*)value) = HSA_VERSION_MINOR;
        break;
    case HSA_SYSTEM_INFO_TIMESTAMP: {
        HsaClockCounters clocks;
        DEVICE->GetClockCounters(0, &clocks);
        *((uint64_t*)value) = clocks.SystemClockCounter;
        break;
    }
    case HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY: {
        assert(sys_clock_freq_ != 0 && "Use of HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY before HSA "
                                       "initialization completes.");
        *(uint64_t*)value = sys_clock_freq_;
        break;
    }
    case HSA_SYSTEM_INFO_SIGNAL_MAX_WAIT:
        *((uint64_t*)value) = 0xFFFFFFFFFFFFFFFF;
        break;
    case HSA_SYSTEM_INFO_ENDIANNESS:
#if defined(HSA_LITTLE_ENDIAN)
        *((endianness_t*)value) = endianness_t::ENDIANNESS_LITTLE;
#else
        *((endianness_t*)value) = endianness_t::ENDIANNESS_BIG;
#endif
        break;
    case HSA_SYSTEM_INFO_MACHINE_MODEL:
#if defined(HSA_LARGE_MODEL)
        *((hsa_machine_model_t*)value) = HSA_MACHINE_MODEL_LARGE;
#else
        *((hsa_machine_model_t*)value) = HSA_MACHINE_MODEL_SMALL;
#endif
        break;
    case HSA_SYSTEM_INFO_EXTENSIONS: {
        memset(value, 0, sizeof(uint8_t) * 128);

        auto setFlag = [&](uint32_t bit) {
            assert(bit < 128 * 8 && "Extension value exceeds extension bitmask");
            uint index = bit / 8;
            uint subBit = bit % 8;
            ((uint8_t*)value)[index] |= 1 << subBit;
        };
#if 0
      if (hsa_internal_api_table_.finalizer_api.hsa_ext_program_finalize_fn != NULL) {
        setFlag(HSA_EXTENSION_FINALIZER);
      }

      if (hsa_internal_api_table_.image_api.hsa_ext_image_create_fn != NULL) {
        setFlag(HSA_EXTENSION_IMAGES);
      }

      if (os::LibHandle lib = os::LoadLib(kAqlProfileLib)) {
        os::CloseLib(lib);
        setFlag(HSA_EXTENSION_AMD_AQLPROFILE);
      }

      setFlag(HSA_EXTENSION_AMD_PROFILER);
#endif

        break;
    }
    default:
        return ERROR_INVALID_ARGUMENT;
    }
    return SUCCESS;
}

#if 0
status_t Runtime::SetAsyncSignalHandler(signal_t signal,
    signal_condition_t cond,
    signal_value_t value,
    hsa_amd_signal_handler handler,
    void* arg)
{
    // Indicate that this signal is in use.
    if (signal.handle != 0) hsa_signal_handle(signal)->Retain();

    ScopedAcquire<KernelMutex> scope_lock(&async_events_control_.lock);

    // Lazy initializer
    if (async_events_control_.async_events_thread_ == NULL) {
        // Create monitoring thread control signal
        auto err = hsa_signal_create(0, 0, NULL, &async_events_control_.wake);
        if (err != SUCCESS) {
            assert(false && "Asyncronous events control signal creation error.");
            return ERROR_OUT_OF_RESOURCES;
        }
        async_events_.PushBack(async_events_control_.wake, HSA_SIGNAL_CONDITION_NE,
            0, NULL, NULL);

        // Start event monitoring thread
        async_events_control_.exit = false;
        async_events_control_.async_events_thread_ = os::CreateThread(AsyncEventsLoop, NULL);
        if (async_events_control_.async_events_thread_ == NULL) {
            assert(false && "Asyncronous events thread creation error.");
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    new_async_events_.PushBack(signal, cond, value, handler, arg);

    hsa_signal_handle(async_events_control_.wake)->StoreRelease(1);

    return SUCCESS;
}
#endif

status_t Runtime::InteropMap(uint32_t num_agents, core::IAgent** agents,
    int interop_handle, uint32_t flags,
    size_t* size, void** ptr,
    size_t* metadata_size, const void** metadata)
{
    static const int tinyArraySize = 8;
    HsaGraphicsResourceInfo info;

    HSAuint32 short_nodes[tinyArraySize];
    HSAuint32* nodes = short_nodes;
    if (num_agents > tinyArraySize) {
        nodes = new HSAuint32[num_agents];
        if (nodes == NULL) return ERROR_OUT_OF_RESOURCES;
    }
    MAKE_SCOPE_GUARD([&]() {
        if (num_agents > tinyArraySize) delete[] nodes;
    });

    for (uint32_t i = 0; i < num_agents; i++)
        agents[i]->GetInfo((agent_info_t)HSA_AMD_AGENT_INFO_DRIVER_NODE_ID,
            &nodes[i]);

    //if (DeviceRegisterGraphicsHandleToNodes(interop_handle, &info, num_agents,
    //                                        nodes) != SUCCESS)
    //  return ERROR;

    HSAuint64 altAddress;
    HsaMemMapFlags map_flags;
    map_flags.Value = 0;
    map_flags.ui32.PageSize = HSA_PAGE_SIZE_64KB;
    if (DEVICE->MapMemoryToGPUNodes(info.MemoryAddress, info.SizeInBytes,
            &altAddress, map_flags, num_agents,
            nodes)
        != SUCCESS) {
        map_flags.ui32.PageSize = HSA_PAGE_SIZE_4KB;
        if (DEVICE->MapMemoryToGPUNodes(info.MemoryAddress, info.SizeInBytes, &altAddress, map_flags,
                num_agents, nodes)
            != SUCCESS) {
            DEVICE->DeregisterMemory(info.MemoryAddress);
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    if (metadata_size != NULL) *metadata_size = info.MetadataSizeInBytes;
    if (metadata != NULL) *metadata = info.Metadata;

    *size = info.SizeInBytes;
    *ptr = info.MemoryAddress;

    return SUCCESS;
}

#if 0
status_t Runtime::InteropUnmap(void* ptr) {
  if(DeviceUnmapMemoryToGPU(ptr)!=SUCCESS)
    return ERROR_INVALID_ARGUMENT;
  if(DeviceDeregisterMemory(ptr)!=SUCCESS)
    return ERROR_INVALID_ARGUMENT;
  return SUCCESS;
}
#endif

status_t Runtime::PtrInfo(void* ptr, hsa_amd_pointer_info_t* info, void* (*alloc)(size_t),
    uint32_t* num_agents_accessible, core::IAgent** accessible,
    PtrInfoBlockData* block_info)
{
    static_assert(static_cast<int>(HSA_POINTER_UNKNOWN) == static_cast<int>(HSA_EXT_POINTER_TYPE_UNKNOWN),
        "Thunk pointer info mismatch");
    static_assert(static_cast<int>(HSA_POINTER_ALLOCATED) == static_cast<int>(HSA_EXT_POINTER_TYPE_HSA),
        "Thunk pointer info mismatch");
    static_assert(static_cast<int>(HSA_POINTER_REGISTERED_USER) == static_cast<int>(HSA_EXT_POINTER_TYPE_LOCKED),
        "Thunk pointer info mismatch");
    static_assert(static_cast<int>(HSA_POINTER_REGISTERED_GRAPHICS) == static_cast<int>(HSA_EXT_POINTER_TYPE_GRAPHICS),
        "Thunk pointer info mismatch");

    HsaPointerInfo thunkInfo;
    uint32_t* mappedNodes;

    hsa_amd_pointer_info_t retInfo;

    // check output struct has an initialized size.
    if (info->size == 0) return ERROR_INVALID_ARGUMENT;

    bool returnListData = ((alloc != nullptr) && (num_agents_accessible != nullptr) && (accessible != nullptr));

    { // memory_lock protects access to the NMappedNodes array and fragment user data since these may
        // change with calls to memory APIs.
        ScopedAcquire<KernelMutex> lock(&memory_lock_);
        // We don't care if this returns an error code.
        // The type will be HSA_EXT_POINTER_TYPE_UNKNOWN if so.
        auto err = DEVICE->QueryPointerInfo(ptr, &thunkInfo);
        assert(((err == DEVICE_STATUS_SUCCESS) || (thunkInfo.Type == HSA_POINTER_UNKNOWN)) && "Thunk ptr info error and not type HSA_POINTER_UNKNOWN.");

        if (returnListData) {
            assert(thunkInfo.NMappedNodes <= agents_by_node_.size() && "PointerInfo: Thunk returned more than all agents in NMappedNodes.");
            mappedNodes = (uint32_t*)alloca(thunkInfo.NMappedNodes * sizeof(uint32_t));
            memcpy(mappedNodes, thunkInfo.MappedNodes, thunkInfo.NMappedNodes * sizeof(uint32_t));
        }
        retInfo.type = (hsa_amd_pointer_type_t)thunkInfo.Type;
        retInfo.agentBaseAddress = reinterpret_cast<void*>(thunkInfo.GPUAddress);
        retInfo.hostBaseAddress = thunkInfo.CPUAddress;
        retInfo.sizeInBytes = thunkInfo.SizeInBytes;
        retInfo.userData = thunkInfo.UserData;
        if (block_info != nullptr) {
            // Block_info reports the thunk allocation from which we may have suballocated.
            // For locked memory we want to return the host address since hostBaseAddress is used to
            // manipulate locked memory and it is possible that hostBaseAddress is different from
            // agentBaseAddress.
            // For device memory, hostBaseAddress is either equal to agentBaseAddress or is NULL when the
            // CPU does not have access.
            assert((retInfo.hostBaseAddress || retInfo.agentBaseAddress) && "Thunk pointer info returned no base address.");
            block_info->base = (retInfo.hostBaseAddress ? retInfo.hostBaseAddress : retInfo.agentBaseAddress);
            block_info->length = retInfo.sizeInBytes;
        }
        auto fragment = allocation_map_.upper_bound(ptr);
        if (fragment != allocation_map_.begin()) {
            fragment--;
            if ((fragment->first <= ptr) && (ptr < reinterpret_cast<const uint8_t*>(fragment->first) + fragment->second.size)) {
                // agent and host address must match here.  Only lock memory is allowed to have differing
                // addresses but lock memory has type HSA_EXT_POINTER_TYPE_LOCKED and cannot be
                // suballocated.
                retInfo.agentBaseAddress = const_cast<void*>(fragment->first);
                retInfo.hostBaseAddress = retInfo.agentBaseAddress;
                retInfo.sizeInBytes = fragment->second.size;
                retInfo.userData = fragment->second.user_ptr;
            }
        }
    } // end lock scope

    retInfo.size = std::min(info->size, (uint32_t)(sizeof(hsa_amd_pointer_info_t)));

    // IPC and Graphics memory may come from a node that does not have an agent in this process.
    // Ex. ROCR_VISIBLE_DEVICES or peer GPU is not supported by ROCm.
    auto nodeAgents = agents_by_node_.find(thunkInfo.Node);
    if (nodeAgents != agents_by_node_.end()) {
        retInfo.agentOwner = nodeAgents->second[0]; // ->public_handle();
        // FIXME  first agent is always Cpu  , so w use second one
        // retInfo.agentOwner = nodeAgents->second[1]->public_handle();
    } else
        retInfo.agentOwner = nullptr; // .handle = 0;

    // Correct agentOwner for locked memory.  Thunk reports the GPU that owns the
    // alias but users are expecting to see a CPU when the memory is system.
    if (retInfo.type == HSA_EXT_POINTER_TYPE_LOCKED) {
        if ((nodeAgents == agents_by_node_.end()) || (nodeAgents->second[0]->agent_type() != core::IAgent::kCpu)) {
            retInfo.agentOwner = cpu_agents_[0]; // ->public_handle();
        }
    }

    memcpy(info, &retInfo, retInfo.size);

    if (returnListData) {
        uint32_t count = 0;
        for (HSAuint32 i = 0; i < thunkInfo.NMappedNodes; i++) {
            assert(mappedNodes[i] < agents_by_node_.size() && "PointerInfo: Invalid node ID returned from thunk.");
            count += agents_by_node_[mappedNodes[i]].size();
        }

        callback_t<decltype(alloc)> Alloc(alloc);
        accessible = (core::IAgent**)Alloc(sizeof(core::IAgent*) * count);
        if ((accessible) == nullptr) return ERROR_OUT_OF_RESOURCES;
        *num_agents_accessible = count;

        uint32_t index = 0;
        for (HSAuint32 i = 0; i < thunkInfo.NMappedNodes; i++) {
            auto& list = agents_by_node_[mappedNodes[i]];
            for (auto agent : list) {
                (accessible)[index] = agent; // agent->public_handle();
                index++;
            }
        }
    }

    return SUCCESS;
}

status_t Runtime::SetPtrInfoData(void* ptr, void* userptr)
{
    { // Use allocation map if possible to handle fragments.
        ScopedAcquire<KernelMutex> lock(&memory_lock_);
        const auto& it = allocation_map_.find(ptr);
        if (it != allocation_map_.end()) {
            it->second.user_ptr = userptr;
            return SUCCESS;
        }
    }
    // Cover entries not in the allocation map (graphics, lock,...)
    // if (DeviceSetMemoryUserData(ptr, userptr) == SUCCESS)
    //  return SUCCESS;
    return ERROR_INVALID_ARGUMENT;
}

status_t Runtime::IPCCreate(void* ptr, size_t len, hsa_amd_ipc_memory_t* handle)
{
    static_assert(sizeof(hsa_amd_ipc_memory_t) == sizeof(HsaSharedMemoryHandle),
        "Thunk IPC mismatch.");
    // Reject sharing allocations larger than ~8TB due to thunk limitations.
    if (len > 0x7FFFFFFF000ull) return ERROR_INVALID_ARGUMENT;

    // Check for fragment sharing.
    PtrInfoBlockData block;
    hsa_amd_pointer_info_t info;
    info.size = sizeof(info);
    if (PtrInfo(ptr, &info, nullptr, nullptr, nullptr, &block) != SUCCESS)
        return ERROR_INVALID_ARGUMENT;
    if ((info.agentBaseAddress != ptr) || (info.sizeInBytes != len))
        return ERROR_INVALID_ARGUMENT;
    if ((block.base != ptr) || (block.length != len)) {
        if (!isMultipleOf(block.base, 2 * 1024 * 1024)) {
            assert(false && "Fragment's block not aligned to 2MB!");
            return ERROR_INVALID_ARGUMENT;
        }
        if (DEVICE->ShareMemory(block.base, block.length, reinterpret_cast<HsaSharedMemoryHandle*>(handle)) != DEVICE_STATUS_SUCCESS)
            return ERROR_INVALID_ARGUMENT;
        uint32_t offset = (reinterpret_cast<uint8_t*>(ptr) - reinterpret_cast<uint8_t*>(block.base)) / 4096;
        // Holds size in (4K?) pages in thunk handle: Mark as a fragment and denote offset.
        handle->handle[6] |= 0x80000000 | offset;
        // Mark block for IPC.  Prevents reallocation of exported memory.
        ScopedAcquire<KernelMutex> lock(&memory_lock_);
        status_t err = allocation_map_[ptr].region->IPCFragmentExport(ptr);
        assert(err == SUCCESS && "Region inconsistent with address map.");
        return err;
    } else {
        if (DEVICE->ShareMemory(ptr, len, reinterpret_cast<HsaSharedMemoryHandle*>(handle)) != DEVICE_STATUS_SUCCESS)
            return ERROR_INVALID_ARGUMENT;
    }
    return SUCCESS;
}

status_t Runtime::IPCAttach(const hsa_amd_ipc_memory_t* handle, size_t len, uint32_t num_agents,
    core::IAgent** agents, void** mapped_ptr)
{
    static const int tinyArraySize = 8;
    void* importAddress;
    HSAuint64 importSize;
    HSAuint64 altAddress;

    hsa_amd_ipc_memory_t importHandle;
    importHandle = *handle;

    // Extract fragment info
    bool isFragment = false;
    uint32_t fragOffset = 0;
    auto fixFragment = [&]() {
        if (!isFragment) return;
        importAddress = reinterpret_cast<uint8_t*>(importAddress) + fragOffset;
        len = Min(len, importSize - fragOffset);
        ScopedAcquire<KernelMutex> lock(&memory_lock_);
        allocation_map_[importAddress] = AllocationRegion(nullptr, len);
    };

    if ((importHandle.handle[6] & 0x80000000) != 0) {
        isFragment = true;
        fragOffset = (importHandle.handle[6] & 0x1FF) * 4096;
        importHandle.handle[6] &= ~(0x80000000 | 0x1FF);
    }

    if (num_agents == 0) {
        if (DEVICE->RegisterSharedHandle(reinterpret_cast<const HsaSharedMemoryHandle*>(&importHandle),
                &importAddress, &importSize)
            != DEVICE_STATUS_SUCCESS)
            return ERROR_INVALID_ARGUMENT;
        if (DEVICE->MapMemoryToGPU(importAddress, importSize, &altAddress) != DEVICE_STATUS_SUCCESS) {
            DEVICE->DeregisterMemory(importAddress);
            return ERROR_OUT_OF_RESOURCES;
        }
        fixFragment();
        *mapped_ptr = importAddress;
        return SUCCESS;
    }

    HSAuint32* nodes = nullptr;
    if (num_agents > tinyArraySize)
        nodes = new HSAuint32[num_agents];
    else
        nodes = (HSAuint32*)alloca(sizeof(HSAuint32) * num_agents);
    if (nodes == NULL) return ERROR_OUT_OF_RESOURCES;

    MAKE_SCOPE_GUARD([&]() {
        if (num_agents > tinyArraySize) delete[] nodes;
    });

    for (uint32_t i = 0; i < num_agents; i++)
        agents[i]->GetInfo((agent_info_t)HSA_AMD_AGENT_INFO_DRIVER_NODE_ID, &nodes[i]);

    if (DEVICE->RegisterSharedHandleToNodes(
            reinterpret_cast<const HsaSharedMemoryHandle*>(&importHandle), &importAddress,
            &importSize, num_agents, nodes)
        != DEVICE_STATUS_SUCCESS)
        return ERROR_INVALID_ARGUMENT;

    HsaMemMapFlags map_flags;
    map_flags.Value = 0;
    map_flags.ui32.PageSize = HSA_PAGE_SIZE_64KB;
    if (DEVICE->MapMemoryToGPUNodes(importAddress, importSize, &altAddress, map_flags, num_agents,
            nodes)
        != DEVICE_STATUS_SUCCESS) {
        map_flags.ui32.PageSize = HSA_PAGE_SIZE_4KB;
        if (DEVICE->MapMemoryToGPUNodes(importAddress, importSize, &altAddress, map_flags, num_agents,
                nodes)
            != DEVICE_STATUS_SUCCESS) {
            DEVICE->DeregisterMemory(importAddress);
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    fixFragment();
    *mapped_ptr = importAddress;
    return SUCCESS;
}

status_t Runtime::IPCDetach(void* ptr)
{
    { // Handle imported fragments.
        ScopedAcquire<KernelMutex> lock(&memory_lock_);
        const auto& it = allocation_map_.find(ptr);
        if (it != allocation_map_.end()) {
            if (it->second.region != nullptr) return ERROR_INVALID_ARGUMENT;
            allocation_map_.erase(it);
            lock.Release(); // Can't hold memory lock when using pointer info.

            PtrInfoBlockData block;
            hsa_amd_pointer_info_t info;
            info.size = sizeof(info);
            if (PtrInfo(ptr, &info, nullptr, nullptr, nullptr, &block) != SUCCESS)
                return ERROR_INVALID_ARGUMENT;
            ptr = block.base;
        }
    }
    if (DEVICE->UnmapMemoryToGPU(ptr) != DEVICE_STATUS_SUCCESS)
        return ERROR_INVALID_ARGUMENT;
    if (DEVICE->DeregisterMemory(ptr) != DEVICE_STATUS_SUCCESS)
        return ERROR_INVALID_ARGUMENT;
    return SUCCESS;
}

#if 0
void Runtime::AsyncEventsLoop(void*)
{
    auto& async_events_control_ = runtime_singleton_->async_events_control_;
    auto& async_events_ = runtime_singleton_->async_events_;
    auto& new_async_events_ = runtime_singleton_->new_async_events_;

    while (!async_events_control_.exit) {
        // Wait for a signal
        signal_value_t value;

        // TODO
        // uint32_t index = hcs::hsa_amd_signal_wait_any(
        //     uint32_t(async_events_.Size()), &async_events_.signal_[0],
        //     &async_events_.cond_[0], &async_events_.value_[0], uint64_t(-1),
        //     HSA_WAIT_STATE_BLOCKED, &value);

        uint32_t index = COSTREAM->WaitAny(
            uint32_t(async_events_.Size()), &async_events_.signal_[0],
            &async_events_.cond_[0], &async_events_.value_[0], uint64_t(-1),
            HSA_WAIT_STATE_BLOCKED, &value);

        // Reset the control signal
        if (index == 0) {
            hsa_signal_handle(async_events_control_.wake)->StoreRelaxed(0);
        } else if (index != -1) {
            // No error or timout occured, process the handler
            assert(async_events_.handler_[index] != NULL);
            bool keep = async_events_.handler_[index](value, async_events_.arg_[index]);
            if (!keep) {
                hsa_signal_handle(async_events_.signal_[index])->Release();
                async_events_.CopyIndex(index, async_events_.Size() - 1);
                async_events_.PopBack();
            }
        }

        // Check for dead signals
        index = 0;
        while (index != async_events_.Size()) {
            if (!hsa_signal_handle(async_events_.signal_[index])->IsValid()) {
                hsa_signal_handle(async_events_.signal_[index])->Release();
                async_events_.CopyIndex(index, async_events_.Size() - 1);
                async_events_.PopBack();
                continue;
            }
            index++;
        }

        // Insert new signals and find plain functions
        typedef std::pair<void (*)(void*), void*> func_arg_t;
        std::vector<func_arg_t> functions;
        {
            ScopedAcquire<KernelMutex> scope_lock(&async_events_control_.lock);
            for (size_t i = 0; i < new_async_events_.Size(); i++) {
                if (new_async_events_.signal_[i].handle == 0) {
                    functions.push_back(
                        func_arg_t((void (*)(void*))new_async_events_.handler_[i],
                            new_async_events_.arg_[i]));
                    continue;
                }
                async_events_.PushBack(
                    new_async_events_.signal_[i], new_async_events_.cond_[i],
                    new_async_events_.value_[i], new_async_events_.handler_[i],
                    new_async_events_.arg_[i]);
            }
            new_async_events_.Clear();
        }

        // Call plain functions
        for (size_t i = 0; i < functions.size(); i++)
            functions[i].first(functions[i].second);
        functions.clear();
    }

    // Release wait count of all pending signals
    for (size_t i = 1; i < async_events_.Size(); i++)
        hsa_signal_handle(async_events_.signal_[i])->Release();
    async_events_.Clear();

    for (size_t i = 0; i < new_async_events_.Size(); i++)
        hsa_signal_handle(new_async_events_.signal_[i])->Release();
    new_async_events_.Clear();
}

void Runtime::BindVmFaultHandler()
{
    if (core::Ig_use_interrupt_wait && !gpu_agents_.empty()) {
        // Create memory event with manual reset to avoid racing condition
        // with driver in case of multiple concurrent VM faults.
        vm_fault_event_ = core::IInterruptSignal::CreateEvent(HSA_EVENTTYPE_MEMORY, true);

        // Create an interrupt signal object to contain the memory event.
        // This signal object will be registered with the async handler global
        // thread.
        // vm_fault_signal_ = new core::IInterruptSignal(0, vm_fault_event_);
        COSTREAM->signal_create(0, 0, nullptr, 0, &vm_fault_signal_, vm_fault_event_);

        if (!vm_fault_signal_->IsValid() || vm_fault_signal_->EopEvent() == NULL) {
            assert(false && "Failed on creating VM fault signal");
            return;
        }

        SetAsyncSignalHandler(vm_fault_signal_,
            HSA_SIGNAL_CONDITION_NE, 0, VMFaultHandler,
            reinterpret_cast<void*>(vm_fault_signal_));
    }
}

bool Runtime::VMFaultHandler(signal_value_t val, void* arg)
{
    // core::IInterruptSignal* vm_fault_signal = reinterpret_cast<core::InterruptSignal*>(arg);
    signal_t vm_fault_signal = reinterpret_cast<signal_t>(arg);

    assert(vm_fault_signal.handle != NULL);

    if (vm_fault_signal == NULL) {
        return false;
    }

    HsaEvent* vm_fault_event = COSTREAM->EopEvent(vm_fault_signal);

    HsaMemoryAccessFault& fault = vm_fault_event->EventData.EventData.MemoryAccessFault;

    status_t custom_handler_status = ERROR;
    auto system_event_handlers = runtime_singleton_->GetSystemEventHandlers();
    // If custom handler is registered, pack the fault info and call the handler
    if (!system_event_handlers.empty()) {
        hsa_amd_event_t memory_fault_event;
        memory_fault_event.event_type = HSA_AMD_GPU_MEMORY_FAULT_EVENT;
        hsa_amd_gpu_memory_fault_info_t& fault_info = memory_fault_event.memory_fault;

        // Find the faulty agent
        auto it = runtime_singleton_->agents_by_node_.find(fault.NodeId);
        assert(it != runtime_singleton_->agents_by_node_.end() && "Can't find faulty agent.");
        Agent* faulty_agent = it->second.front();
        fault_info.agent = Agent::Handle(faulty_agent);

        fault_info.virtual_address = fault.VirtualAddress;
        fault_info.fault_reason_mask = 0;
        if (fault.Failure.NotPresent == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_PAGE_NOT_PRESENT;
        }
        if (fault.Failure.ReadOnly == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_READ_ONLY;
        }
        if (fault.Failure.NoExecute == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_NX;
        }
        if (fault.Failure.GpuAccess == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_HOST_ONLY;
        }
        if (fault.Failure.Imprecise == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_IMPRECISE;
        }
        if (fault.Failure.ECC == 1 && fault.Failure.ErrorType == 0) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_DRAM_ECC;
        }
        if (fault.Failure.ErrorType == 1) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_SRAM_ECC;
        }
        if (fault.Failure.ErrorType == 2) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_DRAM_ECC;
        }
        if (fault.Failure.ErrorType == 3) {
            fault_info.fault_reason_mask |= HSA_AMD_MEMORY_FAULT_HANG;
        }

        for (auto& callback : system_event_handlers) {
            status_t err = callback.first(&memory_fault_event, callback.second);
            if (err == SUCCESS) custom_handler_status = SUCCESS;
        }
    }

    // No custom VM fault handler registered or it failed.
    if (custom_handler_status != SUCCESS) {
        if (runtime_singleton_->flag().enable_vm_fault_message()) {
            std::string reason = "";
            if (fault.Failure.NotPresent == 1) {
                reason += "Page not present or supervisor privilege";
            } else if (fault.Failure.ReadOnly == 1) {
                reason += "Write access to a read-only page";
            } else if (fault.Failure.NoExecute == 1) {
                reason += "Execute access to a page marked NX";
            } else if (fault.Failure.GpuAccess == 1) {
                reason += "Host access only";
            } else if ((fault.Failure.ECC == 1 && fault.Failure.ErrorType == 0) || fault.Failure.ErrorType == 2) {
                reason += "DRAM ECC failure";
            } else if (fault.Failure.ErrorType == 1) {
                reason += "SRAM ECC failure";
            } else if (fault.Failure.ErrorType == 3) {
                reason += "Generic hang recovery";
            } else {
                reason += "Unknown";
            }

            core::IAgent* faultingAgent = runtime_singleton_->agents_by_node_[fault.NodeId][0];

            fprintf(
                stderr,
                "Memory access fault by GPU node-%u (Agent handle: %p) on address %p%s. Reason: %s.\n",
                fault.NodeId, reinterpret_cast<void*>(faultingAgent->public_handle().handle),
                reinterpret_cast<const void*>(fault.VirtualAddress),
                (fault.Failure.Imprecise == 1) ? "(may not be exact address)" : "", reason.c_str());

#ifndef NDEBUG
            PrintMemoryMapNear(reinterpret_cast<void*>(fault.VirtualAddress));
#endif
        }
        assert(false && "GPU memory access fault.");
        std::abort();
    }
    // No need to keep the signal because we are done.
    return false;
}
#endif

void Runtime::PrintMemoryMapNear(void* ptr)
{
    runtime_singleton_->memory_lock_.Acquire();
    auto it = runtime_singleton_->allocation_map_.upper_bound(ptr);
    for (int i = 0; i < 2; i++) {
        if (it != runtime_singleton_->allocation_map_.begin()) it--;
    }
    fprintf(stderr, "Nearby memory map:\n");
    auto start = it;
    for (int i = 0; i < 3; i++) {
        if (it == runtime_singleton_->allocation_map_.end()) break;
        std::string kind = "Non-HSA";
        if (it->second.region != nullptr) {
            const MemoryRegion* region = static_cast<const MemoryRegion*>(it->second.region);
            if (region->IsSystem())
                kind = "System";
            else if (region->IsLocalMemory())
                kind = "VRAM";
            else if (region->IsScratch())
                kind = "Scratch";
            else if (region->IsLDS())
                kind = "LDS";
        }
        fprintf(stderr, "%p, 0x%lx, %s\n", it->first, it->second.size, kind.c_str());
        it++;
    }
    fprintf(stderr, "\n");
    it = start;
    runtime_singleton_->memory_lock_.Release();
    hsa_amd_pointer_info_t info;
    PtrInfoBlockData block;
    uint32_t count;
    core::IAgent** canAccess;
    info.size = sizeof(info);
    for (int i = 0; i < 3; i++) {
        if (it == runtime_singleton_->allocation_map_.end()) break;
        runtime_singleton_->PtrInfo(const_cast<void*>(it->first), &info, malloc, &count, canAccess,
            &block);
        fprintf(stderr, "PtrInfo:\n\tAddress: %p-%p/%p-%p\n\tSize: 0x%lx\n\tType: %u\n\tOwner: %p\n",
            info.agentBaseAddress, (char*)info.agentBaseAddress + info.sizeInBytes,
            info.hostBaseAddress, (char*)info.hostBaseAddress + info.sizeInBytes, info.sizeInBytes,
            info.type, reinterpret_cast<void*>(info.agentOwner));
        fprintf(stderr, "\tCanAccess: %u\n", count);
        for (int t = 0; t < count; t++)
            fprintf(stderr, "\t\t%p\n", reinterpret_cast<void*>(canAccess[t]));
        fprintf(stderr, "\tIn block: %p, 0x%lx\n", block.base, block.length);
        free(canAccess);
        it++;
    }
}

Runtime::Runtime()
    : blit_agent_(NULL)
    , sys_clock_freq_(0)
    , vm_fault_event_(nullptr)
    , vm_fault_signal_({0})
{
    ref_count_ = 0;
    start_svm_address_ = 0;
#if defined(HSA_LARGE_MODEL)
    end_svm_address_ = UINT64_MAX;
#else
    end_svm_address_ = UINT32_MAX;
#endif
}

status_t Runtime::Load()
{
    flag_.Refresh();

    g_use_interrupt_wait = flag_.enable_interrupt();

    // Setup system clock frequency for the first time.
    if (sys_clock_freq_ == 0) {
        // Cache system clock frequency
        HsaClockCounters clocks;
        DEVICE->GetClockCounters(0, &clocks);
        sys_clock_freq_ = clocks.SystemClockFrequencyHz;
    }

    // call topology
    // the topology will call device load which setup csi interface
    if (!PLATFORM->Load()) {
        return ERROR_OUT_OF_RESOURCES;
    }

    BindVmFaultHandler();

    /* move to runtime.cpp
  loader_ = hcs::hsa::loader::Loader::Create(&loader_context_);
  */
    // Load extensions
    // LoadExtensions();

    // Initialize per GPU scratch, blits, and trap handler
    for (core::IAgent* agent : gpu_agents_) {
        status_t status = reinterpret_cast<GpuAgentInt*>(agent)->PostToolsInit();

        if (status != SUCCESS) {
            return status;
        }
    }

    // Load tools libraries
    // LoadTools();

    return SUCCESS;
}

void Runtime::Unload()
{
    /*
  UnloadTools();
  UnloadExtensions();

  hcs::hsa::loader::Loader::Destroy(loader_);
  loader_ = nullptr;
  */

    std::for_each(gpu_agents_.begin(), gpu_agents_.end(), DeleteObject());
    gpu_agents_.clear();

    // TODO async_events_control_.Shutdown();

    if (vm_fault_signal_.handle != 0) {
        STREAM->signal_destroy(vm_fault_signal_);
        vm_fault_signal_.handle = 0;
    }
    // FIXME core::IInterruptSignal::DestroyEvent(vm_fault_event_);
    vm_fault_event_ = nullptr;

    DestroyAgents();

    // CloseTools();

    // call topology
    PLATFORM->Unload();
}
/*
void Runtime::LoadExtensions() {
// Load finalizer and extension library
#ifdef HSA_LARGE_MODEL
  static const std::string kFinalizerLib[] = {"hsa-ext-finalize64.dll",
                                              "libhsa-ext-finalize64.so.1"};
  static const std::string kImageLib[] = {"hsa-ext-image64.dll",
                                          "libhsa-ext-image64.so.1"};
#else
  static const std::string kFinalizerLib[] = {"hsa-ext-finalize.dll",
                                              "libhsa-ext-finalize.so.1"};
  static const std::string kImageLib[] = {"hsa-ext-image.dll",
                                          "libhsa-ext-image.so.1"};
#endif

  // Update Hsa Api Table with handle of Image extension Apis
  extensions_.LoadFinalizer(kFinalizerLib[os_index(os::current_os)]);
  hsa_api_table_.LinkExts(&extensions_.finalizer_api,
                          core::IHsaApiTable::HSA_EXT_FINALIZER_API_TABLE_ID);

  // Update Hsa Api Table with handle of Finalizer extension Apis
  extensions_.LoadImage(kImageLib[os_index(os::current_os)]);
  hsa_api_table_.LinkExts(&extensions_.image_api,
                          core::IHsaApiTable::HSA_EXT_IMAGE_API_TABLE_ID);
}
*/

// void Runtime::UnloadExtensions() { extensions_.Unload(); }

/*
static std::vector<std::string> parse_tool_names(std::string tool_names) {
  std::vector<std::string> names;
  std::string name = "";
  bool quoted = false;
  while (tool_names.size() != 0) {
    auto index = tool_names.find_first_of(" \"\\");
    if (index == std::string::npos) {
      name += tool_names;
      break;
    }
    switch (tool_names[index]) {
      case ' ': {
        if (!quoted) {
          name += tool_names.substr(0, index);
          tool_names.erase(0, index + 1);
          names.push_back(name);
          name = "";
        } else {
          name += tool_names.substr(0, index + 1);
          tool_names.erase(0, index + 1);
        }
        break;
      }
      case '\"': {
        if (quoted) {
          quoted = false;
          name += tool_names.substr(0, index);
          tool_names.erase(0, index + 1);
          names.push_back(name);
          name = "";
        } else {
          quoted = true;
          tool_names.erase(0, index + 1);
        }
        break;
      }
      case '\\': {
        if (tool_names.size() > index + 1) {
          name += tool_names.substr(0, index) + tool_names[index + 1];
          tool_names.erase(0, index + 2);
        }
        break;
      }
    }  // end switch
  }    // end while

  if (name != "") names.push_back(name);
  return names;
}
*/

/*
void Runtime::LoadTools() {
  typedef bool (*tool_init_t)(::HsaApiTable*, uint64_t, uint64_t,
                              const char* const*);
  typedef Agent* (*tool_wrap_t)(Agent*);
  typedef void (*tool_add_t)(Runtime*);

  // Load tool libs
  std::string tool_names = flag_.tools_lib_names();
  if (tool_names != "") {
    std::vector<std::string> names = parse_tool_names(tool_names);
    std::vector<const char*> failed;
    for (auto& name : names) {
      os::LibHandle tool = os::LoadLib(name);

      if (tool != NULL) {
        tool_libs_.push_back(tool);

        tool_init_t ld;
        ld = (tool_init_t)os::GetExportAddress(tool, "OnLoad");
        if (ld) {
          if (!ld(&hsa_api_table_.hsa_api,
                  hsa_api_table_.hsa_api.version.major_id,
                  failed.size(), &failed[0])) {
            failed.push_back(name.c_str());
            os::CloseLib(tool);
            continue;
          }
        }

        tool_wrap_t wrap;
        wrap = (tool_wrap_t)os::GetExportAddress(tool, "WrapAgent");
        if (wrap) {
          std::vector<core::IAgent*>* agent_lists[2] = {&cpu_agents_,
                                                       &gpu_agents_};
          for (std::vector<core::IAgent*>* agent_list : agent_lists) {
            for (size_t agent_idx = 0; agent_idx < agent_list->size();
                 ++agent_idx) {
              Agent* agent = wrap(agent_list->at(agent_idx));
              if (agent != NULL) {
                assert(agent->IsValid() &&
                       "Agent returned from WrapAgent is not valid");
                agent_list->at(agent_idx) = agent;
              }
            }
          }
        }

        tool_add_t add;
        add = (tool_add_t)os::GetExportAddress(tool, "AddAgent");
        if (add) add(this);
      }
      else {
        if (flag().report_tool_load_failures())
          debug_print("Tool lib \"%s\" failed to load.\n", name.c_str());
      }
    }
  }
}
*/
/*
void Runtime::UnloadTools() {
  typedef void (*tool_unload_t)();
  for (size_t i = tool_libs_.size(); i != 0; i--) {
    tool_unload_t unld;
    unld = (tool_unload_t)os::GetExportAddress(tool_libs_[i - 1], "OnUnload");
    if (unld) unld();
  }

  // Reset API table in case some tool doesn't cleanup properly
  hsa_api_table_.Reset();
}
*/

void Runtime::CloseTools()
{
    // Due to valgrind bug, runtime cannot dlclose extensions see:
    // http://valgrind.org/docs/manual/faq.html#faq.unhelpful
    /*FIXME
  if (!flag_.running_valgrind()) {
    for (auto& lib : tool_libs_) os::CloseLib(lib);
  }
  */
    tool_libs_.clear();
}


#if 0
status_t Runtime::SetCustomSystemEventHandler(hsa_amd_system_event_callback_t callback,
    void* data)
{
    ScopedAcquire<KernelMutex> lock(&system_event_lock_);
    system_event_handlers_.push_back(
        std::make_pair(callback_t<hsa_amd_system_event_callback_t>(callback), data));
    return SUCCESS;
}

std::vector<std::pair<callback_t<hsa_amd_system_event_callback_t>, void*>>
Runtime::GetSystemEventHandlers()
{
    ScopedAcquire<KernelMutex> lock(&system_event_lock_);
    return system_event_handlers_;
}

status_t Runtime::SetInternalQueueCreateNotifier(hsa_amd_runtime_queue_notifier callback,
    void* user_data)
{
    if (internal_queue_create_notifier_) {
        return ERROR;
    } else {
        internal_queue_create_notifier_ = callback;
        internal_queue_create_notifier_user_data_ = user_data;
        return SUCCESS;
    }
}

void Runtime::InternalQueueCreateNotify(const queue_t* queue, core::IAgent* agent)
{
    if (internal_queue_create_notifier_)
        internal_queue_create_notifier_(queue, agent, internal_queue_create_notifier_user_data_);
}
#endif

