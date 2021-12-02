
#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "opu_priv.h"
#include "opu_device_queue_manager.h"
#include "opu_pm4_headers_vi.h"
#include "opu_pm4_headers_aldebaran.h"
#include "cwsr_trap_handler.h"
#include "opu_iommu.h"
#include "amdgpu_amdopu.h"
#include "opu_smi_events.h"
#include "opu_migrate.h"

#define MQD_SIZE_ALIGNED 768

/*
 * opu_locked is used to lock the opu driver during suspend or reset
 * once locked, opu driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static atomic_t opu_locked = ATOMIC_INIT(0);

#ifdef CONFIG_DRM_AMDGPU_CIK
extern const struct opu2kgd_calls gfx_v7_opu2kgd;
#endif
extern const struct opu2kgd_calls gfx_v8_opu2kgd;
extern const struct opu2kgd_calls gfx_v9_opu2kgd;
extern const struct opu2kgd_calls arcturus_opu2kgd;
extern const struct opu2kgd_calls aldebaran_opu2kgd;
extern const struct opu2kgd_calls gfx_v10_opu2kgd;
extern const struct opu2kgd_calls gfx_v10_3_opu2kgd;

static const struct opu2kgd_calls *opu2kgd_funcs[] = {
#ifdef OPU_SUPPORT_IOMMU_V2
#ifdef CONFIG_DRM_AMDGPU_CIK
	[CHIP_KAVERI] = &gfx_v7_opu2kgd,
#endif
	[CHIP_CARRIZO] = &gfx_v8_opu2kgd,
	[CHIP_RAVEN] = &gfx_v9_opu2kgd,
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	[CHIP_HAWAII] = &gfx_v7_opu2kgd,
#endif
	[CHIP_TONGA] = &gfx_v8_opu2kgd,
	[CHIP_FIJI] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS10] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS11] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS12] = &gfx_v8_opu2kgd,
	[CHIP_VEGAM] = &gfx_v8_opu2kgd,
	[CHIP_VEGA10] = &gfx_v9_opu2kgd,
	[CHIP_VEGA12] = &gfx_v9_opu2kgd,
	[CHIP_VEGA20] = &gfx_v9_opu2kgd,
	[CHIP_RENOIR] = &gfx_v9_opu2kgd,
	[CHIP_ARCTURUS] = &arcturus_opu2kgd,
	[CHIP_ALDEBARAN] = &aldebaran_opu2kgd,
	[CHIP_NAVI10] = &gfx_v10_opu2kgd,
	[CHIP_NAVI12] = &gfx_v10_opu2kgd,
	[CHIP_NAVI14] = &gfx_v10_opu2kgd,
	[CHIP_SIENNA_CICHLID] = &gfx_v10_3_opu2kgd,
	[CHIP_NAVY_FLOUNDER] = &gfx_v10_3_opu2kgd,
	[CHIP_VANGOGH] = &gfx_v10_3_opu2kgd,
	[CHIP_DIMGREY_CAVEFISH] = &gfx_v10_3_opu2kgd,
	[CHIP_BEIGE_GOBY] = &gfx_v10_3_opu2kgd,
	[CHIP_YELLOW_CARP] = &gfx_v10_3_opu2kgd,
};


static const struct opu_device_info carrizo_device_info = {
	.asic_family = CHIP_CARRIZO,
	.asic_name = "carrizo",
	.max_pasid_bits = 16,
	/* max num of queues for CZ.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

/* For each entry, [0] is regular and [1] is virtualisation device. */
static const struct opu_device_info *opu_supported_devices[][2] = {
	[CHIP_CARRIZO] = {&carrizo_device_info, NULL},
};

static int opu_gtt_sa_init(struct opu_dev *opu, unsigned int buf_size,
				unsigned int chunk_size);
static void opu_gtt_sa_fini(struct opu_dev *opu);

static int opu_resume(struct opu_dev *opu);

struct opu_dev *kgd2opu_probe(struct kgd_dev *kgd,
	struct pci_dev *pdev, unsigned int asic_type, bool vf)
{
	struct opu_dev *opu;
	const struct opu_device_info *device_info;
	const struct opu2kgd_calls *f2g;

