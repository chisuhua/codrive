#pragma once
// #include "StreamType.h"
#include "util/utils.h"
#include "stream_api.h"
#include "drive_api.h"
#include <cstddef>

namespace core {
    class Agent;
}

// #include "inc/pps.h"
typedef int file_t;

typedef enum {
    HSA_PROFILE_BASE = 0,
    HSA_PROFILE_FULL = 1
} profile_t;



/* @brief Struct containing an opaque handle to an executable, which contains
 * ISA for finalized kernels and indirect functions together with the allocated
 * global or readonly segment variables they reference.  */
typedef struct hsa_executable_s {
    uint64_t handle;
} hsa_executable_t;

/* The lifetime of an executable object symbol matches that of the executable
 * associated with it. An operation on a symbol whose associated executable has
 * been destroyed results in undefined behavior.  */
typedef struct hsa_executable_symbol_s {
    uint64_t handle;
} hsa_executable_symbol_t;

// @brief Loaded code object handle.
typedef struct hsa_loaded_code_object_s {
    uint64_t handle;
} hsa_loaded_code_object_t;

/**
 * @brief Code object reader handle. A code object reader is used to
 * load a code object from file (when created using
 * ::hsa_code_object_reader_create_from_file), or from memory (if created using
 * ::hsa_code_object_reader_create_from_memory).
 */
typedef struct hsa_code_object_reader_s {
    uint64_t handle;
} hsa_code_object_reader_t;

typedef enum {
    // Profile this executable is created for. The type of this attribute is _profile_t.
    HSA_EXECUTABLE_INFO_PROFILE = 1,
    // Executable state. The type of this attribute is ::hsa_executable_state_t.
    HSA_EXECUTABLE_INFO_STATE = 2,
    /* Default floating-point rounding mode specified when executable was created.
   * The type of this attribute is ::hsa_default_float_rounding_mode_t.  */
    HSA_EXECUTABLE_INFO_DEFAULT_FLOAT_ROUNDING_MODE = 3
} hsa_executable_info_t;

