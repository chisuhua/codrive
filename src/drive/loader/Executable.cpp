#include "Executable.h"
#include "CodeObject.h"
#include "codeobjectUtil.h"
#include "common/options.h"
#include "ElfDefine.h"
// #include "inc/pps_kernel_code.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libelf.h>

// #include "elfio/elfio.hpp"

// using namespace hcs;
// using namespace hcs::common;

namespace loader {

class LoaderOptions {
public:
    explicit LoaderOptions(std::ostream& error = std::cerr);

    const util::options::NoArgOption* Help() const { return &help; }
    const util::options::NoArgOption* DumpCode() const { return &dump_code; }
    const util::options::NoArgOption* DumpIsa() const { return &dump_isa; }
    const util::options::NoArgOption* DumpExec() const { return &dump_exec; }
    const util::options::NoArgOption* DumpAll() const { return &dump_all; }
    const util::options::ValueOption<std::string>* DumpDir() const { return &dump_dir; }
    const util::options::PrefixOption* Substitute() const { return &substitute; }

    bool ParseOptions(const std::string& options);
    void Reset();
    void PrintHelp(std::ostream& out) const;

private:
    /// @brief Copy constructor - not available.
    LoaderOptions(const LoaderOptions&);

    /// @brief Assignment operator - not available.
    LoaderOptions& operator=(const LoaderOptions&);

    util::options::NoArgOption help;
    util::options::NoArgOption dump_code;
    util::options::NoArgOption dump_isa;
    util::options::NoArgOption dump_exec;
    util::options::NoArgOption dump_all;
    util::options::ValueOption<std::string> dump_dir;
    util::options::PrefixOption substitute;
    util::options::OptionParser option_parser;
};

LoaderOptions::LoaderOptions(std::ostream& error)
    : help("help", "print help")
    , dump_code("dump-code", "Dump finalizer output code object")
    , dump_isa("dump-isa", "Dump finalizer output to ISA text file")
    , dump_exec("dump-exec", "Dump executable to text file")
    , dump_all("dump-all", "Dump all finalizer input and output (as above)")
    , dump_dir("dump-dir", "Dump directory")
    , substitute("substitute", "Substitute code object with given index or index range on loading from file")
    , option_parser(false, error)
{
    option_parser.AddOption(&help);
    option_parser.AddOption(&dump_code);
    option_parser.AddOption(&dump_isa);
    option_parser.AddOption(&dump_exec);
    option_parser.AddOption(&dump_all);
    option_parser.AddOption(&dump_dir);
    option_parser.AddOption(&substitute);
}

bool LoaderOptions::ParseOptions(const std::string& options)
{
    return option_parser.ParseOptions(options.c_str());
}

void LoaderOptions::Reset()
{
    option_parser.Reset();
}

void LoaderOptions::PrintHelp(std::ostream& out) const
{
    option_parser.PrintHelp(out);
}

// static const char *LOADER_DUMP_PREFIX = "hcscode";

Loader* Loader::Create(Context* context)
{
    return new HcsCodeLoader(context);
}

void Loader::Destroy(Loader* loader)
{
    delete loader;
}

Executable* HcsCodeLoader::CreateExecutable(
    profile_t profile, const char* options, hsa_default_float_rounding_mode_t default_float_rounding_mode)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);

    executables.push_back(new ExecutableImpl(profile, context, executables.size(), default_float_rounding_mode));
    return executables.back();
}

void HcsCodeLoader::DestroyExecutable(Executable* executable)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);

    executables[((ExecutableImpl*)executable)->id()] = nullptr;
    delete executable;
}

status_t HcsCodeLoader::IterateExecutables(
    status_t (*callback)(
        hsa_executable_t executable,
        void* data),
    void* data)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);
    assert(callback);

    for (auto& exec : executables) {
        status_t status = callback(Executable::Handle(exec), data);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

status_t HcsCodeLoader::QuerySegmentDescriptors(
    hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
    size_t* num_segment_descriptors)
{
    if (!num_segment_descriptors) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (*num_segment_descriptors == 0 && segment_descriptors) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (*num_segment_descriptors != 0 && !segment_descriptors) {
        return ERROR_INVALID_ARGUMENT;
    }

    this->EnableReadOnlyMode();

    size_t actual_num_segment_descriptors = 0;
    for (auto& executable : executables) {
        if (executable) {
            actual_num_segment_descriptors += executable->GetNumSegmentDescriptors();
        }
    }

    if (*num_segment_descriptors == 0) {
        *num_segment_descriptors = actual_num_segment_descriptors;
        this->DisableReadOnlyMode();
        return SUCCESS;
    }
    if (*num_segment_descriptors != actual_num_segment_descriptors) {
        this->DisableReadOnlyMode();
        return ERROR_INVALID_ARGUMENT;
    }

    size_t i = 0;
    for (auto& executable : executables) {
        if (executable) {
            i += executable->QuerySegmentDescriptors(segment_descriptors, actual_num_segment_descriptors, i);
        }
    }

    this->DisableReadOnlyMode();
    return SUCCESS;
}

uint64_t HcsCodeLoader::FindHostAddress(uint64_t device_address)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    if (device_address == 0) {
        return 0;
    }

    for (auto& exec : executables) {
        if (exec != nullptr) {
            uint64_t host_address = exec->FindHostAddress(device_address);
            if (host_address != 0) {
                return host_address;
            }
        }
    }
    return 0;
}

void HcsCodeLoader::PrintHelp(std::ostream& out)
{
    LoaderOptions().PrintHelp(out);
}

void HcsCodeLoader::EnableReadOnlyMode()
{
    rw_lock_.ReaderLock();
    for (auto& executable : executables) {
        if (executable) {
            ((ExecutableImpl*)executable)->EnableReadOnlyMode();
        }
    }
}

void HcsCodeLoader::DisableReadOnlyMode()
{
    rw_lock_.ReaderUnlock();
    for (auto& executable : executables) {
        if (executable) {
            ((ExecutableImpl*)executable)->DisableReadOnlyMode();
        }
    }
}

//===----------------------------------------------------------------------===//
// SymbolImpl.                                                                    //
//===----------------------------------------------------------------------===//

