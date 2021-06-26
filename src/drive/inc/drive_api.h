#pragma  once
// #include "runtime_api.h"
#include "stream_api.h"
#include "drive_type.h"

namespace core {
    class IAgent;
    class IMemoryRegion;
}


#if 0
typedef struct agent_s {
  /**
   * Opaque handle. Two handles reference the same object of the enclosing type
   * if and only if they are equal.
   */
  uint64_t handle;
} agent_t;

/**
 * @brief A memory region represents a block of virtual memory with certain
 * properties. For example, fine-grained memory in
 * the global segment using a region. A region might be associated with more
 * than one agent.
 */
typedef struct region_s {
  uint64_t handle;
} region_t;
#endif

/*
typedef struct hsa_cache_s {
  uint64_t handle;
} cache_t;
*/

typedef enum {
  HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT = 0,
  // * Operations that specify the default floating-point mode are rounded to zero
  HSA_DEFAULT_FLOAT_ROUNDING_MODE_ZERO = 1,
  /* Operations that specify the default floating-point mode are rounded to the
   * nearest representable number and that ties should be broken by selecting
   * the value with an even least significant bit.
   */
  HSA_DEFAULT_FLOAT_ROUNDING_MODE_NEAR = 2
} hsa_default_float_rounding_mode_t;

// * @brief Memory segment associated with a variable.
typedef enum {
  HSA_VARIABLE_SEGMENT_GLOBAL = 0,
  HSA_VARIABLE_SEGMENT_READONLY = 1
} hsa_variable_segment_t;


typedef enum {
  HSA_REGION_INFO_SEGMENT = 0,
  /**
   * Flag mask. The value of this attribute is undefined if the value of
   * ::HSA_REGION_INFO_SEGMENT is not ::HSA_REGION_SEGMENT_GLOBAL. The type of
   * this attribute is uint32_t, a bit-field of ::hsa_region_global_flag_t
   * values.
   */
  HSA_REGION_INFO_GLOBAL_FLAGS = 1,
  // * Size of this region, in bytes. The type of this attribute is size_t.
  HSA_REGION_INFO_SIZE = 2,
  /**
   * Maximum allocation size in this region, in bytes. Must not exceed the value
   * of ::HSA_REGION_INFO_SIZE. The type of this attribute is size_t.
   *
   * If the region is in the global or readonly segments, this is the maximum
   * size that the application can pass to ::hsa_memory_allocate.
   *
   * If the region is in the group segment, this is the maximum size (per
   * work-group) that can be requested for a given kernel dispatch. If the
   * region is in the private segment, this is the maximum size (per work-item)
   * that can be requested for a specific kernel dispatch, and must be at least
   * 256 bytes.
   */
  HSA_REGION_INFO_ALLOC_MAX_SIZE = 4,
  /**
   * Maximum size (per work-group) of private memory that can be requested for a
   * specific kernel dispatch. Must be at least 65536 bytes. The type of this
   * attribute is uint32_t. The value of this attribute is undefined if the
   * region is not in the private segment.
   */
  HSA_REGION_INFO_ALLOC_MAX_PRIVATE_WORKGROUP_SIZE = 8,
  /**
   * Indicates whether memory in this region can be allocated using
   * ::hsa_memory_allocate. The type of this attribute is bool.
   *
   * The value of this flag is always false for regions in the group and private
   * segments.
   */
  HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED = 5,
  /**
   * Allocation granularity of buffers allocated by ::hsa_memory_allocate in
   * this region. The size of a buffer allocated in this region is a multiple of
   * the value of this attribute. The value of this attribute is only defined if
   * ::HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED is true for this region. The type
   * of this attribute is size_t.
   */
  HSA_REGION_INFO_RUNTIME_ALLOC_GRANULE = 6,
  /**
   * Alignment of buffers allocated by ::hsa_memory_allocate in this region. The
   * value of this attribute is only defined if
   * ::HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED is true for this region, and must be
   * a power of 2. The type of this attribute is size_t.
   */
  HSA_REGION_INFO_RUNTIME_ALLOC_ALIGNMENT = 7
} region_info_t;

/**
 * @brief Type of accesses to a memory pool from a given agent.
 */
