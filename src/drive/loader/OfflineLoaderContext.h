#pragma once

#include "Loader.h"
#include <set>
#include <iostream>

namespace loader {

  class OfflineLoaderContext : public Context {
  private:
    std::ostream& out;
    typedef std::set<void*> PointerSet;
    PointerSet pointers;

  public:
    OfflineLoaderContext();
/*
    hsa_isa_t IsaFromName(const char *name) override;

    bool IsaSupportedByAgent(core::IAgent* agent, hsa_isa_t isa) override;
    */

    void* SegmentAlloc(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, size_t size, size_t align, bool zero) override;

    bool SegmentCopy(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* dst, size_t offset, const void* src, size_t size) override;

    void SegmentFree(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size = 0) override;

    void* SegmentAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset) override;

    void* SegmentHostAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset) override;

    bool SegmentFreeze(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size) override;

    bool ImageExtensionSupported() override;

  };
}