	if (asic_type >= sizeof(opu_supported_devices) / (sizeof(void *) * 2)
		|| asic_type >= sizeof(opu2kgd_funcs) / sizeof(void *)) {
		dev_err(opu_device, "asic_type %d out of range\n", asic_type);
		return NULL; /* asic_type out of range */
	}

	device_info = opu_supported_devices[asic_type][vf];
	f2g = opu2kgd_funcs[asic_type];

	if (!device_info || !f2g) {
		dev_err(opu_device, "%s %s not supported in opu\n",
			amdgpu_asic_name[asic_type], vf ? "VF" : "");
		return NULL;
	}

	opu = kzalloc(sizeof(*opu), GFP_KERNEL);
	if (!opu)
		return NULL;

	/* Allow BIF to recode atomics to PCIe 3.0 AtomicOps.
	 * 32 and 64-bit requests are possible and must be
	 * supported.
	 */
	opu->pci_atomic_requested = amdgpu_amdopu_have_atomics_support(kgd);
	if (device_info->needs_pci_atomics &&
	    !opu->pci_atomic_requested) {
		dev_info(opu_device,
			 "skipped device %x:%x, PCI rejects atomics\n",
			 pdev->vendor, pdev->device);
		kfree(opu);
		return NULL;
	}

	opu->kgd = kgd;
	opu->device_info = device_info;
	opu->pdev = pdev;
	opu->init_complete = false;
	opu->opu2kgd = f2g;
	atomic_set(&opu->compute_profile, 0);

	mutex_init(&opu->doorbell_mutex);
	memset(&opu->doorbell_available_index, 0,
		sizeof(opu->doorbell_available_index));

	atomic_set(&opu->sram_ecc_flag, 0);

	ida_init(&opu->doorbell_ida);

	return opu;
}

static void opu_cwsr_init(struct opu_dev *opu)
{
	if (cwsr_enable && opu->device_info->supports_cwsr) {
		if (opu->device_info->asic_family < CHIP_VEGA10) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx8_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx8_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx8_hex);
		} else if (opu->device_info->asic_family == CHIP_ARCTURUS) {
			BUILD_BUG_ON(sizeof(cwsr_trap_arcturus_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_arcturus_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_arcturus_hex);
		} else if (opu->device_info->asic_family == CHIP_ALDEBARAN) {
			BUILD_BUG_ON(sizeof(cwsr_trap_aldebaran_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_aldebaran_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_aldebaran_hex);
		} else if (opu->device_info->asic_family < CHIP_NAVI10) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx9_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx9_hex);
		} else if (opu->device_info->asic_family < CHIP_SIENNA_CICHLID) {
			BUILD_BUG_ON(sizeof(cwsr_trap_nv1x_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_nv1x_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_nv1x_hex);
		} else {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx10_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx10_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx10_hex);
		}

		opu->cwsr_enabled = true;
	}
}

/**
 * amdgpu_device_ip_init - run init for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Main initialization pass for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the sw_init and hw_init callbacks
 * are run.  sw_init initializes the software state associated with each IP
 * and hw_init initializes the hardware associated with each IP.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_init(struct opu_device *odev)
{
	int i, err;

    odev->num_ips = 0;

    for (i = 0; i < (sizeof(odev->ips_type2idx) / sizeof(odev->ips_type2idx[0])); i++)
        odev->ips_type2idx[i] = -1;

    if (odev->chip_type == OPU_CHIP_OPU) {
        if (!(odev->pdev && (odev->pdev->device == DEVICE_ID_OPU ||
                        odev->pdev->device == DEVICE_ID_OPU_VF)))
        {
            err = -ENODEV;
            return err;
        } else {
            opu_chip_setup(odev);
        }
    } else {
        err = -EINVAL;
        return err;
    }

	for (i = 0; i < odev->num_ips; i++) {
		if (!odev->ips[i].valid)
			continue;
        if (odev->ips[i].funcs->init) {
            err = odev->ips[i].funcs->init(odev);
            if (!err)
                odev->ips[i].valid = true;
        }
	}

    if (ret)
        opu_device_ips_fini(odev);

init_failed:
	return r;
}

/**
 * amdgpu_device_init - initialize the driver
 *
 * @adev: amdgpu_device pointer
 * @flags: driver flags
 *
 * Initializes the driver info and hw (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver startup.
 */