typedef enum {
  // The agent cannot directly access any buffer in the memory pool.
  HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED = 0,
  // The agent can directly access a buffer located in the pool; the application
  // does not need to invoke ::hsa_amd_agents_allow_access.
  HSA_AMD_MEMORY_POOL_ACCESS_ALLOWED_BY_DEFAULT = 1,
  // The agent can directly access a buffer located in the pool, but only if the
  // application has previously requested access to that buffer using hsa_amd_agents_allow_access.
  HSA_AMD_MEMORY_POOL_ACCESS_DISALLOWED_BY_DEFAULT = 2
} hsa_amd_memory_pool_access_t;


// * @brief Memory pool features.
typedef enum {
  //* Segment where the memory pool resides. The type of this attribute is * ::hsa_amd_segment_t.
  HSA_AMD_MEMORY_POOL_INFO_SEGMENT = 0,
  // Flag mask. The value of this attribute is undefined if the value of
  // ::HSA_AMD_MEMORY_POOL_INFO_SEGMENT is not ::HSA_AMD_SEGMENT_GLOBAL. The type
  // of  this attribute is uint32_t, a bit-field of ::hsa_amd_memory_pool_global_flag_t
  // values.
  HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS = 1,
  //* Size of this pool, in bytes. The type of this attribute is size_t.
  HSA_AMD_MEMORY_POOL_INFO_SIZE = 2,
  //* Indicates whether memory in this pool can be allocated using
  //* ::hsa_amd_memory_pool_allocate. The type of this attribute is bool.
  //* The value of this flag is always false for memory pools in the group and private segments.
  HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED = 5,
  //* Allocation granularity of buffers allocated by * ::hsa_amd_memory_pool_allocate
  //* in this memory pool. The size of a buffer allocated in this pool is a
  //* multiple of the value of this attribute. The value of this attribute is
  //* only defined if ::HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED is true for
  //* this pool. The type of this attribute is size_t.
  HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_GRANULE = 6,
  //* Alignment of buffers allocated by ::hsa_amd_memory_pool_allocate in this
  //* pool. The value of this attribute is only defined if
  //* ::HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED is true for this pool, and
  //* must be a power of 2. The type of this attribute is size_t.
  HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALIGNMENT = 7,
  //* This memory_pool can be made directly accessible by all the agents in the
  //* system (::hsa_amd_agent_memory_pool_get_info does not return 
  //* ::HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED for any agent). The type of this
  //* attribute is bool.
  HSA_AMD_MEMORY_POOL_INFO_ACCESSIBLE_BY_ALL = 15,
  //* Maximum aggregate allocation size in bytes. The type of this attribute is size_t.
  HSA_AMD_MEMORY_POOL_INFO_ALLOC_MAX_SIZE = 16,
} hsa_amd_memory_pool_info_t;



// * @brief System attributes.
typedef enum {
  HSA_SYSTEM_INFO_VERSION_MAJOR = 0,
  HSA_SYSTEM_INFO_VERSION_MINOR = 1,
  HSA_SYSTEM_INFO_TIMESTAMP = 2,
  /* Timestamp value increase rate, in Hz. The timestamp (clock) frequency is
   * in the range 1-400MHz. The type of this attribute is uint64_t.  */
  HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY = 3,
  /* Maximum duration of a signal wait operation. Expressed as a count based on
   * the timestamp frequency. The type of this attribute is uint64_t.  */
  HSA_SYSTEM_INFO_SIGNAL_MAX_WAIT = 4,
  /* Endianness of the system. The type of this attribute is ::hsa_endianness_t.  */
  HSA_SYSTEM_INFO_ENDIANNESS = 5,
  /* Machine model supported by the HSA runtime. The type of this attribute is ::hsa_machine_model_t.  */
  HSA_SYSTEM_INFO_MACHINE_MODEL = 6,
  /** * Bit-mask indicating which extensions are supported by the
   * implementation. An extension with an ID of @p i is supported if the bit at
   * position @p i is set. The type of this attribute is uint8_t[128].
   */
  HSA_SYSTEM_INFO_EXTENSIONS = 7
} hsa_system_info_t;


typedef void (*hsa_amd_deallocation_callback_t)(void* ptr, void* user_data);
typedef bool (*hsa_amd_signal_handler)(signal_value_t value, void* arg);

