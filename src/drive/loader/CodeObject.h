#pragma once

// #include "inc/pps.h"
#include "ElfDefine.h"
#include "ElfImage.h"
// #include "inc/pps_ext_finalize.h"
// #include "inc/pps_kernel_code.h"
#include <cassert>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "elfio/elfio.hpp"
#include "loader/program_state.hpp"

namespace common {

template <uint64_t signature>
class Signed {
public:
    static const uint64_t CT_SIGNATURE;
    const uint64_t RT_SIGNATURE;

protected:
    Signed()
        : RT_SIGNATURE(signature)
    {
    }
    virtual ~Signed() { }
};

template <uint64_t signature>
const uint64_t Signed<signature>::CT_SIGNATURE = signature;

bool IsAccessibleMemoryAddress(uint64_t address);

template <typename class_type, typename member_type>
size_t OffsetOf(member_type class_type::*member)
{
    return (char*)&((class_type*)nullptr->*member) - (char*)nullptr;
}

template <typename class_type>
class_type* ObjectAt(uint64_t address)
{
    if (!IsAccessibleMemoryAddress(address)) {
        return nullptr;
    }

    const uint64_t* rt_signature = (const uint64_t*)(address + OffsetOf(&class_type::RT_SIGNATURE));
    if (nullptr == rt_signature) {
        return nullptr;
    }
    if (class_type::CT_SIGNATURE != *rt_signature) {
        return nullptr;
    }

    return (class_type*)address;
}

}

namespace code {

// typedef elf::Segment Segment;
// typedef elf::Section Section;
// typedef elf::RelocationSection RelocationSection;
// typedef elf::Relocation Relocation;

// class KernelSymbol;
// class VariableSymbol;

#if 0
class Symbol {
protected:
    elf::Symbol* elfsym;

public:
    explicit Symbol(elf::Symbol* elfsym_)
        : elfsym(elfsym_)
    {
    }
    virtual ~Symbol() { }
    virtual bool IsKernelSymbol() const { return false; }
    virtual KernelSymbol* AsKernelSymbol()
    {
        assert(false);
        return 0;
    }
    virtual bool IsVariableSymbol() const { return false; }
    virtual VariableSymbol* AsVariableSymbol()
    {
        assert(false);
        return 0;
    }
    elf::Symbol* elfSym() { return elfsym; }
    std::string Name() const { return elfsym ? elfsym->name() : ""; }
    Section* GetSection() { return elfsym->section(); }
    virtual uint64_t SectionOffset() const { return elfsym->value(); }
    virtual uint64_t VAddr() const { return elfsym->section()->addr() + elfsym->value(); }
    uint32_t Index() const { return elfsym ? elfsym->index() : 0; }
    bool IsDeclaration() const;
    bool IsDefinition() const;
    virtual bool IsAgent() const;
    virtual hsa_symbol_kind_t Kind() const = 0;
    hsa_symbol_linkage_t Linkage() const;
    hsa_variable_allocation_t Allocation() const;
    hsa_variable_segment_t Segment() const;
    uint64_t Size() const;
    uint32_t Size32() const;
    uint32_t Alignment() const;
    bool IsConst() const;
    virtual status_t GetInfo(hsa_code_symbol_info_t attribute, void* value);
    static hsa_code_symbol_t ToHandle(Symbol* sym);
    static Symbol* FromHandle(hsa_code_symbol_t handle);
    void setValue(uint64_t value) { elfsym->setValue(value); }
    void setSize(uint32_t size) { elfsym->setSize(size); }