typedef enum {
    // The kind of the symbol. The type of this attribute is ::hsa_symbol_kind_t.
    HSA_EXECUTABLE_SYMBOL_INFO_TYPE = 0,
    // The length of the symbol name in bytes, not including the NUL terminator.  * The type of this attribute is uint32_t.
    HSA_EXECUTABLE_SYMBOL_INFO_NAME_LENGTH = 1,
    /* The name of the symbol. The type of this attribute is character array with
   * the length equal to the value of ::HSA_EXECUTABLE_SYMBOL_INFO_NAME_LENGTH
   * attribute.  */
    HSA_EXECUTABLE_SYMBOL_INFO_NAME = 2,
    HSA_EXECUTABLE_SYMBOL_INFO_MODULE_NAME_LENGTH = 3,  // @depre
    /**
   * The address of the variable. The value of this attribute is undefined if
   * the symbol is not a variable. The type of this attribute is uint64_t.
   *
   * If executable's state is ::HSA_EXECUTABLE_STATE_UNFROZEN, then 0 is
   * returned.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_SIZE = 9,
    HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_ADDRESS = 21,
    // The linkage kind of the symbol. The type of this attribute is symbol_linkage_t.
    HSA_EXECUTABLE_SYMBOL_INFO_LINKAGE = 5,
    // Indicates whether the symbol corresponds to a definition. The type of this attribute is bool.
    HSA_EXECUTABLE_SYMBOL_INFO_IS_DEFINITION = 17,
    HSA_EXECUTABLE_SYMBOL_INFO_VARIABLE_IS_CONST = 10,
    HSA_EXECUTABLE_SYMBOL_INFO_AGENT = 20,
    /**
   * Kernel object handle, used in the kernel dispatch packet. The value of this
   * attribute is undefined if the symbol is not a kernel. The type of this
   * attribute is uint64_t.
   *
   * If the state of the executable is ::HSA_EXECUTABLE_STATE_UNFROZEN, then 0
   * is returned.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT = 22,
    /**
   * Size of kernarg segment memory that is required to hold the values of the
   * kernel arguments, in bytes. Must be a multiple of 16. The value of this
   * attribute is undefined if the symbol is not a kernel. The type of this
   * attribute is uint32_t.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE = 11,
    /**
   * Alignment (in bytes) of the buffer used to pass arguments to the kernel,
   * which is the maximum of 16 and the maximum alignment of any of the kernel
   * arguments. The value of this attribute is undefined if the symbol is not a
   * kernel. The type of this attribute is uint32_t.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_ALIGNMENT = 12,
    /**
   * Size of static group segment memory required by the kernel (per
   * work-group), in bytes. The value of this attribute is undefined
   * if the symbol is not a kernel. The type of this attribute is uint32_t.
   *
   * The reported amount does not include any dynamically allocated group
   * segment memory that may be requested by the application when a kernel is
   * dispatched.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE = 13,
    /**
   * Size of static private, spill, and arg segment memory required by
   * this kernel (per work-item), in bytes. The value of this attribute is
   * undefined if the symbol is not a kernel. The type of this attribute is
   * uint32_t.
   *
   * If the value of ::HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK is
   * true, the kernel may use more private memory than the reported value, and
   * the application must add the dynamic call stack usage to @a
   * private_segment_size when populating a kernel dispatch packet.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE = 14,
    /**
   * Dynamic callstack flag. The value of this attribute is undefined if the
   * symbol is not a kernel. The type of this attribute is bool.
   *
   * If this flag is set (the value is true), the kernel uses a dynamically
   * sized call stack. This can happen if recursive calls, calls to indirect
   * functions, or the HSAIL alloca instruction are present in the kernel.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK = 15,
    /**
   * Indirect function object handle. The value of this attribute is undefined
   * if the symbol is not an indirect function, or the associated agent does
   * not support the Full Profile. The type of this attribute depends on the
   * machine model: the type is uint32_t for small machine model, and uint64_t
   * for large model.
   *
   * If the state of the executable is ::HSA_EXECUTABLE_STATE_UNFROZEN, then 0
   * is returned.
   */
    HSA_EXECUTABLE_SYMBOL_INFO_INDIRECT_FUNCTION_OBJECT = 23,
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_CTRL = 24,
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_MODE = 25
} hsa_executable_symbol_info_t;

//===--- Executable -----------------------------------------------------===//

// status_t hsa_code_object_reader_create_from_file(
//     file_t file,
//     hsa_code_object_reader_t* code_object_reader);

/**
 * @brief Create an empty executable.
 *
 */
status_t hsa_executable_create_alt(
    profile_t profile,
    hsa_default_float_rounding_mode_t default_float_rounding_mode,
    const char* options,
    hsa_executable_t* executable);

status_t hsa_executable_destroy(
    hsa_executable_t executable);

/**
 * @brief Load a program code object into an executable.
 *
 * @details A program code object contains information about resources that are
 * accessible by all kernel agents that run the executable, and can be loaded
 * at most once into an executable.
 *
 * If the program code object uses extensions, the implementation must support
 * them for this operation to return successfully.
 */
/*
status_t hsa_executable_load_program_code_object(
    hsa_executable_t executable,
    hsa_code_object_reader_t code_object_reader,
    const char* options,
    hsa_loaded_code_object_t* loaded_code_object);
    */

