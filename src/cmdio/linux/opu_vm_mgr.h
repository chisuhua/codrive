#ifndef __OPU_VM_MGR_H__
#define __OPU_VM_MGR_H__

#include <linux/idr.h>
#include <linux/kfifo.h>
#include <linux/rbtree.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_file.h>
#include <drm/ttm/ttm_bo_driver.h>

#include "opu_deps.h"

#define INVALID_ADDR (0xFFFFFFFFFFFFFFFFull)

/* Maximum number of PTEs the hardware can write with one command */
#define OPU_VM_MAX_UPDATE_SIZE	0x3FFFF

/* number of entries in page table */
#define OPU_VM_PTE_COUNT(odev) (1 << (odev)->vm_mgr.frame_size[OPU_VM_PTE])

#define OPU_PTE_VALID	(1ULL << 0)
#define OPU_PTE_SYSTEM	(1ULL << 1)
#define OPU_PTE_SNOOPED	(1ULL << 2)


#define OPU_PTE_EXECUTABLE	(1ULL << 4)
#define OPU_PTE_READABLE	(1ULL << 5)
#define OPU_PTE_WRITEABLE	(1ULL << 6)

#define OPU_PTE_FRAG(x)	((x & 0x1fULL) << 7)
#define OPU_PTE_TRANSFURTHER	(1ULL << 11)
#define OPU_PTE_RESIDENT		(1ULL << 52)
#define OPU_PDE_AS_PTE		(1ULL << 53)
#define OPU_PTE_PAGE_SIZE	(1ULL << 54)

#define OPU_PT_FRAME_LEVEL(l)    ((((uint64_t)(l)) & 0x7ULL) << 55)
#define OPU_PT_OFFSET(o)         ((((uint64_t)(o)) & 0x1fULL) << 58)

/* offset is 128 bytes aligned */
#define OPU_PT_OFFSET_ALIGN_BITS    (7)
#define OPU_PT_BYTE_OFFSET2FLAGS(o) OPU_PT_OFFSET(((o) >> OPU_PT_OFFSET_ALIGN_BITS))
#define OPU_VM_MEM_ALIGN            (1u << OPU_PT_OFFSET_ALIGN_BITS)


/* How to program VM fault handling */
#define OPU_VM_FAULT_STOP_NEVER	0
#define OPU_VM_FAULT_STOP_FIRST	1
#define OPU_VM_FAULT_STOP_ALWAYS	2

/* max vmids dedicated for process */
#define OPU_VM_MAX_RESERVED_VMID	1

/* See vm_update_mode */
#define OPU_VM_USE_CPU_FOR_GFX (1 << 0)
#define OPU_VM_USE_CPU_FOR_COMPUTE (1 << 1)

/* VMPT level enumerate, and the hiberachy is:
 * PDB2->PDB1->PDB0->PTB
 */
enum opu_vm_level {
	OPU_VM_PD0,
	OPU_VM_PD1,
	OPU_VM_PDE,
	OPU_VM_PTE,
	OPU_VM_SPE,
	OPU_VM_MAX_LEVEL
};

#define OPU_VIRTUAL_ADDR_SIZE 49
#define OPU_PAGE4K_SHIFT        12
#define OPU_PAGE4K_SIZE         (1 << OPU_PAGE4K_SHIFT)
#define OPU_PAGE4K_MASK         (OPU_PAGE4K_SIZE - 1 )
#define OPU_PAGE4K_ALIGN(a)     (((a) + OPU_PAGE4K_MASK) & ~OPU_PAGE4K_MASK)

#define OPU_PAGE64K_SHIFT        16
#define OPU_PAGE64K_SIZE         (1 << OPU_PAGE64K_SHIFT)
#define OPU_PAGE64K_MASK         (OPU_PAGE64K_SIZE - 1 )
#define OPU_PAGE64K_ALIGN(a)     (((a) + OPU_PAGE64K_MASK) & ~OPU_PAGE64K_MASK)

#define MIN_FRAGMENT_SIZE       3

struct opu_vm_pt {
    struct opu_vm       *vm;
    struct opu_mo       *mo;

    /* if mo is shared with sibling entries */
    bool                is_shared_mo;
    uint16_t            mo_offset;
    uint32_t            num_entries;
    struct opu_mo       *shared_mo;
    struct opu_vm_pt    *parent;

    /* array of page table, one for each directory entry */
    struct opu_vm_pt    *entries;
};

/* hw that can write ptes , e.g. dma */
struct opu_vm_pte_funcs {
    unsigned        copy_pde_num_dw;    /* number of dw to reserve per operation */

    /* copy pte entries for GART */
    void (*copy_pte)(struct opu_ib *ib, uint64_t pe, uint64_t src, unsigned count);

	/* write pte one entry at a time with addr mapping */
	void (*write_pte)(struct opu_ib *ib, uint64_t pe,
			  uint64_t value, unsigned count,
			  uint32_t incr);

    /* for linear pte/pde updates without addr mapping */
    void (*set_pte_pde)(struct opu_ib *ib, uint64_t pe,
                    uint64_t addr,  unsigned count,
                    uint32_t incr,  unsigned flags);
}

