#include "inc/hsakmt.h"
#include "libhsakmt.h"

#include "inc/platform.h"
#include "inc/pps_memory_region.h"
#include "inc/device_tools.h"
#include "inc/memory_manager.h"
#include "inc/command_queue.h"
// #include "inc/queue_lookup.h"
// #include "ppu/ppu_cmdprocessor.h"
#include "cmdio.h"
#include <unistd.h>

// below is from global.c
unsigned long kfd_open_count;
unsigned long system_properties_count;
pthread_mutex_t hsakmt_mutex = PTHREAD_MUTEX_INITIALIZER;
bool is_dgpu;
int PAGE_SIZE;
int PAGE_SHIFT;

//
using namespace device;
using std::function;

/*
template <class T>
struct ValidityError;
template <>
struct ValidityError<core::ISignal*> {
  enum { kValue = ERROR_INVALID_SIGNAL };
};
template <>
struct ValidityError<core::IDevice*> {
  enum { kValue = ERROR_INVALID_AGENT };
};
template <>
struct ValidityError<core::IMemoryRegion*> {
  enum { kValue = ERROR_INVALID_REGION };
};
template <>
struct ValidityError<core::IQueue*> {
  enum { kValue = ERROR_INVALID_QUEUE };
};
template <>
struct ValidityError<device::CmdBuffer*> {
  enum { kValue = ERROR_INVALID_ARGUMENT };
};
template <class T>
struct ValidityError<const T*> {
  enum { kValue = ValidityError<T*>::kValue };
};

#define IS_BAD_PTR(ptr)                                          \
  do {                                                           \
    if ((ptr) == NULL) return ERROR_INVALID_ARGUMENT; \
  } while (false)
#define IS_VALID(ptr)                                            \
  do {                                                           \
    if (((ptr) == NULL) || !((ptr)->IsValid()))                  \
      return status_t(ValidityError<decltype(ptr)>::kValue); \
  } while (false)
#define CHECK_ALLOC(ptr)                                         \
  do {                                                           \
    if ((ptr) == NULL) return ERROR_OUT_OF_RESOURCES; \
  } while (false)

template <class T>
static __forceinline bool IsValid(T* ptr) {
  return (ptr == NULL) ? NULL : ptr->IsValid();
}
*/

// static pid_t parent_pid = -1;
int device_debug_level;

/*
status_t HSA_API hsa_ext_tools_register_softcp_callback(
  bool aql_proxy,
	queue_t* queue, 
	hsa_ext_tools_softcp_pre_dispatch_callback pre_dispatch_callback_func, 
	hsa_ext_tools_softcp_post_dispatch_callback post_dispatch_callback_func) {
	IS_BAD_PTR(queue);

  // Register callbacks for the device cbuf queues
  device::CmdProcessor* HWQueue = lookup_queue<device::CmdProcessor>(queue);
  if (HWQueue->GetID(HWQueue) != queue->id) {
    return ERROR_INVALID_ARGUMENT;
  }
  HWQueue->SetPreDispatchCallback(pre_dispatch_callback_func);
  HWQueue->SetPostDispatchCallback(post_dispatch_callback_func);
  return SUCCESS;
}
*/
/*
status_t HSA_API hsa_ext_tools_register_softcp_callback_args(
  bool aql_proxy,
	queue_t* queue,
  void* pre_arg, void* post_arg) {
	IS_BAD_PTR(queue);

  // Register callback args for the Pm4 queues
  device::CmdProcessor* HWQueue = lookup_queue<device::CmdProcessor>(queue);
  if (HWQueue->GetID(HWQueue) != queue->id) {
    return ERROR_INVALID_ARGUMENT;
  }
  HWQueue->SetPreDispatchCallbackData(pre_arg);
  HWQueue->SetPostDispatchCallbackData(post_arg);
  return SUCCESS;
}
*/
/*
status_t HSA_API hsa_ext_tools_write_CBUF_packet(queue_t* queue, aql_translation_handle token,
                                         const void* cmd_buf, size_t size) {
  IS_BAD_PTR(token);
  device::CmdBuffer* buffer=device::CmdBuffer::Object(token);
  IS_BAD_PTR(buffer);
  if(size*4>UINT32_MAX)
	  return ERROR_OUT_OF_RESOURCES;
  buffer->append(cmd_buf, uint32_t(size*4));
  return SUCCESS;
}
*/

