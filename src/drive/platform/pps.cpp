// HSA C to C++ interface implementation.
// This file does argument checking and conversion to C++.
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define __read__  _read
#define __lseek__ _lseek
#else
#include <unistd.h>
#define __read__  read
#define __lseek__ lseek
#endif // _WIN32 || _WIN64

#include "inc/platform.h"
#include "inc/agent.h"
#include "inc/host_queue.h"
#include "inc/isa.h"
#include "inc/memory_region.h"
#include "inc/queue.h"
#include "inc/signal.h"
#include "inc/default_signal.h"
#include "inc/cache.h"
// #include "core/inc/amd_loader_context.hpp"
// #include "inc/hsa_ven_amd_loader.h"
// #include "inc/hsa_ven_amd_aqlprofile.h"
// #include "core/inc/hsa_ext_amd_impl.h"

// schi using namespace amd::hsa;
/*
template <class T>
struct ValidityError;
template <> struct ValidityError<core::ISignal*> {
  enum { kValue = ERROR_INVALID_SIGNAL };
};
template <> struct ValidityError<core::ISignalGroup*> {
  enum { kValue = ERROR_INVALID_SIGNAL_GROUP };
};
template <> struct ValidityError<core::IAgent*> {
  enum { kValue = ERROR_INVALID_AGENT };
};
template <> struct ValidityError<core::IMemoryRegion*> {
  enum { kValue = ERROR_INVALID_REGION };
};
template <> struct ValidityError<core::IQueue*> {
  enum { kValue = ERROR_INVALID_QUEUE };
};
template <> struct ValidityError<core::ICache*> {
  enum { kValue = ERROR_INVALID_CACHE };
};
template <> struct ValidityError<core::IIsa*> {
  enum { kValue = ERROR_INVALID_ISA };
};
template <class T> struct ValidityError<const T*> {
  enum { kValue = ValidityError<T*>::kValue };
};
*/
/*
template <class T>
static __forceinline bool IsValid(T* ptr) {
  return (ptr == NULL) ? NULL : ptr->IsValid();
}
*/

namespace hcs {
status_t handleException();

template <class T> static __forceinline T handleExceptionT() {
  handleException();
  abort();
  return T();
}
}

//-----------------------------------------------------------------------------
// Basic Checks
//-----------------------------------------------------------------------------
static_assert(sizeof(hsa_barrier_and_packet_t) ==
                  sizeof(hsa_kernel_dispatch_packet_t),
              "AQL packet definitions have wrong sizes!");
static_assert(sizeof(hsa_barrier_and_packet_t) ==
                  sizeof(hsa_agent_dispatch_packet_t),
              "AQL packet definitions have wrong sizes!");
static_assert(sizeof(hsa_barrier_and_packet_t) == 64,
              "AQL packet definitions have wrong sizes!");
static_assert(sizeof(hsa_barrier_and_packet_t) ==
                  sizeof(hsa_barrier_or_packet_t),
              "AQL packet definitions have wrong sizes!");
#ifdef HSA_LARGE_MODEL
static_assert(sizeof(void*) == 8, "HSA_LARGE_MODEL is set incorrectly!");
#else
static_assert(sizeof(void*) == 4, "HSA_LARGE_MODEL is set incorrectly!");
#endif

#if !defined(HSA_LARGE_MODEL) || !defined(__linux__)
// static_assert(false, "Only HSA_LARGE_MODEL (64bit mode) and Linux supported.");
#endif

// namespace HSA {

//---------------------------------------------------------------------------//
//  Init/Shutdown routines
//---------------------------------------------------------------------------//
status_t hsa_init() {
  return core::IRuntime::runtime_singleton_->Acquire();
}

status_t hsa_shut_down() {
  return core::IRuntime::runtime_singleton_->Release();
}

//---------------------------------------------------------------------------//
//  System
//---------------------------------------------------------------------------//
status_t
    hsa_system_get_info(hsa_system_info_t attribute, void* value) {
  return core::IRuntime::runtime_singleton_->GetSystemInfo(attribute, value);
}

