#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sched.h>

#include <algorithm>
#include "inc/device_tools.h"
#include "inc/memory_manager.h"
#include "libhsakmt.h"

/* Number of memory banks added by thunk on top of topology
 * This only includes static heaps like LDS, scratch and SVM,
 * not for MMIO_REMAP heap. MMIO_REMAP memory bank is reported
 * dynamically based on whether mmio aperture was mapped
 * successfully on this node.
 */

static HsaSystemProperties *g_system;
static node_props_t *g_props;

static int processor_vendor = -1;
/* Supported System Vendors */
enum SUPPORTED_PROCESSOR_VENDORS {
	GENUINE_INTEL = 0,
	AUTHENTIC_AMD
};
/* Adding newline to make the search easier */
static const char *supported_processor_vendor_name[] = {
	"GenuineIntel\n",
	"AuthenticAMD\n"
};

// static device_status_t topology_take_snapshot(void);
// static device_status_t topology_drop_snapshot(void);

static struct hsa_device_table {
	uint16_t device_id;		// Device ID
	unsigned char major;		// Major engine version
	unsigned char minor;		// Minor engine version
	unsigned char stepping;		// Stepping info
	unsigned char is_dgpu;		// Predicate for dGPU devices
	const char *dev_name;		// CALName of the device
	enum asic_family_type asic_family;
} device_lookup_table[] = {
	/* PPU Family */
	{ 0xccdd, 1, 0, 0, 1, "PPU", CHIP_PPUe },
	/* PPU2 */
	{ 0xccee, 1, 0, 0, 1, "PPU", CHIP_PPU },
};



device_status_t  DeviceAcquireSystemProperties(HsaSystemProperties *SystemProperties)
{
	device_status_t err = DEVICE_STATUS_SUCCESS;

	if (!SystemProperties)
		return DEVICE_STATUS_ERROR;

    ioctl_get_system_prop_args args = {0};
    int ret = cmd_get_system_prop(&args);

    if (ret)
        return DEVICE_STATUS_ERROR;

    g_system = args.sys_prop;
    g_props = args.node_prop;

	*SystemProperties = *g_system;
	return DEVICE_STATUS_SUCCESS;
}

device_status_t DeviceReleaseSystemProperties(void)
{
	device_status_t err;

	// pthread_mutex_lock(&hsakmt_mutex);

	// err = topology_drop_snapshot();
	err = DEVICE_STATUS_SUCCESS;

	// pthread_mutex_unlock(&hsakmt_mutex);

	return err;
}

device_status_t  DeviceGetNodeProperties(HSAuint32 NodeId,
						HsaCoreProperties **NodeProperties)
{
	device_status_t err;
	uint32_t gpu_id;

	if (!NodeProperties)
		return DEVICE_STATUS_ERROR;

	// pthread_mutex_lock(&hsakmt_mutex);

	/* KFD ADD page 18, snapshot protocol violation */
	if (!g_system) {
		err = DEVICE_STATUS_ERROR;
		assert(g_system);
		goto out;
	}

	if (NodeId >= g_system->NumNodes) {
		err = DEVICE_STATUS_ERROR;
		goto out;
	}

	err = validate_nodeid(NodeId, &gpu_id);
	if (err != DEVICE_STATUS_SUCCESS)
		return err;

	*NodeProperties = g_props[NodeId].core;
	/* For CPU only node don't add any additional GPU memory banks. */
    /* schi move below logic to InitRegionList
	if (gpu_id) {
		if (topology_is_dgpu(get_device_id_by_gpu_id(gpu_id)))
		uint64_t base, limit;
			(*NodeProperties)->NumMemoryBanks += NUM_OF_DGPU_HEAPS;
		else
			(*NodeProperties)->NumMemoryBanks += NUM_OF_IGPU_HEAPS;
		if (mm_get_aperture_base_and_limit(FMM_MMIO, gpu_id, &base,
				&limit) == DEVICE_STATUS_SUCCESS)
			(*NodeProperties)->NumMemoryBanks += 1;
	}
    */
	err = DEVICE_STATUS_SUCCESS;

out:
	// pthread_mutex_unlock(&hsakmt_mutex);
	return err;
}


