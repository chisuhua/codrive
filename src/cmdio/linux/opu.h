#ifndef __OPU_H__
#define __OPU_H__

#include "../../smi/device_factory.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "opu: " fmt

#ifdef dev_fmt
#undef dev_fmt
#endif

#define IRQ_DEBUG

#define dev_fmt(fmt) "dev: " fmt

enum opu_bus_type {
    OPU_BUS_PCIE = 0,
    OPU_BUS_GPIO_I2C,
    OPU_BUS_MAX,
};

enum opu_ip_type {
    OPU_CORE,
    OPU_DMA,
    OPU_IRQ,
    OPU_MC,
    OPU_OCN,
    OPU_IP_MAX,
};

enum opu_dma_direction {
    OPU_H2D = 0,
    OPU_D2H,
    OPU_D2D,
    OPU_DMA_DIRECTION_MAX
};

#define OPU_FENCE_JIFFIES_TIMEOUT   (HZ)

enum opu_chip_flags {
    OPU_ASIC_MASK = 0x0000ffffUL,
};

#define OPU_MAX_BUS_NUM  (OPU_BUS_MAX)
#define OPU_MAX_IP_NUM  (OPU_IP_MAX)
#define OPU_MAX_IP_NAME_SIZE 8

struct opu_bus_funcs {
    int (*init)(void *handle);
    int (*fini)(void *handle);
    int (*reset)(void *handle);
};

struct opu_bus {
    enum opu_bus_type type;
    bool              valid;
    const struct opu_bus_funcs *funcs;
    void             *priv;
};

struct opu_pci_bdf {
    uint32_t pci_domain;
    uint32_t pci_bus;
    uint32_t pci_slot;
    uint32_t pci_func;
};

#define OPU_BDF_DESC_MAX_LENGTH 64

#define MAX_GPU_INSTANCE		16
struct opu_gpu_instance
{
	struct opu_device		*odev;
	int				mgpu_fan_enabled;
};

struct opu_mgpu_info
{
	struct opu_gpu_instance	gpu_ins[MAX_GPU_INSTANCE];
	struct mutex			mutex;
	uint32_t			num_gpu;
	uint32_t			num_dgpu;
	uint32_t			num_apu;

	/* delayed reset_func for XGMI configuration if necessary */
	struct delayed_work		delayed_reset_work;
	bool				pending_reset;
};

enum opu_ss {
	opu_SS_DRV_LOAD,
	opu_SS_DEV_D0,
	opu_SS_DEV_D3,
	opu_SS_DRV_UNLOAD
};

struct opu_watchdog_timer
{
	bool timeout_fatal_disable;
	uint32_t period; /* maxCycles = (1 << period), the number of cycles before a timeout */
};

#define OPU_MAX_TIMEOUT_PARAM_LENGTH	256
#define OPU_MAX_QUEUES 1024

struct opu_device;

enum opu_cp_irq {
	opu_CP_IRQ_GFX_ME0_PIPE0_EOP = 0,
	opu_CP_IRQ_LAST
};

enum opu_chip_reset_type {
    OPU_CORE_RESET,
    OPU_FULL_RESET
};

enum opu_chip_type {
    CHIP_OPU = 0
};

struct core_shared_resources {
	/* Bit n == 1 means VMID n is available for KFD. */
	unsigned int compute_vmid_bitmap;

	/* Bit n == 1 means Queue n is available for KFD */
	DECLARE_BITMAP(cp_queue_bitmap, OPU_MAX_QUEUES);

	/* From SOC15 onward, the doorbell index range not usable for CP
	 * queues.
	 */
	uint32_t non_cp_doorbells_start;
	uint32_t non_cp_doorbells_end;

	/* Base address of doorbell aperture. */
	phys_addr_t doorbell_physical_address;

	/* Size in bytes of doorbell aperture. */
	size_t doorbell_aperture_size;

	/* Number of bytes at start of aperture reserved for KGD. */
	size_t doorbell_start_offset;

	/* GPUVM address space size in bytes */
	uint64_t gpuvm_size;

	/* Minor device number of the render node */
	int drm_render_minor;

};

struct opu_vmid_info {
	uint32_t first_vmid_opu;
	uint32_t last_vmid_opu;
	uint32_t vmid_num_opu;
};

/*
struct amdgpu_ip_block {
	struct amdgpu_ip_block_status status;
	const enum amd_ip_block_type type;
	const struct amd_ip_funcs *funcs;
};
*/
#define OPU_MAX_NUM_OF_PROCESSES 512
#define OPU_MAX_NUM_OF_QUEUES_PER_PROCESS 1024

#define OPU_MAX_NUM_OF_QUEUES_PER_DEVICE		\
	(OPU_MAX_NUM_OF_PROCESSES *			        \
			OPU_MAX_NUM_OF_QUEUES_PER_PROCESS)

#define OPU_KERNEL_QUEUE_SIZE 2048

