//#include "inc/hsakmt.h"
// #include "libhsakmt.h"

#include "inc/platform.h"
// #include "inc/pps_memory_region.h"
#include "inc/Doorbells.h"
#include "inc/Debug.h"
#include "inc/Device.h"
#include "inc/DeviceInfo.h"
#include "inc/MemMgr.h"
#include "inc/Topology.h"
#include "util/intmath.h"
// #include "inc/command_queue.h"
// #include "inc/queue_lookup.h"
// #include "ppu/ppu_cmdprocessor.h"
#include "cmdio.h"
#include <unistd.h>

// below is from global.c
unsigned long kfd_open_count;
unsigned long system_properties_count;
// pthread_mutex_t hsakmt_mutex = PTHREAD_MUTEX_INITIALIZER;

//
// using namespace device;
using std::function;

// static pid_t parent_pid = -1;

// static region_t system_region_ = { 0 };
static uint64_t hsa_freq;
int debug_level;
// extern int g_zfb_support_;
//extern DeviceInfo g_device_info;

// static cmdio *csi;

/* Normally libraries don't print messages. For debugging purpose, we'll
 * print messages if an environment variable, HSAKMT_DEBUG_LEVEL, is set.
 */
inline void Device::init_page_size(void)
{
	PAGE_SIZE = sysconf(_SC_PAGESIZE);
	PAGE_SHIFT = ceilLog2(PAGE_SIZE) - 1;
}

device_info *Device::get_device_info_by_dev_id(uint16_t dev_id) {
    /*
   enum asic_family_type asic;
   if (g_device_info.topology_get_asic_family(dev_id, &asic) != DEVICE_STATUS_SUCCESS)
        return nullptr;
   return dev_lookup_table[asic];
   */
   device_info_->find_device(dev_id);
}

device_status_t Device::Close()
{
	return DEVICE_STATUS_SUCCESS;
}

device_status_t Device::Open()
{
    device_status_t result{DEVICE_STATUS_SUCCESS};
    HsaSystemProperties *sys_props;
    node_props_t *node_props;
    device_info_ = new DeviceInfo();

    MAKE_NAMED_SCOPE_GUARD(init_doorbell_failed, [&]() { mm_->mm_destroy_process_apertures();});
    MAKE_NAMED_SCOPE_GUARD(init_process_aperture_failed, [&]() { Close();});

    {
	    std::lock_guard<std::mutex> lock(mutex_);

	    if (kfd_open_count == 0) {
		    struct ioctl_open_args open_args = {0};
	        cmd_open(&open_args);

	        kfd_open_count = 1;

	        init_page_size();

	        ioctl_get_system_prop_args sys_prop_args = {0};
	        int ret = cmd_get_system_prop(&sys_prop_args);

	        if (ret) return DEVICE_STATUS_ERROR;

	        sys_props = sys_prop_args.sys_prop;
	        node_props = sys_prop_args.node_prop;

	        result = mm_->mm_init_process_apertures(sys_props->NumNodes, node_props);
	        if (result != DEVICE_STATUS_SUCCESS)
	            init_process_aperture_failed.Dismiss();

	        result = doorbells_->init_process_doorbells(sys_props->NumNodes); // , node_props);
	        if (result != DEVICE_STATUS_SUCCESS)
	            init_doorbell_failed.Dismiss();

	        if (debug_->init_device_debugging_memory(sys_props->NumNodes) != DEVICE_STATUS_SUCCESS)
	            WARN("Insufficient Memory. Debugging unavailable\n");

	        // init_counter_props(sys_props.NumNodes);
	    } else {
	        kfd_open_count++;
	        result = DEVICE_STATUS_SUCCESS;
	    }

    }
    return result;
}

