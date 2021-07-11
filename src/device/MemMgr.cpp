#include "inc/MemMgr.h"
#include "inc/Device.h"
#include "inc/Process.h"
#include "inc/Topology.h"
#include "util/intmath.h"
#include <sys/mman.h>
#include <cstring>

// extern bool is_dgpu;
bool id_in_array(uint32_t id, uint32_t *ids_array, uint32_t ids_array_size);

#define SCRATCH_ALIGN 0x10000

MemMgr::MemMgr(Device* device)
    : device_(device)
{
    svm_ = std::make_shared<GpuSVM>(device);
    cpuvm_aperture = new ReservedVma(device, nullptr, nullptr);
}

void* MemMgr::allocate_exec_aligned_memory_cpu(uint32_t size)
{
	void *ptr;

	/* mmap will return a pointer with alignment equal to
	 * sysconf(_SC_PAGESIZE).
	 *
	 * MAP_ANONYMOUS initializes the memory to zero.
	 */
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (ptr == MAP_FAILED)
		return NULL;
	return ptr;
}

void* MemMgr::allocate_exec_aligned_memory_gpu(uint32_t size, uint32_t align,
				       uint32_t NodeId, bool nonPaged,
				       bool DeviceLocal)
{
	void *mem;
	uint64_t gpu_va;
	HsaMemFlags flags;
	device_status_t ret;
	uint32_t cpu_id = 0;

	flags.Value = 0;
	flags.ui32.HostAccess = !DeviceLocal;
	flags.ui32.ExecuteAccess = 1;
	flags.ui32.NonPaged = nonPaged;
	flags.ui32.PageSize = HSA_PAGE_SIZE_4KB;
	flags.ui32.CoarseGrain = DeviceLocal;

	/* Get the closest cpu_id to GPU NodeId for system memory allocation
	 * nonPaged=1 system memory allocation uses GTT path
	 */
	if (!DeviceLocal && !nonPaged) {
		cpu_id = topo_->get_direct_link_cpu(NodeId);
		if (cpu_id == INVALID_NODEID) {
			flags.ui32.NoNUMABind = 1;
			cpu_id = 0;
		}
	}

	size = alignUp(size, align);

	ret = device_->AllocMemory(DeviceLocal ? NodeId : cpu_id, size, flags, &mem);
	if (ret != DEVICE_STATUS_SUCCESS)
		return NULL;

	if (NodeId != 0) {
		uint32_t nodes_array[1] = {NodeId};

		if (device_->RegisterMemoryToNodes(mem, size, 1, nodes_array) != DEVICE_STATUS_SUCCESS) {
			device_->FreeMemory(mem, size);
			return NULL;
		}
	}

	if (device_->MapMemoryToGPU(mem, size, &gpu_va) != DEVICE_STATUS_SUCCESS) {
		device_->FreeMemory(mem, size);
		return NULL;
	}

	return mem;
}

void MemMgr::free_exec_aligned_memory_gpu(void *addr, uint32_t size, uint32_t align)
{
	size = alignUp(size, align);

	if (device_->UnmapMemoryToGPU(addr) == DEVICE_STATUS_SUCCESS)
		device_->FreeMemory(addr, size);
}

/*
 * Allocates memory aligned to sysconf(_SC_PAGESIZE)
 */
void *MemMgr::allocate_exec_aligned_memory(uint32_t size,
					  bool use_ats,
					  uint32_t NodeId,
					  bool DeviceLocal)
{
	if (!use_ats)
		return allocate_exec_aligned_memory_gpu(size, device_->PAGE_SIZE, NodeId,
							DeviceLocal, DeviceLocal);
	return allocate_exec_aligned_memory_cpu(size);
}

void MemMgr::free_exec_aligned_memory(void *addr, uint32_t size, uint32_t align,
				     bool use_ats)
{
	if (!use_ats)
		free_exec_aligned_memory_gpu(addr, size, align);
	else
		munmap(addr, size);
}

device_status_t MemMgr::init_svm_apertures(uint64_t base, uint64_t limit,
					uint32_t align, uint32_t guard_pages)
{
	const uint64_t ADDR_INC = GPU_HUGE_PAGE_SIZE;
	uint64_t len, map_size, alt_base, alt_size;
	bool found = false;
	void *addr, *ret_addr;

	/* If we already have an SVM aperture initialized (from a
	 * parent process), keep using it
	 */
	if (dgpu_shared_aperture_limit_)
		return DEVICE_STATUS_SUCCESS;

	/* Align base and limit to huge page size */
	base = alignUp(base, GPU_HUGE_PAGE_SIZE);
	limit = ((limit + 1) & ~(uint64_t)(GPU_HUGE_PAGE_SIZE - 1)) - 1;

	/* If the limit is greater or equal 47-bits of address space,
	 * it means we have GFXv9 or later GPUs only. We don't need
	 * apertures to determine the MTYPE and the virtual address
	 * space of the GPUs covers the full CPU address range (on
	 * x86_64) or at least mmap is unlikely to run out of
	 * addresses the GPUs can handle.
	 */
	if (limit >= (1ULL << 47) - 1 && !svm_->reserve_svm) {
		device_status_t status = init_mmap_apertures(base, limit, align,
							   guard_pages);

		if (status == DEVICE_STATUS_SUCCESS)
			return status;
		/* fall through: fall back to reserved address space */
	}

	if (limit > SVM_RESERVATION_LIMIT)
		limit = SVM_RESERVATION_LIMIT;
	if (base >= limit) {
		ERROR("No SVM range compatible with all GPU and software constraints\n");
		return DEVICE_STATUS_ERROR;
	}

	/* Try to reserve address space for svm_->
	 *
	 * Inner loop: try start addresses in huge-page increments up
	 * to half the VM size we're trying to reserve
	 *
	 * Outer loop: reduce size of the allocation by factor 2 at a
	 * time and print a warning for every reduction
	 */
	for (len = limit - base + 1; !found && len >= SVM_MIN_VM_SIZE;
	     len = (len + 1) >> 1) {
		for (addr = (void *)base, ret_addr = NULL;
		     (uint64_t)addr + ((len + 1) >> 1) - 1 <= limit;
		     addr = (void *)((uint64_t)addr + ADDR_INC)) {
			uint64_t top = std::min((uint64_t)addr + len, limit+1);

			map_size = (top - (uint64_t)addr) &
				~(uint64_t)(PAGE_SIZE - 1);
			if (map_size < SVM_MIN_VM_SIZE)
				break;

			ret_addr = reserve_address(addr, map_size);
			if (!ret_addr)
				break;
			if ((uint64_t)ret_addr + ((len + 1) >> 1) - 1 <= limit)
				/* At least half the returned address
				 * space is GPU addressable, we'll
				 * take it
				 */
				break;
			munmap(ret_addr, map_size);
			ret_addr = NULL;
		}
		if (!ret_addr) {
			ERROR("Failed to reserve %uGB for SVM ...\n",
				(unsigned int)(len >> 30));
			continue;
		}
		if ((uint64_t)ret_addr + SVM_MIN_VM_SIZE - 1 > limit) {
			/* addressable size is less than the minimum */
			WARN("Got %uGB for SVM at %p with only %dGB usable ...\n",
				(unsigned int)(map_size >> 30), ret_addr,
				(int)((limit - (int64_t)ret_addr) >> 30));
			munmap(ret_addr, map_size);
			ret_addr = NULL;
			continue;
		} else {
			found = true;
			break;
		}
	}

	if (!found) {
		ERROR("Failed to reserve SVM address range. Giving up.\n");
		return DEVICE_STATUS_ERROR;
	}

	base = (uint64_t)ret_addr;
	if (base + map_size - 1 > limit)
		/* trim the tail that's not GPU-addressable */
		munmap((void *)(limit + 1), base + map_size - 1 - limit);
	else
		limit = base + map_size - 1;

	/* init two apertures for non-coherent and coherent memory */
	svm_->apertures[SVM_DEFAULT]->base_  = dgpu_shared_aperture_base_  = ret_addr;
	svm_->apertures[SVM_DEFAULT]->limit_ = dgpu_shared_aperture_limit_ = (void *)limit;
	svm_->apertures[SVM_DEFAULT]->align_ = align;
	svm_->apertures[SVM_DEFAULT]->guard_pages_ = guard_pages;
	svm_->apertures[SVM_DEFAULT]->is_cpu_accessible_ = true;
	// svm_->apertures[SVM_DEFAULT]->ops_ = &reserved_aperture_ops;

	/* Use the first 1/4 of the dGPU aperture as
	 * alternate aperture for coherent access.
	 * Base and size must be 64KB aligned.
	 */
	alt_base = (uint64_t)svm_->apertures[SVM_DEFAULT]->base_;
	alt_size = (VOID_PTRS_SUB(svm_->apertures[SVM_DEFAULT]->limit_,
				  svm_->apertures[SVM_DEFAULT]->base_) + 1) >> 2;
	alt_base = (alt_base + 0xffff) & ~0xffffULL;
	alt_size = (alt_size + 0xffff) & ~0xffffULL;
	svm_->apertures[SVM_COHERENT]->base_ = (void *)alt_base;
	svm_->apertures[SVM_COHERENT]->limit_ = (void *)(alt_base + alt_size - 1);
	svm_->apertures[SVM_COHERENT]->align_ = align;
	svm_->apertures[SVM_COHERENT]->guard_pages_ = guard_pages;
	svm_->apertures[SVM_COHERENT]->is_cpu_accessible_ = true;
	// svm_->apertures[SVM_COHERENT]->ops_ = &reserved_aperture_ops;

	svm_->apertures[SVM_DEFAULT]->base_ = VOID_PTR_ADD(svm_->apertures[SVM_COHERENT]->limit_, 1);

	svm_->apertures[SVM_DEFAULT]->init_allocator();
	svm_->apertures[SVM_COHERENT]->init_allocator();

	INFO("SVM alt (coherent): %12p - %12p\n",
		svm_->apertures[SVM_COHERENT]->base_, svm_->apertures[SVM_COHERENT]->limit_);
	INFO("SVM (non-coherent): %12p - %12p\n",
		svm_->apertures[SVM_DEFAULT]->base_, svm_->apertures[SVM_DEFAULT]->limit_);

	svm_->dgpu_aperture = svm_->apertures[SVM_DEFAULT];
	svm_->dgpu_alt_aperture = svm_->apertures[SVM_COHERENT];

	return DEVICE_STATUS_SUCCESS;
}