    std::string GetModuleName() const;
    std::string GetSymbolName() const;
};
class KernelSymbol : public Symbol {
private:
    uint32_t kernarg_segment_size, kernarg_segment_alignment;
    uint32_t group_segment_size, private_segment_size;
    bool is_dynamic_callstack;

public:
    explicit KernelSymbol(elf::Symbol* elfsym_, const kernel_code_t* akc);
    bool IsKernelSymbol() const override { return true; }
    KernelSymbol* AsKernelSymbol() override { return this; }
    hsa_symbol_kind_t Kind() const override { return HSA_SYMBOL_KIND_KERNEL; }
    status_t GetInfo(hsa_code_symbol_info_t attribute, void* value) override;
};

class VariableSymbol : public Symbol {
public:
    explicit VariableSymbol(elf::Symbol* elfsym_)
        : Symbol(elfsym_)
    {
    }
    bool IsVariableSymbol() const override { return true; }
    VariableSymbol* AsVariableSymbol() override { return this; }
    hsa_symbol_kind_t Kind() const override { return HSA_SYMBOL_KIND_VARIABLE; }
    status_t GetInfo(hsa_code_symbol_info_t attribute, void* value) override;
};
#endif

class SymbolV3;

struct KernelMeta {
    std::string name;
    std::string symbol;
    int max_flat_workgroup_size;
    int kernarg_segment_size;
    int private_segment_fixed_size;
    int wavefront_size;
    std::string language; // OpenCL C
    int kernarg_segment_align;
    int group_segment_fixed_size;
    int kernel_ctrl { 0 };
    int kernel_mode { 0 };
};

class CodeObject {
protected:
    std::ostringstream out;
    std::unique_ptr<elf::Image> img;
    // std::vector<Segment*> dataSegments;
    // std::vector<Section*> dataSections;
    // std::vector<RelocationSection*> relocationSections;
    // std::vector<Symbol*> symbols;
    // bool combineDataSegments;
    // Segment* hsaSegments[AMDGPU_HSA_SEGMENT_LAST][2];
    // Section* hsaSections[AMDGPU_HSA_SECTION_LAST];

    // elf::Section* hsatext;
    // elf::Section* imageInit;
    // elf::Section* samplerInit;
    // elf::Section* debugInfo;
    // elf::Section* debugLine;
    // elf::Section* debugAbbrev;

    std::map<std::string, KernelMeta> m_kernelMeta;

    // bool PullElfV2();

    ELFIO::elfio elf_reader;
    std::vector<ELFIO::segment*> dataSegmentsV3;
    std::vector<ELFIO::section*> dataSectionsV3;
    std::vector<ELFIO::section*> relocationSectionsV3;
    ELFIO::section* hsatextV3;
    std::vector<SymbolV3*> symbolsV3;
    bool PullElfV3();

    std::shared_ptr<KernelMeta> findKernelBySymbol(const std::string& symbol)
    {
        for (auto& e : m_kernelMeta) {
            if (e.second.symbol == symbol) {
                return std::make_shared<decltype(e.second)>(e.second);
            }
        }
        return nullptr;
    }

    // void GetNoteCodeObjectMeta(const std::string &note_content);
    void GetNoteCodeObjectMeta(char* note_content, size_t size);

    // void AddHcsNote(uint32_t type, const void* desc, uint32_t desc_size);
    template <typename S>
    bool GetHcsNote(uint32_t type, S** desc)
    {
        uint32_t desc_size;
        if (!img->note()->getNote("HCS", type, (void**)desc, &desc_size)) {
            out << "Failed to find note, type: " << type << std::endl;
            return false;
        }
        if (desc_size < sizeof(S)) {
            out << "Note size mismatch, type: " << type << " size: " << desc_size << " expected at least " << sizeof(S) << std::endl;
            return false;
        }
        return true;
    }

