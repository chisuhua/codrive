#include "inc/codeobject_util.h"
#include "libelf.h"
#include <fstream>
#include <cstring>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <process.h>
#else // _WIN32
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif // _WIN32
// TODO schi #include "Brig.h"

namespace {
auto eq = " = ";

std::ostream& attr1(std::ostream& out)
{
  out << "  " << std::left << std::setw(60) << std::setfill(' ');
  return out;
}

std::ostream& attr2(std::ostream& out)
{
  out << "    " << std::left << std::setw(58) << std::setfill(' ');
  return out;
}
} // namespace anonymous

// namespace amd {
// namespace hsa {
namespace hcs {
namespace common {

bool IsAccessibleMemoryAddress(uint64_t address)
{
  if (0 == address) {
    return false;
  }
#if defined(_WIN32) || defined(_WIN64)
    MEMORY_BASIC_INFORMATION memory_info;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory_info, sizeof(memory_info))) {
      return false;
    }
    int32_t is_accessible = ((memory_info.Protect & PAGE_READONLY) ||
                             (memory_info.Protect & PAGE_READWRITE) ||
                             (memory_info.Protect & PAGE_WRITECOPY) ||
                             (memory_info.Protect & PAGE_EXECUTE_READ) ||
                             (memory_info.Protect & PAGE_EXECUTE_READWRITE) ||
                             (memory_info.Protect & PAGE_EXECUTE_WRITECOPY));
    if (memory_info.Protect & PAGE_GUARD) {
      is_accessible = 0;
    }
    if (memory_info.Protect & PAGE_NOACCESS) {
      is_accessible = 0;
    }
    return is_accessible > 0;
#else
  int32_t random_fd = 0;
  ssize_t bytes_written = 0;
  if (-1 == (random_fd = open("/dev/random", O_WRONLY))) {
    return false;
  }
  bytes_written = write(random_fd, (void*)address, 1);
  if (-1 == close(random_fd)) {
    return false;
  }
  return bytes_written == 1;
#endif // _WIN32 || _WIN64
}

}

std::string HsaSymbolKindToString(hsa_symbol_kind_t kind)
{
  switch (kind) {
  case HSA_SYMBOL_KIND_VARIABLE: return "VARIABLE";
  case HSA_SYMBOL_KIND_INDIRECT_FUNCTION: return "INDIRECT_FUNCTION";
  case HSA_SYMBOL_KIND_KERNEL: return "KERNEL";
  default: return "UNKNOWN";
  }
}

std::string HsaSymbolLinkageToString(hsa_symbol_linkage_t linkage)
{
  switch (linkage) {
  case HSA_SYMBOL_LINKAGE_MODULE: return "MODULE";
  case HSA_SYMBOL_LINKAGE_PROGRAM: return "PROGRAM";
  default: return "UNKNOWN";
  }
}

std::string HsaVariableAllocationToString(hsa_variable_allocation_t allocation)
{
  switch (allocation) {
  case HSA_VARIABLE_ALLOCATION_AGENT: return "AGENT";
  case HSA_VARIABLE_ALLOCATION_PROGRAM: return "PROGRAM";
  default: return "UNKNOWN";
  }
}

std::string HsaVariableSegmentToString(hsa_variable_segment_t segment)
{
  switch (segment) {
  case HSA_VARIABLE_SEGMENT_GLOBAL: return "GLOBAL";
  case HSA_VARIABLE_SEGMENT_READONLY: return "READONLY";
  default: return "UNKNOWN";
  }
}

std::string HsaProfileToString(profile_t profile)
{
  switch (profile) {
  case HSA_PROFILE_BASE: return "BASE";
  case HSA_PROFILE_FULL: return "FULL";
  default: return "UNKNOWN";
  }
}

