#pragma once

#include "inc/loader_api.h"
#include <cstddef>
#include <cstdint>
// #include "inc/pps.h"
// #include "inc/pps_ext_image.h"
// #include "inc/pps_ven_loader.h"
#include "ElfDefine.h"
#include <mutex>
#include <string>
#include <vector>

/// @brief Major version of the AMD HSA Loader. Major versions are not backwards
/// compatible.
#define AMD_HSA_LOADER_VERSION_MAJOR 0

/// @brief Minor version of the AMD HSA Loader. Minor versions are backwards
/// compatible.
#define AMD_HSA_LOADER_VERSION_MINOR 5

/// @brief Descriptive version of the AMD HSA Loader.
#define AMD_HSA_LOADER_VERSION "AMD HSA Loader v0.05 (June 16, 2015)"

enum hsa_ext_symbol_info_t {
    HSA_EXT_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT_SIZE = 100,
    HSA_EXT_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT_ALIGN = 101,
};

typedef uint32_t hsa_symbol_info32_t;
typedef hsa_executable_symbol_t hsa_symbol_t;
typedef hsa_executable_symbol_info_t hsa_symbol_info_t;

/// @brief Loaded code object attributes.
enum amd_loaded_code_object_info_t {
    AMD_LOADED_CODE_OBJECT_INFO_ELF_IMAGE = 0,
    AMD_LOADED_CODE_OBJECT_INFO_ELF_IMAGE_SIZE = 1
};

/// @brief Loaded segment handle.
typedef struct amd_loaded_segment_s {
    uint64_t handle;
} amd_loaded_segment_t;

/// @brief Loaded segment attributes.
enum amd_loaded_segment_info_t {
    AMD_LOADED_SEGMENT_INFO_TYPE = 0,
    AMD_LOADED_SEGMENT_INFO_ELF_BASE_ADDRESS = 1,
    AMD_LOADED_SEGMENT_INFO_LOAD_BASE_ADDRESS = 2,
    AMD_LOADED_SEGMENT_INFO_SIZE = 3
};

namespace loader {

//===----------------------------------------------------------------------===//
// Context.                                                                   //
//===----------------------------------------------------------------------===//

class Context {
public:
    virtual ~Context() { }

    // virtual hsa_isa_t IsaFromName(const char* name) = 0;

    // virtual bool IsaSupportedByDevice(core::IAgent* device, hsa_isa_t isa) = 0;

    virtual void* SegmentAlloc(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, size_t size, size_t align, bool zero) = 0;

    virtual bool SegmentCopy(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* dst, size_t offset, const void* src, size_t size) = 0;

    virtual void SegmentFree(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size) = 0;

    virtual void* SegmentAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset) = 0;

    virtual void* SegmentHostAddress(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t offset) = 0;

    virtual bool SegmentFreeze(amdgpu_hsa_elf_segment_t segment, core::IAgent* device, void* seg, size_t size) = 0;

    virtual bool ImageExtensionSupported() = 0;

protected:
    Context() { }

private:
    Context(const Context& c);
    Context& operator=(const Context& c);
};

//===----------------------------------------------------------------------===//
// Symbol.                                                                    //
//===----------------------------------------------------------------------===//

class Symbol {
public:
    static hsa_symbol_t Handle(Symbol* symbol)
    {
        hsa_symbol_t symbol_handle = { reinterpret_cast<uint64_t>(symbol) };
        return symbol_handle;
    }

    static Symbol* Object(hsa_symbol_t symbol_handle)
    {
        Symbol* symbol = reinterpret_cast<Symbol*>(symbol_handle.handle);
        return symbol;
    }

    virtual ~Symbol() { }

    virtual bool GetInfo(hsa_symbol_info32_t symbol_info, void* value) = 0;

    virtual core::IAgent* GetAgent() = 0;

protected:
    Symbol() { }

private:
    Symbol(const Symbol& s);
    Symbol& operator=(const Symbol& s);
};

