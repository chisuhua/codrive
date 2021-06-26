#include "CodeObject.h"
// #include "hip/clang_detail/program_state.hpp"
#include "CodeObjectUtil.h"
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <iomanip>
// #include <libelf.h>
#include "elfio/elfio.hpp"
#include "ElfDefine.h"
#include "msgpack.hpp"
#include "utils/lang/hashstring.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
// #include "iguana/msgpack.hpp"

#ifndef _WIN32
#define _alloca alloca
#endif

namespace code {

    using elf::GetNoteString;
#if 0
    bool Symbol::IsDeclaration() const
    {
        return elfsym->type() == STT_COMMON;
    }

    bool Symbol::IsDefinition() const
    {
        return !IsDeclaration();
    }

    bool Symbol::IsAgent() const
    {
        return elfsym->section()->flags() & SHF_AMDGPU_HSA_AGENT ? true : false;
    }

    hsa_symbol_linkage_t Symbol::Linkage() const
    {
        return elfsym->binding() == STB_GLOBAL ? HSA_SYMBOL_LINKAGE_PROGRAM : HSA_SYMBOL_LINKAGE_MODULE;
    }

    hsa_variable_allocation_t Symbol::Allocation() const
    {
        return IsAgent() ? HSA_VARIABLE_ALLOCATION_AGENT : HSA_VARIABLE_ALLOCATION_PROGRAM;
    }

    hsa_variable_segment_t Symbol::Segment() const
    {
        return elfsym->section()->flags() & SHF_AMDGPU_HSA_READONLY ? HSA_VARIABLE_SEGMENT_READONLY : HSA_VARIABLE_SEGMENT_GLOBAL;
    }

    uint64_t Symbol::Size() const
    {
        return elfsym->size();
    }

    uint32_t Symbol::Size32() const
    {
        assert(elfsym->size() < UINT32_MAX);
        return (uint32_t)Size();
    }

    uint32_t Symbol::Alignment() const
    {
        assert(elfsym->section()->addralign() < UINT32_MAX);
        return uint32_t(elfsym->section()->addralign());
    }

    bool Symbol::IsConst() const
    {
        return elfsym->section()->flags() & SHF_WRITE ? true : false;
    }

