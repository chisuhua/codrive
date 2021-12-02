#ifndef OPU_PRIV_H_INCLUDED
#define OPU_PRIV_H_INCLUDED

#include <linux/hashtable.h>
#include <linux/mmu_notifier.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <opu_ioctl.h>
#include <linux/idr.h>
#include <linux/kfifo.h>
#include <linux/seq_file.h>
#include <linux/kref.h>
#include <linux/sysfs.h>
#include <linux/device_cgroup.h>
#include <drm/drm_file.h>
#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_ioctl.h>
//#include <kgd_opu_interface.h>
#include <linux/swap.h>

//#include "amd_shared.h"
#include "opu.h"

#define OPU_MAX_RING_ENTRY_SIZE	8

#define OPU_SYSFS_FILE_MODE 0444

/* GPU ID hash width in bits */
#define OPU_GPU_ID_HASH_WIDTH 16

/* Use upper bits of mmap offset to store OPU driver specific information.
 * BITS[63:62] - Encode MMAP type
 * BITS[61:46] - Encode gpu_id. To identify to which GPU the offset belongs to
 * BITS[45:0]  - MMAP offset value
 *
 * NOTE: struct vm_area_struct.vm_pgoff uses offset in pages. Hence, these
 *  defines are w.r.t to PAGE_SIZE
 */
#define OPU_MMAP_TYPE_SHIFT	62
#define OPU_MMAP_TYPE_MASK	(0x3ULL << OPU_MMAP_TYPE_SHIFT)
#define OPU_MMAP_TYPE_DOORBELL	(0x3ULL << OPU_MMAP_TYPE_SHIFT)
#define OPU_MMAP_TYPE_EVENTS	(0x2ULL << OPU_MMAP_TYPE_SHIFT)
#define OPU_MMAP_TYPE_RESERVED_MEM	(0x1ULL << OPU_MMAP_TYPE_SHIFT)
#define OPU_MMAP_TYPE_MMIO	(0x0ULL << OPU_MMAP_TYPE_SHIFT)

#define OPU_MMAP_GPU_ID_SHIFT 46
#define OPU_MMAP_GPU_ID_MASK (((1ULL << OPU_GPU_ID_HASH_WIDTH) - 1) \
				<< OPU_MMAP_GPU_ID_SHIFT)
#define OPU_MMAP_GPU_ID(gpu_id) ((((uint64_t)gpu_id) << OPU_MMAP_GPU_ID_SHIFT)\
				& OPU_MMAP_GPU_ID_MASK)
#define OPU_MMAP_GET_GPU_ID(offset)    ((offset & OPU_MMAP_GPU_ID_MASK) \
				>> OPU_MMAP_GPU_ID_SHIFT)

/* Macro for allocating structures */
#define opu_alloc_struct(ptr_to_struct)	\
	((typeof(ptr_to_struct)) kzalloc(sizeof(*ptr_to_struct), GFP_KERNEL))

/*
 * Size of the per-process TBA+TMA buffer: 2 pages
 *
 * The first page is the TBA used for the CWSR ISA code. The second
 * page is used as TMA for user-mode trap handler setup in daisy-chain mode.
 */
#define OPU_CWSR_TBA_TMA_SIZE (PAGE_SIZE * 2)
#define OPU_CWSR_TMA_OFFSET PAGE_SIZE



#define OPU_UNMAP_LATENCY_MS	(4000)


/*
 * Kernel module parameter to specify maximum number of supported queues per
 * device
 */
extern int max_num_of_queues_per_device;


/* Kernel module parameter to specify the scheduling policy */
extern int sched_policy;

/*
 * Kernel module parameter to specify the maximum process
 * number per HW scheduler
 */
extern int hws_max_conc_proc;

extern int cwsr_enable;

/*
 * Kernel module parameter to specify whether to send sigterm to HSA process on
 * unhandled exception
 */
extern int send_sigterm;

/*
 * This kernel module is used to simulate large bar machine on non-large bar
 * enabled machines.
 */
extern int debug_largebar;

/*
 * Ignore CRAT table during OPU initialization, can be used to work around
 * broken CRAT tables on some AMD systems
 */
extern int ignore_crat;

/* Set sh_mem_config.retry_disable on GFX v9 */
extern int amdgpu_noretry;

/* Halt if HWS hang is detected */
extern int halt_if_hws_hang;

/* Whether MEC FW support GWS barriers */
extern bool hws_gws_support;

/* Queue preemption timeout in ms */
extern int queue_preemption_timeout_ms;

/*
 * Don't evict process queues on vm fault
 */
extern int amdgpu_no_queue_eviction_on_vm_fault;

/* Enable eviction debug messages */
extern bool debug_evictions;

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

