#pragma once

/** \addtogroup memory Memory
 *  @{
 */

/**
 * @brief Memory segments associated with a region.
 */
typedef enum {
  HSA_REGION_SEGMENT_GLOBAL = 0,
  HSA_REGION_SEGMENT_READONLY = 1,
  HSA_REGION_SEGMENT_PRIVATE = 2,
  HSA_REGION_SEGMENT_GROUP = 3,
  HSA_REGION_SEGMENT_KERNARG = 4
} hsa_region_segment_t;

/**
 * @brief Global region flags.
 */
typedef enum {
  HSA_REGION_GLOBAL_FLAG_KERNARG = 1,
  HSA_REGION_GLOBAL_FLAG_FINE_GRAINED = 2,
  HSA_REGION_GLOBAL_FLAG_COARSE_GRAINED = 4
} hsa_region_global_flag_t;

typedef enum hsa_amd_region_info_s {
  /** * Determine if host can access the region. The type of this attribute * is bool.  */
  HSA_AMD_REGION_INFO_HOST_ACCESSIBLE = 0xA000,
  /** * Base address of the region in flat address space.  */
  HSA_AMD_REGION_INFO_BASE = 0xA001,
  /** * Memory Interface width, the return value type is uint32_t.
   * This attribute is deprecated. Use HSA_AMD_AGENT_INFO_MEMORY_WIDTH.  */
  HSA_AMD_REGION_INFO_BUS_WIDTH = 0xA002,
  /** * Max Memory Clock, the return value type is uint32_t.
   * This attribute is deprecated. Use HSA_AMD_AGENT_INFO_MEMORY_MAX_FREQUENCY.  */
  HSA_AMD_REGION_INFO_MAX_CLOCK_FREQUENCY = 0xA003
} hsa_amd_region_info_t;


#if 0
status_t hsa_region_get_info(
    region_t region,
    hsa_region_info_t attribute,
    void* value);

status_t hsa_agent_iterate_regions(
    device_t agent,
    status_t (*callback)(region_t region, void* data),
    void* data);

status_t hsa_memory_allocate(region_t region,
    size_t size,
    void** ptr);

status_t hsa_memory_free(void* ptr);

status_t hsa_memory_copy(
    void *dst,
    const void *src,
    size_t size);
    // uint8_t dir);

status_t hsa_memory_assign_agent(
    void *ptr,
    device_t agent,
    hsa_access_permission_t access);

status_t hsa_memory_register(
    void *ptr,
    size_t size);

status_t hsa_memory_deregister(
    void *ptr,
    size_t size);

#endif
