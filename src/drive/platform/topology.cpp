#include "inc/topology.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "inc/hsakmt.h"

#include "inc/platform.h"
#include "inc/cpu_agent.h"
#include "inc/ppu_agent.h"
#include "inc/pps_memory_region.h"
#include "inc/device_tools.h"
#include "util/utils.h"

namespace hcs {
// Minimum acceptable KFD version numbers
static const uint kKfdVersionMajor = 0;
static const uint kKfdVersionMinor = 99;

CpuAgent* DiscoverCpu(HSAuint32 node_id, HsaCoreProperties* node_prop) {
  if (node_prop->NumCPUCores == 0) {
    return NULL;
  }

  CpuAgent* cpu = new CpuAgent(node_id, node_prop);
  core::IRuntime::runtime_singleton_->RegisterAgent(cpu);

  return cpu;
}

GpuDevice* DiscoverGpu(HSAuint32 node_id, HsaCoreProperties* node_prop) {
  if (node_prop->NumFComputeCores == 0) {
    return NULL;
  }

  GpuDevice* gpu = new GpuAgent(node_id, node_prop);
  core::IRuntime::runtime_singleton_->RegisterAgent(gpu);

  return gpu;
}

void RegisterLinkInfo(uint32_t node_id, uint32_t num_link) {
  // Register connectivity links for this agent to the runtime.
  if (num_link == 0) {
    return;
  }

  std::vector<HsaIoLinkProperties> links(num_link);
  if (DEVICE_STATUS_SUCCESS !=
      DeviceGetNodeIoLinkProperties(node_id, num_link, &links[0])) {
    return;
  }

  for (HsaIoLinkProperties io_link : links) {
    // Populate link info with thunk property.
    hsa_amd_memory_pool_link_info_t link_info = {0};

    switch (io_link.IoLinkType) {
      case HSA_IOLINKTYPE_HYPERTRANSPORT:
        link_info.link_type = HSA_AMD_LINK_INFO_TYPE_HYPERTRANSPORT;
        link_info.atomic_support_32bit = true;
        link_info.atomic_support_64bit = true;
        link_info.coherent_support = true;
        break;
      case HSA_IOLINKTYPE_PCIEXPRESS:
        link_info.link_type = HSA_AMD_LINK_INFO_TYPE_PCIE;
        link_info.atomic_support_32bit = true;
        link_info.atomic_support_64bit = true;
        link_info.coherent_support = true;
        break;
      case HSA_IOLINK_TYPE_QPI_1_1:
        link_info.link_type = HSA_AMD_LINK_INFO_TYPE_QPI;
        link_info.atomic_support_32bit = true;
        link_info.atomic_support_64bit = true;
        link_info.coherent_support = true;
        break;
      case HSA_IOLINK_TYPE_INFINIBAND:
        link_info.link_type = HSA_AMD_LINK_INFO_TYPE_INFINBAND;
        debug_print("IOLINK is missing atomic and coherency defaults.\n");
        break;
      default:
        debug_print("Unrecognized IOLINK type.\n");
        break;
    }

    if (io_link.Flags.ui32.Override == 1) {
      if (io_link.Flags.ui32.NoPeerToPeerDMA == 1) {
        // Ignore this link since peer to peer is not allowed.
        continue;
      }
      link_info.atomic_support_32bit = (io_link.Flags.ui32.NoAtomics32bit == 0);
      link_info.atomic_support_64bit = (io_link.Flags.ui32.NoAtomics64bit == 0);
      link_info.coherent_support = (io_link.Flags.ui32.NonCoherent == 0);
    }

    link_info.max_bandwidth = io_link.MaximumBandwidth;
    link_info.max_latency = io_link.MaximumLatency;
    link_info.min_bandwidth = io_link.MinimumBandwidth;
    link_info.min_latency = io_link.MinimumLatency;
    link_info.numa_distance = io_link.Weight;

    core::IRuntime::runtime_singleton_->RegisterLinkInfo(
        io_link.NodeFrom, io_link.NodeTo, io_link.Weight, link_info);
  }
}

/// @brief Calls Kfd thunk to get the snapshot of the topology of the system,
/// which includes associations between, node, devices, memory and caches.
void BuildTopology() {
/* TODO  workaround, enble link in future
    core::IRuntime::runtime_singleton_->SetLinkCount(1);
    HsaCoreProperties node_prop = {0};
    node_prop.NumCPUCores = 1;
    node_prop.NumIOLinks = 1;
    const CpuAgent* cpu = DiscoverCpu(0, node_prop);
    RegisterLinkInfo(0, node_prop.NumIOLinks);
    */
/* TODO enable device info
  HsaVersionInfo info;
  if (hsaKmtGetVersion(&info) != SUCCESS) {
    return;
  }

  if (info.KernelInterfaceMajorVersion == kKfdVersionMajor &&
      info.KernelInterfaceMinorVersion < kKfdVersionMinor) {
    return;
  }

  // Disable KFD event support when using open source KFD
  if (info.KernelInterfaceMajorVersion == 1 &&
      info.KernelInterfaceMinorVersion == 0) {
    core::Ig_use_interrupt_wait = false;
  }
*/

  HsaSystemProperties props;
  DeviceReleaseSystemProperties();

  if (DeviceAcquireSystemProperties(&props) != DEVICE_STATUS_SUCCESS) {
    return;
  }

  core::IRuntime::runtime_singleton_->SetLinkCount(props.NumNodes);

  // Discover agents on every node in the platform.
  for (HSAuint32 node_id = 0; node_id < props.NumNodes; node_id++) {
    HsaCoreProperties *node_prop;
    if (DeviceGetNodeProperties(node_id, &node_prop) != DEVICE_STATUS_SUCCESS) {
      continue;
    }

    const CpuAgent* cpu = DiscoverCpu(node_id, node_prop);
    const GpuDevice* gpu = DiscoverGpu(node_id, node_prop);

    assert(!(cpu == NULL && gpu == NULL));

    RegisterLinkInfo(node_id, node_prop->NumIOLinks);
  }
}

bool Load() {
  // Open connection to kernel driver.
  if (DeviceOpen() != DEVICE_STATUS_SUCCESS) {
      return false;
  }
  //if (device::Load() != SUCCESS) {
  //  return false;
  //}

  // Build topology table.
  BuildTopology();

  return device::OnLoad();
}

bool Unload() {
  DeviceReleaseSystemProperties();

  // Close connection to kernel driver.
  DeviceClose();

  return true;
}
}  // namespace hcs
