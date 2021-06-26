#include "LoaderContext.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "inc/Agent.h"
#include "inc/MemoryRegion.h"
#include "util/os.h"

#include <cstdlib>
#include <utility>
// #include "inc/pps.h"
#include "util/utils.h"
// #include "inc/pps_ext.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {

bool IsLocalRegion(const core::IMemoryRegion* region)
{
    const MemoryRegion* hcs_region = (MemoryRegion*)region;
    if (nullptr == hcs_region || !hcs_region->IsLocalMemory()) {
        return false;
    }
    return true;
}

bool IsDebuggerRegistered()
{
    return false;
    // Leaving code commented as it will be used later on
    //return ((core::IRuntime::runtime_singleton_->flag().emulate_aql()) &&
    //        (0 !=
    //         core::IRuntime::runtime_singleton_->flag().tools_lib_names().size()));
}

class SegmentMemory {
public:
    virtual ~SegmentMemory() { }
    virtual void* Address(size_t offset = 0) const = 0;
    virtual void* HostAddress(size_t offset = 0) const = 0;
    virtual bool Allocated() const = 0;
    virtual bool Allocate(size_t size, size_t align, bool zero) = 0;
    virtual bool Copy(size_t offset, const void* src, size_t size) = 0;
    virtual void Free() = 0;
    virtual bool Freeze() = 0;

protected:
    SegmentMemory() { }

private:
    SegmentMemory(const SegmentMemory&);
    SegmentMemory& operator=(const SegmentMemory&);
};

class MallocedMemory final : public SegmentMemory {
public:
    MallocedMemory()
        : SegmentMemory()
        , ptr_(nullptr)
        , size_(0)
    {
    }
    ~MallocedMemory() { }

    void* Address(size_t offset = 0) const override
    {
        assert(this->Allocated());
        return (char*)ptr_ + offset;
    }
    void* HostAddress(size_t offset = 0) const override
    {
        return this->Address(offset);
    }
    bool Allocated() const override
    {
        return nullptr != ptr_;
    }

    bool Allocate(size_t size, size_t align, bool zero) override;
    bool Copy(size_t offset, const void* src, size_t size) override;
    void Free() override;
    bool Freeze() override;

private:
    MallocedMemory(const MallocedMemory&);
    MallocedMemory& operator=(const MallocedMemory&);

    void* ptr_;
    size_t size_;
};

bool MallocedMemory::Allocate(size_t size, size_t align, bool zero)
{
    assert(!this->Allocated());
    assert(0 < size);
    assert(0 < align && 0 == (align & (align - 1)));
    ptr_ = _aligned_malloc(size, align);
    if (nullptr == ptr_) {
        return false;
    }
    if (SUCCESS != hsa_memory_register(ptr_, size)) {
        _aligned_free(ptr_);
        ptr_ = nullptr;
        return false;
    }
    if (zero) {
        memset(ptr_, 0x0, size);
    }
    size_ = size;
    return true;
}

bool MallocedMemory::Copy(size_t offset, const void* src, size_t size)
{
    assert(this->Allocated());
    assert(nullptr != src);
    assert(0 < size);
    memcpy(this->Address(offset), src, size);
    return true;
}

void MallocedMemory::Free()
{
    assert(this->Allocated());
    hsa_memory_deregister(ptr_, size_);
    _aligned_free(ptr_);
    ptr_ = nullptr;
    size_ = 0;
}

bool MallocedMemory::Freeze()
{
    assert(this->Allocated());
    return true;
}

class MappedMemory final : public SegmentMemory {
public:
    MappedMemory(bool is_kv = false)
        : SegmentMemory()
        , is_kv_(is_kv)
        , ptr_(nullptr)
        , size_(0)
    {
    }
    ~MappedMemory() { }

    void* Address(size_t offset = 0) const override
    {
        assert(this->Allocated());
        return (char*)ptr_ + offset;
    }
    void* HostAddress(size_t offset = 0) const override
    {
        return this->Address(offset);
    }
    bool Allocated() const override
    {
        return nullptr != ptr_;
    }