std::string HsaMachineModelToString(hsa_machine_model_t model)
{
  switch (model) {
  case HSA_MACHINE_MODEL_SMALL: return "SMALL";
  case HSA_MACHINE_MODEL_LARGE: return "LARGE";
  default: return "UNKNOWN";
  }
}

std::string HsaFloatRoundingModeToString(hsa_default_float_rounding_mode_t mode)
{
  switch (mode) {
  case HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT: return "DEFAULT";
  case HSA_DEFAULT_FLOAT_ROUNDING_MODE_ZERO: return "ZERO";
  case HSA_DEFAULT_FLOAT_ROUNDING_MODE_NEAR: return "NEAR";
  default: return "UNKNOWN";
  }
}

std::string HcsMachineKindToString(hcs_machine_kind16_t machine)
{
  switch (machine) {
  case HCS_MACHINE_KIND_UNDEFINED: return "UNDEFINED";
  case HCS_MACHINE_KIND_DLA: return "HCS_DLA";
  default: return "UNKNOWN";
  }
}

std::string AmdFloatRoundModeToString(amd_float_round_mode_t round_mode)
{
  switch (round_mode) {
  case AMD_FLOAT_ROUND_MODE_NEAREST_EVEN: return "NEAREST_EVEN";
  case AMD_FLOAT_ROUND_MODE_PLUS_INFINITY: return "PLUS_INFINITY";
  case AMD_FLOAT_ROUND_MODE_MINUS_INFINITY: return "MINUS_INFINITY";
  case AMD_FLOAT_ROUND_MODE_ZERO: return "ZERO";
  default: return "UNKNOWN";
  }
}

std::string AmdFloatDenormModeToString(amd_float_denorm_mode_t denorm_mode)
{
  switch (denorm_mode) {
  case AMD_FLOAT_DENORM_MODE_FLUSH_SOURCE_OUTPUT: return "FLUSH_SOURCE_OUTPUT";
  case AMD_FLOAT_DENORM_MODE_FLUSH_OUTPUT: return "FLUSH_OUTPUT";
  case AMD_FLOAT_DENORM_MODE_FLUSH_SOURCE: return "FLUSH_SOURCE";
  case AMD_FLOAT_DENORM_MODE_NO_FLUSH: return "FLUSH_NONE";
  default: return "UNKNOWN";
  }
}

std::string AmdSystemVgprWorkitemIdToString(amd_system_vgpr_workitem_id_t system_vgpr_workitem_id)
{
  switch (system_vgpr_workitem_id) {
  case AMD_SYSTEM_VGPR_WORKITEM_ID_X: return "X";
  case AMD_SYSTEM_VGPR_WORKITEM_ID_X_Y: return "X, Y";
  case AMD_SYSTEM_VGPR_WORKITEM_ID_X_Y_Z: return "X, Y, Z";
  default: return "UNKNOWN";
  }
}

std::string AmdElementByteSizeToString(amd_element_byte_size_t element_byte_size)
{
  switch (element_byte_size) {
  case AMD_ELEMENT_BYTE_SIZE_2: return "WORD (2 bytes)";
  case AMD_ELEMENT_BYTE_SIZE_4: return "DWORD (4 bytes)";
  case AMD_ELEMENT_BYTE_SIZE_8: return "QWORD (8 bytes)";
  case AMD_ELEMENT_BYTE_SIZE_16: return "16 bytes";
  default: return "UNKNOWN";
  }
}