/**
 * @brief Load an agent code object into an executable.
 *
 * @details The agent code object contains all defined agent
 * allocation variables, functions, indirect functions, and kernels in a given
 * program for a given instruction set architecture.
 *
 * Any module linkage declaration must have been defined either by a define
 * variable or by loading a code object that has a symbol with module linkage
 * definition.
 *
 * The default floating-point rounding mode of the code object associated with
 * @p code_object_reader must match that of the executable
 * (::HSA_EXECUTABLE_INFO_DEFAULT_FLOAT_ROUNDING_MODE), or be default (in which
 * case the value of ::HSA_EXECUTABLE_INFO_DEFAULT_FLOAT_ROUNDING_MODE is used).
 * If the agent code object uses extensions, the implementation and the agent
 * must support them for this operation to return successfully.
 *
 * @param[in] executable Executable.
 *
 * @param[in] agent Agent to load code object for. A code object can be loaded
 * into an executable at most once for a given agent. The instruction set
 * architecture of the code object must be supported by the agent.
 *
 * @param[in] code_object_reader A code object reader that holds the code object
 * to load. If a code object reader is destroyed before all the associated
 * executables are destroyed, the behavior is undefined.
 *
 * @param[in] options Standard and vendor-specific options. Unknown options are
 * ignored. A standard option begins with the "-hsa_" prefix. Options beginning
 * with the "-hsa_ext_<extension_name>_" prefix are reserved for extensions. A
 * vendor-specific option begins with the "-<vendor_name>_" prefix. Must be a
 * NUL-terminated string. May be NULL.
 *
 */
/*
status_t hsa_executable_load_agent_code_object(
    hsa_executable_t executable,
    core::IAgent *agent,
    hsa_code_object_reader_t code_object_reader,
    const char* options,
    hsa_loaded_code_object_t* loaded_code_object);
    */

/**
 * @brief Freeze the executable.
 *
 * @details No modifications to executable can be made after freezing: no code
 * objects can be loaded to the executable, and no external variables can be
 * defined. Freezing the executable does not prevent querying the executable's
 * attributes. The application must define all the external variables in an
 * executable before freezing it.
 *
 * @param[in] executable Executable.
 *
 * @param[in] options Standard and vendor-specific options. Unknown options are
 * ignored. A standard option begins with the "-hsa_" prefix. Options beginning
 * with the "-hsa_ext_<extension_name>_" prefix are reserved for extensions. A
 * vendor-specific option begins with the "-<vendor_name>_" prefix. Must be a
 * NUL-terminated string. May be NULL.
 *
 */
// status_t hsa_executable_freeze(
//     hsa_executable_t executable,
//     const char* options);

/**
 * @brief Get the current value of an attribute for a given executable.
 *
 */
status_t hsa_executable_get_info(
    hsa_executable_t executable,
    hsa_executable_info_t attribute,
    void* value);

/**
 * @brief Define an external global variable with program allocation.
 *
 */
status_t hsa_executable_global_variable_define(
    hsa_executable_t executable,
    const char* variable_name,
    void* address);

/**
 * @brief Define an external global variable with agent allocation.
 *
 * @details This function allows the application to provide the definition
 * of a variable in the global segment memory with agent allocation. The
 * variable must be defined before loading a code object into an executable.
 * In addition, code objects loaded must not define the variable.
 *
 */
status_t hsa_executable_agent_global_variable_define(
    hsa_executable_t executable,
    core::IAgent* agent,
    const char* variable_name,
    void* address);

/**
 * @brief Define an external readonly variable.
 *
 * @details This function allows the application to provide the definition
 * of a variable in the readonly segment memory. The variable must be defined
 * before loading a code object into an executable. In addition, code objects
 * loaded must not define the variable.
 *
 */
status_t hsa_executable_readonly_variable_define(
    hsa_executable_t executable,
    core::IAgent* agent,
    const char* variable_name,
    void* address);
/**
 * @brief Validate an executable. Checks that all code objects have matching
 * machine model, profile, and default floating-point rounding mode. Checks that
 * all declarations have definitions. Checks declaration-definition
 * compatibility (see the HSA Programming Reference Manual for compatibility
 * rules). Invoking this function is equivalent to invoking
 * ::hsa_executable_validate_alt with no options.
 *
 */