device_status_t MemMgr::mm_init_process_apertures(unsigned int NumNodes, node_props_t *node_props)
{
	uint32_t i;
	int32_t gpu_mem_id = 0;
	uint32_t gpu_id;
	HsaCoreProperties *props;
	struct process_device_apertures *process_apertures;
	uint32_t num_of_sysfs_nodes;
	device_status_t ret = DEVICE_STATUS_SUCCESS;
	char *hsaDebug, *disableCache, *pagedUserptr, *checkUserptr, *guardPagesStr, *reserveSvm;
	unsigned int guardPages = 1;
	uint64_t svm_base = 0, svm_limit = 0;
	uint32_t svm_alignment = 0;

	// hsaDebug = getenv("HSA_DEBUG");
	// hsa_debug = hsaDebug && strcmp(hsaDebug, "0");

	/* If HSA_DISABLE_CACHE is set to a non-0 value, disable caching */
	disableCache = getenv("HSA_DISABLE_CACHE");
	svm_->disable_cache = (disableCache && strcmp(disableCache, "0"));

	/* If HSA_USERPTR_FOR_PAGED_MEM is not set or set to a non-0
	 * value, enable userptr for all paged memory allocations
	 */
	pagedUserptr = getenv("HSA_USERPTR_FOR_PAGED_MEM");
	svm_->userptr_for_paged_mem = (!pagedUserptr || strcmp(pagedUserptr, "0"));

	/* If HSA_CHECK_USERPTR is set to a non-0 value, check all userptrs
	 * when they are registered
	 */
	checkUserptr = getenv("HSA_CHECK_USERPTR");
	svm_->check_userptr = (checkUserptr && strcmp(checkUserptr, "0"));

	/* If HSA_RESERVE_SVM is set to a non-0 value,
	 * enable packet capture and replay mode.
	 */
	reserveSvm = getenv("HSA_RESERVE_SVM");
	svm_->reserve_svm = (reserveSvm && strcmp(reserveSvm, "0"));

	/* Specify number of guard pages for SVM apertures, default is 1 */
	guardPagesStr = getenv("HSA_SVM_GUARD_PAGES");
	if (!guardPagesStr || sscanf(guardPagesStr, "%u", &guardPages) != 1)
		guardPages = 1;

	gpu_mem_count_ = 0;
	first_gpu_mem_ = NULL;

	/* Trade off - NumNodes includes GPU nodes + CPU Node. So in
	 * systems with CPU node, slightly more memory is allocated than
	 * necessary
	 */
	// gpu_mem = (gpu_mem_t *)calloc(NumNodes, sizeof(gpu_mem_t));
	// gpu_mem = new gpu_mem_t[NumNodes];
    for (uint32_t i = 0; i < NumNodes; ++i) {
        gpu_mem_.push_back(std::make_shared<GpuMemory>(device_));
    }

	/* Initialize gpu_mem_[] from sysfs topology. Rest of the members are
	 * set to 0 by calloc. This is necessary because this function
	 * gets called before hsaKmtAcquireSystemProperties() is called.
	 */
	gpu_mem_count_ = 0;

	//struct pci_ids pacc;
	// pacc = pci_ids_create();
	// is_dgpu = 

	for (i = 0; i < NumNodes; i++) {
        props = node_props[i].core;
        gpu_id = node_props[i].gpu_id;
		// memset(&props, 0, sizeof(props));
		// ret = topology_sysfs_get_node_props(i, &props, &gpu_id, pacc);
		// ret = topology_sysfs_get_node_props(i, &props, &gpu_id);
		if (ret != DEVICE_STATUS_SUCCESS)
			goto sysfs_parse_failed;

		topo_->topology_setup_is_dgpu_param(props);

		//  Skip non-GPU nodes */
		if (gpu_id != 0) {
			int fd = open_drm_render_device(props->DrmRenderMinor);
			if (fd <= 0) {
				ret = DEVICE_STATUS_ERROR;
				goto sysfs_parse_failed;
			}

			gpu_mem_[gpu_mem_count_]->drm_render_fd = fd;
			gpu_mem_[gpu_mem_count_]->gpu_id = gpu_id;
			gpu_mem_[gpu_mem_count_]->local_mem_size = props->LocalMemSize;
			gpu_mem_[gpu_mem_count_]->device_id = props->DeviceId;
			gpu_mem_[gpu_mem_count_]->node_id = i;

			gpu_mem_[gpu_mem_count_]->scratch_physical->align_ = PAGE_SIZE;
			// gpu_mem_[gpu_mem_count_]->scratch_physical->ops = &reserved_aperture_ops;
			// pthread_mutex_init(&gpu_mem_[gpu_mem_count_].scratch_physical.fmm_mutex, NULL);

			// gpu_mem_[gpu_mem_count_].scratch_aperture.align = PAGE_SIZE;
			// pthread_mutex_init(&gpu_mem_[gpu_mem_count_].scratch_aperture.fmm_mutex, NULL);

			gpu_mem_[gpu_mem_count_]->gpuvm_aperture->align_ = PAGE_SIZE; // get_vm_alignment(props->DeviceId);
			gpu_mem_[gpu_mem_count_]->gpuvm_aperture->guard_pages_ = guardPages;
			// gpu_mem_[gpu_mem_count_].gpuvm_aperture->ops = &reserved_aperture_ops;

			// pthread_mutex_init(&gpu_mem_[gpu_mem_count_].gpuvm_aperture.fmm_mutex, NULL);
			if (!first_gpu_mem_)
				first_gpu_mem_ = gpu_mem_[gpu_mem_count_];

			gpu_mem_count_++;
		}
	}
	// pci_ids_destroy(pacc);

	/* The ioctl will also return Number of Nodes if
	 * args.kfd_process_device_apertures_ptr is set to NULL. This is not
	 * required since Number of nodes is already known. Kernel will fill in
	 * the apertures in kfd_process_device_apertures_ptr
	 */
    /*
	num_of_sysfs_nodes = get_num_sysfs_nodes();
	if (num_of_sysfs_nodes < gpu_mem_count_) {
		ret = DEVICE_STATUS_ERROR;
		goto sysfs_parse_failed;
	}
    */

    /*
	process_apertures = (ioctl_process_device_apertures*)calloc(num_of_sysfs_nodes, sizeof(struct ioctl_process_device_apertures));

	if (!process_apertures) {
		ret = DEVICE_STATUS_NO_MEMORY;
		goto sysfs_parse_failed;
	}
    */

	/* GPU Resource management can disable some of the GPU nodes.
	 * The Kernel driver could be not aware of this.
	 * Get from Kernel driver information of all the nodes and then filter it.
	 */
	ret = get_process_apertures(&process_apertures, &num_of_sysfs_nodes);
	if (ret != DEVICE_STATUS_SUCCESS)
		goto get_aperture_ioctl_failed;

	device_->all_gpu_id_array_size = 0;
	// all_gpu_id_array = NULL;
	if (num_of_sysfs_nodes > 0) {
        // device_->gpu_info_table_.resize(gpu_mem_count_);
		device_->all_gpu_id_array = (uint32_t*)malloc(sizeof(uint32_t) * gpu_mem_count_);
		if (!device_->all_gpu_id_array) {
			ret = DEVICE_STATUS_NO_MEMORY;
			goto get_aperture_ioctl_failed;
		}
	}

	for (i = 0 ; i < num_of_sysfs_nodes ; i++) {
		/* Map Kernel process device data node i <--> gpu_mem_id which
		 * indexes into gpu_mem_[] based on gpu_id
		 */
		gpu_mem_id = gpu_mem_find_by_gpu_id(process_apertures[i].gpu_id);
		if (gpu_mem_id < 0) {
			continue;
		}

		/* if (all_gpu_id_array_size == gpu_mem_count_) {
			ret = DEVICE_STATUS_ERROR;
			goto invalid_gpu_id;
		}*/
		device_->all_gpu_id_array[device_->all_gpu_id_array_size++] = process_apertures[i].gpu_id;
        // device_->gpu_info_table_[all_gpu_id_array_size++] = process_apertures[i].gpu_id;

		gpu_mem_[gpu_mem_id]->lds_aperture.base = (void*)(process_apertures[i].lds_base);
		gpu_mem_[gpu_mem_id]->lds_aperture.size = process_apertures[i].lds_limit - process_apertures[i].lds_base;
		gpu_mem_[gpu_mem_id]->scratch_aperture.base = (void*)(process_apertures[i].scratch_base);
		gpu_mem_[gpu_mem_id]->scratch_aperture.size = process_apertures[i].scratch_limit - process_apertures[i].scratch_base;

		if (IS_CANONICAL_ADDR(process_apertures[i].gpuvm_limit)) {
			uint64_t vm_alignment = PAGE_SIZE; // get_vm_alignment( gpu_mem_[gpu_mem_id].device_id);

			/* Set proper alignment for scratch backing aperture */
			gpu_mem_[gpu_mem_id]->scratch_physical->align_ = vm_alignment;

			/* Non-canonical per-ASIC GPUVM aperture does
			 * not exist on dGPUs in GPUVM64 address mode
			 */
			gpu_mem_[gpu_mem_id]->gpuvm_aperture->base_ = NULL;
			gpu_mem_[gpu_mem_id]->gpuvm_aperture->limit_ = NULL;

			/* Update SVM aperture limits and alignment */
			if (process_apertures[i].gpuvm_base > svm_base)
				svm_base = process_apertures[i].gpuvm_base;
			if (process_apertures[i].gpuvm_limit < svm_limit ||
			    svm_limit == 0)
				svm_limit = process_apertures[i].gpuvm_limit;
			if (vm_alignment > svm_alignment)
				svm_alignment = vm_alignment;
		} else {
			gpu_mem_[gpu_mem_id]->gpuvm_aperture->base_ = (void*)(process_apertures[i].gpuvm_base);
			gpu_mem_[gpu_mem_id]->gpuvm_aperture->limit_ = (void*)(process_apertures[i].gpuvm_limit );
			/* Reserve space at the start of the
			 * aperture. After subtracting the base, we
			 * don't want valid pointers to become NULL.
			 */
			gpu_mem_[gpu_mem_id]->gpuvm_aperture->aperture_allocate_area(
				NULL,
				gpu_mem_[gpu_mem_id]->gpuvm_aperture->align_);
		}

		/* Acquire the VM from the DRM render node for KFD use */
        // it call amdgpu_vm_init in admgpu_vm.c
		ret = acquire_vm(gpu_mem_[gpu_mem_id]->gpu_id,
				 gpu_mem_[gpu_mem_id]->drm_render_fd);
		if (ret != DEVICE_STATUS_SUCCESS)
			goto acquire_vm_failed;
	}
	device_->all_gpu_id_array_size *= sizeof(uint32_t);

	if (svm_limit) {
		/* At least one GPU uses GPUVM in canonical address
		 * space. Set up SVM apertures shared by all such GPUs
		 */
		ret = init_svm_apertures(svm_base, svm_limit, svm_alignment,
					 guardPages);
		if (ret != DEVICE_STATUS_SUCCESS)
			goto init_svm_failed;

		for (i = 0 ; i < num_of_sysfs_nodes ; i++) {
			uintptr_t alt_base;
			uint64_t alt_size;
			int err;

			if (!IS_CANONICAL_ADDR(process_apertures[i].gpuvm_limit))
				continue;

			/* Set memory policy to match the SVM apertures */
			alt_base = (uintptr_t)svm_->dgpu_alt_aperture->base_;
			alt_size = VOID_PTRS_SUB(svm_->dgpu_alt_aperture->limit_,
				svm_->dgpu_alt_aperture->base_) + 1;
            /*
			err = mm_set_memory_policy(process_apertures[i].gpu_id,
						    svm_->disable_cache ?
						    KFD_IOC_CACHE_POLICY_COHERENT :
						    KFD_IOC_CACHE_POLICY_NONCOHERENT,
						    KFD_IOC_CACHE_POLICY_COHERENT,
						    alt_base, alt_size);
                            */
			if (err) {
				ERROR("Failed to set mem policy for GPU [0x%x]\n",
				       process_apertures[i].gpu_id);
				ret = DEVICE_STATUS_ERROR;
				goto set_memory_policy_failed;
			}
		}

#if 0
        uint32_t svm_base_lo;
        uint32_t svm_base_hi;
        uint32_t svm_top_lo;
        uint32_t svm_top_hi;

        svm_base_hi = (uint32_t)(((uint64_t)(svm_base) & 0xFFFFFFFF00000000UL) >> 32) ;
        svm_base_lo = (uint32_t)(((uint64_t)(svm_base) & 0x00000000FFFFFFFFUL)) ;
        svm_top_hi = (uint32_t)(((uint64_t)(svm_limit) & 0xFFFFFFFF00000000UL) >> 32) ;
        svm_top_lo = (uint32_t)((uint64_t)(svm_limit) & 0x00000000FFFFFFFFUL);

        // cmdio* csi = GetDeviceCSI();
        cmd_write_register(mmMMU_GLB_REG_ADDR(MMU_SVM_RANGE_BASE_LO), svm_base_lo );
        cmd_write_register(mmMMU_GLB_REG_ADDR(MMU_SVM_RANGE_BASE_HI), svm_base_hi );
        cmd_write_register(mmMMU_GLB_REG_ADDR(MMU_SVM_RANGE_TOP_LO), svm_top_lo );
        cmd_write_register(mmMMU_GLB_REG_ADDR(MMU_SVM_RANGE_TOP_HI), svm_top_hi );

        reg_CP_GLB_STATUS cp_glb_status;
        cmd_read_register(mmCP_GLB_REG_ADDR(CP_GLB_STATUS), &cp_glb_status.val );
        while(cp_glb_status.bits.active != 1 ) {
            sleep(1);
            cmd_read_register(mmCP_GLB_REG_ADDR(CP_GLB_STATUS), &cp_glb_status.val );
        }

        // cmd_write_register(mmSTART_REG, 0x5f5f );
        // uint32_t start_flag = 0;
        // cmd_read_register(mmSTART_REG, &start_flag );
        // assert(start_flag == 0x5f5f);
        // after mmSTART, the ComputeCore will setup Memory
#endif
	} else {
        assert("PPU only support svm for now\n");
    }

	cpuvm_aperture->align_ = PAGE_SIZE;
	cpuvm_aperture->limit_ = (void *)0x7FFFFFFFFFFF; /* 2^47 - 1 */

	for (gpu_mem_id = 0; (uint32_t)gpu_mem_id < gpu_mem_count_; gpu_mem_id++) {
        /*
		if (!topology_is_svm_needed(gpu_mem_[gpu_mem_id].device_id))
			continue;
            */
		gpu_mem_[gpu_mem_id]->mmio_aperture.base = map_mmio(
				gpu_mem_[gpu_mem_id]->node_id,
				gpu_mem_[gpu_mem_id]->gpu_id,
				-1/*kfd_fd*/);
		if (gpu_mem_[gpu_mem_id]->mmio_aperture.base)
			gpu_mem_[gpu_mem_id]->mmio_aperture.size = PAGE_SIZE - 1;
		else
			ERROR("Failed to map remapped mmio page on gpu_mem %d\n",
					gpu_mem_id);
	}

	// free(process_apertures);
	return ret;

invalid_gpu_id:
init_svm_failed:
acquire_vm_failed:
set_memory_policy_failed:
	// free(all_gpu_id_array);
	// all_gpu_id_array = NULL;
get_aperture_ioctl_failed:
	// free(process_apertures);
sysfs_parse_failed:
	mm_destroy_process_apertures();
	return ret;
}


