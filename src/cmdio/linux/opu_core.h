#ifndef CORE_H_INCLUDED
#define CORE_H_INCLUDED

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
// #include <kgd_core_interface.h>
// #include <drm/ttm/ttm_execbuf_util.h>
// #include "amdgpu_sync.h"
// #include "amdgpu_vm.h"


extern const struct opu_ip_funcs core_v1_0_funcs;

/* Enable eviction debug messages */
extern bool debug_evictions;

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

struct core_event_interrupt_class {
	bool (*interrupt_isr)(struct core_dev *dev,
			const uint32_t *ih_ring_entry, uint32_t *patched_ihre,
			bool *patched_flag);
	void (*interrupt_wq)(struct core_dev *dev,
			const uint32_t *ih_ring_entry);
};

struct opu_core_device_info {
	enum amd_asic_type asic_family;
	const char *asic_name;
	const struct core_event_interrupt_class *event_interrupt_class;
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

struct core_mem_obj {
	uint32_t range_start;
	uint32_t range_end;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
	void *gtt_mem;
};

struct core_vmid_info {
	uint32_t first_vmid_kfd;
	uint32_t last_vmid_kfd;
	uint32_t vmid_num_kfd;
};

/**
 * struct opu_core_funcs
 *
 * @program_sh_mem_settings: A function that should initiate the memory
 * properties such as main aperture memory type (cache / non cached) and
 * secondary aperture base address, size and memory type.
 * This function is used only for no cp scheduling mode.
 *
 * @set_pasid_vmid_mapping: Exposes pasid/vmid pair to the H/W for no cp
 * scheduling mode. Only used for no cp scheduling mode.
 *
 * @hqd_load: Loads the mqd structure to a H/W hqd slot. used only for no cp
 * sceduling mode.
 *
 * @hqd_sdma_load: Loads the SDMA mqd structure to a H/W SDMA hqd slot.
 * used only for no HWS mode.
 *
 * @hqd_dump: Dumps CPC HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_sdma_dump: Dumps SDMA HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_is_occupies: Checks if a hqd slot is occupied.
 *
 * @hqd_destroy: Destructs and preempts the queue assigned to that hqd slot.
 *
 * @hqd_sdma_is_occupied: Checks if an SDMA hqd slot is occupied.
 *
 * @hqd_sdma_destroy: Destructs and preempts the SDMA queue assigned to that
 * SDMA hqd slot.
 *
 * @set_scratch_backing_va: Sets VA for scratch backing memory of a VMID.
 * Only used for no cp scheduling mode
 *
 * @set_vm_context_page_table_base: Program page table base for a VMID
 *
 * @invalidate_tlbs: Invalidate TLBs for a specific PASID
 *
 * @invalidate_tlbs_vmid: Invalidate TLBs for a specific VMID
 *
 * @read_vmid_from_vmfault_reg: On Hawaii the VMID is not set in the
 * IH ring entry. This function allows the KFD ISR to get the VMID
 * from the fault status register as early as possible.
 *
 * @get_cu_occupancy: Function pointer that returns to caller the number
 * of wave fronts that are in flight for all of the queues of a process
 * as identified by its pasid. It is important to note that the value
 * returned by this function is a snapshot of current moment and cannot
 * guarantee any minimum for the number of waves in-flight. This function
 * is defined for devices that belong to GFX9 and later GFX families. Care
 * must be taken in calling this function as it is not defined for devices
 * that belong to GFX8 and below GFX families.
 *
 * This structure contains function pointers to services that the kgd driver
 * provides to amdkfd driver.
 *
 */
struct opu_core_funcs {
	/* Register access functions */
	void (*program_sh_mem_settings)(struct opu_device *kgd, uint32_t vmid,
			uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
			uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

	int (*set_pasid_vmid_mapping)(struct opu_device *kgd, u32 pasid,
					unsigned int vmid);

	int (*init_interrupts)(struct opu_device *kgd, uint32_t pipe_id);

	int (*hqd_load)(struct opu_device *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm);

	int (*hiq_mqd_load)(struct opu_device *kgd, void *mqd,
			    uint32_t pipe_id, uint32_t queue_id,
			    uint32_t doorbell_off);

	int (*hqd_sdma_load)(struct opu_device *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm);

	int (*hqd_dump)(struct opu_device *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);

	int (*hqd_sdma_dump)(struct opu_device *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs);

	bool (*hqd_is_occupied)(struct opu_device *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

	int (*hqd_destroy)(struct opu_device *kgd, void *mqd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);

	bool (*hqd_sdma_is_occupied)(struct opu_device *kgd, void *mqd);

	int (*hqd_sdma_destroy)(struct opu_device *kgd, void *mqd,
				unsigned int timeout);

	int (*address_watch_disable)(struct opu_device *kgd);
	int (*address_watch_execute)(struct opu_device *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
	int (*wave_control_execute)(struct opu_device *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
	uint32_t (*address_watch_get_offset)(struct opu_device *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);
	bool (*get_atc_vmid_pasid_mapping_info)(
					struct opu_device *kgd,
					uint8_t vmid,
					uint16_t *p_pasid);

	/* No longer needed from GFXv9 onward. The scratch base address is
	 * passed to the shader by the CP. It's the user mode driver's
	 * responsibility.
	 */
	void (*set_scratch_backing_va)(struct opu_device *kgd,
				uint64_t va, uint32_t vmid);

	void (*set_vm_context_page_table_base)(struct opu_device *kgd,
			uint32_t vmid, uint64_t page_table_base);
	uint32_t (*read_vmid_from_vmfault_reg)(struct opu_device *kgd);

	void (*get_cu_occupancy)(struct opu_device *kgd, int pasid, int *wave_cnt,
			int *max_waves_per_cu);
};


extern uint64_t core_total_mem_size;

enum TLB_FLUSH_TYPE {
	TLB_FLUSH_LEGACY = 0,
	TLB_FLUSH_LIGHTWEIGHT,
	TLB_FLUSH_HEAVYWEIGHT
};

struct opu_device;

enum core_mem_attachment_type {
	core_MEM_ATT_SHARED,	/* Share kgd_mem->bo or another attachment's */
	core_MEM_ATT_USERPTR,	/* SG bo to DMA map pages from a userptr bo */
	core_MEM_ATT_DMABUF,	/* DMAbuf to DMA map TTM BOs */
};

struct core_mem_attachment {
	struct list_head list;
	enum core_mem_attachment_type type;
	bool is_mapped;
	struct amdgpu_bo_va *bo_va;
	struct opu_device *odev;
	uint64_t va;
	uint64_t pte_flags;
};

struct kgd_mem {
	struct mutex lock;
	struct amdgpu_bo *bo;
	struct dma_buf *dmabuf;
	struct list_head attachments;
	/* protected by core_process_info.lock */
	struct ttm_validate_buffer validate_list;
	struct ttm_validate_buffer resv_list;
	uint32_t domain;
	unsigned int mapped_to_gpu_memory;
	uint64_t va;

	uint32_t alloc_flags;

	atomic_t invalid;
	struct core_process_info *process_info;

	struct amdgpu_sync sync;

	bool aql_queue;
	bool is_imported;
};

/* KFD Memory Eviction */
struct core_fence {
	struct dma_fence base;
	struct mm_struct *mm;
	spinlock_t lock;
	char timeline_name[TASK_COMM_LEN];
	struct svm_range_bo *svm_bo;
};

struct opu_core_dev {
	struct opu_device *odev;
	uint64_t vram_used;
	bool init_complete;
};

enum kgd_engine_type {
	KGD_ENGINE_PFP = 1,
	KGD_ENGINE_ME,
	KGD_ENGINE_CE,
	KGD_ENGINE_MEC1,
	KGD_ENGINE_MEC2,
	KGD_ENGINE_RLC,
	KGD_ENGINE_SDMA1,
	KGD_ENGINE_SDMA2,
	KGD_ENGINE_MAX
};


struct core_process_info {
	/* List head of all VMs that belong to a KFD process */
	struct list_head vm_list_head;
	/* List head for all KFD BOs that belong to a KFD process. */
	struct list_head core_bo_list;
	/* List of userptr BOs that are valid or invalid */
	struct list_head userptr_valid_list;
	struct list_head userptr_inval_list;
	/* Lock to protect core_bo_list */
	struct mutex lock;

	/* Number of VMs */
	unsigned int n_vms;
	/* Eviction Fence */
	struct core_fence *eviction_fence;

	/* MMU-notifier related fields */
	atomic_t evicted_bos;
	struct delayed_work restore_userptr_work;
	struct pid *pid;
};

int core_init(void);
void core_fini(void);

void core_suspend(struct opu_device *odev, bool run_pm);
int core_resume(struct opu_device *odev, bool run_pm);
void core_interrupt(struct opu_device *odev,
			const void *ih_ring_entry);
void core_device_probe(struct opu_device *odev);
void core_device_init(struct opu_device *odev);
void core_device_fini_sw(struct opu_device *odev);
int core_submit_ib(struct opu_device *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len);
void core_set_compute_idle(struct opu_device *kgd, bool idle);
bool core_have_atomics_support(struct opu_device *kgd);
int core_flush_gpu_tlb_vmid(struct opu_device *kgd, uint16_t vmid);
int core_flush_gpu_tlb_pasid(struct opu_device *kgd, uint16_t pasid,
				      enum TLB_FLUSH_TYPE flush_type);

bool core_is_core_vmid(struct opu_device *odev, u32 vmid);

// int core_pre_reset(struct opu_device *odev);

// int core_post_reset(struct opu_device *odev);

// void core_gpu_reset(struct opu_device *kgd);

int amdgpu_queue_mask_bit_to_set_resource_bit(struct opu_device *odev,
					int queue_bit);

struct core_fence *core_fence_create(u64 context,
				struct mm_struct *mm,
				struct svm_range_bo *svm_bo);
bool core_fence_check_mm(struct dma_fence *f, struct mm_struct *mm);
struct core_fence *to_core_fence(struct dma_fence *f);
int core_remove_fence_on_pt_pd_bos(struct amdgpu_bo *bo);
int core_evict_userptr(struct kgd_mem *mem, struct mm_struct *mm);

/* Shared API */
int core_alloc_gtt_mem(struct opu_device *kgd, size_t size,
				void **mem_obj, uint64_t *gpu_addr,
				void **cpu_ptr, bool mqd_gfx9);
void core_free_gtt_mem(struct opu_device *kgd, void *mem_obj);
// int core_alloc_gws(struct opu_device *kgd, size_t size, void **mem_obj);
// void core_free_gws(struct opu_device *kgd, void *mem_obj);
// int core_add_gws_to_process(void *info, void *gws, struct kgd_mem **mem);
/// int core_remove_gws_from_process(void *info, void *mem);
// uint32_t core_get_fw_version(struct opu_device *kgd,
//				      enum kgd_engine_type type);
void core_get_local_mem_info(struct opu_device *kgd,
				      struct core_local_mem_info *mem_info);
uint64_t core_get_gpu_clock_counter(struct opu_device *kgd);

// uint32_t core_get_max_engine_clock_in_mhz(struct opu_device *kgd);
// void core_get_cu_info(struct opu_device *kgd, struct core_cu_info *cu_info);
int core_get_dmabuf_info(struct opu_device *kgd, int dma_buf_fd,
				  struct opu_device **dmabuf_kgd,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags);
// uint64_t core_get_vram_usage(struct opu_device *kgd);
// uint64_t core_get_hive_id(struct opu_device *kgd);
// uint64_t core_get_unique_id(struct opu_device *kgd);
// uint64_t core_get_mmio_remap_phys_addr(struct opu_device *kgd);
// uint32_t core_get_num_gws(struct opu_device *kgd);
uint32_t core_get_asic_rev_id(struct opu_device *kgd);
int core_get_noretry(struct opu_device *kgd);
// uint8_t core_get_xgmi_hops_count(struct opu_device *dst, struct opu_device *src);

/* Read user wptr from a specified user address space with page fault
 * disabled. The memory must be pinned and mapped to the hardware when
 * this is called in hqd_load functions, so it should never fault in
 * the first place. This resolves a circular lock dependency involving
 * four locks, including the DQM lock and mmap_lock.
 */
#define read_user_wptr(mmptr, wptr, dst)				\
	({								\
		bool valid = false;					\
		if ((mmptr) && (wptr)) {				\
			pagefault_disable();				\
			if ((mmptr) == current->mm) {			\
				valid = !get_user((dst), (wptr));	\
			} else if (current->flags & PF_KTHREAD) {	\
				kthread_use_mm(mmptr);			\
				valid = !get_user((dst), (wptr));	\
				kthread_unuse_mm(mmptr);		\
			}						\
			pagefault_enable();				\
		}							\
		valid;							\
	})

/* GPUVM API */
#define drm_priv_to_vm(drm_priv)					\
	(&((struct amdgpu_fpriv *)					\
		((struct drm_file *)(drm_priv))->driver_priv)->vm)

int core_gpuvm_acquire_process_vm(struct opu_device *kgd,
					struct file *filp, u32 pasid,
					void **process_info,
					struct dma_fence **ef);
void core_gpuvm_release_process_vm(struct opu_device *kgd, void *drm_priv);
uint64_t core_gpuvm_get_process_page_dir(void *drm_priv);
int core_gpuvm_alloc_memory_of_gpu(
		struct opu_device *kgd, uint64_t va, uint64_t size,
		void *drm_priv, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags);
int core_gpuvm_free_memory_of_gpu(
		struct opu_device *kgd, struct kgd_mem *mem, void *drm_priv,
		uint64_t *size);
int core_gpuvm_map_memory_to_gpu(
		struct opu_device *kgd, struct kgd_mem *mem, void *drm_priv, bool *table_freed);
int core_gpuvm_unmap_memory_from_gpu(
		struct opu_device *kgd, struct kgd_mem *mem, void *drm_priv);
int core_gpuvm_sync_memory(
		struct opu_device *kgd, struct kgd_mem *mem, bool intr);
int core_gpuvm_map_gtt_bo_to_kernel(struct opu_device *kgd,
		struct kgd_mem *mem, void **kptr, uint64_t *size);
int core_gpuvm_restore_process_bos(void *process_info,
					    struct dma_fence **ef);
int core_gpuvm_get_vm_fault_info(struct opu_device *kgd,
					      struct core_vm_fault_info *info);
int core_gpuvm_import_dmabuf(struct opu_device *kgd,
				      struct dma_buf *dmabuf,
				      uint64_t va, void *drm_priv,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset);
int core_get_tile_config(struct opu_device *kgd,
				struct tile_config *config);

void core_gpuvm_init_mem_limits(void);
void core_gpuvm_destroy_cb(struct opu_device *odev,
				struct amdgpu_vm *vm);
void core_unreserve_memory_limit(struct amdgpu_bo *bo);
void core_reserve_system_mem(uint64_t size);

#endif /* CORE_H_INCLUDED */
