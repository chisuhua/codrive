
#include <linux/sched.h>
#include <linux/device.h>
#include "opu_priv.h"
#include "amdgpu_amdopu.h"

static int opu_init(void)
{
	int err;

	/* Verify module parameters */
	if ((sched_policy < OPU_SCHED_POLICY_HWS) ||
		(sched_policy > OPU_SCHED_POLICY_NO_HWS)) {
		pr_err("sched_policy has invalid value\n");
		return -EINVAL;
	}

	/* Verify module parameters */
	if ((max_num_of_queues_per_device < 1) ||
		(max_num_of_queues_per_device >
			OPU_MAX_NUM_OF_QUEUES_PER_DEVICE)) {
		pr_err("max_num_of_queues_per_device must be between 1 to OPU_MAX_NUM_OF_QUEUES_PER_DEVICE\n");
		return -EINVAL;
	}

	err = opu_chardev_init();
	if (err < 0)
		goto err_ioctl;

	err = opu_topology_init();
	if (err < 0)
		goto err_topology;

	err = opu_process_create_wq();
	if (err < 0)
		goto err_create_wq;

	/* Ignore the return value, so that we can continue
	 * to init the OPU, even if procfs isn't craated
	 */
	opu_procfs_init();

	opu_debugfs_init();

	return 0;

err_create_wq:
	opu_topology_shutdown();
err_topology:
	opu_chardev_exit();
err_ioctl:
	pr_err("OPU is disabled due to module initialization failure\n");
	return err;
}

static void opu_exit(void)
{
	opu_debugfs_fini();
	opu_process_destroy_wq();
	opu_procfs_shutdown();
	opu_topology_shutdown();
	opu_chardev_exit();
}

int kgd2opu_init(void)
{
	return opu_init();
}

void kgd2opu_exit(void)
{
	opu_exit();
}
