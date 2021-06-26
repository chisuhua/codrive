
#include <algorithm>
#include <sstream>
#include <string>

// #include "inc/ppu_agent.h"
//#include "inc/pps_internal.h"
#include "util/utils.h"
#include "inc/blit_cpdma.h"

BlitCpDma::BlitCpDma(queue_t* queue)
    : core::IBlit(),
      queue_(queue),
      num_cus_(0) {
  completion_signal_.handle = 0;
}

BlitCpDma::~BlitCpDma() {}

// TODO schi, from blit_sdma it will create queue for D2H and H2D individully, 
//            but now we only using compute queue, any fence in queue will block Compute/D2H/H2D cmd
//            so i would like to use multple queue and using software queue processor to process the sig dependency.
status_t BlitCpDma::Initialize(const core::IAgent& agent) {
  // queue_bitmask_ = queue_->public_handle()->size - 1;
  queue_bitmask_ = queue_->size - 1;

  status_t status = hsa_signal_create(1, 0, NULL, &completion_signal_);
  if (SUCCESS != status) {
    return status;
  }

  if (agent.profiling_enabled()) {
    return EnableProfiling(true);
  }

  return SUCCESS;
}

status_t BlitCpDma::Destroy(const core::IAgent& agent) {
  std::lock_guard<std::mutex> guard(lock_);

  if (completion_signal_.handle != 0) {
    hsa_signal_destroy(completion_signal_);
  }

  return SUCCESS;
}

status_t BlitCpDma::SubmitLinearCopyCommand(void* dst, const void* src,
                                                size_t size) {
  // Protect completion_signal_.
  std::lock_guard<std::mutex> guard(lock_);

  // hsa_signal_store_relaxed(completion_signal_, 1);
  Runtime::runtime_singleton_->co_stream_->signal_or_relaxed(competion_signal_, 1);

  std::vector<signal_t> dep_signals(0);

  status_t stat = SubmitLinearCopyCommand(
      dst, src, size, dep_signals, completion_signal_);
      // dst, src, size, dep_signals, *core::ISignal::Object(completion_signal_));

  if (stat != SUCCESS) {
    return stat;
  }

  // Wait for the packet to finish.
  if (hsa_signal_wait_scacquire(completion_signal_, HSA_SIGNAL_CONDITION_LT, 1, uint64_t(-1),
                                     HSA_WAIT_STATE_ACTIVE) != 0) {
    // Signal wait returned unexpected value.
    return ERROR;
  }

  return SUCCESS;
}