    // void PrintSegment(std::ostream& out, Segment* segment);
    void PrintSegment(std::ostream& out, ELFIO::segment* segment);
    // void PrintSection(std::ostream& out, Section* section);
    void PrintSection(std::ostream& out, ELFIO::section* section);
    // void PrintRawData(std::ostream& out, Section* section);
    void PrintRawData(std::ostream& out, ELFIO::section* section);
    void PrintRawData(std::ostream& out, const unsigned char* data, size_t size);
    // void PrintRelocationData(std::ostream& out, RelocationSection* section);
    void PrintRelocationData(std::ostream& out, ELFIO::section* section);
    // void PrintSymbol(std::ostream& out, Symbol* sym);
    void PrintSymbol(std::ostream& out, SymbolV3* sym);
    void PrintDisassembly(std::ostream& out, const unsigned char* isa, size_t size, uint32_t isa_offset = 0);
    std::string MangleSymbolName(const std::string& module_name, const std::string symbol_name);
    bool ElfImageError();

public:
    bool PullElf();
    // bool HasHsaText() const { return hsatext != 0; }
    /*
    elf::Section* HsaText()
    {
        assert(hsatext);
        return hsatext;
    }
    const elf::Section* HsaText() const
    {
        assert(hsatext);
        return hsatext;
    }
    elf::SymbolTable* Symtab()
    {
        assert(img);
        return img->symtab();
    }
    uint16_t Machine() const { return img->Machine(); }
    uint32_t EFlags() const { return img->EFlags(); }
    */

    CodeObject(bool combineDataSegments = true);
    virtual ~CodeObject();

    std::string output() { return out.str(); }
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename);
    bool WriteToBuffer(void* buffer);
    // bool InitFromBuffer(const void* buffer, size_t size);
    bool InitAsBuffer(const void* buffer, size_t size);
    bool InitAsHandle(hsa_code_object_t code_handle);
    // hsa_code_object_t GetHandle();
    const char* ElfData();
    uint64_t ElfSize();
    // bool Validate();
    void Print(std::ostream& out);
    void PrintNotes(std::ostream& out);
    void PrintSegments(std::ostream& out);
    void PrintSections(std::ostream& out);
    void PrintSymbols(std::ostream& out);
    void PrintMachineCode(std::ostream& out);
    // void PrintMachineCode(std::ostream& out, KernelSymbol* sym);
    bool PrintToFile(const std::string& filename);

    bool GetNoteCodeObjectVersion(uint32_t* major, uint32_t* minor);
    bool GetNoteCodeObjectVersion(std::string& version);
    // bool GetNoteHsail(uint32_t* hsail_major, uint32_t* hsail_minor, profile_t* profile, hsa_machine_model_t* machine_model, hsa_default_float_rounding_mode_t* default_float_round);
    // bool GetNoteIsa(std::string& vendor_name, std::string& architecture_name, uint32_t* major_version, uint32_t* minor_version, uint32_t* stepping);
    // bool GetNoteIsa(std::string& isaName);
    // bool GetNoteProducer(uint32_t* major, uint32_t* minor, std::string& producer_name);
    // bool GetNoteProducerOptions(std::string& options);

    // status_t GetInfo(hsa_code_object_info_t attribute, void* value);
    status_t GetSymbol(const char* module_name, const char* symbol_name, hsa_code_symbol_t* sym);
    status_t IterateSymbols(hsa_code_object_t code_object,
        status_t (*callback)(
            hsa_code_object_t code_object,
            hsa_code_symbol_t symbol,
            void* data),
        void* data);

    uint64_t NextKernelCodeOffset() const;

    ELFIO::elfio& GetElfio()
    {
        return elf_reader;
    }

    size_t DataSegmentCount() const
    {
        // if (isV3)
            return dataSegmentsV3.size();
        // return dataSegments.size();
    }

    // Segment* DataSegment(size_t i) const { return dataSegments[i]; }
    ELFIO::segment* DataSegmentV3(size_t i) const { return dataSegmentsV3[i]; }

    size_t DataSectionCount()
    {
        // if (isV3)
            return dataSectionsV3.size();
        // return dataSections.size();
    }

    // Section* DataSection(size_t i) { return dataSections[i]; }
    ELFIO::section* DataSectionV3(size_t i) { return dataSectionsV3[i]; }

    // bool HasImageInitSection() const { return imageInit != 0; }

    // bool HasSamplerInitSection() const { return samplerInit != 0; }
    // Segment* HsaSegment(amdgpu_hsa_elf_segment_t segment, bool writable);

    // Section* HsaDataSection(amdgpu_hsa_elf_section_t section, bool combineSegments = true);

