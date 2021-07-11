#pragma once

#include <stdint.h>
#include <mutex>
#include <memory>
#include "inc/DeviceInfo.h"

class Doorbells;
class Device;
class MemMgr;
class Topology;

class process_doorbells {
public:
    process_doorbells(bool use_gpuvm, uint32_t size, void* mapping, Doorbells* doorbells)
        : use_gpuvm_(use_gpuvm)
        , size_(size)
        , mapping_(mapping)
        , doorbells_(doorbells)
    {}
	bool use_gpuvm_;
	uint32_t size_;
	void *mapping_;
    std::mutex mutex_;
    Doorbells *doorbells_;
};

class Doorbells {
public:
    device_status_t init_process_doorbells(unsigned int NumNodes);

    void get_doorbell_map_info(uint16_t dev_id,
				  std::shared_ptr<process_doorbells> doorbell);

    void destroy_process_doorbells(void);
    void clear_process_doorbells(void);

    device_status_t  map_doorbell_apu(uint32_t NodeId, uint32_t gpu_id,
				      uint64_t doorbell_mmap_offset);

    device_status_t map_doorbell_dgpu(uint32_t NodeId, uint32_t gpu_id,
				       uint64_t doorbell_mmap_offset);

    device_status_t map_doorbell(uint32_t NodeId, uint32_t gpu_id,
				  uint64_t doorbell_mmap_offset);

    std::shared_ptr<process_doorbells> get_doorbell (uint32_t node_id) {
        return doorbells_[node_id];
    }

    Device* device_;
    MemMgr* mm_;
    Topology* top_;
    uint32_t num_doorbells_nodes_;
    std::vector<std::shared_ptr<process_doorbells>> doorbells_;
};
