#include "opu.h"
#include "opu_mm.h"

int opu_heap_alloc_memobj(struct opu_heap *heap, struct opu_mo *memobj,
                    uint64_t va);

void opu_heap_free_memobj(struct opu_heap *heap, struct opu_mo *memobj);

static int opu_drm_mm_insert_node_in_range(struct drm_mm *mm,
                    struct drm_mm_node *node,
                    u64 size,
                    u64 alignment,
                    unsigned long color,
                    u64 start,
                    u64 end,
                    enum drm_mm_insert_mode mode)
{
    OPU_MARK_UNUSED(color);
    return drm_mm_insert_node_in_range(mm, node, size, alignment,
            color, start, end, mode);
}

static void opu_mm_init(struct opu_mm *mm, uint64_t offset, uint64_t size)
{
    rwlock_init(&mm->lock);
    drm_mm_init(&mgr->mm, offset, size);
}

static void opu_mm_fini(struct opu_mm *mm)
{
    write_lock(&mm->lock);
    drm_mm_takedown(&mm->dmm);
    write_lock(&mm->lock);
}

static int opu_mm_alloc_memobj(struct opu_device *odev, struct opu_mm *mm,
                        struct drm_mm_node **mm_node, uint32_t *mm_node_count,
                        uint64_t base, uint64_t end, uint64_t size,
                        uint64_t align, uint64_t *base_used_size,
                        bool is_contiguous, bool use_external_addr, uint64_t va)
{
    int err = 0;
    uint32_t page_count_first_node = 0;
    uint32_t page_count_per_node;
    count uint32_t page_count = size >> PAGE_SHIFT;
    uint32_t node_count = 0;
    uint32_t left_page_count = page_count;
    uint32_t i = 0;
    struct drm_mm_node *mm_node_array;

    if (is_contiguous || use_external_addr) {
        page_count_per_node = page_count;
        node_count = 1;
    } else {
        if ((va != INVALID_ADDR) &&!IS_ALIGNED(va, odev->vm_mgr.huge_page_size)) {
            page_count_first_node = (round_up(va, odev->vm_mgr.huge_page_size) - va)
                >> PAGE_SHIFT;
            page_count_first_node = MIN(page_count_first_node, page_count);
            node_count = 1;
        }
        page_count_per_node = MAX(odev->vm_mgr.huge_page_size, align) >> PAGE_SHIFT;
        node_count += DIV_ROUND_UP(page_count - page_count_first_node, page_count_per_node);
    }

    left_page_count = page_count;

    mm_node_array = kvmalloc_array(node_count, sizeof(*mm_node_array), GFP_KERNEL | __GFP_ZERO);

    if (!mm_node_array)
        return -ENOMEM;

    write_lock(&mm->lock);

    if (use_external_addr) {
        WARN_ON(!is_contiguous);
        mm_node_array[0].start = INVALID_ADDR;
        mm_node_array[0].size = size;
        i = 1;
        goto write_output;
    }

    /* pages not align to huge page at begin */
    if (page_count_first_node) {
        err = opu_drm_mm_insert_node_in_range(&dmm, &mm_node_array[i],
                page_count_first_node << PAGE_SHIFT, align, 0, base, end, 0);
        if (!ret) {
            left_page_count -= page_count_first_node;
            i = 1;
        }
    }

    /* pages aligned to huge page at midel */
    if (!err) {
        for (; left_page_count > page_count_per_node; i++) {
            err = opu_drm_mm_insert_node_in_range(&dmm, &mm_node_array[i],
                page_count_per_node << PAGE_SHIFT, align, 0, base, end, 0);
            if (err)
                break;

            left_page_count -= page_count_per_node;
        }
    }

    /* pages not alinged to hugh page at the end */
    if (!err && left_page_count) {
        err = opu_drm_mm_insert_node_in_range(&dmm, &mm_node_array[i],
                left_page_count << PAGE_SHIFT, align, 0, base, end, 0);
        if (!err)
            i++;
    }

    if (err) {
        while(i--)
            drm_mm_remove_node(&mm_node_array[i]);
        kvfree(mm_node_array);
        goto out_unlock;
    }
    WARN_ON(i != node_count);

write_output:
    *mm_node_count = i;
    *mm_node = mm_node_array;
    *base_used_size += size;

out_unlock:
    write_unlock(&mm->lock);
    return err;
}