void MemMgr::mm_destroy_process_apertures(void)
{
	//if (gpu_mem_) {
		// free(gpu_mem_);
		// gpu_mem = NULL;
		gpu_mem_.resize(0);
	//}
	gpu_mem_count_ = 0;
}

// void *fmm_allocate_scratch(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes)
void *MemMgr::mm_allocate_scratch(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes)
{
	Vma *aperture_phy;
	struct ioctl_set_scratch_backing_va_args args = {0};
	int32_t gpu_mem_id;
	void *mem = NULL;
	uint64_t aligned_size = alignUp(MemorySizeInBytes, SCRATCH_ALIGN);

	/* Retrieve gpu_mem id according to gpu_id */
	gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
    // FIXME schi update for apu nodes don't have gpu_mem_id
	if (gpu_mem_id >= 0) {
	    aperture_phy = gpu_mem_[gpu_mem_id]->scratch_physical;
	    if (aperture_phy->base_ || aperture_phy->limit_)
		    /* Scratch was already allocated for this GPU */
		    return NULL;

	    /* Allocate address space for scratch backing, 64KB aligned */
	    // FIXME we will manager both cpu node(apu) and gpu_node memory
        // assert(is_dgpu && "I think only dgpu have gpu_mem");
        ScopedAcquire<KernelMutex> lock(&svm_->dgpu_aperture->mm_lock_);
		mem = svm_->dgpu_aperture->aperture_allocate_area_aligned(
			address, aligned_size, SCRATCH_ALIGN);

	    /* Remember scratch backing aperture for later */
	    aperture_phy->base_ = mem;
	    aperture_phy->limit_ = VOID_PTR_ADD(mem, aligned_size-1);
	    aperture_phy->is_cpu_accessible_ = true;

	    /* Program SH_HIDDEN_PRIVATE_BASE */
	    args.gpu_id = gpu_id;
	    args.va_addr = ((uint64_t)mem) >> 16;

	    if (cmd_set_scratch_backing_va(&args)) {
		    mm_release_scratch(gpu_id);
		    return NULL;
	    }
	} else {
        // APU don't have gpu_mem_t
		uint64_t aligned_padded_size = aligned_size + SCRATCH_ALIGN - PAGE_SIZE;
		void *padded_end, *aligned_start, *aligned_end;

		if (address) return NULL;

		mem = mmap(0, aligned_padded_size,
			   PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,
			   -1, 0);
		if (!mem) return NULL;
		/* align start and unmap padding */
		padded_end = VOID_PTR_ADD(mem, aligned_padded_size);
		aligned_start = (void *)alignUp((uint64_t)mem, SCRATCH_ALIGN);
		aligned_end = VOID_PTR_ADD(aligned_start, aligned_size);
		if (aligned_start > mem)
			munmap(mem, VOID_PTRS_SUB(aligned_start, mem));
		if (aligned_end < padded_end)
			munmap(aligned_end,
			       VOID_PTRS_SUB(padded_end, aligned_end));
		mem = aligned_start;
	}


	return mem;
}

// *fmm_allocate_device
void *MemMgr::mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes, HsaMemFlags flags)
{
    Vma* aperture;
	int32_t gpu_mem_id;
	uint32_t ioc_flags = KFD_IOC_ALLOC_MEM_FLAGS_VRAM;
	uint64_t size, mmap_offset;
	void *mem;
	MemObj *vm_obj = NULL;

	/* Retrieve gpu_mem id according to gpu_id */
	gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
	if (gpu_mem_id < 0)
		return NULL;

	size = MemorySizeInBytes;

	if (flags.ui32.HostAccess) ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC;

	ioc_flags |= mm_translate_hsa_to_ioc_flags(flags);

	if (topo_->topology_is_svm_needed(topo_->get_device_id_by_gpu_id(gpu_id))) {
		aperture = svm_->dgpu_aperture.get();
		if (flags.ui32.AQLQueueMemory)
			size = MemorySizeInBytes * 2;
	} else {
		aperture = gpu_mem_[gpu_mem_id]->gpuvm_aperture;
	}

	if (!flags.ui32.CoarseGrain || svm_->disable_cache)
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_COHERENT;

	mem = aperture->mm_allocate_device(gpu_id, address, size, &mmap_offset,
				    ioc_flags, &vm_obj);

	if (mem && vm_obj) {
        {
            ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
		    /* Store memory allocation flags, not ioc flags */
		    vm_obj->flags = flags.Value;
		    topo_->gpuid_to_nodeid(gpu_id, &vm_obj->node_id);
        }
	}

	if (mem) {
		int map_fd = gpu_mem_[gpu_mem_id]->drm_render_fd;
		int prot = flags.ui32.HostAccess ? PROT_READ | PROT_WRITE :
					PROT_NONE;
		int flag = flags.ui32.HostAccess ? MAP_SHARED | MAP_FIXED :
					MAP_PRIVATE|MAP_FIXED;
        /*
		void *ret = mmap(mem, MemorySizeInBytes, prot, flag,
					map_fd, mmap_offset);
                    */

	    struct ioctl_mmap_args mmap_args = {0};
        mmap_args.start = (uint64_t)mem;
        mmap_args.length = MemorySizeInBytes;
        mmap_args.prot = prot;
        mmap_args.flags = flag;
        mmap_args.fd = map_fd;
        mmap_args.offset = mmap_offset;

	    cmd_mmap(&mmap_args);
		void *ret = (void*)mmap_args.start;


		if (ret == MAP_FAILED) {
			aperture->release(vm_obj);
			return NULL;
		}
		/*
		 * This madvise() call is needed to avoid additional references
		 * to mapped BOs in child processes that can prevent freeing
		 * memory in the parent process and lead to out-of-memory
		 * conditions.
		 */
		madvise(mem, MemorySizeInBytes, MADV_DONTFORK);
	}

	return mem;
}

// void *fmm_allocate_doorbell
void *MemMgr::mm_allocate_doorbell(uint32_t gpu_id, uint64_t MemorySizeInBytes,
			    uint64_t doorbell_mmap_offset)
{
	Vma *aperture;
	int32_t gpu_mem_id;
	uint32_t ioc_flags;
	void *mem;
	MemObj *vm_obj = NULL;

	/* Retrieve gpu_mem id according to gpu_id */
	gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
	if (gpu_mem_id < 0)
		return NULL;

	/* Use fine-grained aperture */
	aperture = svm_->dgpu_alt_aperture.get();
	ioc_flags = KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
		    KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
		    KFD_IOC_ALLOC_MEM_FLAGS_COHERENT;

	mem = aperture->mm_allocate_device(gpu_id, NULL, MemorySizeInBytes, NULL,
				    ioc_flags, &vm_obj);

	if (mem && vm_obj) {
		HsaMemFlags flags;

		/* Cook up some flags for storing in the VM object */
		flags.Value = 0;
		flags.ui32.NonPaged = 1;
		flags.ui32.HostAccess = 1;
		flags.ui32.Reserved = 0xBe1;

        {
            ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
		    vm_obj->flags = flags.Value;
		    topo_->gpuid_to_nodeid(gpu_id, &vm_obj->node_id);
        }
	}

	if (mem) {
	    struct ioctl_mmap_args mmap_args = {0};
        mmap_args.start = (uint64_t)mem;
        mmap_args.length = MemorySizeInBytes;
        mmap_args.prot = PROT_READ | PROT_WRITE;
        mmap_args.flags = MAP_SHARED | MAP_FIXED;
        mmap_args.fd = -1; // kdf_fd
        mmap_args.offset = doorbell_mmap_offset;
        cmd_mmap(&mmap_args);
		void *ret = (void*)mmap_args.start;
		if (ret == MAP_FAILED) {
			aperture->release(vm_obj);
			return NULL;
		}
	}

	return mem;
}

