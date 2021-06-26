#pragma once

#include <vector>
#include "inc/drive_api.h"
#include "core/IAgent.h"
// schi #include "core/inc/checked.h"

namespace core {
// class IAgent;

class IMemoryRegion {
public:
    IMemoryRegion(bool fine_grain, bool full_profile , IAgent* owner)
        : fine_grain_(fine_grain)
        , full_profile_(full_profile), owner_(owner)
    {
        /*assert(owner_ != NULL);*/
    }

    virtual ~IMemoryRegion() { }
#if 0
    // Convert this object into region_t.
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

    // Convert region_t into MemoryRegion *.
    static MemoryRegion* Object(region_t region)
    {
        return reinterpret_cast<MemoryRegion*>(region.handle);
    }
#endif
    enum AllocateEnum {
        AllocateNoFlags = 0,
        AllocateRestrict = (1 << 0), // Don't map system memory to GPU agents
        AllocateExecutable = (1 << 1), // Set executable permission
        AllocateDoubleMap = (1 << 2), // Map twice VA allocation to backing store
        AllocateDirect = (1 << 3), // Bypass fragment cache.
        AllocateIPC = (1 << 4), // System memory that can be IPC-shared
    };

    typedef uint32_t AllocateFlags;

    virtual status_t Allocate(size_t& size, AllocateFlags alloc_flags, void** address) const = 0;

    virtual status_t Free(void* address, size_t size) const = 0;
    // Prepares suballocated memory for IPC export.
    virtual status_t IPCFragmentExport(void* address) const = 0;

    // Translate memory properties into HSA region attribute.
    virtual status_t GetInfo(region_info_t attribute,
        void* value) const = 0;

    // virtual status_t AssignAgent(void* ptr, size_t size, const Agent& agent,
    //                                 hsa_access_permission_t access) const = 0;

    bool fine_grain() const { return fine_grain_; }

    bool full_profile() const { return full_profile_; }

    core::IAgent* owner() const { return owner_; }
    core::IRuntime* GetRuntime() const { return owner_->GetRuntime();}

private:
    const bool fine_grain_;
    const bool full_profile_;

    core::IAgent* owner_;
};
} // namespace core

