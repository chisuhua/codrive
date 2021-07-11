#pragma once
#include <stdint.h>
#include "inc/hsakmttypes.h"

#define CMDAPI_open 1


struct ioctl_open_args {
	uint64_t handle;	/* from KFD */
	uint32_t device_num;	/* to KFD */
};

struct ioctl_close_args {
	uint64_t handle;	/* from KFD */
};

struct ioctl_mmap_args {
	uint64_t start;	/* to KFD */
	uint64_t length;	/* to KFD */
	uint32_t prot;		/* to KFD */
	uint32_t flags;		/* to KFD */
	uint32_t fd;	/* to KFD  -1 use system memory , 0 use kfd_fd */
	uint32_t offset;	/* to KFD */
};


struct ioctl_munmap_args {
	uint64_t start;	/* to KFD */
	uint64_t length;	/* to KFD */
};

#define KFD_IOCTL_DBG_MAJOR_VERSION	2
#define KFD_IOCTL_DBG_MINOR_VERSION	1

struct ioctl_get_version_args {
	uint32_t major_version;	/* from KFD */
	uint32_t minor_version;	/* from KFD */
};

#define KFD_IOC_QUEUE_TYPE_COMPUTE		0x0

#define KFD_MAX_QUEUE_PERCENTAGE	100
#define KFD_MAX_QUEUE_PRIORITY		15


struct ioctl_create_queue_args {
	uint64_t ring_base_address;	/* to KFD */
	uint64_t write_pointer_address;	/* from KFD */
	uint64_t read_pointer_address;	/* from KFD */
    uint64_t doorbell_base;	/* from KFD */ // schi add it for emulation nned it
	uint64_t doorbell_offset;	/* from KFD */

	uint32_t ring_size;		/* to KFD */
	uint32_t gpu_id;		/* to KFD */
	uint32_t queue_type;		/* to KFD */
	uint32_t queue_percentage;	/* to KFD */
	uint32_t queue_priority;	/* to KFD */
	uint32_t queue_id;		/* from KFD */

	uint64_t eop_buffer_address;	/* to KFD */
	uint64_t eop_buffer_size;	/* to KFD */
	uint64_t ctx_save_restore_address; /* to KFD */
	uint32_t ctx_save_restore_size;	/* to KFD */
	uint32_t ctl_stack_size;		/* to KFD */
};

