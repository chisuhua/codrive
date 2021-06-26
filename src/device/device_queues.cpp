// #include "libhsakmt.h"
// #include "fmm.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <math.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "cmdio.h"
#include "inc/device.h"

extern int mm_map_to_gpu(void *address, uint64_t size, uint64_t *gpuvm_address);
extern void mm_release(void *address);
extern int mm_unmap_from_gpu(void *address);
extern void *mm_allocate_doorbell(uint32_t gpu_id, uint64_t MemorySizeInBytes, uint64_t doorbell_offset);
extern uint32_t get_direct_link_cpu(uint32_t gpu_node);



static unsigned int num_doorbells;
struct process_doorbells *doorbells;

const struct device_info ppu_device_info = {
    .asic_family = CHIP_PPU,
    .eop_buffer_size = 4096,
    .doorbell_size = DOORBELL_SIZE,
};


static const struct device_info *dev_lookup_table[] = {
	[CHIP_PPU] = &ppu_device_info,
};


device_status_t init_process_doorbells(unsigned int NumNodes)
{
	unsigned int i;
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	/* doorbells[] is accessed using Topology NodeId. This means doorbells[0],
	 * which corresponds to CPU only Node, might not be used
	 */
	doorbells = (process_doorbells*)malloc(NumNodes * sizeof(struct process_doorbells));
	if (!doorbells)
		return DEVICE_STATUS_NO_MEMORY;

	for (i = 0; i < NumNodes; i++) {
		doorbells[i].use_gpuvm = false;
		doorbells[i].size = 0;
		doorbells[i].mapping = NULL;
		pthread_mutex_init(&doorbells[i].mutex, NULL);
	}

	num_doorbells = NumNodes;

	return ret;
}

 const struct device_info *get_device_info_by_dev_id(uint16_t dev_id)
{
	enum asic_family_type asic;

	if (topology_get_asic_family(dev_id, &asic) != DEVICE_STATUS_SUCCESS)
		return NULL;

	return dev_lookup_table[asic];
}

static void get_doorbell_map_info(uint16_t dev_id,
				  struct process_doorbells *doorbell)
{
	const struct device_info *dev_info;

	dev_info = get_device_info_by_dev_id(dev_id);

	/*
	 * GPUVM doorbell on Tonga requires a workaround for VM TLB ACTIVE bit
	 * lookup bug. Remove ASIC check when this is implemented in amdgpu.
	 */
	doorbell->use_gpuvm = is_dgpu;  // && dev_info->asic_family !=.
	doorbell->size = DOORBELLS_PAGE_SIZE(dev_info->doorbell_size);
}

void destroy_process_doorbells(void)
{
	unsigned int i;

	if (!doorbells)
		return;

	for (i = 0; i < num_doorbells; i++) {
		if (!doorbells[i].size)
			continue;

		if (doorbells[i].use_gpuvm) {
			mm_unmap_from_gpu(doorbells[i].mapping);
			mm_release(doorbells[i].mapping);
		} else
			munmap(doorbells[i].mapping, doorbells[i].size);
	}

	free(doorbells);
	doorbells = NULL;
	num_doorbells = 0;
}

/* This is a special funcion that should be called only from the child process
 * after a fork(). This will clear doorbells duplicated from the parent.
 */
void clear_process_doorbells(void)
{
	unsigned int i;

	if (!doorbells)
		return;

	for (i = 0; i < num_doorbells; i++) {
		if (!doorbells[i].size)
			continue;

		if (!doorbells[i].use_gpuvm)
			munmap(doorbells[i].mapping, doorbells[i].size);
	}

	free(doorbells);
	doorbells = NULL;
	num_doorbells = 0;
}

static device_status_t  map_doorbell_apu(HSAuint32 NodeId, HSAuint32 gpu_id,
				      HSAuint64 doorbell_mmap_offset)
{
	uint64_t* ptr;

	struct ioctl_mmap_args mmap_args = {0};
    mmap_args.start = 0;
    mmap_args.length = doorbells[NodeId].size;
    mmap_args.prot = PROT_WRITE | PROT_READ;
    mmap_args.flags = MAP_SHARED;
    mmap_args.fd = -1;
    mmap_args.offset = doorbell_mmap_offset;

	cmd_mmap(&mmap_args);

	ptr = (uint64_t*)mmap_args.start;
	// ptr = mmap(0, doorbells[NodeId].size, PROT_READ|PROT_WRITE,
	//	   MAP_SHARED, kfd_fd, doorbell_mmap_offset);
		//		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            // FIXME
		   //MAP_SHARED, -1, doorbell_mmap_offset);

	if (ptr == MAP_FAILED)
		return DEVICE_STATUS_ERROR;

	doorbells[NodeId].mapping = ptr;

	return DEVICE_STATUS_SUCCESS;
}

