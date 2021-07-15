#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "opu_priv.h"
#include "cik_regs.h"
#include "opu_pm4_headers.h"
#include "opu_pm4_headers_diq.h"
#include "opu_dbgmgr.h"
#include "opu_dbgdev.h"
#include "opu_device_queue_manager.h"

static DEFINE_MUTEX(opu_dbgmgr_mutex);

struct mutex *opu_get_dbgmgr_mutex(void)
{
	return &opu_dbgmgr_mutex;
}


static void opu_dbgmgr_uninitialize(struct opu_dbgmgr *pmgr)
{
	kfree(pmgr->dbgdev);

	pmgr->dbgdev = NULL;
	pmgr->pasid = 0;
	pmgr->dev = NULL;
}

void opu_dbgmgr_destroy(struct opu_dbgmgr *pmgr)
{
	if (pmgr) {
		opu_dbgmgr_uninitialize(pmgr);
		kfree(pmgr);
	}
}

bool opu_dbgmgr_create(struct opu_dbgmgr **ppmgr, struct opu_dev *pdev)
{
	enum DBGDEV_TYPE type = DBGDEV_TYPE_DIQ;
	struct opu_dbgmgr *new_buff;

	if (WARN_ON(!pdev->init_complete))
		return false;

	new_buff = opu_alloc_struct(new_buff);
	if (!new_buff) {
		pr_err("Failed to allocate dbgmgr instance\n");
		return false;
	}

	new_buff->pasid = 0;
	new_buff->dev = pdev;
	new_buff->dbgdev = opu_alloc_struct(new_buff->dbgdev);
	if (!new_buff->dbgdev) {
		pr_err("Failed to allocate dbgdev instance\n");
		kfree(new_buff);
		return false;
	}

	/* get actual type of DBGDevice cpsch or not */
	if (pdev->dqm->sched_policy == OPU_SCHED_POLICY_NO_HWS)
		type = DBGDEV_TYPE_NODIQ;

	opu_dbgdev_init(new_buff->dbgdev, pdev, type);
	*ppmgr = new_buff;

	return true;
}

long opu_dbgmgr_register(struct opu_dbgmgr *pmgr, struct opu_process *p)
{
	if (pmgr->pasid != 0) {
		pr_debug("H/W debugger is already active using pasid 0x%x\n",
				pmgr->pasid);
		return -EBUSY;
	}

	/* remember pasid */
	pmgr->pasid = p->pasid;

	/* provide the pqm for diq generation */
	pmgr->dbgdev->pqm = &p->pqm;

	/* activate the actual registering */
	pmgr->dbgdev->dbgdev_register(pmgr->dbgdev);

	return 0;
}

long opu_dbgmgr_unregister(struct opu_dbgmgr *pmgr, struct opu_process *p)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != p->pasid) {
		pr_debug("H/W debugger is not registered by calling pasid 0x%x\n",
				p->pasid);
		return -EINVAL;
	}

	pmgr->dbgdev->dbgdev_unregister(pmgr->dbgdev);

	pmgr->pasid = 0;

	return 0;
}

long opu_dbgmgr_wave_control(struct opu_dbgmgr *pmgr,
				struct dbg_wave_control_info *wac_info)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != wac_info->process->pasid) {
		pr_debug("H/W debugger support was not registered for requester pasid 0x%x\n",
				wac_info->process->pasid);
		return -EINVAL;
	}

	return (long) pmgr->dbgdev->dbgdev_wave_control(pmgr->dbgdev, wac_info);
}

long opu_dbgmgr_address_watch(struct opu_dbgmgr *pmgr,
				struct dbg_address_watch_info *adw_info)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != adw_info->process->pasid) {
		pr_debug("H/W debugger support was not registered for requester pasid 0x%x\n",
				adw_info->process->pasid);
		return -EINVAL;
	}

	return (long) pmgr->dbgdev->dbgdev_address_watch(pmgr->dbgdev,
							adw_info);
}