#define OPU_IS_SOC15(chip) ((chip) >= CHIP_VEGA10)
/*
struct opu_event_interrupt_class {
	bool (*interrupt_isr)(struct opu_dev *dev,
			const uint32_t *ih_ring_entry, uint32_t *patched_ihre,
			bool *patched_flag);
	void (*interrupt_wq)(struct opu_dev *dev,
			const uint32_t *ih_ring_entry);
};
*/

struct core_vm_fault_info {
	uint64_t	page_addr;
	uint32_t	vmid;
	uint32_t	mc_id;
	uint32_t	status;
	bool		prot_valid;
	bool		prot_read;
	bool		prot_write;
	bool		prot_exec;
};

/* For getting GPU local memory information from KGD */
struct core_local_mem_info {
	uint64_t local_mem_size_private;
	uint64_t local_mem_size_public;
	uint32_t vram_width;
	uint32_t mem_clk_max;
};

enum core_memory_pool {
	CORE_POOL_SYSTEM_CACHEABLE = 1,
	CORE_POOL_SYSTEM_WRITECOMBINE = 2,
	CORE_POOL_FRAMEBUFFER = 3,
};

struct opu_device_info {
	//enum amd_asic_type asic_family;
	//const char *asic_name;
	// const struct opu_event_interrupt_class *event_interrupt_class;
	unsigned int max_pasid_bits;
	unsigned int max_no_of_hqd;
	unsigned int doorbell_size;
	size_t ih_ring_entry_size;
	uint8_t num_of_watch_points;
	uint16_t mqd_size_aligned;
	bool supports_cwsr;
	bool needs_iommu_device;
	bool needs_pci_atomics;
	unsigned int num_sdma_engines;
	unsigned int num_xgmi_sdma_engines;
	unsigned int num_sdma_queues_per_engine;
};

struct opu_mem_obj {
	uint32_t range_start;
	uint32_t range_end;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
	void *gtt_mem;
};


enum opu_mempool {
	OPU_MEMPOOL_SYSTEM_CACHEABLE = 1,
	OPU_MEMPOOL_SYSTEM_WRITECOMBINE = 2,
	OPU_MEMPOOL_FRAMEBUFFER = 3,
};

/* Character device interface */
int opu_chardev_init(void);
void opu_chardev_exit(void);
struct device *opu_chardev(void);

/**
 * enum opu_unmap_queues_filter - Enum for queue filters.
 *
 * @OPU_UNMAP_QUEUES_FILTER_SINGLE_QUEUE: Preempts single queue.
 *
 * @OPU_UNMAP_QUEUES_FILTER_ALL_QUEUES: Preempts all queues in the
 *						running queues list.
 *
 * @OPU_UNMAP_QUEUES_FILTER_BY_PASID: Preempts queues that belongs to
 *						specific process.
 *
 */
enum opu_unmap_queues_filter {
	OPU_UNMAP_QUEUES_FILTER_SINGLE_QUEUE,
	OPU_UNMAP_QUEUES_FILTER_ALL_QUEUES,
	OPU_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES,
	OPU_UNMAP_QUEUES_FILTER_BY_PASID
};

/**
 * enum opu_queue_type - Enum for various queue types.
 *
 * @OPU_QUEUE_TYPE_COMPUTE: Regular user mode queue type.
 *
 * @OPU_QUEUE_TYPE_SDMA: SDMA user mode queue type.
 *
 * @OPU_QUEUE_TYPE_HIQ: HIQ queue type.
 *
 * @OPU_QUEUE_TYPE_DIQ: DIQ queue type.
 *
 * @OPU_QUEUE_TYPE_SDMA_XGMI: Special SDMA queue for XGMI interface.
 */
enum opu_queue_type  {
	OPU_QUEUE_TYPE_COMPUTE,
	OPU_QUEUE_TYPE_SDMA,
	OPU_QUEUE_TYPE_HIQ,
	OPU_QUEUE_TYPE_DIQ,
	OPU_QUEUE_TYPE_SDMA_XGMI
};

enum opu_queue_format {
	OPU_QUEUE_FORMAT_PM4,
	OPU_QUEUE_FORMAT_AQL
};

enum OPU_QUEUE_PRIORITY {
	OPU_QUEUE_PRIORITY_MINIMUM = 0,
	OPU_QUEUE_PRIORITY_MAXIMUM = 15
};

