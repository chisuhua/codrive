#pragma once

#include "pps_common.h"
#include "pps.h"
#include "inc/hsakmt.h"

// AMD Queue Properties.
typedef uint32_t amd_queue_properties32_t;
enum amd_queue_properties_t {
  HCS_BITS_CREATE_ENUM_ENTRIES(AMD_QUEUE_PROPERTIES_ENABLE_TRAP_HANDLER, 0, 1),
  HCS_BITS_CREATE_ENUM_ENTRIES(AMD_QUEUE_PROPERTIES_IS_PTR64, 1, 1),
  HCS_BITS_CREATE_ENUM_ENTRIES(AMD_QUEUE_PROPERTIES_ENABLE_TRAP_HANDLER_DEBUG_SGPRS, 2, 1),
  HCS_BITS_CREATE_ENUM_ENTRIES(AMD_QUEUE_PROPERTIES_ENABLE_PROFILING, 3, 1),
  HCS_BITS_CREATE_ENUM_ENTRIES(AMD_QUEUE_PROPERTIES_RESERVED1, 4, 28)
};

// cp Queue.
#define AMD_QUEUE_ALIGN_BYTES 64
#define AMD_QUEUE_ALIGN __ALIGNED__(AMD_QUEUE_ALIGN_BYTES)
typedef struct AMD_QUEUE_ALIGN cp_queue_s {
  queue_t hsa_queue;
  uint32_t reserved1[4];
  volatile uint64_t write_dispatch_id;
  uint32_t group_segment_aperture_base_hi;
  uint32_t private_segment_aperture_base_hi;
  uint32_t max_cu_id;
  uint32_t max_wave_id;
  volatile uint64_t max_legacy_doorbell_dispatch_id_plus_1;
  volatile uint32_t legacy_doorbell_lock;
  uint32_t reserved2[9];
  volatile uint64_t read_dispatch_id;
  uint32_t read_dispatch_id_field_base_byte_offset;
  uint32_t compute_tmpring_size;
  uint32_t scratch_resource_descriptor[4];
  uint64_t scratch_backing_memory_location;
  uint64_t scratch_backing_memory_byte_size;
  uint32_t scratch_workitem_byte_size;
  amd_queue_properties32_t queue_properties;
  //uint32_t reserved3[2];
  signal_t queue_inactive_signal;
  //uint32_t reserved4[14];
  HsaQueueResource queue_rsrc;  // schi add it
} co_queue_t;
