#pragma once
#include <stdint.h>

#include "inc/agent.h"

namespace core {
class Blit {
 public:
  explicit Blit() {}
  virtual ~Blit() {}

  /// @brief Initialize a blit object.
  ///
  /// @param agent Pointer to the agent that will execute the blit commands.
  ///
  /// @return status_t
  virtual status_t Initialize(const core::IAgent& agent) = 0;

  /// @brief Marks the blit object as invalid and uncouples its link with
  /// the underlying compute device's control block. Use of blit object
  /// once it has been release is illegal and any behavior is indeterminate
  ///
  /// @note: The call will block until all commands have executed.
  ///
  /// @param agent Agent passed to Initialize.
  ///
  /// @return status_t
  virtual status_t Destroy(const core::IAgent& agent) = 0;

  /// @brief Submit a linear copy command to the the underlying compute device's
  /// control block. The call is blocking until the command execution is
  /// finished.
  ///
  /// @param dst Memory address of the copy destination.
  /// @param src Memory address of the copy source.
  /// @param size Size of the data to be copied.
  virtual status_t SubmitLinearCopyCommand(void* dst, const void* src,
                                               size_t size) = 0;

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
      std::vector<core::ISignal*>& dep_signals, core::Signal& out_signal) = 0;

  /// @brief Submit a linear fill command to the the underlying compute device's
  /// control block. The call is blocking until the command execution is
  /// finished.
  ///
  /// @param ptr Memory address of the fill destination.
  /// @param value Value to be set.
  /// @param num Number of uint32_t element to be set to the value.
  virtual status_t SubmitLinearFillCommand(void* ptr, uint32_t value,
                                               size_t num) = 0;

  /// @brief Enable profiling of the asynchronous copy command. The timestamp
  /// of each copy request will be stored in the completion signal structure.
  ///
  /// @param enable True to enable profiling. False to disable profiling.
  ///
  /// @return SUCCESS if the request to enable/disable profiling is
  /// successful.
  virtual status_t EnableProfiling(bool enable) = 0;

  /// @brief Blit operations use SDMA.
  virtual bool isSDMA() const { return false; }

};
}  // namespace core