/**
 * struct queue_properties
 *
 * @type: The queue type.
 *
 * @queue_id: Queue identifier.
 *
 * @queue_address: Queue ring buffer address.
 *
 * @queue_size: Queue ring buffer size.
 *
 * @priority: Defines the queue priority relative to other queues in the
 * process.
 * This is just an indication and HW scheduling may override the priority as
 * necessary while keeping the relative prioritization.
 * the priority granularity is from 0 to f which f is the highest priority.
 * currently all queues are initialized with the highest priority.
 *
 * @queue_percent: This field is partially implemented and currently a zero in
 * this field defines that the queue is non active.
 *
 * @read_ptr: User space address which points to the number of dwords the
 * cp read from the ring buffer. This field updates automatically by the H/W.
 *
 * @write_ptr: Defines the number of dwords written to the ring buffer.
 *
 * @doorbell_ptr: Notifies the H/W of new packet written to the queue ring
 * buffer. This field should be similar to write_ptr and the user should
 * update this field after updating the write_ptr.
 *
 * @doorbell_off: The doorbell offset in the doorbell pci-bar.
 *
 * @is_interop: Defines if this is a interop queue. Interop queue means that
 * the queue can access both graphics and compute resources.
 *
 * @is_evicted: Defines if the queue is evicted. Only active queues
 * are evicted, rendering them inactive.
 *
 * @is_active: Defines if the queue is active or not. @is_active and
 * @is_evicted are protected by the DQM lock.
 *
 * @is_gws: Defines if the queue has been updated to be GWS-capable or not.
 * @is_gws should be protected by the DQM lock, since changing it can yield the
 * possibility of updating DQM state on number of GWS queues.
 *
 * @vmid: If the scheduling mode is no cp scheduling the field defines the vmid
 * of the queue.
 *
 * This structure represents the queue properties for each queue no matter if
 * it's user mode or kernel mode queue.
 *
 */
struct queue_properties {
	enum opu_queue_type type;
	enum opu_queue_format format;
	unsigned int queue_id;
	uint64_t queue_address;
	uint64_t  queue_size;
	uint32_t priority;
	uint32_t queue_percent;
	uint32_t *read_ptr;
	uint32_t *write_ptr;
	void __iomem *doorbell_ptr;
	uint32_t doorbell_off;
	bool is_interop;
	bool is_evicted;
	bool is_active;
	bool is_gws;
	/* Not relevant for user mode queues in cp scheduling */
	unsigned int vmid;
	/* Relevant only for sdma queues*/
	uint32_t sdma_engine_id;
	uint32_t sdma_queue_id;
	uint32_t sdma_vm_addr;
	/* Relevant only for VI */
	uint64_t eop_ring_buffer_address;
	uint32_t eop_ring_buffer_size;
	uint64_t ctx_save_restore_area_address;
	uint32_t ctx_save_restore_area_size;
	uint32_t ctl_stack_size;
	uint64_t tba_addr;
	uint64_t tma_addr;
	/* Relevant for CU */
	uint32_t cu_mask_count; /* Must be a multiple of 32 */
	uint32_t *cu_mask;
};

#define QUEUE_IS_ACTIVE(q) ((q).queue_size > 0 &&	\
			    (q).queue_address != 0 &&	\
			    (q).queue_percent > 0 &&	\
			    !(q).is_evicted)

/**
 * struct queue
 *
 * @list: Queue linked list.
 *
 * @mqd: The queue MQD (memory queue descriptor).
 *
 * @mqd_mem_obj: The MQD local gpu memory object.
 *
 * @gart_mqd_addr: The MQD gart mc address.
 *
 * @properties: The queue properties.
 *
 * @mec: Used only in no cp scheduling mode and identifies to micro engine id
 *	 that the queue should be executed on.
 *
 * @pipe: Used only in no cp scheduling mode and identifies the queue's pipe
 *	  id.
 *
 * @queue: Used only in no cp scheduliong mode and identifies the queue's slot.
 *
 * @process: The opu process that created this queue.
 *
 * @device: The opu device that created this queue.
 *
 * @gws: Pointing to gws kgd_mem if this is a gws control queue; NULL
 * otherwise.
 *
 * This structure represents user mode compute queues.
 * It contains all the necessary data to handle such queues.
 *
 */

struct queue {
	struct list_head list;
	void *mqd;
	struct opu_mem_obj *mqd_mem_obj;
	uint64_t gart_mqd_addr;
	struct queue_properties properties;

	uint32_t mec;
	uint32_t pipe;
	uint32_t queue;

	unsigned int sdma_id;
	unsigned int doorbell_id;

	struct opu_process	*process;
	struct opu_dev		*device;
	void *gws;

	/* procfs */
	struct kobject kobj;
};

enum OPU_MQD_TYPE {
	OPU_MQD_TYPE_HIQ = 0,		/* for hiq */
	OPU_MQD_TYPE_CP,		/* for cp queues and diq */
	OPU_MQD_TYPE_SDMA,		/* for sdma queues */
	OPU_MQD_TYPE_DIQ,		/* for diq */
	OPU_MQD_TYPE_MAX
};

