#pragma once

#include <vector>

// schi TODO #include "thunk_ref/include/hsakmt.h"

#include "inc/Runtime.h"
// #include "inc/agent.h"
#include "core/blit.h"
// #include "inc/signal.h"
#include "core/IAgent.h"
#include "core/ISignal.h"
#include "inc/cache.h"
#include "lazy_ptr.h"
#include "util/locks.h"
#include "util/small_heap.h"

#include "StreamType.h"

class MemoryRegion;
using namespace core;

// @brief Contains scratch memory information.
struct ScratchInfo {
    void* queue_base;
    size_t size;
    size_t size_per_thread;
    uint32_t lanes_per_wave;
    ptrdiff_t queue_process_offset;
    bool large;
    bool retry;
    signal_t queue_retry;
    uint64_t wanted_slots;
};

// @brief Interface to represent a GPU agent.
class GpuAgentInt : public core::IAgent {
public:
    // @brief Constructor
    GpuAgentInt(uint32_t node_id)
        : core::IAgent(node_id, core::IAgent::AgentType::kGpu)
    {
    }

    // @brief Ensure blits are ready (performance hint).
    virtual void PreloadBlits() { }

    // @brief Initialization hook invoked after tools library has loaded,
    // to allow tools interception of interface functions.
    //
    // @retval SUCCESS if initialization is successful.
    virtual status_t PostToolsInit() = 0;

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
    virtual status_t VisitRegion(bool include_peer,
        status_t (*callback)(const core::IMemoryRegion* region,
            void* data),
        void* data) const = 0;

    // @brief Carve scratch memory from scratch pool.
    //
    // @param [in/out] scratch Structure to be populated with the carved memory
    // information.
    virtual void AcquireQueueScratch(ScratchInfo& scratch) = 0;

    // @brief Release scratch memory back to scratch pool.
    //
    // @param [in/out] scratch Scratch memory previously acquired with call to
    // ::AcquireQueueScratch.
    virtual void ReleaseQueueScratch(ScratchInfo& base) = 0;

    // @brief Translate the kernel start and end dispatch timestamp from agent
    // domain to host domain.
    //
    // virtual void TranslateTime(signal_t signal, hsa_amd_profiling_dispatch_time_t& time) = 0;

    // @brief Translate the async copy start and end timestamp from agent // domain to host domain.
    /*
    virtual void TranslateTime(signal_t signal, hsa_amd_profiling_async_copy_time_t& time)
    {
        return TranslateTime(signal, (hsa_amd_profiling_dispatch_time_t&)time);
    }*/

    // @brief Translate timestamp agent domain to host domain.
    //
    // @param [out] time Timestamp in agent domain.
    virtual uint64_t TranslateTime(uint64_t tick) = 0;

    // @brief Invalidate caches on the agent which may hold code object data.
    virtual void InvalidateCodeCaches() = 0;

    // @brief Sets the coherency type of this agent.
    virtual bool current_coherency_type(hsa_amd_coherency_type_t type) = 0;

    // @brief Returns the current coherency type of this agent.
    virtual hsa_amd_coherency_type_t current_coherency_type() const = 0;

    // @brief Query the agent HSA profile.
    //
    // @retval HSA profile.
    virtual profile_t profile() const = 0;

    // @brief Query the agent memory bus width in bit.
    //
    // @retval Bus width in bit.
    virtual uint32_t memory_bus_width() const = 0;

    // @brief Query the agent memory maximum frequency in MHz.
    //
    // @retval Bus width in MHz.
    virtual uint32_t memory_max_frequency() const = 0;
};
// using namespace hcs;
class GpuAgent : public GpuAgentInt {
public:
    // @brief GPU agent constructor.
    //
    // @param [in] node Node id. Each CPU in different socket will get distinct
    // id.
    // @param [in] node_props Node property.
    GpuAgent(HSAuint32 node, const HsaCoreProperties* node_props);

    // @brief GPU agent destructor.
    ~GpuAgent();

