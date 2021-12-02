/* opu_deps.h */

#ifndef __OPU_DEPS_H__
#define __OPU_DEPS_H__

#include <linux/hashtable.h>

struct dma_fence;
struct opu_device;
struct opu_ring_mgr;

/* Container for fences used to sync command submissions */
struct opu_deps {
    DECLARE_HASHTABLE(fences, 4);
};

void opu_deps_create(struct opu_deps *sync);
int opu_deps_add_fence(struct opu_deps *deps, struct dma_fence *f, bool explicit);
struct dma_fence *opu_deps_peek_fence(struct opu_deps *deps);
struct dma_fence *opu_deps_get_fence(struct opu_deps *deps, bool *explicit);
struct dma_fence *opu_deps_to_array_fence(struct opu_deps *source,
             int max_fence_count, uint64_t fence_context, uint32_t seqno);
int opu_deps_clone(struct opu_deps *source, struct opu_deps *clone);
int opu_deps_wait(struct opu_deps *deps, bool intr);
void opu_deps_free(struct opu_deps *deps);
int opu_deps_init(void);
void opu_deps_fini(void);

#endif
