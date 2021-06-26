
#include <new>
#include <typeinfo>
#include <exception>
#include <set>
#include <utility>
#include <memory>
#include <map>

#include "inc/platform.h"
#include "inc/agent.h"
#include "inc/cpu_agent.h"
#include "inc/ppu_agent.h"
#include "inc/pps_memory_region.h"
#include "inc/signal.h"
#include "inc/default_signal.h"
#include "inc/interrupt_signal.h"
// #include "inc/ipc_signal.h"
// #include "inc/intercept_queue.h"
// #include "inc/pps_csq_queue.h"
#include "inc/exceptions.h"

template <class T>
struct ValidityError;
template <>
struct ValidityError<core::ISignal*> {
  enum { value = ERROR_INVALID_SIGNAL };
};

template <>
struct ValidityError<core::IAgent*> {
  enum { value = ERROR_INVALID_AGENT };
};

template <>
struct ValidityError<core::IMemoryRegion*> {
  enum { value = ERROR_INVALID_REGION };
};

template <>
struct ValidityError<hcs::MemoryRegion*> {
  enum { value = ERROR_INVALID_REGION };
};

template <>
struct ValidityError<core::IQueue*> {
  enum { value = ERROR_INVALID_QUEUE };
};

template <class T>
struct ValidityError<const T*> {
  enum { value = ValidityError<T*>::value };
};

#define IS_BAD_PTR(ptr)                                          \
  do {                                                           \
    if ((ptr) == NULL) return ERROR_INVALID_ARGUMENT; \
  } while (false)

//    if ((ptr) == NULL || !(ptr)->IsValid())
#define IS_VALID(ptr)                                           \
  do {                                                          \
    if ((ptr) == NULL )                                         \
      return status_t(ValidityError<decltype(ptr)>::value); \
  } while (false)

#define CHECK_ALLOC(ptr)                                         \
  do {                                                           \
    if ((ptr) == NULL) return ERROR_OUT_OF_RESOURCES; \
  } while (false)

#define IS_OPEN()                                     \
  do {                                                \
    if (!core::IRuntime::runtime_singleton_->IsOpen()) \
      return ERROR_NOT_INITIALIZED;        \
  } while (false)

template <class T>
static  bool IsValid(T* ptr) {
  return (ptr == NULL) ? NULL : ptr->IsValid();
}

// namespace hcs {
/*
status_t handleException() {
  try {
    throw;
  } catch (const std::bad_alloc& e) {
    debug_print("HSA exception: BadAlloc\n");
    return ERROR_OUT_OF_RESOURCES;
  } catch (const hsa_exception& e) {
    debug_print("HSA exception: %s\n", e.what());
    return e.error_code();
  } catch (const std::exception& e) {
    debug_print("Unhandled exception: %s\n", e.what());
    assert(false && "Unhandled exception.");
    return ERROR;
  } catch (const std::nested_exception& e) {
    debug_print("Callback threw, forwarding.\n");
    e.rethrow_nested();
    return ERROR;
  } catch (...) {
    assert(false && "Unhandled exception.");
    abort();
    return ERROR;
  }
}

template <class T> static  T handleExceptionT() {
  handleException();
  abort();
  return T();
}
*/

status_t hsa_amd_coherency_get_type(device_t agent_handle, hsa_amd_coherency_type_t* type) {

  IS_OPEN();

  const core::IAgent* agent = core::Agent::Object(agent_handle);

  IS_VALID(agent);

  IS_BAD_PTR(type);

  if (agent->device_type() != core::IAgent::kGpu) {
    return ERROR_INVALID_AGENT;
  }

  const GpuDeviceInt* gpu_agent =
      static_cast<const GpuDeviceInt*>(agent);

  *type = gpu_agent->current_coherency_type();

  return SUCCESS;

}