std::string AmdExceptionKindToString(amd_exception_kind16_t exceptions)
{
  std::string e;
  if (exceptions & AMD_EXCEPTION_KIND_INVALID_OPERATION) {
    e += ", INVALID_OPERATON";
    exceptions &= ~AMD_EXCEPTION_KIND_INVALID_OPERATION;
  }
  if (exceptions & AMD_EXCEPTION_KIND_DIVISION_BY_ZERO) {
    e += ", DIVISION_BY_ZERO";
    exceptions &= ~AMD_EXCEPTION_KIND_DIVISION_BY_ZERO;
  }
  if (exceptions & AMD_EXCEPTION_KIND_OVERFLOW) {
    e += ", OVERFLOW";
    exceptions &= ~AMD_EXCEPTION_KIND_OVERFLOW;
  }
  if (exceptions & AMD_EXCEPTION_KIND_UNDERFLOW) {
    e += ", UNDERFLOW";
    exceptions &= ~AMD_EXCEPTION_KIND_UNDERFLOW;
  }
  if (exceptions & AMD_EXCEPTION_KIND_INEXACT) {
    e += ", INEXACT";
    exceptions &= ~AMD_EXCEPTION_KIND_INEXACT;
  }
  if (exceptions) {
    e += ", UNKNOWN";
  }
  if (!e.empty()) {
    e = "[" + e.erase(0, 2) + "]";
  }
  return e;
}

std::string AmdPowerTwoToString(amd_powertwo8_t p)
{
  return std::to_string(1 << (unsigned) p);
}

amdgpu_hsa_elf_segment_t HcsElfSectionSegment(amdgpu_hsa_elf_section_t sec)
{
  switch (sec) {
  case AMDGPU_HSA_RODATA_GLOBAL_PROGRAM:
  case AMDGPU_HSA_DATA_GLOBAL_PROGRAM:
  case AMDGPU_HSA_BSS_GLOBAL_PROGRAM:
    return AMDGPU_HSA_SEGMENT_GLOBAL_PROGRAM;
  case AMDGPU_HSA_RODATA_GLOBAL_AGENT:
  case AMDGPU_HSA_DATA_GLOBAL_AGENT:
  case AMDGPU_HSA_BSS_GLOBAL_AGENT:
    return AMDGPU_HSA_SEGMENT_GLOBAL_AGENT;
  case AMDGPU_HSA_RODATA_READONLY_AGENT:
  case AMDGPU_HSA_DATA_READONLY_AGENT:
  case AMDGPU_HSA_BSS_READONLY_AGENT:
    return AMDGPU_HSA_SEGMENT_READONLY_AGENT;
  default:
    assert(false); return AMDGPU_HSA_SEGMENT_LAST;
  }
}

bool IsHcsElfSectionROData(amdgpu_hsa_elf_section_t sec)
{
  switch (sec) {
  case AMDGPU_HSA_RODATA_GLOBAL_PROGRAM:
  case AMDGPU_HSA_RODATA_GLOBAL_AGENT:
  case AMDGPU_HSA_RODATA_READONLY_AGENT:
  default:
    return false;
  }
}

std::string ElfSegmentToString(amdgpu_hsa_elf_segment_t seg)
{
  switch (seg) {
  case AMDGPU_HSA_SEGMENT_GLOBAL_PROGRAM: return "GLOBAL_PROGRAM";
  case AMDGPU_HSA_SEGMENT_GLOBAL_AGENT: return "GLOBAL_AGENT";
  case AMDGPU_HSA_SEGMENT_READONLY_AGENT: return "READONLY_AGENT";
  case AMDGPU_HSA_SEGMENT_CODE_AGENT: return "CODE_AGENT";
  default: return "UNKNOWN";
  }
}

std::string AmdPTLoadToString(uint64_t type)
{
  if (PT_LOOS <= type && type < PT_LOOS + AMDGPU_HSA_SEGMENT_LAST) {
    return HcsElfSegmentToString((amdgpu_hsa_elf_segment_t) (type - PT_LOOS));
  } else {
    return "UNKNOWN (" + std::to_string(type) + ")";
  }
}

