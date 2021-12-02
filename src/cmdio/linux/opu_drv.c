#include <linux/module.h>
#include "opu_priv.h"
#include "asic_id.h"

#define DRIVER_AUTHOR "opu"
#define DRIVER_NAME "opu"
#define DRIVER_DESC "opu"
#define DRIVER_DATE "20210813"

#define DRIVER_VERSION "0.1.1-1"


MODULE_AUTHOR(DRVER_AUTHOR)
MODULE_DESCRIPTION(DRVER_DESC)
MODULE_VERSION(DRVER_VERSION)
MODULE_LICENSE("GPL and addition rights");

/**
 * DOC: vm_size (int)
 * Override the size of the GPU's per client virtual address space in GiB.  The default is -1 (automatic for each asic).
 */
int opu_vm_size = -1;
MODULE_PARM_DESC(vm_size, "VM address space size in gigabytes (default 64GB)");
module_param_named(vm_size, opu_vm_size, int, S_IRUGO);

int opu_setting_vm_use_64kpage = 0;
MODULE_PARM_DESC(vm_use_64kpage, "set vm use 64k page (default 64GB)");
module_param_named(vm_use_64kpage, opu_setting_vm_use_64kpage, int, S_IRUGO);

int opu_setting_vm_use_largepage = 0;
MODULE_PARM_DESC(vm_use_largepage, "set vm use large page)");
module_param_named(vm_use_largepage, opu_setting_vm_use_largepage, int, S_IRUGO);

int opu_setting_vm_art_heap_page_order = 0;
MODULE_PARM_DESC(vm_art_heap_page_order, "set vm use large page");
module_param_named(vm_art_heap_page_order, opu_setting_vm_art_heap_page_order, int, S_IRUGO);



/**
 * DOC: vm_fragment_size (int)
 * Override VM fragment size in bits (4, 5, etc. 4 = 64K, 9 = 2M). The default is -1 (automatic for each asic).
 */
int opu_vm_block_size = -1;
MODULE_PARM_DESC(vm_fragment_size, "VM fragment size in bits (4, 5, etc. 4 = 64K (default), Max 9 = 2M)");
module_param_named(vm_fragment_size, opu_vm_fragment_size, int, S_IRUGO);
/**
 * DOC: vm_fault_stop (int)
 * Stop on VM fault for debugging (0 = never, 1 = print first, 2 = always). The default is 0 (No stop).
 */
int opu_vm_fault_stop;
MODULE_PARM_DESC(vm_fault_stop, "Stop on VM fault (0 = never (default), 1 = print first, 2 = always)");
module_param_named(vm_fault_stop, opu_vm_fault_stop, int, S_IRUGO);

/**
 * DOC: vm_debug (int)
 * Debug VM handling (0 = disabled, 1 = enabled). The default is 0 (Disabled).
 */
int opu_vm_debug;
MODULE_PARM_DESC(vm_debug, "Debug VM handling (0 = disabled (default), 1 = enabled)");
module_param_named(vm_debug, opu_vm_debug, int, 0644);
/**
 * DOC: vm_update_mode (int)
 * Override VM update mode. VM updated by using CPU (0 = never, 1 = Graphics only, 2 = Compute only, 3 = Both). The default
 * is -1 (Only in large BAR(LB) systems Compute VM tables will be updated by CPU, otherwise 0, never).
 */
int opu_vm_update_mode = -1;
MODULE_PARM_DESC(vm_update_mode, "VM update using CPU (0 = never (default except for large BAR(LB)), 1 = Graphics only, 2 = Compute only (default for LB), 3 = Both");
module_param_named(vm_update_mode, opu_vm_update_mode, int, S_IRUGO);

/**
 * DOC: hws_max_conc_proc (int)
 * Maximum number of processes that HWS can schedule concurrently. The maximum is the
 * number of VMIDs assigned to the HWS, which is also the default.
 */
int hws_max_conc_proc = 8;
module_param(hws_max_conc_proc, int, S_IRUGO);
MODULE_PARM_DESC(hws_max_conc_proc,
	"Max # processes HWS can execute concurrently when sched_policy=0 (0 = no concurrency, #VMIDs for KFD = Maximum(default))");


/**
 * DOC: cwsr_enable (int)
 * CWSR(compute wave store and resume) allows the GPU to preempt shader execution in
 * the middle of a compute wave. Default is 1 to enable this feature. Setting 0
 * disables it.
 */
int cwsr_enable = 1;
module_param(cwsr_enable, int, S_IRUGO);
MODULE_PARM_DESC(cwsr_enable, "CWSR enable (0 = Off, 1 = On (Default))");

