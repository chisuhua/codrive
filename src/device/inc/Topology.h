#pragma once
#include "inc/hsakmttypes.h"
#include <stdint.h>
#include "cmdio/cmdio.h"
#include <vector>

#define NON_VALID_GPU_ID 0

class Device;

class Topology {
public:
    device_status_t validate_nodeid(uint32_t nodeid, uint32_t *gpu_id);

    device_status_t validate_nodeid_array(uint32_t **gpu_id_array,
		uint32_t NumberOfNodes, uint32_t *NodeArray);

    uint16_t get_device_id_by_gpu_id(uint32_t gpu_id);
    uint16_t get_device_id_by_node_id(HSAuint32 node_id);

    int32_t gpu_get_direct_link_cpu(uint32_t gpu_node, std::vector<node_props_t*>& nodes);
    uint32_t get_direct_link_cpu(uint32_t gpu_node);
    device_status_t get_direct_iolink_info(uint32_t node1, uint32_t node2,
					    node_props_t *node_props, HSAuint32 *weight,
					    HSA_IOLINKTYPE *type);

    void topology_setup_is_dgpu_param(HsaCoreProperties *props);
    bool topology_is_svm_needed(uint16_t device_id);

    device_status_t gpuid_to_nodeid(uint32_t gpu_id, uint32_t *node_id);

    Device *device_;
};
