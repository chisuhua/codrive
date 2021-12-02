#include "opu.h"
#include "opu_mm.h"
#include "include/opu_ioctl.h"

static inline struct drm_gem_object *opu_drm_gem_object_lookup(struct drm_device *dev,
                struct drm_file *filp, uint32_t handle);
{
    return drm_gem_object_lookup(filep, handle);
}

static int opu_bo_add_mapping(struct opu_device *odev, struct opu_bo *obo,
                struct opu_vm *vm, uint64_t va, uint64_t flags)
{
    struct opu_bo_va *bo_va;
    int err;
    uint64_t pte_flags = 0;

    if (!odev->vm_mgr.vm_caps.support_vm_pt)
        return 0;

    mutex_lock(&obo->lock);
    list_for_each_entry(bo_va, &obo->bo_va, list) {
        if (bo_va->vm == vm)
            break;
    }

    mutex_lock(&obo->lock);
    if (&bo_va->list == &obo->bo_va) {
        DRM_ERROR("no bo_va");
        return -ENOENT;
    }

    if (flags & OPU_VM_PAGE_EXECUTABLE)
        pte_flags |= OPU_PTE_EXECUTABLE;
    if (flags & OPU_VM_PAGE_READABLE)
        pte_flags |= OPU_PTE_READABLE;
    if (flags & OPU_VM_PAGE_WRITEABLE)
        pte_flags |= OPU_PTE_WRITEABLE;

    err = opu_vm_bo_map(odev, bo_va, va, 0, obo->size, pte_flags);
    return err;
}

static int opu_bo_remove_mapping(struct opu_device *odev,
                    struct opu_bo *obo, struct opu_vm *vm)
{
    struct opu_bo_va    *bo_va;
    int err;

    if (!odev->vm_mgr.vm_caps.support_vm_pt)
        return 0;

    mutex_lock(&obo->lock);
    list_for_each_entry(bo_va, &obo->bo_va, list) {
        if (bo_va->vm == vm)
            break;
    }

    mutex_unlock(&obo->lock);
    if (&bo_va->list == &obo->bo_va) {
        DRM_ERROR("no bo_va");
        return -ENOENT;
    }

    err = opu_vm_bo_unmap(odev, bo_va);
    return err;
}

int opu_bo_create(struct drm_device *dev,   struct drm_file *filp,
                uint32_t heap,      bool is_mirror,
                uint64_t size,      uint32_t align,
                uint32_t *handlep,  bool   is_unified,
                bool  is_physical,  struct opu_mo *mo)
{
    int32_t err;
    struct opu_device *odev = dev->dev_private;
    struct opu_bo *obo = NULL;

    if (heap > OPU_HEAP_MAX) return -EINVAL;
    if (size == 0) return -EINVAL;
    if (align == 0) return -EINVAL;

    obo = kzalloc(sizeof(*obo), GFP_KERNEL);
    if (!obo)
        return -ENOMEM;

    obo->heap = heap;
    obo->odev = odev;
    obo->is_mirror = is_mirror;
    obo->is_physical = is_physical;
    obo->align = align;
    if (is_unified) {
        size = roundup(size, align);
    }
    if (likely(odev->vm_mgr.vm_caps.support_vm_pt &&
                !odev->vm_mgr.vm_caps.vmid0_only)) {
        err = opu_vm_bo_check_align(odev, obo, align, size);
        if (err) {
            DRM_INFO("Invalid align and size: %x, %llx", align, size);
            goto err_obo_alloc;
        }
        obo->size = size;
    } else {
        obo->size = roundup(size, align);
    }

    atomic_set(&obo->in_active, 0);
    if (mo) {
        obo->memobj = mo;
        obo->is_imported = true;
    } else {
        obo->memobj = kzalloc(sizeof(struct opu_mo), GFP_KERNEL);
        if (!obo->memobj) {
            err = -ENOMEM;
            goto err_mo_alloc;
        }
        obo->memobj->obo = obo;
    }
    err = drm_gem_object_init(dev, &obo->gem, obo->size);
    if (unlikely(err))
        goto err_mo_alloc;

    INIT_LIST_HEAD(&obo->bo_va);
    mutex_init(&obo->lock);

    err = drm_gem_handle_create(filp, &obo->gem, handlep);
    drm_gem_object_unreference_unlocked(&obo->gem);

    if (err)
        goto err_mo_alloc;

    return err;

err_mo_alloc:
    kfree(obo->memobj);
err_obo_alloc:
    kfree(obo);
    return err;
}

