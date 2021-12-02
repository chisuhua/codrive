
#include "opu.h"
#include <linux/vga_switcheroo.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
// #include <linux/pm_runtime.h>

void amdgpu_unregister_gpu_instance(struct amdgpu_device *adev)
{
	struct amdgpu_gpu_instance *gpu_instance;
	int i;

	mutex_lock(&mgpu_info.mutex);

	for (i = 0; i < mgpu_info.num_gpu; i++) {
		gpu_instance = &(mgpu_info.gpu_ins[i]);
		if (gpu_instance->adev == adev) {
			mgpu_info.gpu_ins[i] =
				mgpu_info.gpu_ins[mgpu_info.num_gpu - 1];
			mgpu_info.num_gpu--;
			if (adev->flags & AMD_IS_APU)
				mgpu_info.num_apu--;
			else
				mgpu_info.num_dgpu--;
			break;
		}
	}

	mutex_unlock(&mgpu_info.mutex);
}

void amdgpu_register_gpu_instance(struct amdgpu_device *adev)
{
	struct amdgpu_gpu_instance *gpu_instance;

	mutex_lock(&mgpu_info.mutex);

	if (mgpu_info.num_gpu >= MAX_GPU_INSTANCE) {
		DRM_ERROR("Cannot register more gpu instance\n");
		mutex_unlock(&mgpu_info.mutex);
		return;
	}

	gpu_instance = &(mgpu_info.gpu_ins[mgpu_info.num_gpu]);
	gpu_instance->adev = adev;
	gpu_instance->mgpu_fan_enabled = 0;

	mgpu_info.num_gpu++;
	if (adev->flags & AMD_IS_APU)
		mgpu_info.num_apu++;
	else
		mgpu_info.num_dgpu++;

	mutex_unlock(&mgpu_info.mutex);
}


/**
 * amdgpu_driver_unload_kms - Main unload function for KMS.
 *
 * @dev: drm dev pointer
 *
 * This is the main unload function for KMS (all asics).
 * Returns 0 on success.
 */
void amdgpu_driver_unload_kms(struct drm_device *dev)
{
	struct amdgpu_device *adev = drm_to_adev(dev);

	if (adev == NULL)
		return;

	amdgpu_unregister_gpu_instance(adev);

	if (adev->rmmio == NULL)
		return;

	if (amdgpu_acpi_smart_shift_update(dev, AMDGPU_SS_DRV_UNLOAD))
		DRM_WARN("smart shift update failed\n");

	amdgpu_acpi_fini(adev);
	amdgpu_device_fini_hw(adev);
}


/**
 * opu_driver_load_kms - Main load function for KMS.
 *
 * @odev: pointer to struct opu_device
 * @flags: device flags
 *
 * This is the main load function for KMS (all asics).
 * Returns 0 on success, error on failure.
 */
int opu_driver_load_kms(struct opu_device *odev, unsigned long flags)
{
	struct drm_device *dev;
	struct pci_dev *parent;
	int r, acpi_status;

	dev = odev->ddev;

	/* opu_device_init should report only fatal error
	 * like memory allocation failure or iomapping failure,
	 * or memory manager initialization failure, it must
	 * properly initialize the GPU MC controller and permit
	 * VRAM allocation
	 */
	r = opu_device_init(odev, flags);
	if (r) {
		dev_err(dev->dev, "Fatal error during GPU init\n");
		goto out;
	}

	/* Call ACPI methods: require modeset init
	 * but failure is not fatal
	 */

	acpi_status = opu_acpi_init(odev);
	if (acpi_status)
		dev_dbg(dev->dev, "Error during ACPI methods call\n");

	if (opu_acpi_smart_shift_update(dev, opu_SS_DRV_LOAD))
		DRM_WARN("smart shift update failed\n");

out:
	if (r) {
		/* balance pm_runtime_get_sync in opu_driver_unload_kms */
		if (adev->rmmio && adev->runpm)
			pm_runtime_put_noidle(dev->dev);
		opu_driver_unload_kms(dev);
	}

	return r;
}