// TODO below define HeapType for NumMemoryBanks
//  it will impact on ppu_agent.cpp:InitRegionList()
//
device_status_t  DeviceGetNodeMemoryProperties(HSAuint32 NodeId, HSAuint32 NumBanks,
						      HsaMemoryProperties *MemoryProperties)
{
	device_status_t err = DEVICE_STATUS_SUCCESS;
	uint32_t i, gpu_id;
	HSAuint64 aperture_limit;
	bool nodeIsDGPU;

	if (!MemoryProperties)
		return DEVICE_STATUS_ERROR;

	// pthread_mutex_lock(&hsakmt_mutex);

	/* KFD ADD page 18, snapshot protocol violation */
	if (!g_system) {
		err = DEVICE_STATUS_ERROR;
		assert(g_system);
		goto out;
	}

	/* Check still necessary */
	if (NodeId >= g_system->NumNodes) {
		err = DEVICE_STATUS_ERROR;
		goto out;
	}

	err = validate_nodeid(NodeId, &gpu_id);
	if (err != DEVICE_STATUS_SUCCESS)
		goto out;

	memset(MemoryProperties, 0, NumBanks * sizeof(HsaMemoryProperties));

	for (i = 0; i < std::min(g_props[NodeId].core->NumMemoryBanks, NumBanks); i++) {
		assert(g_props[NodeId].mem);
		MemoryProperties[i] = g_props[NodeId].mem[i];
	}

	// nodeIsDGPU = topology_is_dgpu(get_device_id_by_gpu_id(gpu_id));
	/* The following memory banks does not apply to CPU only node */
	if (gpu_id == 0) {
	    if (i < NumBanks) {
        // gpu in apu node, add LDS
		    MemoryProperties[i].VirtualBaseAddress = 1ULL << 48;
		    MemoryProperties[i].HeapType = HSA_HEAPTYPE_GPU_LDS;
		    MemoryProperties[i].SizeInBytes = g_props[NodeId].core->LDSSizeInKB * 1024;
        }
        return err;
    }

	/*Add LDS*/
	if (i < NumBanks &&
		mm_get_aperture_base_and_limit(FMM_LDS, gpu_id,
				&MemoryProperties[i].VirtualBaseAddress, &aperture_limit) == DEVICE_STATUS_SUCCESS) {
		MemoryProperties[i].HeapType = HSA_HEAPTYPE_GPU_LDS;
		MemoryProperties[i].SizeInBytes = g_props[NodeId].core->LDSSizeInKB * 1024;
		i++;
	}



    // TODO for APU, use SYSTEM only(remove SVM/SCATCH)
    //
	/* Add Local memory - HSA_HEAPTYPE_FRAME_BUFFER_PRIVATE.
	 * For dGPU the topology node contains Local Memory and it is added by
	 * the for loop above
	 */
#if 0
    // ? schi: why is !nodeIsDGPU
	if (!nodeIsDGPU && i < NumBanks && g_props[NodeId].node.LocalMemSize > 0 &&
		fmm_g et_aperture_base_and_limit(FMM_GPUVM, gpu_id,
				&MemoryProperties[i].VirtualBaseAddress, &aperture_limit) == DEVICE_STATUS_SUCCESS) {
		// TODO schi MemoryProperties[i].HeapType = HSA_HEAPTYPE_FRAME_BUFFER_PRIVATE;
		MemoryProperties[i].HeapType = HSA_HEAPTYPE_SYSTEM;
        // TODO need to setup right SizeInBytes
		// MemoryProperties[i].SizeInBytes = g_props[NodeId].node.LocalMemSize;
		MemoryProperties[i].SizeInBytes = (aperture_limit - MemoryProperties[i].VirtualBaseAddress) + 1;
		i++;
	}
#endif
	/* Add SCRATCH */
	if (i < NumBanks &&
		mm_get_aperture_base_and_limit(FMM_SCRATCH, gpu_id,
				&MemoryProperties[i].VirtualBaseAddress, &aperture_limit) == DEVICE_STATUS_SUCCESS) {
		MemoryProperties[i].HeapType = HSA_HEAPTYPE_GPU_SCRATCH;
		MemoryProperties[i].SizeInBytes = (aperture_limit - MemoryProperties[i].VirtualBaseAddress) + 1;
		i++;
	}

	/* On dGPUs add SVM aperture */
	if (topology_is_svm_needed(get_device_id_by_gpu_id(gpu_id)) && i < NumBanks &&
	    mm_get_aperture_base_and_limit( FMM_SVM, gpu_id, &MemoryProperties[i].VirtualBaseAddress,
		    &aperture_limit) == DEVICE_STATUS_SUCCESS) {
		MemoryProperties[i].HeapType = HSA_HEAPTYPE_DEVICE_SVM;
		MemoryProperties[i].SizeInBytes = (aperture_limit - MemoryProperties[i].VirtualBaseAddress) + 1;
		i++;
	}

	// Add mmio aperture
	if (i < NumBanks &&
		mm_get_aperture_base_and_limit(FMM_MMIO, gpu_id,
				&MemoryProperties[i].VirtualBaseAddress, &aperture_limit) == DEVICE_STATUS_SUCCESS) {
		MemoryProperties[i].HeapType = HSA_HEAPTYPE_MMIO_REMAP;
		MemoryProperties[i].SizeInBytes = (aperture_limit - MemoryProperties[i].VirtualBaseAddress) + 1;
		i++;
	}


out:
	// pthread_mutex_unlock(&hsakmt_mutex);
	return err;
}

