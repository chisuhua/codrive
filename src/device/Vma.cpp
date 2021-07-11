#include "inc/Vma.h"
#include "inc/GpuMemory.h"
#include "inc/MemMgr.h"
#include "inc/Debug.h"
#include "util/intmath.h"
#include "cmdio/cmdio_types.h"
#include <sys/mman.h>

/* returns 0 on success. Assumes, that fmm_mutex is locked on entry */
MemObj * Vma::aperture_allocate_object( void *new_address,
					     uint64_t handle,
					     uint64_t size,
					     uint32_t flags,
                         void *userptr
                         ) {
    MemObj *mem_object = new MemObj(new_address, size, handle, flags, userptr);

    if (!mem_object)
	    return NULL;
    mem_objs.insert(mem_object);

    return mem_object;
}

uint64_t Vma::vm_align_area_size(uint64_t size) {
    return size + (uint64_t)guard_pages_ * PAGE_SIZE;
}

bool Vma::aperture_is_valid()
{
    if (base_ && limit_ && base_ < limit_)
	    return true;
    return false;
}

void* Vma::mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes,
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
	    release_area(mem, MemorySizeInBytes);
	    /* Assign NULL to mem to indicate failure to calling function */
	    mem = NULL;
    }
    if (vm_obj)
	    *vm_obj = obj;
    return mem;
}

// After allocating the memory, return the MemObj created for this memory.
// fmm_allocate_memory_object
MemObj* Vma::mm_allocate_memory_object(uint32_t gpu_id, void *mem,
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
    if (!device_->is_dgpu() && (flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM))
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

int Vma::release(MemObj *object)
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

    release_area(object->start, object->size);
    vm_remove_object(object);

    return 0;
}