    bool Allocate(size_t size, size_t align, bool zero) override;
    bool Copy(size_t offset, const void* src, size_t size) override;
    void Free() override;
    bool Freeze() override;

private:
    MappedMemory(const MappedMemory&);
    MappedMemory& operator=(const MappedMemory&);

    bool is_kv_;
    void* ptr_;
    size_t size_;
};

bool MappedMemory::Allocate(size_t size, size_t align, bool zero)
{
    assert(!this->Allocated());
    assert(0 < size);
    assert(0 < align && 0 == (align & (align - 1)));
#if defined(_WIN32) || defined(_WIN64)
    ptr_ = (void*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    ptr_ = is_kv_ ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) : mmap(nullptr, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE, -1, 0);
#endif // _WIN32 || _WIN64
    if (nullptr == ptr_) {
        return false;
    }
    assert(0 == ((uintptr_t)ptr_) % align);
    if (SUCCESS != hsa_memory_register(ptr_, size)) {
#if defined(_WIN32) || defined(_WIN64)
        VirtualFree(ptr_, size, MEM_DECOMMIT);
        VirtualFree(ptr_, 0, MEM_RELEASE);
#else
        munmap(ptr_, size);
#endif // _WIN32 || _WIN64
        ptr_ = nullptr;
        return false;
    }
    if (zero) {
        memset(ptr_, 0x0, size);
    }
    size_ = size;
    return true;
}

bool MappedMemory::Copy(size_t offset, const void* src, size_t size)
{
    assert(this->Allocated());
    assert(nullptr != src);
    assert(0 < size);
    memcpy(this->Address(offset), src, size);
    return true;
}

void MappedMemory::Free()
{
    assert(this->Allocated());
    hsa_memory_deregister(ptr_, size_);
#if defined(_WIN32) || defined(_WIN64)
    VirtualFree(ptr_, size_, MEM_DECOMMIT);
    VirtualFree(ptr_, 0, MEM_RELEASE);
#else
    munmap(ptr_, size_);
#endif // _WIN32 || _WIN64
    ptr_ = nullptr;
    size_ = 0;
}

bool MappedMemory::Freeze()
{
    assert(this->Allocated());
    return true;
}

class RegionMemory final : public SegmentMemory {
public:
    static core::IMemoryRegion* AgentLocal(core::IAgent* agent);
    static core::IMemoryRegion* System();

    RegionMemory(core::IMemoryRegion* region)
        : SegmentMemory()
        , region_(region)
        , ptr_(nullptr)
        , host_ptr_(nullptr)
        , size_(0)
    {
    }
    ~RegionMemory() { }

    void* Address(size_t offset = 0) const override
    {
        assert(this->Allocated());
        return (char*)ptr_ + offset;
    }
    void* HostAddress(size_t offset = 0) const override
    {
        assert(this->Allocated());
        return (char*)host_ptr_ + offset;
    }
    bool Allocated() const override
    {
        return nullptr != ptr_;
    }

    bool Allocate(size_t size, size_t align, bool zero) override;
    bool Copy(size_t offset, const void* src, size_t size) override;
    void Free() override;
    bool Freeze() override;

private:
    RegionMemory(const RegionMemory&);
    RegionMemory& operator=(const RegionMemory&);

    core::IMemoryRegion* region_;
    void* ptr_;
    void* host_ptr_;
    size_t size_;
};

core::IMemoryRegion* RegionMemory::AgentLocal(core::IAgent* agent)
{
    core::IMemoryRegion* invalid_region;
    // invalid_region.handle = 0;
    // GpuAgent *_agent = (GpuAgent*)core::IAgent::Object(agent);
    GpuAgent* _agent = dynamic_cast<GpuAgent*>(agent);
    if (nullptr == _agent) {
        return invalid_region;
    }
    auto agent_local_region = std::find_if(_agent->regions().begin(), _agent->regions().end(), IsLocalRegion);
    return const_cast<core::IMemoryRegion*>(agent_local_region == _agent->regions().end() ? invalid_region : /*core::IMemoryRegion::Handle(*/*agent_local_region/*)*/);
}