    status_t Symbol::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);

        switch (attribute) {
        case HSA_CODE_SYMBOL_INFO_TYPE: {
            *((hsa_symbol_kind_t*)value) = Kind();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_NAME_LENGTH: {
            *((uint32_t*)value) = GetSymbolName().size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_NAME: {
            std::string SymbolName = GetSymbolName();
            memset(value, 0x0, SymbolName.size());
            memcpy(value, SymbolName.c_str(), SymbolName.size());
            break;
        }
        case HSA_CODE_SYMBOL_INFO_MODULE_NAME_LENGTH: {
            *((uint32_t*)value) = GetModuleName().size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_MODULE_NAME: {
            std::string ModuleName = GetModuleName();
            memset(value, 0x0, ModuleName.size());
            memcpy(value, ModuleName.c_str(), ModuleName.size());
            break;
        }
        case HSA_CODE_SYMBOL_INFO_LINKAGE: {
            *((hsa_symbol_linkage_t*)value) = Linkage();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_IS_DEFINITION: {
            *((bool*)value) = IsDefinition();
            break;
        }
        default: {
            return ERROR_INVALID_ARGUMENT;
        }
        }
        return SUCCESS;
    }

    std::string Symbol::GetModuleName() const
    {
        std::string FullName = Name();
        return FullName.rfind(":") != std::string::npos ? FullName.substr(0, FullName.find(":")) : "";
    }

    std::string Symbol::GetSymbolName() const
    {
        std::string FullName = Name();
        return FullName.rfind(":") != std::string::npos ? FullName.substr(FullName.rfind(":") + 1) : FullName;
    }

    hsa_code_symbol_t Symbol::ToHandle(Symbol* sym)
    {
        hsa_code_symbol_t s;
        s.handle = reinterpret_cast<uint64_t>(sym);
        return s;
    }

    Symbol* Symbol::FromHandle(hsa_code_symbol_t s)
    {
        return reinterpret_cast<Symbol*>(s.handle);
    }


    KernelSymbol::KernelSymbol(elf::Symbol* elfsym_, const kernel_code_t* akc)
        : Symbol(elfsym_)
        , kernarg_segment_size(0)
        , kernarg_segment_alignment(0)
        , group_segment_size(0)
        , private_segment_size(0)
        , is_dynamic_callstack(0)
    {
        if (akc) {
            kernarg_segment_size = (uint32_t)akc->kernarg_segment_byte_size;
            kernarg_segment_alignment = (uint32_t)(1 << akc->kernarg_segment_alignment);
            group_segment_size = uint32_t(akc->workgroup_group_segment_byte_size);
            private_segment_size = uint32_t(akc->workitem_private_segment_byte_size);
            is_dynamic_callstack = BITS_GET(akc->kernel_code_properties, AMD_KERNEL_CODE_PROPERTIES_IS_DYNAMIC_CALLSTACK) ? true : false;
        }
    }

    status_t KernelSymbol::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);
        switch (attribute) {
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
        case HSA_CODE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK: {
            *((bool*)value) = is_dynamic_callstack;
            break;
        }
        default: {
            return Symbol::GetInfo(attribute, value);
        }
        }
        return SUCCESS;
    }

    status_t VariableSymbol::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);
        switch (attribute) {
        case HSA_CODE_SYMBOL_INFO_VARIABLE_ALLOCATION: {
            *((hsa_variable_allocation_t*)value) = Allocation();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_SEGMENT: {
            *((hsa_variable_segment_t*)value) = Segment();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_ALIGNMENT: {
            *((uint32_t*)value) = Alignment();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_SIZE: {
            *((uint32_t*)value) = Size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_IS_CONST: {
            *((bool*)value) = IsConst();
            break;
        }
        default: {
            return Symbol::GetInfo(attribute, value);
        }
        }
        return SUCCESS;
    }
#endif
    CodeObject::CodeObject(bool combineDataSegments_)
        : img(nullptr)
        // , combineDataSegments(combineDataSegments_)
        //, hsatext(0)
        //, // imageInit(0), samplerInit(0),
        //debugInfo(0)
        //, debugLine(0)
        //, debugAbbrev(0)
    {
        /*
        for (unsigned i = 0; i < AMDGPU_HSA_SEGMENT_LAST; ++i) {
            for (unsigned j = 0; j < 2; ++j) {
                hsaSegments[i][j] = 0;
            }
        }
        for (unsigned i = 0; i < AMDGPU_HSA_SECTION_LAST; ++i) {
            hsaSections[i] = 0;
        }
        */
    }

    CodeObject::~CodeObject()
    {
#if 0
        for (Symbol* sym : symbols) {
            delete sym;
        }
#endif
    }


    bool CodeObject::PullElf()
    {
        uint32_t majorVersion, minorVersion;
        // TODO current CodeObject don't have Note Version infomation
        if (!GetNoteCodeObjectVersion(&majorVersion, &minorVersion)) {
            // return false;
            // TODO schi
            isV3 = true;
            return PullElfV3();
        }
        assert(false);
        // return PullElfV2();
    }

    bool CodeObject::LoadFromFile(const std::string& filename)
    {
        if (!img) {
            img.reset(elf::NewElf64Image());
        }
        if (!img->loadFromFile(filename)) {
            return ElfImageError();
        }
        if (!PullElf()) {
            return ElfImageError();
        }
        return true;
    }

    bool CodeObject::SaveToFile(const std::string& filename)
    {
        return img->saveToFile(filename) || ElfImageError();
    }

    bool CodeObject::WriteToBuffer(void* buffer)
    {
        return img->copyToBuffer(buffer, ElfSize()) || ElfImageError();
    }

    /*
    bool CodeObject::InitFromBuffer(const void* buffer, size_t size)
    {
      if (!img) { img.reset(hcs::elf::NewElf64Image()); }
      if (!img->initFromBuffer(buffer, size)) { return ElfImageError(); }
      if (!PullElf()) { return ElfImageError(); }
      return true;
    }
    */

    bool CodeObject::InitAsBuffer(const void* buffer, size_t size)
    {
        if (!img) {
            img.reset(elf::NewElf64Image());
        }
        if (!img->initAsBuffer(buffer, size)) {
            return ElfImageError();
        }
        if (!PullElf()) {
            return ElfImageError();
        }
        return true;
    }

    bool CodeObject::InitAsHandle(hsa_code_object_t code_object)
    {
        void* elfmemrd = reinterpret_cast<void*>(code_object.handle);
        if (!elfmemrd) {
            return false;
        }
        return InitAsBuffer(elfmemrd, 0);
    }
    /*
    bool CodeObject::InitNew()
    {
      if (!img) {
        img.reset(hcs::elf::NewElf64Image());
        uint32_t flags = 0;
        return img->initNew(EM_AMDGPU, ET_EXEC, ELFOSABI_AMDGPU_HSA, ELFABIVERSION_AMDGPU_HSA, flags) ||
          ElfImageError(); // FIXME: elfutils libelf does not allow program headers in ET_REL file type, so change it later in finalizer.
      }
      return false;
    }
*/
    /*
    bool CodeObject::Freeze()
    {
      return img->Freeze() || ElfImageError();
    }
    */
    /*
    hsa_code_object_t CodeObject::GetHandle()
    {
      hsa_code_object_t code_object;
      code_object.handle = reinterpret_cast<uint64_t>(img->data());
      return code_object;
    }
   */

    const char* CodeObject::ElfData()
    {
        return img->data();
    }

    uint64_t CodeObject::ElfSize()
    {
        return img->size();
    }
    /*
    bool CodeObject::Validate()
    {
      if (!img->Validate()) { return ElfImageError(); }
      if (img->Machine() != EM_AMDGPU) {
        out << "ELF error: Invalid machine" << std::endl;
        return false;
      }
      return true;
    }
    */

    bool CodeObject::GetNoteCodeObjectVersion(uint32_t* major, uint32_t* minor)
    {
        amdgpu_hsa_note_code_object_version_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_CODE_OBJECT_VERSION, &desc)) {
            return false;
        }
        *major = desc->major_version;
        *minor = desc->minor_version;
        return true;
    }

    bool CodeObject::GetNoteCodeObjectVersion(std::string& version)
    {
        amdgpu_hsa_note_code_object_version_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_CODE_OBJECT_VERSION, &desc)) {
            return false;
        }
        version.clear();
        version += std::to_string(desc->major_version);
        version += ".";
        version += std::to_string(desc->minor_version);
        return true;
    }
