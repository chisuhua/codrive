#include "OfflineLoaderContext.h"
#include <cassert>
#include <cstring>

namespace loader {

// Helper function that allocates an aligned memory.
static inline void*
alignedMalloc(size_t size, size_t alignment)
{
#if defined(_WIN32)
    return ::_aligned_malloc(size, alignment);
#else
    void* ptr = NULL;
    alignment = (std::max)(alignment, sizeof(void*));
    if (0 == ::posix_memalign(&ptr, alignment, size)) {
        return ptr;
    }
    return NULL;
#endif
}

// Helper function that frees an aligned memory.
static inline void
alignedFree(void* ptr)
{
#if defined(_WIN32)
    ::_aligned_free(ptr);
#else
    free(ptr);
#endif
}

OfflineLoaderContext::OfflineLoaderContext()
    : out(std::cout)
{
}

/*
hsa_isa_t OfflineLoaderContext::IsaFromName(const char* name)
{
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

bool OfflineLoaderContext::IsaSupportedByAgent(core::IAgent* agent, hsa_isa_t isa)
{
    return true;
}
*/

void* OfflineLoaderContext::SegmentAlloc(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, size_t size, size_t align, bool zero)
{
    void* ptr = alignedMalloc(size, align);
    if (zero) { memset(ptr, 0, size); }
    out << "SegmentAlloc: " << segment << ": "
        << "size=" << size << " align=" << align << " zero=" << zero << " result=" << ptr << std::endl;
    pointers.insert(ptr);
    return ptr;
}

bool OfflineLoaderContext::SegmentCopy(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* dst, size_t offset, const void* src, size_t size)
{
    out << "SegmentCopy: " << segment << ": "
        << "dst=" << dst << " offset=" << offset << " src=" << src << " size=" << size << std::endl;
    if (!dst || !src || dst == src) {
        return false;
    }
    if (0 == size) {
        return true;
    }
    memcpy((char*)dst + offset, src, size);
    return true;
}

void OfflineLoaderContext::SegmentFree(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size)
{
    out << "SegmentFree: " << segment << ": "
        << " ptr=" << seg << " size=" << size << std::endl;
    pointers.erase(seg);
    alignedFree(seg);
}

void* OfflineLoaderContext::SegmentAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset)
{
    out << "SegmentAddress: " << segment << ": "
        << " ptr=" << seg << " offset=" << offset << std::endl;
    return (char*)seg + offset;
}

void* OfflineLoaderContext::SegmentHostAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset)
{
    out << "SegmentHostAddress: " << segment << ": "
        << " ptr=" << seg << " offset=" << offset << std::endl;
    return (char*)seg + offset;
}

bool OfflineLoaderContext::SegmentFreeze(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size)
{
    out << "SegmentFreeze: " << segment << ": "
        << " ptr=" << seg << " size=" << size << std::endl;
    return true;
}

bool OfflineLoaderContext::ImageExtensionSupported()
{
    return false;
}

}