enum OPU_PIPE_PRIORITY {
	OPU_PIPE_PRIORITY_CS_LOW = 0,
	OPU_PIPE_PRIORITY_CS_MEDIUM,
	OPU_PIPE_PRIORITY_CS_HIGH
};

struct scheduling_resources {
	unsigned int vmid_mask;
	enum opu_queue_type type;
	uint64_t queue_mask;
	uint64_t gws_mask;
	uint32_t oac_mask;
	uint32_t gds_heap_base;
	uint32_t gds_heap_size;
};

struct process_queue_manager {
	/* data */
	struct opu_process	*process;
	struct list_head	queues;
	unsigned long		*queue_slot_bitmap;
};

struct qcm_process_device {
	/* The Device Queue Manager that owns this data */
	struct device_queue_manager *dqm;
	struct process_queue_manager *pqm;
	/* Queues list */
	struct list_head queues_list;
	struct list_head priv_queue_list;

	unsigned int queue_count;
	unsigned int vmid;
	bool is_debug;
	unsigned int evicted; /* eviction counter, 0=active */

	/* This flag tells if we should reset all wavefronts on
	 * process termination
	 */
	bool reset_wavefronts;

	/* This flag tells us if this process has a GWS-capable
	 * queue that will be mapped into the runlist. It's
	 * possible to request a GWS BO, but not have the queue
	 * currently mapped, and this changes how the MAP_PROCESS
	 * PM4 packet is configured.
	 */
	bool mapped_gws_queue;

	/* All the memory management data should be here too */
	uint64_t gds_context_area;
	/* Contains page table flags such as AMDGPU_PTE_VALID since gfx9 */
	uint64_t page_table_base;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t gds_size;
	uint32_t num_gws;
	uint32_t num_oac;
	uint32_t sh_hidden_private_base;

	/* CWSR memory */
	void *cwsr_kaddr;
	uint64_t cwsr_base;
	uint64_t tba_addr;
	uint64_t tma_addr;

	/* IB memory */
	uint64_t ib_base;
	void *ib_kaddr;

	/* doorbell resources per process per device */
	unsigned long *doorbell_bitmap;
};

/* OPU Memory Eviction */

/* Approx. wait time before attempting to restore evicted BOs */
#define PROCESS_RESTORE_TIME_MS 100
/* Approx. back off time if restore fails due to lack of memory */
#define PROCESS_BACK_OFF_TIME_MS 100
/* Approx. time before evicting the process again */
#define PROCESS_ACTIVE_TIME_MS 10

/* 8 byte handle containing GPU ID in the most significant 4 bytes and
 * idr_handle in the least significant 4 bytes
 */
#define MAKE_HANDLE(gpu_id, idr_handle) \
	(((uint64_t)(gpu_id) << 32) + idr_handle)
#define GET_GPU_ID(handle) (handle >> 32)
#define GET_IDR_HANDLE(handle) (handle & 0xFFFFFFFF)

enum opu_pdd_bound {
	PDD_UNBOUND = 0,
	PDD_BOUND,
	PDD_BOUND_SUSPENDED,
};

#define MAX_SYSFS_FILENAME_LEN 15

/*
 * SDMA counter runs at 100MHz frequency.
 * We display SDMA activity in microsecond granularity in sysfs.
 * As a result, the divisor is 100.
 */
#define SDMA_ACTIVITY_DIVISOR  100

/* Data that is per-process-per device. */
struct opu_process_device {
	/* The device that owns this data. */
	struct opu_dev *dev;

	/* The process that owns this opu_process_device. */
	struct opu_process *process;

	/* per-process-per device QCM data structure */
	struct qcm_process_device qpd;

	/*Apertures*/
	uint64_t lds_base;
	uint64_t lds_limit;
	uint64_t gpuvm_base;
	uint64_t gpuvm_limit;
	uint64_t scratch_base;
	uint64_t scratch_limit;

	/* VM context for GPUVM allocations */
	struct file *drm_file;
	void *drm_priv;

	/* GPUVM allocations storage */
	struct idr alloc_idr;

	/* Flag used to tell the pdd has dequeued from the dqm.
	 * This is used to prevent dev->dqm->ops.process_termination() from
	 * being called twice when it is already called in IOMMU callback
	 * function.
	 */
	bool already_dequeued;
	bool runtime_inuse;

	/* Is this process/pasid bound to this device? (amd_iommu_bind_pasid) */
	enum opu_pdd_bound bound;

	/* VRAM usage */
	uint64_t vram_usage;
	struct attribute attr_vram;
	char vram_filename[MAX_SYSFS_FILENAME_LEN];

	/* SDMA activity tracking */
	uint64_t sdma_past_activity_counter;
	struct attribute attr_sdma;
	char sdma_filename[MAX_SYSFS_FILENAME_LEN];

