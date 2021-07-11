#pragma once
#include <stdint.h>
#include <vector>
#include "device_type.h"

#define DOORBELL_SIZE 8
#define DOORBELLS_PAGE_SIZE(ds) (1024 * (ds))

enum asic_family_type {
	CHIP_PPUe = 0,
	CHIP_PPU = 0,
	CHIP_LAST
};

enum Asic
{
  INVALID = 0,
  PPU
};


struct device_info {
	uint16_t device_id;		// Device ID
	unsigned char major;		// Major engine version
	unsigned char minor;		// Minor engine version
	unsigned char stepping;		// Stepping info
	unsigned char is_dgpu;		// Predicate for dGPU devices
	const char *dev_name;		// CALName of the device
	enum asic_family_type asic_family;
	uint32_t eop_buffer_size;
	uint32_t doorbell_size;
};

class DeviceInfo {
public:
    DeviceInfo();
    const struct device_info *find_device(uint16_t device_id);
    device_status_t topology_get_asic_family(uint16_t device_id,
					enum asic_family_type *asic);
    std::vector<device_info> device_table_;


    void print_device_id_array(uint32_t *device_id_array, uint32_t device_id_array_size);
};

