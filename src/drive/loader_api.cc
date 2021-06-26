#include "inc/loader_api.h"
#include "inc/Agent.h"

#include "inc/runtime.h"
#include "loader/CodeObject.h"
#include "loader/Loader.h"
#include <memory.h>

// using namespace amd::hsa;
// using namespace core;
// using namespace hcs;

using loader::Executable;
using loader::LoadedCodeObject;

template <class T>
struct ValidityError;

template <>
struct ValidityError<core::IAgent*> {
    enum { value = ERROR_INVALID_AGENT };
};

template <>
struct ValidityError<core::IMemoryRegion*> {
    enum { value = ERROR_INVALID_MEMORY_REGION };
};

template <>
struct ValidityError<MemoryRegion*> {
    enum { value = ERROR_INVALID_MEMORY_REGION };
};

template <class T>
struct ValidityError<const T*> {
    enum { value = ValidityError<T*>::value };
};

#define IS_VALID(ptr)                                             \
    do {                                                          \
        if ((ptr) == NULL)                                        \
            return status_t(ValidityError<decltype(ptr)>::value); \
    } while (false)

#define IS_BAD_PTR(ptr)                                      \
    do {                                                     \
        if ((ptr) == nullptr) return ERROR_INVALID_ARGUMENT; \
    } while (false)
#define CHECK_ALLOC(ptr)                                  \
    do {                                                  \
        if ((ptr) == NULL) return ERROR_OUT_OF_RESOURCES; \
    } while (false)

status_t hsa_ven_amd_loader_query_host_address(
    const void* device_address,
    const void** host_address)
{
    if (false == Runtime::runtime_singleton_->IsOpen()) {
        return ERROR_NOT_INITIALIZED;
    }
    if (nullptr == device_address) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (nullptr == host_address) {
        return ERROR_INVALID_ARGUMENT;
    }

    uint64_t udaddr = reinterpret_cast<uint64_t>(device_address);
    uint64_t uhaddr = Runtime::runtime_singleton_->loader()->FindHostAddress(udaddr);
    if (0 == uhaddr) {
        return ERROR_INVALID_ARGUMENT;
    }

    *host_address = reinterpret_cast<void*>(uhaddr);
    return SUCCESS;
}

status_t hsa_ven_amd_loader_query_segment_descriptors(
    hsa_ven_amd_loader_segment_descriptor_t* segment_descriptors,
    size_t* num_segment_descriptors)
{
    if (false == Runtime::runtime_singleton_->IsOpen()) {
        return ERROR_NOT_INITIALIZED;
    }

    // Arguments are checked by the loader.
    return Runtime::runtime_singleton_->loader()->QuerySegmentDescriptors(segment_descriptors, num_segment_descriptors);
}

status_t hsa_ven_amd_loader_query_executable(
    const void* device_address,
    hsa_executable_t* executable)
{

    if (false == Runtime::runtime_singleton_->IsOpen()) {
        return ERROR_NOT_INITIALIZED;
    }
    if ((nullptr == device_address) || (nullptr == executable)) {
        return ERROR_INVALID_ARGUMENT;
    }

    uint64_t udaddr = reinterpret_cast<uint64_t>(device_address);
    hsa_executable_t exec = Runtime::runtime_singleton_->loader()->FindExecutable(udaddr);
    if (0 == exec.handle) {
        return ERROR_INVALID_ARGUMENT;
    }

    *executable = exec;
    return SUCCESS;
}

status_t hsa_ven_amd_loader_executable_iterate_loaded_code_objects(
    hsa_executable_t executable,
    status_t (*callback)(
        hsa_executable_t executable,
        hsa_loaded_code_object_t loaded_code_object,
        void* data),
    void* data)
{
    if (false == Runtime::runtime_singleton_->IsOpen()) {
        return ERROR_NOT_INITIALIZED;
    }
    if (nullptr == callback) {
        return ERROR_INVALID_ARGUMENT;
    }

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->IterateLoadedCodeObjects(callback, data);
}