void PrintAmdKernelCode(std::ostream& out, const hcs_kernel_code_t *akc)
{
  uint32_t is_debug_enabled = HCS_BITS_GET(akc->kernel_code_properties, AMD_KERNEL_CODE_PROPERTIES_IS_DEBUG_ENABLED);

  out << attr1 << "amd_kernel_code_version_major" << eq
      << akc->amd_kernel_code_version_major
      << std::endl;
  out << attr1 << "amd_kernel_code_version_minor" << eq
      << akc->amd_kernel_code_version_minor
      << std::endl;
  out << attr1 << "hcs_machine_kind" << eq
      << HcsMachineKindToString(akc->hcs_machine_kind)
      << std::endl;
  out << attr1 << "amd_machine_version_major" << eq
      << (uint32_t)akc->amd_machine_version_major
      << std::endl;
  out << attr1 << "amd_machine_version_minor" << eq
      << (uint32_t)akc->amd_machine_version_minor
      << std::endl;
  out << attr1 << "amd_machine_version_stepping" << eq
      << (uint32_t)akc->amd_machine_version_stepping
      << std::endl;
  out << attr1 << "kernel_code_entry_byte_offset" << eq
      << akc->kernel_code_entry_byte_offset
      << std::endl;
  if (akc->kernel_code_prefetch_byte_offset) {
    out << attr1 << "kernel_code_prefetch_byte_offset" << eq
        << akc->kernel_code_prefetch_byte_offset
        << std::endl;
  }
  if (akc->kernel_code_prefetch_byte_size) {
    out << attr1 << "kernel_code_prefetch_byte_size" << eq
        << akc->kernel_code_prefetch_byte_size
        << std::endl;
  }
  out << attr1 << "max_scratch_backing_memory_byte_size" << eq
      << akc->max_scratch_backing_memory_byte_size
      << std::endl;

  if (akc->workitem_private_segment_byte_size) {
    out << attr1 << "workitem_private_segment_byte_size" << eq
        << akc->workitem_private_segment_byte_size
        << std::endl;
  }
  if (akc->workgroup_group_segment_byte_size) {
    out << attr1 << "workgroup_group_segment_byte_size" << eq
        << akc->workgroup_group_segment_byte_size
        << std::endl;
  }
  if (akc->gds_segment_byte_size) {
    out << attr1 << "gds_segment_byte_size" << eq
        << akc->gds_segment_byte_size
        << std::endl;
  }
  if (akc->kernarg_segment_byte_size) {
    out << attr1 << "kernarg_segment_byte_size" << eq
        << akc->kernarg_segment_byte_size
        << std::endl;
  }
  if (akc->workgroup_fbarrier_count) {
    out << attr1 << "workgroup_fbarrier_count" << eq
        << akc->workgroup_fbarrier_count
        << std::endl;
  }
  out << attr1 << "wavefront_sgpr_count" << eq
      << (uint32_t)akc->wavefront_sgpr_count
      << std::endl;
  out << attr1 << "workitem_vgpr_count" << eq
      << (uint32_t)akc->workitem_vgpr_count
      << std::endl;
  if (akc->reserved_vgpr_count > 0) {
    out << attr1 << "reserved_vgpr_first" << eq
        << (uint32_t)akc->reserved_vgpr_first
        << std::endl;
    out << attr1 << "reserved_vgpr_count" << eq
        << (uint32_t)akc->reserved_vgpr_count
        << std::endl;
  }
  if (akc->reserved_sgpr_count > 0) {
    out << attr1 << "reserved_sgpr_first" << eq
        << (uint32_t)akc->reserved_sgpr_first
        << std::endl;
    out << attr1 << "reserved_sgpr_count" << eq
        << (uint32_t)akc->reserved_sgpr_count
        << std::endl;
  }
  if (is_debug_enabled && (akc->debug_wavefront_private_segment_offset_sgpr != uint16_t(-1))) {
    out << attr1 << "debug_wavefront_private_segment_offset_sgpr" << eq
        << (uint32_t)akc->debug_wavefront_private_segment_offset_sgpr
        << std::endl;
  }
  if (is_debug_enabled && (akc->debug_private_segment_buffer_sgpr != uint16_t(-1))) {
    out << attr1 << "debug_private_segment_buffer_sgpr" << eq
        << (uint32_t)akc->debug_private_segment_buffer_sgpr
        << ":"
        << (uint32_t)(akc->debug_private_segment_buffer_sgpr + 3)
        << std::endl;
  }
  if (akc->kernarg_segment_alignment) {
    out << attr1 << "kernarg_segment_alignment" << eq
        << AmdPowerTwoToString(akc->kernarg_segment_alignment)
        << " (" << (uint32_t) akc->kernarg_segment_alignment << ")"
        << std::endl;
  }
  if (akc->group_segment_alignment) {
    out << attr1 << "group_segment_alignment" << eq
        << AmdPowerTwoToString(akc->group_segment_alignment)
        << " (" << (uint32_t) akc->group_segment_alignment << ")"
        << std::endl;
  }
  if (akc->private_segment_alignment) {
    out << attr1 << "private_segment_alignment" << eq
        << AmdPowerTwoToString(akc->private_segment_alignment)
        << " (" << (uint32_t) akc->private_segment_alignment << ")"
        << std::endl;
  }
  out << attr1 << "wavefront_size" << eq
      << AmdPowerTwoToString(akc->wavefront_size)
      << " (" << (uint32_t) akc->wavefront_size << ")"
      << std::endl;
  PrintAmdControlDirectives(out, akc->control_directives);
}