//---------------------------------------------------------------------------//
//  Agent
//---------------------------------------------------------------------------//
status_t
    hsa_iterate_agents(status_t (*callback)(device_t agent, void* data),
                       void* data) {
  // IS_BAD_PTR(callback);
  return core::IRuntime::runtime_singleton_->IterateAgent(callback, data);
}

status_t hsa_device_get_info(device_t agent_handle,
                                        hsa_agent_info_t attribute,
                                        void* value) {
  // IS_OPEN();
  // IS_BAD_PTR(value);
  const core::IAgent* agent = core::IAgent::Object(agent_handle);
  // IS_VALID(agent);
  return agent->GetInfo(attribute, value);
}


/*
status_t hsa_cache_get_info(cache_t cache, cache_info_t attribute, void* value) {
  TRY;
  IS_OPEN();
  core::ICache* Cache = core::Cache::Convert(cache);
  IS_VALID(Cache);
  IS_BAD_PTR(value);
  return Cache->GetInfo(attribute, value);
  CATCH;
}

status_t hsa_agent_iterate_caches(device_t agent_handle,
                                      status_t (*callback)(cache_t cache, void* data),
                                      void* data) {
  TRY;
  IS_OPEN();
  const core::IAgent* agent = core::IAgent::Convert(agent_handle);
  IS_VALID(agent);
  IS_BAD_PTR(callback);
  return agent->IterateCache(callback, data);
  CATCH;
}
*/

/// @brief Api to create a user mode queue.
///
/// @param agent Hsa Agent which will execute Aql commands
///
/// @param size Size of Queue in terms of Aql packet size
///
/// @param type of Queue Single Writer or Multiple Writer
///
/// @param callback Callback function to register in case Quee
/// encounters an error
///
/// @param service_queue Pointer to a service queue
///
/// @param queue Output parameter updated with a pointer to the
/// queue being created
///
/// @return hsa_status
status_t hsa_queue_create(
    device_t agent_handle, uint32_t size, queue_type32_t type,
    void (*callback)(status_t status, queue_t* source, void* data),
    void* data, uint32_t private_segment_size, uint32_t group_segment_size,
    queue_t** queue) {

  if ((queue == nullptr) || (size == 0) || (!IsPowerOfTwo(size)) || (type < HSA_QUEUE_TYPE_MULTI) ||
      (type > HSA_QUEUE_TYPE_SINGLE)) {
    return ERROR_INVALID_ARGUMENT;
  }

  core::IAgent* agent = core::IAgent::Object(agent_handle);

  queue_type32_t agent_queue_type = HSA_QUEUE_TYPE_MULTI;
  status_t status =
      agent->GetInfo(HSA_AGENT_INFO_QUEUE_TYPE, &agent_queue_type);
  assert(SUCCESS == status);

  if (agent_queue_type == HSA_QUEUE_TYPE_SINGLE &&
      type != HSA_QUEUE_TYPE_SINGLE) {
    return ERROR_INVALID_QUEUE_CREATION;
  }

  if (callback == nullptr) callback = core::IQueue::DefaultErrorHandler;

  core::IQueue* cmd_queue = nullptr;
  status = agent->QueueCreate(size, type, callback, data, private_segment_size,
                              group_segment_size, &cmd_queue);
  if (status != SUCCESS) return status;

  assert(cmd_queue != nullptr && "Queue not returned but status was success.\n");
  *queue = core::IQueue::Handle(cmd_queue);
  return status;
}

