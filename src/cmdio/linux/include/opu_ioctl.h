#ifndef OPU_IOCTL_H_INCLUDED
#define OPU_IOCTL_H_INCLUDED

#include <linux/types.h>
#include <linux/ioctl.h>

#define OPU_IOCTL_MAJOR_VERSION 1
#define OPU_IOCTL_MINOR_VERSION 2

/*
 * Debug revision change log
 *
 * 0.1 - Initial revision
 * 0.2 - Fix to include querying pending event that is both trap and vmfault
 * 1.0 - Removed function to set debug data (renumbering functions broke ABI)
 * 1.1 - Allow attaching to processes that have not opened /dev/opu yet
 * 1.2 - Allow flag option to clear queue status on queue suspend
 * 1.3 - Fix race condition between clear on suspend and trap event handling
 * 1.3 - Fix race condition between clear on suspend and trap event handling
 * 1.4 - Fix bad kfifo free
 * 1.5 - Fix ABA issue between queue snapshot and suspend
 * 2.0 - Return number of queues suspended/resumed and mask invalid/error
 *       array slots
 * 2.1 - Add Set Address Watch, and Clear Address Watch support.
 */
#define OPU_IOCTL_DBG_MAJOR_VERSION	2
#define OPU_IOCTL_DBG_MINOR_VERSION	1

struct opu_ioctl_get_version_args {
	__u32 major_version;	/* from OPU */
	__u32 minor_version;	/* from OPU */
};

/* For opu_ioctl_create_queue_args.queue_type. */
#define OPU_IOC_QUEUE_TYPE_COMPUTE		0x0
#define OPU_IOC_QUEUE_TYPE_SDMA			0x1
#define OPU_IOC_QUEUE_TYPE_COMPUTE_AQL		0x2
#define OPU_IOC_QUEUE_TYPE_SDMA_XGMI		0x3

#define OPU_MAX_QUEUE_PERCENTAGE	100
#define OPU_MAX_QUEUE_PRIORITY		15

struct opu_ioctl_create_queue_args {
	__u64 ring_base_address;	/* to OPU */
	__u64 write_pointer_address;	/* from OPU */
	__u64 read_pointer_address;	/* from OPU */
	__u64 doorbell_offset;	/* from OPU */

	__u32 ring_size;		/* to OPU */
	__u32 gpu_id;		/* to OPU */
	__u32 queue_type;		/* to OPU */
	__u32 queue_percentage;	/* to OPU */
	__u32 queue_priority;	/* to OPU */
	__u32 queue_id;		/* from OPU */

	__u64 eop_buffer_address;	/* to OPU */
	__u64 eop_buffer_size;	/* to OPU */
	__u64 ctx_save_restore_address; /* to OPU */
	__u32 ctx_save_restore_size;	/* to OPU */
	__u32 ctl_stack_size;		/* to OPU */
};

struct opu_ioctl_destroy_queue_args {
	__u32 queue_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_update_queue_args {
	__u64 ring_base_address;	/* to OPU */

	__u32 queue_id;		/* to OPU */
	__u32 ring_size;		/* to OPU */
	__u32 queue_percentage;	/* to OPU */
	__u32 queue_priority;	/* to OPU */
};

struct opu_ioctl_set_cu_mask_args {
	__u32 queue_id;		/* to OPU */
	__u32 num_cu_mask;		/* to OPU */
	__u64 cu_mask_ptr;		/* to OPU */
};

struct opu_ioctl_get_queue_wave_state_args {
	__u64 ctl_stack_address;	/* to OPU */
	__u32 ctl_stack_used_size;	/* from OPU */
	__u32 save_area_used_size;	/* from OPU */
	__u32 queue_id;			/* to OPU */
	__u32 pad;
};

struct opu_queue_snapshot_entry {
	__u64 ring_base_address;
	__u64 write_pointer_address;
	__u64 read_pointer_address;
	__u64 ctx_save_restore_address;
	__u32 queue_id;
	__u32 gpu_id;
	__u32 ring_size;
	__u32 queue_type;
	__u32 queue_status;
	__u32 reserved[19];
};

/* For opu_ioctl_set_memory_policy_args.default_policy and alternate_policy */
#define OPU_IOC_CACHE_POLICY_COHERENT 0
#define OPU_IOC_CACHE_POLICY_NONCOHERENT 1

struct opu_ioctl_set_memory_policy_args {
	__u64 alternate_aperture_base;	/* to OPU */
	__u64 alternate_aperture_size;	/* to OPU */