static device_status_t map_doorbell_dgpu(HSAuint32 NodeId, HSAuint32 gpu_id,
				       HSAuint64 doorbell_mmap_offset)
{
	void *ptr;

	ptr = mm_allocate_doorbell(gpu_id, doorbells[NodeId].size,
				doorbell_mmap_offset);

	if (!ptr)
		return DEVICE_STATUS_ERROR;

	/* map for GPU access */
	if (mm_map_to_gpu(ptr, doorbells[NodeId].size, NULL)) {
		mm_release(ptr);
		return DEVICE_STATUS_ERROR;
	}

	doorbells[NodeId].mapping = ptr;

	return DEVICE_STATUS_SUCCESS;
	// return  map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);
}

device_status_t map_doorbell(HSAuint32 NodeId, HSAuint32 gpu_id,
				  HSAuint64 doorbell_mmap_offset)
{
	device_status_t status = DEVICE_STATUS_SUCCESS;

	pthread_mutex_lock(&doorbells[NodeId].mutex);
	if (doorbells[NodeId].size) {
		pthread_mutex_unlock(&doorbells[NodeId].mutex);
		return DEVICE_STATUS_SUCCESS;
	}

	get_doorbell_map_info(get_device_id_by_node_id(NodeId),
			      &doorbells[NodeId]);

    // TODO i use gpu_id for seperate apu and gpu node
	if (gpu_id /* schi add */ && doorbells[NodeId].use_gpuvm) {
		status = map_doorbell_dgpu(NodeId, gpu_id, doorbell_mmap_offset);
		if (status != DEVICE_STATUS_SUCCESS) {
			/* Fall back to the old method if KFD doesn't
			 * support doorbells in GPUVM
			 */
			doorbells[NodeId].use_gpuvm = false;
			status = map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);
		}
	} else
		status = map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);

	if (status != DEVICE_STATUS_SUCCESS)
		doorbells[NodeId].size = 0;

	pthread_mutex_unlock(&doorbells[NodeId].mutex);

	return status;
}

static void *allocate_exec_aligned_memory_cpu(uint32_t size)
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


void *allocate_exec_aligned_memory_gpu(uint32_t size, uint32_t align,
				       uint32_t NodeId, bool nonPaged,
				       bool DeviceLocal)
{
	void *mem;
	HSAuint64 gpu_va;
	HsaMemFlags flags;
	device_status_t ret;
	HSAuint32 cpu_id = 0;

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
		cpu_id = get_direct_link_cpu(NodeId);
		if (cpu_id == INVALID_NODEID) {
			flags.ui32.NoNUMABind = 1;
			cpu_id = 0;
		}
	}

	size = ALIGN_UP(size, align);

	ret = DeviceAllocMemory(DeviceLocal ? NodeId : cpu_id, size, flags, &mem);
	if (ret != DEVICE_STATUS_SUCCESS)
		return NULL;

	if (NodeId != 0) {
		uint32_t nodes_array[1] = {NodeId};

		if (DeviceRegisterMemoryToNodes(mem, size, 1, nodes_array) != DEVICE_STATUS_SUCCESS) {
			DeviceFreeMemory(mem, size);
			return NULL;
		}
	}

	if (DeviceMapMemoryToGPU(mem, size, &gpu_va) != DEVICE_STATUS_SUCCESS) {
		DeviceFreeMemory(mem, size);
		return NULL;
	}

	return mem;
}

void free_exec_aligned_memory_gpu(void *addr, uint32_t size, uint32_t align)
{
	size = ALIGN_UP(size, align);

	if (DeviceUnmapMemoryToGPU(addr) == DEVICE_STATUS_SUCCESS)
		DeviceFreeMemory(addr, size);
}

/*
 * Allocates memory aligned to sysconf(_SC_PAGESIZE)
 */
void *allocate_exec_aligned_memory(uint32_t size,
					  bool use_ats,
					  uint32_t NodeId,
					  bool DeviceLocal)
{
	if (!use_ats)
		return allocate_exec_aligned_memory_gpu(size, PAGE_SIZE, NodeId,
							DeviceLocal, DeviceLocal);
	return allocate_exec_aligned_memory_cpu(size);
}

void free_exec_aligned_memory(void *addr, uint32_t size, uint32_t align,
				     bool use_ats)
{
	if (!use_ats)
		free_exec_aligned_memory_gpu(addr, size, align);
	else
		munmap(addr, size);
}


