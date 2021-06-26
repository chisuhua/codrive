#pragma once

#include <map>
#include <vector>

//#include "inc/pps_ext.h"
#include "core/IAgent.h"
#include "core/IRuntime.h"
#include "loader/Loader.h"

// #include "inc/agent.h"
#include "core/IMemoryRegion.h"
#include "device_type.h"
#include "exceptions.h"
#include "inc/MemoryRegion.h"
#include "inc/Platform.h"
#include "util/flag.h"
#include "util/locks.h"
#include "util/os.h"
#include "util/utils.h"

class MemoryRegion;
class ICoStream;
class Device;
class Platform;

#define COSTREAM Runtime::runtime_singleton_->co_stream_
#define STREAM Runtime::runtime_singleton_->co_stream_
#define DEVICE Runtime::runtime_singleton_->device_
#define PLATFORM Runtime::runtime_singleton_->platform_

/// @brief  Runtime class provides the following functions:
/// - open and close connection to kernel driver.
/// - load supported extension library (image and finalizer).
/// - load tools library.
/// - expose supported agents.
/// - allocate and free memory.
/// - memory copy and fill.
/// - grant access to memory (dgpu memory pool extension).
/// - maintain loader state.
/// - monitor asynchronous event from agent.
class Runtime : public core::IRuntime {
    friend class MemoryRegion;

public:
    static status_t Acquire();
    static status_t Release();
    __forceinline static Runtime& getInstance();
    static bool IsOpen();
    static Runtime* runtime_singleton_;

    // @brief Callback handler for VM fault access.
    static bool VMFaultHandler(signal_value_t val, void* arg);
    // @brief Print known allocations near ptr.
    static void PrintMemoryMapNear(void* ptr);

    /// @brief Set the number of links connecting the agents in the platform.
    void SetLinkCount(size_t num_link);

    /// @brief Register link information connecting @p node_id_from and @p
    /// node_id_to.
    /// @param [in] node_id_from Node id of the source node.
    /// @param [in] node_id_to Node id of the destination node.
    /// @param [in] link_info The link information between source and destination
    /// nodes.
    void RegisterLinkInfo(uint32_t node_id_from, uint32_t node_id_to,
        uint32_t num_hop,
        hsa_amd_memory_pool_link_info_t& link_info);

    /// @brief Query link information between two nodes.
    /// @param [in] node_id_from Node id of the source node.
    /// @param [in] node_id_to Node id of the destination node.
    /// @retval The link information between source and destination nodes.
    const LinkInfo GetLinkInfo(uint32_t node_id_from, uint32_t node_id_to);



    /// @brief Invoke the user provided call back for each agent in the agent
    /// list.
    ///
    /// @param [in] callback User provided callback function.
    /// @param [in] data User provided pointer as input for @p callback.
    ///
    /// @retval ::SUCCESS if the callback function for each traversed
    /// agent returns ::SUCCESS.
    // status_t IterateDevice(status_t (*callback)(device_t agent,
    //                                                   void* data),
    //                          void* data);


    status_t AllocateMemory(size_t size, void** address) override;

    status_t FreeMemory(void* ptr) override;

    status_t CopyMemory(void* dst, const void* src, size_t size) override;

    virtual const timer::fast_clock::duration GetTimeout(double timeout) override;
    const timer::fast_clock::time_point GetTimeNow() override;
    virtual void Sleep(uint32_t milisecond) override;


    /// @brief Allocate memory on a particular region.
    ///
    /// @param [in] region Pointer to region object.
    /// @param [in] size Allocation size in bytes.
    /// @param [in] alloc_flags Modifiers to pass to MemoryRegion allocator.
    /// @param [out] address Pointer to store the allocation result.
    ///
    /// @retval ::SUCCESS If allocation is successful.
    status_t AllocateMemory(const core::IMemoryRegion* region, size_t size,
        core::IMemoryRegion::AllocateFlags alloc_flags,
        void** address);

    status_t RegisterReleaseNotifier(void* ptr, hsa_amd_deallocation_callback_t callback,
        void* user_data);

    status_t DeregisterReleaseNotifier(void* ptr, hsa_amd_deallocation_callback_t callback);


    /// @brief Non-blocking memory copy from src to dst.
    ///
    /// @details The memory copy will be performed after all signals in
    /// @p dep_signals have value of 0. On completion @p completion_signal
    /// will be decremented.
    ///
    /// @param [in] dst Memory address of the destination.
    /// @param [in] dst_agent Device object associated with the destination. This
    /// agent should be able to access the destination and source.
    /// @param [in] src Memory address of the source.
    /// @param [in] src_agent Device object associated with the source. This
    /// agent should be able to access the destination and source.
    /// @param [in] size Copy size in bytes.
    /// @param [in] dep_signals Array of signal dependency.
    /// @param [in] completion_signal Completion signal object.
    ///
    /// @retval ::SUCCESS if copy command has been submitted
    /// successfully to the agent DMA queue.
    status_t CopyMemory(void* dst, core::IAgent& dst_agent, const void* src,
        core::IAgent& src_agent, size_t size,
        std::vector<signal_t>& dep_signals,
        signal_t completion_signal);
    // core::ISignal& completion_signal);

