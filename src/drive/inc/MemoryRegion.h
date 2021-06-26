#pragma once

// #include "inc/hsakmt.h"

// #include "inc/Device.h"
#include "inc/Runtime.h"
#include "core/IMemoryRegion.h"
// #include "inc/platform.h"
#include "util/locks.h"
#include "simple_heap.h"
#include "inc/hsakmttypes.h"
#include "inc/drive_api.h"
#include "inc/Platform.h"
#include "core/IAgent.h"

// #include "inc/pps_ext.h"
/*
namespace core {
class Device;
}*/

// namespace hcs {
class MemoryRegion : public core::IMemoryRegion {
public:
#if 0
    /// @brief Convert this object into region_t.
    static region_t Handle(MemoryRegion* region)
    {
        const region_t region_handle = {
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(region))
        };
        return region_handle;
    }

    static const region_t Handle(const MemoryRegion* region)
    {
        const region_t region_handle = {
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(region))
        };
        return region_handle;
    }

    /// @brief  Convert region_t into hcs::MemoryRegion *.
    static MemoryRegion* Object(region_t region)
    {
        return reinterpret_cast<MemoryRegion*>(region.handle);
    }
#endif
    /// @brief Allocate agent accessible memory (system / local memory).
    static void* AllocateKfdMemory(const HsaMemFlags& flag, uint32_t node_id,
        size_t size);

    /// @brief Free agent accessible memory (system / local memory).
    static void FreeKfdMemory(void* ptr, size_t size);

    static bool RegisterMemory(void* ptr, size_t size, const HsaMemFlags& MemFlags);
    // static bool RegisterMemory(void* ptr, size_t size, size_t num_nodes,
    //                           const uint32_t* nodes);

    static void DeregisterMemory(void* ptr);

    /// @brief Pin memory.
    static bool MakeKfdMemoryResident(size_t num_node, const uint32_t* nodes, const void* ptr,
        size_t size, uint64_t* alternate_va, HsaMemMapFlags map_flag);

    /// @brief Unpin memory.
    static void MakeKfdMemoryUnresident(const void* ptr);

    MemoryRegion(bool fine_grain, bool full_profile, core::IAgent* owner,
        const HsaMemoryProperties& mem_props);

    ~MemoryRegion();

    status_t Allocate(size_t& size, AllocateFlags alloc_flags, void** address) const;

    status_t Free(void* address, size_t size) const;

    status_t IPCFragmentExport(void* address) const;

    status_t GetInfo(region_info_t attribute, void* value) const;

    status_t GetPoolInfo(hsa_amd_memory_pool_info_t attribute,
        void* value) const;

    status_t GetAgentPoolInfo(const core::IAgent& agent,
                                  hsa_amd_agent_memory_pool_info_t attribute,
                                  void* value) const;

    status_t AllowAccess(uint32_t num_agents, core::IAgent** agents,
        const void* ptr, size_t size) const;

    status_t CanMigrate(const MemoryRegion& dst, bool& result) const;

    status_t Migrate(uint32_t flag, const void* ptr) const;

    status_t Lock(uint32_t num_agents, core::IAgent** agents,
        void* host_ptr, size_t size, void** agent_ptr) const;

    status_t Unlock(void* host_ptr) const;

    uint64_t GetBaseAddress() const { return mem_props_.VirtualBaseAddress; }

    uint64_t GetPhysicalSize() const { return mem_props_.SizeInBytes; }

    uint64_t GetVirtualSize() const { return virtual_size_; }

    status_t AssignAgent(void* ptr, size_t size, const core::IAgent& agent,
                             hsa_access_permission_t access) const;

    bool IsLocalMemory() const
    {
        return ((mem_props_.HeapType == HSA_HEAPTYPE_FRAME_BUFFER_PRIVATE) || (mem_props_.HeapType == HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC));
    }

    bool IsPublic() const
    {
        return (mem_props_.HeapType == HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC);
    }

    bool IsSystem() const
    {
        return mem_props_.HeapType == HSA_HEAPTYPE_SYSTEM;
    }

    bool IsLDS() const
    {
        return mem_props_.HeapType == HSA_HEAPTYPE_GPU_LDS;
    }

    bool IsGDS() const
    {
        return mem_props_.HeapType == HSA_HEAPTYPE_GPU_GDS;
    }

    bool IsScratch() const
    {
        return mem_props_.HeapType == HSA_HEAPTYPE_GPU_SCRATCH;
    }

    uint32_t BusWidth() const
    {
        return static_cast<uint32_t>(mem_props_.Width);
    }

    uint32_t MaxMemCloc() const
    {
        return static_cast<uint32_t>(mem_props_.MemoryClockMax);
    }

private:
    const HsaMemoryProperties mem_props_;

    HsaMemFlags mem_flag_;

    HsaMemMapFlags map_flag_;

    size_t max_single_alloc_size_;

    // Used to collect total system memory
    static size_t max_sysmem_alloc_size_;

    uint64_t virtual_size_;

    mutable KernelMutex access_lock_;

    static const size_t kPageSize_ = 4096;

    // Determine access type allowed to requesting device
    hsa_amd_memory_pool_access_t GetAccessInfo(const core::IAgent& device,
        const LinkInfo& link_info) const;

    class BlockAllocator {
    private:
        MemoryRegion& region_;
        static const size_t block_size_ = 2 * 1024 * 1024; // 2MB blocks.
    public:
        explicit BlockAllocator(MemoryRegion& region)
            : region_(region)
        {
        }
        void* alloc(size_t request_size, size_t& allocated_size) const;
        void free(void* ptr, size_t length) const { region_.Free(ptr, length); }
        size_t block_size() const { return block_size_; }
    };

    mutable SimpleHeap<BlockAllocator> fragment_allocator_;
};

// }  // namespace