// *fmm_allocate_host_cpu(void *address, uint64_t MemorySizeInBytes,
void *MemMgr::mm_allocate_host_cpu(void *address, uint64_t MemorySizeInBytes,
				HsaMemFlags flags)
{
	void *mem = NULL;
	MemObj *vm_obj;
	int mmap_prot = PROT_READ;

	if (address)
		return NULL;

	if (flags.ui32.ExecuteAccess)
		mmap_prot |= PROT_EXEC;

	if (!flags.ui32.ReadOnly)
		mmap_prot |= PROT_WRITE;

	/* mmap will return a pointer with alignment equal to
	 * sysconf(_SC_PAGESIZE).
	 */
    /*
	struct ioctl_mmap_args mmap_args = {0};
    mmap_args.length = MemorySizeInBytes;
    mmap_args.prot = mmap_prot;
    mmap_args.flags = MAP_ANONYMOUS | MAP_PRIVATE;
    mmap_args.fd = -1;
    mmap_args.offset = 0;

    cmd_mmap(&mmap_args);

	mem =(void*)mmap_args.start;
    */

	mem = mmap(NULL, MemorySizeInBytes, mmap_prot,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (mem == MAP_FAILED)
		return NULL;

    {
        ScopedAcquire<KernelMutex> lock(&cpuvm_aperture->mm_lock_);
	    vm_obj = cpuvm_aperture->aperture_allocate_object(mem, 0,
				      MemorySizeInBytes, flags.Value);
	    if (vm_obj)
		    vm_obj->node_id = 0; /* APU systems only have one CPU node */
    }

	return mem;
}

void *MemMgr::mm_allocate_host_gpu(uint32_t node_id, void *address,
				   uint64_t MemorySizeInBytes, HsaMemFlags flags)
{
	Vma *aperture;
	void *mem;
	uint64_t mmap_offset;
	uint32_t ioc_flags;
	uint64_t size;
	int32_t gpu_drm_fd;
	uint32_t gpu_id;
	MemObj *vm_obj = NULL;

	if (!first_gpu_mem_)
		return NULL;

	gpu_id = first_gpu_mem_->gpu_id;
	gpu_drm_fd = first_gpu_mem_->drm_render_fd;

	size = MemorySizeInBytes;
	ioc_flags = 0;
	if (flags.ui32.CoarseGrain)
		aperture = svm_->dgpu_aperture.get();
	else
		aperture = svm_->dgpu_alt_aperture.get(); /* always coherent */

	if (!flags.ui32.CoarseGrain || svm_->disable_cache)
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_COHERENT;
	ioc_flags |= mm_translate_hsa_to_ioc_flags(flags);

	if (flags.ui32.AQLQueueMemory)
		size = MemorySizeInBytes * 2;

    MAKE_SCOPE_GUARD([&]() {
        if (mem && !vm_obj) {
            ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
            aperture->release_area(mem, size);
        }
    });


	// Paged memory is allocated as a userptr mapping, non-paged * memory is allocated from KFD
	if (!flags.ui32.NonPaged && svm_->userptr_for_paged_mem) {
        {
		    /* Allocate address space */
            ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
		    mem = aperture->aperture_allocate_area(address, size);
        }
		if (!mem) return nullptr;

		/* Map anonymous pages */
		if (mmap(mem, MemorySizeInBytes, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0)
		    == MAP_FAILED) {
            assert("mmap failed at mm_allocate_host_gpu");
            return nullptr;
        }

		/* Bind to NUMA node */
        /* TODO
		if (bind_mem_to_numa(node_id, mem, MemorySizeInBytes, flags))
            return;
            */

		/* Mappings in the DGPU aperture don't need to be copied on
		 * fork. This avoids MMU notifiers and evictions due to user
		 * memory mappings on fork.
		 */
		madvise(mem, MemorySizeInBytes, MADV_DONTFORK);

		/* Create userptr BO */
		mmap_offset = (uint64_t)mem;
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_USERPTR;
		vm_obj = aperture->mm_allocate_memory_object(gpu_id, mem, size,
						       &mmap_offset, ioc_flags);
		if (!vm_obj) return nullptr;
	} else {
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_GTT;
		mem =  aperture->mm_allocate_device(gpu_id, address, size,
					     &mmap_offset, ioc_flags, &vm_obj);

		if (mem && flags.ui32.HostAccess) {
			int map_fd = gpu_drm_fd;
			void *ret = mmap(mem, MemorySizeInBytes,
					 PROT_READ | PROT_WRITE,
					 MAP_SHARED | MAP_FIXED, map_fd, mmap_offset);
			if (ret == MAP_FAILED) {
				aperture->release(vm_obj);
				return NULL;
			}

			if (flags.ui32.AQLQueueMemory) {
				uint64_t my_buf_size = size / 2;

				memset(ret, 0, MemorySizeInBytes);
				mmap(VOID_PTR_ADD(mem, my_buf_size), MemorySizeInBytes,
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED | MAP_FIXED, map_fd, mmap_offset);
			}
		}
	}

	if (mem && vm_obj) {
		/* Store memory allocation flags, not ioc flags */
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
		vm_obj->flags = flags.Value;
		vm_obj->node_id = node_id;
	    return mem;
	}

	return NULL;
}

// device_status_t fmm_release(void *address)
device_status_t MemMgr::mm_release(void *address)
{
	Vma *aperture = NULL;
	MemObj *object = NULL;
	uint32_t i;

	/* Special handling for scratch memory */
	for (i = 0; i < gpu_mem_count_; i++)
		if (gpu_mem_[i]->gpu_id != NON_VALID_GPU_ID &&
		    address >= gpu_mem_[i]->scratch_physical->base_ &&
		    address <= gpu_mem_[i]->scratch_physical->limit_) {
			mm_release_scratch(gpu_mem_[i]->gpu_id);
			return DEVICE_STATUS_SUCCESS;
		}

	object = vm_find_object(address, 0, &aperture);

	if (!object)
		return DEVICE_STATUS_MEMORY_NOT_REGISTERED;

	if (aperture == cpuvm_aperture) {
		/* APU system memory */
		uint64_t size = 0;

		size = object->size;
		cpuvm_aperture->vm_remove_object(object);
		// TODO pthread_mutex_unlock(&aperture->fmm_mutex);
		munmap(address, size);
	} else {
		// TODO pthread_mutex_unlock(&aperture->fmm_mutex);

		if (aperture->release(object))
			return DEVICE_STATUS_ERROR;
/*
		if (!aperture->is_cpu_accessible)
			mm_print(gpu_mem_[i]->gpu_id);
            */
	}

	return DEVICE_STATUS_SUCCESS;
}

int MemMgr::mm_map_to_gpu(void *address, uint64_t size, uint64_t *gpuvm_address)
{
	Vma *aperture;
	MemObj *object;
	uint32_t i;
	int ret;

	/* Special handling for scratch memory */
	for (i = 0; i < gpu_mem_count_; i++)
		if (gpu_mem_[i]->gpu_id != NON_VALID_GPU_ID &&
		    address >= gpu_mem_[i]->scratch_physical->base_ &&
		    address <= gpu_mem_[i]->scratch_physical->limit_)
			return	gpu_mem_[i]->scratch_physical->mm_map_to_gpu_scratch(
                            gpu_mem_[i]->gpu_id,
							address, size);

	object = vm_find_object(address, size, &aperture);
	if (!object) {
		if (!device_->is_dgpu()) {
			/* Prefetch memory on APUs with dummy-reads */
			mm_check_user_memory(address, size);
			return 0;
		}
		ERROR("Object not found at %p\n", address);
		return -EINVAL;
	}
	/* Successful vm_find_object returns with the aperture locked */

	if (aperture == cpuvm_aperture) {
		/* Prefetch memory on APUs with dummy-reads */
		mm_check_user_memory(address, size);
		ret = 0;
	} else if (object->userptr) {
		ret = svm_->dgpu_aperture->mm_map_to_gpu_userptr(address, size, gpuvm_address, object);
	} else {
		ret = aperture->mm_map_to_gpu(address, size, object, NULL, 0);
		/* Update alternate GPUVM address only for
		 * CPU-invisible apertures on old APUs
		 */
		if (!ret && gpuvm_address && !aperture->is_cpu_accessible_)
			*gpuvm_address = VOID_PTRS_SUB(object->start, aperture->base_);
	}

	return ret;
}

int MemMgr::mm_unmap_from_gpu(void *address)
{
	Vma *aperture;
	MemObj *object;
	uint32_t i;
	int ret;

	/* Special handling for scratch memory */
	for (i = 0; i < gpu_mem_count_; i++)
		if (gpu_mem_[i]->gpu_id != NON_VALID_GPU_ID &&
		    address >= gpu_mem_[i]->scratch_physical->base_ &&
		    address <= gpu_mem_[i]->scratch_physical->limit_)
		    return gpu_mem_[i]->scratch_physical->mm_unmap_from_gpu_scratch(
                            gpu_mem_[i]->gpu_id, address);

	object = vm_find_object(address, 0, &aperture);
	if (!object)
		/* On APUs GPU unmapping of system memory is a no-op */
		return device_->is_dgpu() ? -EINVAL : 0;
	/* Successful vm_find_object returns with the aperture locked */

	if (aperture == cpuvm_aperture)
		/* On APUs GPU unmapping of system memory is a no-op */
		ret = 0;
	else
		ret = aperture->mm_unmap_from_gpu(address, NULL, 0, object);

	return ret;
}

bool MemMgr::mm_get_handle(void *address, uint64_t *handle)
{
	uint32_t i;
	Vma *aperture;
	MemObj *object;
	bool found;

	found = false;
	aperture = NULL;

	/* Find the aperture the requested address belongs to */
	for (i = 0; i < gpu_mem_count_; i++) {
		if (gpu_mem_[i]->gpu_id == NON_VALID_GPU_ID)
			continue;

		if ((address >= gpu_mem_[i]->gpuvm_aperture->base_) &&
			(address <= gpu_mem_[i]->gpuvm_aperture->limit_)) {
			aperture = gpu_mem_[i]->gpuvm_aperture;
			break;
		}
	}

	if (!aperture) {
		if ((address >= svm_->dgpu_aperture->base_) &&
			(address <= svm_->dgpu_aperture->limit_)) {
			aperture = svm_->dgpu_aperture.get();
		} else if ((address >= svm_->dgpu_alt_aperture->base_) &&
			(address <= svm_->dgpu_alt_aperture->limit_)) {
			aperture = svm_->dgpu_alt_aperture.get();
		}
	}

	if (!aperture)
		return false;

    ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	/* Find the object to retrieve the handle */
	object = aperture->vm_find_object_by_address(address, 0);
	if (object && handle) {
		*handle = object->handle;
		found = true;
	}


	return found;
}

// static int32_t nodes[1];
// FIXME remove this function and
device_status_t MemMgr::mm_get_mem_info(const void *address, HsaPointerInfo *info)
{
#if 0
	device_status_t ret = DEVICE_STATUS_SUCCESS;
	memset(info, 0, sizeof(HsaPointerInfo));


    DeviceMemoryObject* mem = GetMemoryManager()->find(MemAddrRange(reinterpret_cast<uint64_t>(address), 1));

    // FIXME on  below ugly workaround
	//	info->Type = HSA_POINTER_REGISTERED_USER;
    if (mem != nullptr) {
        // TODO workaround for now
	    info->Type = HSA_POINTER_ALLOCATED;
	    info->Node = 0;
	    info->GPUAddress = (uint64_t)mem->GetDevicePtr();
	    info->CPUAddress = (void*)mem->GetHostPtr();
	    info->SizeInBytes = mem->GetByteSize();
        info->NMappedNodes = 1;
        info->MappedNodes = (const unsigned int*)&nodes[0];
        nodes[0] = 0;
    } else {
		info->Type = HSA_POINTER_UNKNOWN;
		ret = DEVICE_STATUS_ERROR;
    }

    return ret;
#endif
	device_status_t ret = DEVICE_STATUS_SUCCESS;
	uint32_t i;
	Vma *aperture;
	MemObj *vm_obj;

	memset(info, 0, sizeof(HsaPointerInfo));

	vm_obj = vm_find_object(address, UINT64_MAX, &aperture);
	if (!vm_obj) {
		info->Type = HSA_POINTER_UNKNOWN;
		return DEVICE_STATUS_ERROR;
	}
	/* Successful vm_find_object returns with the aperture locked */

	if (vm_obj->is_imported_kfd_bo)
		info->Type = HSA_POINTER_REGISTERED_SHARED;
	else if (vm_obj->metadata)
		info->Type = HSA_POINTER_REGISTERED_GRAPHICS;
	else if (vm_obj->userptr)
		info->Type = HSA_POINTER_REGISTERED_USER;
	else
		info->Type = HSA_POINTER_ALLOCATED;

	info->Node = vm_obj->node_id;
	info->GPUAddress = (uint64_t)vm_obj->start;
	info->SizeInBytes = vm_obj->size;
	/* registered nodes */
	info->NRegisteredNodes =
		vm_obj->registered_device_id_array_size / sizeof(uint32_t);
	if (info->NRegisteredNodes && !vm_obj->registered_node_id_array) {
		vm_obj->registered_node_id_array = (uint32_t *)
			(uint32_t *)malloc(vm_obj->registered_device_id_array_size);
		/* vm_obj->registered_node_id_array allocated here will be
		 * freed whenever the registration is changed (deregistration or
		 * register to new nodes) or the memory being freed
		 */
		for (i = 0; i < info->NRegisteredNodes; i++)
			topo_->gpuid_to_nodeid(vm_obj->registered_device_id_array[i],
				&vm_obj->registered_node_id_array[i]);
	}
	info->RegisteredNodes = vm_obj->registered_node_id_array;
	/* mapped nodes */
	info->NMappedNodes =
		vm_obj->mapped_device_id_array_size / sizeof(uint32_t);
	if (info->NMappedNodes && !vm_obj->mapped_node_id_array) {
		vm_obj->mapped_node_id_array =
			(uint32_t *)malloc(vm_obj->mapped_device_id_array_size);
		/* vm_obj->mapped_node_id_array allocated here will be
		 * freed whenever the mapping is changed (unmapped or map
		 * to new nodes) or memory being freed
		 */
		for (i = 0; i < info->NMappedNodes; i++)
			topo_->gpuid_to_nodeid(vm_obj->mapped_device_id_array[i],
				&vm_obj->mapped_node_id_array[i]);
	}
	info->MappedNodes = vm_obj->mapped_node_id_array;
	info->UserData = vm_obj->user_data;

	if (info->Type == HSA_POINTER_REGISTERED_USER) {
		info->CPUAddress = vm_obj->userptr;
		info->SizeInBytes = vm_obj->userptr_size;
		info->GPUAddress += ((uint64_t)info->CPUAddress & (PAGE_SIZE - 1));
	} else if (info->Type == HSA_POINTER_ALLOCATED) {
		info->MemFlags.Value = vm_obj->flags;
		info->CPUAddress = vm_obj->start;
	}

	// pthread_mutex_unlock(&aperture->fmm_mutex);
	return ret;
}

device_status_t MemMgr::mm_set_mem_user_data(const void *mem, void *usr_data)
{
	Vma *aperture;
	MemObj *vm_obj;

	vm_obj = vm_find_object(mem, 0, &aperture);
	if (!vm_obj)
		return DEVICE_STATUS_ERROR;

	vm_obj->user_data = usr_data;

	// pthread_mutex_unlock(&aperture->fmm_mutex);
	return DEVICE_STATUS_SUCCESS;
}

device_status_t MemMgr::mm_get_aperture_base_and_limit(aperture_type_e aperture_type, uint32_t gpu_id,
			uint64_t *aperture_base, uint64_t *aperture_limit)
{
	device_status_t err = DEVICE_STATUS_ERROR;
	int32_t slot = gpu_mem_find_by_gpu_id(gpu_id);

	if (slot < 0)
		return DEVICE_STATUS_INVALID_PARAMETER;

	switch (aperture_type) {
	case FMM_GPUVM:
		if (gpu_mem_[slot]->gpuvm_aperture->aperture_is_valid()) {
			*aperture_base = (uint64_t)(gpu_mem_[slot]->gpuvm_aperture->base_);
			*aperture_limit = (uint64_t)(gpu_mem_[slot]->gpuvm_aperture->limit_);
			err = DEVICE_STATUS_SUCCESS;
        }
		break;
	case FMM_SCRATCH: {
		//if (gpu_mem_[slot]->scratch_aperture->aperture_is_valid()) {
			*aperture_base = (uint64_t)(gpu_mem_[slot]->scratch_aperture.base);
			*aperture_limit = (uint64_t)(gpu_mem_[slot]->scratch_aperture.base) +
			                  (uint64_t)(gpu_mem_[slot]->scratch_aperture.size);
			err = DEVICE_STATUS_SUCCESS;
        }
		break;
	case FMM_LDS: {
			*aperture_base = (uint64_t)(gpu_mem_[slot]->lds_aperture.base);
			*aperture_limit = (uint64_t)(gpu_mem_[slot]->lds_aperture.base) +
			                  (uint64_t)(gpu_mem_[slot]->lds_aperture.size);
			err = DEVICE_STATUS_SUCCESS;
        }
		break;
	case FMM_MMIO: {
			*aperture_base = (uint64_t)(gpu_mem_[slot]->mmio_aperture.base);
			*aperture_limit = (uint64_t)(gpu_mem_[slot]->mmio_aperture.base) +
			                  (uint64_t)(gpu_mem_[slot]->mmio_aperture.size);
			err = DEVICE_STATUS_SUCCESS;
		}
		break;
	case FMM_SVM:
		/* Report single SVM aperture, starting at base of
		 * fine-grained, ending at limit of coarse-grained
		 */
		if (svm_->dgpu_alt_aperture->aperture_is_valid()) {
			*aperture_base = (uint64_t)(svm_->dgpu_alt_aperture->base_);
			*aperture_limit = (uint64_t)(svm_->dgpu_aperture->limit_);
			err = DEVICE_STATUS_SUCCESS;
		}
		break;
	default:
		break;
	}

	return err;
}

device_status_t MemMgr::mm_register_memory(void *address, uint64_t size_in_bytes,
				  uint32_t *gpu_id_array,
				  uint32_t gpu_id_array_size,
				  bool coarse_grain)
{
	Vma *aperture = NULL;
	MemObj *object = NULL;
	device_status_t ret;

	if (gpu_id_array_size > 0 && !gpu_id_array)
		return DEVICE_STATUS_INVALID_PARAMETER;

	object = vm_find_object(address, size_in_bytes, &aperture);
	if (!object) {
		if (!device_->is_dgpu())
			/* System memory registration on APUs is a no-op */
			return DEVICE_STATUS_SUCCESS;

		/* Register a new user ptr */
		ret = mm_register_user_memory(address, size_in_bytes, &object, coarse_grain);
		if (ret != DEVICE_STATUS_SUCCESS)
			return ret;
		if (gpu_id_array_size == 0)
			return DEVICE_STATUS_SUCCESS;
		aperture = svm_->dgpu_aperture.get();
		// pthread_mutex_lock(&aperture->fmm_mutex);
        aperture->mm_lock_.Acquire();
		/* fall through for registered device ID array setup */
	} else if (object->userptr) {
		/* Update an existing userptr */
		++object->registration_count;
	}
	/* Successful vm_find_object returns with aperture locked */

	if (object->registered_device_id_array_size > 0) {
		/* Multiple registration is allowed, but not changing nodes */
		if ((gpu_id_array_size != object->registered_device_id_array_size)
			|| memcmp(object->registered_device_id_array,
					gpu_id_array, gpu_id_array_size)) {
			ERROR("Cannot change nodes in a registered addr.\n");
			// pthread_mutex_unlock(&aperture->fmm_mutex);
            aperture->mm_lock_.Release();
			return DEVICE_STATUS_MEMORY_ALREADY_REGISTERED;
		} else {
			/* Delete the new array, keep the existing one. */
			if (gpu_id_array)
				free(gpu_id_array);

			// pthread_mutex_unlock(&aperture->fmm_mutex);
            aperture->mm_lock_.Release();
			return DEVICE_STATUS_MEMORY_ALREADY_REGISTERED;
			return DEVICE_STATUS_SUCCESS;
		}
	}

	if (gpu_id_array_size > 0) {
		object->registered_device_id_array = gpu_id_array;
		object->registered_device_id_array_size = gpu_id_array_size;
		/* Registration of object changed. Lifecycle of object->
		 * registered_node_id_array terminates here. Free old one
		 * and re-allocate on next query
		 */
		if (object->registered_node_id_array) {
			free(object->registered_node_id_array);
			object->registered_node_id_array = NULL;
		}
	}

	// pthread_mutex_unlock(&aperture->fmm_mutex);
    aperture->mm_lock_.Release();
	return DEVICE_STATUS_SUCCESS;
}

#define GRAPHICS_METADATA_DEFAULT_SIZE 64
device_status_t MemMgr::mm_register_graphics_handle(uint64_t GraphicsResourceHandle,
					   HsaGraphicsResourceInfo *GraphicsResourceInfo,
					   uint32_t *gpu_id_array,
					   uint32_t gpu_id_array_size)
{
	struct ioctl_get_dmabuf_info_args infoArgs = {0};
	struct ioctl_import_dmabuf_args importArgs = {0};
	struct ioctl_free_memory_args freeArgs = {0};
	Vma *aperture;
	MemObj *obj;
	void *metadata;
	void *mem, *aperture_base;
	int32_t gpu_mem_id;
	int r;
	device_status_t status = DEVICE_STATUS_ERROR;
	static const uint64_t IMAGE_ALIGN = 256*1024;

	if (gpu_id_array_size > 0 && !gpu_id_array)
		return DEVICE_STATUS_INVALID_PARAMETER;

	infoArgs.dmabuf_fd = GraphicsResourceHandle;
	infoArgs.metadata_size = GRAPHICS_METADATA_DEFAULT_SIZE;
	metadata = calloc(infoArgs.metadata_size, 1);
	if (!metadata)
		return DEVICE_STATUS_NO_MEMORY;
	infoArgs.metadata_ptr = (uint64_t)metadata;
	r = cmd_get_dmabuf_info(&infoArgs);
	if (r && infoArgs.metadata_size > GRAPHICS_METADATA_DEFAULT_SIZE) {
		/* Try again with bigger metadata */
		free(metadata);
		metadata = calloc(infoArgs.metadata_size, 1);
		if (!metadata)
			return DEVICE_STATUS_NO_MEMORY;
		infoArgs.metadata_ptr = (uint64_t)metadata;
		r = cmd_get_dmabuf_info(&infoArgs);
	}


    bool error = true;
    MAKE_SCOPE_GUARD([&]() {
        if (error)
	        free(metadata);
    });

	if (r) return status;

	/* Choose aperture based on GPU and allocate virtual address */
	gpu_mem_id = gpu_mem_find_by_gpu_id(infoArgs.gpu_id);
	if (gpu_mem_id < 0) return status;
	if (topo_->topology_is_svm_needed(gpu_mem_[gpu_mem_id]->device_id)) {
		aperture = svm_->dgpu_aperture.get();
		aperture_base = NULL;
	} else {
		aperture = gpu_mem_[gpu_mem_id]->gpuvm_aperture;
		aperture_base = aperture->base_;
	}
	// if (!aperture_is_valid(aperture->base_, aperture->limit_))
	if (!aperture->aperture_is_valid())
        return status;
    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    mem = aperture->aperture_allocate_area_aligned(NULL, infoArgs.size,
					     IMAGE_ALIGN);
    }

	if (!mem) return status;

	/* Import DMA buffer */
	importArgs.va_addr = VOID_PTRS_SUB(mem, aperture_base);
	importArgs.gpu_id = infoArgs.gpu_id;
	importArgs.dmabuf_fd = GraphicsResourceHandle;
	r = cmd_import_dmabuf(&importArgs);

    MAKE_SCOPE_GUARD([&]() {
        if (r)
	        aperture->release_area(mem, infoArgs.size);
    });

	if (r) return status;

    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    obj = aperture->aperture_allocate_object(mem, importArgs.handle,
				       infoArgs.size, infoArgs.flags);
	    if (obj) {
		    obj->metadata = metadata;
		    obj->registered_device_id_array = gpu_id_array;
		    obj->registered_device_id_array_size = gpu_id_array_size;
		    topo_->gpuid_to_nodeid(infoArgs.gpu_id, &obj->node_id);
	    }
    }

    MAKE_SCOPE_GUARD([&]() {
        if (!obj) {
	        freeArgs.handle = importArgs.handle;
	        cmd_free_memory(&freeArgs);
        }
    });

	if (!obj)
        return status;

	GraphicsResourceInfo->MemoryAddress = mem;
	GraphicsResourceInfo->SizeInBytes = infoArgs.size;
	GraphicsResourceInfo->Metadata = (void *)(unsigned long)infoArgs.metadata_ptr;
	GraphicsResourceInfo->MetadataSizeInBytes = infoArgs.metadata_size;
	GraphicsResourceInfo->Reserved = 0;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t MemMgr::mm_deregister_memory(void *address)
{
	Vma *aperture;
	MemObj *object;

	object = vm_find_object(address, 0, &aperture);
	if (!object)
		/* On APUs we assume it's a random system memory address
		 * where registration and dergistration is a no-op
		 */
		return device_->is_dgpu() ?
			DEVICE_STATUS_MEMORY_NOT_REGISTERED :
			DEVICE_STATUS_SUCCESS;
	/* Successful vm_find_object returns with aperture locked */

	if (aperture == cpuvm_aperture) {
		/* API-allocated system memory on APUs, deregistration
		 * is a no-op
		 */
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		return DEVICE_STATUS_SUCCESS;
	}

	if (object->registration_count > 1) {
		--object->registration_count;
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		return DEVICE_STATUS_SUCCESS;
	}

	if (object->metadata || object->userptr || object->is_imported_kfd_bo) {
		/* An object with metadata is an imported graphics
		 * buffer. Deregistering imported graphics buffers or
		 * userptrs means releasing the BO.
		 */
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		aperture->release(object);
		return DEVICE_STATUS_SUCCESS;
	}

	if (!object->registered_device_id_array ||
		object->registered_device_id_array_size <= 0) {
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		return DEVICE_STATUS_MEMORY_NOT_REGISTERED;
	}

	if (object->registered_device_id_array) {
		free(object->registered_device_id_array);
		object->registered_device_id_array = NULL;
		object->registered_device_id_array_size = 0;
	}
	if (object->registered_node_id_array)
		free(object->registered_node_id_array);
	object->registered_node_id_array = NULL;
	object->registration_count = 0;

	// pthread_mutex_unlock(&aperture->fmm_mutex);

	return DEVICE_STATUS_SUCCESS;
}