status_t hsa_executable_validate(
    hsa_executable_t executable,
    uint32_t* result);

/**
 * @brief Validate an executable. Checks that all code objects have matching
 * machine model, profile, and default floating-point rounding mode. Checks that
 * all declarations have definitions. Checks declaration-definition
 * compatibility (see the HSA Programming Reference Manual for compatibility
 * rules).
 *
 */
status_t hsa_executable_validate_alt(
    hsa_executable_t executable,
    const char* options,
    uint32_t* result);

/**
 * @brief Retrieve the symbol handle corresponding to a given a symbol name.
 *
 */
status_t hsa_executable_get_symbol_by_name(
    hsa_executable_t executable,
    const char* symbol_name,
    const device_t* agent,
    hsa_executable_symbol_t* symbol);

/**
 * @brief Get the current value of an attribute for a given executable symbol.
 *
 */
status_t hsa_executable_symbol_get_info(
    hsa_executable_symbol_t executable_symbol,
    hsa_executable_symbol_info_t attribute,
    void* value);

/**
 * @brief Iterate over the kernels, indirect functions, and agent allocation
 * variables in an executable for a given agent, and invoke an application-
 * defined callback on every iteration.
 *
 */

status_t hsa_executable_iterate_agent_symbols(
    hsa_executable_t executable,
    core::IAgent* agent,
    status_t (*callback)(hsa_executable_t exec,
        core::IAgent* agent,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data);

/**
 * @brief Iterate over the program allocation variables in an executable, and
 * invoke an application-defined callback on every iteration.
 *
 */

status_t hsa_executable_iterate_program_symbols(
    hsa_executable_t executable,
    status_t (*callback)(hsa_executable_t exec,
        hsa_executable_symbol_t symbol,
        void* data),
    void* data);

/**
 * @brief Queries equivalent host address for given @p device_address, and
 * records it in @p host_address.
 *
 *
 * @details Contents of memory pointed to by @p host_address would be identical
 * to contents of memory pointed to by @p device_address. Only difference
 * between the two is host accessibility: @p host_address is always accessible
 * from host, @p device_address might not be accessible from host.
 *
 * If @p device_address already points to host accessible memory, then the value
 * of @p device_address is simply copied into @p host_address.
 *
 * The lifetime of @p host_address is the same as the lifetime of @p
 * device_address, and both lifetimes are limited by the lifetime of the
 * executable that is managing these addresses.
 *
 *
 * @param[in] device_address Device address to query equivalent host address
 * for.
 *
 * @param[out] host_address Pointer to application-allocated buffer to record
 * queried equivalent host address in.
 *
 *
 * @retval SUCCESS Function is executed successfully.
 *
 * @retval ERROR_NOT_INITIALIZED Runtime is not initialized.
 *
 * @retval ERROR_INVALID_ARGUMENT @p device_address is invalid or
 * null, or @p host_address is null.
 */
status_t hsa_ven_amd_loader_query_host_address(
  const void *device_address,
  const void **host_address);

/**
 * @brief The storage type of the code object that is backing loaded memory
 * segment.
 */
typedef enum {
  /**
   * Loaded memory segment is not backed by any code object (anonymous), as the
   * case would be with BSS (uninitialized data).
   */
  HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_NONE = 0,
  /**
   * Loaded memory segment is backed by the code object that is stored in the
   * file.
   */
  HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_FILE = 1,
  /**
   * Loaded memory segment is backed by the code object that is stored in the
   * memory.
   */
  HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY = 2
} hsa_ven_amd_loader_code_object_storage_type_t;

/**
 * @brief Loaded memory segment descriptor.
 *
 *
 * @details Loaded memory segment descriptor describes underlying loaded memory
 * segment. Loaded memory segment is created/allocated by the executable during
 * the loading of the code object that is backing underlying memory segment.
 *
 * The lifetime of underlying memory segment is limited by the lifetime of the
 * executable that is managing underlying memory segment.
 */
typedef struct hsa_ven_amd_loader_segment_descriptor_s {
  /**
   * Agent underlying memory segment is allocated on. If the code object that is
   * backing underlying memory segment is program code object, then 0.
   */
  core::IAgent* agent;
  /**
   * Executable that is managing this underlying memory segment.
   */
  hsa_executable_t executable;
  /**
   * Storage type of the code object that is backing underlying memory segment.
   */
  hsa_ven_amd_loader_code_object_storage_type_t code_object_storage_type;
  /**
   * If the storage type of the code object that is backing underlying memory
   * segment is:
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_NONE, then null;
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_FILE, then null-terminated
   *     filepath to the code object;
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY, then host
   *     accessible pointer to the first byte of the code object.
   */
  const void *code_object_storage_base;
  /**
   * If the storage type of the code object that is backing underlying memory
   * segment is:
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_NONE, then 0;
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_FILE, then the length of
   *     the filepath to the code object (including null-terminating character);
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY, then the size, in
   *     bytes, of the memory occupied by the code object.
   */
  size_t code_object_storage_size;
  /**
   * If the storage type of the code object that is backing underlying memory
   * segment is:
   *   - HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_NONE, then 0;
   *   - other, then offset, in bytes, from the beginning of the code object to
   *     the first byte in the code object data is copied from.
   */
  size_t code_object_storage_offset;
  /**
   * Starting address of the underlying memory segment.
   */
  const void *segment_base;
  /**
   * Size, in bytes, of the underlying memory segment.
   */
  size_t segment_size;
} hsa_ven_amd_loader_segment_descriptor_t;

/**
 * @brief Either queries loaded memory segment descriptors, or total number of
 * loaded memory segment descriptors.
 *
 *
 * @details If @p segment_descriptors is not null and @p num_segment_descriptors
 * points to number that exactly matches total number of loaded memory segment
 * descriptors, then queries loaded memory segment descriptors, and records them
 * in @p segment_descriptors. If @p segment_descriptors is null and @p
 * num_segment_descriptors points to zero, then queries total number of loaded
 * memory segment descriptors, and records it in @p num_segment_descriptors. In
 * all other cases returns appropriate error code (see below).
 *
 * The caller of this function is responsible for the allocation/deallocation
 * and the lifetime of @p segment_descriptors and @p num_segment_descriptors.
 *
 * The lifetime of loaded memory segments that are described by queried loaded
 * memory segment descriptors is limited by the lifetime of the executable that
 * is managing loaded memory segments.
 *
 * Queried loaded memory segment descriptors are always self-consistent: they
 * describe a complete set of loaded memory segments that are being backed by
 * fully loaded code objects that are present at the time (i.e. this function
 * is blocked until all executable manipulations are fully complete).
 *
 *
 * @param[out] segment_descriptors Pointer to application-allocated buffer to
 * record queried loaded memory segment descriptors in. Can be null if @p
 * num_segment_descriptors points to zero.
 *
 * @param[in,out] num_segment_descriptors Pointer to application-allocated
 * buffer that contains either total number of loaded memory segment descriptors
 * or zero.
 *
 *
 * @retval SUCCESS Function is executed successfully.
 *
 * @retval ERROR_NOT_INITIALIZED Runtime is not initialized.
 *
 * @retval ERROR_INVALID_ARGUMENT @p segment_descriptors is null
 * while @p num_segment_descriptors points to non-zero number, @p
 * segment_descriptors is not null while @p num_segment_descriptors points to
 * zero, or @p num_segment_descriptors is null.
 *
 * @retval ERROR_INCOMPATIBLE_ARGUMENTS @p num_segment_descriptors
 * does not point to number that exactly matches total number of loaded memory
 * segment descriptors.
 */
status_t hsa_ven_amd_loader_query_segment_descriptors(
  hsa_ven_amd_loader_segment_descriptor_t *segment_descriptors,
  size_t *num_segment_descriptors);

/**
 * @brief Obtains the handle of executable to which the device address belongs.
 *
 * @details This method should not be used to obtain executable handle by using
 * a host address. The executable returned is expected to be alive until its
 * destroyed by the user.
 *
 * @retval SUCCESS Function is executed successfully.
 *
 * @retval ERROR_NOT_INITIALIZED Runtime is not initialized.
 *
 * @retval ERROR_INVALID_ARGUMENT The input is invalid or there
 * is no exectuable found for this kernel code object.
 */
status_t hsa_ven_amd_loader_query_executable(
  const void *device_address,
  hsa_executable_t *executable);

//===----------------------------------------------------------------------===//

/**
 * @brief Iterate over the loaded code objects in an executable, and invoke
 * an application-defined callback on every iteration.
 *
 * @param[in] executable Executable.
 *
 * @param[in] callback Callback to be invoked once per loaded code object. The
 * HSA runtime passes three arguments to the callback: the executable, a
 * loaded code object, and the application data.  If @p callback returns a
 * status other than ::SUCCESS for a particular iteration, the
 * traversal stops and ::hsa_executable_iterate_symbols returns that status
 * value.
 *
 * @param[in] data Application data that is passed to @p callback on every
 * iteration. May be NULL.
 *
 * @retval ::SUCCESS The function has been executed successfully.
 *
 * @retval ::ERROR_NOT_INITIALIZED The HSA runtime has not been
 * initialized.
 *
 * @retval ::ERROR_INVALID_EXECUTABLE The executable is invalid.
 *
 * @retval ::ERROR_INVALID_ARGUMENT @p callback is NULL.
 */
status_t hsa_ven_amd_loader_executable_iterate_loaded_code_objects(
  hsa_executable_t executable,
  status_t (*callback)(
    hsa_executable_t executable,
    hsa_loaded_code_object_t loaded_code_object,
    void *data),
  void *data);

/**
 * @brief Loaded code object kind.
 */
typedef enum {
  /**
   * Program code object.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_KIND_PROGRAM = 1,
  /**
   * Agent code object.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_KIND_AGENT = 2
} hsa_ven_amd_loader_loaded_code_object_kind_t;

/**
 * @brief Loaded code object attributes.
 */
typedef enum hsa_ven_amd_loader_loaded_code_object_info_e {
  /**
   * The executable in which this loaded code object is loaded. The
   * type of this attribute is ::hsa_executable_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_EXECUTABLE = 1,
  /**
   * The kind of this loaded code object. The type of this attribute is
   * ::uint32_t interpreted as ::hsa_ven_amd_loader_loaded_code_object_kind_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_KIND = 2,
  /**
   * The agent on which this loaded code object is loaded. The
   * value of this attribute is only defined if
   * ::HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_KIND is
   * ::HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_KIND_AGENT. The type of this
   * attribute is ::device_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_AGENT = 3,
  /**
   * The storage type of the code object reader used to load the loaded code object.
   * The type of this attribute is ::uint32_t interpreted as a
   * ::hsa_ven_amd_loader_code_object_storage_type_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_TYPE = 4,
  /**
   * The memory address of the first byte of the code object that was loaaded.
   * The value of this attribute is only defined if
   * ::HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_TYPE is
   * ::HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY. The type of this
   * attribute is ::uint64_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_MEMORY_BASE = 5,
  /**
   * The memory size in bytes of the code object that was loaaded.
   * The value of this attribute is only defined if
   * ::HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_TYPE is
   * ::HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_MEMORY. The type of this
   * attribute is ::uint64_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_MEMORY_SIZE = 6,
  /**
   * The file descriptor of the code object that was loaaded.
   * The value of this attribute is only defined if
   * ::HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_TYPE is
   * ::HSA_VEN_AMD_LOADER_CODE_OBJECT_STORAGE_TYPE_FILE. The type of this
   * attribute is ::int.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_CODE_OBJECT_STORAGE_FILE = 7,
    /**
   * The signed byte address difference of the memory address at which the code
   * object is loaded minus the virtual address specified in the code object
   * that is loaded. The value of this attribute is only defined if the
   * executable in which the code object is loaded is froozen. The type of this
   * attribute is ::int64_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_DELTA = 8,
/**
   * The base memory address at which the code object is loaded. This is the
   * base address of the allocation for the lowest addressed segment of the code
   * object that is loaded. Note that any non-loaded segments before the first
   * loaded segment are ignored. The value of this attribute is only defined if
   * the executable in which the code object is loaded is froozen. The type of
   * this attribute is ::uint64_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_BASE = 9,
  /**
   * The byte size of the loaded code objects contiguous memory allocation. The
   * value of this attribute is only defined if the executable in which the code
   * object is loaded is froozen. The type of this attribute is ::uint64_t.
   */
  HSA_VEN_AMD_LOADER_LOADED_CODE_OBJECT_INFO_LOAD_SIZE = 10
} hsa_ven_amd_loader_loaded_code_object_info_t;

/**
 * @brief Get the current value of an attribute for a given loaded code
 * object.
 *
 * @param[in] loaded_code_object Loaded code object.
 *
 * @param[in] attribute Attribute to query.
 *
 * @param[out] value Pointer to an application-allocated buffer where to store
 * the value of the attribute. If the buffer passed by the application is not
 * large enough to hold the value of @p attribute, the behavior is undefined.
 *
 * @retval ::SUCCESS The function has been executed successfully.
 *
 * @retval ::ERROR_NOT_INITIALIZED The HSA runtime has not been
 * initialized.
 *
 * @retval ::ERROR_INVALID_CODE_OBJECT The loaded code object is
 * invalid.
 *
 * @retval ::ERROR_INVALID_ARGUMENT @p attribute is an invalid
 * loaded code object attribute, or @p value is NULL.
 */
status_t hsa_ven_amd_loader_loaded_code_object_get_info(
  hsa_loaded_code_object_t loaded_code_object,
  hsa_ven_amd_loader_loaded_code_object_info_t attribute,
  void *value);

//===----------------------------------------------------------------------===//

/**
 * @brief Extension version.
 */
#define hsa_ven_amd_loader 001000

/**
 * @brief Extension function table version 1.00.
 */
typedef struct hsa_ven_amd_loader_1_00_pfn_s {
  status_t (*hsa_ven_amd_loader_query_host_address)(
    const void *device_address,
    const void **host_address);

  status_t (*hsa_ven_amd_loader_query_segment_descriptors)(
    hsa_ven_amd_loader_segment_descriptor_t *segment_descriptors,
    size_t *num_segment_descriptors);

  status_t (*hsa_ven_amd_loader_query_executable)(
    const void *device_address,
    hsa_executable_t *executable);
} hsa_ven_amd_loader_1_00_pfn_t;

/**
 * @brief Extension function table version 1.01.
 */
typedef struct hsa_ven_amd_loader_1_01_pfn_s {
  status_t (*hsa_ven_amd_loader_query_host_address)(
    const void *device_address,
    const void **host_address);

  status_t (*hsa_ven_amd_loader_query_segment_descriptors)(
    hsa_ven_amd_loader_segment_descriptor_t *segment_descriptors,
    size_t *num_segment_descriptors);

  status_t (*hsa_ven_amd_loader_query_executable)(
    const void *device_address,
    hsa_executable_t *executable);

  status_t (*hsa_ven_amd_loader_executable_iterate_loaded_code_objects)(
    hsa_executable_t executable,
    status_t (*callback)(
      hsa_executable_t executable,
      hsa_loaded_code_object_t loaded_code_object,
      void *data),
    void *data);

  status_t (*hsa_ven_amd_loader_loaded_code_object_get_info)(
    hsa_loaded_code_object_t loaded_code_object,
    hsa_ven_amd_loader_loaded_code_object_info_t attribute,
    void *value);
} hsa_ven_amd_loader_1_01_pfn_t;

/**
 * @brief Symbol type.
 */
typedef enum {
  HSA_SYMBOL_KIND_VARIABLE = 0,
  HSA_SYMBOL_KIND_KERNEL = 1,
  HSA_SYMBOL_KIND_INDIRECT_FUNCTION = 2
} hsa_symbol_kind_t;

typedef enum {
  HSA_SYMBOL_LINKAGE_MODULE = 0,
  HSA_SYMBOL_LINKAGE_PROGRAM = 1
} hsa_symbol_linkage_t;

typedef enum {
  HSA_VARIABLE_ALLOCATION_AGENT = 0,
  HSA_VARIABLE_ALLOCATION_PROGRAM = 1
} hsa_variable_allocation_t;

typedef enum {
  HSA_CODE_SYMBOL_INFO_TYPE = 0,
  HSA_CODE_SYMBOL_INFO_NAME_LENGTH = 1,
  HSA_CODE_SYMBOL_INFO_NAME = 2,
  HSA_CODE_SYMBOL_INFO_MODULE_NAME_LENGTH = 3,
  HSA_CODE_SYMBOL_INFO_MODULE_NAME = 4,
  HSA_CODE_SYMBOL_INFO_LINKAGE = 5,
  HSA_CODE_SYMBOL_INFO_IS_DEFINITION = 17,
  HSA_CODE_SYMBOL_INFO_VARIABLE_ALLOCATION = 6,
  HSA_CODE_SYMBOL_INFO_VARIABLE_SEGMENT = 7,
  HSA_CODE_SYMBOL_INFO_VARIABLE_ALIGNMENT = 8,
  HSA_CODE_SYMBOL_INFO_VARIABLE_SIZE = 9,
  HSA_CODE_SYMBOL_INFO_VARIABLE_IS_CONST = 10,
  HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE = 11,
  HSA_CODE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_ALIGNMENT = 12,
  HSA_CODE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE = 13,
  HSA_CODE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE = 14,
  HSA_CODE_SYMBOL_INFO_KERNEL_DYNAMIC_CALLSTACK = 15,
  HSA_CODE_SYMBOL_INFO_KERNEL_CALL_CONVENTION = 18,
  HSA_CODE_SYMBOL_INFO_INDIRECT_FUNCTION_CALL_CONVENTION = 16,
  HSA_CODE_SYMBOL_INFO_KERNEL_CTRL = 24,
  HSA_CODE_SYMBOL_INFO_KERNEL_MODE = 25
} hsa_code_symbol_info_t;

typedef enum {
  /** * Executable state, which allows the user to load code objects and define
   * external variables. Variable addresses, kernel code handles, and
   * indirect function code handles are not available in query operations until
   * the executable is frozen (zero always returned).  */
  HSA_EXECUTABLE_STATE_UNFROZEN = 0,
  /** * Executable state, which allows the user to query variable addresses,
   * kernel code handles, and indirect function code handles using query
   * operations. Loading new code objects, as well as defining external
   * variables, is not allowed in this state.  */
  HSA_EXECUTABLE_STATE_FROZEN = 1
} hsa_executable_state_t;


typedef struct hsa_code_symbol_s {
  uint64_t handle;
} hsa_code_symbol_t;


typedef struct hsa_code_object_s {
  uint64_t handle;
} hsa_code_object_t;

// TODO: this struct should be completely gone once debugger designs/implements
// Debugger APIs.
typedef struct loader_debug_info_s {
  const void* elf_raw;
  size_t elf_size;
  const char *kernel_name;
  const void *owning_segment;
} loader_debug_info_t;

