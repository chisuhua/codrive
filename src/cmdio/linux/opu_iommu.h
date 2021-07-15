
#ifndef __OPU_IOMMU_H__
#define __OPU_IOMMU_H__

#include <linux/kconfig.h>

#if IS_REACHABLE(CONFIG_AMD_IOMMU_V2)

#define OPU_SUPPORT_IOMMU_V2

int opu_iommu_check_device(struct opu_dev *opu);
int opu_iommu_device_init(struct opu_dev *opu);

int opu_iommu_bind_process_to_device(struct opu_process_device *pdd);
void opu_iommu_unbind_process(struct opu_process *p);

void opu_iommu_suspend(struct opu_dev *opu);
int opu_iommu_resume(struct opu_dev *opu);

int opu_iommu_add_perf_counters(struct opu_topology_device *kdev);

#else

static inline int opu_iommu_check_device(struct opu_dev *opu)
{
	return -ENODEV;
}
static inline int opu_iommu_device_init(struct opu_dev *opu)
{
#if IS_MODULE(CONFIG_AMD_IOMMU_V2)
	WARN_ONCE(1, "iommu_v2 module is not usable by built-in OPU");
#endif
	return 0;
}

static inline int opu_iommu_bind_process_to_device(
	struct opu_process_device *pdd)
{
	return 0;
}
static inline void opu_iommu_unbind_process(struct opu_process *p)
{
	/* empty */
}

static inline void opu_iommu_suspend(struct opu_dev *opu)
{
	/* empty */
}
static inline int opu_iommu_resume(struct opu_dev *opu)
{
	return 0;
}

static inline int opu_iommu_add_perf_counters(struct opu_topology_device *kdev)
{
	return 0;
}

#endif /* IS_REACHABLE(CONFIG_AMD_IOMMU_V2) */

#endif /* __OPU_IOMMU_H__ */