    /// @brief Fill the first @p count of uint32_t in ptr with value.
    ///
    /// @param [in] ptr Memory address to be filled.
    /// @param [in] value The value/pattern that will be used to set @p ptr.
    /// @param [in] count Number of uint32_t element to be set.
    ///
    /// @retval ::SUCCESS if memory fill is successful and completed.
    status_t FillMemory(void* ptr, uint32_t value, size_t count);

    /// @brief Set agents as the whitelist to access ptr.
    ///
    /// @param [in] num_agents The number of agent handles in @p agents array.
    /// @param [in] agents Device handle array.
    /// @param [in] ptr Pointer of memory previously allocated via
    /// core::IRuntime::AllocateMemory.
    ///
    /// @retval ::SUCCESS The whitelist has been configured
    /// successfully and all agents in the @p agents could start accessing @p ptr.
    status_t AllowAccess(uint32_t num_agents, const core::IAgent** agents,
        const void* ptr);

    /// @brief Query system information.
    ///
    /// @param [in] attribute System info attribute to query.
    /// @param [out] value Pointer to store the attribute value.
    ///
    /// @retval SUCCESS The attribute is valid and the @p value is
    /// set.
    status_t GetSystemInfo(hsa_system_info_t attribute, void* value);

    /// @brief Register a callback function @p handler that is associated with
    /// @p signal to asynchronous event monitor thread.
    ///
    /// @param [in] signal Signal handle associated with @p handler.
    /// @param [in] cond The condition to execute the @p handler.
    /// @param [in] value The value to compare with @p signal value. If the
    /// comparison satisfy @p cond, the @p handler will be called.
    /// @param [in] arg Pointer to the argument that will be provided to @p
    /// handler.
    ///
    /// @retval ::SUCCESS Registration is successful.
    status_t SetAsyncSignalHandler(signal_t signal,
        signal_condition_t cond,
        signal_value_t value,
        hsa_amd_signal_handler handler, void* arg);

    status_t InteropMap(uint32_t num_agents, core::IAgent** agents,
        int interop_handle, uint32_t flags, size_t* size,
        void** ptr, size_t* metadata_size,
        const void** metadata);

    // status_t InteropUnmap(void* ptr);

    struct PtrInfoBlockData {
        void* base;
        size_t length;
    };

    status_t PtrInfo(void* ptr, hsa_amd_pointer_info_t* info, void* (*alloc)(size_t),
        uint32_t* num_agents_accessible, core::IAgent** accessible,
        PtrInfoBlockData* block_info = nullptr);

    status_t SetPtrInfoData(void* ptr, void* userptr);

    status_t IPCCreate(void* ptr, size_t len, hsa_amd_ipc_memory_t* handle);

    status_t IPCAttach(const hsa_amd_ipc_memory_t* handle, size_t len, uint32_t num_agents,
        core::IAgent** mapping_agents, void** mapped_ptr);

    status_t IPCDetach(void* ptr);

    const std::vector<const core::IMemoryRegion*>& system_regions_fine() const
    {
        return system_regions_fine_;
    }

    const std::vector<const core::IMemoryRegion*>& system_regions_coarse() const
    {
        return system_regions_coarse_;
    }

    loader::Loader* loader() { return loader_; }

    // hcs::LoaderContext* loader_context() { return &loader_context_; }

    // hcs::code::CodeObjectManager* code_manager() { return &code_manager_; }

    std::function<void*(size_t, size_t, core::IMemoryRegion::AllocateFlags)>&
    system_allocator()
    {
        return system_allocator_;
    }

    std::function<void(void*)>& system_deallocator()
    {
        return system_deallocator_;
    }

    // schi ExtensionEntryPoints extensions_;

    // status_t SetCustomSystemEventHandler(hsa_amd_system_event_callback_t callback,
    //     void* data);
    // status_t SetInternalQueueCreateNotifier(hsa_amd_runtime_queue_notifier callback,
    //     void* user_data);

    // void InternalQueueCreateNotify(const queue_t* queue, agent_t agent);

protected:
    struct AllocationRegion {
        AllocationRegion()
            : region(NULL)
            , size(0)
            , user_ptr(nullptr)
        {
        }
        AllocationRegion(const core::IMemoryRegion* region_arg, size_t size_arg)
            : region(region_arg)
            , size(size_arg)
            , user_ptr(nullptr)
        {
        }

        struct notifier_t {
            void* ptr;
            callback_t<hsa_amd_deallocation_callback_t> callback;
            void* user_data;
        };

        const core::IMemoryRegion* region;
        size_t size;
        void* user_ptr;
        std::unique_ptr<std::vector<notifier_t>> notifiers;
    };