// * @brief Denotes the type of memory in a pointer info query.
typedef enum {
  // Memory is not known to the HSA driver.  Unallocated or unlocked system memory.
  HSA_EXT_POINTER_TYPE_UNKNOWN = 0,
  // Memory was allocated with an HSA memory allocator.
  HSA_EXT_POINTER_TYPE_HSA = 1,
  /*System memory which has been locked for use with an HSA agent.

  Memory of this type is normal malloc'd memory and is always accessible to
  the CPU.  Pointer info queries may not include CPU agents in the accessible
  agents list as the CPU has implicit access.  */
  HSA_EXT_POINTER_TYPE_LOCKED = 2,
  // Memory originated in a graphics component and is shared with ROCr.
  HSA_EXT_POINTER_TYPE_GRAPHICS = 3,
  // Memory has been shared with the local process via ROCr IPC APIs.
  HSA_EXT_POINTER_TYPE_IPC = 4
} hsa_amd_pointer_type_t;


// * @brief Describes a memory allocation known to ROCr.
typedef struct hsa_amd_pointer_info_s {
  /* Size in bytes of this structure.  Used for version control within a major ROCr
  revision.  Set to sizeof(hsa_amd_pointer_t) prior to calling
  hsa_amd_pointer_info.  If the runtime supports an older version of pointer
  info then size will be smaller on return.  Members starting after the return
  value of size will not be updated by hsa_amd_pointer_info.  */
  uint32_t size;
  // The type of allocation referenced.
  hsa_amd_pointer_type_t type;
  // Base address at which non-host agents may access the allocation.
  void* agentBaseAddress;
  // Base address at which the host agent may access the allocation.
  void* hostBaseAddress;
  size_t sizeInBytes;
  void* userData;
  /*
  Reports an agent which "owns" (ie has preferred access to) the pool in which the allocation was
  made.  When multiple agents share equal access to a pool (ex: multiple CPU agents, or multi-die
  GPU boards) any such agent may be returned.
  */
  core::IAgent* agentOwner;
} hsa_amd_pointer_info_t;

 //* @brief 256-bit process independent identifier for a ROCr shared memory * allocation.
typedef struct hsa_amd_ipc_memory_s {
  uint32_t handle[8];
} hsa_amd_ipc_memory_t;


/**
 * @brief Coherency attributes of fine grain region.
 */
typedef enum hsa_amd_coherency_type_s {
  HSA_AMD_COHERENCY_TYPE_COHERENT = 0,
  HSA_AMD_COHERENCY_TYPE_NONCOHERENT = 1
} hsa_amd_coherency_type_t;

typedef enum {
  // * The length of the cache name in bytes, not including the NUL terminator.
  HSA_CACHE_INFO_NAME_LENGTH = 0,
  // * Human-readable description.  The type of this attribute is a NUL-terminated
  HSA_CACHE_INFO_NAME = 1,
  /* Cache level. A L1 cache must return a value of 1, a L2 must return a value
   * of 2, and so on.  The type of this attribute is uint8_t.*/
  HSA_CACHE_INFO_LEVEL = 2,
  /* Cache size, in bytes. A value of 0 indicates that there is no size
   * information available. The type of this attribute is uint32_t.  */
  HSA_CACHE_INFO_SIZE = 3
} cache_info_t;

status_t hsa_memory_register(void *ptr, size_t size);
status_t hsa_memory_deregister( void *ptr, size_t size);
status_t hsa_memory_allocate(core::IMemoryRegion* region, size_t size, void** ptr);
status_t hsa_memory_free(void* ptr);
status_t hsa_agent_get_info(core::IAgent* agent_handle,
                                        agent_info_t attribute,
                                        void* value);
/**
 * @brief Access permissions.
 */
typedef enum {
  /** * Read-only access.  */
  HSA_ACCESS_PERMISSION_RO = 1,
  /** * Write-only access.  */
  HSA_ACCESS_PERMISSION_WO = 2,
  /** * Read and write access.  */
  HSA_ACCESS_PERMISSION_RW = 3
} hsa_access_permission_t;

typedef struct hsa_isa_s {
  /**
   * Opaque handle. Two handles reference the same object of the enclosing type
   * if and only if they are equal.
   */
  uint64_t handle;
} hsa_isa_t;

status_t hsa_amd_memory_lock(void* host_ptr, size_t size,
                             device_t* agents, int num_agent,
                             void** agent_ptr);

/**
 * @brief Agent features.
 */