/**
 * DOC: max_num_of_queues_per_device (int)
 * Maximum number of queues per device. Valid setting is between 1 and 4096. Default
 * is 4096.
 */
int max_num_of_queues_per_device = KFD_MAX_NUM_OF_QUEUES_PER_DEVICE_DEFAULT;
module_param(max_num_of_queues_per_device, int, S_IRUGO);
MODULE_PARM_DESC(max_num_of_queues_per_device,
	"Maximum number of supported queues per device (1 = Minimum, 4096 = default)");
/**
 * DOC: debug_largebar (int)
 * Set debug_largebar as 1 to enable simulating large-bar capability on non-large bar
 * system. This limits the VRAM size reported to ROCm applications to the visible
 * size, usually 256MB.
 * Default value is 0, diabled.
 */
int debug_largebar;
module_param(debug_largebar, int, S_IRUGO);
MODULE_PARM_DESC(debug_largebar,
	"Debug large-bar flag used to simulate large-bar capability on non-large bar machine (0 = disable, 1 = enable)");

/**
 * DOC: ignore_crat (int)
 * Ignore CRAT table during KFD initialization. By default, KFD uses the ACPI CRAT
 * table to get information about AMD APUs. This option can serve as a workaround on
 * systems with a broken CRAT table.
 *
 * Default is auto (according to asic type, iommu_v2, and crat table, to decide
 * whehter use CRAT)
 */
int ignore_crat;
module_param(ignore_crat, int, S_IRUGO);
MODULE_PARM_DESC(ignore_crat,
	"Ignore CRAT table during KFD initialization (0 = auto (default), 1 = ignore CRAT)");
/**
 * DOC: halt_if_hws_hang (int)
 * Halt if HWS hang is detected. Default value, 0, disables the halt on hang.
 * Setting 1 enables halt on hang.
 */
int halt_if_hws_hang;
module_param(halt_if_hws_hang, int, 0644);
MODULE_PARM_DESC(halt_if_hws_hang, "Halt if HWS hang is detected (0 = off (default), 1 = on)");
/**
  * DOC: queue_preemption_timeout_ms (int)
  * queue preemption timeout in ms (1 = Minimum, 9000 = default)
  */
int queue_preemption_timeout_ms = 9000;
module_param(queue_preemption_timeout_ms, int, 0644);
MODULE_PARM_DESC(queue_preemption_timeout_ms, "queue preemption timeout in ms (1 = Minimum, 9000 = default)");

/**
 * DOC: debug_evictions(bool)
 * Enable extra debug messages to help determine the cause of evictions
 */
bool debug_evictions;
module_param(debug_evictions, bool, 0644);
MODULE_PARM_DESC(debug_evictions, "enable eviction debug messages (false = default)");

/**
 * DOC: no_system_mem_limit(bool)
 * Disable system memory limit, to support multiple process shared memory
 */
bool no_system_mem_limit;
module_param(no_system_mem_limit, bool, 0644);
MODULE_PARM_DESC(no_system_mem_limit, "disable system memory limit (false = default)");

/**
 * DOC: no_queue_eviction_on_vm_fault (int)
 * If set, process queues will not be evicted on gpuvm fault. This is to keep the wavefront context for debugging (0 = queue eviction, 1 = no queue eviction). The default is 0 (queue eviction).
 */
int opu_no_queue_eviction_on_vm_fault = 0;
MODULE_PARM_DESC(no_queue_eviction_on_vm_fault, "No queue eviction on VM fault (0 = queue eviction, 1 = no queue eviction)");
module_param_named(no_queue_eviction_on_vm_fault, opu_no_queue_eviction_on_vm_fault, int, S_IRUGO);
#endif

/**
 * DOC: tmz (int)
 * Trusted Memory Zone (TMZ) is a method to protect data being written
 * to or read from memory.
 *
 * The default value: 0 (off).  TODO: change to auto till it is completed.
 */
MODULE_PARM_DESC(tmz, "Enable TMZ feature (-1 = auto (default), 0 = off, 1 = on)");
module_param_named(tmz, opu_tmz, int, S_IRUGO);


static const struct pci_device_id pciidlist[] = {
	{0xbeaf, 0x6780, PCI_ANY_ID, PCI_ANY_ID, 0, 0, OPU_CHIP},
	{0, 0, 0}
};


MODULE_DEVICE_TABLE(pci, pciidlist);
static const struct drm_driver opu_kms_driver;


