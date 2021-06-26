#pragma once

#include <stdint.h>

#include <string>

#include "util/os.h"
#include "util/utils.h"

class Flag {
 public:
  explicit Flag() { Refresh(); }

  virtual ~Flag() {}

  void Refresh() {
    std::string var = os::GetEnvVar("HSA_CHECK_FLAT_SCRATCH");
    check_flat_scratch_ = (var == "1") ? true : false;

    var = os::GetEnvVar("HSA_ENABLE_VM_FAULT_MESSAGE");
    enable_vm_fault_message_ = (var == "0") ? false : true;

    var = os::GetEnvVar("HSA_ENABLE_QUEUE_FAULT_MESSAGE");
    enable_queue_fault_message_ = (var == "0") ? false : true;

    var = os::GetEnvVar("HSA_ENABLE_INTERRUPT");
    enable_interrupt_ = (var == "1") ? true : false;

    enable_sdma_ = os::GetEnvVar("HSA_ENABLE_SDMA");

    var = os::GetEnvVar("HSA_RUNNING_UNDER_VALGRIND");
    running_valgrind_ = (var == "1") ? true : false;

    var = os::GetEnvVar("HSA_SDMA_WAIT_IDLE");
    sdma_wait_idle_ = (var == "1") ? true : false;

    var = os::GetEnvVar("HSA_MAX_QUEUES");
    max_queues_ = static_cast<uint32_t>(atoi(var.c_str()));

    var = os::GetEnvVar("HSA_SCRATCH_MEM");
    scratch_mem_size_ = atoi(var.c_str());

    tools_lib_names_ = os::GetEnvVar("HSA_TOOLS_LIB");

    var = os::GetEnvVar("HSA_TOOLS_REPORT_LOAD_FAILURE");
#ifdef NDEBUG
    report_tool_load_failures_ = (var == "1") ? true : false;
#else
    report_tool_load_failures_ = (var == "0") ? false : true;
#endif

    var = os::GetEnvVar("HSA_DISABLE_FRAGMENT_ALLOCATOR");
    disable_fragment_alloc_ = (var == "1") ? true : false;

    var = os::GetEnvVar("HSA_ENABLE_SDMA_HDP_FLUSH");
    enable_sdma_hdp_flush_ = (var == "0") ? false : true;
  }

  bool check_flat_scratch() const { return check_flat_scratch_; }

  bool enable_vm_fault_message() const { return enable_vm_fault_message_; }

  bool enable_queue_fault_message() const { return enable_queue_fault_message_; }

  bool enable_interrupt() const { return enable_interrupt_; }

  bool enable_sdma_hdp_flush() const { return enable_sdma_hdp_flush_; }

  bool running_valgrind() const { return running_valgrind_; }

  bool sdma_wait_idle() const { return sdma_wait_idle_; }

  bool report_tool_load_failures() const { return report_tool_load_failures_; }

  bool disable_fragment_alloc() const { return disable_fragment_alloc_; }

  std::string enable_sdma() const { return enable_sdma_; }

  uint32_t max_queues() const { return max_queues_; }

  size_t scratch_mem_size() const { return scratch_mem_size_; }

  std::string tools_lib_names() const { return tools_lib_names_; }

 private:
  bool check_flat_scratch_;
  bool enable_vm_fault_message_;
  bool enable_interrupt_;
  bool enable_sdma_hdp_flush_;
  bool running_valgrind_;
  bool sdma_wait_idle_;
  bool enable_queue_fault_message_;
  bool report_tool_load_failures_;
  bool disable_fragment_alloc_;

  std::string enable_sdma_;

  uint32_t max_queues_;

  size_t scratch_mem_size_;

  std::string tools_lib_names_;

  DISALLOW_COPY_AND_ASSIGN(Flag);
};