core::IMemoryRegion* RegionMemory::System()
{
    core::IMemoryRegion* default_system_region = const_cast<core::IMemoryRegion*>(Runtime::runtime_singleton_->system_regions_fine()[0]);

    assert(default_system_region != NULL);

    return default_system_region;
}

bool RegionMemory::Allocate(size_t size, size_t align, bool zero)
{
    assert(!this->Allocated());
    assert(0 < size);
    assert(0 < align && 0 == (align & (align - 1)));
    if (SUCCESS != hsa_memory_allocate(region_, size, &ptr_)) {
        ptr_ = nullptr;
        return false;
    }
    assert(0 == ((uintptr_t)ptr_) % align);
    if (SUCCESS != hsa_memory_allocate(RegionMemory::System(), size, &host_ptr_)) {
        hsa_memory_free(ptr_);
        ptr_ = nullptr;
        host_ptr_ = nullptr;
        return false;
    }
    if (zero) {
        memset(host_ptr_, 0x0, size);
    }
    size_ = size;
    return true;
}

bool RegionMemory::Copy(size_t offset, const void* src, size_t size)
{
    assert(this->Allocated() && nullptr != host_ptr_);
    assert(nullptr != src);
    assert(0 < size);
    memcpy((char*)host_ptr_ + offset, src, size);
    return true;
}

void RegionMemory::Free()
{
    assert(this->Allocated());
    hsa_memory_free(ptr_);
    if (nullptr != host_ptr_) {
        hsa_memory_free(host_ptr_);
    }
    ptr_ = nullptr;
    host_ptr_ = nullptr;
    size_ = 0;
}

bool RegionMemory::Freeze()
{
    assert(this->Allocated() && nullptr != host_ptr_);

    core::IAgent* agent = region_->owner();
    if (agent != NULL && agent->agent_type() == core::IAgent::kGpu) {
        // if (SUCCESS != agent->DmaCopy(ptr_, host_ptr_, size_, DMA_D2H)) {
        if (SUCCESS != agent->DmaCopy(ptr_, host_ptr_, size_)) {
            return false;
        }
    } else {
        memcpy(ptr_, host_ptr_, size_);
    }

    return true;
}
/*
status_t IsIsaEquivalent(hsa_isa_t isa, void *data) {
  assert(data);

  std::pair<hsa_isa_t, bool> *data_pair = (std::pair<hsa_isa_t, bool>*)data;
  assert(data_pair);
  assert(data_pair->first.handle != 0);
  assert(data_pair->second != true);

  const core::IIsa *isa1 = core::Isa::Object(isa);
  assert(isa1);
  const core::IIsa *isa2 = core::Isa::Object(data_pair->first);
  assert(isa2);
  if (isa1->version() == isa2->version()) {
    data_pair->second = true;
    return HSA_STATUS_INFO_BREAK;
  }

  return SUCCESS;
}
*/

} // namespace anonymous

/*
hsa_isa_t LoaderContext::IsaFromName(const char *name) {
  assert(name);

  status_t hsa_status = SUCCESS;
  hsa_isa_t isa_handle;
  isa_handle.handle = 0;

  hsa_status = hsa_isa_from_name(name, &isa_handle);
  if (SUCCESS != hsa_status) {
    isa_handle.handle = 0;
    return isa_handle;
  }

  return isa_handle;
}

bool LoaderContext::IsaSupportedByAgent(agent_t agent,
                                        hsa_isa_t code_object_isa) {
  assert(agent.handle != 0);

  std::pair<hsa_isa_t, bool> data(code_object_isa, false);
  status_t status = hsa_agent_iterate_isas(agent, IsIsaEquivalent, &data);
  if (status != SUCCESS && status != HSA_STATUS_INFO_BREAK) {
    return false;
  }
  return data.second;
}
*/