void PrintAmdControlDirectives(std::ostream& out, const amd_control_directives_t &control_directives)
{
  if (!control_directives.enabled_control_directives) {
    return;
  }

  out << "  CONTROL_DIRECTIVES:" << std::endl;

  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_ENABLE_BREAK_EXCEPTIONS) {
    out << attr2 << "enable_break_exceptions" << eq
        << AmdExceptionKindToString(control_directives.enable_break_exceptions).c_str()
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_ENABLE_DETECT_EXCEPTIONS) {
    out << attr2 << "enable_detect_exceptions" << eq
        << AmdExceptionKindToString(control_directives.enable_detect_exceptions).c_str()
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_MAX_DYNAMIC_GROUP_SIZE) {
    out << attr2 << "max_dynamic_group_size" << eq
        << control_directives.max_dynamic_group_size
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_MAX_FLAT_GRID_SIZE) {
    out << attr2 << "max_flat_grid_size" << eq
        << control_directives.max_flat_grid_size
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_MAX_FLAT_WORKGROUP_SIZE) {
    out << attr2 << "max_flat_workgroup_size" << eq
        << control_directives.max_flat_workgroup_size
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_REQUIRED_DIM) {
    out << attr2 << "required_dim" << eq
        << (uint32_t)control_directives.required_dim
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_REQUIRED_GRID_SIZE) {
    out << attr2 << "required_grid_size" << eq
        << "("
        << control_directives.required_grid_size[0]
        << ", "
        << control_directives.required_grid_size[1]
        << ", "
        << control_directives.required_grid_size[2]
        << ")"
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_REQUIRED_WORKGROUP_SIZE) {
    out << attr2 << "required_workgroup_size" << eq
        << "("
        << control_directives.required_workgroup_size[0]
        << ", "
        << control_directives.required_workgroup_size[1]
        << ", "
        << control_directives.required_workgroup_size[2]
        << ")"
        << std::endl;
  }
  if (control_directives.enabled_control_directives & AMD_ENABLED_CONTROL_DIRECTIVE_REQUIRE_NO_PARTIAL_WORKGROUPS) {
    out << attr2 << "require_no_partial_workgroups" << eq << "TRUE"
        << std::endl;
  }
}

namespace code_options {

  std::ostream& space(std::ostream& out)
  {
    if (out.tellp()) { out << " "; }
    return out;
  }

