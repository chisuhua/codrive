#pragma once

//#include "libhsakmt.h"
#include "inc/hsakmttypes.h"
#include "cmdio/mem_obj.h"
#include "cmdio/cmdio.h"
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "inc/debug.h"
#include "util/intmath.h"

extern int PAGE_SIZE;
extern bool is_dgpu;

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

void mm_destroy_process_apertures(void);
device_status_t mm_init_process_apertures(unsigned int NumNodes, node_props_t *node_props);
//
/* Memory interface */
void *mm_allocate_scratch(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes);
void *mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes, HsaMemFlags flags);
void *mm_allocate_doorbell(uint32_t gpu_id, uint64_t MemorySizeInBytes, uint64_t doorbell_offset);
void *mm_allocate_host(uint32_t node_id, void *address, uint64_t MemorySizeInBytes,
			HsaMemFlags flags);
//void mm_print(uint32_t node);
device_status_t mm_release(void *address);
int mm_map_to_gpu(void *address, uint64_t size, uint64_t *gpuvm_address);
int mm_unmap_from_gpu(void *address);
bool mm_get_handle(void *address, uint64_t *handle);
device_status_t mm_get_mem_info(const void *address, HsaPointerInfo *info);
device_status_t mm_set_mem_user_data(const void *mem, void *usr_data);

///* Topology interface*/
device_status_t mm_node_added(uint32_t gpu_id);
device_status_t mm_node_removed(uint32_t gpu_id);
device_status_t mm_get_aperture_base_and_limit(aperture_type_e aperture_type, uint32_t gpu_id,
		uint64_t *aperture_base, uint64_t *aperture_limit);

device_status_t mm_register_memory(void *address, uint64_t size_in_bytes,
								  uint32_t *gpu_id_array,
								  uint32_t gpu_id_array_size,
								  bool coarse_grain);
device_status_t mm_register_graphics_handle(uint64_t GraphicsResourceHandle,
					   HsaGraphicsResourceInfo *GraphicsResourceInfo,
					   uint32_t *gpu_id_array,
					   uint32_t gpu_id_array_size);
device_status_t mm_deregister_memory(void *address);
device_status_t mm_share_memory(void *MemoryAddress,
			       uint64_t SizeInBytes,
			       HsaSharedMemoryHandle *SharedMemoryHandle);
device_status_t mm_register_shared_memory(const HsaSharedMemoryHandle *SharedMemoryHandle,
					 uint64_t *SizeInBytes,
					 void **MemoryAddress,
					 uint32_t *gpu_id_array,
					 uint32_t gpu_id_array_size);
device_status_t mm_map_to_gpu_nodes(void *address, uint64_t size,
		uint32_t *nodes_to_map, uint64_t num_of_nodes, uint64_t *gpuvm_address);

void mm_clear_all_mem(void);

class Aperture;

struct ApertureOps{
	void *(*allocate_area_aligned)(Aperture *aper, void *addr,
				       uint64_t size, uint64_t align);
	void (*release_area)(Aperture *aper,
			     void *addr, uint64_t size);
};

typedef struct {
	void *base;
	void *limit;
} aperture_t;

class Aperture;


class gpu_mem_t {
    public:
        gpu_mem_t();
        ~gpu_mem_t();
	uint32_t gpu_id;
	uint32_t device_id;
	uint32_t node_id;
	uint64_t local_mem_size;
	aperture_t lds_aperture;
	aperture_t scratch_aperture;
	aperture_t mmio_aperture;
	Aperture* scratch_physical; /* For dGPU, scratch physical is allocated from
						 * dgpu_aperture. When requested by RT, each
						 * GPU will get a differnt range
						 */
	Aperture* gpuvm_aperture;   /* used for GPUVM on APU, outsidethe canonical address range */
	int drm_render_fd;
};

extern gpu_mem_t *gpu_mem;
extern int32_t gpu_mem_find_by_gpu_id(uint32_t gpu_id);
extern uint32_t all_gpu_id_array_size;
extern uint32_t *all_gpu_id_array;