int opu_bo_create_unified_ctx(struct drm_device *dev, struct drm_file *filp,
                    uint32_t bo_handle, uint64_t ctx_handle, uint64_t *ctx_handlep)
{
    struct drm_gem_object *gobj = opu_drm_gem_object_lookup(dev, filp, bo_handle);
    struct opu_bo *obo = NULL;
    int err = 0;
    if (gobj == NULL)
        return -ENOENT;

    obo = gem_to_opu_bo(gobj);
    err = opu_unified_ctx_create(obo, ctx_handle);
    if (!err)
        *ctx_handlep = (uint64_t)obo->unified_ctx;

    return err;
}

void opu_bo_destroy_unified_ctx(struct opu_bo *bo)
{
    struct opu_unified_context *ctx = bo->unified_ctx;
    if (!ctx)
        return;

    opu_unified_ctx_destroy(bo);
    bo->unified_ctx = NULL;
}

void opu_bo_free(struct drm_gem_object *gobj)
{
    struct opu_bo *bo = gem_to_opu_bo(gobj);

    opu_ipc_obj_put(&bo->ipc_obj);

    if (bo->is_unified) {
        opu_bo_destroy_unified_ctx(bo);
    } else {
        opu_mo_pu(bo->memobj);
    }
    drm_gem_object_release(gobj);
    mutex_destroy(&bo->lock);
    kfree(bo);
}

int opu_bo_mmap(struct drm_device *dev, struct drm_file *filp,
                    struct handle, uint64_t *offset)
{
    struct drm_gem_object *gobj = opu_drm_gem_object_lookup(dev, filp, handle);
    struct opu_bo *bo;
    int err;

    if (gobj == NULL)
        return -ENOENT;

    bo = gem_to_opu_bo(gobj);
    if (bo->heap == OPU_HEAP_FB_INVIISIABLE)
        return -EINVAL;

    err = drm_gem_create_mmap_offset(gobj);

    if (err == 0)
        *offset = drm_vma_node_offset_addr(&gobj->vma_node);

    drm_gem_object_unreference_unlocked(gobj);
    return err;
}

int opu_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
                void *buf, int len, int write)
{
    struct opu_bo *obo = vma->vm_private_data;
    unsigned long offset = (addr) - vma->vm_start +
        ((vma->vm_pgoff - drm_vma_node_start(&obo->gem.vma_node)) << PAGE_SHIFT);

    uint32_t npages = obo->gem.size >> PAGE_SHIFT;

    void *ptr = NULL;

    if (len < 1 || ((offset + len) >> PAGE_SHIFT) > npages)
        return -EIO;

    switch (obo->heap) {
        case OPU_HEAP_FB_INVISIABLE:
            DRM_INFO("TODO");
            len = -EIO;
            break;
        case OPU_HEAP_CACHED:
        case OPU_HEAP_USWC:
        case OPU_HEAP_FB_VISIABLE:
            opu_mo_cpu_map(obo->memobj);
            ptr = (uint8_t *)obo->memobj->cpu_addr + offset;
            if (write)
                memcpy(ptr, buf, len);
            else
                memcpy(buf, ptr, len);
            opu_mo_cpu_unmap(obo->memobj);
            break;
        default:
            len = -EIO;
            break;
    }
    return len;
}