  std::ostream& operator<<(std::ostream& out, const control_directive& d)
  {
    out << space <<
      "-hsa_control_directive:" << d.name << "=";
    return out;
  }
/* TODO schi Brig.h is include
  const char *BrigExceptionString(BrigExceptions32_t e)
  {
    switch (e) {
    case BRIG_EXCEPTIONS_INVALID_OPERATION: return "INVALID_OPERATION";
    case BRIG_EXCEPTIONS_DIVIDE_BY_ZERO: return "DIVIDE_BY_ZERO";
    case BRIG_EXCEPTIONS_OVERFLOW: return "OVERFLOW";
    case BRIG_EXCEPTIONS_INEXACT: return "INEXACT";
    default:
      assert(false); return "<unknown_BRIG_exception>";
    }
  }

  std::ostream& operator<<(std::ostream& out, const exceptions_mask& e)
  {
    bool first = true;
    for (BrigExceptions32_t be = BRIG_EXCEPTIONS_INVALID_OPERATION; be < BRIG_EXCEPTIONS_FIRST_USER_DEFINED; ++be) {
      if (e.mask & be) {
        if (first) { first = false; } else { out << ","; }
        out << BrigExceptionString(be);
      }
    }
    return out;
  }
  */

  std::ostream& operator<<(std::ostream& out, const control_directives& cd)
  {
    const hsa_ext_control_directives_t& d = cd.d;
    uint64_t mask = d.control_directives_mask;
    if (!mask) { return out; }

      /*
    if (mask & BRIG_CONTROL_ENABLEBREAKEXCEPTIONS) {
      out <<
        control_directive("ENABLEBREAKEXCEPTIONS") <<
        exceptions_mask(d.break_exceptions_mask);
    }
    if (mask & BRIG_CONTROL_ENABLEDETECTEXCEPTIONS) {
      out <<
        control_directive("ENABLEDETECTEXCEPTIONS") <<
        exceptions_mask(d.detect_exceptions_mask);
    }
    if (mask & BRIG_CONTROL_MAXDYNAMICGROUPSIZE) {
      out <<
        control_directive("MAXDYNAMICGROUPSIZE") <<
        d.max_dynamic_group_size;
    }
    if (mask & BRIG_CONTROL_MAXFLATGRIDSIZE) {
      out <<
        control_directive("MAXFLATGRIDSIZE") <<
        d.max_flat_grid_size;
    }
    if (mask & BRIG_CONTROL_MAXFLATWORKGROUPSIZE) {
      out <<
        control_directive("MAXFLATWORKGROUPSIZE") <<
        d.max_flat_workgroup_size;
    }
    if (mask & BRIG_CONTROL_REQUIREDDIM) {
      out <<
        control_directive("REQUIREDDIM") <<
        d.required_dim;
    }
    if (mask & BRIG_CONTROL_REQUIREDGRIDSIZE) {
      out <<
        control_directive("REQUIREDGRIDSIZE") <<
        d.required_grid_size[0] << "," <<
        d.required_grid_size[1] << "," <<
        d.required_grid_size[2];
    }
    if (mask & BRIG_CONTROL_REQUIREDWORKGROUPSIZE) {
      out <<
        control_directive("REQUIREDWORKGROUPSIZE") <<
        d.required_workgroup_size.x << "," <<
        d.required_workgroup_size.y << "," <<
        d.required_workgroup_size.z;
    }
  */
    return out;
  }
}