//===----------------------------------------------------------------------===//
// LoadedCodeObject.                                                          //
//===----------------------------------------------------------------------===//

class LoadedCodeObject {
public:
    static hsa_loaded_code_object_t Handle(LoadedCodeObject* object)
    {
        hsa_loaded_code_object_t handle = { reinterpret_cast<uint64_t>(object) };
        return handle;
    }

    static LoadedCodeObject* Object(hsa_loaded_code_object_t handle)
    {
        LoadedCodeObject* object = reinterpret_cast<LoadedCodeObject*>(handle.handle);
        return object;
    }

    virtual ~LoadedCodeObject() { }

    virtual bool GetInfo(amd_loaded_code_object_info_t attribute, void* value) = 0;

    virtual status_t IterateLoadedSegments(
        status_t (*callback)(
            amd_loaded_segment_t loaded_segment,
            void* data),
        void* data)
        = 0;

    virtual core::IAgent* getAgent() const = 0;
    virtual hsa_executable_t getExecutable() const = 0;
    virtual uint64_t getElfData() const = 0;
    virtual uint64_t getElfSize() const = 0;
    virtual uint64_t getStorageOffset() const = 0;
    virtual uint64_t getLoadBase() const = 0;
    virtual uint64_t getLoadSize() const = 0;
    virtual int64_t getDelta() const = 0;

protected:
    LoadedCodeObject() { }

private:
    LoadedCodeObject(const LoadedCodeObject&);
    LoadedCodeObject& operator=(const LoadedCodeObject&);
};

//===----------------------------------------------------------------------===//
// LoadedSegment.                                                             //
//===----------------------------------------------------------------------===//

class LoadedSegment {
public:
    static amd_loaded_segment_t Handle(LoadedSegment* object)
    {
        amd_loaded_segment_t handle = { reinterpret_cast<uint64_t>(object) };
        return handle;
    }

    static LoadedSegment* Object(amd_loaded_segment_t handle)
    {
        LoadedSegment* object = reinterpret_cast<LoadedSegment*>(handle.handle);
        return object;
    }

    virtual ~LoadedSegment() { }

    virtual bool GetInfo(amd_loaded_segment_info_t attribute, void* value) = 0;

protected:
    LoadedSegment() { }

private:
    LoadedSegment(const LoadedSegment&);
    LoadedSegment& operator=(const LoadedSegment&);
};

//===----------------------------------------------------------------------===//
// Executable.                                                                //
//===----------------------------------------------------------------------===//

class Executable {
public:
    static hsa_executable_t Handle(Executable* executable)
    {
        hsa_executable_t executable_handle = { reinterpret_cast<uint64_t>(executable) };
        return executable_handle;
    }

    static Executable* Object(hsa_executable_t executable_handle)
    {
        Executable* executable = reinterpret_cast<Executable*>(executable_handle.handle);
        return executable;
    }

    virtual ~Executable() { }

    virtual status_t GetInfo(
        hsa_executable_info_t executable_info, void* value)
        = 0;

    virtual status_t DefineProgramExternalVariable(
        const char* name, void* address)
        = 0;

    virtual status_t DefineAgentExternalVariable(
        const char* name,
        core::IAgent* device,
        hsa_variable_segment_t segment,
        void* address)
        = 0;

    virtual status_t LoadCodeObject(
        core::IAgent* device,
        hsa_code_object_t code_object,
        const char* options,
        hsa_loaded_code_object_t* loaded_code_object = nullptr)
        = 0;

    virtual status_t LoadCodeObject(
        core::IAgent* device,
        hsa_code_object_t code_object,
        size_t code_object_size,
        const char* options,
        hsa_loaded_code_object_t* loaded_code_object = nullptr)
        = 0;

    virtual status_t Freeze(const char* options) = 0;

    virtual status_t Validate(uint32_t* result) = 0;

    /// @note needed for hsa v1.0.
    /// @todo remove during loader refactoring.
    virtual bool IsProgramSymbol(const char* symbol_name) = 0;