static const struct file_operations opu_driver_kms_fops = {
	.owner          = THIS_MODULE,
	.open           = drm_open,
	.release        = opu_drm_release,
	.mmap           = opu_drm_bo_gem_mmap,
	.unlocked_ioctl = drm_ioctl,
	.poll           = drm_poll,
	.compat_ioctl   = drm_compat_ioctl,
};

static const struct vm_operations_struct opu_vm_ops = {
    .access         = opu_bo_vm_access,
    .fault          = opu_bo_vm_fault,
    .open           = drm_gem_vm_open,
    .close          = drm_gem_vm_close,
};

const struct drm_ioctl_desc opu_ioctls_kms[] = {
	DRM_IOCTL_DEF_DRV(OPU_GEM_CREATE, opu_gem_create_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_CTX, opu_ctx_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_VM, opu_vm_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_SCHED, opu_sched_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(OPU_BO_LIST, opu_bo_list_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_FENCE_TO_HANDLE, opu_cs_fence_to_handle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	/* KMS */
	DRM_IOCTL_DEF_DRV(OPU_GEM_MMAP, opu_gem_mmap_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_GEM_WAIT_IDLE, opu_gem_wait_idle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_CS, opu_cs_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_INFO, opu_info_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_WAIT_CS, opu_cs_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_WAIT_FENCES, opu_cs_wait_fences_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_GEM_METADATA, opu_gem_metadata_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_GEM_VA, opu_gem_va_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_GEM_OP, opu_gem_op_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(OPU_GEM_USERPTR, opu_gem_userptr_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct drm_driver opu_kms_driver = {
	.driver_features =
	    DRIVER_ATOMIC |
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.open = opu_driver_open_kms,
	.postclose = opu_driver_postclose_kms,
	.ioctls = opu_ioctls_kms,
	.num_ioctls = ARRAY_SIZE(opu_ioctls_kms),
	.irq_handler = opu_irq_handler,
	.gem_vm_ops = opu_vm_ops,
	.fops = &opu_driver_kms_fops,
	.release = &opu_driver_release_kms,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

static int opu_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int ret, retry = 0;

	struct drm_device *ddev;
	struct opu_device *odev;
	unsigned long flags = ent->driver_data;
	bool supports_atomic = false;

    struct pci_bus *bus;
    char   bdf[OPU_BDF_DESC_MAX_LENGTH];

    bus = pdev->bus;
    snprintf(bdf, OPU_BDF_DESC_MAX_LENGTH, "%04x:%02x:%02x.%d",
            pci_domain_nr(bus), bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
    DRM_INFO("Probe opu %s on pde=%p", bdf, pdev);

	odev = devm_drm_dev_alloc(&pdev->dev, &opu_kms_driver, typeof(*odev), ddev);
	if (IS_ERR(odev)) {
        DRM_ERROR("ID (%s): %d, opu drm_dev_alloc failed, error code: %ld", bdf, 9, PTR_ERR(dev));
		return PTR_ERR(odev);
    }

	odev->dev  = &pdev->dev;
	odev->pdev = pdev;
	ddev = &odev->ddev;

	if (!supports_atomic)
		ddev->driver_features &= ~DRIVER_ATOMIC;

	ret = pci_enable_device(pdev);
	if (ret) {
        DRM_ERROR("ID (%s): %d, opu pci_enable_device failed, error code: %ld", bdf, 9, PTR_ERR(dev));
		return ret;
    }

	pci_set_drvdata(pdev, ddev);

	ret = opu_driver_load_kms(odev, ent->driver_data);
	if (ret)
		goto err_pci;

retry_init:
	ret = drm_dev_register(ddev, ent->driver_data);
	if (ret == -EAGAIN && ++retry <= 3) {
		DRM_INFO("retry init %d\n", retry);
		/* Don't request EX mode too frequently which is attacking */
		msleep(5000);
		goto retry_init;
	} else if (ret) {
		goto err_pci;
	}

	ret = opu_debugfs_init(odev);
	if (ret)
		DRM_ERROR("Creating debugfs files failed (%d).\n", ret);

	return 0;

err_pci:
	pci_disable_device(pdev);
	return ret;
}

static void opu_pci_remove(struct pci_dev *pdev)
{
    struct drm_device *dev = pci_get_drvdata(pdev);
    drm_dev_unregister(dev);
    drm_dev_unref(dev);
}

static struct pci_driver opu_pci_driver = {
    .name   = DRIVER_NAME,
    .probe  = opu_pci_probe,
    .remove  = opu_pci_remove,
    .id_table = pciidlist
};