    // @brief Ensure blits are ready (performance hint).
    void PreloadBlits() override;

    // @brief Override from core::IAgent.
    status_t PostToolsInit() override;

    uint16_t GetMicrocodeVersion() const;

    uint16_t GetSdmaMicrocodeVersion() const;

    // @brief Assembles SP3 shader source into ISA or AQL code object.
    //
    // @param [in] src_sp3 SP3 shader source text representation.
    // @param [in] func_name Name of the SP3 function to assemble.
    // @param [in] assemble_target ISA or AQL assembly target.
    // @param [out] code_buf Code object buffer.
    // @param [out] code_buf_size Size of code object buffer in bytes.
    enum class AssembleTarget { ISA,
        AQL };

    void AssembleShader(const char* func_name, AssembleTarget assemble_target, void*& code_buf,
        size_t& code_buf_size) const;

    // @brief Frees code object created by AssembleShader.
    //
    // @param [in] code_buf Code object buffer.
    // @param [in] code_buf_size Size of code object buffer in bytes.
    void ReleaseShader(void* code_buf, size_t code_buf_size) const;

    // @brief Override from core::IAgent.
    status_t VisitRegion(bool include_peer,
        status_t (*callback)(const core::IMemoryRegion* region,
            void* data),
        void* data) const override;

    // @brief Override from core::IAgent.
    status_t IterateRegion(status_t (*callback)(const core::IMemoryRegion* region,
                               void* data),
        void* data) const override;

    // @brief Override from core::IAgent.
    status_t IterateCache(status_t (*callback)(core::ICache* cache, void* data),
        void* value) const override;

    // @brief Override from core::IAgent.
    // status_t DmaCopy(void* dst, const void* src, size_t size, uint8_t dir = DMA_SUA) override;
    status_t DmaCopy(void* dst, const void* src, size_t size) override;

    // @brief Override from core::IAgent.
    status_t DmaCopy(void* dst, core::IAgent& dst_agent, const void* src,
        core::IAgent& src_agent, size_t size,
        std::vector<signal_t>& dep_signals,
        signal_t out_signal) override;
    // std::vector<signal_t>& dep_signals,
    // core::ISignal& out_signal) override;
    // @brief Override from core::IAgent.
    /*
    status_t DmaCopyRect(const hsa_pitched_ptr_t* dst, const hsa_dim3_t* dst_offset,
        const hsa_pitched_ptr_t* src, const hsa_dim3_t* src_offset,
        const hsa_dim3_t* range, // hsa_amd_copy_direction_t dir,
        std::vector<signal_t>& dep_signals, core::Signal& out_signal);
        */

    // @brief Override from core::IAgent.
    status_t DmaFill(void* ptr, uint32_t value, size_t count) override;

    // @brief Override from core::IAgent.
    status_t GetInfo(agent_info_t attribute, void* value) const override;

    // @brief Override from core::IAgent.
    status_t QueueCreate(size_t size, queue_type32_t queue_type,
        core::HsaEventCallback event_callback, void* data,
        uint32_t private_segment_size,
        uint32_t group_segment_size,
        queue_t* queue) override;
    // core::IQueue** queue) override;

    // status_t CreateCSPQueue(size_t size, queue_type32_t queue_type,
    //    core::IHsaEventCallback event_callback, void* data, core::Queue** queue);

    // @brief Override from hcs::GpuAgentInt.
    void AcquireQueueScratch(ScratchInfo& scratch) override;

    // @brief Override from hcs::GpuAgentInt.
    void ReleaseQueueScratch(ScratchInfo& scratch) override;

    // @brief Override from AMD::GpuAgentInt.
    // void TranslateTime(signal_t signal, hsa_amd_profiling_dispatch_time_t& time) override;

    // @brief Override from hcs::GpuAgentInt.
    // void TranslateTime(signal_t signal, hsa_amd_profiling_async_copy_time_t& time) override;

    // @brief Override from hcs::GpuAgentInt.
    uint64_t TranslateTime(uint64_t tick) override;

