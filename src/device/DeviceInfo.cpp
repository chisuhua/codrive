#include "inc/DeviceInfo.h"
#include <vector>
#include <string>
#include <unordered_map>

DeviceInfo::DeviceInfo() {
    device_table_.push_back({ 0xccdd, 1, 0, 0, 1, "PPU", CHIP_PPUe, 4096, DOORBELL_SIZE});
    device_table_.push_back({ 0xccee, 1, 0, 0, 1, "PPU", CHIP_PPU, 4096, DOORBELL_SIZE});
}

static DeviceInfo g_device_info;

const device_info* DeviceInfo::find_device(uint16_t device_id)
{
	uint32_t table_size = device_table_.size();

	for (uint32_t i = 0; i < table_size; i++) {
		if (device_table_[i].device_id == device_id)
			return &device_table_[i];
	}
	return nullptr;
}

device_status_t DeviceInfo::topology_get_asic_family(uint16_t device_id,
					enum asic_family_type *asic)
{
	const device_info *device = find_device(device_id);

	if (!device)
		return DEVICE_STATUS_ERROR;

	*asic = device->asic_family;
	return DEVICE_STATUS_SUCCESS;
}

void DeviceInfo::print_device_id_array(uint32_t *device_id_array, uint32_t device_id_array_size)
{
#ifdef DEBUG_PRINT_APERTURE
	device_id_array_size /= sizeof(uint32_t);

	pr_info("device id array size %d\n", device_id_array_size);

	for (uint32_t i = 0 ; i < device_id_array_size; i++)
		pr_info("%d . 0x%x\n", (i+1), device_id_array[i]);
#endif
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

