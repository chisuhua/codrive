#include "inc/isa.h"

#include <cstring>
#include <sstream>

namespace core {

std::string Isa::GetFullName() const {
  std::stringstream full_name;
  full_name << GetArchitecture() << "-" << GetVendor() << "-" << GetOS() << "-"
            << GetEnvironment() << "-dla" << GetMajorVersion()
            << GetMinorVersion() << GetStepping();
  // full_name << "bi-ix-hcs--dla:1:0:0";

  return full_name.str();
}

bool Isa::GetInfo(const hsa_isa_info_t &attribute, void *value) const {
  if (!value) {
    return false;
  }

  switch (attribute) {
    case HSA_ISA_INFO_NAME_LENGTH: {
      std::string full_name = GetFullName();
      *((uint32_t*)value) = static_cast<uint32_t>(full_name.size() + 1);
      return true;
    }
    case HSA_ISA_INFO_NAME: {
      std::string full_name = GetFullName();
      memset(value, 0x0, full_name.size() + 1);
      memcpy(value, full_name.c_str(), full_name.size());
      return true;
    }
    // deprecated.
    case HSA_ISA_INFO_CALL_CONVENTION_COUNT: {
      *((uint32_t*)value) = 1;
      return true;
    }
    // deprecated.
    case HSA_ISA_INFO_CALL_CONVENTION_INFO_WAVEFRONT_SIZE: {
      *((uint32_t*)value) = 64;
      return true;
    }
    // deprecated.
    case HSA_ISA_INFO_CALL_CONVENTION_INFO_WAVEFRONTS_PER_COMPUTE_UNIT: {
      *((uint32_t*)value) = 40;
      return true;
    }
    case HSA_ISA_INFO_MACHINE_MODELS: {
      const bool machine_models[2] = {false, true};
      memcpy(value, machine_models, sizeof(machine_models));
      return true;
    }
    case HSA_ISA_INFO_PROFILES: {
      bool profiles[2] = {true, false};
      if (this->version() == Version(7, 0, 0) ||
          this->version() == Version(8, 0, 1)) {
        profiles[1] = true;
      }
      memcpy(value, profiles, sizeof(profiles));
      return true;
    }
    case HSA_ISA_INFO_DEFAULT_FLOAT_ROUNDING_MODES: {
      const bool rounding_modes[3] = {false, false, true};
      memcpy(value, rounding_modes, sizeof(rounding_modes));
      return true;
    }
    case HSA_ISA_INFO_BASE_PROFILE_DEFAULT_FLOAT_ROUNDING_MODES: {
      const bool rounding_modes[3] = {false, false, true};
      memcpy(value, rounding_modes, sizeof(rounding_modes));
      return true;
    }
    case HSA_ISA_INFO_FAST_F16_OPERATION: {
      if (this->GetMajorVersion() >= 8) {
        *((bool*)value) = true;
      } else {
        *((bool*)value) = false;
      }
      return true;
    }
    case HSA_ISA_INFO_WORKGROUP_MAX_DIM: {
      const uint16_t workgroup_max_dim[3] = {1024, 1024, 1024};
      memcpy(value, workgroup_max_dim, sizeof(workgroup_max_dim));
      return true;
    }
    case HSA_ISA_INFO_WORKGROUP_MAX_SIZE: {
      *((uint32_t*)value) = 1024;
      return true;
    }
    case HSA_ISA_INFO_GRID_MAX_DIM: {
      const hsa_dim3_t grid_max_dim = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
      memcpy(value, &grid_max_dim, sizeof(grid_max_dim));
      return true;
    }
    case HSA_ISA_INFO_GRID_MAX_SIZE: {
      *((uint64_t*)value) = UINT64_MAX;
      return true;
    }
    case HSA_ISA_INFO_FBARRIER_MAX_SIZE: {
      *((uint32_t*)value) = 32;
      return true;
    }
    default: {
      return false;
    }
  }
}


const Isa *IsaRegistry::GetIsa(const std::string &full_name) {
  auto isareg_iter = supported_isas_.find(full_name);
  return isareg_iter == supported_isas_.end() ? nullptr : &isareg_iter->second;
}

const Isa *IsaRegistry::GetIsa(const Isa::Version &version) {
  auto isareg_iter = supported_isas_.find(Isa(version).GetFullName());
  return isareg_iter == supported_isas_.end() ? nullptr : &isareg_iter->second;
}

const IsaRegistry::IsaMap IsaRegistry::supported_isas_ =
  IsaRegistry::GetSupportedIsas();

const IsaRegistry::IsaMap IsaRegistry::GetSupportedIsas() {
#define ISAREG_ENTRY_GEN(maj, min, stp)                          \
  Isa dla_##maj##min##stp;                                       \
  dla_##maj##min##stp.version_ = Isa::Version(maj, min, stp);    \
  supported_isas.insert(std::make_pair(                          \
      dla_##maj##min##stp.GetFullName(),                         \
      dla_##maj##min##stp));                                     \

  IsaMap supported_isas;

  ISAREG_ENTRY_GEN(1, 0, 0)
  ISAREG_ENTRY_GEN(1, 0, 1)

  return supported_isas;
}


} // namespace core