int opu_device_init(struct opu_device *odev,
		       uint32_t flags)
{
    int ret;
	struct drm_device *ddev = odev->ddev;
    struct pci_bus *bus;
	struct pci_dev *pdev = odev->pdev;

    /* init BDF */
    bus = pdev->bus;
    odev->bdf.pci_domain = pci_domain_nr(bus);
    odev->bdf.pci_bus = bus->number;
    odev->bdf.pci_slot = PCI_SLOT(pdev->devfn);
    odev->bdf.pci_func = PCI_FUNC(pdev->devfn);

	odev->shutdown = false;
	odev->flags = flags;

	/* mutex initialization are all done here so we
	 * can recall function without having locking issues */
	mutex_init(&odev->lock);
	mutex_init(&odev->res_lock);
    spin_lock_init(&odev->job_lock);
	mutex_init(&odev->core_info_lock);
	mutex_init(&odev->profile_lock);
	mutex_init(&odev->profile_thread_lock);
	atomic_set(&odev->profile_count, 0);
	atomic_set(&odev->retained, 0);

    odev->cp        = 0;
    odev->profile   = NULL;
    odev->chip_type = flags & OPU_ASIC_MASK;

    ret = opu_va_mgr_create(&g_opu_va_mgr);
    if (ret)
        goto err_va_mgr:

	// r = amdgpu_device_check_arguments(odev);
	// if (r) return r;

	/* Registers mapping */
	/* TODO: block userspace mapping of io register */
	/*odev->rmmio_base = pci_resource_start(odev->pdev, 5);
	odev->rmmio_size = pci_resource_len(odev->pdev, 5);

	odev->rmmio = ioremap(odev->rmmio_base, odev->rmmio_size);
	if (odev->rmmio == NULL) {
		return -ENOMEM;
	}
	DRM_INFO("register mmio base: 0x%08X\n", (uint32_t)odev->rmmio_base);
	DRM_INFO("register mmio size: %u\n", (unsigned)odev->rmmio_size);
    */

	/* enable PCIE atomic ops */
	r = pci_enable_atomic_ops_to_root(odev->pdev,
					  PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
					  PCI_EXP_DEVCAP2_ATOMIC_COMP64);
	if (r) {
		odev->have_atomics_support = false;
		DRM_INFO("PCIE atomic ops is not supported\n");
	} else {
		odev->have_atomics_support = true;
	}
	amdgpu_device_get_pcie_info(odev);

	/* early init functions */
	// r = amdgpu_device_ip_early_init(adev);
	if (r)
		goto failed_unmap;

	/* doorbell bar mapping and doorbell index init*/
	amdgpu_device_doorbell_init(odev);

	amdgpu_reset_init(odev);

	pci_enable_pcie_error_reporting(odev->pdev);

fence_driver_init:
	/* Fence driver */
	r = amdgpu_fence_driver_init(odev);

	/* init the mode config */
	drm_mode_config_init(adev_to_drm(adev));

	r = amdgpu_device_ip_init(adev);

	/*
	 * Register gpu instance before amdgpu_device_enable_mgpu_fan_boost.
	 * Otherwise the mgpu fan boost feature will be skipped due to the
	 * gpu instance is counted less.
	 */
	amdgpu_register_gpu_instance(odev);

	/* Have stored pci confspace at hand for restore in sudden PCI error */
	if (amdgpu_device_cache_pci_state(odev->pdev))
		pci_restore_state(pdev);

	return 0;

failed_unmap:
	iounmap(odev->rmmio);
	odev->rmmio = NULL;

	return r;
}


void kgd2opu_device_exit(struct opu_dev *opu)
{
	if (opu->init_complete) {
		svm_migrate_fini((struct amdgpu_device *)opu->kgd);
		device_queue_manager_uninit(opu->dqm);
		opu_interrupt_exit(opu);
		opu_topology_remove_device(opu);
		opu_doorbell_fini(opu);
		ida_destroy(&opu->doorbell_ida);
		opu_gtt_sa_fini(opu);
		amdgpu_amdopu_free_gtt_mem(opu->kgd, opu->gtt_mem);
		if (opu->gws)
			amdgpu_amdopu_free_gws(opu->kgd, opu->gws);
	}

	kfree(opu);
}