    // Will be created before any user could call hsa_init but also could be
    // destroyed before incorrectly written programs call hsa_shutdown.
    static KernelMutex bootstrap_lock_;

    Runtime();

    Runtime(const Runtime&);

    Runtime& operator=(const Runtime&);

    ~Runtime() { }

    void RegisterAgent(core::IAgent* agent);

    void DestroyAgents();

    status_t IterateAgent(status_t (*callback)(core::IAgent* agent, void* data),
        void* data);
    /// @brief Open connection to kernel driver.
    status_t Load();

    /// @brief Close connection to kernel driver and cleanup resources.
    void Unload();
    /// @brief Dynamically load extension libraries (images, finalizer) and
    /// call OnLoad method on each loaded library.
    // void LoadExtensions();

    /// @brief Call OnUnload method on each extension library then close it.
    // void UnloadExtensions();

    /// @brief Dynamically load tool libraries and call OnUnload method on each
    /// loaded library.
    // void LoadTools();

    /// @brief Call OnUnload method of each tool library.
    // void UnloadTools();

    /// @brief Close tool libraries.
    void CloseTools();

    // @brief Binds virtual memory access fault handler to this node.
    void BindVmFaultHandler();

    // @brief Acquire snapshot of system event handlers.
    // Returns a copy to avoid holding a lock during callbacks.
    // std::vector<std::pair<callback_t<hsa_amd_system_event_callback_t>, void*>>
    // GetSystemEventHandlers();

    /// @brief Get the index of ::link_matrix_.
    /// @param [in] node_id_from Node id of the source node.
    /// @param [in] node_id_to Node id of the destination node.
    /// @retval Index in ::link_matrix_.
    uint32_t GetIndexLinkInfo(uint32_t node_id_from, uint32_t node_id_to);

    // Mutex object to protect multithreaded access to ::allocation_map_,
    // KFD map/unmap, register/unregister, and access to hsaKmtQueryPointerInfo
    // registered & mapped arrays.
    KernelMutex memory_lock_;

    // Array containing tools library handles.
    std::vector<os::LibHandle> tool_libs_;

    // Agent list containing all CPU agents in the platform.
    std::vector<core::IAgent*> cpu_agents_;

    // Agent list containing all compatible GPU agents in the platform.
    std::vector<core::IAgent*> gpu_agents_;

    loader::Loader* loader_;

public:
    core::IAgent* blit_agent() { return blit_agent_; }
    core::IAgent* blit_agent_;
    std::vector<uint32_t> gpu_ids() { return gpu_ids_; }

    std::vector<core::IAgent*> gpu_agents() { return gpu_agents_; }

    // Agent map containing all agents indexed by their KFD node IDs.
    std::map<uint32_t, std::vector<core::IAgent*>> agents_by_node_;

    // Agent list containing all compatible gpu agent ids in the platform.
    std::vector<uint32_t> gpu_ids_;

    // List of all fine grain system memory region in the platform.
    std::vector<const core::IMemoryRegion*> system_regions_fine_;

    // List of all coarse grain system memory region in the platform.
    std::vector<const core::IMemoryRegion*> system_regions_coarse_;

    // Matrix of IO link.
    std::vector<LinkInfo> link_matrix_;

    // Contains the region, address, and size of previously allocated memory.
    std::map<const void*, AllocationRegion> allocation_map_;

    // Allocator using ::system_region_
    std::function<void*(size_t, size_t, core::IMemoryRegion::AllocateFlags)>
        system_allocator_;

    // Deallocator using ::system_region_
    std::function<void(void*)> system_deallocator_;

    // Starting address of SVM address space.
    // On APU the cpu and gpu could access the area inside starting and end of
    // the SVM address space.
    // On dGPU, only the gpu is guaranteed to have access to the area inside the
    // SVM address space, since it maybe backed by private gpu VRAM.
    uintptr_t start_svm_address_;

    // End address of SVM address space.
    // start_svm_address_ + size
    uintptr_t end_svm_address_;

    // System clock frequency.
    uint64_t sys_clock_freq_;
    // Number of Numa Nodes
    size_t num_nodes_;

    // @brief AMD HSA event to monitor for virtual memory access fault.
    HsaEvent* vm_fault_event_;

    // @brief HSA signal to contain the VM fault event.
    //Signal* vm_fault_signal_;
    signal_t vm_fault_signal_;

    // Custom system event handler.
    //std::vector<std::pair<callback_t<hsa_amd_system_event_callback_t>, void*>>
    //    system_event_handlers_;

    // System event handler lock
    KernelMutex system_event_lock_;

    ICoStream* co_stream_;
    Device* device_;
    Platform* platform_;

    // Frees runtime memory when the runtime library is unloaded if safe to do so.
    // Failure to release the runtime indicates an incorrect application but is
    // common (example: calls library routines at process exit).
    friend class RuntimeCleanup;
    friend class core::IAgent;
    friend class Agent;
};

__forceinline Runtime& Runtime::getInstance()
{
    static Runtime obj;
    return obj;
}