	/* Eviction activity tracking */
	uint64_t last_evict_timestamp;
	atomic64_t evict_duration_counter;
	struct attribute attr_evict;

	struct kobject *kobj_stats;
	unsigned int doorbell_index;

	/*
	 * @cu_occupancy: Reports occupancy of Compute Units (CU) of a process
	 * that is associated with device encoded by "this" struct instance. The
	 * value reflects CU usage by all of the waves launched by this process
	 * on this device. A very important property of occupancy parameter is
	 * that its value is a snapshot of current use.
	 *
	 * Following is to be noted regarding how this parameter is reported:
	 *
	 *  The number of waves that a CU can launch is limited by couple of
	 *  parameters. These are encoded by struct amdgpu_cu_info instance
	 *  that is part of every device definition. For GFX9 devices this
	 *  translates to 40 waves (simd_per_cu * max_waves_per_simd) when waves
	 *  do not use scratch memory and 32 waves (max_scratch_slots_per_cu)
	 *  when they do use scratch memory. This could change for future
	 *  devices and therefore this example should be considered as a guide.
	 *
	 *  All CU's of a device are available for the process. This may not be true
	 *  under certain conditions - e.g. CU masking.
	 *
	 *  Finally number of CU's that are occupied by a process is affected by both
	 *  number of CU's a device has along with number of other competing processes
	 */
	struct attribute attr_cu_occupancy;

	/* sysfs counters for GPU retry fault and page migration tracking */
	struct kobject *kobj_counters;
	struct attribute attr_faults;
	struct attribute attr_page_in;
	struct attribute attr_page_out;
	uint64_t faults;
	uint64_t page_in;
	uint64_t page_out;
};

#define qpd_to_pdd(x) container_of(x, struct opu_process_device, qpd)

struct svm_range_list {
	struct mutex			lock;
	struct rb_root_cached		objects;
	struct list_head		list;
	struct work_struct		deferred_list_work;
	struct list_head		deferred_range_list;
	spinlock_t			deferred_list_lock;
	atomic_t			evicted_ranges;
	struct delayed_work		restore_work;
	DECLARE_BITMAP(bitmap_supported, MAX_GPU_INSTANCE);
};

/* Process data */
struct opu_process {
	/*
	 * opu_process are stored in an mm_struct*->opu_process*
	 * hash table (opu_processes in opu_process.c)
	 */
	struct hlist_node opu_processes;

	/*
	 * Opaque pointer to mm_struct. We don't hold a reference to
	 * it so it should never be dereferenced from here. This is
	 * only used for looking up processes by their mm.
	 */
	void *mm;

	struct kref ref;
	struct work_struct release_work;

	struct mutex mutex;

	/*
	 * In any process, the thread that started main() is the lead
	 * thread and outlives the rest.
	 * It is here because amd_iommu_bind_pasid wants a task_struct.
	 * It can also be used for safely getting a reference to the
	 * mm_struct of the process.
	 */
	struct task_struct *lead_thread;

	/* We want to receive a notification when the mm_struct is destroyed */
	struct mmu_notifier mmu_notifier;

	u32 pasid;

	/*
	 * Array of opu_process_device pointers,
	 * one for each device the process is using.
	 */
	struct opu_process_device *pdds[MAX_GPU_INSTANCE];
	uint32_t n_pdds;

	struct process_queue_manager pqm;

	/*Is the user space process 32 bit?*/
	bool is_32bit_user_mode;

	/* Event-related data */
	struct mutex event_mutex;
	/* Event ID allocator and lookup */
	struct idr event_idr;
	/* Event page */
	struct opu_signal_page *signal_page;
	size_t signal_mapped_size;
	size_t signal_event_count;
	bool signal_event_limit_reached;

	/* Information used for memory eviction */
	void *kgd_process_info;
	/* Eviction fence that is attached to all the BOs of this process. The
	 * fence will be triggered during eviction and new one will be created
	 * during restore
	 */
	struct dma_fence *ef;

	/* Work items for evicting and restoring BOs */
	struct delayed_work eviction_work;
	struct delayed_work restore_work;
	/* seqno of the last scheduled eviction */
	unsigned int last_eviction_seqno;
	/* Approx. the last timestamp (in jiffies) when the process was
	 * restored after an eviction
	 */
	unsigned long last_restore_timestamp;

	/* Kobj for our procfs */
	struct kobject *kobj;
	struct kobject *kobj_queues;
	struct attribute attr_pasid;

	/* shared virtual memory registered by this process */
	struct svm_range_list svms;

	bool xnack_enabled;
};

#define OPU_PROCESS_TABLE_SIZE 5 /* bits: 32 entries */
extern DECLARE_HASHTABLE(opu_processes_table, OPU_PROCESS_TABLE_SIZE);
extern struct srcu_struct opu_processes_srcu;

