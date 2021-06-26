#pragma once

#include <array>
#include <cassert>
#include <cstdint>
// #include <libelf.h>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
// #include "inc/pps.h"
// #include "inc/pps_ext_image.h"
#include "CodeObject.h"
#include "Loader.h"
// #include "inc/pps_kernel_code.h"
#include "util/locks.h"
#include "elfio/elfio.hpp"
// #include "iguana/msgpack.hpp"

namespace code {
    class CodeObject;
    class SymbolV3;
    class RelocationV3;
}
namespace loader {

class MemoryAddress;
class SymbolImpl;
class KernelSymbol;
class VariableSymbol;
class ExecutableImpl;

//===----------------------------------------------------------------------===//
// SymbolImpl.                                                                //
//===----------------------------------------------------------------------===//

typedef uint32_t symbol_attribute32_t;

class SymbolImpl : public Symbol {
public:
    virtual ~SymbolImpl() { }

    bool IsKernel() const
    {
        return HSA_SYMBOL_KIND_KERNEL == kind;
    }
    bool IsVariable() const
    {
        return HSA_SYMBOL_KIND_VARIABLE == kind;
    }

    bool is_loaded;
    hsa_symbol_kind_t kind;
    std::string module_name;
    std::string symbol_name;
    hsa_symbol_linkage_t linkage;
    bool is_definition;
    uint64_t address;
    core::IAgent* agent;

    core::IAgent* GetAgent() override
    {
        return agent;
    }

protected:
    SymbolImpl(const bool& _is_loaded,
        const hsa_symbol_kind_t& _kind,
        const std::string& _module_name,
        const std::string& _symbol_name,
        const hsa_symbol_linkage_t& _linkage,
        const bool& _is_definition,
        const uint64_t& _address = 0)
        : is_loaded(_is_loaded)
        , kind(_kind)
        , module_name(_module_name)
        , symbol_name(_symbol_name)
        , linkage(_linkage)
        , is_definition(_is_definition)
        , address(_address)
    {
    }

    virtual bool GetInfo(hsa_symbol_info32_t symbol_info, void* value);

private:
    SymbolImpl(const SymbolImpl& s);
    SymbolImpl& operator=(const SymbolImpl& s);
};

//===----------------------------------------------------------------------===//
// KernelSymbol.                                                              //
//===----------------------------------------------------------------------===//

class KernelSymbol final : public SymbolImpl {
public:
    KernelSymbol(const bool& _is_loaded,
        const std::string& _module_name,
        const std::string& _symbol_name,
        const hsa_symbol_linkage_t& _linkage,
        const bool& _is_definition,
        const uint32_t& _kernarg_segment_size,
        const uint32_t& _kernarg_segment_alignment,
        const uint32_t& _group_segment_size,
        const uint32_t& _private_segment_size,
        const uint32_t& _kernel_ctrl,
        const uint32_t& _kernel_mode,
        const bool& _is_dynamic_callstack,
        const uint32_t& _size,
        const uint32_t& _alignment,
        const uint64_t& _address = 0)
        : SymbolImpl(_is_loaded,
            HSA_SYMBOL_KIND_KERNEL,
            _module_name,
            _symbol_name,
            _linkage,
            _is_definition,
            _address)
        , full_name(_module_name.empty() ? _symbol_name : _module_name + "::" + _symbol_name)
        , kernarg_segment_size(_kernarg_segment_size)
        , kernarg_segment_alignment(_kernarg_segment_alignment)
        , group_segment_size(_group_segment_size)
        , private_segment_size(_private_segment_size)
        , kernel_ctrl(_kernel_ctrl)
        , kernel_mode(_kernel_mode)
        , is_dynamic_callstack(_is_dynamic_callstack)
        , size(_size)
        , alignment(_alignment)
    {
    }

    ~KernelSymbol() { }

    bool GetInfo(hsa_symbol_info32_t symbol_info, void* value);

    std::string full_name;
    uint32_t kernarg_segment_size;
    uint32_t kernarg_segment_alignment;
    uint32_t group_segment_size;
    uint32_t private_segment_size;
    uint32_t kernel_ctrl;
    uint32_t kernel_mode;
    bool is_dynamic_callstack;
    uint32_t size;
    uint32_t alignment;
    loader_debug_info_t debug_info;

private:
    KernelSymbol(const KernelSymbol& ks);
    KernelSymbol& operator=(const KernelSymbol& ks);
};

//===----------------------------------------------------------------------===//
// VariableSymbol.                                                            //
//===----------------------------------------------------------------------===//

class VariableSymbol final : public SymbolImpl {
public:
    VariableSymbol(const bool& _is_loaded,
        const std::string& _module_name,
        const std::string& _symbol_name,
        const hsa_symbol_linkage_t& _linkage,
        const bool& _is_definition,
        const hsa_variable_allocation_t& _allocation,
        const hsa_variable_segment_t& _segment,
        const uint32_t& _size,
        const uint32_t& _alignment,
        const bool& _is_constant,
        const bool& _is_external = false,
        const uint64_t& _address = 0)
        : SymbolImpl(_is_loaded,
            HSA_SYMBOL_KIND_VARIABLE,
            _module_name,
            _symbol_name,
            _linkage,
            _is_definition,
            _address)
        , allocation(_allocation)
        , segment(_segment)
        , size(_size)
        , alignment(_alignment)
        , is_constant(_is_constant)
        , is_external(_is_external)
    {
    }

