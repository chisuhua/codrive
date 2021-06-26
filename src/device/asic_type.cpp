#include "inc/pps_ext.h"
#include "inc/asic_type.h"

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <iostream>

#define DEVICE_ID_PPU 0x0000

static inline bool getPPUAsic(uint32_t device_id,
                             HcsDeviceAsicType *device_asic) {

  switch (device_id) {
    case DEVICE_ID_PPU:
    *device_asic = HcsDeviceAsicTypePPU;
    return true;
  }
  return false;
}


status_t hcsGetAsicFamilyType(const device_t agent, 
                                     HcsDeviceAsicType *device_asic) {
  // Determine input parameters are valid
  if ((agent.handle == 0) || (device_asic == NULL)) {
    return ERROR_INVALID_ARGUMENT;
  }

  status_t status;
  uint32_t device_id;
  hsa_agent_info_t attribute = static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_CHIP_ID);
  status = hsa_device_get_info(agent, attribute, (void *)&device_id);
  if (status != SUCCESS) {
    return status;
  }

  // Use the agent id to determine Fiji ASIC
  if (getPPUAsic(device_id, device_asic) == true) {
    return SUCCESS;
  }

  return ERROR_INVALID_ARGUMENT;
}

