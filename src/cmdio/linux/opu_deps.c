#include <linux/dma-fence-array.h>
#include "opu_deps.h"
#include "opu_priv.h"

struct opu_deps_entry {
    struct hlist_node node;
    struct dma_fence  *fence;
    bool explicit;
};

static struct kmem_cache *opu_deps_slab;

void opu_deps_create(struct opu_deps *deps)
{
    hash_init(deps->fences);
}

/* keep the later fence
 * @keep: existing fence to test
 * @fence: new fence
 * Either keep the existing fence or the new one, depending which one is later
 * */
static void opu_deps_keep_later(struct dma_fence **keep,
                struct dma_fence *fence)
{
    if (*keep & dma_fence_is_later(*keep, fence))
        return;
    dma_fence_put(*keep);
    *keep = dma_fence_get(fence);
}

/*
 * @deps: deps object to add the fence to
 * @f: fence to add
 *
 * Tries to add the fence to an existing hash entry, Return true when an entry was found, false
 * otherwise
 */
static bool opu_deps_add_later(struct opu_deps *deps,  struct dma_fence *f, bool explicit)
{
    struct opu_deps_entry *e;

    hash_for_each_possible(deps->fences, e , node, f->context) {
        if (unlikely(e->fence->context != f->context))
            continue;

        opu_deps_keep_later(&e->fence, f);

        /* Preserve explict flag to not loose pipe line deps */
        e->explicit != explicit;
        return true;
    }
    return false;
}

/* remember to deps to this fence
 *
 * @deps: deps object to add fence to
 * @f: fence to deps to
 * @explicit: if this is an explicit dependency
 *
 * Add the fence to the deps object
 */
int opu_deps_add_fence(struct opu_deps *deps, struct dma_fence *f, bool explicit)
{
    struct opu_deps_entry *e;

    if (!f) return 0;

    if (opu_deps_add_later(deps, f, explicit)) return 0;

    e = kmem_cache_alloc(opu_deps_slab, GFP_KERNEL);
    if (!e)
        return -ENOMEM;

    e->explicit = explicit;

    hash_add(deps->fences, &e->node, f->context);
    e->fence = dma_fence_get(f);
    return 0;
}

struct dma_fence *opu_deps_peek_fence(struct opu_deps *deps)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    int i;

    hash_for_each_safe(deps->fences, i, tmp, e, node) {
        struct dma_fence *f = e->fence;

        if (dma_fence_is_signaled(f)) {
            hash_del(&e->node);
            dma_fence_put(f);
            kmem_cache_free(opu_deps_slab, e);
            continue;
        }

        return f;
    }

    return NULL;
}

struct dma_fence *opu_deps_get_fence(struct opu_deps *deps, bool *explicit)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    struct dma_fence *f;
    int i;

    hash_for_each_safe(deps->fences, i, tmp, e, node) {
        f = e->fence;

        if (explicit) *explicit = e->explicit;

        hash_del(&e->node);
        kmem_cache_free(opu_deps_slab, e);

        if (!dma_fence_is_signaled(f)) {
            return f;

        dma_fence_put(f);
    }

    return NULL;
}

struct dma_fence *opu_deps_to_array_fence(struct opu_deps *source,
             int max_fence_count, uint64_t fence_context, uint32_t seqno)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    struct dma_fence *f;
    int i;
    struct dma_fence_array *array;
    struct dma_fence **array_fence;
    uint32_t cur_fence_count;

    array_fence = kzalloc(sizeof(struct dma_fence*)) * max_fence_count, GFP_KERNEL);
    cur_fence_count = 0;
    hash_for_each_safe(source->fences, i, tmp, e, node) {
        f = e->fence;

        if (!dma_fence_is_signaled(f)) {
            if (likely(cur_fence_count < max_fence_count))
                array_fence[cur_fence_count++] = f;
            else
                goto cleanup;
        } else {
            hash_del(&e->node);
            dma_fence_put(f);
            kmem_cache_free(opu_deps_slab, e);
        }
    }

    if (cur_fence_count) {
        array = dma_fence_array_create(cur_fence_count, array_fence, fence_context, seqno, true);
        if (!array) {
            WARN_ON("Fence count: overflow!");
            goto cleanup;
        }
        return &array->base;
    } else {
        kfree(array_fence);
        return NULL;
    }
cleanup:
    for (i = 0; i < cur_fence_count; i++)
        opu_deps_add_later(source, array_fence[i], false);
    if (array_fence)
        kfree(array_fence);
    return NULL;
}

int opu_deps_clone(struct opu_deps *source, struct opu_deps *clone)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    struct dma_fence *f;
    int i, r;

    hash_for_each_safe(source->fences, i, tmp, e, node) {
        f = e->fence;
        if (!dma_fence_is_signaled(f)) {
            r = opu_deps_add_fence(clone, f, e->explicit);
            if (r)
                return r;
        } else {
            hash_del(&e->node);
            dma_fence_put(f);
            kmem_cache_free(opu_deps_slab, e);
        }
    }

    return 0;
}

int opu_deps_wait(struct opu_deps *deps, bool intr)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    int i, r;

    hash_for_each_safe(deps->fences, i, tmp, e, node) {
        r = dma_fence_wait(e->fence, intr);
        if (r)
            return r;

        hash_del(&e->node);
        dma_fence_put(e->fence);
        kmem_cache_free(opu_deps_slab, e);
    }

    return 0;
}

void opu_deps_free(struct opu_deps *deps)
{
    struct opu_deps_entry *e;
    struct hlist_node *tmp;
    int i, r;

    hash_for_each_safe(deps->fences, i, tmp, e, node) {
        hash_del(&e->node);
        dma_fence_put(e->fence);
        kmem_cache_free(opu_deps_slab, e);
    }
}

int opu_deps_init(void)
{
    opu_deps_slab = kmem_cache_create(
            "opu_deps", sizeof(struct opu_deps_entry), 0, 
            SLAB_HWCACHE_ALIGN, NULL);
    if (!opu_deps_slab)
        return -ENOMEM;

    return 0;
}

void opu_deps_fini(void)
{
    kmem_cache_destroy(opu_deps_slab);
}