bool opu_dev_is_large_bar(struct opu_dev *dev);


void opu_process_destroy_wq(void);
struct opu_process *opu_create_process(struct file *filep);
struct opu_process *opu_get_process(const struct task_struct *);
struct opu_process *opu_lookup_process_by_pasid(u32 pasid);
struct opu_process *opu_lookup_process_by_mm(const struct mm_struct *mm);

int opu_process_gpuidx_from_gpuid(struct opu_process *p, uint32_t gpu_id);
int opu_process_gpuid_from_kgd(struct opu_process *p,
			       struct amdgpu_device *adev, uint32_t *gpuid,
			       uint32_t *gpuidx);
static inline int opu_process_gpuid_from_gpuidx(struct opu_process *p,
				uint32_t gpuidx, uint32_t *gpuid) {
	return gpuidx < p->n_pdds ? p->pdds[gpuidx]->dev->id : -EINVAL;
}
static inline struct opu_process_device *opu_process_device_from_gpuidx(
				struct opu_process *p, uint32_t gpuidx) {
	return gpuidx < p->n_pdds ? p->pdds[gpuidx] : NULL;
}

void opu_unref_process(struct opu_process *p);
int opu_process_evict_queues(struct opu_process *p);
int opu_process_restore_queues(struct opu_process *p);
void opu_suspend_all_processes(void);
int opu_resume_all_processes(void);

int opu_process_device_init_vm(struct opu_process_device *pdd,
			       struct file *drm_file);
struct opu_process_device *opu_bind_process_to_device(struct opu_dev *dev,
						struct opu_process *p);
struct opu_process_device *opu_get_process_device_data(struct opu_dev *dev,
							struct opu_process *p);
struct opu_process_device *opu_create_process_device_data(struct opu_dev *dev,
							struct opu_process *p);

bool opu_process_xnack_mode(struct opu_process *p, bool supported);

int opu_reserved_mem_mmap(struct opu_dev *dev, struct opu_process *process,
			  struct vm_area_struct *vma);

/* OPU process API for creating and translating handles */
int opu_process_device_create_obj_handle(struct opu_process_device *pdd,
					void *mem);
void *opu_process_device_translate_handle(struct opu_process_device *p,
					int handle);
void opu_process_device_remove_obj_handle(struct opu_process_device *pdd,
					int handle);

/* PASIDs */
int opu_pasid_init(void);
void opu_pasid_exit(void);
bool opu_set_pasid_limit(unsigned int new_limit);
unsigned int opu_get_pasid_limit(void);
u32 opu_pasid_alloc(void);
void opu_pasid_free(u32 pasid);

/* Doorbells */
size_t opu_doorbell_process_slice(struct opu_dev *opu);
int opu_doorbell_init(struct opu_dev *opu);
void opu_doorbell_fini(struct opu_dev *opu);
int opu_doorbell_mmap(struct opu_dev *dev, struct opu_process *process,
		      struct vm_area_struct *vma);
void __iomem *opu_get_kernel_doorbell(struct opu_dev *opu,
					unsigned int *doorbell_off);
void opu_release_kernel_doorbell(struct opu_dev *opu, u32 __iomem *db_addr);
u32 read_kernel_doorbell(u32 __iomem *db);
void write_kernel_doorbell(void __iomem *db, u32 value);
void write_kernel_doorbell64(void __iomem *db, u64 value);
unsigned int opu_get_doorbell_dw_offset_in_bar(struct opu_dev *opu,
					struct opu_process_device *pdd,
					unsigned int doorbell_id);
phys_addr_t opu_get_process_doorbells(struct opu_process_device *pdd);
int opu_alloc_process_doorbells(struct opu_dev *opu,
				unsigned int *doorbell_index);
void opu_free_process_doorbells(struct opu_dev *opu,
				unsigned int doorbell_index);
/* GTT Sub-Allocator */

int opu_gtt_sa_allocate(struct opu_dev *opu, unsigned int size,
			struct opu_mem_obj **mem_obj);

int opu_gtt_sa_free(struct opu_dev *opu, struct opu_mem_obj *mem_obj);

extern struct device *opu_device;

/* OPU's procfs */
void opu_procfs_init(void);
void opu_procfs_shutdown(void);
int opu_procfs_add_queue(struct queue *q);
void opu_procfs_del_queue(struct queue *q);

/* Topology */
int opu_topology_init(void);
void opu_topology_shutdown(void);
int opu_topology_add_device(struct opu_dev *gpu);
int opu_topology_remove_device(struct opu_dev *gpu);
struct opu_topology_device *opu_topology_device_by_proximity_domain(
						uint32_t proximity_domain);