status_t hsa_soft_queue_create(region_t region, uint32_t size,
                                   queue_type32_t type, uint32_t features,
                                   signal_t doorbell_signal,
                                   queue_t** queue) {

  if ((queue == NULL) || (region.handle == 0) ||
      (doorbell_signal.handle == 0) || (size == 0) || (!IsPowerOfTwo(size)) ||
      (type < HSA_QUEUE_TYPE_MULTI) || (type > HSA_QUEUE_TYPE_SINGLE) ||
      (features == 0)) {
    return ERROR_INVALID_ARGUMENT;
  }

  // const core::IMemoryRegion* mem_region = core::MemoryRegion::Convert(region);

  // const core::ISignal* signal = core::Signal::Convert(doorbell_signal);

  core::IHostQueue* host_queue = new core::HostQueue(region, size, type, features, doorbell_signal);

  *queue = core::IQueue::Handle(host_queue);

  return SUCCESS;
}

/// @brief Api to destroy a user mode queue
///
/// @param queue Pointer to the queue being destroyed
///
/// @return hsa_status
status_t hsa_queue_destroy(queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  delete cmd_queue;
  return SUCCESS;
}

/// @brief Api to inactivate a user mode queue
///
/// @param queue Pointer to the queue being inactivated
///
/// @return hsa_status
status_t hsa_queue_inactivate(queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  cmd_queue->Inactivate();
  return SUCCESS;
}

/// @brief Api to read the Read Index of Queue using Acquire semantics
///
/// @param queue Pointer to the queue whose read index is being read
///
/// @return uint64_t Value of Read index
uint64_t hsa_queue_load_read_index_scacquire(const queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->LoadReadIndexAcquire();
}

/// @brief Api to read the Read Index of Queue using Relaxed semantics
///
/// @param queue Pointer to the queue whose read index is being read
///
/// @return uint64_t Value of Read index
uint64_t hsa_queue_load_read_index_relaxed(const queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->LoadReadIndexRelaxed();
}

/// @brief Api to read the Write Index of Queue using Acquire semantics
///
/// @param queue Pointer to the queue whose write index is being read
///
/// @return uint64_t Value of Write index
uint64_t hsa_queue_load_write_index_scacquire(const queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->LoadWriteIndexAcquire();
}

/// @brief Api to read the Write Index of Queue using Relaxed semantics
///
/// @param queue Pointer to the queue whose write index is being read
///
/// @return uint64_t Value of Write index
uint64_t hsa_queue_load_write_index_relaxed(const queue_t* queue) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->LoadWriteIndexRelaxed();
}

/// @brief Api to store the Read Index of Queue using Relaxed semantics
///
/// @param queue Pointer to the queue whose read index is being updated
///
/// @param value Value of new read index
void hsa_queue_store_read_index_relaxed(const queue_t* queue,
                                                uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  cmd_queue->StoreReadIndexRelaxed(value);
}

/// @brief Api to store the Read Index of Queue using Release semantics
///
/// @param queue Pointer to the queue whose read index is being updated
///
/// @param value Value of new read index
void hsa_queue_store_read_index_screlease(const queue_t* queue, uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  cmd_queue->StoreReadIndexRelease(value);
}

/// @brief Api to store the Write Index of Queue using Relaxed semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param value Value of new write index
void hsa_queue_store_write_index_relaxed(const queue_t* queue,
                                                 uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  cmd_queue->StoreWriteIndexRelaxed(value);
}

/// @brief Api to store the Write Index of Queue using Release semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param value Value of new write index
void hsa_queue_store_write_index_screlease(const queue_t* queue, uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  cmd_queue->StoreWriteIndexRelease(value);
}

/// @brief Api to compare and swap the Write Index of Queue using Acquire and
/// Release semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param expected Current value of write index
///
/// @param value Value of new write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_cas_write_index_scacq_screl(const queue_t* queue, uint64_t expected,
                                               uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->CasWriteIndexAcqRel(expected, value);
}

/// @brief Api to compare and swap the Write Index of Queue using Acquire
/// Semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param expected Current value of write index
///
/// @param value Value of new write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_cas_write_index_scacquire(const queue_t* queue, uint64_t expected,
                                             uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->CasWriteIndexAcquire(expected, value);
}

