#pragma once

#if defined _WIN32 || defined __CYGWIN__
#include <initguid.h>
#endif

#include "platform/inc/pps.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// @brief Enum listing the various ASIC family types.
//
// @details Enum listing the various ASIC family types whose devices
// are supported by Hsa Runtime.
typedef enum HcsDeviceAsicType {

  // Greenland family of ASICs.
  HcsDeviceAsicTypePPU,

} HcsDeviceAsicType;

//---------------------------------------------------------------------------//
// @brief Get the AMD device type                                            //
//---------------------------------------------------------------------------//
status_t HcsGetAsicFamilyType(const device_t agent, 
                                     HcsDeviceAsicType *device_asic);

#ifdef __cplusplus
}
#endif  // __cplusplus