void* LoaderContext::SegmentAlloc(amdgpu_hsa_elf_segment_t segment,
    core::IAgent* agent,
    size_t size,
    size_t align,
    bool zero)
{
    assert(0 < size);
    assert(0 < align && 0 == (align & (align - 1)));

    SegmentMemory* mem = nullptr;
    switch (segment) {
    case AMDGPU_HSA_SEGMENT_GLOBAL_AGENT:
    case AMDGPU_HSA_SEGMENT_READONLY_AGENT: {
        profile_t agent_profile;
        if (SUCCESS != hsa_agent_get_info(agent, HSA_AGENT_INFO_PROFILE, &agent_profile)) {
            return nullptr;
        }

        switch (agent_profile) {
        case HSA_PROFILE_BASE:
            mem = new (std::nothrow) RegionMemory(RegionMemory::AgentLocal(agent));
            break;
        case HSA_PROFILE_FULL:
            mem = new (std::nothrow) RegionMemory(RegionMemory::System());
            break;
        default:
            assert(false);
        }
        break;
    }
    case AMDGPU_HSA_SEGMENT_GLOBAL_PROGRAM: {
        mem = new (std::nothrow) RegionMemory(RegionMemory::System());
        break;
    }
    case AMDGPU_HSA_SEGMENT_CODE_AGENT: {
        profile_t agent_profile;
        if (SUCCESS != hsa_agent_get_info(agent, HSA_AGENT_INFO_PROFILE, &agent_profile)) {
            return nullptr;
        }

        switch (agent_profile) {
        case HSA_PROFILE_BASE:
            mem = new (std::nothrow) RegionMemory(IsDebuggerRegistered() ? RegionMemory::System() : RegionMemory::AgentLocal(agent));
            break;
        // TODO schi uncomment
        case HSA_PROFILE_FULL:
            // mem = new (std::nothrow) MappedMemory(((GpuDeviceInt*)core::IAgent::Object(agent))->is_kv_device());
            // schi mmap for exec
            mem = new (std::nothrow) MappedMemory();
            break;
        default:
            assert(false);
        }

        // Invalidate agent caches which may hold lines of the new allocation.
        // ((GpuAgentInt*)core::IAgent::Object(agent))->InvalidateCodeCaches();
        (dynamic_cast<GpuAgentInt*>(agent))->InvalidateCodeCaches();

        break;
    }
    default:
        assert(false);
    }

    if (nullptr == mem) {
        return nullptr;
    }

    if (!mem->Allocate(size, align, zero)) {
        delete mem;
        return nullptr;
    }

    return mem;
}

bool LoaderContext::SegmentCopy(amdgpu_hsa_elf_segment_t segment, // not used.
    core::IAgent* agent, // not used.
    void* dst,
    size_t offset,
    const void* src,
    size_t size)
{
    assert(nullptr != dst);
    return ((SegmentMemory*)dst)->Copy(offset, src, size);
}

void LoaderContext::SegmentFree(amdgpu_hsa_elf_segment_t segment, // not used.
    core::IAgent* agent, // not used.
    void* seg,
    size_t size) // not used.
{
    assert(nullptr != seg);
    SegmentMemory* mem = (SegmentMemory*)seg;
    mem->Free();
    delete mem;
    mem = nullptr;
}

void* LoaderContext::SegmentAddress(amdgpu_hsa_elf_segment_t segment, // not used.
    core::IAgent* agent, // not used.
    void* seg,
    size_t offset)
{
    assert(nullptr != seg);
    return ((SegmentMemory*)seg)->Address(offset);
}

void* LoaderContext::SegmentHostAddress(amdgpu_hsa_elf_segment_t segment, // not used.
    core::IAgent* agent, // not used.
    void* seg,
    size_t offset)
{
    assert(nullptr != seg);
    return ((SegmentMemory*)seg)->HostAddress(offset);
}

bool LoaderContext::SegmentFreeze(amdgpu_hsa_elf_segment_t segment, // not used.
    core::IAgent* agent, // not used.
    void* seg,
    size_t size) // not used.
{
    assert(nullptr != seg);
    return ((SegmentMemory*)seg)->Freeze();
}

bool LoaderContext::ImageExtensionSupported()
{
    status_t hsa_status = SUCCESS;
    bool result = false;
    /*
  hsa_status =
      hsa_system_extension_supported(HSA_EXTENSION_IMAGES, 1, 0, &result);
      */
    hsa_status = ERROR;
    if (SUCCESS != hsa_status) {
        return false;
    }

    return result;
}