struct opu_topology_device *opu_topology_device_by_id(uint32_t gpu_id);
struct opu_dev *opu_device_by_id(uint32_t gpu_id);
struct opu_dev *opu_device_by_pci_dev(const struct pci_dev *pdev);
struct opu_dev *opu_device_by_kgd(const struct kgd_dev *kgd);
int opu_topology_enum_opu_devices(uint8_t idx, struct opu_dev **kdev);
int opu_numa_node_to_apic_id(int numa_node_id);
void opu_double_confirm_iommu_support(struct opu_dev *gpu);

/* Interrupts */
int opu_interrupt_init(struct opu_dev *dev);
void opu_interrupt_exit(struct opu_dev *dev);
bool enqueue_ih_ring_entry(struct opu_dev *opu,	const void *ih_ring_entry);
bool interrupt_is_wanted(struct opu_dev *dev,
				const uint32_t *ih_ring_entry,
				uint32_t *patched_ihre, bool *flag);

/* opu Apertures */
int opu_init_apertures(struct opu_process *process);

void opu_process_set_trap_handler(struct qcm_process_device *qpd,
				  uint64_t tba_addr,
				  uint64_t tma_addr);

/* Queue Context Management */
int init_queue(struct queue **q, const struct queue_properties *properties);
void uninit_queue(struct queue *q);
void print_queue_properties(struct queue_properties *q);
void print_queue(struct queue *q);

/*
struct mqd_manager *mqd_manager_init_cik(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
struct mqd_manager *mqd_manager_init_cik_hawaii(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
struct mqd_manager *mqd_manager_init_vi(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
struct mqd_manager *mqd_manager_init_vi_tonga(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
struct mqd_manager *mqd_manager_init_v9(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
struct mqd_manager *mqd_manager_init_v10(enum OPU_MQD_TYPE type,
		struct opu_dev *dev);
*/

struct device_queue_manager *device_queue_manager_init(struct opu_dev *dev);
void device_queue_manager_uninit(struct device_queue_manager *dqm);
struct kernel_queue *kernel_queue_init(struct opu_dev *dev,
					enum opu_queue_type type);
void kernel_queue_uninit(struct kernel_queue *kq, bool hanging);
int opu_process_vm_fault(struct device_queue_manager *dqm, u32 pasid);

/* Process Queue Manager */
struct process_queue_node {
	struct queue *q;
	struct kernel_queue *kq;
	struct list_head process_queue_list;
};

void opu_process_dequeue_from_device(struct opu_process_device *pdd);
void opu_process_dequeue_from_all_devices(struct opu_process *p);
int pqm_init(struct process_queue_manager *pqm, struct opu_process *p);
void pqm_uninit(struct process_queue_manager *pqm);
int pqm_create_queue(struct process_queue_manager *pqm,
			    struct opu_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int *qid,
			    uint32_t *p_doorbell_offset_in_process);
int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid);
int pqm_update_queue(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
int pqm_set_cu_mask(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
int pqm_set_gws(struct process_queue_manager *pqm, unsigned int qid,
			void *gws);
struct kernel_queue *pqm_get_kernel_queue(struct process_queue_manager *pqm,
						unsigned int qid);
struct queue *pqm_get_user_queue(struct process_queue_manager *pqm,
						unsigned int qid);
int pqm_get_wave_state(struct process_queue_manager *pqm,
		       unsigned int qid,
		       void __user *ctl_stack,
		       u32 *ctl_stack_used_size,
		       u32 *save_area_used_size);

int opu_fence_wait_timeout(uint64_t *fence_addr,
			      uint64_t fence_value,
			      unsigned int timeout_ms);

/* Packet Manager */

#define OPU_FENCE_COMPLETED (100)
#define OPU_FENCE_INIT   (10)

struct packet_manager {
	struct device_queue_manager *dqm;
	struct kernel_queue *priv_queue;
	struct mutex lock;
	bool allocated;
	struct opu_mem_obj *ib_buffer_obj;
	unsigned int ib_size_bytes;
	bool is_over_subscription;

	const struct packet_manager_funcs *pmf;
};

struct packet_manager_funcs {
	/* Support ASIC-specific packet formats for PM4 packets */
	int (*map_process)(struct packet_manager *pm, uint32_t *buffer,
			struct qcm_process_device *qpd);
	int (*runlist)(struct packet_manager *pm, uint32_t *buffer,
			uint64_t ib, size_t ib_size_in_dwords, bool chain);
	int (*set_resources)(struct packet_manager *pm, uint32_t *buffer,
			struct scheduling_resources *res);
	int (*map_queues)(struct packet_manager *pm, uint32_t *buffer,
			struct queue *q, bool is_static);
	int (*unmap_queues)(struct packet_manager *pm, uint32_t *buffer,
			enum opu_queue_type type,
			enum opu_unmap_queues_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);
	int (*query_status)(struct packet_manager *pm, uint32_t *buffer,
			uint64_t fence_address,	uint64_t fence_value);
	int (*release_mem)(uint64_t gpu_addr, uint32_t *buffer);

	/* Packet sizes */
	int map_process_size;
	int runlist_size;
	int set_resources_size;
	int map_queues_size;
	int unmap_queues_size;
	int query_status_size;
	int release_mem_size;
};

extern const struct packet_manager_funcs opu_vi_pm_funcs;
extern const struct packet_manager_funcs opu_v9_pm_funcs;
extern const struct packet_manager_funcs opu_aldebaran_pm_funcs;

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm);
void pm_uninit(struct packet_manager *pm, bool hanging);
int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res);
int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues);
int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
				uint64_t fence_value);

