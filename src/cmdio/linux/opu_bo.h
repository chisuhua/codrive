#ifndef __OPU_BO_H__
#define __OPU_BO_H__

struct opu_unified_context;

/* page */
struct opu_pages {
    struct page **pages;
    uint32_t    order;
    uint64_t    npages;
};

struct opu_mo {
    struct kref     refcount;
    uint32_t        heap;
    struct {
        uint32_t    is_cpu_cached : 1;
        uint32_t    is_contiguous : 1; // only one mm_node
        uint32_t    is_valid    : 1;       // mm_node is allocated and mapped
        uint32_t    is_dev_mem  : 1;
        uint32_t    is_pcie_bar_visiable : 1;
        uint32_t    is_imported : 1;
        uint32_t    reserved    : 26;
    }; /*flags */
    uint32_t        align;
    uint64_t        size;
    struct opu_pages  pages;
    dma_addr_t      *dma_addr;
    void            *cpu_addr; /* kernel space addr */
    struct  drm_mm_node     *mm_node_array;
    uint32_t        mm_node_count;
    struct opu_device *odev;

    struct opu_fence  *fence;
    struct opu_bo     *obo;
};

/* buffer object */
struct opu_bo {
    struct drm_gem_object gem;
    struct opu_mo     *memobj;
    uint32_t    heap;
    struct {
        uint32_t    is_mirror   : 1;    /* mapped and shared beteeen all devices */
        uint32_t    is_unified  : 1;    /* unified mem bo*/
        uint32_t    is_imported : 1;    /* imported mem bo*/
        uint32_t    is_physical : 1;    /* physical mem bo*/
        uint32_t    reserved    : 28;
    }; /* flags */
    uint64_t        size;
    uint64_t        align;
    atomic_t        in_active;

    /* contain UMDva mapping of this bo in all VMs */
    struct mutex        lock;
    struct list_head    bo_va;
    struct opu_unified_context    *unified_ctx;
    struct opu_device   *odev;
    struct opu_ipc_obj  *ipc_obj;
};

#define gem_to_obo(gobj) container_of((gobj), struct opu_bo, gem)

#define OPU_SA_NUM_FENCE_LISTS 32

struct opu_sa_mgr {
    wait_queue_head_t   wq;
    struct opu_mo   *mo;
    struct list_head    *hole;
    struct list_head    flist[OPU_SA_NUM_FENCE_LISTS]
    struct list_head    olist;
    unsigned            size;
    uint64_t            addr;
    void*               *cpu_addr;
    uint32_t            heap;
    uint32_t            align;
};

/* sub-allocation buffer*/
struct opu_sa_bo {
    struct  list_head       olist;
    struct  list_head       flist;
    struct  opu_sa_mgr      *mgr;
    unsigned                soffset;
    unsigned                eoffset;
    struct  dma_fence       *fence;
};

/* sub allocation*/
static inline uint64_t  opu_sa_bo_addr(struct opu_sa_bo *sa_bo)
{
    return sa_bo->mgr->addr + sa_bo->soffset;
}

static inline void *opu_sa_bo_cpu_addr(struct opu_sa_bo *sa_bo)
{
    return (void *)((char*)(sa_bo->mgr->cpu_addr) + sa_bo->soffset);
}

int opu_sa_bo_mgr_init(struct opu_device *odev,
                    struct opu_sa_mgr *sa_mgr,
                    unsigned size, u32 align, u32 domain);

void opu_sa_bo_mgr_fini(struct opu_device *odev,
                    struct opu_sa_mgr *sa_mgr);

int opu_sa_bo_mgr_start(struct opu_device *odev,
                    struct opu_sa_mgr *sa_mgr);

int opu_sa_bo_new(struct opu_sa_mgr *sa_mgr,
            struct opu_sa_bo **sa_bo,
            unsigned size, unsigned align);

void opu_sa_bo_free(struct opu_device *odev,
            struct opu_sa_bo **sa_bo,
            struct dma_fence *fence);

/* bo alloc info */
struct opu_bo_param {
    uint64_t    size;
    uint64_t    byte_align;
    uint64_t    heap;
    uint64_t    flags;
};

/* bo va*/
/* bo virtual addresses in a vm bo*/
struct opu_bo_va_mapping {
    struct rb_node      rb;
    uint64_t            start;
    uint64_t            end;
    uint64_t            __subtree_last;
    uint64_t            offset;
    uint64_t            flags;
    uint64_t            inv_tlb_eng_mask;
};

/* user space allocated BO in av vm */
struct opu_bo_va {
    struct list_head    list;
    struct opu_bo_va_mapping    ba_va_mapping;
    struct opu_bo       *bo;
    struct opu_vm       *vm;

    bool                cleared;
};

int opu_bo_create(struct drm_device *ddev,
                struct drm_file *file,
                uint32_t        heap,
                bool            is_mirror,
                uint64_t        size,
                uint32_t        align,
                uint32_t        *handle,
                bool            is_unified,
                bool            is_physical,
                struct          opu_mo *memobj);

void opu_bo_free(struct drm_gem_object *gobj);

int opu_bo_mmap(struct drm_device *dev,
                struct drm_file   *filp,
                uint32_t          handle,
                uint64_t          *offset);

vm_fault_t  opu_bo_vm_fault(struct vm_fault *vmf);

int opu_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
            void *buf, int len, int write);

int opu_bo_resident(struct drm_device *dev,
            struct drm_file     *filp,
            uint32_t    handle,
            uint64_t    *addr,
            uint64_t    input_va,
            uint64_t    flags);


int opu_bo_evict(struct drm_device *dev,
            struct drm_file *filp,
            uint32_t    handle);

int opu_bo_export(struct drm_device *dev,
            struct drm_file *filp,
            struct opu_ipc_export_handle_args *args();

int opu_bo_import(struct drm_device *dev,
            struct drm_file *filp,
            struct opu_ipc_import_handle_args *args();

int opu_bo_gem_mmap(struct file *filp,
            struct vm_area_struct *vma);

int opu_bo_gem_open_object(struct drm_gem_object *gobj, struct drm_file *filp);
int opu_bo_gem_close_object(struct drm_gem_object *gobj, struct drm_file *filp);

int opu_bo_wait_idle(struct drm_device *dev,
            struct drm_file *filp,
            uint32_t    handle,
            uint64_t    timeout_ns);

int opu_bo_create_unified_ctx(struct drm_device *dev, struct drm_file *filp
            uint32_t bo_handle, uint64_t ctx_handle, uint64_t *ctx_handlep);

void opu_bo_destroy_unified_ctx(struct opu_bo *bo);

int opu_bo_do_unified_sync(struct drm_device *dev, struct drm_file *filep, uint32_t bo_handle);

struct opu_mo* opu_mo_get(struct opu_mo *mo);
void opu_mo_put(struct opu_mo *mo);

int opu_bo_get_info(struct drm_device *dev, struct drm_file *filp,
            struct drm_opu_get_info_t *args);

#endif