device_status_t MemMgr::mm_share_memory(void *MemoryAddress,
				uint64_t SizeInBytes,
				HsaSharedMemoryHandle *SharedMemoryHandle)
{
	int r = 0;
	uint32_t gpu_id = 0;
	MemObj *obj = NULL;
	Vma *aperture = NULL;
	struct ioctl_ipc_export_handle_args exportArgs = {0};
	HsaApertureInfo ApeInfo;
	HsaSharedMemoryStruct *SharedMemoryStruct = (HsaSharedMemoryStruct *)SharedMemoryHandle;

	if (SizeInBytes >= (1ULL << ((sizeof(uint32_t) * 8) + PAGE_SHIFT)))
		return DEVICE_STATUS_INVALID_PARAMETER;

	aperture = mm_find_aperture(MemoryAddress, &ApeInfo);
	if (!aperture)
		return DEVICE_STATUS_INVALID_PARAMETER;

    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    obj = aperture->vm_find_object_by_address(MemoryAddress, 0);
    }
	if (!obj)
		return DEVICE_STATUS_INVALID_PARAMETER;

	r = topo_->validate_nodeid(obj->node_id, &gpu_id);
	if (r != DEVICE_STATUS_SUCCESS)
        return DEVICE_STATUS_ERROR;
		// return r;
	if (!gpu_id && device_->is_dgpu()) {
		/* Sharing non paged system memory. Use first GPU which was
		 * used during allocation. See fmm_allocate_host_gpu()
		 */
		if (!first_gpu_mem_)
			return DEVICE_STATUS_ERROR;

		gpu_id = first_gpu_mem_->gpu_id;
	}
	exportArgs.handle = obj->handle;
	exportArgs.gpu_id = gpu_id;


	r = cmd_ipc_export_handle(&exportArgs);
	if (r)
		return DEVICE_STATUS_ERROR;

	memcpy(SharedMemoryStruct->ShareHandle, exportArgs.share_handle,
			sizeof(SharedMemoryStruct->ShareHandle));
	SharedMemoryStruct->ApeInfo = ApeInfo;
	SharedMemoryStruct->SizeInPages = (uint32_t) (SizeInBytes >> PAGE_SHIFT);
	SharedMemoryStruct->ExportGpuId = gpu_id;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t MemMgr::mm_register_shared_memory(const HsaSharedMemoryHandle *SharedMemoryHandle,
						uint64_t *SizeInBytes,
						void **MemoryAddress,
						uint32_t *gpu_id_array,
						uint32_t gpu_id_array_size)
{
	int r = 0;
	device_status_t err = DEVICE_STATUS_ERROR;
	MemObj *obj = NULL;
	void *reservedMem = NULL;
	Vma *aperture;
	struct ioctl_ipc_import_handle_args importArgs = {0};
	struct ioctl_free_memory_args freeArgs = {0};
	const HsaSharedMemoryStruct *SharedMemoryStruct =
	    (const HsaSharedMemoryStruct *)SharedMemoryHandle;

	uint64_t SizeInPages = SharedMemoryStruct->SizeInPages;

	if (gpu_id_array_size > 0 && !gpu_id_array)
		return DEVICE_STATUS_INVALID_PARAMETER;

	memcpy(importArgs.share_handle, SharedMemoryStruct->ShareHandle,
			sizeof(importArgs.share_handle));
	importArgs.gpu_id = SharedMemoryStruct->ExportGpuId;

	aperture = mm_get_aperture(SharedMemoryStruct->ApeInfo);

    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    reservedMem = aperture->aperture_allocate_area(NULL,
			(SizeInPages << PAGE_SHIFT));
    }
	if (!reservedMem) {
		err = DEVICE_STATUS_NO_MEMORY;
		goto err_free_buffer;
	}

	importArgs.va_addr = (uint64_t)reservedMem;
	r = cmd_ipc_import_handle(&importArgs);
	if (r) {
		err = DEVICE_STATUS_ERROR;
		goto err_import;
	}

    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    obj = aperture->aperture_allocate_object(reservedMem, importArgs.handle,
				       (SizeInPages << PAGE_SHIFT),
				       0);
	    if (!obj) {
		    err = DEVICE_STATUS_NO_MEMORY;
		    goto err_free_mem;
        }
	}

	if (importArgs.mmap_offset) {
		int32_t gpu_mem_id = gpu_mem_find_by_gpu_id(importArgs.gpu_id);
		int map_fd;
		void *ret;

		if (gpu_mem_id < 0) {
			err = DEVICE_STATUS_ERROR;
			goto err_free_obj;
		}
		obj->node_id = gpu_mem_[gpu_mem_id]->node_id;
		map_fd = gpu_mem_[gpu_mem_id]->drm_render_fd;
		ret = mmap(reservedMem, (SizeInPages << PAGE_SHIFT),
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_FIXED, map_fd, importArgs.mmap_offset);
		if (ret == MAP_FAILED) {
			err = DEVICE_STATUS_ERROR;
			goto err_free_obj;
		}
	}

	*MemoryAddress = reservedMem;
	*SizeInBytes = (SizeInPages << PAGE_SHIFT);

	if (gpu_id_array_size > 0) {
		obj->registered_device_id_array = gpu_id_array;
		obj->registered_device_id_array_size = gpu_id_array_size;
	}
	obj->is_imported_kfd_bo = true;

	return DEVICE_STATUS_SUCCESS;

