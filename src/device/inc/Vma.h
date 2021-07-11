#pragma once
#include <stdint.h>
#include "inc/MemObj.h"
#include "util/locks.h"
#include "util/debug.h"
#include "inc/Device.h"

typedef enum {
	FMM_FIRST_APERTURE_TYPE = 0,
	FMM_GPUVM = FMM_FIRST_APERTURE_TYPE,
	FMM_LDS,
	FMM_SCRATCH,
	FMM_SVM,
	FMM_MMIO,
	FMM_LAST_APERTURE_TYPE
} aperture_type_e;

typedef struct {
	aperture_type_e app_type;
	uint64_t size;
	void *start_address;
} aperture_properties_t;

/* IPC structures and helper functions */
typedef enum _HSA_APERTURE {
	HSA_APERTURE_UNSUPPORTED = 0,
	HSA_APERTURE_DGPU,
	HSA_APERTURE_DGPU_ALT,
	HSA_APERTURE_GPUVM,
	HSA_APERTURE_CPUVM
} HSA_APERTURE;

typedef struct _HsaApertureInfo {
	HSA_APERTURE	type;		// Aperture type
	uint32_t	idx;		// Aperture index
} HsaApertureInfo;

typedef struct _HsaSharedMemoryStruct {
	uint32_t	ShareHandle[4];
	HsaApertureInfo	ApeInfo;
	uint32_t	SizeInPages;
	uint32_t	ExportGpuId;
} HsaSharedMemoryStruct;


class GpuMemory;
class Vma {
public:
    Vma(Device* device, void *base, void* limit)
        : device_(device)
        , base_(base)
        , limit_(limit)
        , align_(0)
        , guard_pages_(1)
        , allocator_(nullptr)
        , is_cpu_accessible_(false)
        {
            PAGE_SIZE = device->PAGE_SIZE;
            PAGE_SHIFT = device->PAGE_SHIFT;
        };
    virtual ~Vma() {};

    std::shared_ptr<GpuMemory> get_gpu_mem(uint32_t gpu_mem_id);

    Debug* get_debugger() {
        device_->get_debug();
    }

    Device* device_;
	void *base_;
	void *limit_;
	uint64_t align_;
	uint32_t guard_pages_;
    Allocator* allocator_;
    int PAGE_SIZE;
    int PAGE_SHIFT;

    // std::set<void*, MemAddrRangeCmp> mem_obj_map;
    // std::set<MemObj*, MemAddrUserPtrCmp> mem_obj_userptr_map;

    MemObjPool mem_objs;

    // static KernelMutex mm_lock_;
    KernelMutex mm_lock_;
	bool is_cpu_accessible_;

    void init_allocator() {
        allocator_ = new Allocator(base_, (size_t)limit_);
    }

    void *aperture_allocate_area_aligned(
					    void *address,
					    uint64_t bytes,
					    uint64_t align) {
	    return allocate_area_aligned(address, bytes, align);
    }

    void *aperture_allocate_area(void *address, uint64_t bytes) {
	    return allocate_area_aligned(address, bytes, align_);
    }

    virtual void *allocate_area_aligned(void *address,
					    uint64_t bytes,
					    uint64_t align) = 0;

    virtual void release_area(void *address, uint64_t bytes) = 0;

    /* returns 0 on success. Assumes, that fmm_mutex is locked on entry */
    MemObj *aperture_allocate_object( void *new_address,
					     uint64_t handle,
					     uint64_t size,
					     uint32_t flags,
                         void *userptr = nullptr);

    /* Align size of a VM area
    *
    * Leave at least one guard page after every object to catch
    * out-of-bounds accesses with VM faults.
    */
    uint64_t vm_align_area_size(uint64_t size);

    bool aperture_is_valid();

    void *mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes,
		uint64_t *mmap_offset, uint32_t flags, MemObj **vm_obj) ;

    // After allocating the memory, return the MemObj created for this memory.
    // fmm_allocate_memory_object
    MemObj *mm_allocate_memory_object(uint32_t gpu_id, void *mem,
						uint64_t MemorySizeInBytes,
						uint64_t *mmap_offset,
						uint32_t flags);

    int release(MemObj *object);

    void mm_clear_aperture();

    void vm_remove_object(MemObj* object);

    MemObj *vm_find_object_by_address(const void *address, uint64_t size)
    {
	    return vm_find_object_by_address_userptr(address, size, 0);
    }

    MemObj *vm_find_object_by_address_range(const void *address)
    {
	    return vm_find_object_by_address_userptr_range(address, 0);
    }

    MemObj *vm_find_object_by_userptr(const void *address, uint64_t size)
    {
	    return vm_find_object_by_address_userptr(address, size, 1);
    }

    MemObj *vm_find_object_by_userptr_range(const void *address)
    {
	    return vm_find_object_by_address_userptr_range(address, 1);
    }

    MemObj *vm_find_object_by_address_userptr(const void *address, uint64_t size, int is_userptr)
    {
        return mem_objs.find(const_cast<void*>(address), is_userptr);
    }

    MemObj *vm_find_object_by_address_userptr_range(const void *address, int is_userptr)
    {
        return mem_objs.find(const_cast<void*>(address), is_userptr);
    }

    int map_to_gpu_scratch(uint32_t gpu_id, void *address, uint64_t size);
	int unmap_from_gpu(void *address,
			uint32_t *device_ids_array, uint32_t device_ids_array_size,
			MemObj *obj);

	int unmap_from_gpu_scratch(uint32_t gpu_id, void *address);

	int map_to_gpu_userptr(void *addr, uint64_t size,
					   uint64_t *gpuvm_addr, MemObj *object);

    /* If nodes_to_map is not NULL, map the nodes specified; otherwise map all. */
    // _fmm_map_to_gpu
    int map_to_gpu(void *address, uint64_t size, MemObj *obj,
			uint32_t *nodes_to_map, uint32_t nodes_array_size);

    void aperture_print()
    {
	    debug_print("\t Base: %p\n", base_);
	    debug_print("\t Limit: %p\n", limit_);
    }

    void manageable_aperture_print()
    {
        /*
        for (auto area: *allocator) {
		    debug_print("\t\t Range [%p - %p]\n", cur->start, cur->end);
        }

        for (auto obj: mem_objs) {
		    debug_print("\t\t Object [%p - %" PRIu64 "]\n",
				obj->start, obj->size);
        }
        */
    }

    int mm_map_to_gpu_scratch(uint32_t gpu_id, void *address, uint64_t size);

	int mm_map_to_gpu_userptr(void *addr, uint64_t size, uint64_t *gpuvm_addr, MemObj *object);

    int mm_map_to_gpu(void *address, uint64_t size, MemObj *obj,
			uint32_t *nodes_to_map, uint32_t nodes_array_size);

	int mm_unmap_from_gpu(void *address, uint32_t *device_ids_array,
            uint32_t device_ids_array_size, MemObj *obj);
	int mm_unmap_from_gpu_scratch(uint32_t gpu_id, void *address);
};

class MmapVma : public Vma {
public:
    MmapVma(Device *device, void *base, void* limit)
        : Vma(device, base, limit)
        {
        };

    virtual void *allocate_area_aligned(void *address,
					    uint64_t bytes,
					    uint64_t align) override;

    virtual void release_area(void *address, uint64_t bytes) override;

};

class ReservedVma : public Vma {
public:
    ReservedVma(Device* device, void *base, void* limit)
        : Vma(device, base, limit)
        {
        };

    virtual void *allocate_area_aligned(void *address,
					    uint64_t bytes,
					    uint64_t align) override;

    virtual void release_area(void *address, uint64_t bytes) override;

};

