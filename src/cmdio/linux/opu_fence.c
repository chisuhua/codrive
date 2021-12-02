#include <linux/dma-fence-array.h>
#include "opu_priv.h"

#define OPU_FENCE_JIFFIES_TIMEOUT   (HZ)
#define OPU_WAIT_EMPTY_TIMEOUT_MS   2000

static struct kmem_cache *opu_fence_slab;

int opu_fence_slab_init(void)
{
    opu_fence_slab = kmem_cache_create(
            "opu_fence", sizeof(struct opu_fence), 0, SLAB_HWCACHE_ALIGN, NULL);
    if (!opu_fence_slab)
        return -ENOMEM;
    return 0;
}

void opu_fence_slab_fini(void)
{
    rcu_barrier();
    kmem_cache_destroy(opu_fence_slab);
}

static void opu_fence_write(struct opu_hw_fence_mgr *hw_fence_mgr, u32 seq)
{
    *hw_fence_mgr->cpu_addr = cpu_to_le32(seq);
}

static u32 opu_fence_read(struct opu_hw_fence_mgr *hw_fence_mgr)
{
    return le32_to_cpu(*hw_fence_mgr->cpu_addr);
}

int opu_fence_emit(struct opu_ring_mgr *ring, struct opu_job *job,
        struct dma_fence **f,  unsigned flags)
{
    struct opu_device *odev = odev_from
}
