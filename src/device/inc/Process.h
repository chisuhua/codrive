#pragma once
#include "cmdio/cmdio.h"

class Process {
public:
    Process() {};

    device_status_t get_process_apertures(
                process_device_apertures **process_apertures,
	            uint32_t *num_of_nodes);

    int32_t gpu_get_direct_link_cpu(uint32_t gpu_node, node_props_t *nodes);
    uint32_t get_direct_link_cpu(uint32_t gpu_node);
    device_status_t get_direct_iolink_info(uint32_t node1, uint32_t node2,
					    node_props_t *node_props, HSAuint32 *weight,
					    HSA_IOLINKTYPE *type);

    process_device_apertures *apertures_ {nullptr};
};