#if 0
    bool CodeObject::GetNoteHsail(uint32_t* hsail_major, uint32_t* hsail_minor, profile_t* profile, hsa_machine_model_t* machine_model, hsa_default_float_rounding_mode_t* default_float_round)
    {
        amdgpu_hsa_note_hsail_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_HSAIL, &desc)) {
            return false;
        }
        *hsail_major = desc->hsail_major_version;
        *hsail_minor = desc->hsail_minor_version;
        *profile = (profile_t)desc->profile;
        *machine_model = (hsa_machine_model_t)desc->machine_model;
        *default_float_round = (hsa_default_float_rounding_mode_t)desc->default_float_round;
        return true;
    }
    bool CodeObject::GetNoteIsa(std::string& vendor_name, std::string& architecture_name, uint32_t* major_version, uint32_t* minor_version, uint32_t* stepping)
    {
        amdgpu_hsa_note_isa_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_ISA, &desc)) {
            return false;
        }
        vendor_name = GetNoteString(desc->vendor_name_size, desc->vendor_and_architecture_name);
        architecture_name = GetNoteString(desc->architecture_name_size, desc->vendor_and_architecture_name + vendor_name.length() + 1);
        *major_version = desc->major;
        *minor_version = desc->minor;
        *stepping = desc->stepping;
        return true;
    }


    bool CodeObject::GetNoteIsa(std::string& isaName)
    {
        std::string vendor_name, architecture_name;
        uint32_t major_version, minor_version, stepping;
        if (!GetNoteIsa(vendor_name, architecture_name, &major_version, &minor_version, &stepping)) {
            return false;
        }
        isaName.clear();
        /*
      isaName += vendor_name;
      isaName += ":";
      isaName += architecture_name;
      isaName += ":";
      isaName += std::to_string(major_version);
      isaName += ":";
      isaName += std::to_string(minor_version);
      isaName += ":";
      isaName += std::to_string(stepping);
      */
        // TODO AMD:AMDGPU:7:0:0 -> ix:hcs:1.0.0
        isaName = "bi-ix-hcs--dla100";
        return true;
    }

    bool CodeObject::GetNoteProducer(uint32_t* major, uint32_t* minor, std::string& producer_name)
    {
        amdgpu_hsa_note_producer_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_PRODUCER, &desc)) {
            return false;
        }
        *major = desc->producer_major_version;
        *minor = desc->producer_minor_version;
        producer_name = GetNoteString(desc->producer_name_size, desc->producer_name);
        return true;
    }

    bool CodeObject::GetNoteProducerOptions(std::string& options)
    {
        amdgpu_hsa_note_producer_options_t* desc;
        if (!GetHcsNote(NT_AMDGPU_HSA_PRODUCER_OPTIONS, &desc)) {
            return false;
        }
        options = GetNoteString(desc->producer_options_size, desc->producer_options);
        return true;
    }
    status_t CodeObject::GetInfo(hsa_code_object_info_t attribute, void* value)
    {
        assert(value);
        switch (attribute) {
        case HSA_CODE_OBJECT_INFO_VERSION: {
            std::string version;
            if (!GetNoteCodeObjectVersion(version)) {
                return ERROR_INVALID_CODE_OBJECT;
            }
            char* svalue = (char*)value;
            memset(svalue, 0x0, 64);
            memcpy(svalue, version.c_str(), (std::min)(size_t(63), version.length()));
            break;
        }
        case HSA_CODE_OBJECT_INFO_ISA: {
            // TODO: Currently returns string representation instead of hsa_isa_t
            // which is unavailable here.
            std::string isa;
            if (!GetNoteIsa(isa)) {
                return ERROR_INVALID_CODE_OBJECT;
            }
            char* svalue = (char*)value;
            memset(svalue, 0x0, 64);
            memcpy(svalue, isa.c_str(), (std::min)(size_t(63), isa.length()));
            break;
        }
        case HSA_CODE_OBJECT_INFO_MACHINE_MODEL:
        case HSA_CODE_OBJECT_INFO_PROFILE:
        case HSA_CODE_OBJECT_INFO_DEFAULT_FLOAT_ROUNDING_MODE: {
            uint32_t hsail_major, hsail_minor;
            profile_t profile;
            hsa_machine_model_t machine_model;
            hsa_default_float_rounding_mode_t default_float_round;
            if (!GetNoteHsail(&hsail_major, &hsail_minor, &profile, &machine_model, &default_float_round)) {
                return ERROR_INVALID_CODE_OBJECT;
            }
            switch (attribute) {
            case HSA_CODE_OBJECT_INFO_MACHINE_MODEL:
                *((hsa_machine_model_t*)value) = machine_model;
                break;
            case HSA_CODE_OBJECT_INFO_PROFILE:
                *((profile_t*)value) = profile;
                break;
            case HSA_CODE_OBJECT_INFO_DEFAULT_FLOAT_ROUNDING_MODE:
                *((hsa_default_float_rounding_mode_t*)value) = default_float_round;
                break;
            default:
                break;
            }
            break;
        }
        default:
            assert(false);
            return ERROR_INVALID_ARGUMENT;
        }
        return SUCCESS;
    }