const char* hsaerr2str(status_t status) {
  switch ((unsigned) status) {
    case SUCCESS:
      return
          "SUCCESS: The function has been executed successfully.";
    case HSA_STATUS_INFO_BREAK:
      return
          "HSA_STATUS_INFO_BREAK: A traversal over a list of "
          "elements has been interrupted by the application before "
          "completing.";
    case ERROR:
      return "ERROR: A generic error has occurred.";
    case ERROR_INVALID_ARGUMENT:
      return
          "ERROR_INVALID_ARGUMENT: One of the actual "
          "arguments does not meet a precondition stated in the "
          "documentation of the corresponding formal argument.";
    case ERROR_INVALID_QUEUE_CREATION:
      return
          "ERROR_INVALID_QUEUE_CREATION: The requested "
          "queue creation is not valid.";
    case ERROR_INVALID_ALLOCATION:
      return
          "ERROR_INVALID_ALLOCATION: The requested "
          "allocation is not valid.";
    case ERROR_INVALID_AGENT:
      return
          "ERROR_INVALID_AGENT: The agent is invalid.";
    case ERROR_INVALID_REGION:
      return
          "ERROR_INVALID_REGION: The memory region is invalid.";
    case ERROR_INVALID_SIGNAL:
      return
          "ERROR_INVALID_SIGNAL: The signal is invalid.";
    case ERROR_INVALID_QUEUE:
      return
          "ERROR_INVALID_QUEUE: The queue is invalid.";
    case ERROR_OUT_OF_RESOURCES:
      return
          "ERROR_OUT_OF_RESOURCES: The runtime failed to "
          "allocate the necessary resources. This error may also "
          "occur when the core runtime library needs to spawn "
          "threads or create internal OS-specific events.";
    case ERROR_INVALID_PACKET_FORMAT:
      return
          "ERROR_INVALID_PACKET_FORMAT: The AQL packet "
          "is malformed.";
    case ERROR_RESOURCE_FREE:
      return
          "ERROR_RESOURCE_FREE: An error has been "
          "detected while releasing a resource.";
    case ERROR_NOT_INITIALIZED:
      return
          "ERROR_NOT_INITIALIZED: An API other than "
          "hsa_init has been invoked while the reference count of "
          "the HSA runtime is zero.";
    case ERROR_REFCOUNT_OVERFLOW:
      return
          "ERROR_REFCOUNT_OVERFLOW: The maximum "
          "reference count for the object has been reached.";
    case ERROR_INCOMPATIBLE_ARGUMENTS:
      return
          "ERROR_INCOMPATIBLE_ARGUMENTS: The arguments passed to "
          "a functions are not compatible.";
    case ERROR_INVALID_INDEX:
      return "The index is invalid.";
    case ERROR_INVALID_ISA:
      return "The instruction set architecture is invalid.";
    case ERROR_INVALID_CODE_OBJECT:
      return "The code object is invalid.";
    case ERROR_INVALID_EXECUTABLE:
      return "The executable is invalid.";
    case ERROR_FROZEN_EXECUTABLE:
      return "The executable is frozen.";
    case ERROR_INVALID_SYMBOL_NAME:
      return "There is no symbol with the given name.";
    case ERROR_VARIABLE_ALREADY_DEFINED:
      return "The variable is already defined.";
    case ERROR_VARIABLE_UNDEFINED:
      return "The variable is undefined.";
    case HSA_EXT_STATUS_ERROR_INVALID_PROGRAM:
      return
          "HSA_EXT_STATUS_ERROR_INVALID_PROGRAM: Invalid program";
    case HSA_EXT_STATUS_ERROR_INVALID_MODULE:
      return "HSA_EXT_STATUS_ERROR_INVALID_MODULE: Invalid module";
    case HSA_EXT_STATUS_ERROR_INCOMPATIBLE_MODULE:
      return
          "HSA_EXT_STATUS_ERROR_INCOMPATIBLE_MODULE: Incompatible module";
    case HSA_EXT_STATUS_ERROR_MODULE_ALREADY_INCLUDED:
      return
          "HSA_EXT_STATUS_ERROR_MODULE_ALREADY_INCLUDED: Module already "
          "included";
    case HSA_EXT_STATUS_ERROR_SYMBOL_MISMATCH:
      return
          "HSA_EXT_STATUS_ERROR_SYMBOL_MISMATCH: Symbol mismatch";
    case HSA_EXT_STATUS_ERROR_FINALIZATION_FAILED:
      return
          "HSA_EXT_STATUS_ERROR_FINALIZATION_FAILED: Finalization failed";
    case HSA_EXT_STATUS_ERROR_DIRECTIVE_MISMATCH:
      return
          "HSA_EXT_STATUS_ERROR_DIRECTIVE_MISMATCH: Directive mismatch";
    default:
      return
          "Unknown HSA status";
  }
}

