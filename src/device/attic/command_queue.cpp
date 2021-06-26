
#include "inc/hsakmt.h"

#include "inc/asic_type.h"
// #include "tools/threadtrace/thread_trace.h"
#include "inc/command_queue.h"

#include "inc/pps_memory_region.h"
#include "inc/default_signal.h"

#include "utils/pattern/function_traits.h"

// #include "hsa_ven_amd_loader.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

#include <iostream>

extern std::function<void*(size_t, size_t)> g_queue_allocator;
extern std::function<void(void*)> g_queue_deallocator;


namespace device {

int CommandQueue::rtti_id_=0;
int CommandQueue::queue_cnt_ = 0;

HsaEvent* CommandQueue::queue_event_ = nullptr;
std::atomic<uint32_t> CommandQueue::queue_count_(0);
KernelMutex CommandQueue::queue_lock_;

CommandQueue::CommandQueue(GpuDevice* agent, uint32_t ring_size,
                                             HSAuint32 node, const HsaCoreProperties* properties,
                                             queue_type32_t queue_type, ScratchInfo& scratch,
                                             core::IHsaEventCallback callback, void* user_data, uint32_t asic_id)
    : LocalSignal(0),
      DoorbellSignal(signal()),
      queue_type_(HSA_QUEUE_TYPE_MULTI),
      // queue_scratch_(scratch),
      active_(true),
      doorbell_type_(1),
      agent_(agent),
      next_index_(0),
      size_(ring_size),
      node_id_(node),
      kernels_launched_(0),
      needs_barrier_(true),
      errors_callback_(callback),
      errors_data_(user_data)
      {
  queue_address_ = NULL;

  // Ensure Ensure space for at least one full PMC run
  if (size_ < 128) return;

  const size_t aql_queue_buffer_size = size_ * sizeof(core::IAqlPacket);
  queue_address_ = reinterpret_cast<core::IAqlPacket*>(
      g_queue_allocator(aql_queue_buffer_size, HSA_PACKET_ALIGN_BYTES));
  if (queue_address_ == NULL) {
    return;
  }

  MAKE_NAMED_SCOPE_GUARD(bufferGuard, [&]() {
    g_queue_deallocator(queue_address_);
    queue_address_ = NULL;
  });

  assert(IsMultipleOf(queue_address_, HSA_PACKET_ALIGN_BYTES));


  // Initialize queue buffer with invalid packet.
  const uint16_t kInvalidHeader = (HSA_PACKET_TYPE_INVALID << HSA_PACKET_HEADER_TYPE);

  for (size_t i = 0; i < size_; ++i) {
    queue_address_[i].agent.header = kInvalidHeader;
  }
  // Zero the amd_queue_ structure to clear RPTR/WPTR before queue attach.
  memset(&amd_queue_, 0, sizeof(amd_queue_));

  // Initialize and map a HW AQL queue.
  HsaQueueResource queue_rsrc = {0};
  queue_rsrc.Queue_read_ptr_aql = (uint64_t*)&amd_queue_.read_dispatch_id;

  if (doorbell_type_ == 2) {
    // Hardware write pointer supports AQL semantics.
    queue_rsrc.Queue_write_ptr_aql = (uint64_t*)&amd_queue_.write_dispatch_id;
  } else {
    // Map hardware write pointer to a software proxy.
    queue_rsrc.Queue_write_ptr_aql = (uint64_t*)&amd_queue_.max_legacy_doorbell_dispatch_id_plus_1;
  }

  // Populate amd_queue_ structure.
  amd_queue_.hsa_queue.type = HSA_QUEUE_TYPE_MULTI;
  amd_queue_.hsa_queue.features = HSA_QUEUE_FEATURE_KERNEL_DISPATCH;
  amd_queue_.hsa_queue.base_address = queue_address_;
  amd_queue_.hsa_queue.doorbell_signal = Signal::Handle(this);
  amd_queue_.hsa_queue.size = size_;
  // Bind Id of Queue such that is unique i.e. it is not re-used by another
  // queue (AQL, HOST) in the same process during its lifetime.
  // amd_queue_.hsa_queue.id = this->GetQueueId();
  amd_queue_.hsa_queue.id = GetID(this);
  amd_queue_.write_dispatch_id = 0;
  amd_queue_.read_dispatch_id = 0;
  HCS_BITS_SET(
      amd_queue_.queue_properties, AMD_QUEUE_PROPERTIES_ENABLE_PROFILING, 0);
  HCS_BITS_SET(
      amd_queue_.queue_properties, AMD_QUEUE_PROPERTIES_IS_PTR64, (sizeof(void*) == 8));

  // Populate doorbell signal structure.
  memset(&signal_, 0, sizeof(signal_));
  signal_.kind = (doorbell_type_ == 2) ? AMD_SIGNAL_KIND_DOORBELL : AMD_SIGNAL_KIND_LEGACY_DOORBELL;
  signal_.legacy_hardware_doorbell_ptr = nullptr;
  signal_.queue_ptr = &amd_queue_;


  const auto& props = agent_->properties();
  amd_queue_.max_cu_id = (props->NumFComputeCores / props->NumSIMDPerCU) - 1;
  amd_queue_.max_wave_id = (props->MaxWavesPerSIMD * props->NumSIMDPerCU) - 1;

  // Set group and private memory apertures in amd_queue_.
  auto& regions = agent->regions();

  for (auto region : regions) {
    const MemoryRegion* amdregion = static_cast<const hcs::MemoryRegion*>(region);
    uint64_t base = amdregion->GetBaseAddress();

    if (amdregion->IsLDS()) {
      amd_queue_.group_segment_aperture_base_hi = uint32_t(uintptr_t(base) >> 32);
    }

    if (amdregion->IsScratch()) {
      amd_queue_.private_segment_aperture_base_hi = uint32_t(uintptr_t(base) >> 32);
    }
  }

  assert(amd_queue_.group_segment_aperture_base_hi != 0 && "No group region found.");

  if (core::IRuntime::runtime_singleton_->flag().check_flat_scratch()) {
    assert(amd_queue_.private_segment_aperture_base_hi != 0 && "No private region found.");
  }

  // Bind the index of queue just created
  queue_index_ = queue_cnt_;
  queue_cnt_++;

  assert(amd_queue_.group_segment_aperture_base_hi != 0 && "No group region found.");

  if (core::IRuntime::runtime_singleton_->flag().check_flat_scratch()) {
    assert(amd_queue_.private_segment_aperture_base_hi != 0 && "No private region found.");
  }


/*
 * TODO use kernel ioctl to alloc memory, replace buffer_ with queue_address_
  auto err = hsaKmtAllocMemory(agent->node_id(), kCBUFQueueSize, flags, (void**)&queue_address_);
  err = hsaKmtMapMemoryToGPU(queue_address_, kCBUFQueueSize, &alternate_va);
  if (hsaKmtCreateQueue(node_id_, HSA_QUEUE_COMPUTE, 100,
                        HSA_QUEUE_PRIORITY_MAXIMUM, queue_address_,
                        kCBUFQueueSize, NULL,
                        &queue_resource_) != SUCCESS) {
    return;
  }
  */
  device_status_t device_status;
  device_status = DeviceCreateQueue(node_id_, HSA_QUEUE_COMPUTE, 100, HSA_QUEUE_PRIORITY_MAXIMUM, queue_address_,
                        size_, NULL, &queue_rsrc);
  if (device_status != DEVICE_STATUS_SUCCESS)
    throw hcs::hsa_exception(ERROR_OUT_OF_RESOURCES,
                             "Queue create failed at hsaKmtCreateQueue\n");

  signal_.legacy_hardware_doorbell_ptr = (volatile uint32_t*)queue_rsrc.Queue_DoorBell;

  queue_id_ = queue_rsrc.QueueId;
  MAKE_NAMED_SCOPE_GUARD(QueueGuard, [&]() { DeviceDestroyQueue(queue_id_); });

  MAKE_NAMED_SCOPE_GUARD(EventGuard, [&]() {
    ScopedAcquire<KernelMutex> _lock(&queue_lock_);
    queue_count_--;
    if (queue_count_ == 0) {
      core::IInterruptSignal::DestroyEvent(queue_event_);
      queue_event_ = nullptr;
    }
  });

  MAKE_NAMED_SCOPE_GUARD(SignalGuard, [&]() {
    hsa_signal_destroy(amd_queue_.queue_inactive_signal);
  });

  if (core::Ig_use_interrupt_wait) {
    ScopedAcquire<KernelMutex> _lock(&queue_lock_);
    queue_count_++;
    if (queue_event_ == nullptr) {
      assert(queue_count_ == 1 && "Inconsistency in queue event reference counting found.\n");

      queue_event_ = core::InterruptSignal::CreateEvent(HSA_EVENTTYPE_SIGNAL, false);
      if (queue_event_ == nullptr)
        throw hcs::hsa_exception(ERROR_OUT_OF_RESOURCES,
                                 "Queue event creation failed.\n");
    }
    auto Signal = new core::InterruptSignal(0, queue_event_);
    assert(Signal != nullptr && "Should have thrown!\n");
    amd_queue_.queue_inactive_signal = core::IInterruptSignal::Handle(Signal);
  } else {
    EventGuard.Dismiss();
    auto Signal = new core::DefaultSignal(0);
    assert(Signal != nullptr && "Should have thrown!\n");
    amd_queue_.queue_inactive_signal = core::IDefaultSignal::Handle(Signal);
  }

  // Initialize scratch memory related entities
  queue_scratch_.queue_retry = amd_queue_.queue_inactive_signal;
  //InitScratchSRD();
  /*
  if (hsa_amd_signal_async_handler(amd_queue_.queue_inactive_signal, HSA_SIGNAL_CONDITION_NE,
                                        0, DynamicScratchHandler, this) != SUCCESS)
    throw hcs::hsa_exception(ERROR_OUT_OF_RESOURCES,
                             "Queue event handler failed registration.\n");
                             */

  active_ = true;

  bufferGuard.Dismiss();
  QueueGuard.Dismiss();
  EventGuard.Dismiss();
  SignalGuard.Dismiss();
}

CommandQueue::~CommandQueue() {
  // Remove error handler synchronously.
  // Sequences error handler callbacks with queue destroy.
  /* 
  dynamicScratchState |= ERROR_HANDLER_TERMINATE;
  HSA::hsa_signal_store_screlease(amd_queue_.queue_inactive_signal, 0x8000000000000000ull);
  while ((dynamicScratchState & ERROR_HANDLER_DONE) != ERROR_HANDLER_DONE) {
    hsa_signal_wait_relaxed(amd_queue_.queue_inactive_signal, HSA_SIGNAL_CONDITION_NE,
                                 0x8000000000000000ull, -1ull, HSA_WAIT_STATE_BLOCKED);
    hsa_signal_store_relaxed(amd_queue_.queue_inactive_signal, 0x8000000000000000ull);
  }
  */

  // Prevent further use of the queue
  Inactivate();

  // agent_->ReleaseQueueScratch(queue_scratch_);
  hsa_signal_destroy(amd_queue_.queue_inactive_signal);

  if (core::Ig_use_interrupt_wait) {
    ScopedAcquire<KernelMutex> lock(&queue_lock_);
    queue_count_--;
    if (queue_count_ == 0) {
      core::IInterruptSignal::DestroyEvent(queue_event_);
      queue_event_ = nullptr;
    }
  }

  // Check for no AQL queue (indicates abort of queue creation).
  if (!IsValid()) return;

  // drain hw queue
  // FlushDevice();

  // close hw queue.
  /*
  hsaKmtDestroyQueue(queue_resource_.QueueId);
  hsaKmtUnmapMemoryToGPU(queue_address_);
  hsaKmtFreeMemory(queue_address_, kCBUFQueueSize);
  */

  // Release AQL queue
  g_queue_deallocator(queue_address_);

  queue_address_ = NULL;

  if (amd_queue_.hsa_queue.type == HSA_QUEUE_TYPE_COOPERATIVE) {
    // agent_->GWSRelease();
    return;
  }
}

void CommandQueue::Suspend() {
  suspended_ = true;
  /*
  auto err = hsaKmtUpdateQueue(queue_id_, 0, priority_, ring_buf_, ring_buf_alloc_bytes_, NULL);
  assert(err == HSAKMT_STATUS_SUCCESS && "hsaKmtUpdateQueue failed.");
  */
}

status_t CommandQueue::Inactivate() {
  bool active = active_.exchange(false, std::memory_order_relaxed);
  if (active) {
    /*
    auto err = hsaKmtDestroyQueue(queue_id_);
    assert(err == HSAKMT_STATUS_SUCCESS && "hsaKmtDestroyQueue failed.");
    */
    atomic::Fence(std::memory_order_acquire);
  }
  return SUCCESS;
}

status_t CommandQueue::SetPriority(HSA_QUEUE_PRIORITY priority) {
   if (suspended_) {
      return ERROR_INVALID_QUEUE;
   }

   priority_ = priority;
   /*
   auto err = hsaKmtUpdateQueue(queue_id_, 100, priority_, ring_buf_, ring_buf_alloc_bytes_, NULL);
   return (err == HSAKMT_STATUS_SUCCESS ? SUCCESS : ERROR_OUT_OF_RESOURCES);
   */
   return SUCCESS;
}

/*
bool CommandQueue::DynamicScratchHandler(signal_value_t error_code, void* arg) {
  CommandQueue* queue = (CommandQueue*)arg;
  status_t errorCode = SUCCESS;
  bool fatal = false;
  bool changeWait = false;
  signal_value_t waitVal;

  if ((queue->dynamicScratchState & ERROR_HANDLER_SCRATCH_RETRY) == ERROR_HANDLER_SCRATCH_RETRY) {
    queue->dynamicScratchState &= ~ERROR_HANDLER_SCRATCH_RETRY;
    changeWait = true;
    waitVal = 0;
    hcs::hsa_signal_and_relaxed(queue->amd_queue_.queue_inactive_signal, ~0x8000000000000000ull);
    error_code &= ~0x8000000000000000ull;
  }

  // Process errors only if queue is not terminating.
  if ((queue->dynamicScratchState & ERROR_HANDLER_TERMINATE) != ERROR_HANDLER_TERMINATE) {
    if (error_code == 512) {  // Large scratch reclaim
      auto& scratch = queue->queue_scratch_;
      queue->agent_->ReleaseQueueScratch(scratch);
      scratch.queue_base = nullptr;
      scratch.size = 0;
      scratch.size_per_thread = 0;
      scratch.queue_process_offset = 0;
      queue->InitScratchSRD();

      HSA::hsa_signal_store_relaxed(queue->amd_queue_.queue_inactive_signal, 0);
      // Resumes queue processing.
      atomic::Store(&queue->amd_queue_.queue_properties,
                    queue->amd_queue_.queue_properties & (~AMD_QUEUE_PROPERTIES_USE_SCRATCH_ONCE),
                    std::memory_order_release);
      atomic::Fence(std::memory_order_release);
      return true;
    }

    // Process only one queue error.
    if (error_code & 0x401) {  // insufficient scratch, wave64 or wave32
      // Insufficient scratch - recoverable, don't process dynamic scratch if errors are present.
      auto& scratch = queue->queue_scratch_;

      queue->agent_->ReleaseQueueScratch(scratch);

      uint64_t pkt_slot_idx =
          queue->amd_queue_.read_dispatch_id & (queue->amd_queue_.hsa_queue.size - 1);

      core::IAqlPacket& pkt =
          ((core::IAqlPacket*)queue->amd_queue_.hsa_queue.base_address)[pkt_slot_idx];

      uint32_t scratch_request = pkt.dispatch.private_segment_size;

      const uint32_t MaxScratchSlots =
          (queue->amd_queue_.max_cu_id + 1) * queue->agent_->properties().MaxSlotsScratchCU;

      scratch.size_per_thread = scratch_request;
      scratch.lanes_per_wave = (error_code & 0x400) ? 32 : 64;
      // Align whole waves to 1KB.
      scratch.size_per_thread = AlignUp(scratch.size_per_thread, 1024 / scratch.lanes_per_wave);
      scratch.size = scratch.size_per_thread * MaxScratchSlots * scratch.lanes_per_wave;
#ifndef NDEBUG
      scratch.wanted_slots = ((uint64_t(pkt.dispatch.grid_size_x) * pkt.dispatch.grid_size_y) *
                              pkt.dispatch.grid_size_z) / scratch.lanes_per_wave;
      scratch.wanted_slots = Min(scratch.wanted_slots, uint64_t(MaxScratchSlots));
#endif

      queue->agent_->AcquireQueueScratch(scratch);

      if (scratch.retry) {
        queue->dynamicScratchState |= ERROR_HANDLER_SCRATCH_RETRY;
        changeWait = true;
        waitVal = error_code;
      } else {
        // Out of scratch - promote error
        if (scratch.queue_base == nullptr) {
          errorCode = ERROR_OUT_OF_RESOURCES;
        } else {
          // Mark large scratch allocation for single use.
          if (scratch.large) {
            queue->amd_queue_.queue_properties |= AMD_QUEUE_PROPERTIES_USE_SCRATCH_ONCE;
            // Set system release fence to flush scratch stores with older firmware versions.
            if ((queue->agent_->isa()->GetMajorVersion() == 8) &&
                (queue->agent_->GetMicrocodeVersion() < 729)) {
              pkt.dispatch.header &= ~(((1 << HSA_PACKET_HEADER_WIDTH_SCRELEASE_FENCE_SCOPE) - 1)
                                       << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);
              pkt.dispatch.header |=
                  (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);
            }
          }
          // Reset scratch memory related entities for the queue
          queue->InitScratchSRD();
          // Restart the queue.
          HSA::hsa_signal_store_screlease(queue->amd_queue_.queue_inactive_signal, 0);
        }
      }

    } else if ((error_code & 2) == 2) {  // Invalid dim
      errorCode = ERROR_INCOMPATIBLE_ARGUMENTS;

    } else if ((error_code & 4) == 4) {  // Invalid group memory
      errorCode = ERROR_INVALID_ALLOCATION;

    } else if ((error_code & 8) == 8) {  // Invalid (or NULL) code
      errorCode = ERROR_INVALID_CODE_OBJECT;

    } else if (((error_code & 32) == 32) ||    // Invalid format: 32 is generic,
               ((error_code & 256) == 256)) {  // 256 is vendor specific packets
      errorCode = ERROR_INVALID_PACKET_FORMAT;

    } else if ((error_code & 64) == 64) {  // Group is too large
      errorCode = ERROR_INVALID_ARGUMENT;

    } else if ((error_code & 128) == 128) {  // Out of VGPRs
      errorCode = ERROR_INVALID_ISA;

    } else if ((error_code & 0x20000000) == 0x20000000) {  // Memory violation (>48-bit)
      errorCode = status_t(ERROR_MEMORY_APERTURE_VIOLATION);

    } else if ((error_code & 0x40000000) == 0x40000000) {  // Illegal instruction
      errorCode = status_t(ERROR_ILLEGAL_INSTRUCTION);

    } else if ((error_code & 0x80000000) == 0x80000000) {  // Debug trap
      errorCode = ERROR_EXCEPTION;
      fatal = true;

    } else {  // Undefined code
      assert(false && "Undefined queue error code");
      errorCode = ERROR;
      fatal = true;
    }

    if (errorCode == SUCCESS) {
      if (changeWait) {
        core::IRuntime::runtime_singleton_->SetAsyncSignalHandler(
            queue->amd_queue_.queue_inactive_signal, HSA_SIGNAL_CONDITION_NE, waitVal,
            DynamicScratchHandler, queue);
        return false;
      }
      return true;
    }

    queue->Suspend();
    if (queue->errors_callback_ != nullptr) {
      queue->errors_callback_(errorCode, queue->public_handle(), queue->errors_data_);
    }
    if (fatal) {
      // Temporarilly removed until there is clarity on exactly what debugtrap's semantics are.
      // assert(false && "Fatal queue error");
      // std::abort();
    }
  }
  // Copy here is to protect against queue being released between setting the scratch state and
  // updating the signal value.  The signal itself is safe to use because it is ref counted rather
  // than being released with the queue.
  signal_t signal = queue->amd_queue_.queue_inactive_signal;
  queue->dynamicScratchState = ERROR_HANDLER_DONE;
  HSA::hsa_signal_store_screlease(signal, -1ull);
  return false;
}
*/

status_t CommandQueue::SetCUMasking(const uint32_t num_cu_mask_count, const uint32_t* cu_mask) {
    /*
   HSAKMT_STATUS ret = hsaKmtSetQueueCUMask( queue_id_, num_cu_mask_count, reinterpret_cast<HSAuint32*>(const_cast<uint32_t*>(cu_mask)));
    return (HSAKMT_STATUS_SUCCESS == ret) ? SUCCESS : ERROR;
    */
    return SUCCESS;
}

// @brief Define the Scratch Buffer Descriptor and related parameters
// that enable kernel access scratch memory
/* 
void CommandQueue::InitScratchSRD() {

  // Populate scratch resource descriptor
  SQ_BUF_RSRC_WORD0 srd0;
  SQ_BUF_RSRC_WORD1 srd1;
  SQ_BUF_RSRC_WORD2 srd2;
  uint32_t srd3_u32;

  uint32_t scratch_base_hi = 0;
  uintptr_t scratch_base = uintptr_t(queue_scratch_.queue_base);
  #ifdef HSA_LARGE_MODEL
  scratch_base_hi = uint32_t(scratch_base >> 32);
  #endif
  srd0.bits.BASE_ADDRESS = uint32_t(scratch_base);

  srd1.bits.BASE_ADDRESS_HI = scratch_base_hi;
  srd1.bits.STRIDE = 0;
  srd1.bits.CACHE_SWIZZLE = 0;
  srd1.bits.SWIZZLE_ENABLE = 1;

  srd2.bits.NUM_RECORDS = uint32_t(queue_scratch_.size);

  SQ_BUF_RSRC_WORD3_GFX10 srd3;

  srd3.bits.DST_SEL_X = SQ_SEL_X;
  srd3.bits.DST_SEL_Y = SQ_SEL_Y;
  srd3.bits.DST_SEL_Z = SQ_SEL_Z;
  srd3.bits.DST_SEL_W = SQ_SEL_W;
  srd3.bits.FORMAT = BUF_FORMAT_32_UINT;
  srd3.bits.RESERVED1 = 0;
  srd3.bits.INDEX_STRIDE = 0;  // filled in by CP
  srd3.bits.ADD_TID_ENABLE = 1;
  srd3.bits.RESOURCE_LEVEL = 1;
  srd3.bits.RESERVED2 = 0;
  srd3.bits.OOB_SELECT = 2;  // no bounds check in swizzle mode
  srd3.bits.TYPE = SQ_RSRC_BUF;

  srd3_u32 = srd3.u32All;

  // Update Queue's Scratch descriptor's property
  amd_queue_.scratch_resource_descriptor[0] = srd0.u32All;
  amd_queue_.scratch_resource_descriptor[1] = srd1.u32All;
  amd_queue_.scratch_resource_descriptor[2] = srd2.u32All;
  amd_queue_.scratch_resource_descriptor[3] = srd3_u32;

  // Populate flat scratch parameters in amd_queue_.
  amd_queue_.scratch_backing_memory_location = queue_scratch_.queue_process_offset;
  amd_queue_.scratch_backing_memory_byte_size = queue_scratch_.size;

  // For backwards compatibility this field records the per-lane scratch
  // for a 64 lane wavefront. If scratch was allocated for 32 lane waves
  // then the effective size for a 64 lane wave is halved.
  amd_queue_.scratch_wave64_lane_byte_size = uint32_t((queue_scratch_.size_per_thread * queue_scratch_.lanes_per_wave) / 64);

  // Set concurrent wavefront limits only when scratch is being used.
  COMPUTE_TMPRING_SIZE tmpring_size = {};
  if (queue_scratch_.size == 0) {
    amd_queue_.compute_tmpring_size = tmpring_size.u32All;
    return;
  }

  // Determine the maximum number of waves device can support
  const auto& agent_props = agent_->properties();
  uint32_t num_cus = agent_props.NumFComputeCores / agent_props.NumSIMDPerCU;
  uint32_t max_scratch_waves = num_cus * agent_props.MaxSlotsScratchCU;

  // Scratch is allocated program COMPUTE_TMPRING_SIZE register
  // Scratch Size per Wave is specified in terms of kilobytes
  uint32_t wave_scratch = (((queue_scratch_.lanes_per_wave *
                               queue_scratch_.size_per_thread) + 1023) / 1024);
  tmpring_size.bits.WAVESIZE = wave_scratch;
  assert(wave_scratch == tmpring_size.bits.WAVESIZE && "WAVESIZE Overflow.");
  uint32_t num_waves = (queue_scratch_.size / (tmpring_size.bits.WAVESIZE * 1024));
  tmpring_size.bits.WAVES = std::min(num_waves, max_scratch_waves);
  amd_queue_.compute_tmpring_size = tmpring_size.u32All;
  return;
}
*/

/*
status_t CommandQueue::EnableGWS(int gws_slot_count) {
  uint32_t discard;
  auto status = hsaKmtAllocQueueGWS(queue_id_, gws_slot_count, &discard);
  if (status != HSAKMT_STATUS_SUCCESS) return ERROR_OUT_OF_RESOURCES;
  amd_queue_.hsa_queue.type = HSA_QUEUE_TYPE_COOPERATIVE;
  return SUCCESS;
}
*/


}  // namespace hcs
