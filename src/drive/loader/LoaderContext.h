#pragma once

#include "Loader.h"

class LoaderContext final : public loader::Context {
public:
    LoaderContext()
        : loader::Context()
    {
    }

    ~LoaderContext() { }

    // hsa_isa_t IsaFromName(const char* name) override;

    // bool IsaSupportedByAgent(core::IAgent* agent, hsa_isa_t code_object_isa) override;

    void* SegmentAlloc(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, size_t size, size_t align, bool zero) override;

    bool SegmentCopy(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, void* dst, size_t offset, const void* src, size_t size) override;

    void SegmentFree(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, void* seg, size_t size = 0) override;

    void* SegmentAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, void* seg, size_t offset) override;

    void* SegmentHostAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, void* seg, size_t offset) override;

    bool SegmentFreeze(amdgpu_hsa_elf_segment_t segment, core::IAgent* agent, void* seg, size_t size) override;

    bool ImageExtensionSupported() override;

private:
    LoaderContext(const LoaderContext&);
    LoaderContext& operator=(const LoaderContext&);
};