/**
 * amdgpu_driver_open_kms - drm callback for open
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device open, init vm on cayman+ (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_fpriv *fpriv;
	int r, pasid;

	/* Ensure IB tests are run on ring */
	flush_delayed_work(&adev->delayed_init_work);


	if (amdgpu_ras_intr_triggered()) {
		DRM_ERROR("RAS Intr triggered, device disabled!!");
		return -EHWPOISON;
	}

	file_priv->driver_priv = NULL;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0)
		goto pm_put;

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (unlikely(!fpriv)) {
		r = -ENOMEM;
		goto out_suspend;
	}

	pasid = amdgpu_pasid_alloc(16);
	if (pasid < 0) {
		dev_warn(adev->dev, "No more PASIDs available!");
		pasid = 0;
	}

	r = amdgpu_vm_init(adev, &fpriv->vm, pasid);
	if (r)
		goto error_pasid;

	fpriv->prt_va = amdgpu_vm_bo_add(adev, &fpriv->vm, NULL);
	if (!fpriv->prt_va) {
		r = -ENOMEM;
		goto error_vm;
	}

	if (amdgpu_mcbp || amdgpu_sriov_vf(adev)) {
		uint64_t csa_addr = amdgpu_csa_vaddr(adev) & AMDGPU_GMC_HOLE_MASK;

		r = amdgpu_map_static_csa(adev, &fpriv->vm, adev->virt.csa_obj,
						&fpriv->csa_va, csa_addr, AMDGPU_CSA_SIZE);
		if (r)
			goto error_vm;
	}

	mutex_init(&fpriv->bo_list_lock);
	idr_init(&fpriv->bo_list_handles);

	amdgpu_ctx_mgr_init(&fpriv->ctx_mgr);

	file_priv->driver_priv = fpriv;
	goto out_suspend;

error_vm:
	amdgpu_vm_fini(adev, &fpriv->vm);

error_pasid:
	if (pasid)
		amdgpu_pasid_free(pasid);

	kfree(fpriv);

out_suspend:
	pm_runtime_mark_last_busy(dev->dev);
pm_put:
	pm_runtime_put_autosuspend(dev->dev);

	return r;
}

/**
 * amdgpu_driver_postclose_kms - drm callback for post close
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device post close, tear down vm on cayman+ (all asics).
 */
void amdgpu_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;
	struct amdgpu_bo_list *list;
	struct amdgpu_bo *pd;
	u32 pasid;
	int handle;

	if (!fpriv)
		return;

	pm_runtime_get_sync(dev->dev);

	if (amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_UVD) != NULL)
		amdgpu_uvd_free_handles(adev, file_priv);
	if (amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_VCE) != NULL)
		amdgpu_vce_free_handles(adev, file_priv);

	amdgpu_vm_bo_rmv(adev, fpriv->prt_va);

	if (amdgpu_mcbp || amdgpu_sriov_vf(adev)) {
		/* TODO: how to handle reserve failure */
		BUG_ON(amdgpu_bo_reserve(adev->virt.csa_obj, true));
		amdgpu_vm_bo_rmv(adev, fpriv->csa_va);
		fpriv->csa_va = NULL;
		amdgpu_bo_unreserve(adev->virt.csa_obj);
	}

	pasid = fpriv->vm.pasid;
	pd = amdgpu_bo_ref(fpriv->vm.root.bo);

	amdgpu_ctx_mgr_fini(&fpriv->ctx_mgr);
	amdgpu_vm_fini(adev, &fpriv->vm);

	if (pasid)
		amdgpu_pasid_free_delayed(pd->tbo.base.resv, pasid);
	amdgpu_bo_unref(&pd);

	idr_for_each_entry(&fpriv->bo_list_handles, list, handle)
		amdgpu_bo_list_put(list);

	idr_destroy(&fpriv->bo_list_handles);
	mutex_destroy(&fpriv->bo_list_lock);

	kfree(fpriv);
	file_priv->driver_priv = NULL;

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
}


void amdgpu_driver_release_kms(struct drm_device *dev)
{
	struct amdgpu_device *adev = drm_to_adev(dev);

	amdgpu_device_fini_sw(adev);
	pci_set_drvdata(adev->pdev, NULL);
}


