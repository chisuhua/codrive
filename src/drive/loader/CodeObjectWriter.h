#pragma once

#include "inc/ElfImage.h"
#include "inc/ElfDefine.h"
#include "inc/pps_kernel_code.h"
#include "inc/pps.h"
#include "inc/pps_ext_finalize.h"
#include "inc/CodeObject.h"
#include <memory>
#include <sstream>
#include <cassert>
#include <unordered_map>

namespace code {

    class CodeObjectWriter : public CodeObject {
    private:
      std::ostringstream out;

      void AddHcsNote(uint32_t type, const void* desc, uint32_t desc_size);

    public:
      bool HasHsaText() const { return hsatext != 0; }
      hcs::elf::Section* HsaText() { assert(hsatext); return hsatext; }
      const hcs::elf::Section* HsaText() const { assert(hsatext); return hsatext; }
      hcs::elf::SymbolTable* Symtab() { assert(img); return img->symtab(); }
      uint16_t Machine() const { return img->Machine(); }
      uint32_t EFlags() const { return img->EFlags(); }

      CodeObjectWriter(bool combineDataSegments = true);
      virtual ~CodeObjectWriter();

      std::string output() { return out.str(); }
      bool LoadFromFile(const std::string& filename);
      bool SaveToFile(const std::string& filename);
      bool WriteToBuffer(void* buffer);
      bool InitFromBuffer(const void* buffer, size_t size);
      bool InitAsBuffer(const void* buffer, size_t size);
      bool InitAsHandle(hsa_code_object_t code_handle);
      bool InitNew();
      bool Freeze();
      hsa_code_object_t GetHandle();
      const char* ElfData();
      uint64_t ElfSize();
      bool Validate();
      void Print(std::ostream& out);
      void PrintNotes(std::ostream& out);
      void PrintSegments(std::ostream& out);
      void PrintSections(std::ostream& out);
      void PrintSymbols(std::ostream& out);
      void PrintMachineCode(std::ostream& out);
      void PrintMachineCode(std::ostream& out, KernelSymbol* sym);
      bool PrintToFile(const std::string& filename);

      void AddNoteCodeObjectVersion(uint32_t major, uint32_t minor);
      bool GetNoteCodeObjectVersion(uint32_t* major, uint32_t* minor);
      bool GetNoteCodeObjectVersion(std::string& version);
      void AddNoteHsail(uint32_t hsail_major, uint32_t hsail_minor, profile_t profile, hsa_machine_model_t machine_model, hsa_default_float_rounding_mode_t rounding_mode);
      bool GetNoteHsail(uint32_t* hsail_major, uint32_t* hsail_minor, profile_t* profile, hsa_machine_model_t* machine_model, hsa_default_float_rounding_mode_t* default_float_round);
      void AddNoteIsa(const std::string& vendor_name, const std::string& architecture_name, uint32_t major, uint32_t minor, uint32_t stepping);
      bool GetNoteIsa(std::string& vendor_name, std::string& architecture_name, uint32_t* major_version, uint32_t* minor_version, uint32_t* stepping);
      bool GetNoteIsa(std::string& isaName);
      void AddNoteProducer(uint32_t major, uint32_t minor, const std::string& producer);
      bool GetNoteProducer(uint32_t* major, uint32_t* minor, std::string& producer_name);
      void AddNoteProducerOptions(const std::string& options);
      void AddNoteProducerOptions(int32_t call_convention, const hsa_ext_control_directives_t& user_directives, const std::string& user_options);
      bool GetNoteProducerOptions(std::string& options);

      status_t GetInfo(hsa_code_object_info_t attribute, void *value);
      status_t GetSymbol(const char *module_name, const char *symbol_name, hsa_code_symbol_t *sym);
      status_t IterateSymbols(hsa_code_object_t code_object,
                                  status_t (*callback)(
                                    hsa_code_object_t code_object,
                                    hsa_code_symbol_t symbol,
                                    void* data),
                                  void* data);

      void AddHsaTextData(const void* buffer, size_t size);
      uint64_t NextKernelCodeOffset() const;
      bool AddKernelCode(KernelSymbol* sym, const void* code, size_t size);

      Symbol* AddKernelDefinition(const std::string& name, const void* isa, size_t isa_size);

      size_t DataSegmentCount() const { return dataSegments.size(); }
      Segment* DataSegment(size_t i) const { return dataSegments[i]; }