#endif

    status_t CodeObject::GetSymbol(const char* module_name, const char* symbol_name, hsa_code_symbol_t* s)
    {
        std::string mname = MangleSymbolName(module_name ? module_name : "", symbol_name);
        if (isV3) {
            for (SymbolV3* sym : symbolsV3) {
                if (sym->Name() == mname) {
                    *s = SymbolV3::ToHandle(sym);
                    return SUCCESS;
                }
            }
        } else {
            /*
            for (Symbol* sym : symbols) {
                if (sym->Name() == mname) {
                    *s = Symbol::ToHandle(sym);
                    return SUCCESS;
                }
            }
            */
        }
        return ERROR_INVALID_SYMBOL_NAME;
    }

    status_t CodeObject::IterateSymbols(hsa_code_object_t code_object,
        status_t (*callback)(
            hsa_code_object_t code_object,
            hsa_code_symbol_t symbol,
            void* data),
        void* data)
    {
#if 0
        for (Symbol* sym : symbols) {
            hsa_code_symbol_t s = Symbol::ToHandle(sym);
            status_t status = callback(code_object, s, data);
            if (status != SUCCESS) {
                return status;
            }
        }
#endif
        return SUCCESS;
    }

    SymbolV3* CodeObject::GetSymbolByElfIndex(size_t index)
    {
        for (auto& s : symbolsV3) {
            if (s && index == s->Index()) {
                return s;
            }
        }
        return nullptr;
    }

    SymbolV3* CodeObject::FindSymbol(const std::string& n)
    {
        for (auto& s : symbolsV3) {
            if (s && n == s->Name()) {
                return s;
            }
        }
        return nullptr;
    }

    bool CodeObject::PrintToFile(const std::string& filename)
    {
        std::ofstream out(filename);
        if (out.fail()) {
            return false;
        }
        Print(out);
        return out.fail();
    }

    void CodeObject::Print(std::ostream& out)
    {
        PrintNotes(out);
        out << std::endl;
        PrintSegments(out);
        out << std::endl;
        PrintSections(out);
        out << std::endl;
        PrintSymbols(out);
        out << std::endl;
        PrintMachineCode(out);
        out << std::endl;
        out << "AMD HSA Code Object End" << std::endl;
    }

    void CodeObject::PrintNotes(std::ostream& out)
    {
#if 0
        {
            uint32_t major_version, minor_version;
            if (GetNoteCodeObjectVersion(&major_version, &minor_version)) {
                out << "AMD HSA Code Object" << std::endl
                    << "  Version " << major_version << "." << minor_version << std::endl;
            }
        }
        {
            uint32_t hsail_major, hsail_minor;
            profile_t profile;
            hsa_machine_model_t machine_model;
            hsa_default_float_rounding_mode_t rounding_mode;
            if (GetNoteHsail(&hsail_major, &hsail_minor, &profile, &machine_model, &rounding_mode)) {
                out << "HSAIL " << std::endl
                    << "  Version: " << hsail_major << "." << hsail_minor << std::endl
                    << "  Profile: " << HsaProfileToString(profile)
                    << "  Machine model: " << HsaMachineModelToString(machine_model)
                    << "  Default float rounding: " << HsaFloatRoundingModeToString(rounding_mode) << std::endl;
            }
        }
        {
            std::string vendor_name, architecture_name;
            uint32_t major_version, minor_version, stepping;
            if (GetNoteIsa(vendor_name, architecture_name, &major_version, &minor_version, &stepping)) {
                out << "ISA" << std::endl
                    << "  Vendor " << vendor_name
                    << "  Arch " << architecture_name
                    << "  Version " << major_version << ":" << minor_version << ":" << stepping << std::endl;
            }
        }
        {
            std::string producer_name, producer_options;
            uint32_t major, minor;
            if (GetNoteProducer(&major, &minor, producer_name)) {
                out << "Producer '" << producer_name << "' "
                    << "Version " << major << ":" << minor << std::endl;
            }
        }
        {
            std::string producer_options;
            if (GetNoteProducerOptions(producer_options)) {
                out << "Producer options" << std::endl
                    << "  '" << producer_options << "'" << std::endl;
            }
        }
#endif
    }

    void CodeObject::PrintSegments(std::ostream& out)
    {
        out << "Segments (total " << DataSegmentCount() << "):" << std::endl;
        for (size_t i = 0; i < DataSegmentCount(); ++i) {
            if (isV3) {
                PrintSegment(out, DataSegmentV3(i));
            } else {
                // PrintSegment(out, DataSegment(i));
            }
        }
    }

    void CodeObject::PrintSections(std::ostream& out)
    {
        out << "Data Sections (total " << DataSectionCount() << "):" << std::endl;
        for (size_t i = 0; i < DataSectionCount(); ++i) {
            if (isV3) {
                PrintSection(out, DataSectionV3(i));
            } else {
                // PrintSection(out, DataSection(i));
            }
        }
        out << std::endl;
        out << "Relocation Sections (total " << RelocationSectionCount() << "):" << std::endl;
        for (size_t i = 0; i < RelocationSectionCount(); ++i) {
            if (isV3) {
                PrintSection(out, GetRelocationSectionV3(i));
            } else {
                // PrintSection(out, GetRelocationSection(i));
            }
        }
    }

    void CodeObject::PrintSymbols(std::ostream& out)
    {
        out << "Symbols (total " << SymbolCount() << "):" << std::endl;
        for (size_t i = 0; i < SymbolCount(); ++i) {
                PrintSymbol(out, GetSymbolV3(i));
        }
    }

    void CodeObject::PrintMachineCode(std::ostream& out)
    {
        // if (HasHsaText()) {
            out << std::dec;
            for (size_t i = 0; i < SymbolCount(); ++i) {
                if (isV3) {
                    SymbolV3* sym = GetSymbolV3(i);
                    if (sym->IsKernelSymbol() && sym->IsDefinition()) {
                        // FIXME, print out kmeta
                        out << "AMD Kernel Code for " << sym->Name() << ": " << std::endl;
                    }
                }
            }
        // }
    }
#if 0
    void CodeObject::PrintSegment(std::ostream& out, Segment* segment)
    {
        out << "  Segment (" << segment->getSegmentIndex() << ")" << std::endl;
        out << "    Type: " << AmdPTLoadToString(segment->type())
            << " "
            << "    Flags: "
            << "0x" << std::hex << std::setw(8) << std::setfill('0') << segment->flags() << std::dec
            << std::endl
            << "    Image Size: " << segment->imageSize()
            << " "
            << "    Memory Size: " << segment->memSize()
            << " "
            << "    Align: " << segment->align()
            << " "
            << "    VAddr: " << segment->vaddr()
            << std::endl;
        out << std::dec;
    }