err_free_obj:
	// pthread_mutex_lock(&aperture->fmm_mutex);
	aperture->vm_remove_object(obj);
err_free_mem:
	aperture->release_area(reservedMem, (SizeInPages << PAGE_SHIFT));
	// pthread_mutex_unlock(&aperture->fmm_mutex);
err_free_buffer:
	freeArgs.handle = importArgs.handle;
	cmd_free_memory(&freeArgs);
err_import:
	return err;
}

/*
 * This function unmaps all nodes on current mapped nodes list that are not included on nodes_to_map
 * and maps nodes_to_map
 */
// TODO schi ,  we only need to map system memory usrptr to GPU for pasim
device_status_t MemMgr::mm_map_to_gpu_nodes(void *address, uint64_t size,
		uint32_t *nodes_to_map, uint64_t num_of_nodes,
		uint64_t *gpuvm_address)
{
	Vma *aperture;
	MemObj *object;
	uint32_t i;
	uint32_t *registered_node_id_array, registered_node_id_array_size;
	device_status_t ret = DEVICE_STATUS_ERROR;
	int retcode = 0;

	if (!num_of_nodes || !nodes_to_map || !address)
		return DEVICE_STATUS_INVALID_PARAMETER;

	object = vm_find_object(address, size, &aperture);
	if (!object)
		return DEVICE_STATUS_ERROR;
	/* Successful vm_find_object returns with aperture locked */

	/* APU memory is not supported by this function */
	if (aperture == cpuvm_aperture || !aperture->is_cpu_accessible_) {
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		return DEVICE_STATUS_ERROR;
	}

	/* For userptr, we ignore the nodes array and map all registered nodes.
	 * This is to simply the implementation of allowing the same memory
	 * region to be registered multiple times.
	 */
    //FIXME
	if (object->userptr) {
		retcode = svm_->dgpu_aperture->mm_map_to_gpu_userptr(address, size,
					gpuvm_address, object);
		// pthread_mutex_unlock(&aperture->fmm_mutex);
		return retcode ? DEVICE_STATUS_ERROR : DEVICE_STATUS_SUCCESS;
	}

	/* Verify that all nodes to map are registered already */
	registered_node_id_array = device_->all_gpu_id_array;
	registered_node_id_array_size = device_->all_gpu_id_array_size;
	if (object->registered_device_id_array_size > 0 &&
			object->registered_device_id_array) {
		registered_node_id_array = object->registered_device_id_array;
		registered_node_id_array_size = object->registered_device_id_array_size;
	}
	for (i = 0 ; i < num_of_nodes; i++) {
		if (!id_in_array(nodes_to_map[i], registered_node_id_array,
					registered_node_id_array_size)) {
			// pthread_mutex_unlock(&aperture->fmm_mutex);
			return DEVICE_STATUS_ERROR;
		}
	}

	/* Unmap buffer from all nodes that have this buffer mapped that are not included on nodes_to_map array */
	if (object->mapped_device_id_array_size > 0) {
		uint32_t temp_node_id_array[object->mapped_device_id_array_size];
		uint32_t temp_node_id_array_size = 0;

		for (i = 0 ; i < object->mapped_device_id_array_size / sizeof(uint32_t); i++) {
			if (!id_in_array(object->mapped_device_id_array[i],
					nodes_to_map,
					num_of_nodes*sizeof(uint32_t)))
				temp_node_id_array[temp_node_id_array_size++] =
					object->mapped_device_id_array[i];
		}
		temp_node_id_array_size *= sizeof(uint32_t);

		if (temp_node_id_array_size) {
			ret = (device_status_t)aperture->mm_unmap_from_gpu(address,
					temp_node_id_array,
					temp_node_id_array_size,
					object);
			if (ret != DEVICE_STATUS_SUCCESS) {
				// pthread_mutex_unlock(&aperture->fmm_mutex);
				return ret;
			}
		}
	}

	/* Remove already mapped nodes from nodes_to_map
	 * to generate the final map list
	 */
	uint32_t map_node_id_array[num_of_nodes];
	uint32_t map_node_id_array_size = 0;

	for (i = 0; i < num_of_nodes; i++) {
		if (!id_in_array(nodes_to_map[i],
				object->mapped_device_id_array,
				object->mapped_device_id_array_size))
			map_node_id_array[map_node_id_array_size++] =
				nodes_to_map[i];
	}

	if (map_node_id_array_size)
		retcode = aperture->mm_map_to_gpu(address, size, object,
				map_node_id_array,
				map_node_id_array_size * sizeof(uint32_t));

	// pthread_mutex_unlock(&aperture->fmm_mutex);

	if (retcode != 0)
		return DEVICE_STATUS_ERROR;

	return DEVICE_STATUS_SUCCESS;
}