status_t hsa_amd_coherency_set_type(device_t agent_handle,
                                        hsa_amd_coherency_type_t type) {

  IS_OPEN();

  core::IAgent* agent = core::Agent::Object(agent_handle);

  IS_VALID(agent);

  if (type < HSA_AMD_COHERENCY_TYPE_COHERENT ||
      type > HSA_AMD_COHERENCY_TYPE_NONCOHERENT) {
    return ERROR_INVALID_ARGUMENT;
  }

  if (agent->device_type() != core::IAgent::kGpu) {
    return ERROR_INVALID_AGENT;
  }

  GpuDevice* gpu_agent = static_cast<hcs::GpuDevice*>(agent);

  if (!gpu_agent->current_coherency_type(type)) {
    return ERROR;
  }

  return SUCCESS;

}

/*
status_t hsa_amd_memory_fill(void* ptr, uint32_t value, size_t count) {

  IS_OPEN();

  if (ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  if (count == 0) {
    return SUCCESS;
  }

  return core::IRuntime::runtime_singleton_->FillMemory(ptr, value, count);

}
*/

status_t hsa_amd_memory_async_copy(void* dst, device_t dst_agent_handle, const void* src,
                                       device_t src_agent_handle, size_t size, 
                                       uint32_t num_dep_signals, const signal_t* dep_signals,
                                       signal_t completion_signal) {

  if (dst == NULL || src == NULL) { return ERROR_INVALID_ARGUMENT; }

  if ((num_dep_signals == 0 && dep_signals != NULL) ||
      (num_dep_signals > 0 && dep_signals == NULL)) {
    return ERROR_INVALID_ARGUMENT;
  }

  core::IAgent* dst_agent = core::Agent::Object(dst_agent_handle);
  IS_VALID(dst_agent);

  core::IAgent* src_agent = core::Agent::Object(src_agent_handle);
  IS_VALID(src_agent);

  std::vector<core::ISignal*> dep_signal_list(num_dep_signals);
  if (num_dep_signals > 0) {
    for (size_t i = 0; i < num_dep_signals; ++i) {
      core::ISignal* dep_signal_obj = core::Signal::Object(dep_signals[i]);
      IS_VALID(dep_signal_obj);
      dep_signal_list[i] = dep_signal_obj;
    }
  }

  core::ISignal* out_signal_obj = core::Signal::Object(completion_signal);
  IS_VALID(out_signal_obj);

  if (size > 0) {
    return core::IRuntime::runtime_singleton_->CopyMemory(
        dst, *dst_agent, src, *src_agent, size, dep_signal_list,
        *out_signal_obj);
  }

  return SUCCESS;

}

status_t hsa_amd_profiling_set_profiler_enabled(queue_t* queue, int enable) {

  IS_OPEN();

  core::IQueue* cmd_queue = core::Queue::Object(queue);
  IS_VALID(cmd_queue);

  cmd_queue->SetProfiling(enable);

  return SUCCESS;

}

status_t hsa_amd_profiling_async_copy_enable(bool enable) {

  IS_OPEN();

  return core::IRuntime::runtime_singleton_->IterateAgent(
      [](device_t agent_handle, void* data) -> status_t {
        const bool enable = *(reinterpret_cast<bool*>(data));
        return core::IAgent::Object(agent_handle)->profiling_enabled(enable);
      },
      reinterpret_cast<void*>(&enable));

}

status_t hsa_amd_profiling_get_dispatch_time(
    device_t agent_handle, signal_t hsa_signal,
    hsa_amd_profiling_dispatch_time_t* time) {

  IS_OPEN();

  IS_BAD_PTR(time);

  core::IAgent* agent = core::Agent::Object(agent_handle);

  IS_VALID(agent);

  core::ISignal* signal = core::Signal::Object(hsa_signal);

  IS_VALID(signal);

  if (agent->device_type() != core::IAgent::kGpu) {
    return ERROR_INVALID_AGENT;
  }

  GpuDeviceInt* gpu_agent = static_cast<hcs::GpuDeviceInt*>(agent);

  // Translate timestamp from GPU to system domain.
  gpu_agent->TranslateTime(signal, *time);

  return SUCCESS;

}