struct opu_task_info {
	char	process_name[TASK_COMM_LEN];
	char	task_name[TASK_COMM_LEN];
	pid_t	pid;
	pid_t	tgid;
};

/**
 * struct opu_vm_update_params
 *
 * Encapsulate some VM table update parameters to reduce
 * the number of function parameters
 *
 */
struct opu_vm_update_params {
	/* @odev: opu device we do this update for */
	struct opu_device *odev;

	/*@vm: optional opu_vm we do this update for */
	struct opu_vm *vm;

	/*@immediate: if changes should be made immediately */
	bool immediate;

	/*@unlocked: true if the root BO is not locked */
	bool unlocked;

	/**
	 * @pages_addr: DMA addresses to use for mapping
	 */
	dma_addr_t *pages_addr;

    /* @page_order: 0 4k, or 4 (64K) */
    uint32_t page_order;

	/**
	 * @job: job to used for hw submission
	 */
	struct opu_job *job;

	/**
	 * @num_dw_left: number of dw left for the IB
	 */
	unsigned int num_dw_left;

	/**
	 * @table_freed: return true if page table is freed when updating
	 */
	bool table_freed;
};

struct opu_vm_update_funcs {
	int (*map_table)(struct opu_bo_vm *bo);
	int (*prepare)(struct opu_vm_update_params *p, struct dma_resv *resv,
		       enum opu_sync_mode sync_mode);
	int (*update)(struct opu_vm_update_params *p,
		      struct opu_bo_vm *bo, uint64_t pe, uint64_t addr,
		      unsigned count, uint32_t incr, uint64_t flags, bool is_leaf);
	int (*commit)(struct opu_vm_update_params *p,
		      struct dma_fence **fence);
};

struct opu_hw_sem {
    uint32_t            dw_pool_offset;
    volatile uint64_t   *cpu_addr;
    uint64_t            addr;
    uint64_t            emitted_seq;
};

struct opu_vm_inv_tlb_sync_info {
    struct opu_hw_sem       hw_sem;
    uint64_t                seqno;
    uint64_t                last_wait_seqno[OPU_BIGGEST_RING_NUM];
    int                     freed_count; /* items in freed list*/
};

struct opu_vm {
	/* tree of virtual addresses mapped */
	struct rb_root_cached	va;

	/* Lock to prevent eviction while we are updating page tables
	 * use vm_eviction_lock/unlock(vm)
	 */
	struct mutex		eviction_lock;
	bool			    evicting;
	unsigned int		saved_flags;

	/* BOs who needs a validation */
	struct list_head	evicted;

	/* PT BOs which relocated and their parent need an update */
	struct list_head	relocated;

	/* per VM BOs moved, but not yet updated in the PT */
	struct list_head	moved;

	/* All BOs of this VM not currently in the state machine */
	struct list_head	idle;

	/* regular invalidated BOs, but not yet updated in the PT */
	struct list_head	invalidated;
	spinlock_t		invalidated_lock;

	/* BO mappings freed, but not yet updated in the PT */
	struct list_head	freed;

	/* BOs which are invalidated, has been updated in the PTs */
	struct list_head        done;

    /*lock of vm update */
    struct mutex        lock;

	/* contains the page directory */
	struct opu_vm_pt root;
	struct dma_fence	*last_update;

    /* lock for the page directory */
    struct mutex        job_deps_lock;
    atomic_t            num_queues;
    struct opu_deps     job_deps; /*record fences of all user jobs*/
    uint64_t            fence_context;
    uint32_t            seqno;

	/* Scheduler entities for page table updates */
	struct drm_sched_entity	paging_entity;

	unsigned int		pasid;
	/* dedicated to vm */
	struct opu_vmid	*reserved_vmid[OPU_MAX_VMHUBS];

	/* Flag to indicate if VM tables are updated by CPU or GPU (SDMA) */
	bool					use_cpu_for_update;

	/* Functions to use for VM table updates */
	const struct opu_vm_update_funcs	*update_funcs;

	/* Points to the KFD process VM info */
	struct amdkfd_process_info *process_info;

	/* Some basic info about the task */
	struct opu_task_info task_info;

    struct list_head    pst_head;
    struct mutex        pst_lock;

    /* freed bo_va which need to INV TLB */
    struct list_head        freed;
    struct opu_vm_inv_tlb_sync_info     inv_tlb_sync_info[OPU_ENGINE_NUM];
    struct mutex            freed_lock; /* protect freed */
    int                     freed_count; /*items in freed list*/
    uint32_t                full_eng_mask;
    bool                    compress_enable;
    bool                    compress_range_base;
    bool                    compress_range_size;
    pid_t                   pid;
};

struct opu_vm_mgr {
	/* Handling of VMIDs */
	struct opu_vmid_mgr		id_mgr[OPU_MAX_VMHUBS];
	unsigned int				first_kfd_vmid;
	bool					concurrent_flush;

	/* Handling of VM fences */
	uint64_t			    fence_context;
	unsigned				seqno