    size_t RelocationSectionCount()
    {
        // if (isV3)
            return relocationSectionsV3.size();
        // return relocationSections.size();
    }
    // RelocationSection* GetRelocationSection(size_t i) { return relocationSections[i]; }
    ELFIO::section* GetRelocationSectionV3(size_t i) { return relocationSectionsV3[i]; }

    size_t SymbolCount()
    {
        // if (isV3) {
            return symbolsV3.size();
        // } else {
            // return symbols.size();
        // }
    }
    /*
    Symbol* GetSymbol(size_t i)
    {
        return symbols[i];
    }
    */
    SymbolV3* GetSymbolV3(size_t i)
    {
        return symbolsV3[i];
    }

    SymbolV3* GetSymbolByElfIndex(size_t index);
    SymbolV3* FindSymbol(const std::string& n);

    // Section* DebugInfo();
    // Section* DebugLine();
    // Section* DebugAbbrev();
    bool isV3 { true };
};

class CodeObjectManager {
private:
    typedef std::unordered_map<uint64_t, CodeObject*> CodeMap;
    CodeMap codeMap;

public:
    CodeObject* FromHandle(hsa_code_object_t handle);
    bool Destroy(hsa_code_object_t handle);
};

#if 0
class KernelSymbolV2 : public KernelSymbol {
private:
public:
    explicit KernelSymbolV2(elf::Symbol* elfsym_, const kernel_code_t* akc);
    bool IsAgent() const override { return true; }
    uint64_t SectionOffset() const override { return elfsym->value() - elfsym->section()->addr(); }
    uint64_t VAddr() const override { return elfsym->value(); }
};

class VariableSymbolV2 : public VariableSymbol {
private:
public:
    explicit VariableSymbolV2(elf::Symbol* elfsym_)
        : VariableSymbol(elfsym_)
    {
    }
    bool IsAgent() const override { return false; }
    uint64_t SectionOffset() const override { return elfsym->value() - elfsym->section()->addr(); }
    uint64_t VAddr() const override { return elfsym->value(); }
};
#endif
/*
struct ElfSymbol {
    std::string name;
    ELFIO::Elf64_Addr value = 0;
    ELFIO::Elf_Xword size = 0;
    ELFIO::Elf_Half sect_idx = 0;
    uint8_t bind = 0;
    uint8_t type = 0;
    uint8_t other = 0;
};
*/

class KernelSymbolV3;
class VariableSymbolV3;

struct RelocationV3 {
    ELFIO::Elf_Xword index = 0;
    ELFIO::Elf64_Addr offset = 0;
    ELFIO::Elf_Word symbol = 0;
    ELFIO::Elf_Word type = 0;
    ELFIO::Elf_Sxword addend = 0;

    ELFIO::Elf_Xword size;
    ELFIO::Elf64_Addr symbolValue = 0;
    std::string symbolName;
    ELFIO::Elf_Sxword calcValue;
};

inline RelocationV3 read_relocation(const ELFIO::relocation_section_accessor& section,
    unsigned int idx)
{
    assert(idx < section.get_entries_num());

    RelocationV3 r;
    section.get_entry(
        idx, r.offset, r.symbol, r.type, r.addend);

    return r;
}