int kgd2opu_pre_reset(struct opu_dev *opu)
{
	if (!opu->init_complete)
		return 0;

	opu_smi_event_update_gpu_reset(opu, false);

	opu->dqm->ops.pre_reset(opu->dqm);

	kgd2opu_suspend(opu, false);

	opu_signal_reset_event(opu);
	return 0;
}

/*
 * Fix me. OPU won't be able to resume existing process for now.
 * We will keep all existing process in a evicted state and
 * wait the process to be terminated.
 */

int kgd2opu_post_reset(struct opu_dev *opu)
{
	int ret;

	if (!opu->init_complete)
		return 0;

	ret = opu_resume(opu);
	if (ret)
		return ret;
	atomic_dec(&opu_locked);

	atomic_set(&opu->sram_ecc_flag, 0);

	opu_smi_event_update_gpu_reset(opu, true);

	return 0;
}

bool opu_is_locked(void)
{
	return  (atomic_read(&opu_locked) > 0);
}

void kgd2opu_suspend(struct opu_dev *opu, bool run_pm)
{
	if (!opu->init_complete)
		return;

	/* for runtime suspend, skip locking opu */
	if (!run_pm) {
		/* For first OPU device suspend all the OPU processes */
		if (atomic_inc_return(&opu_locked) == 1)
			opu_suspend_all_processes();
	}

	opu->dqm->ops.stop(opu->dqm);
	opu_iommu_suspend(opu);
}

int kgd2opu_resume(struct opu_dev *opu, bool run_pm)
{
	int ret, count;

	if (!opu->init_complete)
		return 0;

	ret = opu_resume(opu);
	if (ret)
		return ret;

	/* for runtime resume, skip unlocking opu */
	if (!run_pm) {
		count = atomic_dec_return(&opu_locked);
		WARN_ONCE(count < 0, "OPU suspend / resume ref. error");
		if (count == 0)
			ret = opu_resume_all_processes();
	}

	return ret;
}

static int opu_resume(struct opu_dev *opu)
{
	int err = 0;

	err = opu_iommu_resume(opu);
	if (err) {
		dev_err(opu_device,
			"Failed to resume IOMMU for device %x:%x\n",
			opu->pdev->vendor, opu->pdev->device);
		return err;
	}

	err = opu->dqm->ops.start(opu->dqm);
	if (err) {
		dev_err(opu_device,
			"Error starting queue manager for device %x:%x\n",
			opu->pdev->vendor, opu->pdev->device);
		goto dqm_start_error;
	}

	return err;

dqm_start_error:
	opu_iommu_suspend(opu);
	return err;
}

static inline void opu_queue_work(struct workqueue_struct *wq,
				  struct work_struct *work)
{
	int cpu, new_cpu;

	cpu = new_cpu = smp_processor_id();
	do {
		new_cpu = cpumask_next(new_cpu, cpu_online_mask) % nr_cpu_ids;
		if (cpu_to_node(new_cpu) == numa_node_id())
			break;
	} while (cpu != new_cpu);

	queue_work_on(new_cpu, wq, work);
}

/* This is called directly from KGD at ISR. */
void kgd2opu_interrupt(struct opu_dev *opu, const void *ih_ring_entry)
{
	uint32_t patched_ihre[OPU_MAX_RING_ENTRY_SIZE];
	bool is_patched = false;
	unsigned long flags;

	if (!opu->init_complete)
		return;

	if (opu->device_info->ih_ring_entry_size > sizeof(patched_ihre)) {
		dev_err_once(opu_device, "Ring entry too small\n");
		return;
	}

	spin_lock_irqsave(&opu->interrupt_lock, flags);

	if (opu->interrupts_active
	    && interrupt_is_wanted(opu, ih_ring_entry,
				   patched_ihre, &is_patched)
	    && enqueue_ih_ring_entry(opu,
				     is_patched ? patched_ihre : ih_ring_entry))
		opu_queue_work(opu->ih_wq, &opu->interrupt_work);

	spin_unlock_irqrestore(&opu->interrupt_lock, flags);
}

int kgd2opu_quiesce_mm(struct mm_struct *mm)
{
	struct opu_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	WARN(debug_evictions, "Evicting pid %d", p->lead_thread->pid);
	r = opu_process_evict_queues(p);

	opu_unref_process(p);
	return r;
}