struct ioctl_destroy_queue_args {
	uint32_t queue_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_update_queue_args {
	uint64_t ring_base_address;	/* to KFD */

	uint32_t queue_id;		/* to KFD */
	uint32_t ring_size;		/* to KFD */
	uint32_t queue_percentage;	/* to KFD */
	uint32_t queue_priority;	/* to KFD */
};

struct ioctl_set_cu_mask_args {
	uint32_t queue_id;		/* to KFD */
	uint32_t num_cu_mask;		/* to KFD */
	uint64_t cu_mask_ptr;		/* to KFD */
};

struct ioctl_get_queue_wave_state_args {
	uint64_t ctl_stack_address;	/* to KFD */
	uint32_t ctl_stack_used_size;	/* from KFD */
	uint32_t save_area_used_size;	/* from KFD */
	uint32_t queue_id;			/* to KFD */
	uint32_t pad;
};

/*
 * All counters are monotonic. They are used for profiling of compute jobs.
 * The profiling is done by userspace.
 *
 * In case of GPU reset, the counter should not be affected.
 */

struct ioctl_get_clock_counters_args {
	uint64_t gpu_clock_counter;	/* from KFD */
	uint64_t cpu_clock_counter;	/* from KFD */
	uint64_t system_clock_counter;	/* from KFD */
	uint64_t system_clock_freq;	/* from KFD */

	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_get_process_apertures_args {
	/* User allocated. Pointer to struct kfd_process_device_apertures
	 * filled in by Kernel
	 */
	uint64_t process_device_apertures_ptr;
	/* to KFD - indicates amount of memory present in
	 *  kfd_process_device_apertures_ptr
	 * from KFD - Number of entries filled by KFD.
	 */
	uint32_t num_of_nodes;
	uint32_t pad;
};

#define MAX_ALLOWED_NUM_POINTS    100
#define MAX_ALLOWED_AW_BUFF_SIZE 4096
#define MAX_ALLOWED_WAC_BUFF_SIZE  128

struct ioctl_dbg_register_args {
	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_dbg_unregister_args {
	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_dbg_address_watch_args {
	uint64_t content_ptr;		/* a pointer to the actual content */
	uint32_t gpu_id;		/* to KFD */
	uint32_t buf_size_in_bytes;	/*including gpu_id and buf_size */
};

struct ioctl_dbg_wave_control_args {
	uint64_t content_ptr;		/* a pointer to the actual content */
	uint32_t gpu_id;		/* to KFD */
	uint32_t buf_size_in_bytes;	/*including gpu_id and buf_size */
};

/* mapping event types to API spec */
#define	KFD_DBG_EV_STATUS_TRAP		1
#define	KFD_DBG_EV_STATUS_VMFAULT	2
#define	KFD_DBG_EV_STATUS_SUSPENDED	4
#define KFD_DBG_EV_STATUS_NEW_QUEUE	8
#define	KFD_DBG_EV_FLAG_CLEAR_STATUS	1

#define KFD_INVALID_QUEUEID	0xffffffff

/* KFD_IOC_DBG_TRAP_ENABLE:
 * ptr:   unused
 * data1: 0=disable, 1=enable
 * data2: queue ID (for future use)
 * data3: return value for fd
 */
#define KFD_IOC_DBG_TRAP_ENABLE 0

/* KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE:
 * ptr:   unused
 * data1: override mode: 0=OR, 1=REPLACE
 * data2: mask
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE 1

/* KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE:
 * ptr:   unused
 * data1: 0=normal, 1=halt, 2=kill, 3=singlestep, 4=disable
 * data2: unused
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE 2

/* KFD_IOC_DBG_TRAP_NODE_SUSPEND:
 * ptr:   pointer to an array of Queues IDs
 * data1: flags
 * data2: number of queues
 * data3: grace period
 */
#define KFD_IOC_DBG_TRAP_NODE_SUSPEND 3

/* KFD_IOC_DBG_TRAP_NODE_RESUME:
 * ptr:   pointer to an array of Queues IDs
 * data1: flags
 * data2: number of queues
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_NODE_RESUME 4

/* KFD_IOC_DBG_TRAP_QUERY_DEBUG_EVENT:
 * ptr: unused
 * data1: queue id (IN/OUT)
 * data2: flags (IN)
 * data3: suspend[2:2], event type [1:0] (OUT)
 */

#define KFD_IOC_DBG_TRAP_QUERY_DEBUG_EVENT 5

/* KFD_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT:
 * ptr: user buffer (IN)
 * data1: flags (IN)
 * data2: number of queue snapshot entries (IN/OUT)
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT 6

/* KFD_IOC_DBG_TRAP_GET_VERSION:
 * ptr: unsused
 * data1: major version (OUT)
 * data2: minor version (OUT)
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_GET_VERSION	7

/* KFD_IOC_DBG_TRAP_CLEAR_ADDRESS_WATCH:
 * ptr: unused
 * data1: watch ID
 * data2: unused
 * data3: unused
 */
#define KFD_IOC_DBG_TRAP_CLEAR_ADDRESS_WATCH 8

/* KFD_IOC_DBG_TRAP_SET_ADDRESS_WATCH:
 * ptr:   Watch address
 * data1: Watch ID (OUT)
 * data2: watch_mode: 0=read, 1=nonread, 2=atomic, 3=all
 * data3: watch address mask
 */
#define KFD_IOC_DBG_TRAP_SET_ADDRESS_WATCH 9

struct ioctl_dbg_trap_args {
	uint64_t ptr;     /* to KFD -- used for pointer arguments: queue arrays */
	uint32_t pid;     /* to KFD */
	uint32_t gpu_id;  /* to KFD */
	uint32_t op;      /* to KFD */
	uint32_t data1;   /* to KFD */
	uint32_t data2;   /* to KFD */
	uint32_t data3;   /* to KFD */
};

/* Matching HSA_EVENTTYPE */
#define KFD_IOC_EVENT_SIGNAL			0
#define KFD_IOC_EVENT_NODECHANGE		1
#define KFD_IOC_EVENT_DEVICESTATECHANGE		2
#define KFD_IOC_EVENT_HW_EXCEPTION		3
#define KFD_IOC_EVENT_SYSTEM_EVENT		4
#define KFD_IOC_EVENT_DEBUG_EVENT		5
#define KFD_IOC_EVENT_PROFILE_EVENT		6
#define KFD_IOC_EVENT_QUEUE_EVENT		7
#define KFD_IOC_EVENT_MEMORY			8

#define KFD_IOC_WAIT_RESULT_COMPLETE		0
#define KFD_IOC_WAIT_RESULT_TIMEOUT		1
#define KFD_IOC_WAIT_RESULT_FAIL		2

#define KFD_SIGNAL_EVENT_LIMIT			4096

/* For kfd_event_data.hw_exception_data.reset_type. */
#define KFD_HW_EXCEPTION_WHOLE_GPU_RESET	0
#define KFD_HW_EXCEPTION_PER_ENGINE_RESET	1

/* For kfd_event_data.hw_exception_data.reset_cause. */
#define KFD_HW_EXCEPTION_GPU_HANG	0
#define KFD_HW_EXCEPTION_ECC		1

/* For kfd_hsa_memory_exception_data.ErrorType */
#define KFD_MEM_ERR_NO_RAS		0
#define KFD_MEM_ERR_SRAM_ECC		1
#define KFD_MEM_ERR_POISON_CONSUMED	2
#define KFD_MEM_ERR_GPU_HANG		3

struct ioctl_create_event_args {
	uint64_t event_page_offset;	/* from KFD */
	uint32_t event_trigger_data;	/* from KFD - signal events only */
	uint32_t event_type;		/* to KFD */
	uint32_t auto_reset;		/* to KFD */
	uint32_t node_id;		/* to KFD - only valid for certain
							event types */
	uint32_t event_id;		/* from KFD */
	uint32_t event_slot_index;	/* from KFD */
};

struct ioctl_destroy_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_set_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_reset_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct kfd_memory_exception_failure {
	uint32_t NotPresent;	/* Page not present or supervisor privilege */
	uint32_t ReadOnly;	/* Write access to a read-only page */
	uint32_t NoExecute;	/* Execute access to a page marked NX */
	uint32_t imprecise;	/* Can't determine the	exact fault address */
};

/* memory exception data */
struct cmd_hsa_memory_exception_data {
	struct kfd_memory_exception_failure failure;
	uint64_t va;
	uint32_t gpu_id;
	uint32_t ErrorType; /* 0 = no RAS error,
			  * 1 = ECC_SRAM,
			  * 2 = Link_SYNFLOOD (poison),
			  * 3 = GPU hang (not attributable to a specific cause),
			  * other values reserved
			  */
};

/* hw exception data */
struct cmd_hsa_hw_exception_data {
	uint32_t reset_type;
	uint32_t reset_cause;
	uint32_t memory_lost;
	uint32_t gpu_id;
};

/* Event data */
struct cmd_event_data {
	union {
		struct cmd_hsa_memory_exception_data memory_exception_data;
		struct cmd_hsa_hw_exception_data hw_exception_data;
	};				/* From KFD */
	uint64_t cmd_event_data_ext;	/* pointer to an extension structure
					   for future exception types */
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_wait_events_args {
	uint64_t events_ptr;		/* pointed to struct
					   kfd_event_data array, to KFD */
	uint32_t num_events;		/* to KFD */
	uint32_t wait_for_all;		/* to KFD */
	uint32_t timeout;		/* to KFD */
	uint32_t wait_result;		/* from KFD */
};

struct ioctl_set_scratch_backing_va_args {
	uint64_t va_addr;	/* to KFD */
	uint32_t gpu_id;	/* to KFD */
	uint32_t pad;
};

struct ioctl_get_tile_config_args {
	/* to KFD: pointer to tile array */
	uint64_t tile_config_ptr;
	/* to KFD: pointer to macro tile array */
	uint64_t macro_tile_config_ptr;
	/* to KFD: array size allocated by user mode
	 * from KFD: array size filled by kernel
	 */
	uint32_t num_tile_configs;
	/* to KFD: array size allocated by user mode
	 * from KFD: array size filled by kernel
	 */
	uint32_t num_macro_tile_configs;

	uint32_t gpu_id;		/* to KFD */
	uint32_t gb_addr_config;	/* from KFD */
	uint32_t num_banks;		/* from KFD */
	uint32_t num_ranks;		/* from KFD */
	/* struct size can be extended later if needed
	 * without breaking ABI compatibility
	 */
};

struct ioctl_set_trap_handler_args {
	uint64_t tba_addr;		/* to KFD */
	uint64_t tma_addr;		/* to KFD */
	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_acquire_vm_args {
	uint32_t drm_fd;	/* to KFD */
	uint32_t gpu_id;	/* to KFD */
};

/* Allocation flags: memory types */
#define KFD_IOC_ALLOC_MEM_FLAGS_VRAM		(1 << 0)
#define KFD_IOC_ALLOC_MEM_FLAGS_GTT		(1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_USERPTR		(1 << 2)
#define KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL	(1 << 3)
#define KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP	(1 << 4)
/* Allocation flags: attributes/access options */
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE	(1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE	(1 << 30)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC		(1 << 29)
#define KFD_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE	(1 << 28)
#define KFD_IOC_ALLOC_MEM_FLAGS_AQL_QUEUE_MEM	(1 << 27)
#define KFD_IOC_ALLOC_MEM_FLAGS_COHERENT	(1 << 26)

/* Allocate memory for later SVM (shared virtual memory) mapping.
 *
 * @va_addr:     virtual address of the memory to be allocated
 *               all later mappings on all GPUs will use this address
 * @size:        size in bytes
 * @handle:      buffer handle returned to user mode, used to refer to
 *               this allocation for mapping, unmapping and freeing
 * @mmap_offset: for CPU-mapping the allocation by mmapping a render node
 *               for userptrs this is overloaded to specify the CPU address
 * @gpu_id:      device identifier
 * @flags:       memory type and attributes. See KFD_IOC_ALLOC_MEM_FLAGS above
 */
struct ioctl_alloc_memory_args {
	uint64_t va_addr;		/* to KFD */
	uint64_t size;		/* to KFD */
	uint64_t handle;		/* from KFD */
	uint64_t mmap_offset;	/* to KFD (userptr), from KFD (mmap offset) */
	uint32_t gpu_id;		/* to KFD */
	uint32_t flags;
};


/* Free memory allocated with ioctl_alloc_memory_of_gpu
 *
 * @handle: memory handle returned by alloc
 */
struct ioctl_free_memory_args {
	uint64_t handle;		/* to KFD */
};

/* Map memory to one or more GPUs
 *
 * @handle:                memory handle returned by alloc
 * @device_ids_array_ptr:  array of gpu_ids (uint32_t per device)
 * @n_devices:             number of devices in the array
 * @n_success:             number of devices mapped successfully
 *
 * @n_success returns information to the caller how many devices from
 * the start of the array have mapped the buffer successfully. It can
 * be passed into a subsequent retry call to skip those devices. For
 * the first call the caller should initialize it to 0.
 *
 * If the ioctl completes with return code 0 (success), n_success ==
 * n_devices.
 */
struct ioctl_map_memory_to_gpu_args {
	uint64_t handle;			/* to KFD */
	uint64_t device_ids_array_ptr;	/* to KFD */
	uint32_t n_devices;		/* to KFD */
	uint32_t n_success;		/* to/from KFD */
};

/* Unmap memory from one or more GPUs
 *
 * same arguments as for mapping
 */
struct ioctl_unmap_memory_from_gpu_args {
	uint64_t handle;			/* to KFD */
	uint64_t device_ids_array_ptr;	/* to KFD */
	uint32_t n_devices;		/* to KFD */
	uint32_t n_success;		/* to/from KFD */
};

/* Allocate GWS for specific queue
 *
 * @queue_id:    queue's id that GWS is allocated for
 * @num_gws:     how many GWS to allocate
 * @first_gws:   index of the first GWS allocated.
 *               only support contiguous GWS allocation
 */
struct ioctl_alloc_queue_gws_args {
	uint32_t queue_id;		/* to KFD */
	uint32_t num_gws;		/* to KFD */
	uint32_t first_gws;	/* from KFD */
	uint32_t pad;		/* to KFD */
};

struct ioctl_get_dmabuf_info_args {
	uint64_t size;		/* from KFD */
	uint64_t metadata_ptr;	/* to KFD */
	uint32_t metadata_size;	/* to KFD (space allocated by user)
				 * from KFD (actual metadata size)
				 */
	uint32_t gpu_id;	/* from KFD */
	uint32_t flags;		/* from KFD (KFD_IOC_ALLOC_MEM_FLAGS) */
	uint32_t dmabuf_fd;	/* to KFD */
};

struct ioctl_import_dmabuf_args {
	uint64_t va_addr;	/* to KFD */
	uint64_t handle;	/* from KFD */
	uint32_t gpu_id;	/* to KFD */
	uint32_t dmabuf_fd;	/* to KFD */
};

/* Register offset inside the remapped mmio page
 */
enum kfd_mmio_remap {
	KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL = 0,
	KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL = 4,
};

struct ioctl_ipc_export_handle_args {
	uint64_t handle;		/* to KFD */
	uint32_t share_handle[4];	/* from KFD */
	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct ioctl_ipc_import_handle_args {
	uint64_t handle;		/* from KFD */
	uint64_t va_addr;		/* to KFD */
	uint64_t mmap_offset;		/* from KFD */
	uint32_t share_handle[4];	/* to KFD */
	uint32_t gpu_id;		/* to KFD */
	uint32_t pad;
};

struct kfd_memory_range {
	uint64_t va_addr;
	uint64_t size;
};

/* flags definitions
 * BIT0: 0: read operation, 1: write operation.
 * This also identifies if the src or dst array belongs to remote process
 */
#define KFD_CROSS_MEMORY_RW_BIT (1 << 0)
#define KFD_SET_CROSS_MEMORY_READ(flags) (flags &= ~KFD_CROSS_MEMORY_RW_BIT)
#define KFD_SET_CROSS_MEMORY_WRITE(flags) (flags |= KFD_CROSS_MEMORY_RW_BIT)
#define KFD_IS_CROSS_MEMORY_WRITE(flags) (flags & KFD_CROSS_MEMORY_RW_BIT)

struct ioctl_cross_memory_copy_args {
	/* to KFD: Process ID of the remote process */
	uint32_t pid;
	/* to KFD: See above definition */
	uint32_t flags;
	/* to KFD: Source GPU VM range */
	uint64_t src_mem_range_array;
	/* to KFD: Size of above array */
	uint64_t src_mem_array_size;
	/* to KFD: Destination GPU VM range */
	uint64_t dst_mem_range_array;
	/* to KFD: Size of above array */
	uint64_t dst_mem_array_size;
	/* from KFD: Total amount of bytes copied */
	uint64_t bytes_copied;
};

// TODO schi i hack to hardcode the apertures here, instead they are from KFD
//      the hardcode value is reference from amdkfd/kfd_flat_memory.c
struct process_device_apertures {
	uint64_t lds_base;		/* from KFD */
	uint64_t lds_limit;		/* from KFD */
	uint64_t scratch_base;		/* from KFD */
	uint64_t scratch_limit;		/* from KFD */
	uint64_t gpuvm_base;		/* from KFD */
	uint64_t gpuvm_limit;		/* from KFD */
	uint32_t gpu_id;		/* from KFD */
	uint32_t pad;
};

struct ioctl_open_drm_args {
	int32_t  drm_render_minor;		/* to KFD */
	uint64_t drm_fd;		/* from KFD */
};

struct ioctl_close_drm_args {
	uint64_t handle;		/* to KFD */
};

/* information from /proc/cpuinfo */
typedef struct proc_cpuinfo {
	uint32_t proc_num; /* processor */
	uint32_t apicid; /* apicid */
	char model_name[HSA_PUBLIC_NAME_SIZE]; /* model name */
} proc_cpuinfo_t;

/* CPU cache table for all CPUs on the system. Each entry has the relative CPU
 * info and caches connected to that CPU.
 */
typedef struct cpu_cacheinfo {
	uint32_t len; /* length of the table -> number of online procs */
	int32_t proc_num; /* this cpu's processor number */
	uint32_t num_caches; /* number of caches connected to this cpu */
	HsaCacheProperties *cache_prop; /* a list of cache properties */
} cpu_cacheinfo_t;

enum cache_type {
	CACHE_TYPE_NULL = 0,
	CACHE_TYPE_DATA = 1,
	CACHE_TYPE_INST = 2,
	CACHE_TYPE_UNIFIED = 3
};

typedef struct {
	uint32_t gpu_id;
	HsaCoreProperties *core;
	HsaMemoryProperties *mem;     /* node->NumBanks elements */
	HsaCacheProperties *cache;
	HsaIoLinkProperties *link;
} node_props_t;

struct ioctl_get_system_prop_args {
    uint32_t num_caches;
    uint32_t num_cpus;
    uint32_t num_nodes;
    proc_cpuinfo_t *cpuinfo;
    cpu_cacheinfo_t *cacheinfo;
    HsaSystemProperties *sys_prop;
    node_props_t *node_prop;
};


#define MAKE_GPUVM_APP_BASE(gpu_num) \
	(((uint64_t)(gpu_num) << 61) + 0x1000000000000L)
#define MAKE_GPUVM_APP_LIMIT(base, size) \
	(((uint64_t)(base) & 0xFFFFFF0000000000UL) + (size) - 1)

#define MAKE_SCRATCH_APP_BASE() ((uint64_t)(0x2UL) << 48)
#define MAKE_SCRATCH_APP_LIMIT(base) \
	(((uint64_t)base & 0xFFFFFFFF00000000UL) | 0xFFFFFFFF)

#define MAKE_LDS_APP_BASE() ((uint64_t)(0x1UL) << 48)
#define MAKE_LDS_APP_LIMIT(base) \
	(((uint64_t)(base) & 0xFFFFFFFF00000000UL) | 0xFFFFFFFF)
/*
 * Size of the per-process TBA+TMA buffer: 2 pages
 *
 * The first page is the TBA used for the CWSR ISA code. The second
 * page is used as TMA for daisy changing a user-mode trap handler.
 */
#define KFD_CWSR_TBA_TMA_SIZE (PAGE_SIZE * 2)
#define KFD_CWSR_TMA_OFFSET PAGE_SIZE


/* User mode manages most of the SVM aperture address space. The low
 * 16MB are reserved for kernel use (CWSR trap handler and kernel IB
 * for now).
 */
#define SVM_USER_BASE 0x1000000ull
#define SVM_CWSR_BASE (SVM_USER_BASE - KFD_CWSR_TBA_TMA_SIZE)
#define SVM_IB_BASE   (SVM_CWSR_BASE - PAGE_SIZE)



#define AMDGPU_GPU_PAGE_SHIFT 12

// below from amdgpu_vm.h
/* hardcode that limit for now */
#define AMDGPU_VA_RESERVED_SIZE			(1ULL << 20)

/* VA hole for 48bit addresses on Vega10 */
#define AMDGPU_VA_HOLE_START			0x0000800000000000ULL
#define AMDGPU_VA_HOLE_END			0xffff800000000000ULL
/*
#ifndef PAGE_SIZE
#define PAGE_SIZE   (1<<12)
#define PAGE_SHIFT  (12)
#endif
*/

/* Managed SVM aperture limits: only reserve up to 40 bits (1TB, what
 * GFX8 supports). Need to find at least 4GB of usable address space.
 */
#define SVM_RESERVATION_LIMIT ((1ULL << 40) - 1)
#define SVM_MIN_VM_SIZE (4ULL << 30)
#define IS_CANONICAL_ADDR(a) ((a) < (1ULL << 47))


#define DRM_FIRST_RENDER_NODE 128
#define DRM_LAST_RENDER_NODE 255


/* TODO: need to put the define to other location */
#define INVALID_QUEUE_ID 0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

#define CMDAPI(CMD)    cmdioError_##CMD ,

enum cmdioError {
    cmdioSuccess,                           // No errors
#include "api.inc"
#undef CMDAPI
    cmdioErrorMemoryAllocation,             // Memory allocation error
    cmdioErrorReadRegister,                 // Memory allocation error
    cmdioErrorWriteRegister                // Memory allocation error
};

typedef enum cmdioError cmdioError_t;

// TODO: each device have a CSI interface
// void* cmdio_open(uint32_t device_num = 0);

#define CMDAPI(x) \
    int cmd_##x (ioctl_##x##_args *args);
#include "api.inc"
#undef CMDAPI

// typedef void* (*pfn_open)(uint32_t device_num );

#define CMDAPI(x) \
    typedef int (*pfn_##x)(ioctl_##x##_args *args);
#include "api.inc"
#undef CMDAPI

// typedef int (*pfn_create_queue)(ioctl_create_queue_args *args);

// typedef int (*pfn_cmd_read_register)(uint32_t index, uint32_t *value);
// typedef int (*pfn_cmd_write_register)(uint32_t index, uint32_t value);

// move to mmio set
int cmd_read_register(uint32_t index, uint32_t *value);
int cmd_write_register(uint32_t index, uint32_t value);




#ifdef __cplusplus
}
#endif

