#pragma once
#include <inttypes.h>
#include <vector>
#include <cassert>
// #include "inc/Engine.h"
#include "inc/Model.h"



typedef void* (*pfn_create_model)(uint32_t device_idx);
typedef uint32_t (*pfn_get_device_cnt)();
typedef void (*pfn_destroy_model)(void* handle);
typedef void (*pfn_write_register)(void* handle, uint32_t offset, uint32_t value);
typedef uint32_t (*pfn_read_register)(void* handle, uint32_t offset);
typedef void (*pfn_read_mmio)(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar);
typedef void (*pfn_write_mmio)(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar);
typedef void (*pfn_query_adapter_info)(uint32_t deviceIdx, AdapterInfo* pInfo);


void*    create_model();
uint32_t get_device_cnt();
void     destroy_model(void* handle);
void     write_register(void* handle, uint32_t offset, uint32_t value);
uint32_t read_register(void* handle, uint32_t offset);
void     read_mmio(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar);
void     write_mmio(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar);
void     query_adapter_info(uint32_t deviceIdx, AdapterInfo* pInfo);