device_status_t  DeviceGetNodeCacheProperties(HSAuint32 NodeId,
						     HSAuint32 ProcessorId,
						     HSAuint32 NumCaches,
						     HsaCacheProperties *CacheProperties)
{
	device_status_t err;
	uint32_t i;

	if (!CacheProperties)
		return DEVICE_STATUS_ERROR;

	pthread_mutex_lock(&hsakmt_mutex);

	/* KFD ADD page 18, snapshot protocol violation */
	if (!g_system) {
		err = DEVICE_STATUS_ERROR;
		assert(g_system);
		goto out;
	}

	if (NodeId >= g_system->NumNodes || NumCaches > g_props[NodeId].core->NumCaches) {
		err = DEVICE_STATUS_ERROR;
		goto out;
	}

	for (i = 0; i < std::min(g_props[NodeId].core->NumCaches, NumCaches); i++) {
		assert(g_props[NodeId].cache);
		CacheProperties[i] = g_props[NodeId].cache[i];
	}

	err = DEVICE_STATUS_SUCCESS;

out:
	pthread_mutex_unlock(&hsakmt_mutex);
	return err;
}

device_status_t  DeviceGetNodeIoLinkProperties(HSAuint32 NodeId,
						      HSAuint32 NumIoLinks,
						      HsaIoLinkProperties *IoLinkProperties)
{
	device_status_t err;
	uint32_t i;

	if (!IoLinkProperties)
		return DEVICE_STATUS_ERROR;


	pthread_mutex_lock(&hsakmt_mutex);

	/* KFD ADD page 18, snapshot protocol violation */
	if (!g_system) {
		err = DEVICE_STATUS_ERROR;
		assert(g_system);
		goto out;
	}

	if (NodeId >= g_system->NumNodes || NumIoLinks > g_props[NodeId].core->NumIOLinks) {
		err = DEVICE_STATUS_ERROR;
		goto out;
	}

	for (i = 0; i < std::min(g_props[NodeId].core->NumIOLinks, NumIoLinks); i++) {
		assert(g_props[NodeId].link);
		IoLinkProperties[i] = g_props[NodeId].link[i];
	}

	err = DEVICE_STATUS_SUCCESS;

out:
	pthread_mutex_unlock(&hsakmt_mutex);
	return err;
}

/*
bool topology_is_dgpu(uint16_t device_id)
{
	const struct hsa_device_table *hsa_device =
				find_hsa_device(device_id);

	if (hsa_device && hsa_device->is_dgpu) {
		is_dgpu = true;
		return true;
	}
	is_dgpu = false;
	return false;
}
*/

bool topology_is_svm_needed(uint16_t device_id)
{
	const struct hsa_device_table *hsa_device;

	//if (topology_is_dgpu(device_id))
	return true;


}

device_status_t validate_nodeid(uint32_t nodeid, uint32_t *gpu_id)
{
	if (!g_props || !g_system || g_system->NumNodes <= nodeid)
		return DEVICE_STATUS_ERROR;
	if (gpu_id)
		*gpu_id = g_props[nodeid].gpu_id;

	return DEVICE_STATUS_SUCCESS;
}