status_t hsa_amd_profiling_get_async_copy_time(
    signal_t hsa_signal, hsa_amd_profiling_async_copy_time_t* time) {

  IS_OPEN();

  IS_BAD_PTR(time);

  core::ISignal* signal = core::Signal::Object(hsa_signal);

  IS_VALID(signal);

  core::IAgent* agent = signal->async_copy_agent();

  if (agent == NULL) {
    return ERROR;
  }

  // Validate the embedded agent pointer if the signal is IPC.
  if (signal->isIPC()) {
    for (auto it : core::IRuntime::runtime_singleton_->gpu_agents()) {
      if (it == agent) break;
    }
    // If the agent isn't a GPU then it is from a different process or it's a CPU.
    // Assume it's a CPU and illegal uses will generate garbage data same as kernel completion.
    agent = core::IRuntime::runtime_singleton_->cpu_agents()[0];
  }

  if (agent->device_type() == core::IAgent::DeviceType::kGpu) {
    // Translate timestamp from GPU to system domain.
    static_cast<GpuDeviceInt*>(agent)->TranslateTime(signal, *time);
    return SUCCESS;
  }

  // The timestamp is already in system domain.
  time->start = signal->signal_.start_ts;
  time->end = signal->signal_.end_ts;
  return SUCCESS;

}

status_t hsa_amd_profiling_convert_tick_to_system_domain(device_t agent_handle,
                                                             uint64_t agent_tick,
                                                             uint64_t* system_tick) {

  IS_OPEN();

  IS_BAD_PTR(system_tick);

  core::IAgent* agent = core::Agent::Object(agent_handle);

  IS_VALID(agent);

  if (agent->device_type() != core::IAgent::kGpu) {
    return ERROR_INVALID_AGENT;
  }

  GpuDeviceInt* gpu_agent = static_cast<hcs::GpuDeviceInt*>(agent);

  *system_tick = gpu_agent->TranslateTime(agent_tick);

  return SUCCESS;

}

status_t hsa_amd_signal_create(signal_value_t initial_value, uint32_t num_consumers,
                                   const device_t* consumers, uint64_t attributes,
                                   signal_t* hsa_signal) {
  struct AgentHandleCompare {
    bool operator()(const device_t& lhs, const hsa_agent_t& rhs) const {
      return lhs.handle < rhs.handle;
    }
  };


  IS_OPEN();
  IS_BAD_PTR(hsa_signal);

  core::ISignal* ret;

  bool enable_ipc = attributes & HSA_AMD_SIGNAL_IPC;
  bool use_default =
      enable_ipc || (attributes & HSA_AMD_SIGNAL_AMD_GPU_ONLY) || (!core::Ig_use_interrupt_wait);

  if ((!use_default) && (num_consumers != 0)) {
    IS_BAD_PTR(consumers);

    // Check for duplicates in consumers.
    std::set<device_t, AgentHandleCompare> consumer_set(consumers, consumers + num_consumers);
    if (consumer_set.size() != num_consumers) {
      return ERROR_INVALID_ARGUMENT;
    }

    use_default = true;
    for (const core::IAgent* cpu_agent : core::Runtime::runtime_singleton_->cpu_agents()) {
      use_default &= (consumer_set.find(cpu_agent->public_handle()) == consumer_set.end());
    }
  }

  if (use_default) {
    ret = new core::IDefaultSignal(initial_value, enable_ipc);
  } else {
    ret = new core::IInterruptSignal(initial_value);
  }

  *hsa_signal = core::ISignal::Handle(ret);
  return SUCCESS;

}

uint32_t hsa_amd_signal_wait_any(uint32_t signal_count, signal_t* hsa_signals,
                                 signal_condition_t* conds, signal_value_t* values,
                                 uint64_t timeout_hint, hsa_wait_state_t wait_hint,
                                 signal_value_t* satisfying_value) {

  IS_OPEN();
  // Do not check for signal invalidation.  Invalidation may occur during async
  // signal handler loop and is not an error.
  for (uint i = 0; i < signal_count; i++)
    assert(hsa_signals[i].handle != 0 && core::ISharedSignal::Object(hsa_signals[i])->IsValid() &&
           "Invalid signal.");

  return core::ISignal::WaitAny(signal_count, hsa_signals, conds, values,
                               timeout_hint, wait_hint, satisfying_value);
  // CATCHRET(uint32_t);
}
/*
status_t hsa_amd_signal_async_handler(signal_t hsa_signal, signal_condition_t cond,
                                          signal_value_t value, hsa_amd_signal_handler handler,
                                          void* arg) {

  IS_OPEN();
  IS_BAD_PTR(handler);

  core::ISignal* signal = core::Signal::Object(hsa_signal);
  IS_VALID(signal);
  if (core::Ig_use_interrupt_wait && (!core::InterruptSignal::IsType(signal)))
    return ERROR_INVALID_SIGNAL;
  return core::IRuntime::runtime_singleton_->SetAsyncSignalHandler(
      hsa_signal, cond, value, handler, arg);
}
*/