bool SymbolImpl::GetInfo(hsa_symbol_info32_t symbol_info, void* value)
{
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_TYPE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_TYPE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_TYPE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_TYPE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_NAME_LENGTH) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_NAME_LENGTH)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_NAME) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_NAME)),
        "attributes are not compatible");
    /*
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_MODULE_NAME_LENGTH) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_MODULE_NAME_LENGTH)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_MODULE_NAME) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_MODULE_NAME)),
        "attributes are not compatible");
        */
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_LINKAGE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_LINKAGE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_IS_DEFINITION) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_IS_DEFINITION)),
        "attributes are not compatible");

    assert(value);

    switch (symbol_info) {
    case HSA_CODE_SYMBOL_INFO_TYPE: {
        *((hsa_symbol_kind_t*)value) = kind;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_NAME_LENGTH: {
        *((uint32_t*)value) = symbol_name.size();
        break;
    }
    case HSA_CODE_SYMBOL_INFO_NAME: {
        memset(value, 0x0, symbol_name.size());
        memcpy(value, symbol_name.c_str(), symbol_name.size());
        break;
    }
    case HSA_CODE_SYMBOL_INFO_MODULE_NAME_LENGTH: {
        *((uint32_t*)value) = module_name.size();
        break;
    }
    case HSA_CODE_SYMBOL_INFO_MODULE_NAME: {
        memset(value, 0x0, module_name.size());
        memcpy(value, module_name.c_str(), module_name.size());
        break;
    }
    case HSA_CODE_SYMBOL_INFO_LINKAGE: {
        *((hsa_symbol_linkage_t*)value) = linkage;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_IS_DEFINITION: {
        *((bool*)value) = is_definition;
        break;
    }
    /*
    case HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_CALL_CONVENTION: {
        *((uint32_t*)value) = 0;
        break;
    }
    */
    case HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT:
    case HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_ADDRESS: {
        if (!is_loaded) {
            return false;
        }
        *((uint64_t*)value) = address;
        break;
    }
    case HSA_EXECUTABLE_SYMBOL_INFO_AGENT: {
        if (!is_loaded) {
            return false;
        }
        *((core::IAgent**)value) = agent;
        break;
    }
    default: {
        return false;
    }
    }

    return true;
}

//===----------------------------------------------------------------------===//
// KernelSymbol.                                                              //
//===----------------------------------------------------------------------===//

bool KernelSymbol::GetInfo(hsa_symbol_info32_t symbol_info, void* value)
{
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_ALIGNMENT) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_ALIGNMENT)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_CTRL) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_CTRL)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_KERNEL_MODE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_MODE)),
        "attributes are not compatible");

    assert(value);

    switch (symbol_info) {
    case HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE: {
        *((uint32_t*)value) = kernarg_segment_size;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_ALIGNMENT: {
        *((uint32_t*)value) = kernarg_segment_alignment;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE: {
        *((uint32_t*)value) = group_segment_size;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE: {
        *((uint32_t*)value) = private_segment_size;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_CTRL: {
        *((uint32_t*)value) = kernel_ctrl;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_MODE: {
        *((uint32_t*)value) = kernel_mode;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK: {
        *((bool*)value) = is_dynamic_callstack;
        break;
    }
    case HSA_EXT_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT_SIZE: {
        *((uint32_t*)value) = size;
        break;
    }
    case HSA_EXT_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT_ALIGN: {
        *((uint32_t*)value) = alignment;
        break;
    }
    default: {
        return SymbolImpl::GetInfo(symbol_info, value);
    }
    }

    return true;
}

//===----------------------------------------------------------------------===//
// VariableSymbol.                                                            //
//===----------------------------------------------------------------------===//

bool VariableSymbol::GetInfo(hsa_symbol_info32_t symbol_info, void* value)
{
    /*
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_VARIABLE_ALLOCATION) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_ALLOCATION)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_VARIABLE_SEGMENT) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_SEGMENT)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_VARIABLE_ALIGNMENT) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_ALIGNMENT)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_VARIABLE_SIZE) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_SIZE)),
        "attributes are not compatible");
    static_assert(
        (symbol_attribute32_t(HSA_CODE_SYMBOL_INFO_VARIABLE_IS_CONST) == symbol_attribute32_t(HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_IS_CONST)),
        "attributes are not compatible");*/

    switch (symbol_info) {
    case HSA_CODE_SYMBOL_INFO_VARIABLE_ALLOCATION: {
        *((hsa_variable_allocation_t*)value) = allocation;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_VARIABLE_SEGMENT: {
        *((hsa_variable_segment_t*)value) = segment;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_VARIABLE_ALIGNMENT: {
        *((uint32_t*)value) = alignment;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_VARIABLE_SIZE: {
        *((uint32_t*)value) = size;
        break;
    }
    case HSA_CODE_SYMBOL_INFO_VARIABLE_IS_CONST: {
        *((bool*)value) = is_constant;
        break;
    }
    default: {
        return SymbolImpl::GetInfo(symbol_info, value);
    }
    }

    return true;
}

bool LoadedCodeObjectImpl::GetInfo(amd_loaded_code_object_info_t attribute, void* value)
{
    assert(value);

    switch (attribute) {
    case AMD_LOADED_CODE_OBJECT_INFO_ELF_IMAGE:
        ((hsa_code_object_t*)value)->handle = reinterpret_cast<uint64_t>(elf_data);
        break;
    case AMD_LOADED_CODE_OBJECT_INFO_ELF_IMAGE_SIZE:
        *((size_t*)value) = elf_size;
        break;
    default: {
        return false;
    }
    }

    return true;
}

status_t LoadedCodeObjectImpl::IterateLoadedSegments(
    status_t (*callback)(
        amd_loaded_segment_t loaded_segment,
        void* data),
    void* data)
{
    assert(callback);

    for (auto& loaded_segment : loaded_segments) {
        status_t status = callback(LoadedSegment::Handle(loaded_segment), data);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

void LoadedCodeObjectImpl::Print(std::ostream& out)
{
    out << "Code Object" << std::endl;
}

bool Segment::GetInfo(amd_loaded_segment_info_t attribute, void* value)
{
    assert(value);

    switch (attribute) {
    case AMD_LOADED_SEGMENT_INFO_TYPE: {
        *((amdgpu_hsa_elf_segment_t*)value) = segment;
        break;
    }
    case AMD_LOADED_SEGMENT_INFO_ELF_BASE_ADDRESS: {
        *((uint64_t*)value) = vaddr;
        break;
    }
    case AMD_LOADED_SEGMENT_INFO_LOAD_BASE_ADDRESS: {
        *((uint64_t*)value) = reinterpret_cast<uint64_t>(this->Address(this->VAddr()));
        break;
    }
    case AMD_LOADED_SEGMENT_INFO_SIZE: {
        *((size_t*)value) = size;
        break;
    }
    default: {
        return false;
    }
    }

    return true;
}

uint64_t Segment::Offset(uint64_t addr)
{
    assert(IsAddressInSegment(addr));
    return addr - vaddr;
}

void* Segment::Address(uint64_t addr)
{
    return owner->context()->SegmentAddress(segment, agent, ptr, Offset(addr));
}

bool Segment::Freeze()
{
    return !frozen ? (frozen = owner->context()->SegmentFreeze(segment, agent, ptr, size)) : true;
}

bool Segment::IsAddressInSegment(uint64_t addr)
{
    return vaddr <= addr && addr < vaddr + size;
}

void Segment::Copy(uint64_t addr, const void* src, size_t size)
{
    // loader must do copies before freezing.
    assert(!frozen);

    if (size > 0) {
        owner->context()->SegmentCopy(segment, agent, ptr, Offset(addr), src, size);
    }
}

void Segment::Print(std::ostream& out)
{
    out << "Segment" << std::endl
        << "    Type: " << code::ElfSegmentToString(segment)
        << "    Size: " << size
        << "    VAddr: " << vaddr << std::endl
        << "    Ptr: " << std::hex << ptr << std::dec
        << std::endl;
}

void Segment::Destroy()
{
    owner->context()->SegmentFree(segment, agent, ptr, size);
}

//===----------------------------------------------------------------------===//
// ExecutableImpl.                                                                //
//===----------------------------------------------------------------------===//

ExecutableImpl::ExecutableImpl(
    const profile_t& _profile,
    Context* context,
    size_t id,
    hsa_default_float_rounding_mode_t default_float_rounding_mode)
    : Executable()
    , profile_(_profile)
    , context_(context)
    , id_(id)
    , default_float_rounding_mode_(default_float_rounding_mode)
    , state_(HSA_EXECUTABLE_STATE_UNFROZEN)
    , program_allocation_segment(nullptr)
{
}

ExecutableImpl::~ExecutableImpl()
{
    for (ExecutableObject* o : objects) {
        o->Destroy();
        delete o;
    }
    objects.clear();

    for (auto& symbol_entry : program_symbols_) {
        delete symbol_entry.second;
    }
    for (auto& symbol_entry : agent_symbols_) {
        delete symbol_entry.second;
    }
}

status_t ExecutableImpl::DefineProgramExternalVariable(
    const char* name, void* address)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);
    assert(name);

    if (HSA_EXECUTABLE_STATE_FROZEN == state_) {
        return ERROR_FROZEN_EXECUTABLE;
    }

    auto symbol_entry = program_symbols_.find(std::string(name));
    if (symbol_entry != program_symbols_.end()) {
        return ERROR_VARIABLE_ALREADY_DEFINED;
    }

    program_symbols_.insert(
        std::make_pair(std::string(name),
            new VariableSymbol(true,
                "", // Only program linkage symbols can be
                // defined.
                std::string(name),
                HSA_SYMBOL_LINKAGE_PROGRAM,
                true,
                HSA_VARIABLE_ALLOCATION_PROGRAM,
                HSA_VARIABLE_SEGMENT_GLOBAL,
                0, // TODO: size.
                0, // TODO: align.
                false, // TODO: const.
                true,
                reinterpret_cast<uint64_t>(address))));
    return SUCCESS;
}

status_t ExecutableImpl::DefineAgentExternalVariable(
    const char* name,
    core::IAgent* agent,
    hsa_variable_segment_t segment,
    void* address)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);
    assert(name);

    if (HSA_EXECUTABLE_STATE_FROZEN == state_) {
        return ERROR_FROZEN_EXECUTABLE;
    }

    auto symbol_entry = agent_symbols_.find(std::make_pair(std::string(name), agent));
    if (symbol_entry != agent_symbols_.end()) {
        return ERROR_VARIABLE_ALREADY_DEFINED;
    }

    auto insert_status = agent_symbols_.insert(
        std::make_pair(std::make_pair(std::string(name), agent),
            new VariableSymbol(true,
                "", // Only program linkage symbols can be
                // defined.
                std::string(name),
                HSA_SYMBOL_LINKAGE_PROGRAM,
                true,
                HSA_VARIABLE_ALLOCATION_AGENT,
                segment,
                0, // TODO: size.
                0, // TODO: align.
                false, // TODO: const.
                true,
                reinterpret_cast<uint64_t>(address))));
    assert(insert_status.second);
    insert_status.first->second->agent = agent;

    return SUCCESS;
}

bool ExecutableImpl::IsProgramSymbol(const char* symbol_name)
{
    assert(symbol_name);

    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    return program_symbols_.find(std::string(symbol_name)) != program_symbols_.end();
}

Symbol* ExecutableImpl::GetSymbol(
    const char* symbol_name,
    const core::IAgent* agent)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    return this->GetSymbolInternal(symbol_name, agent);
}

Symbol* ExecutableImpl::GetSymbolInternal(
    const char* symbol_name,
    const core::IAgent* agent)
{
    assert(symbol_name);

    std::string mangled_name = std::string(symbol_name);
    if (mangled_name.empty()) {
        return nullptr;
    }

    if (!agent) {
        auto program_symbol = program_symbols_.find(mangled_name);
        if (program_symbol != program_symbols_.end()) {
            return program_symbol->second;
        }
        return nullptr;
    }

    auto agent_symbol = agent_symbols_.find(std::make_pair(mangled_name, agent));
    if (agent_symbol != agent_symbols_.end()) {
        return agent_symbol->second;
    }
    return nullptr;
}

status_t ExecutableImpl::IterateSymbols(
    iterate_symbols_f callback, void* data)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    assert(callback);

    for (auto& symbol_entry : program_symbols_) {
        status_t hsc = callback(Executable::Handle(this), Symbol::Handle(symbol_entry.second), data);
        if (SUCCESS != hsc) {
            return hsc;
        }
    }
    for (auto& symbol_entry : agent_symbols_) {
        status_t hsc = callback(Executable::Handle(this), Symbol::Handle(symbol_entry.second), data);
        if (SUCCESS != hsc) {
            return hsc;
        }
    }

    return SUCCESS;
}

status_t ExecutableImpl::IterateAgentSymbols(
    core::IAgent* agent,
    status_t (*callback)(hsa_executable_t exec,
        core::IAgent* agent,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    assert(callback);

    for (auto& symbol_entry : agent_symbols_) {
        if (symbol_entry.second->GetAgent() != agent) {
            continue;
        }

        status_t status = callback(
            Executable::Handle(this), agent, Symbol::Handle(symbol_entry.second),
            data);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

status_t ExecutableImpl::IterateProgramSymbols(
    status_t (*callback)(hsa_executable_t exec,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    assert(callback);

    for (auto& symbol_entry : program_symbols_) {
        status_t status = callback(
            Executable::Handle(this), Symbol::Handle(symbol_entry.second), data);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

status_t ExecutableImpl::IterateLoadedCodeObjects(
    status_t (*callback)(
        hsa_executable_t executable,
        hsa_loaded_code_object_t loaded_code_object,
        void* data),
    void* data)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    assert(callback);

    for (auto& loaded_code_object : loaded_code_objects) {
        status_t status = callback(
            Executable::Handle(this),
            LoadedCodeObject::Handle(loaded_code_object),
            data);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

size_t ExecutableImpl::GetNumSegmentDescriptors()
{
    // assuming we are in readonly mode.
    size_t actual_num_segment_descriptors = 0;
    for (auto& obj : loaded_code_objects) {
        actual_num_segment_descriptors += obj->LoadedSegments().size();
    }
    return actual_num_segment_descriptors;
}

size_t ExecutableImpl::QuerySegmentDescriptors(
    hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
    size_t total_num_segment_descriptors,
    size_t first_empty_segment_descriptor)
{
    // assuming we are in readonly mode.
    assert(segment_descriptors);
    assert(first_empty_segment_descriptor < total_num_segment_descriptors);

    size_t i = first_empty_segment_descriptor;
    for (auto& obj : loaded_code_objects) {
        assert(i < total_num_segment_descriptors);
        for (auto& seg : obj->LoadedSegments()) {
            segment_descriptors[i].agent = seg->Agent();
            segment_descriptors[i].executable = Executable::Handle(seg->Owner());
            segment_descriptors[i].code_object_storage_type = HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY;
            segment_descriptors[i].code_object_storage_base = obj->ElfData();
            segment_descriptors[i].code_object_storage_size = obj->ElfSize();
            segment_descriptors[i].code_object_storage_offset = seg->StorageOffset();
            segment_descriptors[i].segment_base = seg->Address(seg->VAddr());
            segment_descriptors[i].segment_size = seg->Size();
            ++i;
        }
    }

    return i - first_empty_segment_descriptor;
}

core::IAgent* LoadedCodeObjectImpl::getAgent() const
{
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return loaded_segments.front()->Agent();
}
hsa_executable_t LoadedCodeObjectImpl::getExecutable() const
{
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return Executable::Handle(loaded_segments.front()->Owner());
}
uint64_t LoadedCodeObjectImpl::getElfData() const
{
    return reinterpret_cast<uint64_t>(elf_data);
}
uint64_t LoadedCodeObjectImpl::getElfSize() const
{
    return (uint64_t)elf_size;
}
uint64_t LoadedCodeObjectImpl::getStorageOffset() const
{
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return (uint64_t)loaded_segments.front()->StorageOffset();
}
uint64_t LoadedCodeObjectImpl::getLoadBase() const
{
    // TODO Add support for code objects with 0 segments.
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return reinterpret_cast<uint64_t>(loaded_segments.front()->Address(0));
}
uint64_t LoadedCodeObjectImpl::getLoadSize() const
{
    // TODO Add support for code objects with 0 or >1 segments.
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return (uint64_t)loaded_segments.front()->Size();
}
int64_t LoadedCodeObjectImpl::getDelta() const
{
    // TODO Add support for code objects with 0 segments.
    assert(loaded_segments.size() == 1 && "Only supports code objects v2+");
    return getLoadBase() - loaded_segments.front()->VAddr();
}

hsa_executable_t HcsCodeLoader::FindExecutable(uint64_t device_address)
{
    hsa_executable_t execHandle = { 0 };
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);
    if (device_address == 0) {
        return execHandle;
    }

    for (auto& exec : executables) {
        if (exec != nullptr) {
            uint64_t host_address = exec->FindHostAddress(device_address);
            if (host_address != 0) {
                return Executable::Handle(exec);
            }
        }
    }
    return execHandle;
}

uint64_t ExecutableImpl::FindHostAddress(uint64_t device_address)
{
    for (auto& obj : loaded_code_objects) {
        assert(obj);
        for (auto& seg : obj->LoadedSegments()) {
            assert(seg);
            uint64_t paddr = (uint64_t)(uintptr_t)seg->Address(seg->VAddr());
            if (paddr <= device_address && device_address < paddr + seg->Size()) {
                void* haddr = context_->SegmentHostAddress(
                    seg->ElfSegment(), seg->Agent(), seg->Ptr(), device_address - paddr);
                return nullptr == haddr ? 0 : (uint64_t)(uintptr_t)haddr;
            }
        }
    }
    return 0;
}

void ExecutableImpl::EnableReadOnlyMode()
{
    rw_lock_.ReaderLock();
}

void ExecutableImpl::DisableReadOnlyMode()
{
    rw_lock_.ReaderUnlock();
}

#define HSAERRCHECK(hsc)             \
    if (hsc != SUCCESS) { \
        assert(false);               \
        return hsc;                  \
    }

status_t ExecutableImpl::GetInfo(
    hsa_executable_info_t executable_info, void* value)
{
    ReaderLockGuard<ReaderWriterLock> reader_lock(rw_lock_);

    assert(value);

    switch (executable_info) {
    case HSA_EXECUTABLE_INFO_PROFILE: {
        *((profile_t*)value) = profile_;
        ;
        break;
    }
    case HSA_EXECUTABLE_INFO_STATE: {
        *((hsa_executable_state_t*)value) = state_;
        break;
    }
    case HSA_EXECUTABLE_INFO_DEFAULT_FLOAT_ROUNDING_MODE: {
        *((hsa_default_float_rounding_mode_t*)value) = default_float_rounding_mode_;
        break;
    }
    default: {
        return ERROR_INVALID_ARGUMENT;
    }
    }

    return SUCCESS;
}

/*
static uint32_t NextCodeObjectNum()
{
  static std::atomic_uint_fast32_t dumpN(1);
  return dumpN++;
}
*/

status_t ExecutableImpl::LoadCodeObject(
    core::IAgent* agent,
    hsa_code_object_t code_object,
    const char* options,
    hsa_loaded_code_object_t* loaded_code_object)
{
    return LoadCodeObject(agent, code_object, 0, options, loaded_code_object);
}

status_t ExecutableImpl::LoadCodeObject(
    core::IAgent* agent,
    hsa_code_object_t code_object,
    size_t code_object_size,
    const char* options,
    hsa_loaded_code_object_t* loaded_code_object)
{
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);
    if (HSA_EXECUTABLE_STATE_FROZEN == state_) {
        return ERROR_FROZEN_EXECUTABLE;
    }

    LoaderOptions loaderOptions;
    if (options && !loaderOptions.ParseOptions(options)) {
        return ERROR;
    }

    /*
  llvm_ir_kernel_loader kc_loader;
  kc_loader.load_kernel((char *)code_object, kernel_loader::LOAD_FROM_MEM);
  */

    code.reset(new code::CodeObject());

    if (!code->InitAsHandle(code_object)) {
        return ERROR_INVALID_CODE_OBJECT;
    }
    /*
  if (loaderOptions.DumpAll()->is_set() || loaderOptions.DumpCode()->is_set()) {
    if (!code->SaveToFile(hcs::DumpFileName(loaderOptions.DumpDir()->value(), LOADER_DUMP_PREFIX, "hsaco", codeNum))) {
      // Ignore error.
    }
  }
  if (loaderOptions.DumpAll()->is_set() || loaderOptions.DumpIsa()->is_set()) {
    if (!code->PrintToFile(hcs::DumpFileName(loaderOptions.DumpDir()->value(), LOADER_DUMP_PREFIX, "isa", codeNum))) {
      // Ignore error.
    }
  }
  */

    uint32_t majorVersion, minorVersion;

    if (code->isV3) {
        majorVersion = 3;
        minorVersion = 0;
    } else {
#if 0
            std::string codeIsa;
            if (!code->GetNoteIsa(codeIsa)) {
                return ERROR_INVALID_CODE_OBJECT;
            }

            hsa_isa_t objectsIsa = context_->IsaFromName(codeIsa.c_str());
            if (!objectsIsa.handle) {
                return ERROR_INVALID_ISA_NAME;
            }

            if (!code->GetNoteCodeObjectVersion(&majorVersion, &minorVersion)) {
                return ERROR_INVALID_CODE_OBJECT;
            }

            if (majorVersion != 1 && majorVersion != 2) {
                return ERROR_INVALID_CODE_OBJECT;
            }
            if (agent.handle == 0 && majorVersion == 1) {
                return ERROR_INVALID_AGENT;
            }
            if (agent.handle != 0 && !context_->IsaSupportedByAgent(agent, objectsIsa)) {
                return ERROR_INCOMPATIBLE_ARGUMENTS;
            }

            uint32_t codeHsailMajor;
            uint32_t codeHsailMinor;
            profile_t codeProfile;
            hsa_machine_model_t codeMachineModel;
            hsa_default_float_rounding_mode_t codeRoundingMode;
            if (!code->GetNoteHsail(&codeHsailMajor, &codeHsailMinor, &codeProfile, &codeMachineModel, &codeRoundingMode)) {
                codeProfile = HSA_PROFILE_FULL;
            }
            if (profile_ != codeProfile) {
                return ERROR_INCOMPATIBLE_ARGUMENTS;
            }
#endif
    }

    status_t status;

    objects.push_back(new LoadedCodeObjectImpl(this, agent, code->ElfData(), code->ElfSize()));
    loaded_code_objects.push_back((LoadedCodeObjectImpl*)objects.back());

    status = LoadSegments(agent, code.get());
    if (status != SUCCESS) return status;

    for (size_t i = 0; i < code->SymbolCount(); ++i) {
        if (code->isV3) {
            status = LoadSymbol(agent, code->GetSymbolV3(i), majorVersion);
        } else {
            assert("code is not V3");
            // status = LoadSymbol(agent, code->GetSymbol(i), majorVersion);
        }
        if (status != SUCCESS) {
            assert("LoadSymbol Failed");
            return status;
        }
    }

    if (code->isV3) {
        status = ApplyRelocationsV3(agent, code.get());
    } else {
        assert("code is not V3");
        // status = ApplyRelocations(agent, code.get());
    }

    if (status != SUCCESS) {
        assert("ApplyRelocations Failed");
        return status;
    }

    code.reset();
    /*
  if (loaderOptions.DumpAll()->is_set() || loaderOptions.DumpExec()->is_set()) {
    if (!PrintToFile(hcs::DumpFileName(loaderOptions.DumpDir()->value(), LOADER_DUMP_PREFIX, "exec", codeNum))) {
      // Ignore error.
    }
  }
*/

    if (nullptr != loaded_code_object) {
        *loaded_code_object = LoadedCodeObject::Handle(loaded_code_objects.back());
    }
    return SUCCESS;
}
/*
    status_t ExecutableImpl::LoadSegments(core::IAgent* agent,
        const code::CodeObject* c,
        uint32_t majorVersion)
    {
        return LoadSegmentsV2(agent, c);
    }
*/
status_t ExecutableImpl::LoadSegments(core::IAgent* agent,
    const code::CodeObject* c)
{
    // TODO assert(c->Machine() == EM_AMDGPU && "Program code objects are not supported");
    // now I add Machine suport EM_X86_64

    if (!c->DataSegmentCount()) return ERROR_INVALID_CODE_OBJECT;

    uint64_t vaddr;
    uint64_t size;
    if (c->isV3) {
        vaddr = c->DataSegmentV3(0)->get_virtual_address(); // TODO verify use get_address is right
        size = c->DataSegmentV3(c->DataSegmentCount() - 1)->get_virtual_address() + c->DataSegmentV3(c->DataSegmentCount() - 1)->get_memory_size();
    } else {
        // vaddr = c->DataSegment(0)->vaddr();
        // size = c->DataSegment(c->DataSegmentCount() - 1)->vaddr() + c->DataSegment(c->DataSegmentCount() - 1)->memSize();
    }

    void* ptr = context_->SegmentAlloc(AMDGPU_HSA_SEGMENT_CODE_AGENT, agent, size,
        ISA_ALIGN_BYTES, true);
    if (!ptr) return ERROR_OUT_OF_RESOURCES;

    Segment* load_segment = new Segment(this, agent, AMDGPU_HSA_SEGMENT_CODE_AGENT,
        ptr, size, vaddr, c->DataSegmentV3(0)->get_offset());
    if (!load_segment) return ERROR_OUT_OF_RESOURCES;

    status_t status = SUCCESS;
    for (size_t i = 0; i < c->DataSegmentCount(); ++i) {
        if (c->isV3) {
            status = LoadSegmentV3(c->DataSegmentV3(i), load_segment);
        } else {
            assert("not CodeV3");
            // status = LoadSegmentV2(c->DataSegment(i), load_segment);
        }
        if (status != SUCCESS) return status;
    }

    objects.push_back(load_segment);
    loaded_code_objects.back()->LoadedSegments().push_back(load_segment);

    return SUCCESS;
}

#if 0
    status_t ExecutableImpl::LoadSegmentV2(const code::Segment* data_segment,
        loader::Segment* load_segment)
    {
        assert(data_segment && load_segment);
        load_segment->Copy(data_segment->vaddr(), data_segment->data(),
            data_segment->imageSize());

        return SUCCESS;
    }
#endif
// TODO V3
status_t ExecutableImpl::LoadSegmentV3(const ELFIO::segment* data_segment,
    loader::Segment* load_segment)
{
    assert(data_segment && load_segment);
    load_segment->Copy(data_segment->get_virtual_address(), data_segment->get_data(),
        data_segment->get_file_size()); // FIXME find out corresponding imageSize in V2

    // load_segment->Print(std::cout);  // TODO to find out debug symbol purpose
    return SUCCESS;
}

#if 0
    status_t ExecutableImpl::LoadSymbol(core::IAgent* agent,
        code::Symbol* sym,
        uint32_t majorVersion)
    {
        if (sym->IsDeclaration()) {
            return LoadDeclarationSymbol(agent, sym, majorVersion);
        } else {
            return LoadDefinitionSymbol(agent, sym, majorVersion);
        }
    }

    status_t ExecutableImpl::LoadDefinitionSymbol(core::IAgent* agent,
        code::Symbol* sym,
        uint32_t majorVersion)
    {
        bool isAgent = sym->IsAgent();
        if (majorVersion >= 2) {
            isAgent = agent.handle != 0;
        }
        if (isAgent) {
            auto agent_symbol = agent_symbols_.find(std::make_pair(sym->Name(), agent));
            if (agent_symbol != agent_symbols_.end()) {
                // TODO(spec): this is not spec compliant.
                return ERROR_VARIABLE_ALREADY_DEFINED;
            }
        } else {
            auto program_symbol = program_symbols_.find(sym->Name());
            if (program_symbol != program_symbols_.end()) {
                // TODO(spec): this is not spec compliant.
                return ERROR_VARIABLE_ALREADY_DEFINED;
            }
        }

        uint64_t address = SymbolAddress(agent, sym);
        if (!address) {
            return ERROR_INVALID_CODE_OBJECT;
        }

        SymbolImpl* symbol = nullptr;
        if (sym->IsVariableSymbol()) {
            symbol = new VariableSymbol(true,
                sym->GetModuleName(),
                sym->GetSymbolName(),
                sym->Linkage(),
                true, // sym->IsDefinition()
                sym->Allocation(),
                sym->Segment(),
                sym->Size(),
                sym->Alignment(),
                sym->IsConst(),
                false,
                address);
        } else if (sym->IsKernelSymbol()) {
            hcs_kernel_code_t akc;
            sym->GetSection()->getData(sym->SectionOffset(), &akc, sizeof(akc));

            uint32_t kernarg_segment_size = uint32_t(akc.kernarg_segment_byte_size);
            uint32_t kernarg_segment_alignment = uint32_t(1 << akc.kernarg_segment_alignment);
            uint32_t group_segment_size = uint32_t(akc.workgroup_group_segment_byte_size);
            uint32_t private_segment_size = uint32_t(akc.workitem_private_segment_byte_size);
            uint32_t kernel_ctrl = uint32_t(akc.kernel_ctrl);
            uint32_t kernel_mode = uint32_t(akc.kernel_mode);
            bool is_dynamic_callstack = HCS_BITS_GET(akc.kernel_code_properties, AMD_KERNEL_CODE_PROPERTIES_IS_DYNAMIC_CALLSTACK) ? true : false;

            uint64_t size = sym->Size();

            if (!size && sym->SectionOffset() < sym->GetSection()->size()) {
                // ORCA Runtime relies on symbol size equal to size of kernel ISA. If symbol size is 0 in ELF,
                // calculate end of segment - symbol value.
                size = sym->GetSection()->size() - sym->SectionOffset();
            }
            KernelSymbol* kernel_symbol = new KernelSymbol(true,
                sym->GetModuleName(),
                sym->GetSymbolName(),
                sym->Linkage(),
                true, // sym->IsDefinition()
                kernarg_segment_size,
                kernarg_segment_alignment,
                group_segment_size,
                private_segment_size,
                kernel_ctrl,
                kernel_mode,
                is_dynamic_callstack,
                size,
                256,
                address);
            kernel_symbol->debug_info.elf_raw = code->ElfData();
            kernel_symbol->debug_info.elf_size = code->ElfSize();
            kernel_symbol->debug_info.kernel_name = kernel_symbol->full_name.c_str();
            kernel_symbol->debug_info.owning_segment = (void*)SymbolSegment(agent, sym)->Address(sym->GetSection()->addr());
            symbol = kernel_symbol;

            // \todo This is a debugger backdoor: needs to be
            // removed.
            /*
      // TODO schi  llvm-ir kernel don't us akc, so it should not be override any bit
      uint64_t target_address = sym->GetSection()->addr() + sym->SectionOffset() + ((size_t)(&((hcs_kernel_code_t*)0)->runtime_loader_kernel_symbol));
      uint64_t source_value = (uint64_t) (uintptr_t) &kernel_symbol->debug_info;
      SymbolSegment(agent, sym)->Copy(target_address, &source_value, sizeof(source_value));
      */
        } else {
            assert(!"Unexpected symbol type in LoadDefinitionSymbol");
            return ERROR;
        }
        assert(symbol);
        if (isAgent) {
            symbol->agent = agent;
            agent_symbols_.insert(std::make_pair(std::make_pair(sym->Name(), agent), symbol));
        } else {
            program_symbols_.insert(std::make_pair(sym->Name(), symbol));
        }
        return SUCCESS;
    }

    status_t ExecutableImpl::LoadDeclarationSymbol(core::IAgent* agent,
        code::Symbol* sym,
        uint32_t majorVersion)
    {
        auto program_symbol = program_symbols_.find(sym->Name());
        if (program_symbol == program_symbols_.end()) {
            auto agent_symbol = agent_symbols_.find(std::make_pair(sym->Name(), agent));
            if (agent_symbol == agent_symbols_.end()) {
                // TODO(spec): this is not spec compliant.
                return ERROR_VARIABLE_UNDEFINED;
            }
        }
        return SUCCESS;
    }
#endif
Segment* ExecutableImpl::VirtualAddressSegment(uint64_t vaddr)
{
    for (auto& seg : loaded_code_objects.back()->LoadedSegments()) {
        if (seg->IsAddressInSegment(vaddr)) {
            return seg;
        }
    }
    return 0;
}

#if 0
uint64_t ExecutableImpl::SymbolAddress(core::IAgent* agent, code::Symbol* sym)
{
    code::Section* sec = sym->GetSection();
    Segment* seg = SectionSegment(agent, sec);
    return nullptr == seg ? 0 : (uint64_t)(uintptr_t)seg->Address(sym->VAddr());
}

uint64_t ExecutableImpl::SymbolAddress(core::IAgent* agent, elf::Symbol* sym)
{
    // TODO
    // elf::Section* sec = sym->section();
    elf::Section* sec = sym->section();
    Segment* seg = SectionSegment(agent, sec);
    uint64_t vaddr = sec->addr() + sym->value();
    return nullptr == seg ? 0 : (uint64_t)(uintptr_t)seg->Address(vaddr);
}

Segment* ExecutableImpl::SymbolSegment(core::IAgent* agent, code::Symbol* sym)
{
    return SectionSegment(agent, sym->GetSection());
}

Segment* ExecutableImpl::SectionSegment(core::IAgent* agent, code::Section* sec)
{
    for (Segment* seg : loaded_code_objects.back()->LoadedSegments()) {
        if (seg->IsAddressInSegment(sec->addr())) {
            return seg;
        }
    }
    return 0;
}
#endif

// TODO V3
Segment* ExecutableImpl::SectionSegment(core::IAgent* agent, ELFIO::section* sec)
{
    for (Segment* seg : loaded_code_objects.back()->LoadedSegments()) {
        if (seg->IsAddressInSegment(sec->get_address())) {
            return seg;
        }
    }
    return 0;
}

#if 0
    // status_t ExecutableImpl::ApplyRelocations(core::IAgent* agent, amd::hsa::code::CodeObject *c)
    status_t ExecutableImpl::ApplyRelocations(core::IAgent* agent, hcs::code::CodeObject* c)
    {
        status_t status = SUCCESS;
        for (size_t i = 0; i < c->RelocationSectionCount(); ++i) {
            if (c->GetRelocationSection(i)->targetSection()) {
                status = ApplyStaticRelocationSection(agent, c->GetRelocationSection(i));
            } else {
                // Dynamic relocations are supported starting code object v2.1.
                uint32_t majorVersion, minorVersion;
                if (!c->GetNoteCodeObjectVersion(&majorVersion, &minorVersion)) {
                    return ERROR_INVALID_CODE_OBJECT;
                }
                if (majorVersion < 2) {
                    return ERROR_INVALID_CODE_OBJECT;
                }
                if (majorVersion == 2 && minorVersion < 1) {
                    return ERROR_INVALID_CODE_OBJECT;
                }
                status = ApplyDynamicRelocationSection(agent, c->GetRelocationSection(i));
            }
            if (status != SUCCESS) {
                return status;
            }
        }
        return SUCCESS;
    }
#endif
status_t ExecutableImpl::ApplyRelocationsV3(core::IAgent* agent, code::CodeObject* c)
{
    status_t status = SUCCESS;
    for (size_t i = 0; i < c->RelocationSectionCount(); ++i) {
        ELFIO::section* reloc_sec = c->GetRelocationSectionV3(i);
        if (reloc_sec) {
            ELFIO::section* target_section = c->GetElfio().sections[reloc_sec->get_info()];
            if (target_section->get_type() & SHF_ALLOC) {
                status = ApplyStaticRelocationSectionV3(agent, c, reloc_sec);
            } else {
                status = ApplyDynamicRelocationSectionV3(agent, c, reloc_sec);
            }
            if (status != SUCCESS) {
                return status;
            }
        }
    }
    return SUCCESS;
}

status_t ExecutableImpl::ApplyStaticRelocationSectionV3(core::IAgent* agent, code::CodeObject* c, ELFIO::section* sec)
{
    status_t status = SUCCESS;
    // Skip link-time relocations (if any).
    // TODO below need more verify
    ELFIO::section* target_section = c->GetElfio().sections[sec->get_info()];
    ELFIO::section* symbol_section = c->GetElfio().sections[sec->get_link()];
    if (!(target_section->get_type() & SHF_ALLOC)) {
        return SUCCESS;
    }
    ELFIO::relocation_section_accessor reloc_reader(c->GetElfio(), sec);
    /*

        auto it = find_section_if(c->GetElfio(), [](const ELFIO::section* x) {
            return x->get_type() == SHT_SYMTAB;
        });

        assert(!it);
        const ELFIO::symbol_section_accessor symtab{c->getElfio(), it};
*/
    ELFIO::symbol_section_accessor symtab { c->GetElfio(), symbol_section };

    for (size_t i = 0; i < reloc_reader.get_entries_num(); ++i) {
        code::RelocationV3 rel = code::read_relocation(reloc_reader, i);
        status = ApplyStaticRelocationV3(agent, symtab, rel);
        if (status != SUCCESS) {
            return status;
        }
    }

    return status;
}

#if 0
status_t ExecutableImpl::ApplyStaticRelocationSection(core::IAgent* agent, code::RelocationSection* sec)
{
    // Skip link-time relocations (if any).
    if (!(sec->targetSection()->flags() & SHF_ALLOC)) {
        return SUCCESS;
    }
    status_t status = SUCCESS;
    for (size_t i = 0; i < sec->relocationCount(); ++i) {
        status = ApplyStaticRelocation(agent, sec->relocation(i));
        if (status != SUCCESS) {
            return status;
        }
    }
    return SUCCESS;
}

status_t ExecutableImpl::ApplyStaticRelocation(core::IAgent* agent, code::Relocation* rel)
{
    // status_t status = SUCCESS;
    elf::Symbol* sym = rel->symbol();
    code::RelocationSection* rsec = rel->section();
    code::Section* sec = rsec->targetSection();
    Segment* rseg = SectionSegment(agent, sec);
    size_t reladdr = sec->addr() + rel->offset();
    switch (rel->type()) {
    case R_AMDGPU_32_LOW:
    case R_AMDGPU_32_HIGH:
    case R_AMDGPU_64: {
        uint64_t addr;
        switch (sym->type()) {
        case STT_OBJECT:
        case STT_SECTION:
        case STT_AMDGPU_HSA_KERNEL:
        case STT_AMDGPU_HSA_INDIRECT_FUNCTION:
            addr = SymbolAddress(agent, sym);
            if (!addr) {
                return ERROR_INVALID_CODE_OBJECT;
            }
            break;
        case STT_COMMON: {
            core::IAgent* sagent = agent;
            if (STA_AMDGPU_HSA_GLOBAL_PROGRAM == ELF64_ST_AMDGPU_ALLOCATION(sym->other())) {
                sagent = nullptr;
            }
            SymbolImpl* esym = (SymbolImpl*)GetSymbolInternal(sym->name().c_str(), sagent);
            if (!esym) {
                return ERROR_VARIABLE_UNDEFINED;
            }
            addr = esym->address;
            break;
        }
        default:
            return ERROR_INVALID_CODE_OBJECT;
        }
        addr += rel->addend();

        uint32_t addr32 = 0;
        switch (rel->type()) {
        case R_AMDGPU_32_HIGH:
            addr32 = uint32_t((addr >> 32) & 0xFFFFFFFF);
            rseg->Copy(reladdr, &addr32, sizeof(addr32));
            break;
        case R_AMDGPU_32_LOW:
            addr32 = uint32_t(addr & 0xFFFFFFFF);
            rseg->Copy(reladdr, &addr32, sizeof(addr32));
            break;
        case R_AMDGPU_64:
            rseg->Copy(reladdr, &addr, sizeof(addr));
            break;
        default:
            return ERROR_INVALID_CODE_OBJECT;
        }
        break;
    }
        /*
    case R_AMDGPU_INIT_SAMPLER:
    {
      if (STT_AMDGPU_HSA_METADATA != sym->type() ||
          SHT_PROGBITS != sym->section()->type() ||
          !(sym->section()->flags() & SHF_MERGE)) {
        return ERROR_INVALID_CODE_OBJECT;
      }
      amdgpu_hsa_sampler_descriptor_t desc;
      if (!sym->section()->getData(sym->value(), &desc, sizeof(desc))) {
        return ERROR_INVALID_CODE_OBJECT;
      }
      if (AMDGPU_HSA_METADATA_KIND_INIT_SAMP != desc.kind) {
        return ERROR_INVALID_CODE_OBJECT;
      }

      hsa_ext_sampler_descriptor_t hsa_sampler_descriptor;
      hsa_sampler_descriptor.coordinate_mode =
        hsa_ext_sampler_coordinate_mode_t(desc.coord);
      hsa_sampler_descriptor.filter_mode =
        hsa_ext_sampler_filter_mode_t(desc.filter);
      hsa_sampler_descriptor.address_mode =
        hsa_ext_sampler_addressing_mode_t(desc.addressing);

      hsa_ext_sampler_t hsa_sampler = {0};
      status = context_->SamplerCreate(agent, &hsa_sampler_descriptor, &hsa_sampler);
      if (status != SUCCESS) { return status; }
      assert(hsa_sampler.handle);
      rseg->Copy(reladdr, &hsa_sampler, sizeof(hsa_sampler));
      break;
    }

    case R_AMDGPU_INIT_IMAGE:
    {
      if (STT_AMDGPU_HSA_METADATA != sym->type() ||
          SHT_PROGBITS != sym->section()->type() ||
          !(sym->section()->flags() & SHF_MERGE)) {
        return ERROR_INVALID_CODE_OBJECT;
      }

      amdgpu_hsa_image_descriptor_t desc;
      if (!sym->section()->getData(sym->value(), &desc, sizeof(desc))) {
        return ERROR_INVALID_CODE_OBJECT;
      }
      if (AMDGPU_HSA_METADATA_KIND_INIT_ROIMG != desc.kind &&
          AMDGPU_HSA_METADATA_KIND_INIT_WOIMG != desc.kind &&
          AMDGPU_HSA_METADATA_KIND_INIT_RWIMG != desc.kind) {
        return ERROR_INVALID_CODE_OBJECT;
      }

      hsa_ext_image_format_t hsa_image_format;
      hsa_image_format.channel_order =
        hsa_ext_image_channel_order_t(desc.channel_order);
      hsa_image_format.channel_type =
        hsa_ext_image_channel_type_t(desc.channel_type);

      hsa_ext_image_descriptor_t hsa_image_descriptor;
      hsa_image_descriptor.geometry =
        hsa_ext_image_geometry_t(desc.geometry);
      hsa_image_descriptor.width = size_t(desc.width);
      hsa_image_descriptor.height = size_t(desc.height);
      hsa_image_descriptor.depth = size_t(desc.depth);
      hsa_image_descriptor.array_size = size_t(desc.array);
      hsa_image_descriptor.format = hsa_image_format;

      hsa_access_permission_t hsa_image_permission = HSA_ACCESS_PERMISSION_RO;
      switch (desc.kind) {
        case AMDGPU_HSA_METADATA_KIND_INIT_ROIMG: {
          hsa_image_permission = HSA_ACCESS_PERMISSION_RO;
          break;
        }
        case AMDGPU_HSA_METADATA_KIND_INIT_WOIMG: {
          hsa_image_permission = HSA_ACCESS_PERMISSION_WO;
          break;
        }
        case AMDGPU_HSA_METADATA_KIND_INIT_RWIMG: {
          hsa_image_permission = HSA_ACCESS_PERMISSION_RW;
          break;
        }
        default: {
          assert(false);
          return ERROR_INVALID_CODE_OBJECT;
        }
      }

      hsa_ext_image_t hsa_image = {0};
      status = context_->ImageCreate(agent, hsa_image_permission,
                                  &hsa_image_descriptor,
                                  NULL, // TODO: image_data?
                                  &hsa_image);
      if (status != SUCCESS) { return status; }
      rseg->Copy(reladdr, &hsa_image, sizeof(hsa_image));
      break;
    }
*/
    default:
        // Ignore.
        break;
    }
    return SUCCESS;
}
#endif

status_t ExecutableImpl::ApplyStaticRelocationV3(core::IAgent* agent, ELFIO::symbol_section_accessor& symbols, code::RelocationV3& rel)
{
    // Find the symbol
    unsigned char bind;
    unsigned char symbolType;
    ELFIO::Elf_Half section;
    unsigned char other;

    symbols.get_symbol(rel.symbol, rel.symbolName, rel.symbolValue, rel.size, bind, symbolType,
        section, other);
    switch (rel.type) {
    case R_386_NONE: // none
        rel.calcValue = 0;
        break;
    case R_386_32: // S + A
        rel.calcValue = rel.symbolValue + rel.addend;
        break;
    case R_386_PC32: // S + A - P
        rel.calcValue = rel.symbolValue + rel.addend - rel.offset;
        break;
    case R_386_GOT32: // G + A - P
        rel.calcValue = 0;
        break;
    case R_386_PLT32: // L + A - P
        rel.calcValue = 0;
        break;
    case R_386_COPY: // none
        rel.calcValue = 0;
        break;
    case R_386_GLOB_DAT: // S
    case R_386_JMP_SLOT: // S
        rel.calcValue = rel.symbolValue;
        break;
    case R_386_RELATIVE: // B + A
        rel.calcValue = rel.addend;
        break;
    case R_386_GOTOFF: // S + A - GOT
        rel.calcValue = 0;
        break;
    case R_386_GOTPC: // GOT + A - P
        rel.calcValue = 0;
        break;
    default: // Not recognized symbol!
        rel.calcValue = 0;
        break;
    }

    // TODO need modify below to update rel addr
    /*
        code::Section* rsec = rel->section();
        code::Section* sec = rsec->targetSection();
        Segment* rseg = SectionSegment(agent, sec);
        size_t reladdr = sec->addr() + rel->offset();

        switch (rel->type()) {
        case R_AMDGPU_32_LOW:
        case R_AMDGPU_32_HIGH:
        case R_AMDGPU_64: {
            uint64_t addr;
            switch (sym->type()) {
            case STT_OBJECT:
            case STT_SECTION:
            case STT_AMDGPU_HSA_KERNEL:
            case STT_AMDGPU_HSA_INDIRECT_FUNCTION:
                addr = SymbolAddress(agent, sym);
                if (!addr) {
                    return ERROR_INVALID_CODE_OBJECT;
                }
                break;
            case STT_COMMON: {
                core::IAgent* sagent = &agent;
                if (STA_AMDGPU_HSA_GLOBAL_PROGRAM == ELF64_ST_AMDGPU_ALLOCATION(sym->other())) {
                    sagent = nullptr;
                }
                SymbolImpl* esym = (SymbolImpl*)GetSymbolInternal(sym->name().c_str(), sagent);
                if (!esym) {
                    return ERROR_VARIABLE_UNDEFINED;
                }
                addr = esym->address;
                break;
            }
            default:
                return ERROR_INVALID_CODE_OBJECT;
            }
            addr += rel->addend();

            uint32_t addr32 = 0;
            switch (rel->type()) {
            case R_AMDGPU_32_HIGH:
                addr32 = uint32_t((addr >> 32) & 0xFFFFFFFF);
                rseg->Copy(reladdr, &addr32, sizeof(addr32));
                break;
            case R_AMDGPU_32_LOW:
                addr32 = uint32_t(addr & 0xFFFFFFFF);
                rseg->Copy(reladdr, &addr32, sizeof(addr32));
                break;
            case R_AMDGPU_64:
                rseg->Copy(reladdr, &addr, sizeof(addr));
                break;
            default:
                return ERROR_INVALID_CODE_OBJECT;
            }
            break;
        }
        default:
            // Ignore.
            break;
        }
        */
    return SUCCESS;
}

#if 0
status_t ExecutableImpl::ApplyDynamicRelocationSection(core::IAgent* agent, code::RelocationSection* sec)
{
    status_t status = SUCCESS;
    for (size_t i = 0; i < sec->relocationCount(); ++i) {
        status = ApplyDynamicRelocation(agent, sec->relocation(i));
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

status_t ExecutableImpl::ApplyDynamicRelocation(core::IAgent* agent, code::Relocation* rel)
{
    Segment* relSeg = VirtualAddressSegment(rel->offset());
    uint64_t symAddr = 0;
    switch (rel->symbol()->type()) {
    case STT_OBJECT:
    case STT_AMDGPU_HSA_KERNEL: {
        Segment* symSeg = VirtualAddressSegment(rel->symbol()->value());
        symAddr = reinterpret_cast<uint64_t>(symSeg->Address(rel->symbol()->value()));
        break;
    }

    // External symbols, they must be defined prior loading.
    case STT_NOTYPE: {
        // TODO: Only agent allocation variables are supported in v2.1. How will
        // we distinguish between program allocation and agent allocation
        // variables?
        auto agent_symbol = agent_symbols_.find(std::make_pair(rel->symbol()->name(), agent));
        if (agent_symbol == agent_symbols_.end()) {
            // External symbols must be defined prior loading.
            return ERROR_VARIABLE_UNDEFINED;
        }
        symAddr = agent_symbol->second->address;
        break;
    }

    default:
        // Only objects and kernels are supported in v2.1.
        return ERROR_INVALID_CODE_OBJECT;
    }
    symAddr += rel->addend();

    switch (rel->type()) {
    case R_AMDGPU_32_HIGH: {
        uint32_t symAddr32 = uint32_t((symAddr >> 32) & 0xFFFFFFFF);
        relSeg->Copy(rel->offset(), &symAddr32, sizeof(symAddr32));
        break;
    }

    case R_AMDGPU_32_LOW: {
        uint32_t symAddr32 = uint32_t(symAddr & 0xFFFFFFFF);
        relSeg->Copy(rel->offset(), &symAddr32, sizeof(symAddr32));
        break;
    }

    case R_AMDGPU_64: {
        relSeg->Copy(rel->offset(), &symAddr, sizeof(symAddr));
        break;
    }

    case R_AMDGPU_INIT_IMAGE:
    case R_AMDGPU_INIT_SAMPLER:
        // Images and samplers are not supported in v2.1.
        return ERROR_INVALID_CODE_OBJECT;

    default:
        // Ignore.
        break;
    }
    return SUCCESS;
}
#endif

status_t ExecutableImpl::ApplyDynamicRelocationSectionV3(core::IAgent* agent, code::CodeObject* c, ELFIO::section* sec)
{
    status_t status = SUCCESS;

    // ELFIO::section* target_section = c->GetElfio().sections[sec->get_info()];
    ELFIO::section* symbol_section = c->GetElfio().sections[sec->get_link()];
    ELFIO::relocation_section_accessor reloc_reader(c->GetElfio(), sec);
    ELFIO::symbol_section_accessor symtab { c->GetElfio(), symbol_section };

    for (size_t i = 0; i < reloc_reader.get_entries_num(); ++i) {
        code::RelocationV3 rel = code::read_relocation(reloc_reader, i);
        status = ApplyDynamicRelocationV3(agent, c, symtab, rel);
        if (status != SUCCESS) {
            return status;
        }
    }

    return SUCCESS;
}

status_t ExecutableImpl::ApplyDynamicRelocationV3(core::IAgent* agent, code::CodeObject* c, ELFIO::symbol_section_accessor& symbols, code::RelocationV3& rel)
{
    // Find the symbol
    unsigned char bind;
    unsigned char symbolType;
    ELFIO::Elf_Half section;
    unsigned char other;

    symbols.get_symbol(rel.symbol, rel.symbolName, rel.symbolValue, rel.size, bind, symbolType,
        section, other);
    switch (rel.type) {
    case R_386_NONE: // none
        rel.calcValue = 0;
        break;
    case R_386_32: // S + A
        rel.calcValue = rel.symbolValue + rel.addend;
        break;
    case R_386_PC32: // S + A - P
        rel.calcValue = rel.symbolValue + rel.addend - rel.offset;
        break;
    case R_386_GOT32: // G + A - P
        rel.calcValue = 0;
        break;
    case R_386_PLT32: // L + A - P
        rel.calcValue = 0;
        break;
    case R_386_COPY: // none
        rel.calcValue = 0;
        break;
    case R_386_GLOB_DAT: // S
    case R_386_JMP_SLOT: // S
        rel.calcValue = rel.symbolValue;
        break;
    case R_386_RELATIVE: // B + A
        rel.calcValue = rel.addend;
        break;
    case R_386_GOTOFF: // S + A - GOT
        rel.calcValue = 0;
        break;
    case R_386_GOTPC: // GOT + A - P
        rel.calcValue = 0;
        break;
    default: // Not recognized symbol!
        rel.calcValue = 0;
        break;
    }
    //
    Segment* relSeg = VirtualAddressSegment(rel.offset);
    Segment* symSeg = VirtualAddressSegment(rel.symbolValue);
    uint64_t symAddr = reinterpret_cast<uint64_t>(symSeg->Address(rel.calcValue));
    // symAddr += rel->addend();
    // raddr = (uint64_t)(uintptr_t)seg->Address(rel.calcValue);

    relSeg->Copy(rel.offset, &symAddr, sizeof(symAddr));

    /*
        Segment* relSeg = VirtualAddressSegment(rel->offset());
        uint64_t symAddr = 0;
        switch (rel->symbol()->type()) {
        case STT_OBJECT:
        case STT_AMDGPU_HSA_KERNEL: {
            Segment* symSeg = VirtualAddressSegment(rel->symbol()->value());
            symAddr = reinterpret_cast<uint64_t>(symSeg->Address(rel->symbol()->value()));
            break;
        }

        // External symbols, they must be defined prior loading.
        case STT_NOTYPE: {
            // TODO: Only agent allocation variables are supported in v2.1. How will
            // we distinguish between program allocation and agent allocation
            // variables?
            auto agent_symbol = agent_symbols_.find(std::make_pair(rel->symbol()->name(), agent));
            if (agent_symbol == agent_symbols_.end()) {
                // External symbols must be defined prior loading.
                return ERROR_VARIABLE_UNDEFINED;
            }
            symAddr = agent_symbol->second->address;
            break;
        }

        default:
            // Only objects and kernels are supported in v2.1.
            return ERROR_INVALID_CODE_OBJECT;
        }
        symAddr += rel->addend();

        switch (rel->type()) {
        case R_AMDGPU_32_HIGH: {
            uint32_t symAddr32 = uint32_t((symAddr >> 32) & 0xFFFFFFFF);
            relSeg->Copy(rel->offset(), &symAddr32, sizeof(symAddr32));
            break;
        }

        case R_AMDGPU_32_LOW: {
            uint32_t symAddr32 = uint32_t(symAddr & 0xFFFFFFFF);
            relSeg->Copy(rel->offset(), &symAddr32, sizeof(symAddr32));
            break;
        }

        case R_AMDGPU_64: {
            relSeg->Copy(rel->offset(), &symAddr, sizeof(symAddr));
            break;
        }

        case R_AMDGPU_INIT_IMAGE:
        case R_AMDGPU_INIT_SAMPLER:
            // Images and samplers are not supported in v2.1.
            return ERROR_INVALID_CODE_OBJECT;

        default:
            // Ignore.
            break;
        }
        */
    return SUCCESS;
}

status_t ExecutableImpl::Freeze(const char* options)
{
    // amd::hsa::common::WriterLockGuard<amd::hsa::common::ReaderWriterLock> writer_lock(rw_lock_);
    WriterLockGuard<ReaderWriterLock> writer_lock(rw_lock_);
    if (HSA_EXECUTABLE_STATE_FROZEN == state_) {
        return ERROR_FROZEN_EXECUTABLE;
    }

    for (auto& lco : loaded_code_objects) {
        for (auto& ls : lco->LoadedSegments()) {
            ls->Freeze();
        }
    }

    state_ = HSA_EXECUTABLE_STATE_FROZEN;
    return SUCCESS;
}

void ExecutableImpl::Print(std::ostream& out)
{
    out << "AMD Executable" << std::endl;
    out << "  Id: " << id()
        << "  Profile: " << code::HsaProfileToString(profile())
        << std::endl
        << std::endl;
    out << "Loaded Objects (total " << objects.size() << ")" << std::endl;
    size_t i = 0;
    for (ExecutableObject* o : objects) {
        out << "Loaded Object " << i++ << ": ";
        o->Print(out);
        out << std::endl;
    }
    out << "End AMD Executable" << std::endl;
}

bool ExecutableImpl::PrintToFile(const std::string& filename)
{
    std::ofstream out(filename);
    if (out.fail()) {
        return false;
    }
    Print(out);
    return out.fail();
}

status_t ExecutableImpl::LoadSymbol(core::IAgent* agent,
    code::SymbolV3* sym,
    uint32_t majorVersion)
{
    if (sym->IsDeclaration()) {
        return LoadDeclarationSymbol(agent, sym, majorVersion);
    } else {
        return LoadDefinitionSymbol(agent, sym, majorVersion);
    }
}

status_t ExecutableImpl::LoadDefinitionSymbol(core::IAgent* agent,
    code::SymbolV3* sym,
    uint32_t majorVersion)
{
    bool isAgent = sym->IsAgent();
    if (majorVersion >= 3) {
        isAgent = agent != nullptr;
    }
    if (isAgent) {
        auto agent_symbol = agent_symbols_.find(std::make_pair(sym->Name(), agent));
        if (agent_symbol != agent_symbols_.end()) {
            // TODO(spec): this is not spec compliant.
            return ERROR_VARIABLE_ALREADY_DEFINED;
        }
    } else {
        auto program_symbol = program_symbols_.find(sym->Name());
        if (program_symbol != program_symbols_.end()) {
            // TODO(spec): this is not spec compliant.
            return ERROR_VARIABLE_ALREADY_DEFINED;
        }
    }

    uint64_t address = SymbolAddress(agent, sym);
    if (!address) {
        return ERROR_INVALID_CODE_OBJECT;
    }

    SymbolImpl* symbol = nullptr;
    if (sym->IsVariableSymbol()) {
        symbol = new VariableSymbol(true,
            sym->GetModuleName(),
            sym->GetSymbolName(),
            sym->Linkage(),
            true, // sym->IsDefinition()
            sym->Allocation(),
            sym->Segment(),
            sym->Size(),
            sym->Alignment(),
            sym->IsConst(),
            false,
            address);
    } else if (sym->IsKernelSymbol()) {

        code::KernelSymbolV3* ksym = dynamic_cast<code::KernelSymbolV3*>(sym);

        uint32_t kernarg_segment_size = ksym->kernarg_segment_size;
        uint32_t kernarg_segment_alignment = ksym->kernarg_segment_alignment;
        uint32_t group_segment_size = ksym->group_segment_size;
        uint32_t private_segment_size = ksym->private_segment_size;
        bool is_dynamic_callstack = ksym->is_dynamic_callstack;
        uint32_t kernel_ctrl = ksym->kernel_ctrl;
        uint32_t kernel_mode = ksym->kernel_mode;

        uint64_t size = ksym->Size();

        if (!size && ksym->SectionOffset() < ksym->GetSection()->get_size()) {
            // ORCA Runtime relies on symbol size equal to size of kernel ISA. If symbol size is 0 in ELF,
            // calculate end of segment - symbol value.
            size = ksym->GetSection()->get_size() - ksym->SectionOffset();
        }
        KernelSymbol* kernel_symbol = new KernelSymbol(true,
            sym->GetModuleName(),
            sym->GetSymbolName(),
            sym->Linkage(),
            true, // sym->IsDefinition()
            kernarg_segment_size,
            kernarg_segment_alignment,
            group_segment_size,
            private_segment_size,
            kernel_ctrl,
            kernel_mode,
            is_dynamic_callstack,
            size,
            256,
            address);
        kernel_symbol->debug_info.elf_raw = code->ElfData();
        kernel_symbol->debug_info.elf_size = code->ElfSize();
        kernel_symbol->debug_info.kernel_name = kernel_symbol->full_name.c_str();
        kernel_symbol->debug_info.owning_segment = (void*)SymbolSegment(agent, sym)->Address(sym->GetSection()->get_address());
        symbol = kernel_symbol;

        // \todo This is a debugger backdoor: needs to be
        // removed.
        /*
      // TODO schi  llvm-ir kernel don't us akc, so it should not be override any bit
      uint64_t target_address = sym->GetSection()->addr() + sym->SectionOffset() + ((size_t)(&((hcs_kernel_code_t*)0)->runtime_loader_kernel_symbol));
      uint64_t source_value = (uint64_t) (uintptr_t) &kernel_symbol->debug_info;
      SymbolSegment(agent, sym)->Copy(target_address, &source_value, sizeof(source_value));
      */
    } else {
        assert(!"Unexpected symbol type in LoadDefinitionSymbol");
        return ERROR;
    }
    assert(symbol);
    if (isAgent) {
        symbol->agent = agent;
        agent_symbols_.insert(std::make_pair(std::make_pair(sym->Name(), agent), symbol));
    } else {
        program_symbols_.insert(std::make_pair(sym->Name(), symbol));
    }
    return SUCCESS;
}

status_t ExecutableImpl::LoadDeclarationSymbol(core::IAgent* agent,
    code::SymbolV3* sym,
    uint32_t majorVersion)
{
    auto program_symbol = program_symbols_.find(sym->Name());
    if (program_symbol == program_symbols_.end()) {
        auto agent_symbol = agent_symbols_.find(std::make_pair(sym->Name(), agent));
        if (agent_symbol == agent_symbols_.end()) {
            // TODO(spec): this is not spec compliant.
            return ERROR_VARIABLE_UNDEFINED;
        }
    }
    return SUCCESS;
}

uint64_t ExecutableImpl::SymbolAddress(core::IAgent* agent, code::SymbolV3* sym)
{
    /* FIXME
  code::Section* sec = sym->GetSection();
  Segment* seg = SectionSegment(agent, sec);
  return nullptr == seg ? 0 : (uint64_t) (uintptr_t) seg->Address(sym->VAddr());
  */
    ELFIO::section* sec = sym->GetSection();
    Segment* seg = SectionSegment(agent, sec);
    return nullptr == seg ? 0 : (uint64_t)(uintptr_t)seg->Address(sym->VAddr());
}

Segment* ExecutableImpl::SymbolSegment(core::IAgent* agent, code::SymbolV3* sym)
{
    return SectionSegment(agent, sym->GetSection());
}

} // namespace loader
// } // namespace amd