	__u32 gpu_id;			/* to OPU */
	__u32 default_policy;		/* to OPU */
	__u32 alternate_policy;		/* to OPU */
	__u32 pad;
};

/*
 * All counters are monotonic. They are used for profiling of compute jobs.
 * The profiling is done by userspace.
 *
 * In case of GPU reset, the counter should not be affected.
 */

struct opu_ioctl_get_clock_counters_args {
	__u64 gpu_clock_counter;	/* from OPU */
	__u64 cpu_clock_counter;	/* from OPU */
	__u64 system_clock_counter;	/* from OPU */
	__u64 system_clock_freq;	/* from OPU */

	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_process_device_apertures {
	__u64 lds_base;		/* from OPU */
	__u64 lds_limit;		/* from OPU */
	__u64 scratch_base;		/* from OPU */
	__u64 scratch_limit;		/* from OPU */
	__u64 gpuvm_base;		/* from OPU */
	__u64 gpuvm_limit;		/* from OPU */
	__u32 gpu_id;		/* from OPU */
	__u32 pad;
};

/*
 * OPU_IOC_GET_PROCESS_APERTURES is deprecated. Use
 * OPU_IOC_GET_PROCESS_APERTURES_NEW instead, which supports an
 * unlimited number of GPUs.
 */
#define NUM_OF_SUPPORTED_GPUS 7
struct opu_ioctl_get_process_apertures_args {
	struct opu_process_device_apertures
			process_apertures[NUM_OF_SUPPORTED_GPUS];/* from OPU */

