#include "libhsakmt.h"
#include "cmdio.h"
// #include "linux/kfd_ioctl.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "inc/pps.h"
#include "inc/device_tools.h"
#include "inc/memory_manager.h"
// #include "inc/memory.h"
// #include "utils/lang/debug.h"
// #include "utils/lang/string.h"
// TODO mlvm
// #include "grid_engine/cpu/aasim_reg.h"
// #include "inc/RegDef.h"
// #include "util/small_heap.h"

#if 0
device_status_t DeviceSetMemoryPolicy(uint32_t Node,
					      uint32_t DefaultPolicy,
					      uint32_t AlternatePolicy,
					      void *MemoryAddressAlternate,
					      uint64_t MemorySizeInBytes)
{
	DB << utils::fmt("[%s] node %d; default %d; alternate %d\n",
		__func__, Node, DefaultPolicy, AlternatePolicy);

	if (is_dgpu)
		/* On dGPU the alternate aperture is setup and used
		 * automatically for coherent allocations. Don't let
		 * app override it.
		 */
		return DEVICE_STATUS_ERROR;
    return DEVICE_STATUS_SUCCESS;
}
#endif

extern int zfb_support;

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

device_status_t DeviceAllocMemory(uint32_t PreferredNode,
					  uint64_t SizeInBytes,
					  HsaMemFlags MemFlags,
					  void **MemoryAddress)
{
	device_status_t result;
	uint32_t gpu_id;
	uint64_t page_size;


	pr_debug("[%s] node %d\n", __func__, PreferredNode);

	result = validate_nodeid(PreferredNode, &gpu_id);
	if (result != DEVICE_STATUS_SUCCESS) {
		pr_err("[%s] invalid node ID: %d\n", __func__, PreferredNode);
		return result;
	}

	page_size = PageSizeFromFlags(MemFlags.ui32.PageSize);

	if (!MemoryAddress || !SizeInBytes || (SizeInBytes & (page_size-1)))
		return DEVICE_STATUS_INVALID_PARAMETER;

	if (MemFlags.ui32.FixedAddress) {
		if (*MemoryAddress == NULL)
			return DEVICE_STATUS_INVALID_PARAMETER;
	} else
		*MemoryAddress = NULL;

	if (MemFlags.ui32.Scratch) {
		*MemoryAddress = mm_allocate_scratch(gpu_id, *MemoryAddress, SizeInBytes);

		if (!(*MemoryAddress)) {
			pr_err("[%s] failed to allocate %lu bytes from scratch\n",
				__func__, SizeInBytes);
			return DEVICE_STATUS_NO_MEMORY;
		}

		return DEVICE_STATUS_SUCCESS;
	}

	/* GPU allocated system memory */
	if (!gpu_id || !MemFlags.ui32.NonPaged || zfb_support) {
		/* Backwards compatibility hack: Allocate system memory if app
		 * asks for paged memory from a GPU node.
		 */

		/* If allocate VRAM under ZFB mode */
		if (zfb_support && gpu_id && MemFlags.ui32.NonPaged == 1)
			MemFlags.ui32.CoarseGrain = 1;

		*MemoryAddress = mm_allocate_host(PreferredNode,  *MemoryAddress,
						   SizeInBytes,	MemFlags);

		if (!(*MemoryAddress)) {
			pr_err("[%s] failed to allocate %lu bytes from host\n",
				__func__, SizeInBytes);
			return DEVICE_STATUS_ERROR;
		}

		return DEVICE_STATUS_SUCCESS;
	}

	/* GPU allocated VRAM */
	*MemoryAddress = mm_allocate_device(gpu_id, *MemoryAddress, SizeInBytes, MemFlags);

	if (!(*MemoryAddress)) {
		pr_err("[%s] failed to allocate %lu bytes from device\n",
			__func__, SizeInBytes);
		return DEVICE_STATUS_NO_MEMORY;
	}

#if 0
 	// if (isSystem || (gpu_id == 0 && !MemFlags.ui32.Scratch)) {
	if (!gpu_id || !MemFlags.ui32.NonPaged /*|| zfb_support*/) {
            *MemoryAddress = fmm_allocate_host(PreferredNode, SizeInBytes, MemFlags);
            GetMemoryManager()->Alloc(SizeInBytes, HSA_HEAPTYPE_SYSTEM, 0x0, (uint32_t**)MemoryAddress);

            if (!(*MemoryAddress)) {
            	DB << utils::fmt("[%s] failed to allocate %lu bytes from host\n",
            		__func__, SizeInBytes);
            	return DEVICE_STATUS_ERROR;
            }
            return DEVICE_STATUS_SUCCESS;
	}
    // FIXME we should allocate device instead
    GetMemoryManager()->Alloc(SizeInBytes, HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC, 0x0, (uint32_t**)MemoryAddress);

/* TODO put them in ppu_cmdio
	// Allocate object
	pthread_mutex_lock(&aperture->fmm_mutex);
	vm_obj = aperture_allocate_object(aperture, mem, args.handle,
				      MemorySizeInBytes, flags);
*/

	// *MemoryAddress = memory->GetDevicePtr();
#endif
	return DEVICE_STATUS_SUCCESS;
}