int kgd2opu_resume_mm(struct mm_struct *mm)
{
	struct opu_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	r = opu_process_restore_queues(p);

	opu_unref_process(p);
	return r;
}

/** kgd2opu_schedule_evict_and_restore_process - Schedules work queue that will
 *   prepare for safe eviction of OPU BOs that belong to the specified
 *   process.
 *
 * @mm: mm_struct that identifies the specified OPU process
 * @fence: eviction fence attached to OPU process BOs
 *
 */
int kgd2opu_schedule_evict_and_restore_process(struct mm_struct *mm,
					       struct dma_fence *fence)
{
	struct opu_process *p;
	unsigned long active_time;
	unsigned long delay_jiffies = msecs_to_jiffies(PROCESS_ACTIVE_TIME_MS);

	if (!fence)
		return -EINVAL;

	if (dma_fence_is_signaled(fence))
		return 0;

	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (fence->seqno == p->last_eviction_seqno)
		goto out;

	p->last_eviction_seqno = fence->seqno;

	/* Avoid OPU process starvation. Wait for at least
	 * PROCESS_ACTIVE_TIME_MS before evicting the process again
	 */
	active_time = get_jiffies_64() - p->last_restore_timestamp;
	if (delay_jiffies > active_time)
		delay_jiffies -= active_time;
	else
		delay_jiffies = 0;

	/* During process initialization eviction_work.dwork is initialized
	 * to opu_evict_bo_worker
	 */
	WARN(debug_evictions, "Scheduling eviction of pid %d in %ld jiffies",
	     p->lead_thread->pid, delay_jiffies);
	schedule_delayed_work(&p->eviction_work, delay_jiffies);
out:
	opu_unref_process(p);
	return 0;
}

static int opu_gtt_sa_init(struct opu_dev *opu, unsigned int buf_size,
				unsigned int chunk_size)
{
	unsigned int num_of_longs;

	if (WARN_ON(buf_size < chunk_size))
		return -EINVAL;
	if (WARN_ON(buf_size == 0))
		return -EINVAL;
	if (WARN_ON(chunk_size == 0))
		return -EINVAL;

	opu->gtt_sa_chunk_size = chunk_size;
	opu->gtt_sa_num_of_chunks = buf_size / chunk_size;

	num_of_longs = (opu->gtt_sa_num_of_chunks + BITS_PER_LONG - 1) /
		BITS_PER_LONG;

	opu->gtt_sa_bitmap = kcalloc(num_of_longs, sizeof(long), GFP_KERNEL);

	if (!opu->gtt_sa_bitmap)
		return -ENOMEM;

	pr_debug("gtt_sa_num_of_chunks = %d, gtt_sa_bitmap = %p\n",
			opu->gtt_sa_num_of_chunks, opu->gtt_sa_bitmap);

	mutex_init(&opu->gtt_sa_lock);

	return 0;

}

static void opu_gtt_sa_fini(struct opu_dev *opu)
{
	mutex_destroy(&opu->gtt_sa_lock);
	kfree(opu->gtt_sa_bitmap);
}

static inline uint64_t opu_gtt_sa_calc_gpu_addr(uint64_t start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return start_addr + bit_num * chunk_size;
}

static inline uint32_t *opu_gtt_sa_calc_cpu_addr(void *start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return (uint32_t *) ((uint64_t) start_addr + bit_num * chunk_size);
}