#endif
    // TODO V3
    void CodeObject::PrintSegment(std::ostream& out, ELFIO::segment* segment)
    {
        out << "  Segment (" << segment->get_index() << ")" << std::endl;
        out << "    Type: " << AmdPTLoadToString(segment->get_type())
            << " "
            << "    Flags: "
            << "0x" << std::hex << std::setw(8) << std::setfill('0') << segment->get_flags() << std::dec
            << std::endl
            << "    Image Size: " << segment->get_file_size()
            << " "
            << "    Memory Size: " << segment->get_memory_size()
            << " "
            << "    Align: " << segment->get_align()
            << " "
            << "    VAddr: " << segment->get_virtual_address()
            << std::endl;
        out << std::dec;
    }
#if 0
    void CodeObject::PrintSection(std::ostream& out, Section* section)
    {
        out << "  Section " << section->Name() << " (Index " << section->getSectionIndex() << ")" << std::endl;
        out << "    Type: " << section->type()
            << " "
            << "    Flags: "
            << "0x" << std::hex << std::setw(8) << std::setfill('0') << section->flags() << std::dec
            << std::endl
            << "    Size:  " << section->size()
            << " "
            << "    Address: " << section->addr()
            << " "
            << "    Align: " << section->addralign()
            << std::endl;
        out << std::dec;

        if (section->flags() & SHF_AMDGPU_HSA_CODE) {
            // Printed separately.
            return;
        }

        switch (section->type()) {
        case SHT_NOBITS:
            return;
        case SHT_RELA:
            PrintRelocationData(out, section->asRelocationSection());
            return;
        default:
            PrintRawData(out, section);
        }
    }
#endif
    // TODO V3
    void CodeObject::PrintSection(std::ostream& out, ELFIO::section* section)
    {
        out << "  Section " << section->get_name() << " (Index " << section->get_index() << ")" << std::endl;
        out << "    Type: " << section->get_type()
            << " "
            << "    Flags: "
            << "0x" << std::hex << std::setw(8) << std::setfill('0') << section->get_flags() << std::dec
            << std::endl
            << "    Size:  " << section->get_size()
            << " "
            << "    Address: " << section->get_address()
            << " "
            << "    Align: " << section->get_addr_align()
            << std::endl;
        out << std::dec;

        if (section->get_flags() & SHF_AMDGPU_HSA_CODE) {
            // Printed separately.
            return;
        }

        switch (section->get_type()) {
        case SHT_NOBITS:
            return;
        case SHT_RELA:
            // PrintRelocationData(out, section->asRelocationSection());
            // TODO V3
            PrintRelocationData(out, section);
            return;
        default:
            PrintRawData(out, section);
        }
    }
#if 0
    void CodeObject::PrintRawData(std::ostream& out, Section* section)
    {
        out << "    Data:" << std::endl;
        unsigned char* sdata = (unsigned char*)alloca(section->size());
        section->getData(0, sdata, section->size());
        PrintRawData(out, sdata, section->size());
    }
#endif
    void CodeObject::PrintRawData(std::ostream& out, ELFIO::section* section)
    {
        out << "    Data:" << std::endl;
        const unsigned char* sdata; //  = (unsigned char*)alloca(section->get_size());
        sdata = (const unsigned char*)section->get_data();
        PrintRawData(out, sdata, section->get_size());
    }

    void CodeObject::PrintRawData(std::ostream& out, const unsigned char* data, size_t size)
    {
        out << std::hex << std::setfill('0');
        for (size_t i = 0; i < size; i += 16) {
            out << "      " << std::setw(7) << i << ":";

            for (size_t j = 0; j < 16; j += 1) {
                uint32_t value = i + j < size ? (uint32_t)data[i + j] : 0;
                if (j % 2 == 0) {
                    out << ' ';
                }
                out << std::setw(2) << value;
            }
            out << "  ";

            for (size_t j = 0; i + j < size && j < 16; j += 1) {
                char value = (char)data[i + j] >= 32 && (char)data[i + j] <= 126 ? (char)data[i + j] : '.';
                out << value;
            }
            out << std::endl;
        }
        out << std::dec;
    }


    void CodeObject::PrintRelocationData(std::ostream& out, ELFIO::section* section)
    {
        ELFIO::relocation_section_accessor relo(elf_reader, section);
        /*FIXME V3 
        if (section->targetSection()) {
            out << "    Relocation Entries for " << section->targetSection()->Name() << " Section (total " << section->relocationCount() << "):" << std::endl;
        } else {
            // Dynamic relocations do not have a target section, they work with
            // virtual addresses.
            out << "    Dynamic Relocation Entries (total " << section->relocationCount() << "):" << std::endl;
        }
        for (size_t i = 0; i < relo->get_entries_num(); ++i) {
            out << "      Relocation (Index " << i << "):" << std::endl;
            out << "        Type: " << relo->get_entry(i)->get_type() << std::endl;
            out << "        Symbol: " << relo->get_entry(i)->symbol()->name() << std::endl;
            out << "        Offset: " << relo->get_entry(i)->offset() << " Addend: " << section->relocation(i)->addend() << std::endl;
        }
        out << std::dec;
        */
    }

    // TODO V3
    void CodeObject::PrintSymbol(std::ostream& out, SymbolV3* sym)
    {
        out << "  Symbol " << sym->Name() << " (Index " << sym->Index() << "):" << std::endl;
        if (sym->IsKernelSymbol() || sym->IsVariableSymbol()) {
            out << "    Section: " << sym->GetSection()->get_name() << " ";
            out << "    Section Offset: " << sym->SectionOffset() << std::endl;
            out << "    VAddr: " << sym->VAddr() << " ";
            out << "    Size: " << sym->Size() << " ";
            out << "    Alignment: " << sym->Alignment() << std::endl;
            out << "    Kind: " << HsaSymbolKindToString(sym->Kind()) << " ";
            out << "    Linkage: " << HsaSymbolLinkageToString(sym->Linkage()) << " ";
            out << "    Definition: " << (sym->IsDefinition() ? "TRUE" : "FALSE") << std::endl;
        }
        if (sym->IsVariableSymbol()) {
            out << "    Allocation: " << HsaVariableAllocationToString(sym->Allocation()) << " ";
            out << "    Segment: " << HsaVariableSegmentToString(sym->Segment()) << " ";
            out << "    Constant: " << (sym->IsConst() ? "TRUE" : "FALSE") << std::endl;
        }
        out << std::dec;
    }
