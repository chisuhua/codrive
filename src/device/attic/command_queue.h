#pragma once

#include "inc/hsakmt.h"

#include "inc/platform.h"
#include "inc/signal.h"
#include "inc/queue.h"
#include "inc/ppu_agent.h"
#include "inc/pps_ext.h"
#include "inc/device_tools.h"

using namespace hcs;
namespace device {

/// It work as HCS packet processor for things such as Doorbell register, read, write index
/// and a buffer. signal_send for doorbell ring to handle any AQL processor deficiencies.
class CommandQueue : public core::IQueue,
                              private core::ILocalSignal,
                              public core::IDoorbellSignal {
 public:
  static const uint64_t key = 0xB4C312A4ull;

  /// @param agent Handle of HCS device which will execute commands
  /// @param ring_size Size of queue in terms of Aql packets
  /// @param node Id of HCS device used in communication with thunk calls
  /// @param props Properties of HCS device used in communication wih thunk
  /// calls
  /// @param type Type of queue to encapsulate
  CommandQueue(GpuDevice* agent, uint32_t ring_size, HSAuint32 node,
                        const HsaCoreProperties* props, queue_type32_t type,
                        ScratchInfo& scratch, core::IHsaEventCallback callback, void* user_data, uint32_t asic_id);

  /// @brief Release the resources of Aql processor
  ~CommandQueue();

  /// @brief Determines if queue buffer is valid
  bool IsValid() const { return queue_address_ != NULL; }

  /// @brief Queue interface to inactivate the queue
  status_t Inactivate();

  /// @brief Change the scheduling priority of the queue
  status_t SetPriority(HSA_QUEUE_PRIORITY priority) override;

  uint64_t LoadReadIndexRelaxed() {
    return atomic::Load(&amd_queue_.read_dispatch_id, std::memory_order_relaxed);
  }
  uint64_t LoadReadIndexAcquire() {
    return atomic::Load(&amd_queue_.read_dispatch_id, std::memory_order_acquire);
  }
  uint64_t LoadWriteIndexRelaxed() {
    return atomic::Load(&amd_queue_.write_dispatch_id, std::memory_order_relaxed);
  }
  uint64_t LoadWriteIndexAcquire() {
    return atomic::Load(&amd_queue_.write_dispatch_id, std::memory_order_acquire);
  }
  void StoreReadIndexRelaxed(uint64_t value) { assert(false); }
  void StoreReadIndexRelease(uint64_t value) { assert(false); }
  void StoreWriteIndexRelaxed(uint64_t value) {
    atomic::Store(&amd_queue_.write_dispatch_id, value, std::memory_order_relaxed);
  }
  void StoreWriteIndexRelease(uint64_t value) {
    atomic::Store(&amd_queue_.write_dispatch_id, value, std::memory_order_release);
  }
  uint64_t CasWriteIndexAcqRel(uint64_t expected, uint64_t value) {
    return atomic::Cas(&amd_queue_.write_dispatch_id, value, expected,
                       std::memory_order_acq_rel);
  }
  uint64_t CasWriteIndexAcquire(uint64_t expected, uint64_t value) {
    return atomic::Cas(&amd_queue_.write_dispatch_id, value, expected,
                       std::memory_order_acquire);
  }
  uint64_t CasWriteIndexRelaxed(uint64_t expected, uint64_t value) {
    return atomic::Cas(&amd_queue_.write_dispatch_id, value, expected,
                       std::memory_order_relaxed);
  }
  uint64_t CasWriteIndexRelease(uint64_t expected, uint64_t value) {
    return atomic::Cas(&amd_queue_.write_dispatch_id, value, expected,
                       std::memory_order_release);
  }
  uint64_t AddWriteIndexAcqRel(uint64_t value) {
    return atomic::Add(&amd_queue_.write_dispatch_id, value,
                       std::memory_order_acq_rel);
  }
  uint64_t AddWriteIndexAcquire(uint64_t value) {
    return atomic::Add(&amd_queue_.write_dispatch_id, value,
                       std::memory_order_acquire);
  }
  uint64_t AddWriteIndexRelaxed(uint64_t value) {
    return atomic::Add(&amd_queue_.write_dispatch_id, value,
                       std::memory_order_relaxed);
  }
  uint64_t AddWriteIndexRelease(uint64_t value) {
    return atomic::Add(&amd_queue_.write_dispatch_id, value,
                       std::memory_order_release);
  }

  // should we need it? FIXME on doorbell signal write
  void StoreRelaxed(signal_value_t value) {
    if (doorbell_type_ == 2) {
        // Hardware doorbell supports AQL semantics.
        atomic::Store(signal_.hardware_doorbell_ptr, uint64_t(value), std::memory_order_release);
        //atomic::Store(&signal_.value, int64_t(value), std::memory_order_relaxed);
        return;
    }

    // Acquire spinlock protecting the legacy doorbell.
    while (atomic::Cas(&amd_queue_.legacy_doorbell_lock, 1U, 0U,
                     std::memory_order_acquire) != 0) {
        os::YieldThread();
    }

#ifdef HSA_LARGE_MODEL
  // AMD hardware convention expects the packet index to point beyond
  // the last packet to be processed. Packet indices written to the
  // max_legacy_doorbell_dispatch_id_plus_1 field must conform to this
  // expectation, since this field is used as the HW-visible write index.
  uint64_t legacy_dispatch_id = value + 1;
#else
  // In the small machine model it is difficult to distinguish packet index
  // wrap at 2^32 packets from a backwards doorbell. Instead, ignore the
  // doorbell value and submit the write index instead. It is OK to issue
  // a doorbell for packets in the INVALID or ALWAYS_RESERVED state.
  // The HW will stall on these packets until they enter a valid state.
  uint64_t legacy_dispatch_id = amd_queue_.write_dispatch_id;

  // The write index may extend more than a full queue of packets beyond
  // the read index. The hardware can process at most a full queue of packets
  // at a time. Clamp the write index appropriately. A doorbell for the
  // remaining packets is guaranteed to be sent at a later time.
  legacy_dispatch_id =
      Min(legacy_dispatch_id,
          uint64_t(amd_queue_.read_dispatch_id) + amd_queue_.hsa_queue.size);
#endif

    // Discard backwards and duplicate doorbells.
    if (legacy_dispatch_id > amd_queue_.max_legacy_doorbell_dispatch_id_plus_1) {
      // Record the most recent packet index used in a doorbell submission.
      // This field will be interpreted as a write index upon HW queue connect.
      // Make ring buffer visible to HW before updating write index.
      atomic::Store(&amd_queue_.max_legacy_doorbell_dispatch_id_plus_1,
                    legacy_dispatch_id, std::memory_order_release);

      // Write the dispatch id to the hardware MMIO doorbell.
      // Make write index visible to HW before sending doorbell.
      if (doorbell_type_ == 0) {
#if 0
        // The legacy GFXIP 7 hardware doorbell expects:
        //   1. Packet index wrapped to a point within the ring buffer
        //   2. Packet index converted to DWORD count
        uint64_t queue_size_mask =
            ((1 + queue_full_workaround_) * amd_queue_.hsa_queue.size) - 1;

        atomic::Store(signal_.legacy_hardware_doorbell_ptr,
                      uint32_t((legacy_dispatch_id & queue_size_mask) *
                               (sizeof(core::IAqlPacket) / sizeof(uint32_t))),
                      std::memory_order_release);
#endif
      } else if (doorbell_type_ == 1) {
        atomic::Store(signal_.legacy_hardware_doorbell_ptr,
                      uint32_t(legacy_dispatch_id), std::memory_order_release);
      } else {
        assert(false && "Agent has unsupported doorbell semantics");
      }
    }
    // Release spinlock protecting the legacy doorbell.
    // Also ensures timely delivery of (write-combined) doorbell to HW.
    atomic::Store(&amd_queue_.legacy_doorbell_lock, 0U,
                std::memory_order_release);
    // atomic::Store(&signal_.value, int64_t(value), std::memory_order_relaxed);

  };
  void StoreRelease(signal_value_t value) {
    std::atomic_thread_fence(std::memory_order_release);
    StoreRelaxed(value);
  }

  // void ExecuteCBUF(uint32_t* cmd_data, size_t cmd_size_b) override {
  //   assert(false && "HostQueue::ExecutePM4 is unimplemented");
  // }

  // Useful for lightweight RTTI
  static uint32_t GetID(CommandQueue* queue) {
    return uint32_t(key ^ uint64_t(uintptr_t((Queue*)queue)));
  }

  static bool IsType(core::ISignal* signal) { return signal->IsType(&rtti_id_); }
  static bool IsType(core::IQueue* queue) { return queue->IsType(&rtti_id_); }

  // GpuDevice* GpuDevice() const { return agent_; }
  GpuDevice* GetGpuAgent() const
  {
      return agent_;
  }

/*
  void InitScratchSRD() ;
  */

  void Suspend();

 protected:
  bool _IsA(Queue::rtti_t id) const { return id == &rtti_id_; }

  static int rtti_id_;
  static int queue_cnt_;

  // Size of buffer used to build Barrier command plus one.
  // This size is reserved in the queue buffer
  static const uint32_t kBarrierWords = 9;

 public:

  status_t SetCUMasking(const uint32_t num_cu_mask_count, const uint32_t* cu_mask) override;

  // Type of queue objects it will support
  const uint32_t queue_type_;

  // Scratch memory block descriptor
  // ScratchInfo queue_scratch_;
  // Id of the Queue used in communication with thunk
  HSA_QUEUEID queue_id_;

  // Indicates if queue is active/inactive
  std::atomic<bool> active_;

  // Cached value of HsaCoreProperties.HSA_CAPABILITY.DoorbellType
  int doorbell_type_;

  // Handle of the HCS device that will execute Aql commands
  // GpuDevice* const agent_;
  GpuDevice * agent_;

  // Index for the queue object
  uint64_t queue_index_;

  // Handle of scratch memory descriptor
  ScratchInfo queue_scratch_;

  // Index for the next Aql packet
  uint64_t next_index_;

  // Size of queue in terms of Aql Dispatch packets
  uint32_t size_;

  // Id of HCS device used in communication with Thunk
  HSAuint32 node_id_;

  // For scratchPool cleanup
  uint64_t kernels_launched_;

  // Tracks need for barrier bit based on barrier
  // in signal implementation.
  bool needs_barrier_;

  // Queue currently suspended or scheduled
  bool suspended_;

  // Thunk dispatch and wavefront scheduling priority
  HSA_QUEUE_PRIORITY priority_;

  // Shared event used for queue errors
  static HsaEvent* queue_event_;

  // Queue count - used to ref count queue_event_
  static std::atomic<uint32_t> queue_count_;

  //Async errors callback
  core::IHsaEventCallback errors_callback_;

  //Async errors user data
  void* errors_data_;

  // Base address of queue used to enqueue Aql commands
  // core::IAqlPacket* buffer_;

  // Used to serialize accesses
  static KernelMutex queue_lock_;

  // Base address of queue buffer
  core::IAqlPacket* queue_address_;

  // Forbid copying and moving of this object
  DISALLOW_COPY_AND_ASSIGN(CommandQueue);
};
}  // namespace device