    // @brief Override from hcs::GpuAgentInt.
    void InvalidateCodeCaches() override;

    // @brief Override from hcs::GpuAgentInt.
    bool current_coherency_type(hsa_amd_coherency_type_t type) override;

    hsa_amd_coherency_type_t current_coherency_type() const override
    {
        return current_coherency_type_;
    }

    // Getter & setters.
    // @brief Returns Hive ID
    __forceinline uint64_t HiveId() const override { return properties_->HiveID; }

    // @brief Returns node property.
    const HsaCoreProperties* properties() const
    {
        return properties_;
    }

    // @brief Returns number of data caches.
    size_t num_cache() const { return cache_props_.size(); }

    // @brief Returns data cache property.
    //
    // @param [in] idx Cache level.
    const HsaCacheProperties& cache_prop(int idx) const
    {
        return cache_props_[idx];
    }

    // @brief Override from core::IAgent.
    const std::vector<const core::IMemoryRegion*>& regions() const override
    {
        return regions_;
    }

    // @brief Override from core::IAgent.
    // const core::IIsa* isa() const override { return isa_; }

    // @brief Override from hcs::GpuAgentInt.
    profile_t profile() const override { return profile_; }

    // @brief Override from hcs::GpuAgentInt.
    uint32_t memory_bus_width() const override
    {
        return memory_bus_width_;
    }

    // @brief Override from hcs::GpuAgentInt.
    uint32_t memory_max_frequency() const override
    {
        return memory_max_frequency_;
    }

protected:
    static const uint32_t minAqlSize_ = 0x1000; // 4KB min
    static const uint32_t maxAqlSize_ = 0x20000; // 8MB max

    // @brief Create a queue through HSA API to allow tools to intercept.
    queue_t* CreateInterceptibleQueue();

    // @brief Create SDMA blit object.
    //
    // @retval NULL if SDMA blit creation and initialization failed.
    // schi core::IBlit* CreateBlitSdma(bool h2d);

    // @brief Create Kernel blit object using provided compute queue.
    //
    // @retval NULL if Kernel blit creation and initialization failed.
    // schi core::IBlit* CreateBlitKernel(core::Queue* queue);

    // @brief Invoke the user provided callback for every region in @p regions.
    //
    // @param [in] regions Array of region object.
    // @param [in] callback User provided callback function.
    // @param [in] data User provided pointer as input for @p callback.
    //
    // @retval ::SUCCESS if the callback function for each traversed
    // region returns ::SUCCESS.
    status_t VisitRegion(const std::vector<const core::IMemoryRegion*>& regions,
        status_t (*callback)(const core::IMemoryRegion* region, void* data),
        void* data) const;

    // @brief Update ::t1_ tick count.
    void SyncClocks();

    // @brief Binds the second-level trap handler to this node.
    void BindTrapHandler();

    // @brief Override from core::IAgent.
    status_t EnableDmaProfiling(bool enable) override;

    // @brief Node properties.
    const HsaCoreProperties* properties_;

    // @brief Current coherency type.
    hsa_amd_coherency_type_t current_coherency_type_;

    // @brief Maximum number of queues that can be created.
    uint32_t max_queues_;

    // @brief Object to manage scratch memory.
    SmallHeap scratch_pool_;

    // @brief Current short duration scratch memory size.
    size_t scratch_used_large_;

    // @brief Notifications for scratch release.
    std::map<uint64_t, signal_value_t> scratch_notifiers_;

    // @brief Default scratch size per queue.
    size_t queue_scratch_len_;

    // @brief Default scratch size per work item.
    size_t scratch_per_thread_;

    // @brief Blit interfaces for each data path.
    enum BlitEnum { BlitDevToDev,
        BlitHostToDev,
        BlitDevToHost,
        DefaultBlitCount };

    // Blit objects managed by an instance of GpuAgent
    std::vector<lazy_ptr<core::IBlit>> blits_;

