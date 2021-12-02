#include <linux/sched.h>
#include <linux/device.h>
#include "opu_priv.h"
// #include "amdgpu_amdopu.h"
//

uint32_t    global_opuid = 0;
struct  opu_va_mgr *g_opu_va_mgr = NULL;

static void opu_init_setting(void)
{
    opu_setting_page_order = opu_setting_page_order != 0xffffffff ?
        min(opu_setting_page_order, 10u)  : opu_setting_page_order;

    opu_setting_page_scheme = min(opu_setting_page_scheme, OPU_PAGE_TABLE_SCHEME_STATIC);
}

static int __init opu_init(void)
{
	int err;

    // adapt driver drm version info
    char ver[] = DRIVER_VERSION;
    char *tmp = ver;
    char *cur = strsep(&tmp, ".");

    opu_drm_driver.major = simple_strtol(cur, NULL, 0);
    cur = strsep(&tmp, ".");
    opu_drm_driver.minor = simple_strtol(cur, NULL, 0);
    cur = strsep(&tmp, ".");
    opu_drm_driver.patchlevel = simple_strtol(cur, NULL, 0);

    opu_init_setting();

	err = opu_chardev_init();
	if (err < 0)
		goto err_ioctl;

	err = opu_topology_init();
	if (err < 0)
		goto err_topology;

    err = opu_deps_init();
	if (err < 0)
		goto err_deps;

    err = opu_fence_slab_init();
	if (err < 0)
		goto err_fence;

	err = opu_process_create_wq();
	if (err < 0)
		goto err_create_wq;

    err = pci_register_driver(&opu_pci_driver);

	opu_procfs_init();

	opu_debugfs_init();

	return 0;

err_register_pci:
    pci_unregister_driver(&opu_pci_driver);
err_create_wq:
	opu_process_destroy_wq();
err_fence:
	opu_fence_slab_fini();
err_deps:
	opu_deps_fini();
err_topology:
	opu_topology_shutdown();
err_chardev:
	opu_chardev_exit();
err_ioctl:
	pr_err("OPU is disabled due to module initialization failure\n");
	return err;
}


static void __exit opu_exit(void)
{
    DRM_INFO("OPU Driver Exit");
    pci_unregister(&opu_pci_driver);
	opu_debugfs_fini();
	opu_fence_slab_fini();
	opu_deps_fini();
	opu_process_destroy_wq();
	opu_procfs_shutdown();
	opu_topology_shutdown();
	opu_chardev_exit();
}

module_init(opu_init);
module_exit(opu_exit);
