#pragma once

// #include "inc/pps_ginternal.h"
//#include "inc/pps.h"
// #include "core/inc/checked.h"
#include "util/utils.h"
#include <utility>
#include <string>

namespace core {

class ICache {
 public:
  /*
  static __forceinline cache_t Convert(const Cache* cache) {
    const cache_t handle = {static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cache))};
    return handle;
  }
  static __forceinline Cache* Convert(const cache_t cache) {
    return reinterpret_cast<Cache*>(static_cast<uintptr_t>(cache.handle));
  }
  */

  ICache(const std::string& name, uint8_t level, uint32_t size)
      : name_(name), level_(level), size_(size) {}

  ICache(std::string&& name, uint8_t level, uint32_t size)
      : name_(std::move(name)), level_(level), size_(size) {}

  status_t GetInfo(cache_info_t attribute, void* value);

 private:
  std::string name_;
  uint32_t level_;
  uint32_t size_;

  // Forbid copying and moving of this object
  DISALLOW_COPY_AND_ASSIGN(ICache);
};
}