/* This is a special funcion that should be called only from the child process
 * after a fork(). This will clear all vm_objects and mmaps duplicated from
 * the parent.
 */
void MemMgr::mm_clear_all_mem(void)
{
	uint32_t i;
	void *map_addr;

    ioctl_close_drm_args args = {0};
	/* Close render node FDs. The child process needs to open new ones */
	for (i = 0; i <= DRM_LAST_RENDER_NODE - DRM_FIRST_RENDER_NODE; i++)
		if (drm_render_fds[i]) {
            args.handle = drm_render_fds[i];
			cmd_close_drm(&args);
			drm_render_fds[i] = 0;
		}

	cpuvm_aperture->mm_clear_aperture();
	svm_->apertures[SVM_DEFAULT]->mm_clear_aperture();
	svm_->apertures[SVM_COHERENT]->mm_clear_aperture();

	if (dgpu_shared_aperture_limit_) {
		/* Use the same dgpu range as the parent. If failed, then set
		 * is_dgpu_mem_init to false. Later on dgpu_mem_init will try
		 * to get a new range
		 */
		map_addr = mmap(dgpu_shared_aperture_base_, (uint64_t)(dgpu_shared_aperture_limit_)-
			(uint64_t)(dgpu_shared_aperture_base_) + 1, PROT_NONE,
			MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE | MAP_FIXED, -1, 0);

		if (map_addr == MAP_FAILED) {
			munmap(dgpu_shared_aperture_base_,
				   (uint64_t)(dgpu_shared_aperture_limit_) -
				   (uint64_t)(dgpu_shared_aperture_base_) + 1);

			dgpu_shared_aperture_base_ = NULL;
			dgpu_shared_aperture_limit_ = NULL;
		}
	}
/*
 * TODO
	if (all_gpu_id_array)
		free(all_gpu_id_array);

	all_gpu_id_array_size = 0;
	all_gpu_id_array = NULL;
*/
	/* Nothing is initialized. */
	//if (!gpu_mem_)
    //		return;

	for (i = 0; i < gpu_mem_count_; i++) {
		gpu_mem_[i]->gpuvm_aperture->mm_clear_aperture();
		gpu_mem_[i]->scratch_physical->mm_clear_aperture();
	}
/* TODO
	gpu_mem_count_ = 0;
	free(gpu_mem);
	gpu_mem = NULL;
    */
}

int32_t MemMgr::gpu_mem_find_by_gpu_id(uint32_t gpu_id)
{
	uint32_t i;

	for (i = 0 ; i < gpu_mem_count_ ; i++)
		if (gpu_mem_[i]->gpu_id == gpu_id)
			return i;

	return -1;
}

std::shared_ptr<GpuMemory> MemMgr::get_gpu_mem(uint32_t gpu_id)
{
	uint32_t i;
	for (i = 0 ; i < gpu_mem_count_ ; i++)
		if (gpu_mem_[i]->gpu_id == gpu_id)
            return gpu_mem_[i];
    return nullptr;

}

device_status_t MemMgr::init_mmap_apertures(uint64_t base, uint64_t limit,
					 uint32_t align, uint32_t guard_pages)
{
	void *addr;

	if (align > (uint32_t)PAGE_SIZE) {
		/* This should never happen. Alignment constraints
		 * only apply to old GPUs that don't support 48-bit
		 * virtual addresses.
		 */
		INFO("Falling back to reserved SVM apertures due to alignment contraints.\n");
		return DEVICE_STATUS_ERROR;
	}

	/* Set up one SVM aperture */
	svm_->apertures[SVM_DEFAULT]->base_  = (void *)base;
	svm_->apertures[SVM_DEFAULT]->limit_ = (void *)limit;
	svm_->apertures[SVM_DEFAULT]->align_ = align;
	svm_->apertures[SVM_DEFAULT]->guard_pages_ = guard_pages;
	svm_->apertures[SVM_DEFAULT]->is_cpu_accessible_ = true;
	// svm_->apertures[SVM_DEFAULT]->ops = &mmap_aperture_ops;

	svm_->apertures[SVM_DEFAULT]->init_allocator();

	svm_->apertures[SVM_COHERENT]->base_ = svm_->apertures[SVM_COHERENT]->limit_ = NULL;

	svm_->apertures[SVM_COHERENT]->init_allocator();

	/* Try to allocate one page. If it fails, we'll fall back to
	 * managing our own reserved address range.
	 */
	addr = svm_->apertures[SVM_DEFAULT]->aperture_allocate_area(NULL, PAGE_SIZE);
	if (addr) {
		svm_->apertures[SVM_DEFAULT]->release_area(addr, PAGE_SIZE);

		svm_->dgpu_aperture = svm_->dgpu_alt_aperture = svm_->apertures[SVM_DEFAULT];
		INFO("Initialized unreserved SVM apertures: %p - %p\n",
			svm_->apertures[SVM_DEFAULT]->base_,
			svm_->apertures[SVM_DEFAULT]->limit_);
	} else {
		INFO("Failed to allocate unreserved SVM address space.\n");
		INFO("Falling back to reserved SVM apertures.\n");
	}

	return addr ? DEVICE_STATUS_SUCCESS : DEVICE_STATUS_ERROR;
}

void *MemMgr::reserve_address(void *addr, unsigned long long int len)
{
	void *ret_addr;

	if (len <= 0)
		return NULL;

	// ret_addr = mmap(addr, len, PROT_EXEC | PROT_READ | PROT_WRITE,
	ret_addr = mmap(addr, len, PROT_NONE,
				 MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE, -1, 0);
	if (ret_addr == MAP_FAILED)
		return NULL;

	return ret_addr;
}


void *MemMgr::map_mmio(uint32_t node_id, uint32_t gpu_id, int mmap_fd)
{
	void *mem;
	Vma *aperture = svm_->dgpu_alt_aperture.get();
	uint32_t ioc_flags;
	MemObj *vm_obj = NULL;
	HsaMemFlags flags;
	void *ret;
	uint64_t mmap_offset;

	/* Allocate physical memory and vm object*/
	ioc_flags = KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP |
		KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
		KFD_IOC_ALLOC_MEM_FLAGS_COHERENT;
	mem = aperture->mm_allocate_device(gpu_id, NULL, PAGE_SIZE,
			&mmap_offset, ioc_flags, &vm_obj);

	if (!mem || !vm_obj)
		return NULL;

	flags.Value = 0;
	flags.ui32.NonPaged = 1;
	flags.ui32.HostAccess = 1;
	flags.ui32.Reserved = 0;
    {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
	    vm_obj->flags = flags.Value;
	    vm_obj->node_id = node_id;
    }

	/* Map for CPU access*/
    struct ioctl_mmap_args mmap_args = {0};
    mmap_args.start = (uint64_t)mem;
    mmap_args.length = PAGE_SIZE;
    mmap_args.prot = PROT_READ | PROT_WRITE;
    mmap_args.flags = MAP_SHARED | MAP_FIXED;
    mmap_args.fd = -1;
    mmap_args.offset = mmap_offset;
    // FIXME cmd_mmap(&mmap_args);
    /*
	ret = mmap(mem, PAGE_SIZE,
			 PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_FIXED, mmap_fd,
			 mmap_offset);
	if (ret == MAP_FAILED) {
		aperture->release(vm_obj);
		return NULL;
	}
             */

	/* Map for GPU access*/
	if (mm_map_to_gpu(mem, PAGE_SIZE, NULL)) {
		aperture->release(vm_obj);
		return NULL;
	}

	return mem;
}

void MemMgr::release_mmio(void)
{
	uint32_t gpu_mem_id;

	for (gpu_mem_id = 0; (uint32_t)gpu_mem_id < gpu_mem_count_; gpu_mem_id++) {
		if (!gpu_mem_[gpu_mem_id]->mmio_aperture.base)
			continue;
		mm_unmap_from_gpu(gpu_mem_[gpu_mem_id]->mmio_aperture.base);
		munmap(gpu_mem_[gpu_mem_id]->mmio_aperture.base, PAGE_SIZE);
		mm_release(gpu_mem_[gpu_mem_id]->mmio_aperture.base);
	}
}

int MemMgr::open_drm_render_device(int minor)
{
	int index;

	if (minor < DRM_FIRST_RENDER_NODE || minor > DRM_LAST_RENDER_NODE) {
		ERROR("DRM render minor %d out of range [%d, %d]\n", minor,
		       DRM_FIRST_RENDER_NODE, DRM_LAST_RENDER_NODE);
		return -EINVAL;
	}


	index = minor - DRM_FIRST_RENDER_NODE;
	/* If the render node was already opened, keep using the same FD */
	if (drm_render_fds[index])
		return drm_render_fds[index];

    ioctl_open_drm_args drm_args = {0};
    drm_args.drm_render_minor = minor;
    cmd_open_drm(&drm_args);

	drm_render_fds[index] = drm_args.drm_fd;

	return drm_args.drm_fd;
}

device_status_t MemMgr::acquire_vm(uint32_t gpu_id, int fd)
{
	struct ioctl_acquire_vm_args args;

	args.gpu_id = gpu_id;
	args.drm_fd = fd;
	INFO("acquiring VM for %x using %d\n", gpu_id, fd);
	if (cmd_acquire_vm(&args)) {
		ERROR("AMDKFD_IOC_ACQUIRE_VM failed\n");
		return DEVICE_STATUS_ERROR;
	}

	return DEVICE_STATUS_SUCCESS;
}

// void fmm_release_scratch(uint32_t gpu_id)
void MemMgr::mm_release_scratch(uint32_t gpu_id)
{
	int32_t gpu_mem_id;
	uint64_t size;
	MemObj *obj;
	Vma *aperture;

	gpu_mem_id = gpu_mem_find_by_gpu_id(gpu_id);
	if (gpu_mem_id < 0)
		return;

	aperture = gpu_mem_[gpu_mem_id]->scratch_physical;

	size = VOID_PTRS_SUB(aperture->limit_, aperture->base_) + 1;

	if (device_->is_dgpu()) {
		/* unmap and remove all remaining objects */
        {
            ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
            for(auto obj = aperture->mem_objs.begin(); obj != aperture->mem_objs.end(); obj++) {
			    void *obj_addr = (*obj)->start;
			    aperture->mm_unmap_from_gpu_scratch(gpu_id, obj_addr);

            }

        }
            // ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
			// aperture->mm_unmap_from_gpu_scratch(gpu_id, obj_addr);

		/* release address space */
        {
            ScopedAcquire<KernelMutex> lock(&svm_->dgpu_aperture->mm_lock_);
		    svm_->dgpu_aperture->release_area(
				      gpu_mem_[gpu_mem_id]->scratch_physical->base_,
				      size);
        }
	} else
		/* release address space */
		munmap(gpu_mem_[gpu_mem_id]->scratch_physical->base_, size);

	/* invalidate scratch backing aperture */
	gpu_mem_[gpu_mem_id]->scratch_physical->base_ = NULL;
	gpu_mem_[gpu_mem_id]->scratch_physical->limit_ = NULL;
}

uint32_t MemMgr::mm_translate_hsa_to_ioc_flags(HsaMemFlags flags)
{
	uint32_t ioc_flags = 0;

	if (flags.ui32.AQLQueueMemory)
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_AQL_QUEUE_MEM;
	if (!flags.ui32.ReadOnly)
		ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE;
	/* TODO: Since, ROCr interfaces doesn't allow caller to set page
	 * permissions, mark all user allocations with exec permission.
	 * Check for flags.ui32.ExecuteAccess once ROCr is ready.
	 */
	ioc_flags |= KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE;
	return ioc_flags;
}

