#pragma once

#include <vector>

// schi TODO #include "thunk_ref/include/hsakmt.h"

#include "inc/platform.h"
#include "inc/agent.h"
#include "inc/queue.h"
#include "inc/cache.h"
#include "inc/isa.h"

namespace hcs {
// @brief Class to represent a CPU device.
class CpuAgent : public core::IAgent {
 public:
  // @brief CpuAgent constructor.
  //
  // @param [in] node Node id. Each CPU in different socket will get distinct
  // id.
  // @param [in] node_props Node property.
  CpuAgent(HSAuint32 node, const HsaCoreProperties* node_props);

  // @brief CpuAgent destructor.
  ~CpuAgent();

  // @brief Invoke the user provided callback for each region accessible by
  // this agent.
  //
  // @param [in] include_peer If true, the callback will be also invoked on each
  // peer memory region accessible by this agent. If false, only invoke the
  // callback on memory region owned by this agent.
  // @param [in] callback User provided callback function.
  // @param [in] data User provided pointer as input for @p callback.
  //
  // @retval ::SUCCESS if the callback function for each traversed
  // region returns ::SUCCESS.
  status_t VisitRegion(bool include_peer,
                           status_t (*callback)(region_t region,
                                                    void* data),
                           void* data) const;

  // @brief Override from core::IAgent.
  status_t IterateRegion(status_t (*callback)(region_t region,
                                                      void* data),
                             void* data) const override;

  // @brief Override from core::IAgent.
  status_t IterateCache(status_t (*callback)(cache_t cache, void* data),
                            void* value) const override;

  // @brief Override from core::IAgent.
  status_t GetInfo(hsa_agent_info_t attribute, void* value) const override;

  // @brief Override from core::IAgent.
  status_t QueueCreate(size_t size, queue_type32_t queue_type,
                           core::IHsaEventCallback event_callback, void* data,
                           uint32_t private_segment_size,
                           uint32_t group_segment_size,
                           core::IQueue** queue) override;

  // @brief Returns number of data caches.
  __forceinline size_t num_cache() const { return cache_props_.size(); }
  // @brief Returns Hive ID
  __forceinline uint64_t HiveId() const override { return  properties_->HiveID; }

  // @brief Returns data cache property.
  //
  // @param [in] idx Cache level.
  __forceinline const HsaCacheProperties& cache_prop(int idx) const {
    return cache_props_[idx];
  }

  // @brief Override from core::IAgent.
  const std::vector<const core::IMemoryRegion*>& regions() const override {
    return regions_;
  }

  // @brief OVerride from core::IAgent.
  const core::Isa* isa() const override { return NULL; }

 private:
  // @brief Query the driver to get the region list owned by this agent.
  void InitRegionList();

  // @brief Query the driver to get the cache properties.
  void InitCacheList();

  // @brief Invoke the user provided callback for every region in @p regions.
  //
  // @param [in] regions Array of region object.
  // @param [in] callback User provided callback function.
  // @param [in] data User provided pointer as input for @p callback.
  //
  // @retval ::SUCCESS if the callback function for each traversed
  // region returns ::SUCCESS.
  status_t VisitRegion(
      const std::vector<const core::IMemoryRegion*>& regions,
      status_t (*callback)(region_t region, void* data),
      void* data) const;

  // @brief Node property.
  const HsaCoreProperties *properties_;

  // @brief Array of data cache property. The array index represents the cache
  // level.
  std::vector<HsaCacheProperties> cache_props_;

  // @brief Array of HSA cache objects.
  std::vector<std::unique_ptr<core::ICache>> caches_;

  // @brief Array of regions owned by this agent.
  std::vector<const core::IMemoryRegion*> regions_;

  DISALLOW_COPY_AND_ASSIGN(CpuAgent);
};

}  // namespace hcs