int pm_send_unmap_queue(struct packet_manager *pm, enum opu_queue_type type,
			enum opu_unmap_queues_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);

void pm_release_ib(struct packet_manager *pm);

/* Following PM funcs can be shared among VI and AI */
unsigned int pm_build_pm4_header(unsigned int opcode, size_t packet_size);

uint64_t opu_get_number_elems(struct opu_dev *opu);

/* Events */
extern const struct opu_event_interrupt_class event_interrupt_class_cik;
extern const struct opu_event_interrupt_class event_interrupt_class_v9;

extern const struct opu_device_global_init_class device_global_init_class_cik;

void opu_event_init_process(struct opu_process *p);
void opu_event_free_process(struct opu_process *p);
int opu_event_mmap(struct opu_process *process, struct vm_area_struct *vma);
int opu_wait_on_events(struct opu_process *p,
		       uint32_t num_events, void __user *data,
		       bool all, uint32_t user_timeout_ms,
		       uint32_t *wait_result);
void opu_signal_event_interrupt(u32 pasid, uint32_t partial_id,
				uint32_t valid_id_bits);
void opu_signal_iommu_event(struct opu_dev *dev,
			    u32 pasid, unsigned long address,
			    bool is_write_requested, bool is_execute_requested);
void opu_signal_hw_exception_event(u32 pasid);
int opu_set_event(struct opu_process *p, uint32_t event_id);
int opu_reset_event(struct opu_process *p, uint32_t event_id);
int opu_event_page_set(struct opu_process *p, void *kernel_address,
		       uint64_t size);
int opu_event_create(struct file *devopu, struct opu_process *p,
		     uint32_t event_type, bool auto_reset, uint32_t node_id,
		     uint32_t *event_id, uint32_t *event_trigger_data,
		     uint64_t *event_page_offset, uint32_t *event_slot_index);
int opu_event_destroy(struct opu_process *p, uint32_t event_id);

void opu_signal_vm_fault_event(struct opu_dev *dev, u32 pasid,
				struct opu_vm_fault_info *info);

void opu_signal_reset_event(struct opu_dev *dev);

void opu_signal_poison_consumed_event(struct opu_dev *dev, u32 pasid);

void opu_flush_tlb(struct opu_process_device *pdd, enum TLB_FLUSH_TYPE type);

int dbgdev_wave_reset_wavefronts(struct opu_dev *dev, struct opu_process *p);

bool opu_is_locked(void);

/* Compute profile */
void opu_inc_compute_active(struct opu_dev *dev);
void opu_dec_compute_active(struct opu_dev *dev);

/* Cgroup Support */
/* Check with device cgroup if @opu device is accessible */
static inline int opu_devcgroup_check_permission(struct opu_dev *opu)
{
#if defined(CONFIG_CGROUP_DEVICE) || defined(CONFIG_CGROUP_BPF)
	struct drm_device *ddev = opu->ddev;

	return devcgroup_check_permission(DEVCG_DEV_CHAR, DRM_MAJOR,
					  ddev->render->index,
					  DEVCG_ACC_WRITE | DEVCG_ACC_READ);
#else
	return 0;
#endif
}

/* Debugfs */
#if defined(CONFIG_DEBUG_FS)

void opu_debugfs_init(void);
void opu_debugfs_fini(void);
int opu_debugfs_mqds_by_process(struct seq_file *m, void *data);
int pqm_debugfs_mqds(struct seq_file *m, void *data);
int opu_debugfs_hqds_by_device(struct seq_file *m, void *data);
int dqm_debugfs_hqds(struct seq_file *m, void *data);
int opu_debugfs_rls_by_device(struct seq_file *m, void *data);
int pm_debugfs_runlist(struct seq_file *m, void *data);

int opu_debugfs_hang_hws(struct opu_dev *dev);
int pm_debugfs_hang_hws(struct packet_manager *pm);
int dqm_debugfs_execute_queues(struct device_queue_manager *dqm);

#else

static inline void opu_debugfs_init(void) {}
static inline void opu_debugfs_fini(void) {}

#endif

#endif
