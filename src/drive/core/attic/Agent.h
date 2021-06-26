#pragma once

#include <assert.h>

#include <vector>

// schi #include "core/inc/checked.h"
// #include "inc/isa.h"
// #include "inc/queue.h"
// #include "inc/MemoryRegion.h"
#include "util/utils.h"
#include "inc/drive_api.h"

namespace core {
class Signal;

typedef void (*HsaEventCallback)(status_t status, queue_t* source,
                                 void* data);

class MemoryRegion;

// Agent is intended to be an pure interface class and may be wrapped or
// replaced by tools libraries. All funtions other than Convert, node_id,
// agent_type, and public_handle must be virtual.
class Agent {
 public:
    #if 0
  // @brief Convert agent object into agent_t.
  // @param [in] agent Pointer to an agent.
  // @retval agent_t
  static agent_t Handle(Agent* agent) {
    const agent_t agent_handle = {
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(agent))};
    return agent_handle;
  }

  // @brief Convert agent object into const agent_t.
  // @param [in] agent Pointer to an agent.
  // @retval const agent_t
  static const agent_t Handle(const Agent* agent) {
    const agent_t agent_handle = {
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(agent))};
    return agent_handle;
  }

  // @brief Convert agent_t handle into Agent*.
  // @param [in] agent An agent_t handle.
  // @retval Agent*
  static Agent* Object(agent_t agent) {
    return reinterpret_cast<Agent*>(agent.handle);
  }