/*
 * 512 = 0x200
 * The doorbell index distance between SDMA RLC (2*i) and (2*i+1) in the
 * same SDMA engine on SOC15, which has 8-byte doorbells for SDMA.
 * 512 8-byte doorbell distance (i.e. one page away) ensures that SDMA RLC
 * (2*i+1) doorbells (in terms of the lower 12 bit address) lie exactly in
 * the OFFSET and SIZE set in registers like BIF_SDMA0_DOORBELL_RANGE.
 */
#define OPU_QUEUE_DOORBELL_MIRROR_OFFSET 512


struct opu_device {
	struct drm_device		*ddev;
	struct device			*dev;
    struct opu_pci_bdf      bdf;
    struct opu_core_dev     *core_dev;

	struct pci_dev			*pdev;
	// enum opu_chip_type		chip_type;
	unsigned long			flags;
    bool                    vf_function;

    uint32_t                uniqueid;
    uint32_t                uniqueid_0;
    char                    bdf_desc[OPU_BDF_DESC_MAX_LENGTH];

	const struct opu_core_device_info *device_info;
	unsigned int id;		/* topology stub index */

	phys_addr_t doorbell_base;	/* Start of actual doorbells used by
					 * OPU. It is aligned for mapping
					 * into user mode
					 */
	size_t doorbell_base_dw_offset;	/* Offset from the start of the PCI
					 * doorbell BAR to the first OPU
					 * doorbell in dwords. GFX reserves
					 * the segment before this offset.
					 */
	u32 __iomem *doorbell_kernel_ptr; /* This is a pointer for a doorbells
					   * page used by kernel queue
					   */

	struct core_shared_resources core_resources;
	struct opu_vmid_info vm_info;

	const struct opu_core_funcs *core_funcs;

	struct mutex doorbell_mutex;

	DECLARE_BITMAP(doorbell_available_index,
			OPU_MAX_NUM_OF_QUEUES_PER_PROCESS);

	/* Interrupts */
	struct kfifo ih_fifo;
	struct workqueue_struct *ih_wq;
	struct work_struct interrupt_work;
	spinlock_t interrupt_lock;

	/* QCM Device instance */
	struct device_queue_manager *dqm;

	bool init_complete;
	/*
	 * Interrupts of interest to OPU are copied
	 * from the HW ring into a SW ring.
	 */
	bool interrupts_active;

	/* Debug manager */
	struct opu_dbgmgr *dbgmgr;

	/* CWSR */
	bool cwsr_enabled;
	const void *cwsr_isa;
	unsigned int cwsr_isa_size;



	/* Register/doorbell mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;

	// struct opu_mmio_remap        rmmio_remap;

	/* protects concurrent SMC based register access */
	/* protects concurrent PCIE register access */
	spinlock_t pcie_idx_lock;
	// struct opu_doorbell		doorbell;

	/* clock/pll info */
	// struct opu_clock            clock;

	/* MC */
	dma_addr_t			dummy_page_addr;
	// struct opu_vm_manager	vm_manager;
	// struct opu_vmhub             vmhub[opu_MAX_VMHUBS];
	// unsigned			num_vmhubs;

	/* memory management */
	// struct opu_mman		mman;
	// struct opu_vram_scratch	vram_scratch;
	// struct opu_wb		wb;
	atomic64_t			num_bytes_moved;
	atomic64_t			num_evictions;
	atomic64_t			num_vram_cpu_page_faults;

	/* data for buffer migration throttling */
	struct {
		spinlock_t		lock;
		s64			last_update_us;
		s64			accum_us; /* accumulated microseconds */
		s64			accum_us_vis; /* for visible VRAM */
		u32			log2_max_MBps;
	} mm_stats;

	u64				fence_context;
	unsigned			num_rings;
	// struct amdgpu_ring		*rings[AMDGPU_MAX_RINGS];
	// bool				ib_pool_ready;

    struct opu_mem_mgr      *mem_mgr;
	// struct amdgpu_ip_block          ip_blocks[AMDGPU_MAX_IP_NUM];
	// uint32_t		        harvest_ip_mask;
	// int				num_ip_blocks;
	// struct mutex	mn_lock;
	// DECLARE_HASHTABLE(mn_hash, 7);

	/* interrupts */
	// struct opu_irq		irq;
    struct opu_irq_mgr      *irq_mgr;

	/* link all shadow bo */
	struct list_head                shadow_list;
	struct mutex                    shadow_list_lock;

	/* s3/s4 mask */
	bool                            in_suspend;
	bool				in_s3;
	bool				in_s4;
	bool				in_s0ix;

	atomic_t 			in_gpu_reset;
	struct opu_doorbell_index doorbell_index;
	int asic_reset_res;
	uint64_t			unique_id;

	struct pci_saved_state          *pci_state;
	struct opu_reset_control     *reset_cntl;
};


// opu kms.c
int opu_driver_load_kms(struct drm_device *dev, unsigned long flags);
void opu_driver_unload_kms(struct drm_device *dev);
// int opu_driver_opn(struct drm_device *dev);


#endif