void topology_setup_is_dgpu_param(HsaCoreProperties *props)
{
	/* if we found a dGPU node, then treat the whole system as dGPU */
	if (!props->NumCPUCores && props->NumFComputeCores)
		is_dgpu = true;
}

device_status_t gpuid_to_nodeid(uint32_t gpu_id, uint32_t *node_id)
{
	uint64_t node_idx;

	for (node_idx = 0; node_idx < g_system->NumNodes; node_idx++) {
		if (g_props[node_idx].gpu_id == gpu_id) {
			*node_id = node_idx;
			return DEVICE_STATUS_SUCCESS;
		}
	}

	return DEVICE_STATUS_ERROR;

}

uint16_t get_device_id_by_node_id(HSAuint32 node_id)
{
	if (!g_props || !g_system || g_system->NumNodes <= node_id)
		return 0;

	return g_props[node_id].core->DeviceId;
}

device_status_t validate_nodeid_array(uint32_t **gpu_id_array,
		uint32_t NumberOfNodes, uint32_t *NodeArray)
{
	device_status_t ret;
	unsigned int i;

	if (NumberOfNodes == 0 || !NodeArray || !gpu_id_array)
		return DEVICE_STATUS_ERROR;

	/* Translate Node IDs to gpu_ids */
	*gpu_id_array = (uint32_t*)malloc(NumberOfNodes * sizeof(uint32_t));
	if (!(*gpu_id_array))
		return DEVICE_STATUS_ERROR;
	for (i = 0; i < NumberOfNodes; i++) {
		ret = validate_nodeid(NodeArray[i], *gpu_id_array + i);
		if (ret != DEVICE_STATUS_SUCCESS) {
			free(*gpu_id_array);
			break;
		}
	}

	return ret;
}

uint16_t get_device_id_by_gpu_id(HSAuint32 gpu_id)
{
	unsigned int i;

	if (!g_props || !g_system)
		return 0;

	for (i = 0; i < g_system->NumNodes; i++) {
		if (g_props[i].gpu_id == gpu_id)
			return g_props[i].core->DeviceId;
	}

	return 0;
}

/* Find the CPU that this GPU (gpu_node) directly connects to */
int32_t gpu_get_direct_link_cpu(uint32_t gpu_node, node_props_t *nodes)
{
	HsaIoLinkProperties *props = nodes[gpu_node].link;
	uint32_t i;

	if (!nodes[gpu_node].gpu_id || !props ||
			nodes[gpu_node].core->NumIOLinks == 0)
		return -1;

	for (i = 0; i < nodes[gpu_node].core->NumIOLinks; i++)
		if (props[i].IoLinkType == HSA_IOLINKTYPE_PCIEXPRESS &&
			props[i].Weight <= 20) /* >20 is GPU->CPU->GPU */
			return props[i].NodeTo;

	return -1;
}

uint32_t get_direct_link_cpu(uint32_t gpu_node)
{
	HSAuint64 size = 0;
	int32_t cpu_id;
	HSAuint32 i;

	cpu_id = gpu_get_direct_link_cpu(gpu_node, g_props);
	if (cpu_id == -1)
		return INVALID_NODEID;

	assert(g_props[cpu_id].mem);

	for (i = 0; i < g_props[cpu_id].core->NumMemoryBanks; i++)
		size += g_props[cpu_id].mem[i].SizeInBytes;

	return size ? (uint32_t)cpu_id : INVALID_NODEID;
}


/* Get node1->node2 IO link information. This should be a direct link that has
 * been created in the kernel.
 */
static device_status_t get_direct_iolink_info(uint32_t node1, uint32_t node2,
					    node_props_t *node_props, HSAuint32 *weight,
					    HSA_IOLINKTYPE *type)
{
	HsaIoLinkProperties *props = node_props[node1].link;
	uint32_t i;

	if (!props)
		return DEVICE_STATUS_ERROR;

	for (i = 0; i < node_props[node1].core->NumIOLinks; i++)
		if (props[i].NodeTo == node2) {
			if (weight)
				*weight = props[i].Weight;
			if (type)
				*type = props[i].IoLinkType;
			return DEVICE_STATUS_SUCCESS;
		}

	return DEVICE_STATUS_ERROR;
}

