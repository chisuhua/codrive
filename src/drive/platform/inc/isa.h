#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>

#include "inc/pps.h"

namespace core {

class Isa {
 public:
  /// @brief Isa's version type.
  typedef std::tuple<int32_t, int32_t, int32_t> Version;

  /// @brief Default destructor.
  ~Isa() {}

  /// @returns Handle equivalent of @p isa_object.
  static hsa_isa_t Handle(const Isa *isa_object) {
    hsa_isa_t isa_handle = { reinterpret_cast<uint64_t>(isa_object) };
    return isa_handle;
  }
  /// @returns Object equivalent of @p isa_handle.
  static Isa *Object(const hsa_isa_t &isa_handle) {
    Isa *isa_object = (Isa*)(isa_handle.handle);
    return isa_object;
  }

  /// @returns This Isa's version.
  const Version &version() const {
    return version_;
  }
  /// @returns This Isa's supported wavefront.
  /*
  const Wavefront &wavefront() const {
    return wavefront_;
  }
  */

  /// @returns This Isa's architecture.
  std::string GetArchitecture() const {
    return "bi";
  }
  /// @returns This Isa's vendor.
  std::string GetVendor() const {
    return "ix";
  }
  /// @returns This Isa's OS.
  std::string GetOS() const {
    return "hcs";
  }
  /// @returns This Isa's environment.
  std::string GetEnvironment() const {
    return "";
  }
  /// @returns This Isa's major version.
  int32_t GetMajorVersion() const {
    return std::get<0>(version_);
  }
  /// @returns This Isa's minor version.
  int32_t GetMinorVersion() const {
    return std::get<1>(version_);
  }
  /// @returns This Isa's stepping.
  int32_t GetStepping() const {
    return std::get<2>(version_);
  }

  /// @brief Isa is always in valid state.
  bool IsValid() const {
    return true;
  }

  /// @returns This Isa's full name.
  std::string GetFullName() const;

  /// @brief Query value of requested @p attribute and record it in @p value.
  bool GetInfo(const hsa_isa_info_t &attribute, void *value) const;

  /// @returns Round method (single or double) used to implement the floating-
  /// point multiply add instruction (mad) for a given combination of @p fp_type
  /// and @p flush_mode.
  /*
  hsa_round_method_t GetRoundMethod(
      hsa_fp_type_t fp_type,
      hsa_flush_mode_t flush_mode) const;
      */

 private:
  /// @brief Default constructor.
  Isa(): version_(Version(-1, -1, -1)) {}

  /// @brief Construct from @p version.
  Isa(const Version &version): version_(version) {}


  /// @brief Isa's version.
  Version version_;

  /// @brief Isa's supported wavefront.
  //  Wavefront wavefront_;
  uint32_t warp_size_;

  /// @brief Isa's friends.
  friend class IsaRegistry;
}; // class Isa


/// @class IsaRegistry.
/// @brief Instruction Set Architecture Registry.
class IsaRegistry final {
 public:
  /// @returns Isa for requested @p full_name, null pointer if not supported.
  static const Isa *GetIsa(const std::string &full_name);
  /// @returns Isa for requested @p version, null pointer if not supported.
  static const Isa *GetIsa(const Isa::Version &version);

 private:
  /// @brief IsaRegistry's map type.
  typedef std::unordered_map<std::string, Isa> IsaMap;

  /// @brief Supported instruction set architectures.
  static const IsaMap supported_isas_;

  /// @brief Default constructor - not available.
  IsaRegistry();
  /// @brief Default destructor - not available.
  ~IsaRegistry();

  /// @returns Supported instruction set architectures.
  static const IsaMap GetSupportedIsas();
}; // class IsaRegistry


} // namespace core

