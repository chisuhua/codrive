//#include "inc/hsakmt.h"
// #include "libhsakmt.h"

#include "inc/platform.h"
// #include "inc/pps_memory_region.h"
#include "inc/Device.h"
#include "inc/memory_manager.h"
// #include "inc/command_queue.h"
// #include "inc/queue_lookup.h"
// #include "ppu/ppu_cmdprocessor.h"
#include "cmdio.h"
#include <unistd.h>

// below is from global.c
unsigned long kfd_open_count;
unsigned long system_properties_count;
// pthread_mutex_t hsakmt_mutex = PTHREAD_MUTEX_INITIALIZER;
bool is_dgpu;
int PAGE_SIZE;
int PAGE_SHIFT;

//
// using namespace device;
using std::function;

// static pid_t parent_pid = -1;
function<void *(size_t, size_t, uint32_t)> g_queue_allocator;
function<void(void *)> g_queue_deallocator;

// static region_t system_region_ = { 0 };
static uint64_t hsa_freq;
// extern int g_zfb_support;

// static cmdio *csi;

/* Normally libraries don't print messages. For debugging purpose, we'll
 * print messages if an environment variable, HSAKMT_DEBUG_LEVEL, is set.
 */
static inline void init_page_size(void)
{
	PAGE_SIZE = sysconf(_SC_PAGESIZE);
	PAGE_SHIFT = ffs(PAGE_SIZE) - 1;
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

    MAKE_NAMED_SCOPE_GUARD(init_doorbell_failed, [&]() { mm_destroy_process_apertures();});
    MAKE_NAMED_SCOPE_GUARD(init_process_aperture_failed, [&]() { Close();});

    {
	    std::lock_guard<std::mutex> mutex_;

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

	        result = mm_init_process_apertures(sys_props->NumNodes, node_props);
	        if (result != DEVICE_STATUS_SUCCESS)
	            init_process_aperture_failed.Dismiss();

	        result = init_process_doorbells(sys_props->NumNodes); // , node_props);
	        if (result != DEVICE_STATUS_SUCCESS)
	            init_doorbell_failed.Dismiss();

	        if (init_device_debugging_memory(sys_props.NumNodes) != DEVICE_STATUS_SUCCESS)
	            pr_warn("Insufficient Memory. Debugging unavailable\n");

	        // init_counter_props(sys_props.NumNodes);
	    } else {
	        kfd_open_count++;
	        result = DEVICE_STATUS_SUCCESS;
	    }

    }
    return result;
}

//cmdio *GetDeviceCSI()
//{
//	return csi;
//}

namespace device
{

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


static uint32_t GetAsicID(const std::string &);

class AsicMap final
{
  private:
	AsicMap()
	{
		asic_map_[std::string("0.0.0")] = PPU; // 0.0.0 is APU
		asic_map_[std::string("1.0.0")] = PPU; // 1.0.0 is PPU
		// add other
	}

	bool find(const std::string &version) const
	{
		return asic_map_.find(version) != asic_map_.end() ? true : false;
	}

	std::unordered_map<std::string, int> asic_map_;

	friend uint32_t GetAsicID(const std::string &);
};

// @brief Return mapped value based on platform version number, return
// Asic::INVALID if the version is not supported yet. The function is
// thread safe.
static uint32_t GetAsicID(const std::string &asic_info)
{
	static const AsicMap map;

	if (map.find(asic_info))
		return map.asic_map_.at(asic_info);
	else
		return INVALID;
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

} // namespace device
