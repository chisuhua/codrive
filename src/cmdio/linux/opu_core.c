#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "core_priv.h"
#include "core_device_queue_manager.h"
#include "cwsr_trap_handler.h"
#include "opu_iommu.h"
#include "opu.h"
#include "opu_smi_events.h"
#include "opu_migrate.h"

/*
 * core_locked is used to lock the kfd driver during suspend or reset
 * once locked, kfd driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static atomic_t core_locked = ATOMIC_INIT(0);

extern const struct opu_core_funcs core_asic_v1_funcs;

static const struct opu_core_funcs *core_asic_funcs = {
	[CHIP_OPU] = &core_asic_v1_funcs,
}

static const struct opu_core_device_info *core_devices_info[][2] = {
	[CHIP_OPU] = {&core_asic_v1_info, NULL},
}

/* Total memory size in system memory and all GPU VRAM. Used to
 * estimate worst case amount of memory to reserve for page tables
 */
uint64_t core_total_mem_size;

static bool core_initialized;

int core_v1_0_init(opu_device* odev)
{
	bool vf = 0; // amdgpu_sriov_vf(odev);

    odev->core_funcs = core_asic_funcs[odev->asic_type];
    odev->device_info = devices_info[odev->asic_type][vf];
	odev->core_dev->init_complete = false;

	struct sysinfo si;
	int ret;

	si_meminfo(&si);
	core_total_mem_size = si.freeram - si.freehigh;
	core_total_mem_size *= si.mem_unit;

	odev->core_dev.init_complete = core_init(odev);

	core_gpuvm_init_mem_limits();
    core_initialized = !ret;

	return ret;
}

void core_v1_0_fini(void)
{
	if (core_initialized) {
		core_exit();
		core_initialized = false;
	}
}


bool core_init(struct opu_device *odev)
{

	unsigned int size, map_process_packet_size;

	odev->core_resources = {
		.compute_vmid_bitmap =
				((1 << AMDGPU_NUM_VMID) - 1) -
				((1 << odev->vm_manager.first_core_vmid) - 1),
		.gpuvm_size = min(odev->vm_manager.max_pfn
				  << AMDGPU_GPU_PAGE_SHIFT,
				  AMDGPU_GMC_HOLE_START),
		.drm_render_minor = odev_to_drm(odev)->render->index,
		.sdma_doorbell_idx = odev->doorbell_index.sdma_engine,
	};

	/* The first num_doorbells are used by amdgpu.
	 * amdkfd takes whatever's left in the aperture.
	 */
	if (odev->doorbell.size > odev->doorbell.num_doorbells * sizeof(u32)) {
		odev->core_resources.doorbell_physical_address = odev->doorbell.base;
		odev->core_resources.doorbell_aperture_size = odev->doorbell.size;
		odev->core_resources.doorbell_start_offset = odev->doorbell.num_doorbells * sizeof(u32);
	} else {
		odev->core_resources.doorbell_physical_address = 0;
		odev->core_resources.doorbell_aperture_size = 0;
		odev->core_resources.doorbell_start_offset = 0;
	}

	opu->vm_info.first_vmid_opu = ffs(gpu_resources->compute_vmid_bitmap)-1;
	opu->vm_info.last_vmid_opu = fls(gpu_resources->compute_vmid_bitmap)-1;
	opu->vm_info.vmid_num_opu = opu->vm_info.last_vmid_opu
			- opu->vm_info.first_vmid_opu + 1;

	/* Verify module parameters regarding mapped process number*/
	if ((hws_max_conc_proc < 0)
			|| (hws_max_conc_proc > opu->vm_info.vmid_num_opu)) {
		dev_err(opu_device,
			"hws_max_conc_proc %d must be between 0 and %d, use %d instead\n",
			hws_max_conc_proc, opu->vm_info.vmid_num_opu,
			opu->vm_info.vmid_num_opu);
		opu->max_proc_per_quantum = opu->vm_info.vmid_num_opu;
	} else
		opu->max_proc_per_quantum = hws_max_conc_proc;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_queues_per_device *
			opu->device_info->mqd_size_aligned;

	if (opu_doorbell_init(opu)) {
		dev_err(opu_device,
			"Error initializing doorbell aperture\n");
		goto opu_doorbell_error;
	}

	opu->hive_id = amdgpu_amdopu_get_hive_id(opu->kgd);

	opu->noretry = amdgpu_amdopu_get_noretry(opu->kgd);

	if (opu_interrupt_init(opu)) {
		dev_err(opu_device, "Error initializing interrupts\n");
		goto opu_interrupt_error;
	}

	if (opu_iommu_device_init(opu)) {
		dev_err(opu_device, "Error initializing iommuv2\n");
		goto device_iommu_error;
	}

	opu_cwsr_init(opu);

	svm_migrate_init((struct opu_device *)opu->kgd);

	if (opu_resume(opu))
		goto opu_resume_error;

	opu->dbgmgr = NULL;

	if (opu_topology_add_device(opu)) {
		dev_err(opu_device, "Error adding device to topology\n");
		goto opu_topology_add_device_error;
	}

	opu_smi_init(opu);

	opu->init_complete = true;
	dev_info(opu_device, "added device %x:%x\n", opu->pdev->vendor,
		 opu->pdev->device);

	pr_debug("Starting opu with the following scheduling policy %d\n",
		opu->dqm->sched_policy);

	goto out;

opu_topology_add_device_error:
opu_resume_error:
device_iommu_error:
gws_error:
	device_queue_manager_uninit(opu->dqm);
device_queue_manager_error:
	opu_interrupt_exit(opu);
opu_interrupt_error:
	opu_doorbell_fini(opu);
opu_doorbell_error:
	opu_gtt_sa_fini(opu);
opu_gtt_sa_init_error:
	amdgpu_amdopu_free_gtt_mem(opu->kgd, opu->gtt_mem);
alloc_gtt_mem_failure:
	if (opu->gws)
		amdgpu_amdopu_free_gws(opu->kgd, opu->gws);
	dev_err(opu_device,
		"device %x:%x NOT added due to errors\n",
		opu->pdev->vendor, opu->pdev->device);
out:
	return opu->init_complete;
}

