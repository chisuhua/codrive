#include "inc/Topology.h"
#include "inc/Device.h"
#include "cmdio/cmdio.h"


uint16_t Topology::get_device_id_by_gpu_id(uint32_t gpu_id)
{
	unsigned int i;

	if (!device_->props_.size() || !device_->system_)
		return 0;

	for (i = 0; i < device_->system_->NumNodes; i++) {
		if (device_->props_[i]->gpu_id == gpu_id)
			return device_->props_[i]->core->DeviceId;
	}

	return 0;
}

uint16_t Topology::get_device_id_by_node_id(uint32_t node_id)
{
	if (!device_->props_.size() || !device_->system_ || device_->system_->NumNodes <= node_id)
		return 0;

	return device_->props_[node_id]->core->DeviceId;
}

/* Find the CPU that this GPU (gpu_node) directly connects to */
int32_t Topology::gpu_get_direct_link_cpu(uint32_t gpu_node, std::vector<node_props_t*>& nodes)
{
	HsaIoLinkProperties *props = nodes[gpu_node]->link;
	uint32_t i;

	if (!nodes[gpu_node]->gpu_id || !props ||
			nodes[gpu_node]->core->NumIOLinks == 0)
		return -1;

	for (i = 0; i < nodes[gpu_node]->core->NumIOLinks; i++)
		if (props[i].IoLinkType == HSA_IOLINKTYPE_PCIEXPRESS &&
			props[i].Weight <= 20) /* >20 is GPU->CPU->GPU */
			return props[i].NodeTo;

	return -1;
}

uint32_t Topology::get_direct_link_cpu(uint32_t gpu_node)
{
	uint64_t size = 0;
	int32_t cpu_id;
	uint32_t i;

	cpu_id = gpu_get_direct_link_cpu(gpu_node, device_->props_);
	if (cpu_id == -1)
		return INVALID_NODEID;

	assert(device_->props_[cpu_id]->mem);

	for (i = 0; i < device_->props_[cpu_id]->core->NumMemoryBanks; i++)
		size += device_->props_[cpu_id]->mem[i].SizeInBytes;

	return size ? (uint32_t)cpu_id : INVALID_NODEID;
}


/* Get node1->node2 IO link information. This should be a direct link that has
 * been created in the kernel.
 */
device_status_t Topology::get_direct_iolink_info(uint32_t node1, uint32_t node2,
					    node_props_t *node_props, uint32_t *weight,
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

void Topology::topology_setup_is_dgpu_param(HsaCoreProperties *props)
{
	/* if we found a dGPU node, then treat the whole system as dGPU */
	if (!props->NumCPUCores && props->NumFComputeCores)
		device_->set_dgpu();
}

bool Topology::topology_is_svm_needed(uint16_t device_id)
{
	const struct hsa_device_table *hsa_device;

	//if (topology_is_dgpu(device_id))
	return true;
}

device_status_t Topology::gpuid_to_nodeid(uint32_t gpu_id, uint32_t *node_id)
{
	uint64_t node_idx;

	for (node_idx = 0; node_idx < device_->system_->NumNodes; node_idx++) {
		if (device_->props_[node_idx]->gpu_id == gpu_id) {
			*node_id = node_idx;
			return DEVICE_STATUS_SUCCESS;
		}
	}

	return DEVICE_STATUS_ERROR;
}

device_status_t Topology::validate_nodeid(uint32_t nodeid, uint32_t *gpu_id)
{
	if (!device_->props_.size() || !device_->system_ || device_->system_->NumNodes <= nodeid)
		return DEVICE_STATUS_ERROR;
	if (gpu_id)
		*gpu_id = device_->props_[nodeid]->gpu_id;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t Topology::validate_nodeid_array(uint32_t **gpu_id_array,
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