function<void *(size_t, size_t, uint32_t)> g_queue_allocator;
function<void(void *)> g_queue_deallocator;

static region_t g_system_region = { 0 };
static uint64_t hsa_freq;
int zfb_support;

// static cmdio *csi;

/* Normally libraries don't print messages. For debugging purpose, we'll
 * print messages if an environment variable, HSAKMT_DEBUG_LEVEL, is set.
 */
static void init_vars_from_env(void)
{
	char *envvar;
	int debug_level;

	device_debug_level = DEVICE_DEBUG_LEVEL_DEFAULT;

	envvar = getenv("DEVICE_DEBUG_LEVEL");
	if (envvar)
	{
		debug_level = atoi(envvar);
		if (debug_level >= DEVICE_DEBUG_LEVEL_ERR &&
			debug_level <= DEVICE_DEBUG_LEVEL_DEBUG)
			device_debug_level = debug_level;
	}

	/* Check whether to support Zero frame buffer */
	envvar = getenv("HSA_ZFB");
	if (envvar)
		zfb_support = atoi(envvar);

}

static inline void init_page_size(void)
{
	PAGE_SIZE = sysconf(_SC_PAGESIZE);
	PAGE_SHIFT = ffs(PAGE_SIZE) - 1;
}

device_status_t DeviceClose()
{
	return DEVICE_STATUS_SUCCESS;
}

device_status_t DeviceOpen()
{
    device_status_t result{DEVICE_STATUS_SUCCESS};
    init_vars_from_env();
    // int fd;
    HsaSystemProperties *sys_props;
    node_props_t *node_props;

    pthread_mutex_lock(&hsakmt_mutex);

    if (kfd_open_count == 0) {
	    struct ioctl_open_args open_args = {0};
        cmd_open(&open_args);
/*
        fd = open(kfd_device_name, O_RDWR | O_CLOEXEC);
        if (fd != -1) {
            kfd_fd = fd;
            kfd_open_count = 1;
        } else {
            result = HSAKMT_STATUS_KERNEL_IO_CHANNEL_NOT_OPENED;
            goto open_failed;
        }
*/

        kfd_open_count = 1;

        init_page_size();

        ioctl_get_system_prop_args sys_prop_args = {0};
        int ret = cmd_get_system_prop(&sys_prop_args);

        if (ret) return DEVICE_STATUS_ERROR;

        sys_props = sys_prop_args.sys_prop;
        node_props = sys_prop_args.node_prop;

        result = mm_init_process_apertures(sys_props->NumNodes, node_props);
        if (result != DEVICE_STATUS_SUCCESS)
            goto init_process_aperture_failed;

        result = init_process_doorbells(sys_props->NumNodes); // , node_props);
        if (result != DEVICE_STATUS_SUCCESS)
            goto init_doorbell_failed;

/*
        if (init_device_debugging_memory(sys_props.NumNodes) != DEVICE_STATUS_SUCCESS)
            pr_warn("Insufficient Memory. Debugging unavailable\n");

        init_counter_props(sys_props.NumNodes);
*/
    } else {
        kfd_open_count++;
        result = DEVICE_STATUS_SUCCESS;
    }

    pthread_mutex_unlock(&hsakmt_mutex);
    return result;

init_doorbell_failed:
    mm_destroy_process_apertures();
init_process_aperture_failed:
topology_sysfs_failed:
    // close(fd);
    // Deviceclose();
// open_failed:
    pthread_mutex_unlock(&hsakmt_mutex);
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
          g_system_region = core::IMemoryRegion::Handle(core_region);
          return HSA_STATUS_INFO_BREAK;
        }
      }
    }
    return SUCCESS;
  },
    NULL);
  assert(err == HSA_STATUS_INFO_BREAK && "Failed to retrieve system region");
  assert(g_system_region.handle != 0);

  g_queue_allocator = [](size_t size, size_t alignment, uint32_t flags) -> void * {
    assert(alignment <= 4096);
    void* ptr = NULL;
    return (SUCCESS ==
      hsa_memory_allocate(g_system_region, size, &ptr))
      ? ptr
      : NULL;
  };

  g_queue_deallocator = [](void* ptr) { hsa_memory_free(ptr); };

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