vm_fault_t opu_bo_vm_fault(struct vm_fault *vmf)
{
    struct vm_area_struct *vma = vmf->vma;
    struct opu_bo *obo = vma->vm_private_data;
    uint32_t npages = obo->gem.size >> PAGE_SHIFT;
    uint32_t handle_pages = 0;
    uint32_t i = 0;

    uint64_t va = vmf->address;
    pgoff_t page_offset = ((vmf->address - vma->vm_start) >> PAGE_SHIFT) +
                vm->vm_pgoff - drm_vma_node_start(&obo->gem.vma_node);

    uint64_t pfn;
    int err;

    WARN_ON((vma->vm_pgoff - drm_vma_node_struct(&obo->gem.vma_node)) != 0);

    if (page_offset > npages)
        return VM_FAULT_SIGBUS;

    if (obo->is_unified){
        // TODO
    } else {
        err = opu_mo_get_pfn(obo->memobj, page_offset, &pfn);
        handle_pages = 1;
    }

    if (err)
        return err;

    for (i = 0; i < handle_pages; i++) {
        err |= vm_insert_mixed(vma, va + i * PAGE_SIZE, __pfn_to_pfn_t(pfn + i, PFN_DEV));
    }

    switch (err) {
        case (0):
        case -EBUSY:
            return VM_FAULT_NOPAGE;
        case -ENOMEM:
            return VM_FAULT_OOM;
        default:
            return VM_FAULT_SIGBUS;
    }
}

struct opu_mo* opu_mo_get(struct opu_mo *mo)
{
    kref_get(&mo->refcount);
    return mo;
}

static void opu_mo_release(struct kref *ref)
{
    struct opu_mo *mo = container_of(ref, struct opu_mo, refcount);

    opu_mo_evict(mo);

    if (mo == mo->obo->memobj)
        mo->obo->memobj = NULL;

    kfree(mo);
}

void opu_mo_put(struct opu_mo *mo)
{
    if (mo)
        kref_put(&mo->refcount, opu_mo_release);
}

int opu_bo_resident(struct drm_device *dev,
                struct drm_file *filp,
                uint32_t    handle,
                uint64_t    *addr,
                uint64_t    input_va,
                uint64_y    flags)
{
    struct opu_device *odev = dev->dev_private;
    struct opu_fpriv  *fpriv = filp->driver_priv;
    struct drm_gem_object *gobj = opu_drm_gem_object_lookup(dev, filp, handle);
    struct opu_bo   *obo;
    int32_t err = 0;

    if (gobj == NULL)
        return -ENOENT;

    obo = gem_to_opu_bo(gobj);
    if (!obo->is_unfied && !obo->is_imported) {
        union opu_heap_flag heap_flag;
        heap_flag.flags = 0;
        heap_flag.is_mirror = obo->is_mirror;
        if (odev->vm_mgr.vm_caps.support_vm_pt &&
                odev->vm_mgr.vm_caps.vmid0_only) {
            heap_flag.is_for_kernel = 1;
        }
        opu_mo_init(odev, obo->memobj, obo->heap, obo->size, obo->align, heap_flag.flags);
        err = opu_mo_make_resident(obo->memobj, input_va);

        if (!err)
            *addr = opu_mo_get_addr(obo->memobj, 0);
        else
            opu_bo_evict(dev, filp, handle);
    } else if (obo->is_imported && !odev->vm_mgr.vm_caps.support_vm_pt) {
        *addr = opu_mo_get_addr(obo->memobj, 0);
    }

    if (!err && (odev->vm_mgr.vm_caps.support_vm_pt && !odev->vm_mgr.vm_caps.vmid0_only)
            &&!obo->is_physical)
        err = opu_bo_add_mapping(odev, obo, &fpriv->vm, input_va, flags);

    if (!err && (odev->vm_mgr.vm_caps.support_vm_pt && !odev->vm_mgr.vm_caps.vmid0_only)
            &&!obo->is_physical) {
        struct opu_bo_va *bo_va;
        mutex_lock(&obo->lock);
        list_for_each_entry(bo_va, &obo->bo_va, list) {
            if (bo_va->vm == &fpriv->vm)
                break;
        }
        mutex_lock(&obo->lock);
        if (&bo_va->list == &obo->bo_va) {
            DRM_ERROR("no bo_va");
            err = -ENOENT;
            opu_bo_evict(dev, filp, handle);
        } else {
            err = opu_vm_bo_update(odev, bo_va, false);
            if (err)
                opu_bo_evict(dev, filp, handle);
        }
    }

    drm_gem_object_unreference_unlocked(gobj);
    return err;
}