device_status_t Device::AllocMemory(uint32_t PreferredNode,
					  uint64_t SizeInBytes,
					  HsaMemFlags MemFlags,
					  void **MemoryAddress)
{
	device_status_t result;
	uint32_t gpu_id;
	uint64_t page_size;


	DEBUG("[%s] node %d\n", __func__, PreferredNode);

	result = topo_->validate_nodeid(PreferredNode, &gpu_id);
	if (result != DEVICE_STATUS_SUCCESS) {
		ERROR("[%s] invalid node ID: %d\n", __func__, PreferredNode);
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
		*MemoryAddress = mm_->mm_allocate_scratch(gpu_id, *MemoryAddress, SizeInBytes);

		if (!(*MemoryAddress)) {
			ERROR("[%s] failed to allocate %lu bytes from scratch\n",
				__func__, SizeInBytes);
			return DEVICE_STATUS_NO_MEMORY;
		}

		return DEVICE_STATUS_SUCCESS;
	}

	/* GPU allocated system memory */
	if (!gpu_id || !MemFlags.ui32.NonPaged || zfb_support_) {
		/* Backwards compatibility hack: Allocate system memory if app
		 * asks for paged memory from a GPU node.
		 */

		/* If allocate VRAM under ZFB mode */
		if (zfb_support_ && gpu_id && MemFlags.ui32.NonPaged == 1)
			MemFlags.ui32.CoarseGrain = 1;

		*MemoryAddress = mm_->mm_allocate_host(PreferredNode,  *MemoryAddress,
						   SizeInBytes,	MemFlags);

		if (!(*MemoryAddress)) {
			ERROR("[%s] failed to allocate %lu bytes from host\n",
				__func__, SizeInBytes);
			return DEVICE_STATUS_ERROR;
		}

		return DEVICE_STATUS_SUCCESS;
	}

	/* GPU allocated VRAM */
	*MemoryAddress = mm_->mm_allocate_device(gpu_id, *MemoryAddress, SizeInBytes, MemFlags);

	if (!(*MemoryAddress)) {
		ERROR("[%s] failed to allocate %lu bytes from device\n",
			__func__, SizeInBytes);
		return DEVICE_STATUS_NO_MEMORY;
	}

#if 0
 	// if (isSystem || (gpu_id == 0 && !MemFlags.ui32.Scratch)) {
	if (!gpu_id || !MemFlags.ui32.NonPaged /*|| zfb_support_*/) {
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

device_status_t Device::FreeMemory(void *MemoryAddress, uint64_t SizeInBytes)
{
	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		ERROR("FIXME: freeing NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	mm_->mm_release(MemoryAddress);
    return DEVICE_STATUS_SUCCESS;
}

device_status_t Device::RegisterMemory(void *MemoryAddress, uint64_t MemorySizeInBytes)
{
	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	if (!is_dgpu())
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_SUCCESS;

	return mm_->mm_register_memory(MemoryAddress, MemorySizeInBytes, NULL, 0, true);
}

device_status_t Device::RegisterMemoryToNodes(void *MemoryAddress,
						    uint64_t MemorySizeInBytes,
						    uint64_t NumberOfNodes,
						    uint32_t *NodeArray)
{
	uint32_t *gpu_id_array;
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	DEBUG("[%s] address %p number of nodes %lu\n",
		__func__, MemoryAddress, NumberOfNodes);

	if (!is_dgpu())
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_NOT_SUPPORTED;

	ret = topo_->validate_nodeid_array(&gpu_id_array, NumberOfNodes, NodeArray);

	if (ret == DEVICE_STATUS_SUCCESS) {
		ret = mm_->mm_register_memory(MemoryAddress, MemorySizeInBytes,
					  gpu_id_array,
					  NumberOfNodes*sizeof(uint32_t),
					  true);
		if (ret != DEVICE_STATUS_SUCCESS)
			free(gpu_id_array);
	}

	return ret;
}

device_status_t Device::RegisterMemoryWithFlags(void *MemoryAddress,
						    uint64_t MemorySizeInBytes,
						    HsaMemFlags MemFlags)
{
	device_status_t ret = DEVICE_STATUS_SUCCESS;

	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	// Registered memory should be ordinary paged host memory.
	if ((MemFlags.ui32.HostAccess != 1) || (MemFlags.ui32.NonPaged == 1))
		return DEVICE_STATUS_NOT_SUPPORTED;

	if (!is_dgpu())
		/* TODO: support mixed APU and dGPU configurations */
		return DEVICE_STATUS_NOT_SUPPORTED;

	ret = mm_->mm_register_memory(MemoryAddress, MemorySizeInBytes,
		NULL, 0, MemFlags.ui32.CoarseGrain);

	return ret;
}

device_status_t Device::DeregisterMemory(void *MemoryAddress)
{

	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	return mm_->mm_deregister_memory(MemoryAddress);
}

device_status_t Device::MapMemoryToGPU(void *MemoryAddress,
					     uint64_t MemorySizeInBytes,
					     uint64_t *AlternateVAGPU)
{

	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		ERROR("FIXME: mapping NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	if (AlternateVAGPU)
		*AlternateVAGPU = 0;

	if (!mm_->mm_map_to_gpu(MemoryAddress, MemorySizeInBytes, AlternateVAGPU))
		return DEVICE_STATUS_SUCCESS;
	else
		return DEVICE_STATUS_ERROR;
}

device_status_t Device::MapMemoryToGPUNodes(void *MemoryAddress,
						  uint64_t MemorySizeInBytes,
						  uint64_t *AlternateVAGPU,
						  HsaMemMapFlags MemMapFlags,
						  uint64_t NumberOfNodes,
						  uint32_t *NodeArray)
{
	uint32_t *gpu_id_array;
	device_status_t ret;

	DEBUG("[%s] address %p number of nodes %lu\n",
		__func__, MemoryAddress, NumberOfNodes);

	if (!MemoryAddress) {
		ERROR("FIXME: mapping NULL pointer\n");
		return DEVICE_STATUS_ERROR;
	}

	if (!is_dgpu() && NumberOfNodes == 1)
		return MapMemoryToGPU(MemoryAddress,
				MemorySizeInBytes,
				AlternateVAGPU);

	ret = topo_->validate_nodeid_array(&gpu_id_array,
				NumberOfNodes, NodeArray);
	if (ret != DEVICE_STATUS_SUCCESS)
		return ret;

/// TODO schi need to map system meory to dgpu, need to setup garttab
///      but in PPU, the simulator can accessor the memory already(because they are in same process)
	ret = mm_->mm_map_to_gpu_nodes(MemoryAddress, MemorySizeInBytes,
		gpu_id_array, NumberOfNodes, AlternateVAGPU);

	if (gpu_id_array)
		free(gpu_id_array);

	return ret;
}

device_status_t Device::UnmapMemoryToGPU(void *MemoryAddress)
{
	DEBUG("[%s] address %p\n", __func__, MemoryAddress);

	if (!MemoryAddress) {
		/* Workaround for runtime bug */
		ERROR("FIXME: Unmapping NULL pointer\n");
		return DEVICE_STATUS_SUCCESS;
	}

	if (!mm_->mm_unmap_from_gpu(MemoryAddress))
		return DEVICE_STATUS_SUCCESS;
	else
		return DEVICE_STATUS_ERROR;
}

//cmdio *GetDeviceCSI()
//{
//	return csi;
//}

namespace device
{

#if 0
// FIXME to use init_queue_allocator
// onload is call after topology is build up
bool OnLoad()
{
  // if (csi == nullptr)
  //  return false;

  // now we use hsa_qqueue_t in ppu cmdprocessor
  // csi->CreateQueue(false);

  hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &hsa_freq);

  // Find memory region to allocate the queue object.
  auto err = hsa_iterate_agents(
    [](device_t agent, void* data) -> status_t {
    core::IDevice* core_agent =
      reinterpret_cast<core::IDevice*>(core::IAgent::Object(agent));
    if (core_agent->device_type() == core::IDevice::DeviceType::kCpuDevice) {
      for (const core::IMemoryRegion* core_region : core_agent->regions()) {
        if ((reinterpret_cast<const hcs::MemoryRegion*>(core_region))->IsSystem()) {
          system_region_ = core_region;
          return HSA_STATUS_INFO_BREAK;
        }
      }
    }
    return SUCCESS;
  },
    NULL);
  assert(err == HSA_STATUS_INFO_BREAK && "Failed to retrieve system region");
  assert(system_region_ != nullptr);

  g_queue_allocator = [](size_t size, size_t alignment, uint32_t flags) -> void * {
    assert(alignment <= 4096);
    void* ptr = NULL;
    return (SUCCESS ==
      // hsa_memory_allocate(system_region_, size, &ptr))
      runtime_->AllocateMemory(system_region_, size, core::IMemoryRegion::AllocateNoFlags, ptr);
      ? ptr
      : NULL;
  };

  g_qeue_deallocator = [](void* ptr) {
      runtime_->FreeMemory(ptr);
  };

/* BaseShared is allocated when register cpu agent, so below can be delete
  core::IBaseShared::SetAllocateAndFree(g_queue_allocator, g_queue_deallocator);
  */

  return true;
}



CommandQueue *CreateCmdProcessor(GpuDevice *agent, uint32_t ring_size,
								 HSAuint32 node, const HsaCoreProperties *properties,
								 queue_type32_t queue_type, ScratchInfo &scratch,
								 core::IHsaEventCallback callback, void *user_data)
{
	HsaCoreProperties* props = const_cast<HsaCoreProperties*>(properties);
	std::string asic_info;
	std::ostringstream os;
	os << props->EngineId.ui32.Major << "." << props->EngineId.ui32.Minor << "." << props->EngineId.ui32.Stepping;
	asic_info = os.str();
	uint32_t asic_id = GetAsicID(asic_info);
	switch (asic_id)
	{
	case PPU:
	{
        /*
		return new device::PPUCmdProcessor(agent, ring_size, node, properties,
											 queue_type, scratch, callback, user_data, asic_id);
                                             */
		CommandQueue *cmd_queue = new device::CommandQueue(agent, ring_size, node, properties,
											 queue_type, scratch, callback, user_data, asic_id);
        // csi->CreateQueue(core::IQueue::Handle(cmd_queue));
        // DeviceCreateQueue(node, core::IQueue::Handle(cmd_queue));
        return cmd_queue;
	}
	case INVALID:
	default:
	{
        assert( 0 && "Failed Plase check GetAsicID");
		return NULL;
	}
	}
}
#endif

} // namespace device