    ~VariableSymbol() { }

    bool GetInfo(hsa_symbol_info32_t symbol_info, void* value);

    hsa_variable_allocation_t allocation;
    hsa_variable_segment_t segment;
    uint32_t size;
    uint32_t alignment;
    bool is_constant;
    bool is_external;

private:
    VariableSymbol(const VariableSymbol& vs);
    VariableSymbol& operator=(const VariableSymbol& vs);
};

//===----------------------------------------------------------------------===//
// Executable.                                                                //
//===----------------------------------------------------------------------===//

class ExecutableImpl;
class LoadedCodeObjectImpl;
class Segment;

class ExecutableObject {
protected:
    ExecutableImpl* owner;
    core::IAgent* agent;

public:
    ExecutableObject(ExecutableImpl* owner_, core::IAgent* agent_)
        : owner(owner_)
        , agent(agent_)
    {
    }

    ExecutableImpl* Owner() const { return owner; }
    core::IAgent* Agent() const { return agent; }
    virtual void Print(std::ostream& out) = 0;
    virtual void Destroy() = 0;

    virtual ~ExecutableObject() { }
};

class LoadedCodeObjectImpl : public LoadedCodeObject, public ExecutableObject {
private:
    LoadedCodeObjectImpl(const LoadedCodeObjectImpl&);
    LoadedCodeObjectImpl& operator=(const LoadedCodeObjectImpl&);

    const void* elf_data;
    const size_t elf_size;
    std::vector<Segment*> loaded_segments;

public:
    LoadedCodeObjectImpl(ExecutableImpl* owner_, core::IAgent* agent_, const void* elf_data_, size_t elf_size_)
        : ExecutableObject(owner_, agent_)
        , elf_data(elf_data_)
        , elf_size(elf_size_)
    {
    }

    const void* ElfData() const { return elf_data; }
    size_t ElfSize() const { return elf_size; }
    std::vector<Segment*>& LoadedSegments() { return loaded_segments; }

    bool GetInfo(amd_loaded_code_object_info_t attribute, void* value) override;

    status_t IterateLoadedSegments(
        status_t (*callback)(
            amd_loaded_segment_t loaded_segment,
            void* data),
        void* data) override;

    void Print(std::ostream& out) override;

    void Destroy() override { }

    core::IAgent* getAgent() const override;
    hsa_executable_t getExecutable() const override;
    uint64_t getElfData() const override;
    uint64_t getElfSize() const override;
    uint64_t getStorageOffset() const override;
    uint64_t getLoadBase() const override;
    uint64_t getLoadSize() const override;
    int64_t getDelta() const override;
};

class Segment : public LoadedSegment, public ExecutableObject {
private:
    amdgpu_hsa_elf_segment_t segment;
    void* ptr;
    size_t size;
    uint64_t vaddr;
    bool frozen;
    size_t storage_offset;

public:
    Segment(ExecutableImpl* owner_, core::IAgent* agent_, amdgpu_hsa_elf_segment_t segment_, void* ptr_, size_t size_, uint64_t vaddr_, size_t storage_offset_)
        : ExecutableObject(owner_, agent_)
        , segment(segment_)
        , ptr(ptr_)
        , size(size_)
        , vaddr(vaddr_)
        , frozen(false)
        , storage_offset(storage_offset_)
    {
    }

    amdgpu_hsa_elf_segment_t ElfSegment() const { return segment; }
    void* Ptr() const { return ptr; }
    size_t Size() const { return size; }
    uint64_t VAddr() const { return vaddr; }
    size_t StorageOffset() const { return storage_offset; }

    bool GetInfo(amd_loaded_segment_info_t attribute, void* value) override;

    uint64_t Offset(uint64_t addr); // Offset within segment. Used together with ptr with loader context functions.

    void* Address(uint64_t addr); // Address in segment. Used for relocations and valid on agent.

    bool Freeze();