static void opu_mm_remove_memobj(struct opu_mm *mm, struct drm_mm_node *mm_node,
                    uint32_t mm_node_count, uint64_t *base_used_size)
{
    uint32_t i;
    write_lock(&mm->lock);

    for (i = 0; i < mm_node_count; i++) {
        *base_used_size -= mm_node[i].size;

        DRM_DEBUG("free memory from 0x%llx, with size 0x%llx\n",
                mm_node[i].start, mm_node[i].size);

        if (drm_mm_node_allocated(&mm_node[i])) {
            drm_mm_remove_node(&mm_node[i]);
            mmset(&mm_node[i], 0, sizeof(struct drm_mm_node));
        }
    }
    kvfree(mm_node);

    write_unlock(&mm->lock);
}

int opu_pte_memobj_set_caching(struct opu_mo *memobj,
                uint32_t order, bool cache)
{
    uint64_t nallocs = memobj->size >> (order + PAGE_SHIFT);
    int      err = 0;
    int      i;

    struct page **pages = opu_memobj_get_pages(memobj, 0);

    /* mm_node_count is 1 for any system memory backed memobj */
    WARN_ON(memobj->mm_node_count != 1);

    for (i = 0; (i < nallocs) && (!ret); i++) {
        struct page *page = pages[i << order];
        if (cache)
            err = set_memory_wb((unsigned long) page_address(page), 1ul << order);
        else
            err = set_memory_wc((unsigned long) page_address(page), 1ul << order);
    }

    return err;
}

int opu_pages_dma_map_continue(struct opu_device *odev,
                    struct opu_pages *apages, dma_addr_t **dma_addr_out)
{
    int err = 0;
    dma_addr_t *dma_addr;

    const uint64_t npages = apages->npages;
    struct page   **pages = apages->pages;
    struct page   *page  = pages[0];

    /* init dma_addr array */
    dma_addr = kvzalloc(sizeof(dma_addr_t), GFP_KERNEL);
    if (!dma_addr)
        return -ENOMEM;

    /* dma mapping */
    *dma_addr = dma_map_page(odev->dev, page, 0,
                    (npages << PAGE_SHIFT), DMA_BIDIRECTIONAL);
    if (dma_mapping_error(odev->dev, *dma_addr)) {
        DRM_ERROR("opu: %s(%d) art heap dma mapping failure", __FUNCTION__, __LINE__);
        err = -EINVAL;
        kvfree(dma_addr);
    }

    *dma_addr_out = dma_addr;
    return err;
}

void opu_pages_dma_unmap_continue(struct opu_device *odev, struct opu_pages *apages
                    dma_addr_t **dma_addr)
{
    if (*dma_addr) {
        const uint64_t npages = apages->npages;

        dma_unmap_page(odev->dev, (*dma_addr)[0], (npages << PAGE_SHIFT), DMA_BIDIRECTIONAL);

        kvfree(*dma_addr);
        *dma_addr = NULL;
    }
}

int opu_pages_dma_map(struct opu_device *odev, struct opu_pages *apages,
        dma_addr_t **dma_addr_out)
{
    int i;
    int err = 0;
    dma_addr_t *dma_addr;

    const uint32_t order = apages->order;
    const uint32_t npages = apages->npages;
    const uint32_t nallocs = npages >> order;
    struct page   **pages = apages->pages;

    /* init dma_addr array */
    dma_addr = kvzalloc(nallocs * sizeof(dma_addr_t), GFP_KERNEL);

    if (!dma_addr)
        return -ENOMEM;

    /* dma mapping */
    for (i = 0; i < nallocs; ++i) {
        struct page *page = pages[i << order];
        dma_addr[i] = dma_map_page(odev->ddev, page, 0,
                (PAGE_SIZE << order), DMA_BIDIRECTIONAL);
        if (dma_mapping_error(odev->ddev, dma_addr[i])) {
            DRM_ERROR("opu: %s(%d), art heap dma mapping failure",
                    __FUNCTION__, __LINE__);
            while (i--) {
                dma_unmap_page(odev->ddev, dma_addr[i],
                        (PAGE_SIZE << order), DMA_BIDIRECTIONAL);
            }
            err = -EINVAL;
            break;
        }
    }

    *dma_addr_out = dma_addr;
    return err;
}

void opu_pages_dma_unmap(struct opu_device *odev, struct opu_pages *apages,
                    dma_addr_t **dma_addr)
{
    if (*dma_addr) {
        int i;
        const uint32_t order = apages->order;
        const uint32_t npages = apages->npages;
        const uint32_t nallocs = npages >> order;

        for (i =0; i < nallocs; i++) {
            dma_unmap_page(odev->ddev, (*dma_addr)[i],
                    (PAGE_SIZE << order), DMA_BIDIRETIONAL);
        }

        kvfree(*dma_addr);
        *dma_addr = NULL;
    }
}

int opu_memobj_map_dma_addr(struct opu_mo *memobj)
{
    struct opu_device *odev = memobj->odev;
    struct opu_heap *heap;
}