      size_t DataSectionCount() { return dataSections.size(); }
      Section* DataSection(size_t i) { return dataSections[i]; }

      Section* AddEmptySection();
      Section* AddCodeSection(Segment* segment);
      Section* AddDataSection(const std::string &name,
                              uint32_t type,
                              uint64_t flags,
                              Segment* segment);

      /*
      Section* ImageInitSection();
      void AddImageInitializer(Symbol* image, uint64_t destOffset, const amdgpu_hsa_image_descriptor_t& init);
      void AddImageInitializer(Symbol* image, uint64_t destOffset,
        amdgpu_hsa_metadata_kind16_t kind,
        amdgpu_hsa_image_geometry8_t geometry,
        amdgpu_hsa_image_channel_order8_t channel_order, amdgpu_hsa_image_channel_type8_t channel_type,
        uint64_t width, uint64_t height, uint64_t depth, uint64_t array);
        */

     /*
      hcs::elf::Section* SamplerInitSection();
      hcs::elf::Section* AddSamplerInit();
      void AddSamplerInitializer(Symbol* sampler, uint64_t destOffset, const amdgpu_hsa_sampler_descriptor_t& init);
      void AddSamplerInitializer(Symbol* sampler, uint64_t destOffset,
        amdgpu_hsa_sampler_coord8_t coord,
        amdgpu_hsa_sampler_filter8_t filter,
        amdgpu_hsa_sampler_addressing8_t addressing);
        */

      void AddInitVarWithAddress(bool large, Symbol* dest, uint64_t destOffset, Symbol* addrOf, uint64_t addrAddend);

      void InitHsaSegment(amdgpu_hsa_elf_segment_t segment, bool writable);
      bool AddHsaSegments();
      Segment* HsaSegment(amdgpu_hsa_elf_segment_t segment, bool writable);

      void InitHsaSectionSegment(amdgpu_hsa_elf_section_t section, bool combineSegments = true);
      Section* HsaDataSection(amdgpu_hsa_elf_section_t section, bool combineSegments = true);

      Symbol* AddExecutableSymbol(const std::string &name,
                                  unsigned char type,
                                  unsigned char binding,
                                  unsigned char other,
                                  Section *section = 0);

      Symbol* AddVariableSymbol(const std::string &name,
                                unsigned char type,
                                unsigned char binding,
                                unsigned char other,
                                Section *section,
                                uint64_t value,
                                uint64_t size);
      void AddSectionSymbols();

      size_t RelocationSectionCount() { return relocationSections.size(); }
      RelocationSection* GetRelocationSection(size_t i) { return relocationSections[i]; }

      size_t SymbolCount() { return symbols.size(); }
      Symbol* GetSymbol(size_t i) { return symbols[i]; }
      Symbol* GetSymbolByElfIndex(size_t index);
      Symbol* FindSymbol(const std::string &n);

      void AddData(amdgpu_hsa_elf_section_t section, const void* data = 0, size_t size = 0);

      Section* DebugInfo();
      Section* DebugLine();
      Section* DebugAbbrev();

      Section* AddHsaHlDebug(const std::string& name, const void* data, size_t size);
    };
/*
    class HcsCodeManager {
    private:
      typedef std::unordered_map<uint64_t, HcsCode*> CodeMap;
      CodeMap codeMap;

    public:
      HcsCode* FromHandle(hsa_code_object_t handle);
      bool Destroy(hsa_code_object_t handle);
    };
*/
    /*
    class KernelSymbolV2 : public KernelSymbol {
    private:
    public:
      explicit KernelSymbolV2(hcs::elf::Symbol* elfsym_, const hcs_kernel_code_t* akc);
      bool IsAgent() const override { return true; }
      uint64_t SectionOffset() const override { return elfsym->value() - elfsym->section()->addr(); }
      uint64_t VAddr() const override { return elfsym->value(); }
    };

    class VariableSymbolV2 : public VariableSymbol {
    private:
    public:
      explicit VariableSymbolV2(hcs::elf::Symbol* elfsym_) : VariableSymbol(elfsym_) { }
      bool IsAgent() const override { return false; }
      uint64_t SectionOffset() const override { return elfsym->value() - elfsym->section()->addr(); }
      uint64_t VAddr() const override { return elfsym->value(); }
    };
    */
}