    virtual Symbol* GetSymbol(
        const char* symbol_name,
        const core::IAgent* device)
        = 0;

    typedef status_t (*iterate_symbols_f)(
        hsa_executable_t executable,
        hsa_symbol_t symbol_handle,
        void* data);

    virtual status_t IterateSymbols(
        iterate_symbols_f callback, void* data)
        = 0;

    /// @since hsa v1.1.
    virtual status_t IterateAgentSymbols(
        core::IAgent* agent,
        status_t (*callback)(hsa_executable_t exec,
            core::IAgent* device,
            hsa_executable_symbol_t symbol,
            void* data),
        void* data)
        = 0;

    /// @since hsa v1.1.
    virtual status_t IterateProgramSymbols(
        status_t (*callback)(hsa_executable_t exec,
            hsa_executable_symbol_t symbol,
            void* data),
        void* data)
        = 0;

    virtual status_t IterateLoadedCodeObjects(
        status_t (*callback)(
            hsa_executable_t executable,
            hsa_loaded_code_object_t loaded_code_object,
            void* data),
        void* data)
        = 0;

    virtual size_t GetNumSegmentDescriptors() = 0;

    virtual size_t QuerySegmentDescriptors(
        hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
        size_t total_num_segment_descriptors,
        size_t first_empty_segment_descriptor)
        = 0;

    virtual uint64_t FindHostAddress(uint64_t device_address) = 0;

    virtual void Print(std::ostream& out) = 0;
    virtual bool PrintToFile(const std::string& filename) = 0;

protected:
    Executable() { }

private:
    Executable(const Executable& e);
    Executable& operator=(const Executable& e);

    static std::vector<Executable*> executables;
    static std::mutex executables_mutex;
};

/// @class Loader
class Loader {
public:
    /// @brief Destructor.
    virtual ~Loader() { }

    /// @brief Creates AMD HSA Loader with specified @p context.
    ///
    /// @param[in] context Context. Must not be null.
    ///
    /// @returns AMD HSA Loader on success, null on failure.
    static Loader* Create(Context* context);

    /// @brief Destroys AMD HSA Loader @p Loader_object.
    ///
    /// @param[in] loader AMD HSA Loader to destroy. Must not be null.
    static void Destroy(Loader* loader);

    /// @returns Context associated with Loader.
    virtual Context* GetContext() const = 0;

    /// @brief Creates empty AMD HSA Executable with specified @p profile,
    virtual Executable* CreateExecutable(
        profile_t profile,
        const char* options,
        hsa_default_float_rounding_mode_t default_float_rounding_mode = HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT) = 0;


    /// @brief Destroys @p executable
    virtual void DestroyExecutable(Executable* executable) = 0;

    /// @brief Invokes @p callback for each created executable
    virtual status_t IterateExecutables(
        status_t (*callback)(
            hsa_executable_t executable,
            void* data),
        void* data)
        = 0;

    /// @brief same as hsa_ven_amd_loader_query_segment_descriptors.
    virtual status_t QuerySegmentDescriptors(
        hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
        size_t* num_segment_descriptors)
        = 0;

    /// @brief Finds the handle of executable to which @p device_address
    /// belongs. Return NULL handle if device address is invalid.
    virtual hsa_executable_t FindExecutable(uint64_t device_address) = 0;

    /// @brief Returns host address given @p device_address. If @p device_address
    /// is already host address, returns null pointer. If @p device_address is
    /// invalid address, returns null pointer.
    virtual uint64_t FindHostAddress(uint64_t device_address) = 0;

    /// @brief Print loader help.
    virtual void PrintHelp(std::ostream& out) = 0;

protected:
    /// @brief Default constructor.
    Loader() { }

private:
    /// @brief Copy constructor - not available.
    Loader(const Loader&);

    /// @brief Assignment operator - not available.
    Loader& operator=(const Loader&);
};

} // namespace loader