int opu_gtt_sa_allocate(struct opu_dev *opu, unsigned int size,
			struct opu_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;

	if (size == 0)
		return -EINVAL;

	if (size > opu->gtt_sa_num_of_chunks * opu->gtt_sa_chunk_size)
		return -ENOMEM;

	*mem_obj = kzalloc(sizeof(struct opu_mem_obj), GFP_KERNEL);
	if (!(*mem_obj))
		return -ENOMEM;

	pr_debug("Allocated mem_obj = %p for size = %d\n", *mem_obj, size);

	start_search = 0;

	mutex_lock(&opu->gtt_sa_lock);

opu_gtt_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(opu->gtt_sa_bitmap,
					opu->gtt_sa_num_of_chunks,
					start_search);

	pr_debug("Found = %d\n", found);

	/* If there wasn't any free chunk, bail out */
	if (found == opu->gtt_sa_num_of_chunks)
		goto opu_gtt_no_free_chunk;

	/* Update fields of mem_obj */
	(*mem_obj)->range_start = found;
	(*mem_obj)->range_end = found;
	(*mem_obj)->gpu_addr = opu_gtt_sa_calc_gpu_addr(
					opu->gtt_start_gpu_addr,
					found,
					opu->gtt_sa_chunk_size);
	(*mem_obj)->cpu_ptr = opu_gtt_sa_calc_cpu_addr(
					opu->gtt_start_cpu_ptr,
					found,
					opu->gtt_sa_chunk_size);

	pr_debug("gpu_addr = %p, cpu_addr = %p\n",
			(uint64_t *) (*mem_obj)->gpu_addr, (*mem_obj)->cpu_ptr);

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= opu->gtt_sa_chunk_size) {
		pr_debug("Single bit\n");
		set_bit(found, opu->gtt_sa_bitmap);
		goto opu_gtt_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - opu->gtt_sa_chunk_size;
	do {
		(*mem_obj)->range_end =
			find_next_zero_bit(opu->gtt_sa_bitmap,
					opu->gtt_sa_num_of_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mem_obj)->range_end != found) {
			start_search = found;
			goto opu_gtt_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == opu->gtt_sa_num_of_chunks)
			goto opu_gtt_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= opu->gtt_sa_chunk_size)
			cur_size = 0;
		else
			cur_size -= opu->gtt_sa_chunk_size;

	} while (cur_size > 0);

	pr_debug("range_start = %d, range_end = %d\n",
		(*mem_obj)->range_start, (*mem_obj)->range_end);

	/* Mark the chunks as allocated */
	for (found = (*mem_obj)->range_start;
		found <= (*mem_obj)->range_end;
		found++)
		set_bit(found, opu->gtt_sa_bitmap);

opu_gtt_out:
	mutex_unlock(&opu->gtt_sa_lock);
	return 0;

opu_gtt_no_free_chunk:
	pr_debug("Allocation failed with mem_obj = %p\n", *mem_obj);
	mutex_unlock(&opu->gtt_sa_lock);
	kfree(*mem_obj);
	return -ENOMEM;
}