/// @brief Api to compare and swap the Write Index of Queue using Relaxed
/// Semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param expected Current value of write index
///
/// @param value Value of new write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_cas_write_index_relaxed(const queue_t* queue,
                                                   uint64_t expected,
                                                   uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->CasWriteIndexRelaxed(expected, value);
}

/// @brief Api to compare and swap the Write Index of Queue using Release
/// Semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param expected Current value of write index
///
/// @param value Value of new write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_cas_write_index_screlease(const queue_t* queue, uint64_t expected,
                                             uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->CasWriteIndexRelease(expected, value);
}

/// @brief Api to Add to the Write Index of Queue using Acquire and Release
/// Semantics
///
/// @param queue Pointer to the queue whose write index is being updated
///
/// @param value Value to add to write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_add_write_index_scacq_screl(const queue_t* queue, uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->AddWriteIndexAcqRel(value);
}

/// @brief Api to Add to the Write Index of Queue using Acquire Semantics
/// @param queue Pointer to the queue whose write index is being updated
/// @param value Value to add to write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_add_write_index_scacquire(const queue_t* queue, uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->AddWriteIndexAcquire(value);
}

/// @brief Api to Add to the Write Index of Queue using Relaxed Semantics
/// @param queue Pointer to the queue whose write index is being updated
/// @param value Value to add to write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_add_write_index_relaxed(const queue_t* queue,
                                                   uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->AddWriteIndexRelaxed(value);
}

/// @brief Api to Add to the Write Index of Queue using Release Semantics
/// @param queue Pointer to the queue whose write index is being updated
/// @param value Value to add to write index
///
/// @return uint64_t Value of write index before the update
uint64_t hsa_queue_add_write_index_screlease(const queue_t* queue, uint64_t value) {
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  return cmd_queue->AddWriteIndexRelease(value);
}

//-----------------------------------------------------------------------------
// Memory
//-----------------------------------------------------------------------------
status_t hsa_agent_iterate_regions(
    device_t agent_handle,
    status_t (*callback)(region_t region, void* data), void* data) {
  const core::IAgent* agent = core::IAgent::Object(agent_handle);
  return agent->IterateRegion(callback, data);
}

status_t hsa_region_get_info(region_t region,
                                         hsa_region_info_t attribute,
                                         void* value) {

  const core::IMemoryRegion* mem_region = core::MemoryRegion::Object(region);

  return mem_region->GetInfo(attribute, value);
}

status_t hsa_memory_register(void* address, size_t size) {

  if (size == 0 && address != NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  return SUCCESS;
}

status_t hsa_memory_deregister(void* address, size_t size) {

  return SUCCESS;
}

status_t
    hsa_memory_allocate(region_t region, size_t size, void** ptr) {

  if (size == 0 || ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  const core::IMemoryRegion* mem_region = core::MemoryRegion::Object(region);

  return core::IRuntime::runtime_singleton_->AllocateMemory(
      mem_region, size, core::IMemoryRegion::AllocateNoFlags, ptr);
}

status_t hsa_memory_free(void* ptr) {

  if (ptr == NULL) {
    return SUCCESS;
  }

  return core::IRuntime::runtime_singleton_->FreeMemory(ptr);
}

/*
status_t hsa_memory_assign_agent(void* ptr,
                                             device_t agent_handle,
                                             hsa_access_permission_t access) {
  TRY;
  IS_OPEN();

  if ((ptr == NULL) || (access < HSA_ACCESS_PERMISSION_RO) ||
      (access > HSA_ACCESS_PERMISSION_RW)) {
    return ERROR_INVALID_ARGUMENT;
  }

  const core::IAgent* agent = core::IAgent::Convert(agent_handle);

  return SUCCESS;
  CATCH;
}
*/

// status_t hsa_memory_copy(void* dst, const void* src, size_t size, uint8_t dir) {
status_t hsa_memory_copy(void* dst, const void* src, size_t size) {

  if (dst == NULL || src == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  if (size == 0) {
    return SUCCESS;
  }

  // return core::IRuntime::runtime_singleton_->CopyMemory(dst, src, size, dir);
  return core::IRuntime::runtime_singleton_->CopyMemory(dst, src, size);
}

//-----------------------------------------------------------------------------
// Signals
//-----------------------------------------------------------------------------

status_t
    hsa_signal_create(signal_value_t initial_value, uint32_t num_consumers,
                      const device_t* consumers, signal_t* hsa_signal) {
 //  return hcs::hsa_amd_signal_create(initial_value, num_consumers, consumers, 0, hsa_signal);
  core::ISignal* ret = new core::DefaultSignal(initial_value, false);
  *hsa_signal = core::ISignal::Handle(ret);
  return SUCCESS;
}

status_t hsa_signal_destroy(signal_t hsa_signal) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->DestroySignal();
  return SUCCESS;
}

signal_value_t hsa_signal_load_relaxed(signal_t hsa_signal) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->LoadRelaxed();
}

signal_value_t hsa_signal_load_scacquire(signal_t hsa_signal) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->LoadAcquire();
}

