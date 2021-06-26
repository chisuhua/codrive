#pragma once

// #include "csi.h"
#include "inc/pps.h"
#include "inc/hsakmt.h"
#include "inc/command_queue.h"
#include "inc/ppu_agent.h"

extern pthread_mutex_t hsakmt_mutex;
extern bool is_dgpu;

extern int PAGE_SIZE;
extern int PAGE_SHIFT;

class cmdio;

//===----------------------------------------------------------------------===//
// @brief Runtime structure used to communicate tools information             //
//        between tools and core runtime.                                     //
//===----------------------------------------------------------------------===//
typedef struct hsa_ext_tools_info_s
{
  // Information needed by HSA tools
  // Scratch memory address
  void *scratch_address;

  // Scratch memory size
  size_t scratch_size;

  // Global memory address
  const void *global_address;

  // Information set by HSA tools debugger information
  // Cache mask, indicating caches disabled
  uint32_t cache_disable_mask;

  // Exception mask
  uint32_t exception_mask;

  // Number of reserved CUs for display, which ranges from
  // 0 to 7 in the current implementation.
  uint32_t reserved_cu_num;

  // Debug or profiler mode
  bool monitor_mode;

  // SQ debug mode
  bool gpu_single_step_mode;

  // Trap handler address
  void *trap_handler;

  // Trap buffer address
  void *trap_buffer;

  // Id of the Aql packet. Aql packet id applies to all
  // Aql packets - Kernel and Barrier packets, etc.
  uint64_t aql_pkt_id;
} hsa_ext_tools_info_t;

typedef void *aql_translation_handle;
/*
//---------------------------------------------------------------------------//
// @brief Tools extension to support write PM4 packets                       //
// @param cmd_buf Pointer to buffer holding the pm4 command                  //
// @param size Size of the command in terms of 32-bit words                  //
//---------------------------------------------------------------------------//
status_t HSA_API
  hsa_ext_tools_write_CBUF_packet(
    queue_t* queue, aql_translation_handle token, const void* cmd_buf, size_t size);
*/
/*
enum asic_family_type
{
  CHIP_PPUe = 0,
  CHIP_PPUm
};
*/

enum Asic
{
  INVALID = 0,
  PPU
};

// cmdio *GetDeviceCSI();

namespace device
{
class CommandQueue;
using namespace hcs;

// @brief Enum definition

#define IS_DGPU(chip) 1

status_t Load();
bool OnLoad();

device::CommandQueue *CreateCmdProcessor(GpuDevice *agent, uint32_t ring_size,
                                         HSAuint32 node, const HsaCoreProperties *properties,
                                         queue_type32_t queue_type, ScratchInfo &scratch,
                                         core::IHsaEventCallback callback, void *user_data);
} // namespace device