class SymbolV3 {
protected:
    // std::unique_ptr<hip_impl::Symbol> elfsym;
    hip_impl::Symbol elfsym;
    ELFIO::section* section;

public:
    // explicit SymbolV3(std::unique_ptr<hip_impl::Symbol> elfsym_, const ELFIO::elfio& elf_reader)
    explicit SymbolV3(hip_impl::Symbol elfsym_, const ELFIO::elfio& elf_reader)
        : elfsym(std::move(elfsym_))
    {
        section = elf_reader.sections[elfsym.sect_idx];
    }
    virtual ~SymbolV3() { }
    virtual bool IsKernelSymbol() const { return false; }
    virtual KernelSymbolV3* AsKernelSymbol()
    {
        assert(false);
        return 0;
    }
    virtual bool IsVariableSymbol() const { return false; }
    virtual VariableSymbolV3* AsVariableSymbol()
    {
        assert(false);
        return 0;
    }
    // hip_impl::Symbol* elfSym() { return elfsym->get(); }
    std::string Name() const { return elfsym.name; }
    ELFIO::section* GetSection() { return section; }
    virtual uint64_t SectionOffset() const { return elfsym.value; }
    virtual uint64_t VAddr() const { return section->get_address() + SectionOffset(); }
    // FIXEM is it section_idx?
    // uint32_t Index() const { return elfsym ? elfsym.sect_idx : 0; }
    uint32_t Index() const { return elfsym.sect_idx; }
    bool IsDeclaration() const;
    bool IsDefinition() const;
    virtual bool IsAgent() const;
    virtual hsa_symbol_kind_t Kind() const = 0;
    hsa_symbol_linkage_t Linkage() const;
    hsa_variable_allocation_t Allocation() const;
    hsa_variable_segment_t Segment() const;
    uint64_t Size() const;
    uint32_t Size32() const;
    uint32_t Alignment() const;
    bool IsConst() const;
    virtual status_t GetInfo(hsa_code_symbol_info_t attribute, void* value);
    static hsa_code_symbol_t ToHandle(SymbolV3* sym);
    static SymbolV3* FromHandle(hsa_code_symbol_t handle);
    void setValue(uint64_t value) { elfsym.value = value; }
    void setSize(uint32_t size) { elfsym.size = size; }

    std::string GetModuleName() const;
    std::string GetSymbolName() const;
};

class KernelSymbolV3 : public SymbolV3 {
public:
    uint32_t kernarg_segment_size, kernarg_segment_alignment;
    uint32_t group_segment_size, private_segment_size;
    uint32_t kernel_ctrl, kernel_mode;
    bool is_dynamic_callstack;

public:
    // explicit KernelSymbolV3(std::unique_ptr<hip_impl::Symbol> elfsym_, const ELFIO::elfio& elf_reader, KernelMeta* kmeta);
    explicit KernelSymbolV3(hip_impl::Symbol elfsym_, const ELFIO::elfio& elf_reader, KernelMeta* kmeta);
    bool IsKernelSymbol() const override { return true; }
    KernelSymbolV3* AsKernelSymbol() override { return this; }
    hsa_symbol_kind_t Kind() const override { return HSA_SYMBOL_KIND_KERNEL; }
    status_t GetInfo(hsa_code_symbol_info_t attribute, void* value) override;

    bool IsAgent() const override { return true; }
    /* FIXME
        uint64_t SectionOffset() const override { return elfsym->value; }
        uint64_t VAddr() const override { return section->get_address() + elfsym->value; }
        */
    uint64_t SectionOffset() const override { return elfsym.value - section->get_address(); }
    uint64_t VAddr() const override { return elfsym.value; }
};

class VariableSymbolV3 : public SymbolV3 {
public:
    // explicit VariableSymbolV3(std::unique_ptr<hip_impl::Symbol> elfsym_, const ELFIO::elfio& elf_reader)
    explicit VariableSymbolV3(hip_impl::Symbol elfsym_, const ELFIO::elfio& elf_reader)
        : SymbolV3(std::move(elfsym_), elf_reader)
    {
    }
    bool IsVariableSymbol() const override { return true; }
    VariableSymbolV3* AsVariableSymbol() override { return this; }
    hsa_symbol_kind_t Kind() const override { return HSA_SYMBOL_KIND_VARIABLE; }
    status_t GetInfo(hsa_code_symbol_info_t attribute, void* value) override;

    bool IsAgent() const override { return false; }
    /* FIXME
        uint64_t SectionOffset() const override { return elfsym->value; }
        uint64_t VAddr() const override { return section->get_address() + elfsym->value; }
        */

    uint64_t SectionOffset() const override { return elfsym.value - section->get_address(); }
    uint64_t VAddr() const override { return elfsym.value; }
};

}