void hsa_signal_store_relaxed(signal_t hsa_signal,
                                      signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->StoreRelaxed(value);
}

void hsa_signal_store_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->StoreRelease(value);
}

signal_value_t
    hsa_signal_wait_relaxed(signal_t hsa_signal,
                            signal_condition_t condition,
                            signal_value_t compare_value,
                            uint64_t timeout_hint,
                            hsa_wait_state_t wait_state_hint) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->WaitRelaxed(condition, compare_value, timeout_hint,
                             wait_state_hint);
}

signal_value_t hsa_signal_wait_scacquire(signal_t hsa_signal,
                                             signal_condition_t condition,
                                             signal_value_t compare_value,
                                             uint64_t timeout_hint,
                                             hsa_wait_state_t wait_state_hint) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->WaitAcquire(condition, compare_value, timeout_hint,
                             wait_state_hint);
}

status_t hsa_signal_group_create(uint32_t num_signals, const signal_t* signals,
                                     uint32_t num_consumers, const device_t* consumers,
                                     hsa_signal_group_t* signal_group) {
  if (num_signals == 0) return ERROR_INVALID_ARGUMENT;
  core::ISignalGroup* group = new core::SignalGroup(num_signals, signals);
  // CHECK_ALLOC(group);
  if (!group->IsValid()) {
    delete group;
    return ERROR_OUT_OF_RESOURCES;
  }
  *signal_group = core::ISignalGroup::Convert(group);
  return SUCCESS;
}

status_t hsa_signal_group_destroy(hsa_signal_group_t signal_group) {
  core::ISignalGroup* group = core::SignalGroup::Convert(signal_group);
  delete group;
  return SUCCESS;
}

status_t hsa_signal_group_wait_any_relaxed(hsa_signal_group_t signal_group,
                                               const signal_condition_t* conditions,
                                               const signal_value_t* compare_values,
                                               hsa_wait_state_t wait_state_hint,
                                               signal_t* signal, signal_value_t* value) {
  const core::ISignalGroup* group = core::SignalGroup::Convert(signal_group);
  const uint32_t index = core::ISignal::WaitAny(
      group->Count(), const_cast<signal_t*>(group->List()),
      const_cast<signal_condition_t*>(conditions),
      const_cast<signal_value_t*>(compare_values), uint64_t(-1), wait_state_hint, value);

  if (index >= group->Count()) return ERROR_INVALID_ARGUMENT;
  *signal = group->List()[index];
  return SUCCESS;
}

status_t hsa_signal_group_wait_any_scacquire(hsa_signal_group_t signal_group,
                                                 const signal_condition_t* conditions,
                                                 const signal_value_t* compare_values,
                                                 hsa_wait_state_t wait_state_hint,
                                                 signal_t* signal, signal_value_t* value) {
  status_t ret = hsa_signal_group_wait_any_relaxed(
      signal_group, conditions, compare_values, wait_state_hint, signal, value);
  std::atomic_thread_fence(std::memory_order_acquire);
  return ret;
}