#if 0
    void CodeObject::PrintMachineCode(std::ostream& out, KernelSymbol* sym)
    {
        assert(HsaText());
        kernel_code_t kernel_code;
        HsaText()->getData(sym->SectionOffset(), &kernel_code, sizeof(kernel_code_t));

        out << "HCS Kernel Code for " << sym->Name() << ": " << std::endl
            << std::dec;
        PrintAmdKernelCode(out, &kernel_code);
        out << std::endl;

        std::vector<uint8_t> isa(HsaText()->size(), 0);
        HsaText()->getData(0, isa.data(), HsaText()->size());
        uint64_t isa_offset = sym->SectionOffset() + kernel_code.kernel_code_entry_byte_offset;

        out << "Disassembly for " << sym->Name() << ": " << std::endl;
        PrintDisassembly(out, isa.data(), HsaText()->size(), isa_offset);
        out << std::endl
            << std::dec;
    }
#endif
    void CodeObject::PrintDisassembly(std::ostream& out, const unsigned char* isa, size_t size, uint32_t isa_offset)
    {
        PrintRawData(out, isa, size);
        out << std::dec;
    }

    std::string CodeObject::MangleSymbolName(const std::string& module_name, const std::string symbol_name)
    {
        if (module_name.empty()) {
            return symbol_name;
        } else {
            return module_name + "::" + symbol_name;
        }
    }

    bool CodeObject::ElfImageError()
    {
        out << img->output();
        return false;
    }

#if 0
    KernelSymbolV2::KernelSymbolV2(elf::Symbol* elfsym_, const kernel_code_t* akc)
        : KernelSymbol(elfsym_, akc)
    {
    }