void print_device_id_array(uint32_t *device_id_array, uint32_t device_id_array_size);

class Aperture {
    public:
    Aperture(void *base, void* limit, ApertureOps *ops /*&reserved_aperture_ops*/)
        : base_(base)
        , limit_(limit)
        , align_(0)
        , guard_pages_(1)
        , allocator(nullptr)
        , is_cpu_accessible(false)
        , ops(ops) {
        };

	void *base_;
	void *limit_;
	uint64_t align_;
	uint32_t guard_pages_;
    Allocator* allocator;
    ApertureOps* ops;
    // std::set<void*, MemAddrRangeCmp> mem_obj_map;
    // std::set<MemObj*, MemAddrUserPtrCmp> mem_obj_userptr_map;

    MemObjPool mem_objs;

    static KernelMutex mm_lock_;
	bool is_cpu_accessible;

    void init_allocator() {
        allocator = new Allocator(base_, (size_t)limit_);
    }

    void *aperture_allocate_area_aligned(
					    void *address,
					    uint64_t bytes,
					    uint64_t align) {
	    return ops->allocate_area_aligned(this, address, bytes, align);
    }

    void *aperture_allocate_area(void *address, uint64_t bytes) {
	    return ops->allocate_area_aligned(this, address, bytes, align_);
    }

    void aperture_release_area(void *address, uint64_t bytes) {
	    ops->release_area(this, address, bytes);
    }

    /* returns 0 on success. Assumes, that fmm_mutex is locked on entry */
    MemObj *aperture_allocate_object( void *new_address,
					     uint64_t handle,
					     uint64_t size,
					     uint32_t flags,
                         void *userptr = nullptr
                         )
    {
	    MemObj *mem_object = new MemObj(new_address, size, handle, flags, userptr);

	    if (!mem_object)
		    return NULL;
        mem_objs.insert(mem_object);

	    return mem_object;
    }

    /* Align size of a VM area
    *
    * Leave at least one guard page after every object to catch
    * out-of-bounds accesses with VM faults.
    */
    uint64_t vm_align_area_size(uint64_t size)
    {
	    return size + (uint64_t)guard_pages_ * PAGE_SIZE;
    }

    bool aperture_is_valid()
    {
	    if (base_ && limit_ && base_ < limit_)
		    return true;
	    return false;
    }