bool ReadFileIntoBuffer(const std::string& filename, std::vector<char>& buffer)
{
  std::ifstream file(filename, std::ios::binary);
  if (!file) { return false; }
  file.seekg(0, std::ios::end);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  buffer.resize((size_t) size);
  if (!file.read(buffer.data(), size)) { return false; }
  return true;
}

#ifndef _WIN32
#define _tempnam tempnam
#define _close close
#define _getpid getpid
#define _open open
#endif // _WIN32

int OpenTempFile(const char* prefix)
{
  unsigned c = 0;
  std::string tname = prefix;
  tname += "_";
  tname += std::to_string(_getpid());
  tname += "_";
  while (c++ < 20) { // Loop because several threads can generate same filename.
#ifdef _WIN32
    char dir[MAX_PATH+1];
    if (!GetTempPath(sizeof(dir), dir)) { return -1; }
#else // _WIN32
    char *dir = NULL;
#endif // _WIN32
    char *name = _tempnam(dir, tname.c_str());
    if (!name) { return -1; }
#ifdef _WIN32
    HANDLE h = CreateFile(
      name,
      GENERIC_READ | GENERIC_WRITE,
      0, // No sharing
      NULL,
      CREATE_NEW,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
      NULL);
    free(name);
    if (h == INVALID_HANDLE_VALUE) { continue; }
    return _open_osfhandle((intptr_t)h, 0);
#else // _WIN32
    int d = _open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (d < 0) { free(name); continue; }
    if (unlink(name) < 0) { free(name); _close(d); return -1; }
    free(name);
    return d;
#endif // _WIN32
  }
  return -1;
}

void CloseTempFile(int fd)
{
  _close(fd);
}

const char * CommentTopCallBack(void *ctx, int type) {
  static const char* amd_kernel_code_t_begin = "amd_kernel_code_t begin";
  static const char* amd_kernel_code_t_end = "amd_kernel_code_t end";
  static const char* isa_begin = "isa begin";
  switch(type) {
  case COMMENT_AMD_KERNEL_CODE_T_BEGIN:
    return amd_kernel_code_t_begin;
  case COMMENT_AMD_KERNEL_CODE_T_END:
    return amd_kernel_code_t_end;
  case COMMENT_KERNEL_ISA_BEGIN:
    return isa_begin;
  default:
    assert(false);
    return "";
  }
}
const char * CommentRightCallBack(void *ctx, int type) {
  return nullptr;
}

uint32_t ParseInstructionOffset(const std::string& instruction) {
  // instruction format: opcode op1, op2 ... // offset: binopcode
  std::string::size_type n = instruction.find("//");
  assert(n != std::string::npos);
  std::string comment = instruction.substr(n);
  n = comment.find(':');
  assert(n != std::string::npos);
  comment.erase(n);
  assert(comment.size() > 3);
  comment.erase(0, 3);
  return strtoul(comment.c_str(), nullptr, 16);
}

bool IsNotSpace(char c) {
  return !isspace(static_cast<int>(c));
}

void ltrim(std::string &str) {
  str.erase(str.begin(), std::find_if(str.begin(), str.end(), IsNotSpace));
}

std::string DumpFileName(const std::string& dir, const char* prefix, const char* ext, unsigned n, unsigned i)
{
  std::ostringstream ss;
  if (!dir.empty()) {
    ss << dir << "/";
  }
  ss <<
    prefix <<
    std::setfill('0') << std::setw(3) << n;
  if (i) { ss << "_" << i; }
  if (ext) { ss << "." << ext; }
  return ss.str();
}


}
//}