int opu_bo_evict(struct drm_device *dev, struct drm_file *filp,
            uint32_t handle)
{
    struct drm_gem_object *gobj = opu_drm_gem_object_lookup(dev, filp, handle);
    struct opu_bo *obo;

    if (gobj == NULL)
        return -ENOENT;

    obo = gem_to_opu_bo(gobj);

    opu_mo_put(obj->memobj);

    drm_gem_object_unreference_unlocked(gobj);
    return 0;
}

int opu_bo_export(struct drm_device *dev, struct drm_file *filp,
                struct opu_ipc_export_handle_args *args)
{
    // TODO
    return 0;
}

int opu_bo_import(struct drm_device *dev, struct drm_file *filp,
                struct opu_ipc_export_handle_args *args)
{
    // TODO
    return 0;
}

int opu_bo_get_info(struct drm_device *dev, struct drm_file *filp,
                struct drm_opu_get_info_1 *args)
{
    // TODO
    return 0;
}

int opu_bo_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int err = drm_gem_mmap(filp, vma);

    if (err == 0) {
        struct opu_bo *obo = vma->vm_private_data;

        vma->vm_flags &= ~VM_PFNMAP;
        vma->vm_flags |= VM_MIXEDMAP;
        vma->vm_page_prot = opu_mo_get_prot(obo->memobj, vm_get_page_prot(vma->vm_flags));
    }

    return ret;
}

int opu_bo_gem_open_object(struct drm_gem_object *gobj, struct drm_file *filp)
{
    struct opu_bo *obo = gem_to_opu_bo(gobj);
    struct opu_fpriv *fpriv = filp->driver_priv;
    struct opu_device *odev = fpriv->odev;

    if (odev->vm_mgr.vm_caps.support_vm_pt && !odev->vm_mgr.vm_caps.vmid0_only && !obo->is_physical)
    {
        struct opu_bo_va *bo_va;

        bo_va = kvzalloc(sizeof(*bo_va), GFP_KERNEL);
        if (!bo_va)
            return -ENOMEM;

        bo_va->bo = obo;
        bo_va->vm - &fpriv->vm;
        bo_va->bo_va_mapping.start = INVALID_ADDR;
        mutex_lock(&obo->lock);
        list_add(&bo_va->list, &obo->bo_va);
        mutex_unlock(&obo->lock);
    }
    return 0;
}

int opu_bo_gem_close_object(struct drm_gem_object *gobj, struct drm_file *filp)
{
    struct opu_bo *obo = gem_to_opu_bo(gobj);
    struct opu_fpriv *fpriv = filp->driver_priv;
    struct opu_device *odev = fpriv->odev;

    if (odev->vm_mgr.vm_caps.support_vm_pt && !odev->vm_mgr.vm_caps.vmid0_only && !obo->is_physical)
    {
        struct opu_bo_va *bo_va, *tmp;
        mutex_lock(&obo->lock);
        list_for_each_entry_safe(bo_va, tmp, &obo->bo_va, list) {
            if (bo_va->vm == &fpriv->vm) {
                if (bo_va->bo_va_mapping.start != INVALID_ADDR) {
                    opu_vm_bo_update(odev, bo_va, true);
                    mutex_unlock(&obo->lock);
                    opu_bo_remove_mapping(odev, obo, bo_va->vm);
                    mutex_lock(&obo->lock);
                }
                list_del(&bo_va->list);
                opu_vm_add_freed_mapping(bo_va->vm, bo_va);
            }
        }
        mutex_unlock(&obo->lock);
    }
}

static int opu_bo_wait_idle_internal(struct opu_device *odev,
                    struct opu_bo *obo,
                    uint64_t timeout_ns,
                    bool interruptible)
{
    int32_t ret = 0;
    unsigned long timeout_expire;

    DEFINE_WAIT(wait);

    if (atomic_read(&obo->in_active) == 0) {
        DRM_DEBUG("bo (%p) is idlke for the firset try", obo);
        return 0;
    }

    if (timeout_ns == 0)
        return -ETIME;

    timeout_expire = jiffies + nsecs_to_jiffies(timeout_ns);

    // TODO
    finish_wait(&odev->fence_event, &wait);

    return err;
}

int opu_bo_wait_idle(struct drm_device *dev, struct drm_file *filp,
                uint32_t    handle, uint64_t timeout_ns)
{
    // TODO struct opu_device *odev = dev->dev_private;
    return 0;

}