status_t BlitCpDma::SubmitLinearCopyCommand(
    void* dst, const void* src, size_t size,
    std::vector<signal_t>& dep_signals, core::Signal& out_signal) {

  std::cout << "BlitCpDma::SubmitLinearCopyCommand" << std::endl;

  uint64_t write_index = AcquireWriteIndex(1);

  PopulateQueue(write_index, dst, src, size, core::ISignal::Handle(&out_signal));

  ReleaseWriteIndex(write_index, 1);

  return SUCCESS;

/*
  // Reserve write index for barrier(s) + dispatch packet.
  const uint32_t num_barrier_packet = uint32_t((dep_signals.size() + 4) / 5);
  const uint32_t total_num_packet = num_barrier_packet + 1;

  uint64_t write_index = AcquireWriteIndex(total_num_packet);
  uint64_t write_index_temp = write_index;

  // Insert barrier packets to handle dependent signals.
  // Barrier bit keeps signal checking traffic from competing with a copy.
  const uint16_t kBarrierPacketHeader = (HSA_PACKET_TYPE_BARRIER_AND << HSA_PACKET_HEADER_TYPE) |
      (1 << HSA_PACKET_HEADER_BARRIER) |
      (HSA_FENCE_SCOPE_NONE << HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE) |
      (HSA_FENCE_SCOPE_AGENT << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);

  hsa_barrier_and_packet_t barrier_packet = {0};
  barrier_packet.header = HSA_PACKET_TYPE_INVALID;

  hsa_barrier_and_packet_t* queue_buffer =
      reinterpret_cast<hsa_barrier_and_packet_t*>(
          queue_->public_handle()->base_address);

  const size_t dep_signal_count = dep_signals.size();
  for (size_t i = 0; i < dep_signal_count; ++i) {
    const size_t idx = i % 5;
    barrier_packet.dep_signal[idx] = core::ISignal::Convert(dep_signals[i]);
    if (i == (dep_signal_count - 1) || idx == 4) {
      std::atomic_thread_fence(std::memory_order_acquire);
      queue_buffer[(write_index)&queue_bitmask_] = barrier_packet;
      std::atomic_thread_fence(std::memory_order_release);
      queue_buffer[(write_index)&queue_bitmask_].header = kBarrierPacketHeader;

      ++write_index;

      memset(&barrier_packet, 0, sizeof(hsa_barrier_and_packet_t));
      barrier_packet.header = HSA_PACKET_TYPE_INVALID;
    }
  }

  // Insert dispatch packet for copy kernel.
  KernelArgs* args = ObtainAsyncKernelCopyArg();
  KernelCode* kernel_code = nullptr;
  int num_workitems = 0;

  bool aligned = ((uintptr_t(src) & 0x3) == (uintptr_t(dst) & 0x3));

  if (aligned) {
    // Use dword-based aligned kernel.
    kernel_code = &kernels_[KernelType::CopyAligned];

    // Compute the size of each copy phase.
    num_workitems = 64 * 4 * num_cus_;

    // Phase 1 (byte copy) ends when destination is 0x100-aligned.
    uintptr_t src_start = uintptr_t(src);
    uintptr_t dst_start = uintptr_t(dst);
    uint64_t phase1_size =
        std::min(size, uint64_t(0x100 - (dst_start & 0xFF)) & 0xFF);

    // Phase 2 (unrolled dwordx4 copy) ends when last whole block fits.
    uint64_t phase2_block = num_workitems * sizeof(uint32_t) *
                            kCopyAlignedUnroll * kCopyAlignedVecWidth;
    uint64_t phase2_size = ((size - phase1_size) / phase2_block) * phase2_block;

    // Phase 3 (dword copy) ends when last whole dword fits.
    uint64_t phase3_size =
        ((size - phase1_size - phase2_size) / sizeof(uint32_t)) *
        sizeof(uint32_t);

    args->copy_aligned.phase1_src_start = src_start;
    args->copy_aligned.phase1_dst_start = dst_start;
    args->copy_aligned.phase2_src_start = src_start + phase1_size;
    args->copy_aligned.phase2_dst_start = dst_start + phase1_size;
    args->copy_aligned.phase3_src_start = src_start + phase1_size + phase2_size;
    args->copy_aligned.phase3_dst_start = dst_start + phase1_size + phase2_size;
    args->copy_aligned.phase4_src_start =
        src_start + phase1_size + phase2_size + phase3_size;
    args->copy_aligned.phase4_dst_start =
        dst_start + phase1_size + phase2_size + phase3_size;
    args->copy_aligned.phase4_src_end = src_start + size;
    args->copy_aligned.phase4_dst_end = dst_start + size;
    args->copy_aligned.num_workitems = num_workitems;
  } else {
    // Use byte-based misaligned kernel.
    kernel_code = &kernels_[KernelType::CopyMisaligned];

    // Compute the size of each copy phase.
    num_workitems = 64 * 4 * num_cus_;

    // Phase 1 (unrolled byte copy) ends when last whole block fits.
    uintptr_t src_start = uintptr_t(src);
    uintptr_t dst_start = uintptr_t(dst);
    uint64_t phase1_block =
        num_workitems * sizeof(uint8_t) * kCopyMisalignedUnroll;
    uint64_t phase1_size = (size / phase1_block) * phase1_block;

    args->copy_misaligned.phase1_src_start = src_start;
    args->copy_misaligned.phase1_dst_start = dst_start;
    args->copy_misaligned.phase2_src_start = src_start + phase1_size;
    args->copy_misaligned.phase2_dst_start = dst_start + phase1_size;
    args->copy_misaligned.phase2_src_end = src_start + size;
    args->copy_misaligned.phase2_dst_end = dst_start + size;
    args->copy_misaligned.num_workitems = num_workitems;
  }

  signal_t signal = {(core::ISignal::Convert(&out_signal)).handle};
  PopulateQueue(write_index, uintptr_t(kernel_code->code_buf_), args,
                num_workitems, signal);

  // Submit barrier(s) and dispatch packets.
  ReleaseWriteIndex(write_index_temp, total_num_packet);

  return SUCCESS;
*/
}