device_status_t DeviceFreeMemory(void *MemoryAddress, uint64_t SizeInBytes)
{
	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		pr_err("FIXME: freeing NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	mm_release(MemoryAddress);
    return DEVICE_STATUS_SUCCESS;
}

device_status_t DeviceRegisterMemory(void *MemoryAddress, uint64_t MemorySizeInBytes)
{
	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	if (!is_dgpu)
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_SUCCESS;

	return mm_register_memory(MemoryAddress, MemorySizeInBytes, NULL, 0, true);
}

device_status_t DeviceRegisterMemoryToNodes(void *MemoryAddress,
						    uint64_t MemorySizeInBytes,
						    uint64_t NumberOfNodes,
						    uint32_t *NodeArray)
{
	uint32_t *gpu_id_array;
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	pr_debug("[%s] address %p number of nodes %lu\n",
		__func__, MemoryAddress, NumberOfNodes);

	if (!is_dgpu)
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_NOT_SUPPORTED;

	ret = validate_nodeid_array(&gpu_id_array,
			NumberOfNodes, NodeArray);

	if (ret == DEVICE_STATUS_SUCCESS) {
		ret = mm_register_memory(MemoryAddress, MemorySizeInBytes,
					  gpu_id_array,
					  NumberOfNodes*sizeof(uint32_t),
					  true);
		if (ret != DEVICE_STATUS_SUCCESS)
			free(gpu_id_array);
	}

	return ret;
}

device_status_t DeviceRegisterMemoryWithFlags(void *MemoryAddress,
						    uint64_t MemorySizeInBytes,
						    HsaMemFlags MemFlags)
{
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	pr_debug("[%s] address %p\n",
		__func__, MemoryAddress);

	// Registered memory should be ordinary paged host memory.
	if ((MemFlags.ui32.HostAccess != 1) || (MemFlags.ui32.NonPaged == 1))
		return DEVICE_STATUS_NOT_SUPPORTED;

	if (!is_dgpu)
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_NOT_SUPPORTED;

	ret = mm_register_memory(MemoryAddress, MemorySizeInBytes,
		NULL, 0, MemFlags.ui32.CoarseGrain);

	return ret;
}

device_status_t hsaKmtRegisterGraphicsHandleToNodes(HSAuint64 GraphicsResourceHandle,
							    HsaGraphicsResourceInfo *GraphicsResourceInfo,
							    HSAuint64 NumberOfNodes,
							    HSAuint32 *NodeArray)
{
	uint32_t *gpu_id_array;
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	pr_debug("[%s] number of nodes %lu\n", __func__, NumberOfNodes);

	ret = validate_nodeid_array(&gpu_id_array,
			NumberOfNodes, NodeArray);

	if (ret == DEVICE_STATUS_SUCCESS) {
		ret = mm_register_graphics_handle(
			GraphicsResourceHandle, GraphicsResourceInfo,
			gpu_id_array, NumberOfNodes * sizeof(uint32_t));
		if (ret != DEVICE_STATUS_SUCCESS)
			free(gpu_id_array);
	}

	return ret;
}

device_status_t DeviceShareMemory(void *MemoryAddress,
					  uint64_t SizeInBytes,
					  HsaSharedMemoryHandle *SharedMemoryHandle)
{

	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	if (!SharedMemoryHandle)
		return DEVICE_STATUS_INVALID_PARAMETER;

	return mm_share_memory(MemoryAddress, SizeInBytes, SharedMemoryHandle);
}

device_status_t DeviceRegisterSharedHandle(const HsaSharedMemoryHandle *SharedMemoryHandle,
						   void **MemoryAddress,
						   uint64_t *SizeInBytes)
{

	pr_debug("[%s] handle %p\n", __func__, SharedMemoryHandle);

	return DeviceRegisterSharedHandleToNodes(SharedMemoryHandle,
						 MemoryAddress,
						 SizeInBytes,
						 0,
						 NULL);
}

device_status_t DeviceRegisterSharedHandleToNodes(const HsaSharedMemoryHandle *SharedMemoryHandle,
							  void **MemoryAddress,
							  uint64_t *SizeInBytes,
							  uint64_t NumberOfNodes,
							  uint32_t *NodeArray)
{

	uint32_t *gpu_id_array = NULL;
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	pr_debug("[%s] handle %p number of nodes %lu\n",
		__func__, SharedMemoryHandle, NumberOfNodes);

	if (!SharedMemoryHandle)
		return DEVICE_STATUS_INVALID_PARAMETER;

	if (NodeArray) {
		ret = validate_nodeid_array(&gpu_id_array, NumberOfNodes, NodeArray);
		if (ret != DEVICE_STATUS_SUCCESS)
			goto error;
	}

	ret = mm_register_shared_memory(SharedMemoryHandle,
					 SizeInBytes,
					 MemoryAddress,
					 gpu_id_array,
					 NumberOfNodes*sizeof(uint32_t));
	if (ret != DEVICE_STATUS_SUCCESS)
		goto error;

	return ret;

error:
	if (gpu_id_array)
		free(gpu_id_array);
	return ret;
}
/*
static uint64_t convertHsaToKfdRange(HsaMemoryRange *HsaRange)
{
	if (sizeof(struct kfd_memory_range) !=
		sizeof(HsaMemoryRange)) {
		pr_err("Struct size mismatch in thunk. Cannot cast Hsa Range to KFD IOCTL range\n");
		return 0;
	}
	return (uint64_t) HsaRange;
}

device_status_t hsaKmtProcessVMRead(HSAuint32 Pid,
					    HsaMemoryRange *LocalMemoryArray,
					    HSAuint64 LocalMemoryArrayCount,
					    HsaMemoryRange *RemoteMemoryArray,
					    HSAuint64 RemoteMemoryArrayCount,
					    HSAuint64 *SizeCopied)
{
	int ret = DEVICE_STATUS_SUCCESS;
	struct ioctl_cross_memory_copy_args args = {0};

	pr_debug("[%s]\n", __func__);

	if (!LocalMemoryArray || !RemoteMemoryArray ||
		LocalMemoryArrayCount == 0 || RemoteMemoryArrayCount == 0)
		return DEVICE_STATUS_ERROR;

	args.flags = 0;
	KFD_SET_CROSS_MEMORY_READ(args.flags);
	args.pid = Pid;
	args.src_mem_range_array = convertHsaToKfdRange(RemoteMemoryArray);
	args.src_mem_array_size = RemoteMemoryArrayCount;
	args.dst_mem_range_array = convertHsaToKfdRange(LocalMemoryArray);
	args.dst_mem_array_size = LocalMemoryArrayCount;
	args.bytes_copied = 0;

	if (kmtIoctl(kfd_fd, AMDKFD_IOC_CROSS_MEMORY_COPY, &args))
		ret = DEVICE_STATUS_ERROR;

	if (SizeCopied)
		*SizeCopied = args.bytes_copied;

	return ret;
}

device_status_t hsaKmtProcessVMWrite(HSAuint32 Pid,
					     HsaMemoryRange *LocalMemoryArray,
					     HSAuint64 LocalMemoryArrayCount,
					     HsaMemoryRange *RemoteMemoryArray,
					     HSAuint64 RemoteMemoryArrayCount,
					     HSAuint64 *SizeCopied)
{
	int ret = DEVICE_STATUS_SUCCESS;
	struct ioctl_cross_memory_copy_args args = {0};

	pr_debug("[%s]\n", __func__);

	if (SizeCopied)
		*SizeCopied = 0;

	if (!LocalMemoryArray || !RemoteMemoryArray ||
		LocalMemoryArrayCount == 0 || RemoteMemoryArrayCount == 0)
		return DEVICE_STATUS_ERROR;

	args.flags = 0;
	KFD_SET_CROSS_MEMORY_WRITE(args.flags);
	args.pid = Pid;
	args.src_mem_range_array = convertHsaToKfdRange(LocalMemoryArray);
	args.src_mem_array_size = LocalMemoryArrayCount;
	args.dst_mem_range_array = convertHsaToKfdRange(RemoteMemoryArray);
	args.dst_mem_array_size = RemoteMemoryArrayCount;
	args.bytes_copied = 0;

	if (kmtIoctl(kfd_fd, AMDKFD_IOC_CROSS_MEMORY_COPY, &args))
		ret = DEVICE_STATUS_ERROR;

	if (SizeCopied)
		*SizeCopied = args.bytes_copied;

	return ret;
}
*/

device_status_t DeviceDeregisterMemory(void *MemoryAddress)
{

	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	return mm_deregister_memory(MemoryAddress);
}

device_status_t DeviceMapMemoryToGPU(void *MemoryAddress,
					     uint64_t MemorySizeInBytes,
					     uint64_t *AlternateVAGPU)
{

	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		pr_err("FIXME: mapping NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	if (AlternateVAGPU)
		*AlternateVAGPU = 0;

	if (!mm_map_to_gpu(MemoryAddress, MemorySizeInBytes, AlternateVAGPU))
		return DEVICE_STATUS_SUCCESS;
	else
		return DEVICE_STATUS_ERROR;
}

device_status_t DeviceMapMemoryToGPUNodes(void *MemoryAddress,
						  uint64_t MemorySizeInBytes,
						  uint64_t *AlternateVAGPU,
						  HsaMemMapFlags MemMapFlags,
						  uint64_t NumberOfNodes,
						  uint32_t *NodeArray)
{
	uint32_t *gpu_id_array;
	device_status_t ret;

	pr_debug("[%s] address %p number of nodes %lu\n",
		__func__, MemoryAddress, NumberOfNodes);

	if (!MemoryAddress) {
		pr_err("FIXME: mapping NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	if (!is_dgpu && NumberOfNodes == 1)
		return DeviceMapMemoryToGPU(MemoryAddress,
				MemorySizeInBytes,
				AlternateVAGPU);

	ret = validate_nodeid_array(&gpu_id_array,
				NumberOfNodes, NodeArray);
	if (ret != DEVICE_STATUS_SUCCESS)
		return ret;

/// TODO schi need to map system meory to dgpu, need to setup garttab
///      but in PPU, the simulator can accessor the memory already(because they are in same process)
	ret = mm_map_to_gpu_nodes(MemoryAddress, MemorySizeInBytes,
		gpu_id_array, NumberOfNodes, AlternateVAGPU);

	if (gpu_id_array)
		free(gpu_id_array);

	return ret;
}

device_status_t DeviceUnmapMemoryToGPU(void *MemoryAddress)
{

	pr_debug("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		/* Workaround for runtime bug */
		pr_err("FIXME: Unmapping NULL pointer\n");
		return DEVICE_STATUS_SUCCESS;
	}

	if (!mm_unmap_from_gpu(MemoryAddress))
		return DEVICE_STATUS_SUCCESS;
	else
		return DEVICE_STATUS_ERROR;
}

device_status_t hsaKmtGetTileConfig(HSAuint32 NodeId, HsaGpuTileConfig *config)
{
	struct ioctl_get_tile_config_args args = {0};
	uint32_t gpu_id;
	device_status_t result;

	pr_debug("[%s] node %d\n", __func__, NodeId);

	result = validate_nodeid(NodeId, &gpu_id);
	if (result != DEVICE_STATUS_SUCCESS)
		return result;

	/* Avoid Valgrind warnings about uninitialized data. Valgrind doesn't
	 * know that KFD writes this.
	 */
	memset(config->TileConfig, 0, sizeof(*config->TileConfig) * config->NumTileConfigs);
	memset(config->MacroTileConfig, 0, sizeof(*config->MacroTileConfig) * config->NumMacroTileConfigs);

	args.gpu_id = gpu_id;
	args.tile_config_ptr = (uint64_t)config->TileConfig;
	args.macro_tile_config_ptr = (uint64_t)config->MacroTileConfig;
	args.num_tile_configs = config->NumTileConfigs;
	args.num_macro_tile_configs = config->NumMacroTileConfigs;

	if (cmd_get_tile_config(&args) != 0)
		return DEVICE_STATUS_ERROR;

	config->NumTileConfigs = args.num_tile_configs;
	config->NumMacroTileConfigs = args.num_macro_tile_configs;

	config->GbAddrConfig = args.gb_addr_config;

	config->NumBanks = args.num_banks;
	config->NumRanks = args.num_ranks;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t DeviceQueryPointerInfo(const void *Pointer,
					       HsaPointerInfo *PointerInfo)
{
	pr_debug("[%s] pointer %p\n", __func__, Pointer);

	if (!PointerInfo)
		return DEVICE_STATUS_INVALID_HANDLE;

	return mm_get_mem_info(Pointer, PointerInfo);
}

device_status_t hsaKmtSetMemoryUserData(const void *Pointer,
						void *UserData)
{
	pr_debug("[%s] pointer %p\n", __func__, Pointer);

	return mm_set_mem_user_data(Pointer, UserData);
}