#endif
    bool SymbolV3::IsDeclaration() const
    {
        return elfsym.type == STT_COMMON;
    }

    bool SymbolV3::IsDefinition() const
    {
        return !IsDeclaration();
    }

    bool SymbolV3::IsAgent() const
    {
        return section->get_flags() & SHF_AMDGPU_HSA_AGENT ? true : false;
    }

    hsa_symbol_linkage_t SymbolV3::Linkage() const
    {
        return elfsym.bind == STB_GLOBAL ? HSA_SYMBOL_LINKAGE_PROGRAM : HSA_SYMBOL_LINKAGE_MODULE;
    }

    hsa_variable_allocation_t SymbolV3::Allocation() const
    {
        return IsAgent() ? HSA_VARIABLE_ALLOCATION_AGENT : HSA_VARIABLE_ALLOCATION_PROGRAM;
    }

    hsa_variable_segment_t SymbolV3::Segment() const
    {
        return section->get_flags() & SHF_AMDGPU_HSA_READONLY ? HSA_VARIABLE_SEGMENT_READONLY : HSA_VARIABLE_SEGMENT_GLOBAL;
    }

    uint64_t SymbolV3::Size() const
    {
        return elfsym.size;
    }

    uint32_t SymbolV3::Size32() const
    {
        assert(elfsym.size < UINT32_MAX);
        return (uint32_t)Size();
    }

    uint32_t SymbolV3::Alignment() const
    {
        assert(section->get_addr_align() < UINT32_MAX);
        return uint32_t(section->get_addr_align());
    }

    bool SymbolV3::IsConst() const
    {
        return section->get_flags() & SHF_WRITE ? true : false;
    }

    status_t SymbolV3::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);

        switch (attribute) {
        case HSA_CODE_SYMBOL_INFO_TYPE: {
            *((hsa_symbol_kind_t*)value) = Kind();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_NAME_LENGTH: {
            *((uint32_t*)value) = GetSymbolName().size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_NAME: {
            std::string SymbolName = GetSymbolName();
            memset(value, 0x0, SymbolName.size());
            memcpy(value, SymbolName.c_str(), SymbolName.size());
            break;
        }
        case HSA_CODE_SYMBOL_INFO_MODULE_NAME_LENGTH: {
            *((uint32_t*)value) = GetModuleName().size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_MODULE_NAME: {
            std::string ModuleName = GetModuleName();
            memset(value, 0x0, ModuleName.size());
            memcpy(value, ModuleName.c_str(), ModuleName.size());
            break;
        }
        case HSA_CODE_SYMBOL_INFO_LINKAGE: {
            *((hsa_symbol_linkage_t*)value) = Linkage();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_IS_DEFINITION: {
            *((bool*)value) = IsDefinition();
            break;
        }
        default: {
            return ERROR_INVALID_ARGUMENT;
        }
        }
        return SUCCESS;
    }

    std::string SymbolV3::GetModuleName() const
    {
        std::string FullName = Name();
        return FullName.rfind(":") != std::string::npos ? FullName.substr(0, FullName.find(":")) : "";
    }

    std::string SymbolV3::GetSymbolName() const
    {
        std::string FullName = Name();
        return FullName.rfind(":") != std::string::npos ? FullName.substr(FullName.rfind(":") + 1) : FullName;
    }

    hsa_code_symbol_t SymbolV3::ToHandle(SymbolV3* sym)
    {
        hsa_code_symbol_t s;
        s.handle = reinterpret_cast<uint64_t>(sym);
        return s;
    }

    SymbolV3* SymbolV3::FromHandle(hsa_code_symbol_t s)
    {
        return reinterpret_cast<SymbolV3*>(s.handle);
    }

    // KernelSymbolV3::KernelSymbolV3(std::unique_ptr<hip_impl::Symbol> elfsym_, const ELFIO::elfio& elf_reader, KernelMeta* kmeta)
    KernelSymbolV3::KernelSymbolV3(hip_impl::Symbol elfsym_, const ELFIO::elfio& elf_reader, KernelMeta* kmeta)
        : SymbolV3(std::move(elfsym_), elf_reader)
        , kernarg_segment_size(0)
        , kernarg_segment_alignment(0)
        , group_segment_size(0)
        , private_segment_size(0)
        , is_dynamic_callstack(0)
        , kernel_ctrl(0)
        , kernel_mode(0)
    {
        if (kmeta) {
            kernarg_segment_size = (uint32_t)kmeta->kernarg_segment_size;
            kernarg_segment_alignment = (uint32_t)(1 << kmeta->kernarg_segment_align);
            group_segment_size = uint32_t(kmeta->group_segment_fixed_size);
            private_segment_size = uint32_t(kmeta->private_segment_fixed_size);
            kernel_ctrl = uint32_t(kmeta->kernel_ctrl);
            kernel_mode = uint32_t(kmeta->kernel_mode);
            // is_dynamic_callstack = HCS_BITS_GET(akc->kernel_code_properties, AMD_KERNEL_CODE_PROPERTIES_IS_DYNAMIC_CALLSTACK) ? true : false;
        }
    }

    status_t KernelSymbolV3::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);
        switch (attribute) {
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
        case HSA_CODE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK: {
            *((bool*)value) = is_dynamic_callstack;
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
        default: {
            return SymbolV3::GetInfo(attribute, value);
        }
        }
        return SUCCESS;
    }

    status_t VariableSymbolV3::GetInfo(hsa_code_symbol_info_t attribute, void* value)
    {
        assert(value);
        switch (attribute) {
        case HSA_CODE_SYMBOL_INFO_VARIABLE_ALLOCATION: {
            *((hsa_variable_allocation_t*)value) = Allocation();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_SEGMENT: {
            *((hsa_variable_segment_t*)value) = Segment();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_ALIGNMENT: {
            *((uint32_t*)value) = Alignment();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_SIZE: {
            *((uint32_t*)value) = Size();
            break;
        }
        case HSA_CODE_SYMBOL_INFO_VARIABLE_IS_CONST: {
            *((bool*)value) = IsConst();
            break;
        }
        default: {
            return SymbolV3::GetInfo(attribute, value);
        }
        }
        return SUCCESS;
    }

    using namespace std;
    // void CodeObject::GetNoteCodeObjectMeta(const std::string &note_content)
    void CodeObject::GetNoteCodeObjectMeta(char* note_content, size_t size)
    {
        // std::istringstream ss(note_content);
        // msgpack::object_handle oh = msgpack::unpack(ss.str().data(), ss.str().size());
        msgpack::object_handle oh = msgpack::unpack(note_content, size);
        msgpack::object obj = oh.get();

        using MapType = std::unordered_map<std::string, msgpack::object>;
        using ArrType = std::vector<msgpack::object>;
/*
        MapType meta_root = obj.as<MapType>();
        for (auto& m : meta_root) {
            std::cout << "the Kernel Meta Dump:" << std::endl;
            std::cout << m.first << "->" << m.second << std::endl;
            if (m.first.rfind("PPU.Kernels") == std::string::npos) {
                assert("Can't find PPU.Kernels key in kernel meta note");
            }
            ArrType kernels = m.second.as<ArrType>();
            */
            ArrType kernels = obj.as<ArrType>();
            std::cout << "the kernel size is " << kernels.size() << std::endl;
            for (auto& k : kernels) {
                MapType km_map = k.as<MapType>();
                std::string kernel_name = "";

                KernelMeta meta;
                auto parse_km = [&meta](const std::string& meta_name, msgpack::object& o) {
                    std::cout << meta_name << std::endl;
                    switch (hash_(meta_name.c_str())) {
                    // case ".name"_hash:
                    case hash_compile_time(".name"):
                        meta.name = o.as<std::string>();
                        break;
                    case hash_compile_time(".symbol"):
                        meta.symbol = o.as<std::string>();
                        break;
                    case hash_compile_time(".max_flat_workgroup_size"):
                        meta.max_flat_workgroup_size = o.as<int>();
                        break;
                    case hash_compile_time(".kernarg_segment_size"):
                        meta.kernarg_segment_size = o.as<int>();
                        break;
                    case hash_compile_time(".private_segment_fixed_size"):
                        meta.private_segment_fixed_size = o.as<int>();
                        break;
                    case hash_compile_time(".wavefront_size"):
                        meta.wavefront_size = o.as<int>();
                        break;
                    case hash_compile_time(".language"):
                        meta.language = o.as<std::string>();
                        break;
                    case hash_compile_time(".kernarg_segment_align"):
                        meta.kernarg_segment_align = o.as<int>();
                        break;
                    case hash_compile_time(".group_segment_fixed_size"):
                        meta.group_segment_fixed_size = o.as<int>();
                        break;
                    case hash_compile_time(".kernel_ctrl"):
                        meta.kernel_ctrl = o.as<int>();
                        break;
                    case hash_compile_time(".kernel_mode"):
                        meta.kernel_mode = o.as<int>();
                        break;
                    default:
                        break;
                    }
                };
                for (auto& km : km_map) {
                    if (km.first.rfind(".name") != std::string::npos) {
                        kernel_name = km.second.as<std::string>();
                    }
                    parse_km(km.first, km.second);
                }

                std::cout << "find meta for kernel " << kernel_name << std::endl;
                m_kernelMeta.insert(std::make_pair(kernel_name, meta));
            }
        //}
    }

    bool CodeObject::PullElfV3()
    {
        std::istringstream elf_buf(std::string(ElfData(), ElfSize()));
        // if (elf_reader.load(ElfData()) == false) {
        if (elf_reader.load(elf_buf) == false) {
            return false;
        }
        for (const auto& seg : elf_reader.segments) {
            if (seg->get_type() == PT_LOAD) {
                dataSegmentsV3.push_back(seg);
                auto vaddr = seg->get_virtual_address(); // TODO verify use get_address is right
                auto mem_size = seg->get_memory_size(); // TODO verify use get_address is right
                std::cout << "vaddr:" << std::hex << vaddr << "mem_size" << mem_size << std::endl;
            }
        }

        for (const auto& sec : elf_reader.sections) {
            if ((sec->get_type() == SHT_PROGBITS || sec->get_type() == SHT_NOBITS) && !(sec->get_flags() & SHF_EXECINSTR)) {
                dataSectionsV3.push_back(sec);
            } else if (sec->get_type() == SHT_RELA) {
                // sh_info is target section, sh_link is symtab
                /*
                auto link = sec->get_link();
                auto info = sec->get_info();
                std::cout << "relo section link " << link << " info: " << info << std::endl;
                */
                relocationSectionsV3.push_back(sec);
            }
            if (sec->get_name() == ".text") {
                hsatextV3 = sec;
            }
        }

        // TODO debug
        // Print(out);

        // use functon from program_state.hpp
        auto note_section = hip_impl::find_section_if(elf_reader, [](const ELFIO::section* x) {
            return x->get_type() == SHT_NOTE;
        });

        if (!note_section) return false;
        ELFIO::note_section_accessor notes(elf_reader, note_section);
        ELFIO::Elf_Word number_notes;
        ELFIO::Elf_Word note_index;
        ELFIO::Elf_Word note_type;
        std::string note_name;
        void* note_content;
        ELFIO::Elf_Word note_size;
        number_notes = notes.get_notes_num();
        for (note_index = 0; note_index < number_notes; ++note_index) {
            notes.get_note(note_index, note_type, note_name, note_content, note_size);
            // TODO use macro instead of 10
            if (note_type == 0x20) {
                GetNoteCodeObjectMeta(static_cast<char*>(note_content), (size_t)note_size);
            }
        }

        auto symbol_section = hip_impl::find_section_if(elf_reader, [](const ELFIO::section* x) {
            return x->get_type() == SHT_SYMTAB;
        });

        if (!symbol_section) return false;
        ELFIO::symbol_section_accessor symtab(elf_reader, symbol_section);

        // build up all function name symbol map
        std::map<std::string, hip_impl::Symbol> function_symbol;
        for (auto i = 0u; i != symtab.get_symbols_num(); ++i) {
            auto tmp = hip_impl::read_symbol(symtab, i);
            if (tmp.type == STT_FUNC && tmp.sect_idx != SHN_UNDEF && !tmp.name.empty()) {
                function_symbol.insert(std::make_pair(tmp.name, tmp));
            }
        }

        for (auto i = 0u; i != symtab.get_symbols_num(); ++i) {
            hip_impl::Symbol s = hip_impl::read_symbol(symtab, i);

            SymbolV3* sym { nullptr };
            // auto symbol = std::make_unique<hip_impl::Symbol>(s);
            std::shared_ptr<KernelMeta> pMeta = findKernelBySymbol(s.name);

            switch (s.type) {
            case STT_LOOS: // TODO schi to make clean what STT_TYPE is required: ex STT_PPU_KERNEL
            {
                // Variable symbol
                if (pMeta) {
                    // kernel_code_t akc;
                    ELFIO::section* sec = elf_reader.sections[s.sect_idx];
                    if (!sec) {
                        std::cout << "Failed to find section for symbol " << s.name << std::endl;
                        return false;
                    }
                    if (!(sec->get_flags() & (SHF_ALLOC | SHF_EXECINSTR))) {
                        std::cout << "Invalid code section for symbol " << s.name << std::endl;
                        return false;
                    }
                    /*
                    uint64_t offset = s.value - sec->get_address();
                    memcpy(&akc, sec->get_data() + offset, sizeof(akc));
                    */
                    if (function_symbol.find(pMeta->name) != function_symbol.end()) {
                        // sym = new KernelSymbolV3(std::make_unique<hip_impl::Symbol>(function_symbol[pMeta->name]), elf_reader, pMeta.get());
                        // sym = new KernelSymbolV3(function_symbol[pMeta->name], elf_reader, pMeta.get());
                        sym = new KernelSymbolV3(std::move(s), elf_reader, pMeta.get());
                    } else {
                        assert(1 && "Failed to find function symbol");
                    }
                }
                /*
                if (!(sec->get_data() + s.value - sec->get_address()), &akc, sizeof(kernel_code_t)))
                    {
                        out << "Failed to get PPU Kernel Code for symbol " << symbol_name << std::endl;
                        return false;
                    }
                */
                break;
            }
            case STT_OBJECT:
            case STT_COMMON:
                sym = new VariableSymbolV3(std::move(s), elf_reader);
                break;
            default:
                break; // Skip unknown symbols.
            }
            if (sym) {
                symbolsV3.push_back(sym);
            }
        }
        return true;
    }
}