    // List of agents connected via xGMI
    std::vector<const core::IAgent*> xgmi_peer_list_;

    // @brief AQL queues for cache management and blit compute usage.
    enum QueueEnum {
        QueueUtility, // Cache management and agent to {host,agent} blit compute
        QueueBlitOnly, // Host to agent blit
        QueueCount
    };

    // lazy_ptr<core::IQueue> queues_[QueueCount];
    lazy_ptr<queue_t> queues_[QueueCount];

    // @brief Mutex to protect the update to coherency type.
    KernelMutex coherency_lock_;

    // @brief Mutex to protect access to scratch pool.
    KernelMutex scratch_lock_;

    // @brief Mutex to protect access to ::t1_.
    KernelMutex t1_lock_;

    // @brief Mutex to protect access to blit objects.
    KernelMutex blit_lock_;

    // @brief GPU tick on initialization.
    HsaClockCounters t0_;

    HsaClockCounters t1_;

    double historical_clock_ratio_;

    // @brief Array of GPU cache property.
    std::vector<HsaCacheProperties> cache_props_;

    // @brief Array of HSA cache objects.
    std::vector<std::unique_ptr<core::ICache>> caches_;

    // @brief Array of regions owned by this agent.
    std::vector<const core::IMemoryRegion*> regions_;

    MemoryRegion* local_region_;

    // core::IIsa* isa_;

    // @brief HSA profile.
    profile_t profile_;

    void* trap_code_buf_;

    size_t trap_code_buf_size_;

    // @brief Mappings from doorbell index to queue, for trap handler.
    // Correlates with output of s_sendmsg(MSG_GET_DOORBELL) for queue identification.
    co_queue_t** doorbell_queue_map_;

    // @brief The GPU memory bus width in bit.
    uint32_t memory_bus_width_;

    // @brief The GPU memory maximum frequency in MHz.
    uint32_t memory_max_frequency_;

    // @brief HDP flush registers
    // hsa_amd_hdp_flush_t HDP_flush_ = { nullptr, nullptr };

    bool is_apu_node;

private:
    // @brief Query the driver to get the region list owned by this agent.
    void InitRegionList();

    // @brief Reserve memory for scratch pool to be used by AQL queue of this
    // agent.
    void InitScratchPool();

    // @brief Query the driver to get the cache properties.
    void InitCacheList();

    // @brief Create internal queues and blits.
    void InitDma();

    // @brief Setup GWS accessing queue.
    void InitGWS();

    // @brief Register signal for notification when scratch may become available.
    // @p signal is notified by OR'ing with @p value.
    bool AddScratchNotifier(signal_t signal, signal_value_t value)
    {
        if (signal.handle != 0) return false;
        scratch_notifiers_[signal.handle] = value;
        return true;
    }

    // @brief Deregister scratch notification signals.
    void ClearScratchNotifiers() { scratch_notifiers_.clear(); }

    // Bind index of peer agent that is connected via xGMI links
    lazy_ptr<core::IBlit>& GetXgmiBlit(const core::IAgent& peer_agent);

    // Bind the Blit object that will drive the copy operation
    // across PCIe links (H2D or D2H) or is within same agent D2D
    lazy_ptr<core::IBlit>& GetPcieBlit(const core::IAgent& dst_agent, const core::IAgent& src_agent);

    // Bind the Blit object that will drive the copy operation
    lazy_ptr<core::IBlit>& GetBlitObject(const core::IAgent& dst_agent, const core::IAgent& src_agent);
    // @brief Alternative aperture base address. Only on KV.
    uintptr_t ape1_base_;

    // @brief Queue with GWS access.
    struct {
        // lazy_ptr<core::IQueue> queue_;
        lazy_ptr<queue_t> queue_;
        int ref_ct_;
        KernelMutex lock_;
    } gws_queue_;

    // core::IBlit* CreateCpDma(core::Queue* queue);
    core::IBlit* CreateCpDma(queue_t* queue);

    DISALLOW_COPY_AND_ASSIGN(GpuAgent);
};