status_t hsa_ven_amd_loader_loaded_code_object_get_info(
    hsa_loaded_code_object_t loaded_code_object,
    hsa_ven_amd_loader_loaded_code_object_info_t attribute,
    void* value)
{
    if (false == Runtime::runtime_singleton_->IsOpen()) {
        return ERROR_NOT_INITIALIZED;
    }
    if (nullptr == value) {
        return ERROR_INVALID_ARGUMENT;
    }

    const LoadedCodeObject* lcobj = LoadedCodeObject::Object(loaded_code_object);
    if (!lcobj) {
        return ERROR_INVALID_CODE_OBJECT;
    }

    switch (attribute) {
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_EXECUTABLE: {
        *((hsa_executable_t*)value) = lcobj->getExecutable();
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_KIND: {
        *((uint32_t*)value) = lcobj->getAgent() == nullptr
            ? HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_KIND_PROGRAM
            : HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_KIND_AGENT;
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_AGENT: {
        core::IAgent* agent = lcobj->getAgent();
        if (agent == nullptr) {
            return ERROR_INVALID_ARGUMENT;
        }
        *((core::IAgent**)value) = agent;
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_TYPE: {
        // TODO Update loader so it keeps track if code object was loaded from a
        // file or memory.
        *((uint32_t*)value) = HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY;
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_MEMORY_BASE: {
        *((uint64_t*)value) = lcobj->getElfData();
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_MEMORY_SIZE: {
        *((uint64_t*)value) = lcobj->getElfSize();
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_FILE: {
        // TODO Update loader so it keeps track if code object was loaded from a
        // file or memory.
        return ERROR_INVALID_ARGUMENT;
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_DELTA: {
        // TODO Check if executable is frozen.
        // This suggests this code should be moved into LoadedCodeObjectImpl::getinfo
        // as is done for other *_get_info methods. Currently LoadedCodeObject has a
        // GetInfo method which is likely not used.
        // Also should this have a *NOT_FROZEN ststus code added?
        // if (state_ != HSA_EXECUTABLE_STATE_FROZEN) {
        //   return ERROR_INVALID_ARGUMENT;
        // }
        *((int64_t*)value) = lcobj->getDelta();
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_BASE: {
        // TODO Check if executable is frozen.
        *((uint64_t*)value) = lcobj->getLoadBase();
        break;
    }
    case HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_SIZE: {
        // TODO Check if executable is frozen.
        *((uint64_t*)value) = lcobj->getLoadSize();
        break;
    }
    default: {
        return ERROR_INVALID_ARGUMENT;
    }
    }

    return SUCCESS;
}

#if 0
status_t hsa_system_get_major_extension_table(uint16_t extension, uint16_t version_major,
                                                  size_t table_length, void* table) {

  if (table_length == 0) return ERROR_INVALID_ARGUMENT;

  if (extension == HSA_EXTENSION_IMAGES) {
      return ERROR;
      /*
    if (version_major !=
       core::IRuntime::runtime_singleton_->extensions_.image_api.version.major_id) {
      return ERROR;
    }

    hsa_ext_images_1_pfn_t ext_table;
    ext_table.hsa_ext_image_clear = hsa_ext_image_clear;
    ext_table.hsa_ext_image_copy = hsa_ext_image_copy;
    ext_table.hsa_ext_image_create = hsa_ext_image_create;
    ext_table.hsa_ext_image_data_get_info = hsa_ext_image_data_get_info;
    ext_table.hsa_ext_image_destroy = hsa_ext_image_destroy;
    ext_table.hsa_ext_image_export = hsa_ext_image_export;
    ext_table.hsa_ext_image_get_capability = hsa_ext_image_get_capability;
    ext_table.hsa_ext_image_import = hsa_ext_image_import;
    ext_table.hsa_ext_sampler_create = hsa_ext_sampler_create;
    ext_table.hsa_ext_sampler_destroy = hsa_ext_sampler_destroy;
    ext_table.hsa_ext_image_get_capability_with_layout = hsa_ext_image_get_capability_with_layout;
    ext_table.hsa_ext_image_data_get_info_with_layout = hsa_ext_image_data_get_info_with_layout;
    ext_table.hsa_ext_image_create_with_layout = hsa_ext_image_create_with_layout;

    memcpy(table, &ext_table, Min(sizeof(ext_table), table_length));

    return SUCCESS;
    */
  }

  if (extension == HSA_EXTENSION_FINALIZER) {
      return ERROR;
      /*
    if (version_major !=
        core::IRuntime::runtime_singleton_->extensions_.finalizer_api.version.major_id) {
      return ERROR;
    }

    hsa_ext_finalizer_1_00_pfn_t ext_table;
    ext_table.hsa_ext_program_add_module = hsa_ext_program_add_module;
    ext_table.hsa_ext_program_create = hsa_ext_program_create;
    ext_table.hsa_ext_program_destroy = hsa_ext_program_destroy;
    ext_table.hsa_ext_program_finalize = hsa_ext_program_finalize;
    ext_table.hsa_ext_program_get_info = hsa_ext_program_get_info;
    ext_table.hsa_ext_program_iterate_modules = hsa_ext_program_iterate_modules;

    memcpy(table, &ext_table, Min(sizeof(ext_table), table_length));

    return SUCCESS;
    */
  }

  if (extension == HSA_EXTENSION_AMD_LOADER) {
    if (version_major != 1) return ERROR;
    hsa_ven_amd_loader_1_01_pfn_t ext_table;
    ext_table.hsa_ven_amd_loader_query_host_address = hsa_ven_amd_loader_query_host_address;
    ext_table.hsa_ven_amd_loader_query_segment_descriptors =
        hsa_ven_amd_loader_query_segment_descriptors;
    ext_table.hsa_ven_amd_loader_query_executable = hsa_ven_amd_loader_query_executable;
    ext_table.hsa_ven_amd_loader_executable_iterate_loaded_code_objects =
        hsa_ven_amd_loader_executable_iterate_loaded_code_objects;
    ext_table.hsa_ven_amd_loader_loaded_code_object_get_info =
        hsa_ven_amd_loader_loaded_code_object_get_info;

    memcpy(table, &ext_table, Min(sizeof(ext_table), table_length));

    return SUCCESS;
  }

  if (extension == HSA_EXTENSION_AMD_AQLPROFILE) {
    return ERROR;
      /*
    if (version_major != hsa_ven_amd_aqlprofile_VERSION_MAJOR) {
      return ERROR;
    }

    os::LibHandle lib = os::LoadLib(kAqlProfileLib);
    if (lib == NULL) {
      debug_print("Loading '%s' failed\n", kAqlProfileLib);
      return ERROR;
    }

    hsa_ven_amd_aqlprofile_1_00_pfn_t ext_table;
    ext_table.hsa_ven_amd_aqlprofile_version_major =
      (decltype(::hsa_ven_amd_aqlprofile_version_major)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_version_major");
    ext_table.hsa_ven_amd_aqlprofile_version_minor =
      (decltype(::hsa_ven_amd_aqlprofile_version_minor)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_version_minor");
    ext_table.hsa_ven_amd_aqlprofile_error_string =
      (decltype(::hsa_ven_amd_aqlprofile_error_string)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_error_string");
    ext_table.hsa_ven_amd_aqlprofile_validate_event =
      (decltype(::hsa_ven_amd_aqlprofile_validate_event)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_validate_event");
    ext_table.hsa_ven_amd_aqlprofile_start =
      (decltype(::hsa_ven_amd_aqlprofile_start)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_start");
    ext_table.hsa_ven_amd_aqlprofile_stop =
      (decltype(::hsa_ven_amd_aqlprofile_stop)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_stop");
    ext_table.hsa_ven_amd_aqlprofile_read =
      (decltype(::hsa_ven_amd_aqlprofile_read)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_read");
    ext_table.hsa_ven_amd_aqlprofile_legacy_get_pm4 =
      (decltype(::hsa_ven_amd_aqlprofile_legacy_get_pm4)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_legacy_get_pm4");
    ext_table.hsa_ven_amd_aqlprofile_get_info =
      (decltype(::hsa_ven_amd_aqlprofile_get_info)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_get_info");
    ext_table.hsa_ven_amd_aqlprofile_iterate_data =
      (decltype(::hsa_ven_amd_aqlprofile_iterate_data)*)
        os::GetExportAddress(lib, "hsa_ven_amd_aqlprofile_iterate_data");

    memcpy(table, &ext_table, Min(sizeof(ext_table), table_length));
    */

    return SUCCESS;
  }

  return ERROR;
}

status_t hsa_system_major_extension_supported(uint16_t extension, uint16_t version_major,
                                                uint16_t* version_minor, bool* result) {
    /*
   TRY;
   IS_OPEN();
   IS_BAD_PTR(version_minor);
   IS_BAD_PTR(result);
   */
    // TODO schi should we disable support the extension now
    // return ERROR;
   if ((extension == HSA_EXTENSION_IMAGES) && (version_major == 1)) {
     *version_minor = 0;
     *result = true;
     return SUCCESS;
   }

   if ((extension == HSA_EXTENSION_FINALIZER) && (version_major == 1)) {
     *version_minor = 0;
     *result = true;
     return SUCCESS;
   }

   if ((extension == HSA_EXTENSION_AMD_LOADER) && (version_major == 1)) {
     *version_minor = 0;
     *result = true;
     return SUCCESS;
   }

   if ((extension == HSA_EXTENSION_AMD_AQLPROFILE) && (version_major == 1)) {
     *version_minor = 0;
     *result = true;
     return SUCCESS;
   }

   *result = false;
   return SUCCESS;
   // CATCH;
}
#endif

using loader::Executable;
using loader::Loader;

Loader* GetLoader()
{
    return Runtime::runtime_singleton_->loader();
}

#if 0
status_t hsa_code_object_reader_create_from_file(
    file_t file,
    hsa_code_object_reader_t* code_object_reader)
{
    IS_BAD_PTR(code_object_reader);

    off_t file_size = __lseek__(file, 0, SEEK_END);
    if (file_size == (off_t)-1) {
        return ERROR_INVALID_FILE;
    }

    if (__lseek__(file, 0, SEEK_SET) == (off_t)-1) {
        return ERROR_INVALID_FILE;
    }

    unsigned char* code_object_memory = new unsigned char[file_size];
    CHECK_ALLOC(code_object_memory);

    if (__read__(file, code_object_memory, file_size) != file_size) {
        delete[] code_object_memory;
        return ERROR_INVALID_FILE;
    }

    CodeObjectReader* wrapper = new (std::nothrow) CodeObjectReader(
        code_object_memory, file_size, true);
    if (!wrapper) {
        delete[] code_object_memory;
        return ERROR_OUT_OF_RESOURCES;
    }

    *code_object_reader = CodeObjectReader::Handle(wrapper);
    return SUCCESS;
    CATCH;
}
#endif

status_t hsa_executable_create_alt(
    profile_t profile,
    hsa_default_float_rounding_mode_t default_float_rounding_mode,
    const char* options,
    hsa_executable_t* executable)
{
    /* 
  IS_BAD_PROFILE(profile);
  IS_BAD_ROUNDING_MODE(default_float_rounding_mode); // NOTES: should we check
                                                     // if default float
                                                     // rounding mode is valid?
                                                     // spec does not say so.
                                                     */
    IS_BAD_PTR(executable);

    Executable* exec = GetLoader()->CreateExecutable(
        profile, options, default_float_rounding_mode);
    CHECK_ALLOC(exec);

    *executable = Executable::Handle(exec);
    return SUCCESS;
}

status_t hsa_executable_destroy(
    hsa_executable_t executable)
{

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    GetLoader()->DestroyExecutable(exec);
    return SUCCESS;
}

/* deprecated */
/*
status_t hsa_executable_load_code_object(
    hsa_executable_t executable,
    core::IAgent* agent,
    hsa_code_object_t code_object,
    const char *options) {
  TRY;
  IS_OPEN();

  Executable *exec = Executable::Object(executable);
  if (!exec) {
    return ERROR_INVALID_EXECUTABLE;
  }

  return exec->LoadCodeObject(agent, code_object, options);
  CATCH;
}
*/

#if 0
status_t hsa_executable_load_program_code_object(
    hsa_executable_t executable,
    hsa_code_object_reader_t code_object_reader,
    const char* options,
    hsa_loaded_code_object_t* loaded_code_object)
{

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    CodeObjectReader* wrapper = CodeObjectReader::Object(
        code_object_reader);
    if (!wrapper) {
        return ERROR_INVALID_CODE_OBJECT_READER;
    }

    hsa_code_object_t code_object = { reinterpret_cast<uint64_t>(wrapper->code_object_memory) };
    return exec->LoadCodeObject(
        { 0 }, code_object, options, loaded_code_object);
    CATCH;
}


status_t hsa_executable_freeze(
    hsa_executable_t executable,
    const char* options)
{
    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->Freeze(options);
    CATCH;
}
#endif

status_t hsa_executable_get_info(
    hsa_executable_t executable,
    hsa_executable_info_t attribute,
    void* value)
{
    IS_BAD_PTR(value);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->GetInfo(attribute, value);
}

status_t hsa_executable_global_variable_define(
    hsa_executable_t executable,
    const char* variable_name,
    void* address)
{
    IS_BAD_PTR(variable_name);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->DefineProgramExternalVariable(variable_name, address);
}

status_t hsa_executable_agent_global_variable_define(
    hsa_executable_t executable,
    core::IAgent* agent,
    const char* variable_name,
    void* address)
{
    IS_BAD_PTR(variable_name);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->DefineAgentExternalVariable(
        variable_name, agent, HSA_VARIABLE_SEGMENT_GLOBAL, address);
}

status_t hsa_executable_readonly_variable_define(
    hsa_executable_t executable,
    core::IAgent* agent,
    const char* variable_name,
    void* address)
{
    IS_BAD_PTR(variable_name);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->DefineAgentExternalVariable(
        variable_name, agent, HSA_VARIABLE_SEGMENT_READONLY, address);
}

status_t hsa_executable_validate(
    hsa_executable_t executable,
    uint32_t* result)
{
    IS_BAD_PTR(result);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->Validate(result);
}

status_t hsa_executable_validate_alt(
    hsa_executable_t executable,
    const char* options,
    uint32_t* result)
{
    IS_BAD_PTR(result);

    return hsa_executable_validate(executable, result);
}

/* deprecated */
/*
status_t hsa_executable_get_symbol(
    hsa_executable_t executable,
    const char *module_name,
    const char *symbol_name,
    core::IAgent* agent,
    int32_t call_convention,
    hsa_executable_symbol_t *symbol) {
  TRY;
  IS_OPEN();
  IS_BAD_PTR(symbol_name);
  IS_BAD_PTR(symbol);

  std::string mangled_name(symbol_name);
  if (mangled_name.empty()) {
    return ERROR_INVALID_SYMBOL_NAME;
  }
  if (module_name && !std::string(module_name).empty()) {
    mangled_name.insert(0, "::");
    mangled_name.insert(0, std::string(module_name));
  }

  Executable *exec = Executable::Object(executable);
  if (!exec) {
    return ERROR_INVALID_EXECUTABLE;
  }

  // Invoke non-deprecated API.
  return HSA::hsa_executable_get_symbol_by_name(
      executable, mangled_name.c_str(),
      exec->IsProgramSymbol(mangled_name.c_str()) ? nullptr : &agent, symbol);
  CATCH;
}
*/
/*
status_t hsa_executable_get_symbol_by_name(
    hsa_executable_t executable,
    const char* symbol_name,
    const core::IAgent** agent, // NOTES: this is not consistent with the rest of
    // of the specification, but seems like a better
    // approach to distinguish program/agent symbols.
    hsa_executable_symbol_t* symbol)
{
    IS_BAD_PTR(symbol_name);
    IS_BAD_PTR(symbol);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    loader::Symbol* sym = exec->GetSymbol(symbol_name, agent);
    if (!sym) {
        return ERROR_INVALID_SYMBOL_NAME;
    }

    *symbol = loader::Symbol::Handle(sym);
    return SUCCESS;
    CATCH;
}*/

status_t hsa_executable_symbol_get_info(
    hsa_executable_symbol_t executable_symbol,
    hsa_executable_symbol_info_t attribute,
    void* value)
{
    IS_BAD_PTR(value);

    loader::Symbol* sym = loader::Symbol::Object(executable_symbol);
    if (!sym) {
        return ERROR_INVALID_EXECUTABLE_SYMBOL;
    }

    return sym->GetInfo(attribute, value) ? SUCCESS : ERROR_INVALID_ARGUMENT;
}

/* deprecated */
/*
status_t hsa_executable_iterate_symbols(
    hsa_executable_t executable,
    status_t (*callback)(hsa_executable_t executable,
                             hsa_executable_symbol_t symbol,
                             void *data),
    void *data) {
  TRY;
  IS_OPEN();
  IS_BAD_PTR(callback);

  Executable *exec = Executable::Object(executable);
  if (!exec) {
    return ERROR_INVALID_EXECUTABLE;
  }

  return exec->IterateSymbols(callback, data);
  CATCH;
}
*/

status_t hsa_executable_iterate_agent_symbols(
    hsa_executable_t executable,
    core::IAgent* agent,
    status_t (*callback)(hsa_executable_t exec,
        core::IAgent* agent,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data)
{
    IS_BAD_PTR(callback);

    // NOTES: should we check if agent is valid? spec does not say so.
    // const core::IAgent *agent_object = core::Agent::Object(agent);
    // IS_VALID(agent_object);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->IterateAgentSymbols(agent, callback, data);
}

status_t hsa_executable_iterate_program_symbols(
    hsa_executable_t executable,
    status_t (*callback)(hsa_executable_t exec,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data)
{
    IS_BAD_PTR(callback);

    Executable* exec = Executable::Object(executable);
    if (!exec) {
        return ERROR_INVALID_EXECUTABLE;
    }

    return exec->IterateProgramSymbols(callback, data);
}

status_t hsa_amd_coherency_get_type(const core::IAgent* agent, hsa_amd_coherency_type_t* type)
{

    IS_VALID(agent);

    IS_BAD_PTR(type);

    if (agent->agent_type() != core::IAgent::AgentType::kGpu) {
        return ERROR_INVALID_AGENT;
    }

    const GpuAgentInt* gpu_agent = static_cast<const GpuAgentInt*>(agent);

    *type = gpu_agent->current_coherency_type();

    return SUCCESS;
}

status_t hsa_amd_coherency_set_type(core::IAgent* agent,
    hsa_amd_coherency_type_t type)
{

    IS_VALID(agent);

    if (type < HSA_AMD_COHERENCY_TYPE_COHERENT || type > HSA_AMD_COHERENCY_TYPE_NONCOHERENT) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (agent->agent_type() != core::IAgent::AgentType::kGpu) {
        return ERROR_INVALID_AGENT;
    }

    GpuAgent* gpu_agent = static_cast<GpuAgent*>(agent);

    if (!gpu_agent->current_coherency_type(type)) {
        return ERROR;
    }

    return SUCCESS;
}

/*
status_t hsa_amd_memory_fill(void* ptr, uint32_t value, size_t count) {

  IS_OPEN();

  if (ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  if (count == 0) {
    return SUCCESS;
  }

  return core::IRuntime::runtime_singleton_->FillMemory(ptr, value, count);

}
*/

status_t hsa_amd_memory_async_copy(void* dst, core::IAgent* dst_agent, const void* src,
    core::IAgent* src_agent, size_t size,
    uint32_t num_dep_signals, const signal_t* dep_signals,
    signal_t completion_signal)
{

    if (dst == NULL || src == NULL) { return ERROR_INVALID_ARGUMENT; }

    if ((num_dep_signals == 0 && dep_signals != NULL) || (num_dep_signals > 0 && dep_signals == NULL)) {
        return ERROR_INVALID_ARGUMENT;
    }

    IS_VALID(dst_agent);
    IS_VALID(src_agent);

    std::vector<signal_t> dep_signal_list(num_dep_signals);
    if (num_dep_signals > 0) {
        for (size_t i = 0; i < num_dep_signals; ++i) {
            dep_signal_list[i] = dep_signals[i];
        }
    }
    // signal_t out_signal_obj = core::Signal::Object(completion_signal);
    // IS_VALID(out_signal_obj);

    if (size > 0) {
        return Runtime::runtime_singleton_->CopyMemory(
            dst, *dst_agent, src, *src_agent, size, dep_signal_list,
            completion_signal);
        // *out_signal_obj);
    }

    return SUCCESS;
}

#if 0
status_t hsa_amd_profiling_set_profiler_enabled(queue_t* queue, int enable)
{

    core::IQueue* cmd_queue = core::Queue::Object(queue);
    IS_VALID(cmd_queue);

    cmd_queue->SetProfiling(enable);

    return SUCCESS;
}

status_t hsa_amd_profiling_async_copy_enable(bool enable)
{

    return core::IRuntime::runtime_singleton_->IterateAgent(
        [](core::IAgent* agent_handle, void* data) -> status_t {
            const bool enable = *(reinterpret_cast<bool*>(data));
            return agent_handle->profiling_enabled(enable);
        },
        reinterpret_cast<void*>(&enable));
}

status_t hsa_amd_profiling_get_dispatch_time(
    core::IAgent* agent_handle, signal_t hsa_signal,
    hsa_amd_profiling_dispatch_time_t* time)
{


    IS_BAD_PTR(time);

    core::IAgent* agent = core::Agent::Object(agent_handle);

    IS_VALID(agent);

    signal_t signal = core::Signal::Object(hsa_signal);

    IS_VALID(signal);

    if (agent->agent_type() != core::IAgent::AgentType::kGpu) {
        return ERROR_INVALID_AGENT;
    }

    GpuAgentInt* gpu_agent = static_cast<GpuAgentInt*>(agent);

    // Translate timestamp from GPU to system domain.
    gpu_agent->TranslateTime(signal, *time);

    return SUCCESS;
}

status_t hsa_amd_profiling_get_async_copy_time(
    signal_t hsa_signal, hsa_amd_profiling_async_copy_time_t* time)
{


    IS_BAD_PTR(time);

    signal_t signal = core::Signal::Object(hsa_signal);

    IS_VALID(signal);

    core::IAgent* agent = signal->async_copy_agent();

    if (agent == NULL) {
        return ERROR;
    }

    // Validate the embedded agent pointer if the signal is IPC.
    if (signal->isIPC()) {
        for (auto it : core::IRuntime::runtime_singleton_->gpu_agents()) {
            if (it == agent) break;
        }
        // If the agent isn't a GPU then it is from a different process or it's a CPU.
        // Assume it's a CPU and illegal uses will generate garbage data same as kernel completion.
        agent = core::IRuntime::runtime_singleton_->cpu_agents()[0];
    }

    if (agent->agent_type() == core::IAgent::AgentType::kGpu) {
        // Translate timestamp from GPU to system domain.
        static_cast<GpuAgentInt*>(agent)->TranslateTime(signal, *time);
        return SUCCESS;
    }

    // The timestamp is already in system domain.
    time->start = signal->signal_.start_ts;
    time->end = signal->signal_.end_ts;
    return SUCCESS;
}

status_t hsa_amd_profiling_convert_tick_to_system_domain(core::IAgent* agent_handle,
    uint64_t agent_tick,
    uint64_t* system_tick)
{


    IS_BAD_PTR(system_tick);

    core::IAgent* agent = core::Agent::Object(agent_handle);

    IS_VALID(agent);

    if (agent->agent_type() != core::IAgent::AgentType::kGpu) {
        return ERROR_INVALID_AGENT;
    }

    GpuAgentInt* gpu_agent = static_cast<GpuAgentInt*>(agent);

    *system_tick = gpu_agent->TranslateTime(agent_tick);

    return SUCCESS;
}
#endif

#if 0
status_t hsa_amd_signal_create(signal_value_t initial_value, uint32_t num_consumers,
    const core::IAgent** consumers, uint64_t attributes,
    signal_t* hsa_signal)
{
    struct AgentHandleCompare {
        bool operator()(const core::IAgent* lhs, const core::Agent* rhs) const
        {
            return lhs.handle < rhs.handle;
        }
    };

    IS_BAD_PTR(hsa_signal);

    signal_t ret;

    bool enable_ipc = attributes & HSA_AMD_SIGNAL_IPC;
    bool use_default = enable_ipc || (attributes & HSA_AMD_SIGNAL_AMD_GPU_ONLY) || (!core::Ig_use_interrupt_wait);

    if ((!use_default) && (num_consumers != 0)) {
        IS_BAD_PTR(consumers);

        // Check for duplicates in consumers.
        std::set<core::IAgent*, AgentHandleCompare> consumer_set(consumers, consumers + num_consumers);
        if (consumer_set.size() != num_consumers) {
            return ERROR_INVALID_ARGUMENT;
        }

        use_default = true;
        for (const core::IAgent* cpu_agent : core::Runtime::runtime_singleton_->cpu_agents()) {
            use_default &= (consumer_set.find(cpu_agent->public_handle()) == consumer_set.end());
        }
    }

    if (use_default) {
        ret = new core::DefaultSignal(initial_value, enable_ipc);
    } else {
        ret = new core::InterruptSignal(initial_value);
    }

    *hsa_signal = core::ISignal::Handle(ret);
    return SUCCESS;
}

uint32_t hsa_amd_signal_wait_any(uint32_t signal_count, signal_t* hsa_signals,
    signal_condition_t* conds, signal_value_t* values,
    uint64_t timeout_hint, hsa_wait_state_t wait_hint,
    signal_value_t* satisfying_value)
{

    // Do not check for signal invalidation.  Invalidation may occur during async
    // signal handler loop and is not an error.
    for (uint i = 0; i < signal_count; i++)
        assert(hsa_signals[i].handle != 0 && core::ISharedSignal::Object(hsa_signals[i])->IsValid() && "Invalid signal.");

    return core::ISignal::WaitAny(signal_count, hsa_signals, conds, values,
        timeout_hint, wait_hint, satisfying_value);
    // CATCHRET(uint32_t);
}
#endif

/*
status_t hsa_amd_signal_async_handler(signal_t hsa_signal, signal_condition_t cond,
                                          signal_value_t value, hsa_amd_signal_handler handler,
                                          void* arg) {

  IS_OPEN();
  IS_BAD_PTR(handler);

  signal_t signal = core::Signal::Object(hsa_signal);
  IS_VALID(signal);
  if (core::Ig_use_interrupt_wait && (!core::InterruptSignal::IsType(signal)))
    return ERROR_INVALID_SIGNAL;
  return core::IRuntime::runtime_singleton_->SetAsyncSignalHandler(
      hsa_signal, cond, value, handler, arg);
}
*/

/*
status_t hsa_amd_async_function(void (*callback)(void* arg), void* arg) {

  IS_OPEN();

  IS_BAD_PTR(callback);
  static const signal_t null_signal = {0};
  return core::IRuntime::runtime_singleton_->SetAsyncSignalHandler(
      null_signal, HSA_SIGNAL_CONDITION_EQ, 0, (hsa_amd_signal_handler)callback,
      arg);

}
*/

status_t hsa_amd_memory_lock(void* host_ptr, size_t size,
    core::IAgent** agents, int num_agent,
    void** agent_ptr)
{

    *agent_ptr = NULL;

    if (size == 0 || host_ptr == NULL || agent_ptr == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    if ((agents != NULL && num_agent == 0) || (agents == NULL && num_agent != 0)) {
        return ERROR_INVALID_ARGUMENT;
    }

    const MemoryRegion* system_region = reinterpret_cast<const MemoryRegion*>(
        Runtime::runtime_singleton_->system_regions_fine()[0]);

    return system_region->Lock(num_agent, agents, host_ptr, size, agent_ptr);
}

status_t hsa_amd_memory_unlock(void* host_ptr)
{

    const MemoryRegion* system_region = reinterpret_cast<const MemoryRegion*>(
        Runtime::runtime_singleton_->system_regions_fine()[0]);

    return system_region->Unlock(host_ptr);
}

status_t hsa_amd_memory_pool_get_info(const MemoryRegion* mem_region,
    hsa_amd_memory_pool_info_t attribute, void* value)
{

    IS_BAD_PTR(value);

    if (mem_region == NULL) {
        return (status_t)ERROR_INVALID_MEMORY_REGION;
    }

    return mem_region->GetPoolInfo(attribute, value);
}

status_t hsa_amd_agent_iterate_memory_pools(
    const core::IAgent* agent,
    status_t (*callback)(const MemoryRegion* memory_pool, void* data),
    void* data)
{

    IS_BAD_PTR(callback);
    IS_VALID(agent);

#if 0
    if (agent->agent_type() == core::IAgent::AgentType::kCpu) {
        return reinterpret_cast<const CpuAgent*>(agent)->VisitRegion(
            false, reinterpret_cast<status_t (*)(const MemoryRegion* memory_pool, void* data)>(callback),
            data);
    }
#endif

    return reinterpret_cast<const GpuAgentInt*>(agent)->VisitRegion(
        false,
        reinterpret_cast<status_t (*)(const core::IMemoryRegion* memory_pool, void* data)>(
            callback),
        data);
}

status_t hsa_amd_memory_pool_allocate(const MemoryRegion* memory_pool, size_t size,
    uint32_t flags, void** ptr)
{

    if (size == 0 || ptr == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    // const core::IMemoryRegion* mem_region = dynamic_cast<const core::IMemoryRegion*>(memory_pool);

    // if (mem_region == NULL || !mem_region->IsValid()) {
    if (memory_pool == NULL) {
        return (status_t)ERROR_INVALID_MEMORY_REGION;
    }

    return Runtime::runtime_singleton_->AllocateMemory(
        memory_pool, size, core::IMemoryRegion::AllocateRestrict, ptr);
}

status_t hsa_amd_memory_pool_free(void* ptr)
{
    return hsa_memory_free(ptr);
}

status_t hsa_amd_agents_allow_access(uint32_t num_agents, const core::IAgent** agents,
    const uint32_t* flags, const void* ptr)
{

    if (num_agents == 0 || agents == NULL || flags != NULL || ptr == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    return Runtime::runtime_singleton_->AllowAccess(num_agents, agents,
        ptr);
}

status_t hsa_amd_memory_pool_can_migrate(const MemoryRegion* src_mem_region,
    const MemoryRegion* dst_mem_region, bool* result)
{

    if (result == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    // if (src_mem_region == NULL || !src_mem_region->IsValid()) {
    if (src_mem_region == NULL) {
        return static_cast<status_t>(ERROR_INVALID_MEMORY_REGION);
    }

    // if (dst_mem_region == NULL || !dst_mem_region->IsValid()) {
    if (dst_mem_region == NULL) {
        return static_cast<status_t>(ERROR_INVALID_MEMORY_REGION);
    }

    return src_mem_region->CanMigrate(*dst_mem_region, *result);
}

status_t hsa_amd_memory_migrate(const void* ptr,
    const MemoryRegion* dst_mem_region,
    uint32_t flags)
{

    if (ptr == NULL || flags != 0) {
        return ERROR_INVALID_ARGUMENT;
    }

    // if (dst_mem_region == NULL || !dst_mem_region->IsValid()) {
    if (dst_mem_region == NULL) {
        return static_cast<status_t>(ERROR_INVALID_MEMORY_REGION);
    }

    return dst_mem_region->Migrate(flags, ptr);
}

status_t hsa_amd_agent_memory_pool_get_info(
    core::IAgent* agent, const MemoryRegion* mem_region,
    hsa_amd_agent_memory_pool_info_t attribute, void* value)
{

    if (value == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }

    IS_VALID(agent);

    // if (mem_region == NULL || !mem_region->IsValid()) {
    if (mem_region == NULL) {
        return static_cast<status_t>(ERROR_INVALID_MEMORY_REGION);
    }

    return mem_region->GetAgentPoolInfo(*agent, attribute, value);
}
/*
status_t hsa_amd_interop_map_buffer(uint32_t num_agents,
core::IAgent** agents, int interop_handle,
                                        uint32_t flags, size_t* size,
                                        void** ptr, size_t* metadata_size,
                                        const void** metadata) {
  static const int tinyArraySize=8;

  IS_OPEN();
  IS_BAD_PTR(agents);
  IS_BAD_PTR(size);
  IS_BAD_PTR(ptr);
  if (flags != 0) return ERROR_INVALID_ARGUMENT;
  if (num_agents == 0) return ERROR_INVALID_ARGUMENT;

  core::IAgent* short_agents[tinyArraySize];
  core::IAgent** core_agents = short_agents;
  if (num_agents > tinyArraySize) {
    core_agents = new core::IAgent* [num_agents];
    if (core_agents == NULL) return ERROR_OUT_OF_RESOURCES;
  }

  for (uint32_t i = 0; i < num_agents; i++) {
    core::IAgent* device = core::Agent::Object(agents[i]);
    IS_VALID(device);
    core_agents[i] = device;
  }

  auto ret = core::IRuntime::runtime_singleton_->InteropMap(
      num_agents, core_agents, interop_handle, flags, size, ptr, metadata_size,
      metadata);

  if (num_agents > tinyArraySize) delete[] core_agents;
  return ret;

}
*/

/*
status_t hsa_amd_interop_unmap_buffer(void* ptr) {

  IS_OPEN();
  if (ptr != NULL) core::IRuntime::runtime_singleton_->InteropUnmap(ptr);
  return SUCCESS;

}
*/

status_t hsa_amd_pointer_info(void* ptr, hsa_amd_pointer_info_t* info, void* (*alloc)(size_t),
    uint32_t* num_accessible, core::IAgent** accessible)
{

    IS_BAD_PTR(ptr);
    IS_BAD_PTR(info);
    return Runtime::runtime_singleton_->PtrInfo(ptr, info, alloc, num_accessible, accessible);
}

/*
status_t hsa_amd_pointer_info_set_userdata(void* ptr, void* userdata) {

  IS_OPEN();
  IS_BAD_PTR(ptr);
  return core::IRuntime::runtime_singleton_->SetPtrInfoData(ptr, userdata);
}
*/

status_t hsa_amd_ipc_memory_create(void* ptr, size_t len, hsa_amd_ipc_memory_t* handle)
{

    IS_BAD_PTR(ptr);
    IS_BAD_PTR(handle);
    return Runtime::runtime_singleton_->IPCCreate(ptr, len, handle);
}

status_t hsa_amd_ipc_memory_attach(const hsa_amd_ipc_memory_t* ipc, size_t len,
    uint32_t num_agents, const core::IAgent** mapping_agents,
    void** mapped_ptr)
{
    static const int tinyArraySize = 8;

    IS_BAD_PTR(mapped_ptr);
    if (num_agents != 0) IS_BAD_PTR(mapping_agents);

    core::IAgent** core_agents = nullptr;
    if (num_agents > tinyArraySize)
        core_agents = new core::IAgent*[num_agents];
    else
        core_agents = (core::IAgent**)alloca(sizeof(core::Agent*) * num_agents);
    if (core_agents == NULL) return ERROR_OUT_OF_RESOURCES;
    MAKE_SCOPE_GUARD([&]() {
        if (num_agents > tinyArraySize) delete[] core_agents;
    });

    for (uint32_t i = 0; i < num_agents; i++) {
        const core::IAgent* agent = mapping_agents[i];
        IS_VALID(agent);
        core_agents[i] = const_cast<core::IAgent*>(agent);
    }

    return Runtime::runtime_singleton_->IPCAttach(ipc, len, num_agents, core_agents,
        mapped_ptr);
}

status_t hsa_amd_ipc_memory_detach(void* mapped_ptr)
{

    IS_BAD_PTR(mapped_ptr);
    return Runtime::runtime_singleton_->IPCDetach(mapped_ptr);
}
/*
status_t hsa_amd_ipc_signal_create(signal_t hsa_signal, hsa_amd_ipc_signal_t* handle) {

  IS_OPEN();
  IS_BAD_PTR(handle);
  signal_t signal = core::Signal::Object(hsa_signal);
  IS_VALID(signal);
  core::IIPCSignal::CreateHandle(signal, handle);
  return SUCCESS;

}

status_t hsa_amd_ipc_signal_attach(const hsa_amd_ipc_signal_t* handle,
                                       signal_t* hsa_signal) {

  IS_OPEN();
  IS_BAD_PTR(handle);
  IS_BAD_PTR(hsa_signal);
  signal_t signal = core::IPCSignal::Attach(handle);
  *hsa_signal = core::ISignal::Object(signal);
  return SUCCESS;

}
*/

// For use by tools only - not in library export table.
/*
status_t hsa_amd_queue_intercept_create(
core::IAgent* agent_handle, uint32_t size, queue_type32_t type,
    void (*callback)(status_t status, queue_t* source, void* data), void* data,
    uint32_t private_segment_size, uint32_t group_segment_size, queue_t** queue) {

  IS_OPEN();
  IS_BAD_PTR(queue);
  queue_t* lower_queue;
  status_t err = hsa_queue_create(agent_handle, size, type, callback, data,
                                           private_segment_size, group_segment_size, &lower_queue);
  if (err != SUCCESS) return err;
  std::unique_ptr<core::IQueue> lowerQueue(core::Queue::Object(lower_queue));

  std::unique_ptr<core::InterceptQueue> upperQueue(new core::InterceptQueue(std::move(lowerQueue)));

  *queue = core::IQueue::Handle(upperQueue.release());
  return SUCCESS;
}

// For use by tools only - not in library export table.
status_t hsa_amd_queue_intercept_register(queue_t* queue,
                                              hsa_amd_queue_intercept_handler callback,
                                              void* user_data) {

  IS_OPEN();
  IS_BAD_PTR(callback);
  core::IQueue* cmd_queue = core::Queue::Object(queue);
  IS_VALID(cmd_queue);
  if (!core::InterceptQueue::IsType(cmd_queue)) return ERROR_INVALID_QUEUE;
  core::InterceptQueue* iQueue = static_cast<core::InterceptQueue*>(cmd_queue);
  iQueue->AddInterceptor(callback, user_data);
  return SUCCESS;

}
*/
/*
status_t hsa_amd_register_system_event_handler(hsa_amd_system_event_callback_t callback,
    void* data)
{
    return core::IRuntime::runtime_singleton_->SetCustomSystemEventHandler(callback, data);
}

status_t HSA_API hsa_amd_queue_set_priority(queue_t* queue,
    hsa_amd_queue_priority_t priority)
{
    IS_BAD_PTR(queue);
    core::IQueue* cmd_queue = core::Queue::Object(queue);
    IS_VALID(cmd_queue);

    static std::map<hsa_amd_queue_priority_t, HSA_QUEUE_PRIORITY> ext_kmt_priomap = {
        { HSA_AMD_QUEUE_PRIORITY_LOW, HSA_QUEUE_PRIORITY_MINIMUM },
        { HSA_AMD_QUEUE_PRIORITY_NORMAL, HSA_QUEUE_PRIORITY_NORMAL },
        { HSA_AMD_QUEUE_PRIORITY_HIGH, HSA_QUEUE_PRIORITY_MAXIMUM },
    };

    auto priority_it = ext_kmt_priomap.find(priority);

    if (priority_it == ext_kmt_priomap.end()) {
        return ERROR_INVALID_ARGUMENT;
    }

    return cmd_queue->SetPriority(priority_it->second);
}
*/

status_t hsa_amd_register_deallocation_callback(void* ptr,
    hsa_amd_deallocation_callback_t callback,
    void* user_data)
{
    IS_BAD_PTR(ptr);
    IS_BAD_PTR(callback);

    return Runtime::runtime_singleton_->RegisterReleaseNotifier(ptr, callback, user_data);
}

status_t hsa_amd_deregister_deallocation_callback(void* ptr,
    hsa_amd_deallocation_callback_t callback)
{
    IS_BAD_PTR(ptr);
    IS_BAD_PTR(callback);

    return Runtime::runtime_singleton_->DeregisterReleaseNotifier(ptr, callback);
}

#if 0
// For use by tools only - not in library export table.
status_t hsa_amd_runtime_queue_create_register(hsa_amd_runtime_queue_notifier callback,
    void* user_data)
{
    return Runtime::runtime_singleton_->SetInternalQueueCreateNotifier(callback, user_data);
}
#endif