    // __fmm_allocate_device
    void *mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes,
		uint64_t *mmap_offset, uint32_t flags, MemObj **vm_obj) {
	    void *mem = nullptr;
	    MemObj *obj;

	    /* Check that aperture is properly initialized/supported */
	    if (!aperture_is_valid()) return nullptr;

	    /* Allocate address space */
        {
            ScopedAcquire<KernelMutex> lock(&mm_lock_);
	        mem = aperture_allocate_area(address, MemorySizeInBytes);
        }

	    //* Now that we have the area reserved, allocate memory in the device
	    obj = mm_allocate_memory_object(gpu_id, mem, MemorySizeInBytes, mmap_offset, flags);
	    if (!obj) {
		    // * allocation of memory in device failed.  Release region in aperture
            ScopedAcquire<KernelMutex> lock(&mm_lock_);
		    aperture_release_area(mem, MemorySizeInBytes);
		    /* Assign NULL to mem to indicate failure to calling function */
		    mem = NULL;
	    }
	    if (vm_obj)
		    *vm_obj = obj;
	    return mem;
    }

    // After allocating the memory, return the MemObj created for this memory.
    // fmm_allocate_memory_object
    MemObj *mm_allocate_memory_object(uint32_t gpu_id, void *mem,
						uint64_t MemorySizeInBytes,
						uint64_t *mmap_offset,
						uint32_t flags)
    {
	    struct ioctl_alloc_memory_args args = {0};
	    struct ioctl_free_memory_args free_args = {0};
	    MemObj *vm_obj = NULL;

	    if (!mem)
		    return NULL;

	    /* Allocate memory from amdkfd */
	    args.gpu_id = gpu_id;
	    args.size = MemorySizeInBytes;

	    args.flags = flags | KFD_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE;
	    args.va_addr = (uint64_t)mem;
	    if (!is_dgpu && (flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM))
		    args.va_addr = VOID_PTRS_SUB(mem, base_);

        void* userptr = nullptr;
	    if (flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR){
		    args.mmap_offset = *mmap_offset;
        }

	    if (cmd_alloc_memory(&args))
		    return nullptr;

	    if (mmap_offset) {
		    *mmap_offset = args.mmap_offset;
            userptr = (void*)(*mmap_offset);
        }

        {
            ScopedAcquire<KernelMutex> lock(&mm_lock_);
	        vm_obj = this->aperture_allocate_object(mem, args.handle,
				      MemorySizeInBytes, flags, userptr);
        }
        // lock.Release();

	    return vm_obj;

        MAKE_SCOPE_GUARD([&]() {
            if (!vm_obj) {
	            free_args.handle = args.handle;
	            cmd_free_memory(&free_args);
            }
        });
    }

    // int __fmm_release
    int release(MemObj *object)
    {
	    struct ioctl_free_memory_args args = {0};

	    if (!object)
		    return -EINVAL;

        ScopedAcquire<KernelMutex> lock(&mm_lock_);

	    /* If memory is user memory and it's still GPU mapped, munmap
	    * would cause an eviction. If the restore happens quickly
	    * enough, restore would also fail with an error message. So
	    * free the BO before unmapping the pages.
	    */
	    args.handle = object->handle;
	    if (cmd_free_memory(&args)) {
		    return -errno;
	    }

	    aperture_release_area(object->start, object->size);
	    vm_remove_object(object);

	    return 0;
    }

    void mm_clear_aperture() {
        ScopedAcquire<KernelMutex> lock(&mm_lock_);
        for (auto itr = mem_objs.begin(); itr != mem_objs.end(); ++itr) {
		    vm_remove_object(*itr);
        }
        // FIXME
        /* clear allocator
    	while (app->vm_ranges)
		    vm_remove_area(app, app->vm_ranges);
            */
    }

    void vm_remove_object(MemObj* object)
    {
        mem_objs.erase(object);
        free(object);
    }

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

    // _fmm_map_to_gpu_scratch
    int mm_map_to_gpu_scratch(uint32_t gpu_id, void *address, uint64_t size)
    {
		int32_t gpu_mem_id;
		int ret;
		bool is_debugger = 0;
		void *mmap_ret = NULL;
		uint64_t mmap_offset = 0;
		int map_fd;
		MemObj *obj;
	
		/* Retrieve gpu_mem id according to gpu_id */
		gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
		if (gpu_mem_id < 0)
			return -1;
	
		if (!is_dgpu)
			return 0; /* Nothing to do on APU */
	
		/* sanity check the address */
		if (address < base_ || (VOID_PTR_ADD(address, size - 1) > limit_))
			return -1;
	
		ret = debug_get_reg_status(gpu_mem[gpu_mem_id].node_id, &is_debugger);
		/* allocate object within the scratch backing aperture */
		if (!ret && !is_debugger) {
			obj = mm_allocate_memory_object( gpu_id, address, size, &mmap_offset,
				KFD_IOC_ALLOC_MEM_FLAGS_VRAM | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
			if (!obj)
				return -1;
			/* Create a CPU mapping for the debugger */
			map_fd = gpu_mem[gpu_mem_id].drm_render_fd;
			mmap_ret = mmap(address, size, PROT_NONE,
					MAP_PRIVATE | MAP_FIXED, map_fd, mmap_offset);
			if (mmap_ret == MAP_FAILED) {
				release(obj);
				return -1;
			}
		} else {
			obj = mm_allocate_memory_object(gpu_id, address, size, &mmap_offset,
				KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
			map_fd = gpu_mem[gpu_mem_id].drm_render_fd;
			mmap_ret = mmap(address, size,
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_FIXED, map_fd, mmap_offset);
			if (mmap_ret == MAP_FAILED) {
				release(obj);
				return -1;
			}
		}
	
		/* map to GPU */
		ret = mm_map_to_gpu(address, size, NULL, &gpu_id, sizeof(uint32_t));
		if (ret != 0)
			release(obj);
	
		return ret;
	}

	// int _fmm_unmap_from_gpu(manageable_aperture_t *aperture, void *address,
	int mm_unmap_from_gpu(void *address,
			uint32_t *device_ids_array, uint32_t device_ids_array_size,
			MemObj *obj)
	{
		MemObj *object;
		int ret = 0;
		struct ioctl_unmap_memory_from_gpu_args args = {0};
		HSAuint32 page_offset = (HSAint64)address & (PAGE_SIZE - 1);
	
		if (!obj)
			mm_lock_.Acquire();

		MAKE_SCOPE_GUARD([&]() {
            if (!obj)
			    mm_lock_.Release();
        });

		/* Find the object to retrieve the handle */
		object = obj;
		if (!object) {
			object = vm_find_object_by_address(VOID_PTR_SUB(address, page_offset), 0);
			if (!object) {
                return -1;
			}
		}
	
		if (object->userptr && object->mapping_count > 1) {
			--object->mapping_count;
            return 0;
		}
	
		args.handle = object->handle;
		if (device_ids_array && device_ids_array_size > 0) {
			args.device_ids_array_ptr = (uint64_t)device_ids_array;
			args.n_devices = device_ids_array_size / sizeof(uint32_t);
		} else if (object->mapped_device_id_array_size > 0) {
			args.device_ids_array_ptr = (uint64_t)object->mapped_device_id_array;
			args.n_devices = object->mapped_device_id_array_size /
				sizeof(uint32_t);
		} else {
			/*
			 * When unmap exits here it should return failing error code as the user tried to
			 * unmap already unmapped buffer. Currently we returns success as KFDTEST and RT
			 * need to deploy the change on there side before thunk fails on this case.
			 */
            return 0;
		}
		args.n_success = 0;
/*	
		print_device_id_array((void *)args.device_ids_array_ptr,
				      args.n_devices * sizeof(uint32_t));
*/	
		ret = cmd_unmap_memory_from_gpu(&args);
	
		object->remove_device_ids_from_mapped_array((uint32_t *)args.device_ids_array_ptr,
				args.n_success * sizeof(uint32_t));
	
		if (object->mapped_node_id_array)
			free(object->mapped_node_id_array);
		object->mapped_node_id_array = NULL;
		object->mapping_count = 0;
	
		return ret;
	}

	// int _fmm_unmap_from_gpu_scratch(uint32_t gpu_id,
	int mm_unmap_from_gpu_scratch(uint32_t gpu_id, void *address)
	{
		int32_t gpu_mem_id;
		MemObj *object;
		struct ioctl_unmap_memory_from_gpu_args args = {0};
		int ret;
	
		/* Retrieve gpu_mem id according to gpu_id */
		gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
		if (gpu_mem_id < 0)
			return -1;
	
		if (!is_dgpu)
			return 0; /* Nothing to do on APU */

        {
            ScopedAcquire<KernelMutex> lock(&mm_lock_);
	
		    /* Find the object to retrieve the handle and size */
		    object = vm_find_object_by_address(address, 0);
		    if (!object) {
                return -EINVAL;
		    }
	
		    if (!object->mapped_device_id_array ||
				object->mapped_device_id_array_size == 0) {
			    return 0;
		    }
	
		    /* unmap from GPU */
		    args.handle = object->handle;
		    args.device_ids_array_ptr = (uint64_t)object->mapped_device_id_array;
		    args.n_devices = object->mapped_device_id_array_size / sizeof(uint32_t);
		    args.n_success = 0;
		    ret = cmd_unmap_memory_from_gpu(&args);
		    if (ret) return ret;
	
		    /* unmap from CPU while keeping the address space reserved */
		    mmap(address, object->size, PROT_NONE,
		        MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE | MAP_FIXED,
		        -1, 0);
	
		    object->remove_device_ids_from_mapped_array(
				(uint32_t *)args.device_ids_array_ptr,
				args.n_success * sizeof(uint32_t));
	
		    if (object->mapped_node_id_array)
			    free(object->mapped_node_id_array);
		    object->mapped_node_id_array = NULL;
	
        }	
	
		/* free object in scratch backing aperture */
		return release(object);
	}

	// int _fmm_map_to_gpu_userptr(
	int mm_map_to_gpu_userptr(void *addr, uint64_t size,
					   uint64_t *gpuvm_addr, MemObj *object)
	{
        // TODO check it is  svm.dgpu_aperture
		// aperture = svm.dgpu_aperture;
		void *svm_addr;
		uint64_t svm_size;
		uint32_t page_offset = (uint64_t)addr & (PAGE_SIZE-1);
		int ret;
	
		svm_addr = object->start;
		svm_size = object->size;
	
		/* Map and return the GPUVM address adjusted by the offset
		 * from the start of the page
		 */
		ret = mm_map_to_gpu(svm_addr, svm_size, object, NULL, 0);
		if (ret == 0 && gpuvm_addr) {
			*gpuvm_addr = (uint64_t)svm_addr + page_offset;

        }
	
		return ret;
	}

    /* If nodes_to_map is not NULL, map the nodes specified; otherwise map all. */
    // _fmm_map_to_gpu
    int mm_map_to_gpu(void *address, uint64_t size, MemObj *obj,
			uint32_t *nodes_to_map, uint32_t nodes_array_size)
	{
		struct ioctl_map_memory_to_gpu_args args = {0};
		MemObj *object;
		int ret = 0;
	
		if (!obj)
			mm_lock_.Acquire();

	    MAKE_SCOPE_GUARD([&]() {
            if (!obj)
			    mm_lock_.Release();
        });

		object = obj;
		if (!object) {
			/* Find the object to retrieve the handle */
			object = vm_find_object_by_address(address, 0);
			if (!object) {
				return -EINVAL;
			}
		}
	
		/* For a memory region that is registered by user pointer, changing
		 * mapping nodes is not allowed, so we don't need to check the mapping
		 * nodes or map if it's already mapped. Just increase the reference.
		 */
		if (object->userptr && object->mapping_count) {
			++object->mapping_count;
            return 0;
		}
	
		args.handle = object->handle;

		if (nodes_to_map) {
		/* If specified, map the requested */
			args.device_ids_array_ptr = (uint64_t)nodes_to_map;
			args.n_devices = nodes_array_size;
		} else if (object->registered_device_id_array_size > 0) {
		/* otherwise map all registered */
			args.device_ids_array_ptr = (uint64_t)object->registered_device_id_array;
			args.n_devices = object->registered_device_id_array_size;
		} else {
		/* not specified, not registered: map all GPUs */
			args.device_ids_array_ptr = (uint64_t)all_gpu_id_array;
			args.n_devices = all_gpu_id_array_size;
		}
		args.n_success = 0;
	
		ret = cmd_map_memory_to_gpu(&args);
	
		object->add_device_ids_to_mapped_array((uint32_t *)args.device_ids_array_ptr,
					args.n_success * sizeof(uint32_t));
		print_device_id_array((uint32_t *)object->mapped_device_id_array,
				      object->mapped_device_id_array_size);
	
		object->mapping_count = 1;
		/* Mapping changed and lifecycle of object->mapped_node_id_array
		 * terminates here. Free it and allocate on next query
		 */
		if (object->mapped_node_id_array) {
			free(object->mapped_node_id_array);
			object->mapped_node_id_array = NULL;
		}
	
		return ret;
	}

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

}; // end Aperture


