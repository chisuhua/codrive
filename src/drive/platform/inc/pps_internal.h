#pragma once

#include "inc/pps.h"

//namespace HSA
//{

  // Define core namespace interfaces - copy of function declarations in hsa.h
  status_t HSA_API hsa_init();
  status_t HSA_API hsa_shut_down();
  status_t HSA_API
    hsa_system_get_info(hsa_system_info_t attribute, void *value);
  status_t HSA_API hsa_extension_get_name(uint16_t extension, const char** name);
  status_t HSA_API hsa_system_extension_supported(uint16_t extension, uint16_t version_major,
                                                      uint16_t version_minor, bool* result);
  status_t HSA_API hsa_system_major_extension_supported(uint16_t extension,
                                                            uint16_t version_major,
                                                            uint16_t* version_minor, bool* result);
  status_t HSA_API
    hsa_system_get_extension_table(uint16_t extension, uint16_t version_major,
    uint16_t version_minor, void *table);
  status_t HSA_API hsa_system_get_major_extension_table(uint16_t extension,
                                                            uint16_t version_major,
                                                            size_t table_length, void* table);
  status_t HSA_API
    hsa_iterate_agents(status_t (*callback)(device_t agent, void *data),
    void *data);
  status_t HSA_API hsa_device_get_info(device_t agent,
    hsa_agent_info_t attribute,
    void *value);
  status_t HSA_API hsa_agent_get_exception_policies(device_t agent,
    profile_t profile,
    uint16_t *mask);
  status_t HSA_API hsa_cache_get_info(cache_t cache, cache_info_t attribute,
                                          void* value);
  status_t HSA_API hsa_agent_iterate_caches(
      device_t agent, status_t (*callback)(cache_t cache, void* data), void* value);
  status_t HSA_API
    hsa_agent_extension_supported(uint16_t extension, device_t agent,
    uint16_t version_major,
    uint16_t version_minor, bool *result);
  status_t HSA_API hsa_agent_major_extension_supported(uint16_t extension, device_t agent,
                                                           uint16_t version_major,
                                                           uint16_t* version_minor, bool* result);
  status_t HSA_API
    hsa_queue_create(device_t agent, uint32_t size, queue_type32_t type,
    void (*callback)(status_t status, queue_t *source,
    void *data),
    void *data, uint32_t private_segment_size,
    uint32_t group_segment_size, queue_t **queue);
  status_t HSA_API
    hsa_soft_queue_create(region_t region, uint32_t size,
    queue_type32_t type, uint32_t features,
    signal_t completion_signal, queue_t **queue);
  status_t HSA_API hsa_queue_destroy(queue_t *queue);
  status_t HSA_API hsa_queue_inactivate(queue_t *queue);
  uint64_t HSA_API hsa_queue_load_read_index_scacquire(const queue_t* queue);
  uint64_t HSA_API hsa_queue_load_read_index_relaxed(const queue_t *queue);
  uint64_t HSA_API hsa_queue_load_write_index_scacquire(const queue_t* queue);
  uint64_t HSA_API hsa_queue_load_write_index_relaxed(const queue_t *queue);
  void HSA_API hsa_queue_store_write_index_relaxed(const queue_t *queue,
    uint64_t value);
  void HSA_API hsa_queue_store_write_index_screlease(const queue_t* queue, uint64_t value);
  uint64_t HSA_API hsa_queue_cas_write_index_scacq_screl(const queue_t* queue,
                                                         uint64_t expected, uint64_t value);
  uint64_t HSA_API hsa_queue_cas_write_index_scacquire(const queue_t* queue, uint64_t expected,
                                                       uint64_t value);
  uint64_t HSA_API hsa_queue_cas_write_index_relaxed(const queue_t *queue,
    uint64_t expected,
    uint64_t value);
  uint64_t HSA_API hsa_queue_cas_write_index_screlease(const queue_t* queue, uint64_t expected,
                                                       uint64_t value);
  uint64_t HSA_API hsa_queue_add_write_index_scacq_screl(const queue_t* queue, uint64_t value);
  uint64_t HSA_API hsa_queue_add_write_index_scacquire(const queue_t* queue, uint64_t value);
  uint64_t HSA_API
    hsa_queue_add_write_index_relaxed(const queue_t *queue, uint64_t value);
  uint64_t HSA_API hsa_queue_add_write_index_screlease(const queue_t* queue, uint64_t value);
  void HSA_API hsa_queue_store_read_index_relaxed(const queue_t *queue,
    uint64_t value);
  void HSA_API hsa_queue_store_read_index_screlease(const queue_t* queue, uint64_t value);
  status_t HSA_API hsa_agent_iterate_regions(
    device_t agent,
    status_t (*callback)(region_t region, void *data), void *data);
  status_t HSA_API hsa_region_get_info(region_t region,
    hsa_region_info_t attribute,
    void *value);
  status_t HSA_API hsa_memory_register(void *address, size_t size);
  status_t HSA_API hsa_memory_deregister(void *address, size_t size);
  status_t HSA_API
    hsa_memory_allocate(region_t region, size_t size, void **ptr);
  status_t HSA_API hsa_memory_free(void *ptr);
  status_t HSA_API hsa_memory_copy(void *dst, const void *src, size_t size, uint8_t dir);
  status_t HSA_API hsa_memory_assign_agent(void *ptr, device_t agent,
    hsa_access_permission_t access);
  status_t HSA_API
    hsa_signal_create(signal_value_t initial_value, uint32_t num_consumers,
    const device_t *consumers, signal_t *signal);
  status_t HSA_API hsa_signal_destroy(signal_t signal);
  signal_value_t HSA_API hsa_signal_load_relaxed(signal_t signal);
  signal_value_t HSA_API hsa_signal_load_scacquire(signal_t signal);
  void HSA_API
    hsa_signal_store_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_store_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_silent_store_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_silent_store_screlease(signal_t signal, signal_value_t value);
  signal_value_t HSA_API
    hsa_signal_wait_relaxed(signal_t signal,
    signal_condition_t condition,
    signal_value_t compare_value,
    uint64_t timeout_hint,
    hsa_wait_state_t wait_expectancy_hint);
  signal_value_t HSA_API hsa_signal_wait_scacquire(signal_t signal,
                                                       signal_condition_t condition,
                                                       signal_value_t compare_value,
                                                       uint64_t timeout_hint,
                                                       hsa_wait_state_t wait_expectancy_hint);
  status_t HSA_API hsa_signal_group_create(uint32_t num_signals, const signal_t* signals,
                                               uint32_t num_consumers, const device_t* consumers,
                                               hsa_signal_group_t* signal_group);
  status_t HSA_API hsa_signal_group_destroy(hsa_signal_group_t signal_group);
  status_t HSA_API hsa_signal_group_wait_any_scacquire(hsa_signal_group_t signal_group,
                                                           const signal_condition_t* conditions,
                                                           const signal_value_t* compare_values,
                                                           hsa_wait_state_t wait_state_hint,
                                                           signal_t* signal,
                                                           signal_value_t* value);
  status_t HSA_API hsa_signal_group_wait_any_relaxed(hsa_signal_group_t signal_group,
                                                         const signal_condition_t* conditions,
                                                         const signal_value_t* compare_values,
                                                         hsa_wait_state_t wait_state_hint,
                                                         signal_t* signal,
                                                         signal_value_t* value);
  void HSA_API
    hsa_signal_and_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_and_scacquire(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_and_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_and_scacq_screl(signal_t signal, signal_value_t value);
  void HSA_API
    hsa_signal_or_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_or_scacquire(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_or_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_or_scacq_screl(signal_t signal, signal_value_t value);
  void HSA_API
    hsa_signal_xor_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_xor_scacquire(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_xor_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_xor_scacq_screl(signal_t signal, signal_value_t value);
  void HSA_API
    hsa_signal_add_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_add_scacquire(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_add_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_add_scacq_screl(signal_t signal, signal_value_t value);
  void HSA_API
    hsa_signal_subtract_relaxed(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_subtract_scacquire(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_subtract_screlease(signal_t signal, signal_value_t value);
  void HSA_API hsa_signal_subtract_scacq_screl(signal_t signal, signal_value_t value);
  signal_value_t HSA_API
    hsa_signal_exchange_relaxed(signal_t signal, signal_value_t value);
  signal_value_t HSA_API hsa_signal_exchange_scacquire(signal_t signal,
                                                           signal_value_t value);
  signal_value_t HSA_API hsa_signal_exchange_screlease(signal_t signal,
                                                           signal_value_t value);
  signal_value_t HSA_API hsa_signal_exchange_scacq_screl(signal_t signal,
                                                             signal_value_t value);
  signal_value_t HSA_API hsa_signal_cas_relaxed(signal_t signal,
    signal_value_t expected,
    signal_value_t value);
  signal_value_t HSA_API hsa_signal_cas_scacquire(signal_t signal,
                                                      signal_value_t expected,
                                                      signal_value_t value);
  signal_value_t HSA_API hsa_signal_cas_screlease(signal_t signal,
                                                      signal_value_t expected,
                                                      signal_value_t value);
  signal_value_t HSA_API hsa_signal_cas_scacq_screl(signal_t signal,
                                                        signal_value_t expected,
                                                        signal_value_t value);

  //===--- Instruction Set Architecture -----------------------------------===//

  status_t HSA_API hsa_isa_from_name(
      const char *name,
      hsa_isa_t *isa);
  status_t HSA_API hsa_agent_iterate_isas(
      device_t agent,
      status_t (*callback)(hsa_isa_t isa,
                               void *data),
      void *data);
  /* deprecated */ status_t HSA_API hsa_isa_get_info(
      hsa_isa_t isa,
      hsa_isa_info_t attribute,
      uint32_t index,
      void *value);
  status_t HSA_API hsa_isa_get_info_alt(
      hsa_isa_t isa,
      hsa_isa_info_t attribute,
      void *value);
  status_t HSA_API hsa_isa_get_exception_policies(
      hsa_isa_t isa,
      profile_t profile,
      uint16_t *mask);
  status_t HSA_API hsa_isa_get_round_method(
      hsa_isa_t isa,
      hsa_fp_type_t fp_type,
      hsa_flush_mode_t flush_mode,
      hsa_round_method_t *round_method);
  status_t HSA_API hsa_wavefront_get_info(
      hsa_wavefront_t wavefront,
      hsa_wavefront_info_t attribute,
      void *value);
  status_t HSA_API hsa_isa_iterate_wavefronts(
      hsa_isa_t isa,
      status_t (*callback)(hsa_wavefront_t wavefront,
                               void *data),
      void *data);
  /* deprecated */ status_t HSA_API hsa_isa_compatible(
      hsa_isa_t code_object_isa,
      hsa_isa_t agent_isa,
      bool *result);

  //===--- Code Objects (deprecated) --------------------------------------===//

  /* deprecated */ status_t HSA_API hsa_code_object_serialize(
      hsa_code_object_t code_object,
      status_t (*alloc_callback)(size_t size,
                                     hsa_callback_data_t data,
                                     void **address),
      hsa_callback_data_t callback_data,
      const char *options,
      void **serialized_code_object,
      size_t *serialized_code_object_size);
  /* deprecated */ status_t HSA_API hsa_code_object_deserialize(
      void *serialized_code_object,
      size_t serialized_code_object_size,
      const char *options,
      hsa_code_object_t *code_object);
  /* deprecated */ status_t HSA_API hsa_code_object_destroy(
      hsa_code_object_t code_object);
  /* deprecated */ status_t HSA_API hsa_code_object_get_info(
      hsa_code_object_t code_object,
      hsa_code_object_info_t attribute,
      void *value);
  /* deprecated */ status_t HSA_API hsa_code_object_get_symbol(
      hsa_code_object_t code_object,
      const char *symbol_name,
      hsa_code_symbol_t *symbol);
  /* deprecated */ status_t HSA_API hsa_code_object_get_symbol_from_name(
      hsa_code_object_t code_object,
      const char *module_name,
      const char *symbol_name,
      hsa_code_symbol_t *symbol);
  /* deprecated */ status_t HSA_API hsa_code_symbol_get_info(
      hsa_code_symbol_t code_symbol,
      hsa_code_symbol_info_t attribute,
      void *value);
  /* deprecated */ status_t HSA_API hsa_code_object_iterate_symbols(
      hsa_code_object_t code_object,
      status_t (*callback)(hsa_code_object_t code_object,
                               hsa_code_symbol_t symbol,
                               void *data),
      void *data);

  //===--- Executable -----------------------------------------------------===//

  status_t HSA_API hsa_code_object_reader_create_from_file(
      hsa_file_t file,
      hsa_code_object_reader_t *code_object_reader);
  status_t HSA_API hsa_code_object_reader_create_from_memory(
      const void *code_object,
      size_t size,
      hsa_code_object_reader_t *code_object_reader);
  status_t HSA_API hsa_code_object_reader_destroy(
      hsa_code_object_reader_t code_object_reader);
  /* deprecated */ status_t HSA_API hsa_executable_create(
      profile_t profile,
      hsa_executable_state_t executable_state,
      const char *options,
      hsa_executable_t *executable);
  status_t HSA_API hsa_executable_create_alt(
      profile_t profile,
      hsa_default_float_rounding_mode_t default_float_rounding_mode,
      const char *options,
      hsa_executable_t *executable);
  status_t HSA_API hsa_executable_destroy(
      hsa_executable_t executable);
  /* deprecated */ status_t HSA_API hsa_executable_load_code_object(
      hsa_executable_t executable,
      device_t agent,
      hsa_code_object_t code_object,
      const char *options);
  status_t HSA_API hsa_executable_load_program_code_object(
      hsa_executable_t executable,
      hsa_code_object_reader_t code_object_reader,
      const char *options,
      hsa_loaded_code_object_t *loaded_code_object);
  status_t HSA_API hsa_executable_load_agent_code_object(
      hsa_executable_t executable,
      device_t agent,
      hsa_code_object_reader_t code_object_reader,
      const char *options,
      hsa_loaded_code_object_t *loaded_code_object);
  status_t HSA_API hsa_executable_freeze(
      hsa_executable_t executable,
      const char *options);
  status_t HSA_API hsa_executable_get_info(
      hsa_executable_t executable,
      hsa_executable_info_t attribute,
      void *value);
  status_t HSA_API hsa_executable_global_variable_define(
      hsa_executable_t executable,
      const char *variable_name,
      void *address);
  status_t HSA_API hsa_executable_agent_global_variable_define(
      hsa_executable_t executable,
      device_t agent,
      const char *variable_name,
      void *address);
  status_t HSA_API hsa_executable_readonly_variable_define(
      hsa_executable_t executable,
      device_t agent,
      const char *variable_name,
      void *address);
  status_t HSA_API hsa_executable_validate(
      hsa_executable_t executable,
      uint32_t *result);
  status_t HSA_API hsa_executable_validate_alt(
      hsa_executable_t executable,
      const char *options,
      uint32_t *result);
  /* deprecated */ status_t HSA_API hsa_executable_get_symbol(
      hsa_executable_t executable,
      const char *module_name,
      const char *symbol_name,
      device_t agent,
      int32_t call_convention,
      hsa_executable_symbol_t *symbol);
  status_t HSA_API hsa_executable_get_symbol_by_name(
      hsa_executable_t executable,
      const char *symbol_name,
      const device_t *agent,
      hsa_executable_symbol_t *symbol);
  status_t HSA_API hsa_executable_symbol_get_info(
      hsa_executable_symbol_t executable_symbol,
      hsa_executable_symbol_info_t attribute,
      void *value);
  /* deprecated */ status_t HSA_API hsa_executable_iterate_symbols(
      hsa_executable_t executable,
      status_t (*callback)(hsa_executable_t executable,
                               hsa_executable_symbol_t symbol,
                               void *data),
      void *data);
  status_t HSA_API hsa_executable_iterate_agent_symbols(
      hsa_executable_t executable,
      device_t agent,
      status_t (*callback)(hsa_executable_t exec,
                               device_t agent,
                               hsa_executable_symbol_t symbol,
                               void *data),
      void *data);
  status_t HSA_API hsa_executable_iterate_program_symbols(
      hsa_executable_t executable,
      status_t (*callback)(hsa_executable_t exec,
                               hsa_executable_symbol_t symbol,
                               void *data),
      void *data);

  //===--- Runtime Notifications ------------------------------------------===//

  status_t HSA_API hsa_status_string(
      status_t status,
      const char **status_string);
//}

#ifdef BUILDING_HSA_CORE_RUNTIME
//This using declaration is deliberate!
//We want unqualified name resolution to fail when building the runtime.  This is a guard against accidental use of the intercept layer in the runtime.
using namespace HSA;
#endif