void hsa_signal_and_relaxed(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AndRelaxed(value);
}

void hsa_signal_and_scacquire(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AndAcquire(value);
}

void hsa_signal_and_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AndRelease(value);
}

void hsa_signal_and_scacq_screl(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AndAcqRel(value);
}

void hsa_signal_or_relaxed(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->OrRelaxed(value);
}

void hsa_signal_or_scacquire(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->OrAcquire(value);
}

void hsa_signal_or_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->OrRelease(value);
}

void hsa_signal_or_scacq_screl(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->OrAcqRel(value);
}

void hsa_signal_xor_relaxed(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->XorRelaxed(value);
}

void hsa_signal_xor_scacquire(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->XorAcquire(value);
}

void hsa_signal_xor_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->XorRelease(value);
}

void hsa_signal_xor_scacq_screl(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->XorAcqRel(value);
}

void hsa_signal_add_relaxed(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AddRelaxed(value);
}

void hsa_signal_add_scacquire(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AddAcquire(value);
}

void hsa_signal_add_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AddRelease(value);
}

void hsa_signal_add_scacq_screl(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->AddAcqRel(value);
}

void hsa_signal_subtract_relaxed(signal_t hsa_signal,
                                         signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->SubRelaxed(value);
}

void hsa_signal_subtract_scacquire(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->SubAcquire(value);
}

void hsa_signal_subtract_screlease(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->SubRelease(value);
}

void hsa_signal_subtract_scacq_screl(signal_t hsa_signal, signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  signal->SubAcqRel(value);
}

signal_value_t
    hsa_signal_exchange_relaxed(signal_t hsa_signal,
                                signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->ExchRelaxed(value);
}

signal_value_t hsa_signal_exchange_scacquire(signal_t hsa_signal,
                                                 signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->ExchAcquire(value);
}

signal_value_t hsa_signal_exchange_screlease(signal_t hsa_signal,
                                                 signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->ExchRelease(value);
}

signal_value_t hsa_signal_exchange_scacq_screl(signal_t hsa_signal,
                                                   signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->ExchAcqRel(value);
}

signal_value_t hsa_signal_cas_relaxed(signal_t hsa_signal,
                                                  signal_value_t expected,
                                                  signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->CasRelaxed(expected, value);
}

signal_value_t hsa_signal_cas_scacquire(signal_t hsa_signal, hsa_signal_value_t expected,
                                            signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->CasAcquire(expected, value);
}

signal_value_t hsa_signal_cas_screlease(signal_t hsa_signal, hsa_signal_value_t expected,
                                            signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->CasRelease(expected, value);
}

signal_value_t hsa_signal_cas_scacq_screl(signal_t hsa_signal, hsa_signal_value_t expected,
                                              signal_value_t value) {
  core::ISignal* signal = core::Signal::Object(hsa_signal);
  return signal->CasAcqRel(expected, value);
}

//===--- Instruction Set Architecture -------------------------------------===//

using core::IIsa;
using core::IIsaRegistry;

status_t hsa_isa_from_name(
    const char *name,
    hsa_isa_t *isa) {

  const Isa *isa_object = IsaRegistry::GetIsa(name);
  if (!isa_object) {
    return ERROR_INVALID_ISA_NAME;
  }

  *isa = Isa::Handle(isa_object);
  return SUCCESS;
}


status_t hsa_agent_iterate_isas(
    device_t agent,
    status_t (*callback)(hsa_isa_t isa,
                             void *data),
    void *data) {

  const core::IAgent *agent_object = core::IAgent::Object(agent);

  const Isa *isa_object = agent_object->isa();
  if (!isa_object) {
    return ERROR_INVALID_AGENT;
  }

  return callback(Isa::Handle(isa_object), data);
}
/*
hcs::Loader *GetLoader() {
  return core::IRuntime::runtime_singleton_->loader();
}
*/


// }  // end of namespace HSA