status_t BlitCpDma::SubmitLinearFillCommand(void* ptr, uint32_t value,
                                                 size_t count) {
/*
  std::lock_guard<std::mutex> guard(lock_);

  // Reject misaligned base address.
  if ((uintptr_t(ptr) & 0x3) != 0) {
    return ERROR;
  }

  // Compute the size of each fill phase.
  int num_workitems = 64 * num_cus_;

  // Phase 1 (unrolled dwordx4 copy) ends when last whole block fits.
  uintptr_t dst_start = uintptr_t(ptr);
  uint64_t fill_size = count * sizeof(uint32_t);

  uint64_t phase1_block =
      num_workitems * sizeof(uint32_t) * kFillUnroll * kFillVecWidth;
  uint64_t phase1_size = (fill_size / phase1_block) * phase1_block;

  KernelArgs* args = ObtainAsyncKernelCopyArg();
  args->fill.phase1_dst_start = dst_start;
  args->fill.phase2_dst_start = dst_start + phase1_size;
  args->fill.phase2_dst_end = dst_start + fill_size;
  args->fill.fill_value = value;
  args->fill.num_workitems = num_workitems;

  // Submit dispatch packet.
  HSA::hsa_signal_store_relaxed(completion_signal_, 1);

  uint64_t write_index = AcquireWriteIndex(1);
  PopulateQueue(write_index, uintptr_t(kernels_[KernelType::Fill].code_buf_),
                args, num_workitems, completion_signal_);
  ReleaseWriteIndex(write_index, 1);

  // Wait for the packet to finish.
  if (HSA::hsa_signal_wait_scacquire(completion_signal_, HSA_SIGNAL_CONDITION_LT, 1, uint64_t(-1),
                                     HSA_WAIT_STATE_ACTIVE) != 0) {
    // Signal wait returned unexpected value.
    return ERROR;
  }
*/

  return SUCCESS;
}

status_t BlitCpDma::EnableProfiling(bool enable) {
  queue_->SetProfiling(enable);
  return SUCCESS;
}

uint64_t BlitCpDma::AcquireWriteIndex(uint32_t num_packet) {
  assert(queue_->public_handle()->size >= num_packet);

  uint64_t write_index = queue_->AddWriteIndexAcqRel(num_packet);

  while (write_index + num_packet - queue_->LoadReadIndexRelaxed() > queue_->public_handle()->size) {
    os::YieldThread();
  }

  return write_index;
}

void BlitCpDma::ReleaseWriteIndex(uint64_t write_index, uint32_t num_packet) {
  // Update doorbel register with last packet id.
  signal_t doorbell =
      core::ISignal::Object(queue_->public_handle()->doorbell_signal);
  doorbell->StoreRelease(write_index + num_packet - 1);
}

void BlitCpDma::PopulateQueue(uint64_t index, void* dst, const void* src,
                              uint64_t bytes, signal_t completion_signal) {

  hsa_dma_copy_packet_t packet = {0};

  packet.header = HSA_PACKET_TYPE_DMA_COPY << HSA_PACKET_HEADER_TYPE;
  packet.src = src;
  packet.dst = dst;
  // packet.copy_dir = static_cast<dma_copy_dir>(dir);
  packet.bytes = bytes;
  packet.completion_signal = completion_signal;

  hsa_dma_copy_packet_t* queue_buffer =
      reinterpret_cast<hsa_dma_copy_packet_t*>(
          queue_->public_handle()->base_address);
  std::atomic_thread_fence(std::memory_order_acquire);
  queue_buffer[index & queue_bitmask_] = packet;
  std::atomic_thread_fence(std::memory_order_release);


/*
  assert(IsMultipleOf(args, 16));

  hsa_kernel_dispatch_packet_t packet = {0};

  static const uint16_t kDispatchPacketHeader =
      (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
      (((completion_signal.handle != 0) ? 1 : 0) << HSA_PACKET_HEADER_BARRIER) |
      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCACQUIRE_FENCE_SCOPE) |
      (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_SCRELEASE_FENCE_SCOPE);

  packet.header = kInvalidPacketHeader;
  packet.kernel_object = code_handle;
  packet.kernarg_address = args;

  // Setup working size.
  const int kNumDimension = 1;
  packet.setup = kNumDimension << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
  packet.grid_size_x = alignUp(static_cast<uint32_t>(grid_size_x), 64);
  packet.grid_size_y = packet.grid_size_z = 1;
  packet.workgroup_size_x = 64;
  packet.workgroup_size_y = packet.workgroup_size_z = 1;

  packet.completion_signal = completion_signal;

  // Populate queue buffer with AQL packet.
  hsa_kernel_dispatch_packet_t* queue_buffer =
      reinterpret_cast<hsa_kernel_dispatch_packet_t*>(
          queue_->public_handle()->base_address);
  std::atomic_thread_fence(std::memory_order_acquire);
  queue_buffer[index & queue_bitmask_] = packet;
  std::atomic_thread_fence(std::memory_order_release);
  queue_buffer[index & queue_bitmask_].header = kDispatchPacketHeader;
*/
}