typedef enum {
    /**
     * The agent supports AQL packets of kernel dispatch type. If this
     * feature is enabled, the agent is also a kernel agent.
     */
    HSA_AGENT_FEATURE_KERNEL_DISPATCH = 1,
    /**
     * The agent supports AQL packets of agent dispatch type.
     */
    HSA_AGENT_FEATURE_AGENT_DISPATCH = 2
} hsa_agent_feature_t;

/**
 * @brief Hardware device type.
 */
typedef enum {
    /** * CPU device.  */
    HSA_AGENT_TYPE_CPU = 0,
    /** * GPU device.  */
    HSA_AGENT_TYPE_GPU = 1,
    /** * DSP device.  */
    HSA_AGENT_TYPE_DSP = 2
} hsa_agent_type_t;

typedef enum {
    /** * Small machine model. Addresses use 32 bits.  */
    HSA_MACHINE_MODEL_SMALL = 0,
    /** * Large machine model. Addresses use 64 bits.  */
    HSA_MACHINE_MODEL_LARGE = 1
} hsa_machine_model_t;

/** * @brief Three-dimensional coordinate.  */
typedef struct hsa_dim3_s {
   /** * X dimension.  */
   uint32_t x;
   /** * Y dimension.  */
   uint32_t y;
   /** * Z dimension.  */
   uint32_t z;
} hsa_dim3_t;

enum class endianness_t {
    /** * The least significant byte is stored in the smallest address.  */
    ENDIANNESS_LITTLE = 0,
    /** * The most significant byte is stored in the smallest address.  */
    ENDIANNESS_BIG = 1
} ;

/**
 * @brief GPU system event type.
 */
typedef enum hsa_amd_event_type_s {
  /*
   AMD GPU memory fault.
   */
  // GPU_MEMORY_FAULT_EVENT = 0
  HSA_AMD_GPU_MEMORY_FAULT_EVENT = 0,
} hsa_amd_event_type_t;

/**
 * @brief Flags denoting the cause of a memory fault.
 */
typedef enum {
  // Page not present or supervisor privilege.
  HSA_AMD_MEMORY_FAULT_PAGE_NOT_PRESENT = 1 << 0,
  // Write access to a read-only page.
  HSA_AMD_MEMORY_FAULT_READ_ONLY = 1 << 1,
  // Execute access to a page marked NX.
  HSA_AMD_MEMORY_FAULT_NX = 1 << 2,
  // GPU attempted access to a host only page.
  HSA_AMD_MEMORY_FAULT_HOST_ONLY = 1 << 3,
  // DRAM ECC failure.
  HSA_AMD_MEMORY_FAULT_DRAM_ECC = 1 << 4,
  // Can't determine the exact fault address.
  HSA_AMD_MEMORY_FAULT_IMPRECISE = 1 << 5,
  // SRAM ECC failure (ie registers, no fault address).
  HSA_AMD_MEMORY_FAULT_SRAM_ECC = 1 << 6,
  // GPU reset following unspecified hang.
  HSA_AMD_MEMORY_FAULT_HANG = 1 << 31
} hsa_amd_memory_fault_reason_t;

/**
 * @brief AMD GPU memory fault event data.
 */
typedef struct hsa_amd_gpu_memory_fault_info_s {
  /*
  The agent where the memory fault occurred.
  */
  device_t agent;
  /*
  Virtual address accessed.
  */
  uint64_t virtual_address;
  /*
  Bit field encoding the memory access failure reasons. There could be multiple bits set
  for one fault.  Bits are defined in hsa_amd_memory_fault_reason_t.
  0x00000001 Page not present or supervisor privilege.
  0x00000010 Write access to a read-only page.
  0x00000100 Execute access to a page marked NX.
  0x00001000 Host access only.
  0x00010000 ECC failure (if supported by HW).
  0x00100000 Can't determine the exact fault address.
  */
  uint32_t fault_reason_mask;
} hsa_amd_gpu_memory_fault_info_t;

/**
 * @brief AMD GPU event data passed to event handler.
 */
typedef struct hsa_amd_event_s {
  /*
  The event type.
  */
  hsa_amd_event_type_t event_type;
  union {
    /*
    The memory fault info, only valid when @p event_type is HSA_AMD_GPU_MEMORY_FAULT_EVENT.
    */
    hsa_amd_gpu_memory_fault_info_t memory_fault;
  };
} hsa_amd_event_t;

typedef status_t (*hsa_amd_system_event_callback_t)(const hsa_amd_event_t* event, void* data);