void core_exit(struct opu_device *odev)
{
	if (odev->core_dev->init_complete) {
		svm_migrate_fini((struct opu_device *)kfd->kgd);
		device_queue_manager_uninit(kfd->dqm);
		core_interrupt_exit(kfd);
		core_topology_remove_device(kfd);
		core_doorbell_fini(kfd);
		ida_destroy(&kfd->doorbell_ida);
		core_gtt_sa_fini(kfd);
		core_free_gtt_mem(kfd->kgd, kfd->gtt_mem);
		if (kfd->gws)
			core_free_gws(kfd->kgd, kfd->gws);
	}
	kfree(kfd);
}



const struct opu_ip_funcs core_v1_0_funcs = {
    .init = core_v1_0_init,
    .fini = core_v1_0_fini,
    .reinit = core_v1_0_init,
}

int core_alloc_gtt_mem(struct opu_device *kgd, size_t size,
				void **mem_obj, uint64_t *gpu_addr,
				void **cpu_ptr, bool cp_mqd_gfx9)
{
	struct opu_device *odev = (struct opu_device *)kgd;
	struct amdgpu_bo *bo = NULL;
	struct amdgpu_bo_param bp;
	int r;
	void *cpu_ptr_tmp = NULL;

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_GTT;
	bp.flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	if (cp_mqd_gfx9)
		bp.flags |= AMDGPU_GEM_CREATE_CP_MQD_GFX9;

	r = amdgpu_bo_create(odev, &bp, &bo);
	if (r) {
		dev_err(odev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = amdgpu_bo_reserve(bo, true);
	if (r) {
		dev_err(odev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
	if (r) {
		dev_err(odev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}

	r = amdgpu_ttm_alloc_gart(&bo->tbo);
	if (r) {
		dev_err(odev->dev, "%p bind failed\n", bo);
		goto allocate_mem_kmap_bo_failed;
	}

	r = amdgpu_bo_kmap(bo, &cpu_ptr_tmp);
	if (r) {
		dev_err(odev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}

	*mem_obj = bo;
	*gpu_addr = amdgpu_bo_gpu_offset(bo);
	*cpu_ptr = cpu_ptr_tmp;

	amdgpu_bo_unreserve(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:
	amdgpu_bo_unref(&bo);

	return r;
}

void core_free_gtt_mem(struct opu_device *kgd, void *mem_obj)
{
	struct amdgpu_bo *bo = (struct amdgpu_bo *) mem_obj;

	amdgpu_bo_reserve(bo, true);
	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&(bo));
}

void core_get_local_mem_info(struct opu_device *kgd,
				      struct core_local_mem_info *mem_info)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	memset(mem_info, 0, sizeof(*mem_info));

	mem_info->local_mem_size_public = odev->gmc.visible_vram_size;
	mem_info->local_mem_size_private = odev->gmc.real_vram_size -
						odev->gmc.visible_vram_size;

	mem_info->vram_width = odev->gmc.vram_width;

	pr_debug("Address base: %pap public 0x%llx private 0x%llx\n",
			&odev->gmc.aper_base,
			mem_info->local_mem_size_public,
			mem_info->local_mem_size_private);

	if (amdgpu_sriov_vf(odev))
		mem_info->mem_clk_max = odev->clock.default_mclk / 100;
	else if (odev->pm.dpm_enabled) {
		if (amdgpu_emu_mode == 1)
			mem_info->mem_clk_max = 0;
		else
			mem_info->mem_clk_max = amdgpu_dpm_get_mclk(odev, false) / 100;
	} else
		mem_info->mem_clk_max = 100;
}

uint64_t core_get_gpu_clock_counter(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	if (odev->gfx.funcs->get_gpu_clock_counter)
		return odev->gfx.funcs->get_gpu_clock_counter(odev);
	return 0;
}

int core_get_dmabuf_info(struct opu_device *kgd, int dma_buf_fd,
				  struct opu_device **dma_buf_kgd,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags)
{
	struct opu_device *odev = (struct opu_device *)kgd;
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	uint64_t metadata_flags;
	int r = -EINVAL;

	dma_buf = dma_buf_get(dma_buf_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &amdgpu_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		goto out_put;

	obj = dma_buf->priv;
	if (obj->dev->driver != odev_to_drm(odev)->driver)
		/* Can't handle buffers from different drivers */
		goto out_put;

	odev = drm_to_odev(obj->dev);
	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->preferred_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		goto out_put;

	r = 0;
	if (dma_buf_kgd)
		*dma_buf_kgd = (struct opu_device *)odev;
	if (bo_size)
		*bo_size = amdgpu_bo_size(bo);
	if (metadata_buffer)
		r = amdgpu_bo_get_metadata(bo, metadata_buffer, buffer_size,
					   metadata_size, &metadata_flags);
	if (flags) {
		*flags = (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
				core_IOC_ALLOC_MEM_FLAGS_VRAM
				: core_IOC_ALLOC_MEM_FLAGS_GTT;

		if (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			*flags |= core_IOC_ALLOC_MEM_FLAGS_PUBLIC;
	}

out_put:
	dma_buf_put(dma_buf);
	return r;
}

uint64_t core_get_hive_id(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->gmc.xgmi.hive_id;
}

uint64_t core_get_hive_id(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->gmc.xgmi.hive_id;
}

uint8_t core_get_xgmi_hops_count(struct opu_device *dst, struct opu_device *src)
{
	struct opu_device *peer_odev = (struct opu_device *)src;
	struct opu_device *odev = (struct opu_device *)dst;
	int ret = amdgpu_xgmi_get_hops_count(odev, peer_odev);

	if (ret < 0) {
		DRM_ERROR("amdgpu: failed to get  xgmi hops count between node %d and %d. ret = %d\n",
			odev->gmc.xgmi.physical_node_id,
			peer_odev->gmc.xgmi.physical_node_id, ret);
		ret = 0;
	}
	return  (uint8_t)ret;
}

uint64_t core_get_mmio_remap_phys_addr(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->rmmio_remap.bus_addr;
}

uint32_t core_get_asic_rev_id(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->rev_id;
}

int core_get_noretry(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->gmc.noretry;
}

int core_submit_ib(struct opu_device *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len)
{
	struct opu_device *odev = (struct opu_device *)kgd;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct amdgpu_ring *ring;
	struct dma_fence *f = NULL;
	int ret;

	switch (engine) {
	case KGD_ENGINE_MEC1:
		ring = &odev->gfx.compute_ring[0];
		break;
	case KGD_ENGINE_SDMA1:
		ring = &odev->sdma.instance[0].ring;
		break;
	case KGD_ENGINE_SDMA2:
		ring = &odev->sdma.instance[1].ring;
		break;
	default:
		pr_err("Invalid engine in IB submission: %d\n", engine);
		ret = -EINVAL;
		goto err;
	}

	ret = amdgpu_job_alloc(odev, 1, &job, NULL);
	if (ret)
		goto err;

	ib = &job->ibs[0];
	memset(ib, 0, sizeof(struct amdgpu_ib));

	ib->gpu_addr = gpu_addr;
	ib->ptr = ib_cmd;
	ib->length_dw = ib_len;
	/* This works for NO_HWS. TODO: need to handle without knowing VMID */
	job->vmid = vmid;

	ret = amdgpu_ib_schedule(ring, 1, ib, job, &f);

	if (ret) {
		DRM_ERROR("amdgpu: failed to schedule IB.\n");
		goto err_ib_sched;
	}

	ret = dma_fence_wait(f, false);

err_ib_sched:
	dma_fence_put(f);
	amdgpu_job_free(job);
err:
	return ret;
}

void core_set_compute_idle(struct opu_device *kgd, bool idle)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	amdgpu_dpm_switch_power_profile(odev,
					PP_SMC_POWER_PROFILE_COMPUTE,
					!idle);
}

bool core_is_core_vmid(struct opu_device *odev, u32 vmid)
{
	if (odev->kfd.dev)
		return vmid >= odev->vm_manager.first_core_vmid;

	return false;
}

int core_flush_gpu_tlb_vmid(struct opu_device *kgd, uint16_t vmid)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	if (odev->family == AMDGPU_FAMILY_AI) {
		int i;

		for (i = 0; i < odev->num_vmhubs; i++)
			amdgpu_gmc_flush_gpu_tlb(odev, vmid, i, 0);
	} else {
		amdgpu_gmc_flush_gpu_tlb(odev, vmid, AMDGPU_GFXHUB_0, 0);
	}

	return 0;
}

int core_flush_gpu_tlb_pasid(struct opu_device *kgd, uint16_t pasid,
				      enum TLB_FLUSH_TYPE flush_type)
{
	struct opu_device *odev = (struct opu_device *)kgd;
	bool all_hub = false;

	if (odev->family == AMDGPU_FAMILY_AI)
		all_hub = true;

	return amdgpu_gmc_flush_gpu_tlb_pasid(odev, pasid, flush_type, all_hub);
}

bool core_have_atomics_support(struct opu_device *kgd)
{
	struct opu_device *odev = (struct opu_device *)kgd;

	return odev->have_atomics_support;
}