int opu_gtt_sa_free(struct opu_dev *opu, struct opu_mem_obj *mem_obj)
{
	unsigned int bit;

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("Free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&opu->gtt_sa_lock);

	/* Mark the chunks as free */
	for (bit = mem_obj->range_start;
		bit <= mem_obj->range_end;
		bit++)
		clear_bit(bit, opu->gtt_sa_bitmap);

	mutex_unlock(&opu->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}

void kgd2opu_set_sram_ecc_flag(struct opu_dev *opu)
{
	if (opu)
		atomic_inc(&opu->sram_ecc_flag);
}

void opu_inc_compute_active(struct opu_dev *opu)
{
	if (atomic_inc_return(&opu->compute_profile) == 1)
		amdgpu_amdopu_set_compute_idle(opu->kgd, false);
}

void opu_dec_compute_active(struct opu_dev *opu)
{
	int count = atomic_dec_return(&opu->compute_profile);

	if (count == 0)
		amdgpu_amdopu_set_compute_idle(opu->kgd, true);
	WARN_ONCE(count < 0, "Compute profile ref. count error");
}

void kgd2opu_smi_event_throttle(struct opu_dev *opu, uint32_t throttle_bitmask)
{
	if (opu && opu->init_complete)
		opu_smi_event_update_thermal_throttling(opu, throttle_bitmask);
}

#if defined(CONFIG_DEBUG_FS)

/* This function will send a package to HIQ to hang the HWS
 * which will trigger a GPU reset and bring the HWS back to normal state
 */
int opu_debugfs_hang_hws(struct opu_dev *dev)
{
	int r = 0;

	if (dev->dqm->sched_policy != OPU_SCHED_POLICY_HWS) {
		pr_err("HWS is not enabled");
		return -EINVAL;
	}

	r = pm_debugfs_hang_hws(&dev->dqm->packets);
	if (!r)
		r = dqm_debugfs_execute_queues(dev->dqm);

	return r;
}

/**
 * amdgpu_device_fini - tear down the driver
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the driver info (all asics).
 * Called at driver shutdown.
 */
void amdgpu_device_fini_hw(struct amdgpu_device *adev)
{
	dev_info(adev->dev, "amdgpu: finishing device.\n");
	flush_delayed_work(&adev->delayed_init_work);
	ttm_bo_lock_delayed_workqueue(&adev->mman.bdev);
	adev->shutdown = true;

	/* make sure IB test finished before entering exclusive mode
	 * to avoid preemption on IB test
	 * */
	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_request_full_gpu(adev, false);
		amdgpu_virt_fini_data_exchange(adev);
	}

	/* disable all interrupts */
	amdgpu_irq_disable_all(adev);
	if (adev->mode_info.mode_config_initialized){
		if (!amdgpu_device_has_dc_support(adev))
			drm_helper_force_disable_all(adev_to_drm(adev));
		else
			drm_atomic_helper_shutdown(adev_to_drm(adev));
	}
	amdgpu_fence_driver_fini_hw(adev);

	if (adev->pm_sysfs_en)
		amdgpu_pm_sysfs_fini(adev);
	if (adev->ucode_sysfs_en)
		amdgpu_ucode_sysfs_fini(adev);
	sysfs_remove_files(&adev->dev->kobj, amdgpu_dev_attributes);

	amdgpu_fbdev_fini(adev);

	amdgpu_irq_fini_hw(adev);

	amdgpu_device_ip_fini_early(adev);

	amdgpu_gart_dummy_page_fini(adev);

	amdgpu_device_unmap_mmio(adev);
}

/**
 * amdgpu_device_check_arguments - validate module params
 *
 * @adev: amdgpu_device pointer
 *
 * Validates certain module parameters and updates
 * the associated values used by the driver (all asics).
 */
static int amdgpu_device_check_arguments(struct amdgpu_device *adev)
{
	if (amdgpu_sched_jobs < 4) {
		dev_warn(adev->dev, "sched jobs (%d) must be at least 4\n",
			 amdgpu_sched_jobs);
		amdgpu_sched_jobs = 4;
	} else if (!is_power_of_2(amdgpu_sched_jobs)){
		dev_warn(adev->dev, "sched jobs (%d) must be a power of 2\n",
			 amdgpu_sched_jobs);
		amdgpu_sched_jobs = roundup_pow_of_two(amdgpu_sched_jobs);
	}

	if (amdgpu_gart_size != -1 && amdgpu_gart_size < 32) {
		/* gart size must be greater or equal to 32M */
		dev_warn(adev->dev, "gart size (%d) too small\n",
			 amdgpu_gart_size);
		amdgpu_gart_size = -1;
	}

	if (amdgpu_gtt_size != -1 && amdgpu_gtt_size < 32) {
		/* gtt size must be greater or equal to 32M */
		dev_warn(adev->dev, "gtt size (%d) too small\n",
				 amdgpu_gtt_size);
		amdgpu_gtt_size = -1;
	}

	/* valid range is between 4 and 9 inclusive */
	if (amdgpu_vm_fragment_size != -1 &&
	    (amdgpu_vm_fragment_size > 9 || amdgpu_vm_fragment_size < 4)) {
		dev_warn(adev->dev, "valid range is between 4 and 9\n");
		amdgpu_vm_fragment_size = -1;
	}

	if (amdgpu_sched_hw_submission < 2) {
		dev_warn(adev->dev, "sched hw submission jobs (%d) must be at least 2\n",
			 amdgpu_sched_hw_submission);
		amdgpu_sched_hw_submission = 2;
	} else if (!is_power_of_2(amdgpu_sched_hw_submission)) {
		dev_warn(adev->dev, "sched hw submission jobs (%d) must be a power of 2\n",
			 amdgpu_sched_hw_submission);
		amdgpu_sched_hw_submission = roundup_pow_of_two(amdgpu_sched_hw_submission);
	}

	amdgpu_device_check_smu_prv_buffer_size(adev);

	amdgpu_device_check_vm_size(adev);

	amdgpu_device_check_block_size(adev);

	adev->firmware.load_type = amdgpu_ucode_get_load_type(adev, amdgpu_fw_load_type);

	amdgpu_gmc_tmz_set(adev);

	amdgpu_gmc_noretry_set(adev);

	return 0;
}

#endif
