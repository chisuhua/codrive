#pragma once

#include <map>
#include <mutex>
#include <stdint.h>

#include "inc/blit.h"

namespace hcs {
class BlitCpDma : public core::IBlit {
 public:
  explicit BlitCpDma(core::IQueue* queue);
  virtual ~BlitCpDma() override;

  /// @brief Initialize a blit kernel object.
  ///
  /// @param agent Pointer to the agent that will execute the AQL packets.
  ///
  /// @return status_t
  virtual status_t Initialize(const core::IAgent& agent) override;

  /// @brief Marks the blit kernel object as invalid and uncouples its link with
  /// the underlying AQL kernel queue. Use of the blit object
  /// once it has been release is illegal and any behavior is indeterminate
  ///
  /// @note: The call will block until all AQL packets have been executed.
  ///
  /// @param agent Agent passed to Initialize.
  ///
  /// @return status_t
  virtual status_t Destroy(const core::IAgent& agent) override;

  /// @brief Submit an AQL packet to perform vector copy. The call is blocking
  /// until the command execution is finished.
  ///
  /// @param dst Memory address of the copy destination.
  /// @param src Memory address of the copy source.
  /// @param size Size of the data to be copied.
  virtual status_t SubmitLinearCopyCommand(void* dst, const void* src,
                                               size_t size) override;

  /// @brief Submit a linear copy command to the the underlying compute device's
  /// control block. The call is non blocking. The memory transfer will start
  /// after all dependent signals are satisfied. After the transfer is
  /// completed, the out signal will be decremented.
  ///
  /// @param dst Memory address of the copy destination.
  /// @param src Memory address of the copy source.
  /// @param size Size of the data to be copied.
  /// @param dep_signals Arrays of dependent signal.
  /// @param out_signal Output signal.
  virtual status_t SubmitLinearCopyCommand(
      void* dst, const void* src, size_t size,
      std::vector<core::ISignal*>& dep_signals,
      core::ISignal& out_signal ) override;

  /// @brief Submit an AQL packet to perform memory fill. The call is blocking
  /// until the command execution is finished.
  ///
  /// @param ptr Memory address of the fill destination.
  /// @param value Value to be set.
  /// @param count Number of uint32_t element to be set to the value.
  virtual status_t SubmitLinearFillCommand(void* ptr, uint32_t value,
                                               size_t count) override;

  virtual status_t EnableProfiling(bool enable) override;

 private:
  /// Reserve a slot in the queue buffer. The call will wait until the queue
  /// buffer has a room.
  uint64_t AcquireWriteIndex(uint32_t num_packet);

  /// Update the queue doorbell register with ::write_index. This
  /// function also serializes concurrent doorbell update to ensure that the
  /// packet processor doesn't get invalid packet.
  void ReleaseWriteIndex(uint64_t write_index, uint32_t num_packet);

  void PopulateQueue(uint64_t index, void* dst, const void* src,
                     uint64_t bytes, signal_t completion_signal);

  /// AQL queue for submitting the vector copy kernel.
  core::IQueue* queue_;
  uint32_t queue_bitmask_;

  /// Completion signal for every kernel dispatched.
  signal_t completion_signal_;

  /// Lock to synchronize access to kernarg_ and completion_signal_
  std::mutex lock_;

  /// Number of CUs on the underlying agent.
  int num_cus_;
};
}  // namespace amd