    bool IsAddressInSegment(uint64_t addr);
    void Copy(uint64_t addr, const void* src, size_t size);
    void Print(std::ostream& out) override;
    void Destroy() override;
};

/*
class Sampler : public ExecutableObject {
private:
  hsa_ext_sampler_t samp;

public:
  Sampler(ExecutableImpl *owner, core::IAgent* agent, hsa_ext_sampler_t samp_)
    : ExecutableObject(owner, agent), samp(samp_) { }
  void Print(std::ostream& out) override;
  void Destroy() override;
};

class Image : public ExecutableObject {
private:
  hsa_ext_image_t img;

public:
  Image(ExecutableImpl *owner, core::IAgent* agent, hsa_ext_image_t img_)
    : ExecutableObject(owner, agent), img(img_) { }
  void Print(std::ostream& out) override;
  void Destroy() override;
};
*/

typedef std::string ProgramSymbol;
typedef std::unordered_map<ProgramSymbol, SymbolImpl*> ProgramSymbolMap;

typedef std::pair<std::string, const core::IAgent*> AgentSymbol;
struct ASC {
    bool operator()(const AgentSymbol& las, const AgentSymbol& ras) const
    {
        return las.first == ras.first && las.second == ras.second;
    }
};
struct ASH {
    size_t operator()(const AgentSymbol& as) const
    {
        size_t h = std::hash<std::string>()(as.first);
        size_t i = std::hash<const core::IAgent*>()(as.second);
        return h ^ (i << 1);
    }
};
typedef std::unordered_map<AgentSymbol, SymbolImpl*, ASH, ASC> AgentSymbolMap;


class ExecutableImpl final : public Executable {
public:
    const profile_t& profile() const
    {
        return profile_;
    }
    const hsa_executable_state_t& state() const
    {
        return state_;
    }

    ExecutableImpl(
        const profile_t& _profile,
        Context* context,
        size_t id,
        hsa_default_float_rounding_mode_t default_float_rounding_mode);

    ~ExecutableImpl();

    status_t GetInfo(hsa_executable_info_t executable_info, void* value) override;

    status_t DefineProgramExternalVariable(
        const char* name, void* address) override;

    status_t DefineAgentExternalVariable(
        const char* name,
        core::IAgent* agent,
        hsa_variable_segment_t segment,
        void* address) override;

    status_t LoadCodeObject(
    core::IAgent* agent,
    hsa_code_object_t code_object,
    const char *options,
    hsa_loaded_code_object_t *loaded_code_object) override;

    status_t LoadCodeObject(
    core::IAgent* agent,
    hsa_code_object_t code_object,
    size_t code_object_size,
    const char *options,
    hsa_loaded_code_object_t *loaded_code_object) override;

    status_t Freeze(const char* options) override;

    status_t Validate(uint32_t* result) override
    {
        // amd::hsa::common::ReaderLockGuard<amd::hsa::common::ReaderWriterLock> reader_lock(rw_lock_);
        ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
        assert(result);
        *result = 0;
        return SUCCESS;
    }

    /// @note needed for hsa v1.0.
    /// @todo remove during loader refactoring.
    bool IsProgramSymbol(const char* symbol_name) override;

    Symbol* GetSymbol(
        const char* symbol_name,
        const core::IAgent* agent) override;

    status_t IterateSymbols(
        iterate_symbols_f callback, void* data) override;

    /// @since hsa v1.1.
    status_t IterateAgentSymbols(
        core::IAgent* agent,
        status_t (*callback)(hsa_executable_t exec,
            core::IAgent* agent,
            hsa_executable_symbol_t symbol,
            void* data),
        void* data) override;

    /// @since hsa v1.1.
    status_t IterateProgramSymbols(
        status_t (*callback)(hsa_executable_t exec,
            hsa_executable_symbol_t symbol,
            void* data),
        void* data) override;

    status_t IterateLoadedCodeObjects(
        status_t (*callback)(
            hsa_executable_t executable,
            hsa_loaded_code_object_t loaded_code_object,
            void* data),
        void* data) override;

    size_t GetNumSegmentDescriptors() override;

    size_t QuerySegmentDescriptors(
        hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
        size_t total_num_segment_descriptors,
        size_t first_empty_segment_descriptor) override;

    uint64_t FindHostAddress(uint64_t device_address) override;

    void EnableReadOnlyMode();
    void DisableReadOnlyMode();

    void Print(std::ostream& out) override;
    bool PrintToFile(const std::string& filename) override;

    Context* context() { return context_; }
    size_t id() { return id_; }

private:
    ExecutableImpl(const ExecutableImpl& e);
    ExecutableImpl& operator=(const ExecutableImpl& e);

    // TODO schi std::unique_ptr<amd::hsa::code::CodeObject> code;
    std::unique_ptr<code::CodeObject> code;

