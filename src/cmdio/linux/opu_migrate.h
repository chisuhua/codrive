
#ifndef OPU_MIGRATE_H_
#define OPU_MIGRATE_H_

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)

#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/hmm.h>
#include "opu_priv.h"
#include "opu_svm.h"

enum MIGRATION_COPY_DIR {
	FROM_RAM_TO_VRAM = 0,
	FROM_VRAM_TO_RAM
};

int svm_migrate_to_vram(struct svm_range *prange,  uint32_t best_loc,
			struct mm_struct *mm);
int svm_migrate_vram_to_ram(struct svm_range *prange, struct mm_struct *mm);
unsigned long
svm_migrate_addr_to_pfn(struct amdgpu_device *adev, unsigned long addr);

int svm_migrate_init(struct amdgpu_device *adev);
void svm_migrate_fini(struct amdgpu_device *adev);

#else

static inline int svm_migrate_init(struct amdgpu_device *adev)
{
	return 0;
}
static inline void svm_migrate_fini(struct amdgpu_device *adev)
{
	/* empty */
}

#endif /* IS_ENABLED(CONFIG_HSA_AMD_SVM) */

#endif /* OPU_MIGRATE_H_ */