#endif

  // Lightweight RTTI for vendor specific implementations.
  enum AgentType { kGpu = 0, kCpu = 1, kUnknown = 2 };

  // @brief Agent class contructor.
  //
  // @param [in] type CPU or GPU or other.
  explicit Agent(uint32_t node_id, AgentType type)
      : node_id_(node_id),
        agent_type_(uint32_t(type)),
        profiling_enabled_(false) {
    // public_handle_ = Handle(this);
  }

  // @brief Agent class contructor.
  //
  // @param [in] type CPU or GPU or other.
  explicit Agent(uint32_t node_id, uint32_t type)
      : node_id_(node_id), agent_type_(type), profiling_enabled_(false) {
    // public_handle_ = Handle(this);
  }

  // @brief Agent class destructor.
  virtual ~Agent() {}

  // @brief Submit DMA copy command to move data from src to dst and wait
  // until it is finished.
  //
  // @details The agent must be able to access @p dst and @p src.
  //
  // @param [in] dst Memory address of the destination.
  // @param [in] src Memory address of the source.
  // @param [in] size Copy size in bytes.
  //
  // @retval SUCCESS The memory copy is finished and successful.
  virtual status_t DmaCopy(void* dst, const void* src, size_t size) {
    return ERROR;
  }

  // @brief Submit DMA copy command to move data from src to dst. This call
  // does not wait until the copy is finished
  //
  // @details The agent must be able to access @p dst and @p src. Memory copy
  // will be performed after all signals in @p dep_signals have value of 0.
  // On memory copy completion, the value of out_signal is decremented.
  //
  // @param [in] dst Memory address of the destination.
  // @param [in] dst_agent Agent that owns the memory pool associated with @p
  // dst.
  // @param [in] src Memory address of the source.
  // @param [in] src_agent Agent that owns the memory pool associated with @p
  // src.
  // @param [in] size Copy size in bytes.
  // @param [in] dep_signals Array of signal dependency.
  // @param [in] out_signal Completion signal.
  //
  // @retval SUCCESS The memory copy is finished and successful.
  // TODO remote DMA_SUA
  virtual status_t DmaCopy(void* dst, core::IAgent& dst_agent,
                               const void* src, core::IAgent& src_agent,
                               size_t size,
                               std::vector<signal_t>& dep_signals,
                               signal_t out_signal) {
                               // core::ISignal& out_signal) {
    return ERROR;
  }

  // @brief Submit DMA command to set the content of a pointer and wait
  // until it is finished.
  //
  // @details The agent must be able to access @p ptr
  //
  // @param [in] ptr Address of the memory to be set.
  // @param [in] value The value/pattern that will be used to set @p ptr.
  // @param [in] count Number of uint32_t element to be set.
  //
  // @retval SUCCESS The memory fill is finished and successful.
  virtual status_t DmaFill(void* ptr, uint32_t value, size_t count) {
    return ERROR;
  }

  // @brief Invoke the user provided callback for each region accessible by
  // this agent.
  //
  // @param [in] callback User provided callback function.
  // @param [in] data User provided pointer as input for @p callback.
  //
  // @retval ::SUCCESS if the callback function for each traversed
  // region returns ::SUCCESS.
  virtual status_t IterateRegion(
      status_t (*callback)(const MemoryRegion* region, void* data),
      void* data) const = 0;

  // @brief Invoke the callback for each cache useable by this agent.
  virtual status_t IterateCache(status_t (*callback)(cache_t cache, void* data),
                                    void* data) const = 0;

  // @brief Create queue.
  //
  // @param [in] size Number of packets the queue is expected to hold. Must be a
  // power of 2 greater than 0.
  // @param [in] queue_type Queue type.
  // @param [in] event_callback Callback invoked for every
  // asynchronous event related to the newly created queue. May be NULL.The HSA
  // runtime passes three arguments to the callback : a code identifying the
  // event that triggered the invocation, a pointer to the queue where the event
  // originated, and the application data.
  // @param [in] data Application data that is passed to @p callback.
  // @param [in] private_segment_size A hint to indicate the maximum expected
  // private segment usage per work-item, in bytes.
  // @param [in] group_segment_size A hint to indicate the maximum expected
  // group segment usage per work-group, in bytes.
  // @param[out] queue Memory location where the HSA runtime stores a pointer
  // to the newly created queue.
  //
  // @retval SUCCESS The queue has been created successfully.
  virtual status_t QueueCreate(size_t size, queue_type32_t queue_type,
                                   HsaEventCallback event_callback, void* data,
                                   uint32_t private_segment_size,
                                   uint32_t group_segment_size,
                                   queue_t* queue) = 0;
                                   // Queue** queue) = 0;

  // @brief Query the value of an attribute.
  //
  // @param [in] attribute Attribute to query.
  // @param [out] value Pointer to store the value of the attribute.
  //
  // @param SUCCESS @p value has been filled with the value of the
  // attribute.
  virtual status_t GetInfo(agent_info_t attribute,
                               void* value) const = 0;

  // @brief Returns an array of regions owned by the agent.
  virtual const std::vector<const core::IMemoryRegion*>& regions() const = 0;

  // @details Returns the agent's instruction set architecture.
  // virtual const Isa* isa() const = 0;

  virtual uint64_t HiveId() const { return 0; }
  // @brief Returns the agent type (CPU/GPU/Others).
  __forceinline uint32_t agent_type() const { return agent_type_; }

  // @brief Returns agent_t handle exposed to end user.
  //
  // @details Only matters when tools library need to intercept HSA calls.
  // __forceinline agent_t public_handle() const { return public_handle_; }

  // @brief Returns node id associated with this agent.
  __forceinline uint32_t node_id() const { return node_id_; }

  // @brief Getter for profiling_enabled_.
  __forceinline bool profiling_enabled() const { return profiling_enabled_; }

  // @brief Setter for profiling_enabled_.
  virtual status_t profiling_enabled(bool enable) {
    const status_t stat = EnableDmaProfiling(enable);
    if (SUCCESS == stat) {
      profiling_enabled_ = enable;
    }

    return stat;
  }


 protected:
  // Intention here is to have a polymorphic update procedure for public_handle_
  // which is callable on any Agent* but only from some class dervied from
  // Agent*.  do_set_public_handle should remain protected or private in all
  // derived types.
#if 0
  static __forceinline void set_public_handle(Agent* agent,
                                              agent_t handle) {
    agent->do_set_public_handle(handle);
  }

  virtual void do_set_public_handle(agent_t handle) {
    public_handle_ = handle;
  }
#endif
  // @brief Enable profiling of the asynchronous DMA copy. The timestamp
  // of each copy request will be stored in the completion signal structure.
  //
  // @param enable True to enable profiling. False to disable profiling.
  //
  // @retval SUCCESS The profiling is enabled and the
  // timing of subsequent async copy will be measured.
  virtual status_t EnableDmaProfiling(bool enable) {
    return SUCCESS;
  }

//  agent_t public_handle_;

 private:
  // @brief Node id.
  const uint32_t node_id_;

  const uint32_t agent_type_;

  bool profiling_enabled_;

  // Forbid copying and moving of this object
  DISALLOW_COPY_AND_ASSIGN(Agent);
};
}  // namespace core