void Vma::mm_clear_aperture() {
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

void Vma::vm_remove_object(MemObj* object)
{
    mem_objs.erase(object);
    free(object);
}

int Vma::map_to_gpu_scratch(uint32_t gpu_id, void *address, uint64_t size)
{
	int ret;
	bool is_debugger = 0;
	void *mmap_ret = NULL;
	uint64_t mmap_offset = 0;
	int map_fd;
	MemObj *obj;
    std::shared_ptr<GpuMemory> gpu_mem = device_->get_mm()->get_gpu_mem(gpu_id);

	/* Retrieve gpu_mem id according to gpu_id */
	// gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
	// if (gpu_mem_id < 0)
	// 	return -1;


	if (!device_->is_dgpu())
		return 0; /* Nothing to do on APU */

	/* sanity check the address */
	if (address < base_ || (VOID_PTR_ADD(address, size - 1) > limit_))
		return -1;

	ret = device_->get_debug()->debug_get_reg_status(gpu_mem->node_id, &is_debugger);
	/* allocate object within the scratch backing aperture */
	if (!ret && !is_debugger) {
		obj = mm_allocate_memory_object( gpu_id, address, size, &mmap_offset,
			KFD_IOC_ALLOC_MEM_FLAGS_VRAM | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
		if (!obj)
			return -1;
		/* Create a CPU mapping for the debugger */
		map_fd = gpu_mem->drm_render_fd;
		mmap_ret = mmap(address, size, PROT_NONE,
				MAP_PRIVATE | MAP_FIXED, map_fd, mmap_offset);
		if (mmap_ret == MAP_FAILED) {
			release(obj);
			return -1;
		}
	} else {
		obj = mm_allocate_memory_object(gpu_id, address, size, &mmap_offset,
			KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
		map_fd = gpu_mem->drm_render_fd;
		mmap_ret = mmap(address, size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_FIXED, map_fd, mmap_offset);
		if (mmap_ret == MAP_FAILED) {
			release(obj);
			return -1;
		}
	}

	/* map to GPU */
	ret = map_to_gpu(address, size, NULL, &gpu_id, sizeof(uint32_t));
	if (ret != 0)
		release(obj);

	return ret;
}

int Vma::unmap_from_gpu(void *address,
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

	// print_device_id_array((void *)args.device_ids_array_ptr,
	//		      args.n_devices * sizeof(uint32_t));

	ret = cmd_unmap_memory_from_gpu(&args);

	object->remove_device_ids_from_mapped_array((uint32_t *)args.device_ids_array_ptr,
			args.n_success * sizeof(uint32_t));

	if (object->mapped_node_id_array)
		free(object->mapped_node_id_array);
	object->mapped_node_id_array = NULL;
	object->mapping_count = 0;

	return ret;
}

int Vma::unmap_from_gpu_scratch(uint32_t gpu_id, void *address)
{
	// int32_t gpu_mem_id;
	MemObj *object;
	struct ioctl_unmap_memory_from_gpu_args args = {0};
	int ret;

	/* Retrieve gpu_mem id according to gpu_id */
    // std::shared_ptr<GpuMemory> gpu_mem = device_->get_mm()->get_gpu_mem(gpu_id);
	// if (gpu_mem_id < 0)
    //		return -1;

	if (!device_->is_dgpu())
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

int Vma::map_to_gpu_userptr(void *addr, uint64_t size,
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
	ret = map_to_gpu(svm_addr, svm_size, object, NULL, 0);
	if (ret == 0 && gpuvm_addr) {
		*gpuvm_addr = (uint64_t)svm_addr + page_offset;

    }

	return ret;
}

int Vma::map_to_gpu(void *address, uint64_t size, MemObj *obj,
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
		args.device_ids_array_ptr = (uint64_t)(device_->all_gpu_id_array);
		args.n_devices = device_->all_gpu_id_array_size;
	}
	args.n_success = 0;

	ret = cmd_map_memory_to_gpu(&args);

	object->add_device_ids_to_mapped_array((uint32_t *)args.device_ids_array_ptr,
				args.n_success * sizeof(uint32_t));
    /*
	print_device_id_array((uint32_t *)object->mapped_device_id_array,
			      object->mapped_device_id_array_size);
                  */

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

/*
 * returns allocated address or NULL. Assumes, that fmm_mutex is locked
 * on entry.
 */
void* ReservedVma::allocate_area_aligned(void *address,
					    uint64_t bytes,
					    uint64_t align)
{
	uint64_t offset = 0, orig_align = align;
	void *start;

	if (align < align_)
		align = align_;

	// Align big buffers to the next power-of-2 up to huge page
	// size for flexible fragment size TLB optimizations

	while (align < GPU_HUGE_PAGE_SIZE && bytes >= (align << 1))
		align <<= 1;

	/* If no specific alignment was requested, align the end of
	 * buffers instead of the start. For fragment optimizations,
	 * aligning the start or the end achieves the same effective
	 * optimization. End alignment to the TLB cache line size is
	 * needed as a workaround for TLB issues on some older GPUs.
	 */
	if (orig_align <= (uint64_t)PAGE_SIZE)
		offset = align - (bytes & (align - 1));

	bytes = vm_align_area_size(bytes);

	// Find a big enough "hole" in the address space
	start = address ? address :
		(void *)(alignUp((uint64_t)base_, align) + offset);

    start = allocator_->alloc(start, bytes, address ? true: false);
	return start;
}

/*
 * Assumes that fmm_mutex is locked on entry.
 */
void ReservedVma::release_area(void *address,
				      uint64_t bytes)
{
	// vm_area_t *area;
	uint64_t SizeOfRegion;

	bytes = vm_align_area_size(bytes);

    bool result = allocator_->free(address, bytes);
	if (!result)
		return;

	SizeOfRegion = bytes; // VOID_PTRS_SUB(area->end, area->start) + 1;

	if (is_cpu_accessible_) {
		void *mmap_ret;

		/* Reset NUMA policy */
		// TODO mbind(address, bytes, MPOL_DEFAULT, NULL, 0, 0);

		/* Remove any CPU mapping, but keep the address range reserved */
		mmap_ret = mmap(address, bytes, PROT_NONE,
			MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE | MAP_FIXED,
			-1, 0);
		if (mmap_ret == MAP_FAILED && errno == ENOMEM) {
			/* When mmap count reaches max_map_count, any mmap will
			 * fail. Reduce the count with munmap then map it as
			 * NORESERVE immediately.
			 */
			munmap(address, bytes);
			mmap(address, bytes, PROT_NONE,
				MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE | MAP_FIXED,
				-1, 0);
		}
	}
}

void* MmapVma::allocate_area_aligned(void *address,
					    uint64_t size, uint64_t align)
{
	uint64_t aligned_padded_size, guard_size;
	void *addr, *aligned_addr, *aligned_end, *mapping_end;

	if (address)
		return NULL;

	if (!is_cpu_accessible_) {
		ERROR("MMap Aperture must be CPU accessible\n");
		return NULL;
	}

	/* Align big buffers to the next power-of-2 up to huge page
	 * size for flexible fragment size TLB optimizations
	 */
	while (align < GPU_HUGE_PAGE_SIZE && size >= (align << 1))
		align <<= 1;

	/* Add padding to guarantee proper alignment and leave guard
	 * pages on both sides
	 */
	guard_size = (uint64_t)guard_pages_ * PAGE_SIZE;
	aligned_padded_size = size + align +
		2*guard_size - PAGE_SIZE;

	/* Map memory */
	addr = mmap(0, aligned_padded_size, PROT_NONE,
		    MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED) {
		// ERROR("mmap failed: %s\n", strerror(errno));
		return NULL;
	}

	/* Adjust for alignment and guard pages, range-check the reslt */
	aligned_addr = (void *)alignUp((uint64_t)addr + guard_size, align);
	if (aligned_addr < base_ ||
	    VOID_PTR_ADD(aligned_addr, size - 1) > limit_) {
		ERROR("mmap returned %p, out of range %p-%p\n", aligned_addr,
		       base_, limit_);
		munmap(addr, aligned_padded_size);
		return NULL;
	}

	/* Unmap padding and guard pages */
	if (aligned_addr > addr)
		munmap(addr, VOID_PTRS_SUB(aligned_addr, addr));

	aligned_end = VOID_PTR_ADD(aligned_addr, size);
	mapping_end = VOID_PTR_ADD(addr, aligned_padded_size);
	if (mapping_end > aligned_end)
		munmap(aligned_end, VOID_PTRS_SUB(mapping_end, aligned_end));

	return aligned_addr;
}

void MmapVma::release_area(void *addr, uint64_t size)
{
	if (!is_cpu_accessible_) {
		ERROR("MMap Aperture must be CPU accessible\n");
		return;
	}

	/* Reset NUMA policy */
	// TODO mbind(addr, size, MPOL_DEFAULT, NULL, 0, 0);

	/* Unmap memory */
	munmap(addr, size);
}

int Vma::mm_map_to_gpu_scratch(uint32_t gpu_id, void *address, uint64_t size)
{
	int32_t gpu_mem_id;
	int ret;
	bool is_debugger = 0;
	void *mmap_ret = NULL;
	uint64_t mmap_offset = 0;
	int map_fd;
	MemObj *obj;

	/* Retrieve gpu_mem id according to gpu_id */
	gpu_mem_id = device_->get_mm()->gpu_mem_find_by_gpu_id(gpu_id);
	if (gpu_mem_id < 0)
		return -1;

	if (!device_->is_dgpu())
		return 0; /* Nothing to do on APU */

	/* sanity check the address */
	if (address < base_ || (VOID_PTR_ADD(address, size - 1) > limit_))
		return -1;

	ret = get_debugger()->debug_get_reg_status(get_gpu_mem(gpu_mem_id)->node_id, &is_debugger);
	/* allocate object within the scratch backing aperture */
	if (!ret && !is_debugger) {
		obj = mm_allocate_memory_object( gpu_id, address, size, &mmap_offset,
			KFD_IOC_ALLOC_MEM_FLAGS_VRAM | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
		if (!obj)
			return -1;
		/* Create a CPU mapping for the debugger */
		map_fd = get_gpu_mem(gpu_mem_id)->drm_render_fd;
		mmap_ret = mmap(address, size, PROT_NONE,
				MAP_PRIVATE | MAP_FIXED, map_fd, mmap_offset);
		if (mmap_ret == MAP_FAILED) {
			release(obj);
			return -1;
		}
	} else {
		obj = mm_allocate_memory_object(gpu_id, address, size, &mmap_offset,
			KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE);
		map_fd = get_gpu_mem(gpu_mem_id)->drm_render_fd;
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

int Vma::mm_map_to_gpu_userptr(void *addr, uint64_t size,
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
int Vma::mm_map_to_gpu(void *address, uint64_t size, MemObj *obj,
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
		args.device_ids_array_ptr = (uint64_t)(device_->all_gpu_id_array);
		args.n_devices = device_->all_gpu_id_array_size;
	}
	args.n_success = 0;

	ret = cmd_map_memory_to_gpu(&args);

	object->add_device_ids_to_mapped_array((uint32_t *)args.device_ids_array_ptr,
				args.n_success * sizeof(uint32_t));
	//print_device_id_array((uint32_t *)object->mapped_device_id_array,
    //			      object->mapped_device_id_array_size);

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

int Vma::mm_unmap_from_gpu(void *address,
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

int Vma::mm_unmap_from_gpu_scratch(uint32_t gpu_id, void *address)
{
	int32_t gpu_mem_id;
	MemObj *object;
	struct ioctl_unmap_memory_from_gpu_args args = {0};
	int ret;

	/* Retrieve gpu_mem id according to gpu_id */
	gpu_mem_id = device_->get_mm()->gpu_mem_find_by_gpu_id(gpu_id);
	if (gpu_mem_id < 0)
		return -1;

	if (!device_->is_dgpu())
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

std::shared_ptr<GpuMemory> Vma::get_gpu_mem(uint32_t gpu_mem_id) {
    device_->get_mm()->gpu_mem_[gpu_mem_id];
}