    Symbol* GetSymbolInternal(
        const char* symbol_name,
        const core::IAgent* agent);

    status_t LoadSegments(core::IAgent* agent, const code::CodeObject* c);
    // status_t LoadSegmentV2(const code::Segment* data_segment, loader::Segment* load_segment);
    // TODO V3
    status_t LoadSegmentV3(const ELFIO::segment* data_segment, loader::Segment* load_segment);

    // TODO schi change amd::hsa::code to code
    // status_t LoadSymbol(core::IAgent* agent, code::Symbol* sym, uint32_t majorVersion);
    status_t LoadSymbol(core::IAgent* agent, code::SymbolV3* sym, uint32_t majorVersion);
    // status_t LoadDefinitionSymbol(core::IAgent* agent, code::Symbol* sym, uint32_t majorVersion);
    status_t LoadDefinitionSymbol(core::IAgent* agent, code::SymbolV3* sym, uint32_t majorVersion);
    // status_t LoadDeclarationSymbol(core::IAgent* agent, code::Symbol* sym, uint32_t majorVersion);
    status_t LoadDeclarationSymbol(core::IAgent* agent, code::SymbolV3* sym, uint32_t majorVersion);

    // status_t ApplyRelocations(core::IAgent* agent, code::CodeObject* c);
    // status_t ApplyStaticRelocationSection(core::IAgent* agent, code::RelocationSection* sec);
    // status_t ApplyStaticRelocation(core::IAgent* agent, code::Relocation* rel);
    // status_t ApplyDynamicRelocationSection(core::IAgent* agent, code::RelocationSection* sec);
    // status_t ApplyDynamicRelocation(core::IAgent* agent, code::Relocation* rel);

    status_t ApplyRelocationsV3(core::IAgent* agent, code::CodeObject* c);
    status_t ApplyStaticRelocationSectionV3(core::IAgent* agent, code::CodeObject* c, ELFIO::section* sec);
    status_t ApplyStaticRelocationV3(core::IAgent* agent, ELFIO::symbol_section_accessor& symbols, code::RelocationV3& rel);
    status_t ApplyDynamicRelocationSectionV3(core::IAgent* agent, code::CodeObject* c, ELFIO::section* sec);
    status_t ApplyDynamicRelocationV3(core::IAgent* agent, code::CodeObject* c, ELFIO::symbol_section_accessor& symbols, code::RelocationV3& rel);

    Segment* VirtualAddressSegment(uint64_t vaddr);
    // uint64_t SymbolAddress(core::IAgent* agent, code::Symbol* sym);
    uint64_t SymbolAddress(core::IAgent* agent, code::SymbolV3* sym);
    // uint64_t SymbolAddress(core::IAgent* agent, elf::Symbol* sym);
    // Segment* SymbolSegment(core::IAgent* agent, code::Symbol* sym);
    Segment* SymbolSegment(core::IAgent* agent, code::SymbolV3* sym);
    // Segment* SectionSegment(core::IAgent* agent, code::Section* sec);
    Segment* SectionSegment(core::IAgent* agent, ELFIO::section* sec);

    // amd::hsa::common::ReaderWriterLock rw_lock_;
    ReaderWriterLock rw_lock_;
    profile_t profile_;
    Context* context_;
    const size_t id_;
    hsa_default_float_rounding_mode_t default_float_rounding_mode_;
    hsa_executable_state_t state_;

    ProgramSymbolMap program_symbols_;
    AgentSymbolMap agent_symbols_;
    std::vector<ExecutableObject*> objects;
    Segment* program_allocation_segment;
    std::vector<LoadedCodeObjectImpl*> loaded_code_objects;
};

class HcsCodeLoader : public Loader {
private:
    Context* context;
    std::vector<Executable*> executables;
    // amd::hsa::common::ReaderWriterLock rw_lock_;
    ReaderWriterLock rw_lock_;

public:
    HcsCodeLoader(Context* context_)
        : context(context_)
    {
        assert(context);
    }

    Context* GetContext() const override { return context; }

    Executable* CreateExecutable(
        profile_t profile,
        const char* options,
        hsa_default_float_rounding_mode_t default_float_rounding_mode = HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT) override;

    void DestroyExecutable(Executable* executable) override;

    status_t IterateExecutables(
        status_t (*callback)(
            hsa_executable_t executable,
            void* data),
        void* data) override;

    status_t QuerySegmentDescriptors(
        hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
        size_t* num_segment_descriptors) override;

    hsa_executable_t FindExecutable(uint64_t device_address) override;

    uint64_t FindHostAddress(uint64_t device_address) override;

    void PrintHelp(std::ostream& out) override;

    void EnableReadOnlyMode();
    void DisableReadOnlyMode();
};

} // namespace loader