/* vm_find_object - Find a VM object in any aperture
 *
 * @addr: VM address of the object
 * @size: size of the object, 0 means "don't care",
 *        UINT64_MAX means addr can match any address within the object
 * @out_aper: Vma where the object was found
 *
 * Returns a pointer to the object if found, NULL otherwise. If an
 * object is found, this function returns with the
 * (*out_aper)->fmm_mutex locked.
 */
MemObj *MemMgr::vm_find_object(const void *addr, uint64_t size,
				   Vma **out_aper)
{
	Vma *aper = NULL;
	bool range = (size == UINT64_MAX);
	bool userptr = false;
	MemObj *obj = NULL;
	uint32_t i;

	for (i = 0; i < gpu_mem_count_; i++)
		if (gpu_mem_[i]->gpu_id != NON_VALID_GPU_ID &&
		    addr >= gpu_mem_[i]->gpuvm_aperture->base_ &&
		    addr <= gpu_mem_[i]->gpuvm_aperture->limit_) {
			aper = gpu_mem_[i]->gpuvm_aperture;
			break;
		}

	if (!aper) {
		if (!svm_->dgpu_aperture)
			goto no_svm;

		if ((addr >= svm_->dgpu_aperture->base_) &&
		    (addr <= svm_->dgpu_aperture->limit_))
			aper = svm_->dgpu_aperture.get();
		else if ((addr >= svm_->dgpu_alt_aperture->base_) &&
			 (addr <= svm_->dgpu_alt_aperture->limit_))
			aper = svm_->dgpu_alt_aperture.get();
		else {
			aper = svm_->dgpu_aperture.get();
			userptr = true;
		}
	}

    {
        ScopedAcquire<KernelMutex> lock(&aper->mm_lock_);
	    if (range) {
		    /* mmap_apertures can have userptrs in them. Try to
		    * look up addresses as userptrs first to sort out any
		    * ambiguity of multiple overlapping mappings at
		    * different GPU addresses.
		    */
		    if (userptr) //  || aper->ops == &mmap_aperture_ops)
			    obj = aper->vm_find_object_by_userptr_range(addr);
		    if (!obj && !userptr)
			    obj = aper->vm_find_object_by_address_range(addr);
	    } else {
		    if (userptr) //  || aper->ops == &mmap_aperture_ops)
			    obj = aper->vm_find_object_by_userptr(addr, size);
		    if (!obj && !userptr) {
			    long page_offset = (long)addr & (PAGE_SIZE-1);
			    const void *page_addr = (const uint8_t *)addr - page_offset;

			    obj = aper->vm_find_object_by_address(page_addr, 0);
			    /* If we find a userptr here, it's a match on
			    * the aligned GPU address. Make sure that the
			    * page offset and size match too.
			    */
			    if (obj && obj->userptr &&
			        (((long)obj->userptr & (PAGE_SIZE - 1)) != page_offset ||
			        (size && size != obj->userptr_size)))
				    obj = NULL;
		    }
	    }
    }

no_svm:
	if (!obj && !device_->is_dgpu()) {
		/* On APUs try finding it in the CPUVM aperture */
		aper = cpuvm_aperture;

        ScopedAcquire<KernelMutex> lock(&aper->mm_lock_);
		if (range)
			obj = aper->vm_find_object_by_address_range(addr);
		else
			obj = aper->vm_find_object_by_address(addr, 0);
	}

	if (obj) {
		*out_aper = aper;
		return obj;
	}

	return NULL;
}

// fmm_check_user_memory(const void *addr, uint64_t size)
uint8_t MemMgr::mm_check_user_memory(const void *addr, uint64_t size)
{
	volatile const uint8_t *ptr = static_cast<volatile const uint8_t*>(addr);
	volatile const uint8_t *end = ptr + size;
	uint8_t sum = 0;

	/* Access every page in the buffer to make sure the mapping is
	 * valid. If it's not, it will die with a segfault that's easy
	 * to debug.
	 */
	for (; ptr < end; ptr = (volatile const uint8_t *)alignUp(ptr + 1, PAGE_SIZE))
		sum += *ptr;

	return sum;
}

device_status_t MemMgr::mm_register_user_memory(void *addr, uint64_t size,
				  MemObj **obj_ret, bool coarse_grain)
{
	Vma *aperture = svm_->dgpu_aperture.get();
	uint32_t page_offset = (uint64_t)addr & (PAGE_SIZE-1);
	uint64_t aligned_addr = (uint64_t)addr - page_offset;
	uint64_t aligned_size = alignUp(page_offset + size, PAGE_SIZE);
	void *svm_addr;
	uint32_t gpu_id;
	MemObj *obj;

	/* Find first GPU for creating the userptr BO */
	if (!first_gpu_mem_)
		return DEVICE_STATUS_ERROR;

	gpu_id = first_gpu_mem_->gpu_id;

	/* Optionally check that the CPU mapping is valid */
	if (svm_->check_userptr)
		mm_check_user_memory(addr, size);

	/* Allocate BO, userptr address is passed in mmap_offset */
	svm_addr = aperture->mm_allocate_device(gpu_id, NULL, aligned_size,
			 &aligned_addr, KFD_IOC_ALLOC_MEM_FLAGS_USERPTR |
			 KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
			 KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE |
			 (coarse_grain ? 0 : KFD_IOC_ALLOC_MEM_FLAGS_COHERENT), &obj);
	if (!svm_addr)
		return DEVICE_STATUS_ERROR;

	if (obj) {
        ScopedAcquire<KernelMutex> lock(&aperture->mm_lock_);
		obj->userptr = addr;
		topo_->gpuid_to_nodeid(gpu_id, &obj->node_id);
		obj->userptr_size = size;
		obj->registration_count = 1;
	} else
		return DEVICE_STATUS_ERROR;

	if (obj_ret)
		*obj_ret = obj;
	return DEVICE_STATUS_SUCCESS;
}


Vma *MemMgr::mm_find_aperture(const void *address,
						HsaApertureInfo *info)
{
	Vma *aperture = NULL;
	uint32_t i;
	HsaApertureInfo _info = { .type = HSA_APERTURE_UNSUPPORTED, .idx = 0};

	if (device_->is_dgpu()) {
		if (address >= svm_->dgpu_aperture->base_ && address <= svm_->dgpu_aperture->limit_) {
			aperture = mm_is_scratch_aperture(address);
			if (!aperture) {
				aperture = svm_->dgpu_aperture.get();
				_info.type = HSA_APERTURE_DGPU;
			}
		} else if (address >= svm_->dgpu_alt_aperture->base_ && address <= svm_->dgpu_alt_aperture->limit_) {
			aperture = svm_->dgpu_alt_aperture.get();
			_info.type = HSA_APERTURE_DGPU_ALT;
		} else {
			/* Not in SVM, it can be system memory registered by userptr */
			aperture = svm_->dgpu_aperture.get();
			_info.type = HSA_APERTURE_DGPU;
		}
	} else { /* APU */
		if (address >= svm_->dgpu_aperture->base_ && address <= svm_->dgpu_aperture->limit_) {
			aperture = svm_->dgpu_aperture.get();
			_info.type = HSA_APERTURE_DGPU;
		} else {
			/* gpuvm_aperture */
			for (i = 0; i < gpu_mem_count_; i++) {
				if ((address >= gpu_mem_[i]->gpuvm_aperture->base_) &&
					(address <= gpu_mem_[i]->gpuvm_aperture->limit_)) {
					aperture = gpu_mem_[i]->gpuvm_aperture;
					_info.type = HSA_APERTURE_GPUVM;
					_info.idx = i;
				}
			}
		}
		if (!aperture) {
			/* Not in GPUVM */
			aperture = cpuvm_aperture;
			_info.type = HSA_APERTURE_CPUVM;
		}
	}

	if (info)
		*info = _info;

	return aperture;
}

Vma *MemMgr::mm_get_aperture(HsaApertureInfo info)
{
	switch (info.type) {
	case HSA_APERTURE_DGPU:
		return svm_->dgpu_aperture.get();
	case HSA_APERTURE_DGPU_ALT:
		return svm_->dgpu_alt_aperture.get();
	case HSA_APERTURE_GPUVM:
		return gpu_mem_[info.idx]->gpuvm_aperture;
	case HSA_APERTURE_CPUVM:
		return cpuvm_aperture;
	default:
		return NULL;
	}
}

Vma *MemMgr::mm_is_scratch_aperture(const void *address)
{
	uint32_t i;

	for (i = 0; i < gpu_mem_count_; i++) {
		if (gpu_mem_[i]->gpu_id == NON_VALID_GPU_ID)
			continue;

		if ((address >= gpu_mem_[i]->scratch_physical->base_) &&
			(address <= gpu_mem_[i]->scratch_physical->limit_))
			return gpu_mem_[i]->scratch_physical;

	}
	return NULL;
}

void *MemMgr::mm_allocate_host(uint32_t node_id, void *address,
			uint64_t MemorySizeInBytes, HsaMemFlags flags)
{
	if (device_->is_dgpu())
		return mm_allocate_host_gpu(node_id, address, MemorySizeInBytes, flags);
	return mm_allocate_host_cpu(address, MemorySizeInBytes, flags);
}

device_status_t MemMgr::get_process_apertures(
	process_device_apertures **process_apertures,
	uint32_t *num_of_nodes)
{
    if (apertures_) {
        *process_apertures = apertures_;
		return DEVICE_STATUS_SUCCESS;
    }
    // refer to gpu/drm/amd/amdgpu/amdgpu_vm.c
    // refer to gpu/drm/amd/amdgpu/amdgpu_amdkfd_gpuvm.c
	struct ioctl_get_process_apertures_args args = {0};
	if (!cmd_get_process_apertures(&args)) {
		*num_of_nodes = args.num_of_nodes;
	    *process_apertures = (process_device_apertures*)args.process_device_apertures_ptr;
        apertures_ = *process_apertures;
		return DEVICE_STATUS_SUCCESS;
	}
	return DEVICE_STATUS_ERROR;
#if 0
	process_apertures->gpu_id = 1;
	process_apertures->lds_base = MAKE_LDS_APP_BASE();
	process_apertures->lds_limit = MAKE_LDS_APP_LIMIT(process_apertures->lds_base);
	// process_apertures->gpuvm_base = MAKE_GPUVM_APP_BASE(1+1);
	// process_apertures->gpuvm_limit = MAKE_GPUVM_APP_LIMIT(process_apertures->gpuvm_base);

	process_apertures->scratch_base = MAKE_SCRATCH_APP_BASE();
	process_apertures->scratch_limit = MAKE_SCRATCH_APP_LIMIT(process_apertures->scratch_base);

	process_apertures->gpuvm_base = SVM_USER_BASE;
	//process_apertures->gpuvm_limit = dev->shared_resources.gpuvm_size - 1;
	process_apertures->gpuvm_limit = SVM_MIN_VM_SIZE * 2;   // 8GB
	// process_apertures->qpd.cwsr_base = SVM_CWSR_BASE;
	// process_apertures->qpd.ib_base = SVM_IB_BASE;

	return DEVICE_STATUS_SUCCESS;
#endif
}


uint32_t PageSizeFromFlags(unsigned int pageSizeFlags)
{
	switch (pageSizeFlags) {
	case HSA_PAGE_SIZE_4KB: return 4*1024;
	case HSA_PAGE_SIZE_64KB: return 64*1024;
	case HSA_PAGE_SIZE_2MB: return 2*1024*1024;
	case HSA_PAGE_SIZE_1GB: return 1024*1024*1024;
	default:
		assert(false);
		return 4*1024;
	}
}

bool id_in_array(uint32_t id, uint32_t *ids_array,
		uint32_t ids_array_size)
{
	uint32_t i;

	for (i = 0; i < ids_array_size; i++) {
		if (id == ids_array[i])
			return true;
	}
	return false;
}

