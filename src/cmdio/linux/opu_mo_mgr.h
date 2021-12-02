#ifndef __OPU_MM_H__
#define __OPU_MM_H__

#include <linux/types.h>

struct opu_device;

struct opu_mm {
    rwlock_t    lock;
    struct drm_mm dmm;
};

struct opu_va_mgr {
    atomic ref;
    struct opu_mm omm;
};

struct opu_heap_funcs {
    int (*alloc)(struct opu_mo *memobj);
    int (*free)(struct opu_mo *memobj);
    int (*map)(struct opu_mo *memobj);
    int (*unmap)(struct opu_mo *memobj);
    int (*cpu_map)(struct opu_mo *memobj);
    int (*cpu_unmap)(struct opu_mo *memobj);
};

union opu_heap_flag {
    uint32_t flags;
};

struct opu_heap {
    struct opu_device *odev;
    struct opu_mm     mm_allocator;
    uint64_t         addr;      /*virtual*/
    uint64_t         size;      /*total_size*/
    uint64_t         used_size;
    uint32_t         align;      /*minimum align*/
    struct opu_pages    pages;          /* system backed mem*/
    phys_addr_t         addr_cpu_phy;   /* for device mem*/
    struct opu_heap_funcs   *funcs;
};

struct opu_mm_funcs {
    void (*memobj_init) (struct opu_device *odev,
            struct opu_mo *mo, uint32_t heap,
            uint64_t size, uint32_t align, uint32_t flag);
};

struct opu_mm {
    struct opu_heap **heap_mems;
    uint8_t heap_count;
    struct opu_device *odev;
    struct opu_mm_funcs funcs;
};

#define opu_mo_init(odev, mo, heap, size, align, flag) \
    (odev->mm->funcs.memobj_init(odev, mo, heap, size, align, flag))

int opu_mo_cpu_map(struct opu_mo *memobj);

void opu_mo_cpu_unmap(struct opu_mo *memobj);

int opu_pages_dma_map_continue(struct opu_device *odev,
                struct opu_pages *apages, dma_addr_t **dma_addr_out);

void opu_pages_dma_unmap_continue(struct opu_device *odev,
                struct opu_pages *apages, dma_addr_t **dma_addr);

int opu_pages_dma_map(struct opu_device *odev,
                struct opu_pages *apages, dma_addr_t **dma_addr_out);

void opu_pages_dma_unmap(struct opu_device *odev,
                struct opu_pages *apages, dma_addr_t **dma_addr);

int opu_mo_map_dma_addr(struct opu_mo *memobj);
int opu_mo_unmap_dma_addr(struct opu_mo *memobj);

void opu_mo_fini(struct opu_mo *memobj);

int opu_mo_make_resident(struct opu_mo *memobj, uint64_t va);

void opu_mo_evict(struct opu_mo *memobj);

int opu_mo_get_pfn(struct opu_mo *memobj,
                  uint64_t page_offset, uint64_t *pfn);

pgprot_t opu_mo_get_prot(struct opu_mo *memobj, pgprot_t orig_prot);

struct page **opu_mo_get_pages(struct opu_mo *memobj, uint32_t range_idx);

uint64_t opu_mo_get_addr(struct opu_mo *memobj, uint32_t range_idx);

int opu_mo_create_internal(struct opu_device *odev,
                        uint32_t heap,
                        uint64_t size,
                        uint32_t align,
                        uint64_t *addr,
                        void     **cpu_addr,
                        struct opu_mo **amo);

void opu_mo_free_internal(struct opu_mo *mo, void **cpu_addr);

int opu_heap_init(struct opu_device *odev,
                        struct opu_heap *heap_mem,
                        struct opu_heap_funcs *funcs,
                        uint64_t addr, uint64_t size,
                        uint32_t align, bool use_external_addr);

int opu_pte_memobj_set_caching(struct opu_pages *apages, bool is_cached);

void opu_heap_fini(struct opu_heap *heap_mem);

int opu_mm_init(struct opu_mm *mm,  struct opu_device *odev,
                uint32_t internal_heap_count, struct opu_mm_funcs *funcs);

void opu_mm_fini(struct opu_mm *mm);

int opu_va_mgr_create(struct opu_va_mgr **va_mgr);
int opu_va_mgr_destroy(struct opu_va_mgr *va_mgr);

int opu_va_alloc(struct opu_mm *mm, struct drm_mm_node **mm_node, uint64_t size, uint64_t align);
int opu_va_free(struct opu_mm *mm, struct drm_mm_node **mm_node, uint64_t size, uint64_t align);

#endif