/*
status_t hsa_amd_async_function(void (*callback)(void* arg), void* arg) {

  IS_OPEN();

  IS_BAD_PTR(callback);
  static const signal_t null_signal = {0};
  return core::IRuntime::runtime_singleton_->SetAsyncSignalHandler(
      null_signal, HSA_SIGNAL_CONDITION_EQ, 0, (hsa_amd_signal_handler)callback,
      arg);

}
*/
/*
status_t hsa_amd_queue_cu_set_mask(const queue_t* queue,
                                               uint32_t num_cu_mask_count,
                                               const uint32_t* cu_mask) {

  IS_OPEN();
  IS_BAD_PTR(cu_mask);

  core::IQueue* cmd_queue = core::Queue::Object(queue);
  IS_VALID(cmd_queue);
  return cmd_queue->SetCUMasking(num_cu_mask_count, cu_mask);

}
*/

status_t hsa_amd_memory_lock(void* host_ptr, size_t size,
                                 device_t* agents, int num_agent,
                                 void** agent_ptr) {

  IS_OPEN();
  *agent_ptr = NULL;

  if (size == 0 || host_ptr == NULL || agent_ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  if ((agents != NULL && num_agent == 0) ||
      (agents == NULL && num_agent != 0)) {
    return ERROR_INVALID_ARGUMENT;
  }

  const hcs::MemoryRegion* system_region =
      reinterpret_cast<const hcs::MemoryRegion*>(
          core::IRuntime::runtime_singleton_->system_regions_fine()[0]);

  return system_region->Lock(num_agent, agents, host_ptr, size, agent_ptr);

}

status_t hsa_amd_memory_unlock(void* host_ptr) {

  IS_OPEN();

  const hcs::MemoryRegion* system_region =
      reinterpret_cast<const hcs::MemoryRegion*>(
          core::IRuntime::runtime_singleton_->system_regions_fine()[0]);

  return system_region->Unlock(host_ptr);

}

status_t hsa_amd_memory_pool_get_info(hsa_amd_memory_pool_t memory_pool,
                                          hsa_amd_memory_pool_info_t attribute, void* value) {

  IS_OPEN();
  IS_BAD_PTR(value);

  region_t region = {memory_pool.handle};
  const hcs::MemoryRegion* mem_region = hcs::MemoryRegion::Object(region);
  if (mem_region == NULL) {
    return (status_t)ERROR_INVALID_MEMORY_POOL;
  }

  return mem_region->GetPoolInfo(attribute, value);

}

status_t hsa_amd_agent_iterate_memory_pools(
    device_t agent_handle,
    status_t (*callback)(hsa_amd_memory_pool_t memory_pool, void* data),
    void* data) {

  IS_OPEN();
  IS_BAD_PTR(callback);
  const core::IAgent* agent = core::Agent::Object(agent_handle);
  IS_VALID(agent);

  if (agent->device_type() == core::IAgent::kCpu) {
    return reinterpret_cast<const hcs::CpuAgent*>(agent)->VisitRegion(
        false, reinterpret_cast<status_t (*)(region_t memory_pool,
                                                 void* data)>(callback),
        data);
  }

  return reinterpret_cast<const GpuDeviceInt*>(agent)->VisitRegion(
      false,
      reinterpret_cast<status_t (*)(region_t memory_pool, void* data)>(
          callback),
      data);

}

status_t hsa_amd_memory_pool_allocate(hsa_amd_memory_pool_t memory_pool, size_t size,
                                          uint32_t flags, void** ptr) {

  IS_OPEN();

  if (size == 0 || ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  region_t region = {memory_pool.handle};
  const core::IMemoryRegion* mem_region = core::MemoryRegion::Object(region);

  // if (mem_region == NULL || !mem_region->IsValid()) {
  if (mem_region == NULL) {
    return (status_t)ERROR_INVALID_MEMORY_POOL;
  }

  return core::IRuntime::runtime_singleton_->AllocateMemory(
      mem_region, size, core::IMemoryRegion::AllocateRestrict, ptr);

}

status_t hsa_amd_memory_pool_free(void* ptr) {
  return hsa_memory_free(ptr);
}

status_t hsa_amd_agents_allow_access(uint32_t num_agents, const device_t* agents,
                                         const uint32_t* flags, const void* ptr) {

  IS_OPEN();

  if (num_agents == 0 || agents == NULL || flags != NULL || ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  return core::IRuntime::runtime_singleton_->AllowAccess(num_agents, agents,
                                                        ptr);

}

status_t hsa_amd_memory_pool_can_migrate(hsa_amd_memory_pool_t src_memory_pool,
                                             hsa_amd_memory_pool_t dst_memory_pool, bool* result) {

  IS_OPEN();

  if (result == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  region_t src_region_handle = {src_memory_pool.handle};
  const hcs::MemoryRegion* src_mem_region =
      hcs::MemoryRegion::Object(src_region_handle);

  // if (src_mem_region == NULL || !src_mem_region->IsValid()) {
  if (src_mem_region == NULL) {
    return static_cast<status_t>(ERROR_INVALID_MEMORY_POOL);
  }

  region_t dst_region_handle = {dst_memory_pool.handle};
  const hcs::MemoryRegion* dst_mem_region =
      hcs::MemoryRegion::Object(dst_region_handle);

  // if (dst_mem_region == NULL || !dst_mem_region->IsValid()) {
  if (dst_mem_region == NULL) {
    return static_cast<status_t>(ERROR_INVALID_MEMORY_POOL);
  }

  return src_mem_region->CanMigrate(*dst_mem_region, *result);

}

status_t hsa_amd_memory_migrate(const void* ptr,
                                    hsa_amd_memory_pool_t memory_pool,
                                    uint32_t flags) {

  IS_OPEN();

  if (ptr == NULL || flags != 0) {
    return ERROR_INVALID_ARGUMENT;
  }

  region_t dst_region_handle = {memory_pool.handle};
  const hcs::MemoryRegion* dst_mem_region =
      hcs::MemoryRegion::Object(dst_region_handle);

  // if (dst_mem_region == NULL || !dst_mem_region->IsValid()) {
  if (dst_mem_region == NULL ) {
    return static_cast<status_t>(ERROR_INVALID_MEMORY_POOL);
  }

  return dst_mem_region->Migrate(flags, ptr);

}

status_t hsa_amd_agent_memory_pool_get_info(
    device_t agent_handle, hsa_amd_memory_pool_t memory_pool,
    hsa_amd_agent_memory_pool_info_t attribute, void* value) {

  IS_OPEN();

  if (value == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  const core::IAgent* agent = core::Agent::Object(agent_handle);
  IS_VALID(agent);

  region_t region_handle = {memory_pool.handle};
  const hcs::MemoryRegion* mem_region =
      hcs::MemoryRegion::Object(region_handle);

  // if (mem_region == NULL || !mem_region->IsValid()) {
  if (mem_region == NULL) {
    return static_cast<status_t>(ERROR_INVALID_MEMORY_POOL);
  }

  return mem_region->GetAgentPoolInfo(*agent, attribute, value);

}
/*
status_t hsa_amd_interop_map_buffer(uint32_t num_agents,
                                        device_t* agents, int interop_handle,
                                        uint32_t flags, size_t* size,
                                        void** ptr, size_t* metadata_size,
                                        const void** metadata) {
  static const int tinyArraySize=8;

  IS_OPEN();
  IS_BAD_PTR(agents);
  IS_BAD_PTR(size);
  IS_BAD_PTR(ptr);
  if (flags != 0) return ERROR_INVALID_ARGUMENT;
  if (num_agents == 0) return ERROR_INVALID_ARGUMENT;

  core::IAgent* short_agents[tinyArraySize];
  core::IAgent** core_agents = short_agents;
  if (num_agents > tinyArraySize) {
    core_agents = new core::IAgent* [num_agents];
    if (core_agents == NULL) return ERROR_OUT_OF_RESOURCES;
  }

  for (uint32_t i = 0; i < num_agents; i++) {
    core::IAgent* device = core::Agent::Object(agents[i]);
    IS_VALID(device);
    core_agents[i] = device;
  }

  auto ret = core::IRuntime::runtime_singleton_->InteropMap(
      num_agents, core_agents, interop_handle, flags, size, ptr, metadata_size,
      metadata);

  if (num_agents > tinyArraySize) delete[] core_agents;
  return ret;

}
*/

/*
status_t hsa_amd_interop_unmap_buffer(void* ptr) {

  IS_OPEN();
  if (ptr != NULL) core::IRuntime::runtime_singleton_->InteropUnmap(ptr);
  return SUCCESS;

}
*/

status_t hsa_amd_pointer_info(void* ptr, hsa_amd_pointer_info_t* info, void* (*alloc)(size_t),
                                  uint32_t* num_accessible, device_t** accessible) {

  IS_OPEN();
  IS_BAD_PTR(ptr);
  IS_BAD_PTR(info);
  return core::IRuntime::runtime_singleton_->PtrInfo(ptr, info, alloc, num_accessible, accessible);
}


/*
status_t hsa_amd_pointer_info_set_userdata(void* ptr, void* userdata) {

  IS_OPEN();
  IS_BAD_PTR(ptr);
  return core::IRuntime::runtime_singleton_->SetPtrInfoData(ptr, userdata);
}
*/


status_t hsa_amd_ipc_memory_create(void* ptr, size_t len, hsa_amd_ipc_memory_t* handle) {

  IS_OPEN();
  IS_BAD_PTR(ptr);
  IS_BAD_PTR(handle);
  return core::IRuntime::runtime_singleton_->IPCCreate(ptr, len, handle);

}


status_t hsa_amd_ipc_memory_attach(const hsa_amd_ipc_memory_t* ipc, size_t len,
                                       uint32_t num_agents, const device_t* mapping_agents,
                                       void** mapped_ptr) {
  static const int tinyArraySize = 8;

  IS_OPEN();
  IS_BAD_PTR(mapped_ptr);
  if (num_agents != 0) IS_BAD_PTR(mapping_agents);

  core::IAgent** core_agents = nullptr;
  if (num_agents > tinyArraySize)
    core_agents = new core::IAgent*[num_agents];
  else
    core_agents = (core::IAgent**)alloca(sizeof(core::Agent*) * num_agents);
  if (core_agents == NULL) return ERROR_OUT_OF_RESOURCES;
  MAKE_SCOPE_GUARD([&]() {
    if (num_agents > tinyArraySize) delete[] core_agents;
  });

  for (uint32_t i = 0; i < num_agents; i++) {
    core::IAgent* device = core::Agent::Object(mapping_agents[i]);
    IS_VALID(device);
    core_agents[i] = device;
  }

  return core::IRuntime::runtime_singleton_->IPCAttach(ipc, len, num_agents, core_agents,
                                                      mapped_ptr);

}


status_t hsa_amd_ipc_memory_detach(void* mapped_ptr) {

  IS_OPEN();
  IS_BAD_PTR(mapped_ptr);
  return core::IRuntime::runtime_singleton_->IPCDetach(mapped_ptr);

}
/*
status_t hsa_amd_ipc_signal_create(signal_t hsa_signal, hsa_amd_ipc_signal_t* handle) {

  IS_OPEN();
  IS_BAD_PTR(handle);
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  IS_VALID(signal);
  core::IIPCSignal::CreateHandle(signal, handle);
  return SUCCESS;

}

status_t hsa_amd_ipc_signal_attach(const hsa_amd_ipc_signal_t* handle,
                                       signal_t* hsa_signal) {

  IS_OPEN();
  IS_BAD_PTR(handle);
  IS_BAD_PTR(hsa_signal);
  core::ISignal* signal = core::IPCSignal::Attach(handle);
  *hsa_signal = core::ISignal::Object(signal);
  return SUCCESS;

}
*/

// For use by tools only - not in library export table.
/*
status_t hsa_amd_queue_intercept_create(
    device_t agent_handle, uint32_t size, queue_type32_t type,
    void (*callback)(status_t status, queue_t* source, void* data), void* data,
    uint32_t private_segment_size, uint32_t group_segment_size, queue_t** queue) {

  IS_OPEN();
  IS_BAD_PTR(queue);
  queue_t* lower_queue;
  status_t err = hsa_queue_create(agent_handle, size, type, callback, data,
                                           private_segment_size, group_segment_size, &lower_queue);
  if (err != SUCCESS) return err;
  std::unique_ptr<core::IQueue> lowerQueue(core::Queue::Object(lower_queue));

  std::unique_ptr<core::IInterceptQueue> upperQueue(new core::InterceptQueue(std::move(lowerQueue)));

  *queue = core::IQueue::Handle(upperQueue.release());
  return SUCCESS;
}

// For use by tools only - not in library export table.
status_t hsa_amd_queue_intercept_register(queue_t* queue,
                                              hsa_amd_queue_intercept_handler callback,
                                              void* user_data) {

  IS_OPEN();
  IS_BAD_PTR(callback);
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  IS_VALID(cmd_queue);
  if (!core::IInterceptQueue::IsType(cmd_queue)) return ERROR_INVALID_QUEUE;
  core::IInterceptQueue* iQueue = static_cast<core::InterceptQueue*>(cmd_queue);
  iQueue->AddInterceptor(callback, user_data);
  return SUCCESS;

}
*/

status_t hsa_amd_register_system_event_handler(hsa_amd_system_event_callback_t callback,
                                                   void* data) {
  IS_OPEN();
  return core::IRuntime::runtime_singleton_->SetCustomSystemEventHandler(callback, data);
}

status_t HSA_API hsa_amd_queue_set_priority(queue_t* queue,
                                                hsa_amd_queue_priority_t priority) {
  IS_OPEN();
  IS_BAD_PTR(queue);
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  IS_VALID(cmd_queue);

  static std::map<hsa_amd_queue_priority_t, HSA_QUEUE_PRIORITY> ext_kmt_priomap = {
      {HSA_AMD_QUEUE_PRIORITY_LOW, HSA_QUEUE_PRIORITY_MINIMUM},
      {HSA_AMD_QUEUE_PRIORITY_NORMAL, HSA_QUEUE_PRIORITY_NORMAL},
      {HSA_AMD_QUEUE_PRIORITY_HIGH, HSA_QUEUE_PRIORITY_MAXIMUM},
  };

  auto priority_it = ext_kmt_priomap.find(priority);

  if (priority_it == ext_kmt_priomap.end()) {
    return ERROR_INVALID_ARGUMENT;
  }

  return cmd_queue->SetPriority(priority_it->second);
}

status_t hsa_amd_register_deallocation_callback(void* ptr,
                                                    hsa_amd_deallocation_callback_t callback,
                                                    void* user_data) {
  IS_OPEN();
  IS_BAD_PTR(ptr);
  IS_BAD_PTR(callback);

  return core::IRuntime::runtime_singleton_->RegisterReleaseNotifier(ptr, callback, user_data);

}

status_t hsa_amd_deregister_deallocation_callback(void* ptr,
                                                      hsa_amd_deallocation_callback_t callback) {
  IS_OPEN();
  IS_BAD_PTR(ptr);
  IS_BAD_PTR(callback);

  return core::IRuntime::runtime_singleton_->DeregisterReleaseNotifier(ptr, callback);

}

// For use by tools only - not in library export table.
status_t hsa_amd_runtime_queue_create_register(hsa_amd_runtime_queue_notifier callback,
                                                   void* user_data) {
  IS_OPEN();
  return core::IRuntime::runtime_singleton_->SetInternalQueueCreateNotifier(callback, user_data);
}

// } // end of hcs namespace