	/* from OPU, should be in the range [1 - NUM_OF_SUPPORTED_GPUS] */
	__u32 num_of_nodes;
	__u32 pad;
};

struct opu_ioctl_get_process_apertures_new_args {
	/* User allocated. Pointer to struct opu_process_device_apertures
	 * filled in by Kernel
	 */
	__u64 opu_process_device_apertures_ptr;
	/* to OPU - indicates amount of memory present in
	 *  opu_process_device_apertures_ptr
	 * from OPU - Number of entries filled by OPU.
	 */
	__u32 num_of_nodes;
	__u32 pad;
};

#define MAX_ALLOWED_NUM_POINTS    100
#define MAX_ALLOWED_AW_BUFF_SIZE 4096
#define MAX_ALLOWED_WAC_BUFF_SIZE  128

struct opu_ioctl_dbg_register_args {
	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_dbg_unregister_args {
	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_dbg_address_watch_args {
	__u64 content_ptr;		/* a pointer to the actual content */
	__u32 gpu_id;		/* to OPU */
	__u32 buf_size_in_bytes;	/*including gpu_id and buf_size */
};

struct opu_ioctl_dbg_wave_control_args {
	__u64 content_ptr;		/* a pointer to the actual content */
	__u32 gpu_id;		/* to OPU */
	__u32 buf_size_in_bytes;	/*including gpu_id and buf_size */
};

/* mapping event types to API spec */
#define	OPU_DBG_EV_STATUS_TRAP		1
#define	OPU_DBG_EV_STATUS_VMFAULT	2
#define	OPU_DBG_EV_STATUS_SUSPENDED	4
#define OPU_DBG_EV_STATUS_NEW_QUEUE	8
#define OPU_DBG_EV_STATUS_HOST_TRAP_TIMEDOUT	16
#define	OPU_DBG_EV_FLAG_CLEAR_STATUS	1

#define OPU_INVALID_QUEUEID	0xffffffff

/* OPU_IOC_DBG_TRAP_ENABLE:
 * ptr:   unused
 * data1: 0=disable, 1=enable
 * data2: queue ID (for future use)
 * data3: return value for fd
 */
#define OPU_IOC_DBG_TRAP_ENABLE 0

/* OPU_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE:
 * ptr:   unused
 * data1: override mode: 0=OR, 1=REPLACE
 * data2: mask
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE 1

/* OPU_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE:
 * ptr:   unused
 * data1: 0=normal, 1=halt, 2=kill, 3=singlestep, 4=disable
 * data2: unused
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE 2

/* OPU_IOC_DBG_TRAP_NODE_SUSPEND:
 * ptr:   pointer to an array of Queues IDs
 * data1: flags
 * data2: number of queues
 * data3: grace period
 */
#define OPU_IOC_DBG_TRAP_NODE_SUSPEND 3

/* OPU_IOC_DBG_TRAP_NODE_RESUME:
 * ptr:   pointer to an array of Queues IDs
 * data1: flags
 * data2: number of queues
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_NODE_RESUME 4

/* OPU_IOC_DBG_TRAP_QUERY_DEBUG_EVENT:
 * ptr: unused
 * data1: queue id (IN/OUT)
 * data2: flags (IN)
 * data3: suspend[2:2], event type [1:0] (OUT)
 */

#define OPU_IOC_DBG_TRAP_QUERY_DEBUG_EVENT 5

/* OPU_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT:
 * ptr: user buffer (IN)
 * data1: flags (IN)
 * data2: number of queue snapshot entries (IN/OUT)
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT 6

/* OPU_IOC_DBG_TRAP_GET_VERSION:
 * ptr: unsused
 * data1: major version (OUT)
 * data2: minor version (OUT)
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_GET_VERSION	7

/* OPU_IOC_DBG_TRAP_CLEAR_ADDRESS_WATCH:
 * ptr: unused
 * data1: watch ID
 * data2: unused
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_CLEAR_ADDRESS_WATCH 8

/* OPU_IOC_DBG_TRAP_SET_ADDRESS_WATCH:
 * ptr:   Watch address
 * data1: Watch ID (OUT)
 * data2: watch_mode: 0=read, 1=nonread, 2=atomic, 3=all
 * data3: watch address mask
 */
#define OPU_IOC_DBG_TRAP_SET_ADDRESS_WATCH 9

/* OPU_IOC_DBG_SEND_HOST_TRAP:
 * ptr:   unused
 * data1: unused
 * data2: unused
 * data3: unused
 */
#define OPU_IOC_DBG_TRAP_SEND_HOST_TRAP 10

struct opu_ioctl_dbg_trap_args {
	__u64 ptr;     /* to OPU -- used for pointer arguments: queue arrays */
	__u32 pid;     /* to OPU */
	__u32 gpu_id;  /* to OPU */
	__u32 op;      /* to OPU */
	__u32 data1;   /* to OPU */
	__u32 data2;   /* to OPU */
	__u32 data3;   /* to OPU */
};

/* Matching HSA_EVENTTYPE */
#define OPU_IOC_EVENT_SIGNAL			0
#define OPU_IOC_EVENT_NODECHANGE		1
#define OPU_IOC_EVENT_DEVICESTATECHANGE		2
#define OPU_IOC_EVENT_HW_EXCEPTION		3
#define OPU_IOC_EVENT_SYSTEM_EVENT		4
#define OPU_IOC_EVENT_DEBUG_EVENT		5
#define OPU_IOC_EVENT_PROFILE_EVENT		6
#define OPU_IOC_EVENT_QUEUE_EVENT		7
#define OPU_IOC_EVENT_MEMORY			8

#define OPU_IOC_WAIT_RESULT_COMPLETE		0
#define OPU_IOC_WAIT_RESULT_TIMEOUT		1
#define OPU_IOC_WAIT_RESULT_FAIL		2

#define OPU_SIGNAL_EVENT_LIMIT			4096

/* For opu_event_data.hw_exception_data.reset_type. */
#define OPU_HW_EXCEPTION_WHOLE_GPU_RESET	0
#define OPU_HW_EXCEPTION_PER_ENGINE_RESET	1

/* For opu_event_data.hw_exception_data.reset_cause. */
#define OPU_HW_EXCEPTION_GPU_HANG	0
#define OPU_HW_EXCEPTION_ECC		1

/* For opu_hsa_memory_exception_data.ErrorType */
#define OPU_MEM_ERR_NO_RAS		0
#define OPU_MEM_ERR_SRAM_ECC		1
#define OPU_MEM_ERR_POISON_CONSUMED	2
#define OPU_MEM_ERR_GPU_HANG		3

struct opu_ioctl_create_event_args {
	__u64 event_page_offset;	/* from OPU */
	__u32 event_trigger_data;	/* from OPU - signal events only */
	__u32 event_type;		/* to OPU */
	__u32 auto_reset;		/* to OPU */
	__u32 node_id;		/* to OPU - only valid for certain
							event types */
	__u32 event_id;		/* from OPU */
	__u32 event_slot_index;	/* from OPU */
};

struct opu_ioctl_destroy_event_args {
	__u32 event_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_set_event_args {
	__u32 event_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_reset_event_args {
	__u32 event_id;		/* to OPU */
	__u32 pad;
};

struct opu_memory_exception_failure {
	__u32 NotPresent;	/* Page not present or supervisor privilege */
	__u32 ReadOnly;	/* Write access to a read-only page */
	__u32 NoExecute;	/* Execute access to a page marked NX */
	__u32 imprecise;	/* Can't determine the	exact fault address */
};

/* memory exception data */
struct opu_hsa_memory_exception_data {
	struct opu_memory_exception_failure failure;
	__u64 va;
	__u32 gpu_id;
	__u32 ErrorType; /* 0 = no RAS error,
			  * 1 = ECC_SRAM,
			  * 2 = Link_SYNFLOOD (poison),
			  * 3 = GPU hang (not attributable to a specific cause),
			  * other values reserved
			  */
};

/* hw exception data */
struct opu_hsa_hw_exception_data {
	__u32 reset_type;
	__u32 reset_cause;
	__u32 memory_lost;
	__u32 gpu_id;
};

/* Event data */
struct opu_event_data {
	union {
		struct opu_hsa_memory_exception_data memory_exception_data;
		struct opu_hsa_hw_exception_data hw_exception_data;
	};				/* From OPU */
	__u64 opu_event_data_ext;	/* pointer to an extension structure
					   for future exception types */
	__u32 event_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_wait_events_args {
	__u64 events_ptr;		/* pointed to struct
					   opu_event_data array, to OPU */
	__u32 num_events;		/* to OPU */
	__u32 wait_for_all;		/* to OPU */
	__u32 timeout;		/* to OPU */
	__u32 wait_result;		/* from OPU */
};

struct opu_ioctl_set_scratch_backing_va_args {
	__u64 va_addr;	/* to OPU */
	__u32 gpu_id;	/* to OPU */
	__u32 pad;
};

struct opu_ioctl_get_tile_config_args {
	/* to OPU: pointer to tile array */
	__u64 tile_config_ptr;
	/* to OPU: pointer to macro tile array */
	__u64 macro_tile_config_ptr;
	/* to OPU: array size allocated by user mode
	 * from OPU: array size filled by kernel
	 */
	__u32 num_tile_configs;
	/* to OPU: array size allocated by user mode
	 * from OPU: array size filled by kernel
	 */
	__u32 num_macro_tile_configs;

	__u32 gpu_id;		/* to OPU */
	__u32 gb_addr_config;	/* from OPU */
	__u32 num_banks;		/* from OPU */
	__u32 num_ranks;		/* from OPU */
	/* struct size can be extended later if needed
	 * without breaking ABI compatibility
	 */
};

struct opu_ioctl_set_trap_handler_args {
	__u64 tba_addr;		/* to OPU */
	__u64 tma_addr;		/* to OPU */
	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_acquire_vm_args {
	__u32 drm_fd;	/* to OPU */
	__u32 gpu_id;	/* to OPU */
};

/* Allocation flags: memory types */
#define OPU_IOC_ALLOC_MEM_FLAGS_VRAM		(1 << 0)
#define OPU_IOC_ALLOC_MEM_FLAGS_GTT		(1 << 1)
#define OPU_IOC_ALLOC_MEM_FLAGS_USERPTR		(1 << 2)
#define OPU_IOC_ALLOC_MEM_FLAGS_DOORBELL	(1 << 3)
#define OPU_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP	(1 << 4)
/* Allocation flags: attributes/access options */
#define OPU_IOC_ALLOC_MEM_FLAGS_WRITABLE	(1 << 31)
#define OPU_IOC_ALLOC_MEM_FLAGS_EXECUTABLE	(1 << 30)
#define OPU_IOC_ALLOC_MEM_FLAGS_PUBLIC		(1 << 29)
#define OPU_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE	(1 << 28)
#define OPU_IOC_ALLOC_MEM_FLAGS_AQL_QUEUE_MEM	(1 << 27)
#define OPU_IOC_ALLOC_MEM_FLAGS_COHERENT	(1 << 26)
#define OPU_IOC_ALLOC_MEM_FLAGS_UNCACHED	(1 << 25)

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
 * @flags:       memory type and attributes. See OPU_IOC_ALLOC_MEM_FLAGS above
 */
struct opu_ioctl_alloc_memory_of_gpu_args {
	__u64 va_addr;		/* to OPU */
	__u64 size;		/* to OPU */
	__u64 handle;		/* from OPU */
	__u64 mmap_offset;	/* to OPU (userptr), from OPU (mmap offset) */
	__u32 gpu_id;		/* to OPU */
	__u32 flags;
};

/* Free memory allocated with opu_ioctl_alloc_memory_of_gpu
 *
 * @handle: memory handle returned by alloc
 */
struct opu_ioctl_free_memory_of_gpu_args {
	__u64 handle;		/* to OPU */
};

/* Map memory to one or more GPUs
 *
 * @handle:                memory handle returned by alloc
 * @device_ids_array_ptr:  array of gpu_ids (__u32 per device)
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
struct opu_ioctl_map_memory_to_gpu_args {
	__u64 handle;			/* to OPU */
	__u64 device_ids_array_ptr;	/* to OPU */
	__u32 n_devices;		/* to OPU */
	__u32 n_success;		/* to/from OPU */
};

/* Unmap memory from one or more GPUs
 *
 * same arguments as for mapping
 */
struct opu_ioctl_unmap_memory_from_gpu_args {
	__u64 handle;			/* to OPU */
	__u64 device_ids_array_ptr;	/* to OPU */
	__u32 n_devices;		/* to OPU */
	__u32 n_success;		/* to/from OPU */
};

/* Allocate GWS for specific queue
 *
 * @queue_id:    queue's id that GWS is allocated for
 * @num_gws:     how many GWS to allocate
 * @first_gws:   index of the first GWS allocated.
 *               only support contiguous GWS allocation
 */
struct opu_ioctl_alloc_queue_gws_args {
	__u32 queue_id;		/* to OPU */
	__u32 num_gws;		/* to OPU */
	__u32 first_gws;	/* from OPU */
	__u32 pad;		/* to OPU */
};

struct opu_ioctl_get_dmabuf_info_args {
	__u64 size;		/* from OPU */
	__u64 metadata_ptr;	/* to OPU */
	__u32 metadata_size;	/* to OPU (space allocated by user)
				 * from OPU (actual metadata size)
				 */
	__u32 gpu_id;	/* from OPU */
	__u32 flags;		/* from OPU (OPU_IOC_ALLOC_MEM_FLAGS) */
	__u32 dmabuf_fd;	/* to OPU */
};

struct opu_ioctl_import_dmabuf_args {
	__u64 va_addr;	/* to OPU */
	__u64 handle;	/* from OPU */
	__u32 gpu_id;	/* to OPU */
	__u32 dmabuf_fd;	/* to OPU */
};

/*
 * OPU SMI(System Management Interface) events
 */
enum opu_smi_event {
	OPU_SMI_EVENT_NONE = 0, /* not used */
	OPU_SMI_EVENT_VMFAULT = 1, /* event start counting at 1 */
	OPU_SMI_EVENT_THERMAL_THROTTLE = 2,
	OPU_SMI_EVENT_GPU_PRE_RESET = 3,
	OPU_SMI_EVENT_GPU_POST_RESET = 4,
};

#define OPU_SMI_EVENT_MASK_FROM_INDEX(i) (1ULL << ((i) - 1))

struct opu_ioctl_smi_events_args {
	__u32 gpuid;	/* to OPU */
	__u32 anon_fd;	/* from OPU */
};

/**
 * opu_ioctl_spm_op - SPM ioctl operations
 *
 * @OPU_IOCTL_SPM_OP_ACQUIRE: acquire exclusive access to SPM
 * @OPU_IOCTL_SPM_OP_RELEASE: release exclusive access to SPM
 * @OPU_IOCTL_SPM_OP_SET_DEST_BUF: set or unset destination buffer for SPM streaming
 */
enum opu_ioctl_spm_op {
	OPU_IOCTL_SPM_OP_ACQUIRE,
	OPU_IOCTL_SPM_OP_RELEASE,
	OPU_IOCTL_SPM_OP_SET_DEST_BUF
};

/**
 * opu_ioctl_spm_args - Arguments for SPM ioctl
 *
 * @op[in]:            specifies the operation to perform
 * @gpu_id[in]:        GPU ID of the GPU to profile
 * @dst_buf[in]:       used for the address of the destination buffer
 *                      in @OPU_IOCTL_SPM_SET_DEST_BUFFER
 * @buf_size[in]:      size of the destination buffer
 * @timeout[in/out]:   [in]: timeout in milliseconds, [out]: amount of time left
 *                      `in the timeout window
 * @bytes_copied[out]: amount of data that was copied to the previous dest_buf
 * @has_data_loss:     boolean indicating whether data was lost
 *                      (e.g. due to a ring-buffer overflow)
 *
 * This ioctl performs different functions depending on the @op parameter.
 *
 * OPU_IOCTL_SPM_OP_ACQUIRE
 * ------------------------
 *
 * Acquires exclusive access of SPM on the specified @gpu_id for the calling process.
 * This must be called before using OPU_IOCTL_SPM_OP_SET_DEST_BUF.
 *
 * OPU_IOCTL_SPM_OP_RELEASE
 * ------------------------
 *
 * Releases exclusive access of SPM on the specified @gpu_id for the calling process,
 * which allows another process to acquire it in the future.
 *
 * OPU_IOCTL_SPM_OP_SET_DEST_BUF
 * -----------------------------
 *
 * If @dst_buf is NULL, the destination buffer address is unset and copying of counters
 * is stopped.
 *
 * If @dst_buf is not NULL, it specifies the pointer to a new destination buffer.
 * @buf_size specifies the size of the buffer.
 *
 * If @timeout is non-0, the call will wait for up to @timeout ms for the previous
 * buffer to be filled. If previous buffer to be filled before timeout, the @timeout
 * will be updated value with the time remaining. If the timeout is exceeded, the function
 * copies any partial data available into the previous user buffer and returns success.
 * The amount of valid data in the previous user buffer is indicated by @bytes_copied.
 *
 * If @timeout is 0, the function immediately replaces the previous destination buffer
 * without waiting for the previous buffer to be filled. That means the previous buffer
 * may only be partially filled, and @bytes_copied will indicate how much data has been
 * copied to it.
 *
 * If data was lost, e.g. due to a ring buffer overflow, @has_data_loss will be non-0.
 *
 * Returns negative error code on failure, 0 on success.
 */
struct opu_ioctl_spm_args {
	__u64 dest_buf;
	__u32 buf_size;
	__u32 op;
	__u32 timeout;
	__u32 gpu_id;
	__u32 bytes_copied;
	__u32 has_data_loss;
};

/* Register offset inside the remapped mmio page
 */
enum opu_mmio_remap {
	OPU_MMIO_REMAP_HDP_MEM_FLUSH_CNTL = 0,
	OPU_MMIO_REMAP_HDP_REG_FLUSH_CNTL = 4,
};

struct opu_ioctl_ipc_export_handle_args {
	__u64 handle;		/* to OPU */
	__u32 share_handle[4];	/* from OPU */
	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_ioctl_ipc_import_handle_args {
	__u64 handle;		/* from OPU */
	__u64 va_addr;		/* to OPU */
	__u64 mmap_offset;		/* from OPU */
	__u32 share_handle[4];	/* to OPU */
	__u32 gpu_id;		/* to OPU */
	__u32 pad;
};

struct opu_memory_range {
	__u64 va_addr;
	__u64 size;
};

/* flags definitions
 * BIT0: 0: read operation, 1: write operation.
 * This also identifies if the src or dst array belongs to remote process
 */
#define OPU_CROSS_MEMORY_RW_BIT (1 << 0)
#define OPU_SET_CROSS_MEMORY_READ(flags) (flags &= ~OPU_CROSS_MEMORY_RW_BIT)
#define OPU_SET_CROSS_MEMORY_WRITE(flags) (flags |= OPU_CROSS_MEMORY_RW_BIT)
#define OPU_IS_CROSS_MEMORY_WRITE(flags) (flags & OPU_CROSS_MEMORY_RW_BIT)

struct opu_ioctl_cross_memory_copy_args {
	/* to OPU: Process ID of the remote process */
	__u32 pid;
	/* to OPU: See above definition */
	__u32 flags;
	/* to OPU: Source GPU VM range */
	__u64 src_mem_range_array;
	/* to OPU: Size of above array */
	__u64 src_mem_array_size;
	/* to OPU: Destination GPU VM range */
	__u64 dst_mem_range_array;
	/* to OPU: Size of above array */
	__u64 dst_mem_array_size;
	/* from OPU: Total amount of bytes copied */
	__u64 bytes_copied;
};

#define OPU_IOCTL_BASE 'K'
#define OPU_IO(nr)			_IO(OPU_IOCTL_BASE, nr)
#define OPU_IOR(nr, type)		_IOR(OPU_IOCTL_BASE, nr, type)
#define OPU_IOW(nr, type)		_IOW(OPU_IOCTL_BASE, nr, type)
#define OPU_IOWR(nr, type)		_IOWR(OPU_IOCTL_BASE, nr, type)

#define OPU_IOC_GET_VERSION			\
		OPU_IOR(0x01, struct opu_ioctl_get_version_args)

#define OPU_IOC_CREATE_QUEUE			\
		OPU_IOWR(0x02, struct opu_ioctl_create_queue_args)

#define OPU_IOC_DESTROY_QUEUE		\
		OPU_IOWR(0x03, struct opu_ioctl_destroy_queue_args)

#define OPU_IOC_SET_MEMORY_POLICY		\
		OPU_IOW(0x04, struct opu_ioctl_set_memory_policy_args)

#define OPU_IOC_GET_CLOCK_COUNTERS		\
		OPU_IOWR(0x05, struct opu_ioctl_get_clock_counters_args)

#define OPU_IOC_GET_PROCESS_APERTURES	\
		OPU_IOR(0x06, struct opu_ioctl_get_process_apertures_args)

#define OPU_IOC_UPDATE_QUEUE			\
		OPU_IOW(0x07, struct opu_ioctl_update_queue_args)

#define OPU_IOC_CREATE_EVENT			\
		OPU_IOWR(0x08, struct opu_ioctl_create_event_args)

#define OPU_IOC_DESTROY_EVENT		\
		OPU_IOW(0x09, struct opu_ioctl_destroy_event_args)

#define OPU_IOC_SET_EVENT			\
		OPU_IOW(0x0A, struct opu_ioctl_set_event_args)

#define OPU_IOC_RESET_EVENT			\
		OPU_IOW(0x0B, struct opu_ioctl_reset_event_args)

#define OPU_IOC_WAIT_EVENTS			\
		OPU_IOWR(0x0C, struct opu_ioctl_wait_events_args)

#define OPU_IOC_DBG_REGISTER			\
		OPU_IOW(0x0D, struct opu_ioctl_dbg_register_args)

#define OPU_IOC_DBG_UNREGISTER		\
		OPU_IOW(0x0E, struct opu_ioctl_dbg_unregister_args)

#define OPU_IOC_DBG_ADDRESS_WATCH		\
		OPU_IOW(0x0F, struct opu_ioctl_dbg_address_watch_args)

#define OPU_IOC_DBG_WAVE_CONTROL		\
		OPU_IOW(0x10, struct opu_ioctl_dbg_wave_control_args)

#define OPU_IOC_SET_SCRATCH_BACKING_VA	\
		OPU_IOWR(0x11, struct opu_ioctl_set_scratch_backing_va_args)

#define OPU_IOC_GET_TILE_CONFIG		\
		OPU_IOWR(0x12, struct opu_ioctl_get_tile_config_args)

#define OPU_IOC_SET_TRAP_HANDLER		\
		OPU_IOW(0x13, struct opu_ioctl_set_trap_handler_args)

#define OPU_IOC_GET_PROCESS_APERTURES_NEW	\
		OPU_IOWR(0x14,		\
			struct opu_ioctl_get_process_apertures_new_args)

#define OPU_IOC_ACQUIRE_VM			\
		OPU_IOW(0x15, struct opu_ioctl_acquire_vm_args)

#define OPU_IOC_ALLOC_MEMORY_OF_GPU		\
		OPU_IOWR(0x16, struct opu_ioctl_alloc_memory_of_gpu_args)

#define OPU_IOC_FREE_MEMORY_OF_GPU		\
		OPU_IOW(0x17, struct opu_ioctl_free_memory_of_gpu_args)

#define OPU_IOC_MAP_MEMORY_TO_GPU		\
		OPU_IOWR(0x18, struct opu_ioctl_map_memory_to_gpu_args)

#define OPU_IOC_UNMAP_MEMORY_FROM_GPU	\
		OPU_IOWR(0x19, struct opu_ioctl_unmap_memory_from_gpu_args)

#define OPU_IOC_SET_CU_MASK		\
		OPU_IOW(0x1A, struct opu_ioctl_set_cu_mask_args)

#define OPU_IOC_GET_QUEUE_WAVE_STATE		\
		OPU_IOWR(0x1B, struct opu_ioctl_get_queue_wave_state_args)

#define OPU_IOC_GET_DMABUF_INFO		\
		OPU_IOWR(0x1C, struct opu_ioctl_get_dmabuf_info_args)

#define OPU_IOC_IMPORT_DMABUF		\
		OPU_IOWR(0x1D, struct opu_ioctl_import_dmabuf_args)

#define OPU_IOC_ALLOC_QUEUE_GWS		\
		OPU_IOWR(0x1E, struct opu_ioctl_alloc_queue_gws_args)

#define OPU_COMMAND_START		0x01
#define OPU_COMMAND_END		0x1F

/* non-upstream ioctls */
#define OPU_IOC_IPC_IMPORT_HANDLE                                    \
		OPU_IOWR(0x80, struct opu_ioctl_ipc_import_handle_args)

#define OPU_IOC_IPC_EXPORT_HANDLE		\
		OPU_IOWR(0x81, struct opu_ioctl_ipc_export_handle_args)

#define OPU_IOC_DBG_TRAP			\
		OPU_IOWR(0x82, struct opu_ioctl_dbg_trap_args)

#define OPU_IOC_CROSS_MEMORY_COPY		\
		OPU_IOWR(0x83, struct opu_ioctl_cross_memory_copy_args)

#define OPU_IOC_RLC_SPM		\
		OPU_IOWR(0x84, struct opu_ioctl_spm_args)



#define OPU_COMMAND_START_2		0x80
#define OPU_COMMAND_END_2		0x85

#endif