	uint64_t				max_pfn;
	uint32_t				num_level;

	uint32_t				frame_size[OPU_VM_MAX_LEVEL];
	uint32_t				fragment_size;
	enum opu_vm_level	root_level;
	/* vram base address for page table entry  */
	u64					    vram_base_offset;
	/* vm pte handling, only one dedicated paging queue */
	const struct opu_vm_pte_funcs	*vm_pte_funcs;
	struct drm_gpu_scheduler		    *vm_pte_scheds;
	uint32_t				vm_pte_num_scheds;
    uint32_t                va_align;
    uint32_t                huge_page_size;


	/* partial resident texture handling */
	spinlock_t				prt_lock;
	atomic_t				num_prt_users;

    union {
        struct {
            uint32_t        support_vm_pt : 1; /* support per vm page table */
            uint32_t        use_64k_page  : 1; /* 64kpage as default */
            uint32_t        vmid0_only    : 1; /* umd work on wmid0 */
            uint32_t        reserved      : 29;
        }
    } vm_caps;

	/* PASID to VM mapping, will be used in interrupt context to
	 * look up VM of a page fault
	 */
	struct idr				pasid_idr;
	spinlock_t				pasid_lock;
};

#define opu_vm_copy_pte(odev, ib, pe, src, count) \
    ((odev)->vm_mgr.vm_pte_funcs->copy_pte((ib), (pe), (src), (count)))

#define opu_vm_set_pte_pde(odev, ib, pe, addr, count, incr, flags) \
    ((odev)->vm_mgr.vm_pte_funcs->set_pte_pde((ib), (pe), (addr), (count), (incr), (flags)))

#define opu_vm_invalidate_tlb(odev, ib, vmid, start_addr, end_addr) \
    ((odev)->vm_mgr.vm_pte_funcs->invalidate_tlb((ib), (vmid), (start_addr), (end_addr)))

#define opu_vm_emit_fence(odev, ib, addr, seq, flags) \
    ((odev)->vm_mgr.vm_pte_funcs->emit_fence((ib), (addr), (seq), (flags)))

#define opu_vm_mem_sync(odev, ib, addr, size, inv_llc, clean_llc) \
    ((odev)->vm_mgr.vm_pte_funcs->mem_sync((ib), (addr), (size), (inv_llc), (clean_llc)))

// #define opu_vm_write_pte(odev, ib, pe, value, count, incr) \
// ((odev)->vm_manager.vm_pte_funcs->write_pte((ib), (pe), (value), (count), (incr)))
extern const struct opu_vm_update_funcs opu_vm_cpu_funcs;
extern const struct opu_vm_update_funcs opu_vm_sdma_funcs;

void opu_vm_dump(struct opu_device *odev, uint32_t vmid);
void opu_vm_page_walk(struct opu_device *odev, uint32_t vmid,
                uint64_t vmid_pa, uint32_t va);

void opu_vm_manager_init(struct opu_device *odev);
void opu_vm_manager_fini(struct opu_device *odev);

long opu_vm_wait_idle(struct opu_vm *vm, long timeout);
int opu_vm_init(struct opu_device *odev, struct opu_vm *vm, u32 pasid);
void opu_vm_fini(struct opu_device *odev, struct opu_vm *vm);
int opu_vm_flush(struct opu_ring_mgr *ring, struct opu_job *job, bool need_pipe_sync);
int opu_vm_flush_vmid_switch(struct opu_ring_mgr *ring, struct opu_job *job,
                bool opu_vmid *id);
uint64_t opu_vm_get_art_pa(const dma_addr_t *pages_addr, uint32_t pages_order, uint64_t addr);

int opu_vm_bo_update(struct opu_device *odev, struct opu_bo_va *bo_va,
		        struct dma_fence *exclusive, bool clear);
int opu_vm_bo_map(struct opu_device *odev,
		        struct opu_bo_va *bo_va,
		        uint64_t addr, uint64_t offset,
		        uint64_t size, uint64_t flags);
int opu_vm_bo_unmap(struct opu_device *odev, struct opu_bo_va *bo_va);
struct opu_bo_va_mapping *opu_vm_bo_lookup_mapping(struct opu_vm *vm,
							 uint64_t addr);


void opu_vm_add_freed_mapping(struct opu_vm *vm, struct opu_bo_va *bo_va);
bool opu_vm_need_pipeline_sync(struct opu_ring_mgr *ring, struct opu_job *job);

int opu_vm_bo_check_align(struct opu_device *odev, struct opu_bo *obo,
                uint32_t align, uint64_t size);

int opu_update_dev_page_table(struct opu_bo *obo, uint64_t va,
                struct opu_mo *mo, bool clear);

int opu_vm_remove_pst_buffers(struct opu_device *odev, struct opu_vm *vm, bool clear);
int opu_vm_readd_pst_buffers(struct opu_device *odev, struct opu_vm *vm);
int opu_vm_enable_cache_compress(struct opu_device *odev, struct opu_vm *vm,
                uint64_t addr, uint64_t size, bool re_enable);


#endif