/**
 * @brief Register AMD GPU event handler.
 *
 * @param[in] callback Callback to be invoked when an event is triggered.
 * The HSA runtime passes two arguments to the callback: @p event
 * is defined per event by the HSA runtime, and @p data is the user data.
 *
 * @param[in] data User data that is passed to @p callback. May be NULL.
 *
 * @retval SUCCESS The handler has been registered successfully.
 *
 * @retval ERROR An event handler has already been registered.
 *
 * @retval ERROR_INVALID_ARGUMENT @p event is invalid.
 */
status_t hsa_amd_register_system_event_handler(hsa_amd_system_event_callback_t callback,
                                                   void* data);

/**
 * @brief Per-queue dispatch and wavefront scheduling priority.
 */
typedef enum hsa_amd_queue_priority_s {
  /*
  Below normal/high priority compute and all graphics
  */
  HSA_AMD_QUEUE_PRIORITY_LOW = 0,
  /*
  Above low priority compute, below high priority compute and all graphics
  */
  HSA_AMD_QUEUE_PRIORITY_NORMAL = 1,
  /*
  Above low/normal priority compute and all graphics
  */
  HSA_AMD_QUEUE_PRIORITY_HIGH = 2,
} hsa_amd_queue_priority_t;

/**
 * @brief Modifies the dispatch and wavefront scheduling prioirty for a
 * given compute queue. The default is HSA_AMD_QUEUE_PRIORITY_NORMAL.
 *
 * @param[in] queue Compute queue to apply new priority to.
 *
 * @param[in] priority Priority to associate with queue.
 *
 * @retval SUCCESS if priority was changed successfully.
 *
 * @retval ERROR_INVALID_QUEUE if queue is not a valid
 * compute queue handle.
 *
 * @retval ERROR_INVALID_ARGUMENT if priority is not a valid
 * value from hsa_amd_queue_priority_t.
 */
status_t hsa_amd_queue_set_priority(queue_t* queue,
                                                hsa_amd_queue_priority_t priority);
/**
 * @brief Deallocation notifier function type.
 */
typedef void (*hsa_amd_deallocation_callback_t)(void* ptr, void* user_data);

/**
 * @brief Registers a deallocation notifier monitoring for release of agent
 * accessible address @p ptr.  If successful, @p callback will be invoked when
 * @p ptr is removed from accessibility from all agents.
 *
 * Notification callbacks are automatically deregistered when they are invoked.
 *
 * Note: The current version supports notifications of address release
 * originating from ::hsa_amd_memory_pool_free.  Support for other address
 * release APIs will follow.
 *
 * @param[in] ptr Agent accessible address to monitor for deallocation.  Passed
 * to @p callback.
 *
 * @param[in] callback Notifier to be invoked when @p ptr is released from
 * agent accessibility.
 *
 * @param[in] user_data User provided value passed to @p callback.  May be NULL.
 *
 * @retval ::SUCCESS The notifier registered successfully
 *
 * @retval ::ERROR_NOT_INITIALIZED The HSA runtime has not been
 * initialized.
 *
 * @retval ::ERROR_INVALID_ALLOCATION @p ptr does not refer to a valid agent accessible
 * address.
 *
 * @retval ::ERROR_INVALID_ARGUMENT @p callback is NULL or @p ptr is NULL.
 *
 * @retval ::ERROR_OUT_OF_RESOURCES if there is a failure in allocating
 * necessary resources
 */
status_t hsa_amd_register_deallocation_callback(void* ptr,
                                                    hsa_amd_deallocation_callback_t callback,
                                                    void* user_data);

/**
 * @brief Removes a deallocation notifier previously registered with
 * ::hsa_amd_register_deallocation_callback.  Arguments must be identical to
 * those given in ::hsa_amd_register_deallocation_callback.
 *
 * @param[in] ptr Agent accessible address which was monitored for deallocation.
 *
 * @param[in] callback Notifier to be removed.
 *
 * @retval ::SUCCESS The notifier has been removed successfully.
 *
 * @retval ::ERROR_NOT_INITIALIZED The HSA runtime has not been
 * initialized.
 *
 * @retval ::ERROR_INVALID_ARGUMENT The given notifier was not registered.
 */
status_t hsa_amd_deregister_deallocation_callback(void* ptr,
                                                      hsa_amd_deallocation_callback_t callback);

