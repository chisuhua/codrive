#pragma once

#include "inc/hsakmt.h"
#include "inc/device_tools.h"
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
// #include <pci/pci.h>

extern pthread_mutex_t hsakmt_mutex;
extern bool is_dgpu;

#undef HSAKMTAPI
#define HSAKMTAPI __attribute__((visibility ("default")))

/*Avoid pointer-to-int-cast warning*/
#define PORT_VPTR_TO_UINT64(vptr) ((uint64_t)(unsigned long)(vptr))

/*Avoid int-to-pointer-cast warning*/
#define PORT_UINT64_TO_VPTR(v) ((void*)(unsigned long)(v))

extern int PAGE_SIZE;
extern int PAGE_SHIFT;

/* VI HW bug requires this virtual address alignment */
#define TONGA_PAGE_SIZE 0x8000

/* 64KB BigK fragment size for TLB efficiency */
#define GPU_BIGK_PAGE_SIZE (1 << 16)

/* 2MB huge page size for 4-level page tables on Vega10 and later GPUs */
#define GPU_HUGE_PAGE_SIZE (2 << 20)

#define CHECK_PAGE_MULTIPLE(x) \
	do { if ((uint64_t)PORT_VPTR_TO_UINT64(x) % PAGE_SIZE) return ERROR_INVALID_ARGUMENT } while(0)

#define ALIGN_UP(x,align) (((uint64_t)(x) + (align) - 1) & ~(uint64_t)((align)-1))
#define ALIGN_UP_32(x,align) (((uint32_t)(x) + (align) - 1) & ~(uint32_t)((align)-1))
#define PAGE_ALIGN_UP(x) ALIGN_UP(x,PAGE_SIZE)
#define BITMASK(n) (((n) < sizeof(1ULL) * CHAR_BIT ? (1ULL << (n)) : 0) - 1ULL)
#define ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))

/* HSA Thunk logging usage */
extern int device_debug_level;
#define hsakmt_print(level, fmt, ...) \
	do { if (level <= device_debug_level) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#define DEVICE_DEBUG_LEVEL_DEFAULT	-1
#define DEVICE_DEBUG_LEVEL_ERR		3
#define DEVICE_DEBUG_LEVEL_WARNING	4
#define DEVICE_DEBUG_LEVEL_INFO		6
#define DEVICE_DEBUG_LEVEL_DEBUG	7
#define pr_err(fmt, ...) \
	hsakmt_print(DEVICE_DEBUG_LEVEL_ERR, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) \
	hsakmt_print(DEVICE_DEBUG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	hsakmt_print(DEVICE_DEBUG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) \
	hsakmt_print(DEVICE_DEBUG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

enum asic_family_type {
	CHIP_PPUe = 0,
	CHIP_PPU = 0,
	CHIP_LAST
};

// #define IS_DGPU(chip) ((chip) != CHIP_KAVERI && (chip) != CHIP_CARRIZO && (chip) != CHIP_RAVEN)
// #define IS_SOC15(chip) ((chip) >= CHIP_PPUe)

device_status_t validate_nodeid(uint32_t nodeid, uint32_t *gpu_id);
device_status_t gpuid_to_nodeid(uint32_t gpu_id, uint32_t* node_id);
// uint16_t get_device_id_by_node(HSAuint32 node_id);
uint16_t get_device_id_by_node_id(HSAuint32 node_id);
uint16_t get_device_id_by_gpu_id(HSAuint32 gpu_id);
int get_drm_render_fd_by_gpu_id(HSAuint32 gpu_id);
device_status_t validate_nodeid_array(uint32_t **gpu_id_array,
		uint32_t NumberOfNodes, uint32_t *NodeArray);

device_status_t topology_sysfs_get_gpu_id(uint32_t node_id, uint32_t *gpu_id);
device_status_t topology_sysfs_get_node_props(uint32_t node_id, HsaCoreProperties *props, uint32_t *gpu_id); // , struct pci_access* pacc);
device_status_t topology_sysfs_get_system_props(HsaSystemProperties *props);
bool topology_is_dgpu(uint16_t device_id);
bool topology_is_svm_needed(uint16_t device_id);
device_status_t topology_get_asic_family(uint16_t device_id,
					enum asic_family_type *asic);

HSAuint32 PageSizeFromFlags(unsigned int pageSizeFlags);

void* allocate_exec_aligned_memory_gpu(uint32_t size, uint32_t align,
                                       uint32_t NodeId, bool NonPaged,
				                        bool DeviceLocal);
void free_exec_aligned_memory_gpu(void *addr, uint32_t size, uint32_t align);


// device_status_t init_process_doorbells(unsigned int NumNodes, node_props_t *node_props);
device_status_t init_process_doorbells(unsigned int NumNodes); // , node_props_t *node_props);
void destroy_process_doorbells(void);
device_status_t init_device_debugging_memory(unsigned int NumNodes);
void destroy_device_debugging_memory(void);
device_status_t init_counter_props(unsigned int NumNodes);
void destroy_counter_props(void);

extern int kmtIoctl(int fd, unsigned long request, void *arg);

/* Void pointer arithmetic (or remove -Wpointer-arith to allow void pointers arithmetic) */
#define VOID_PTR_ADD32(ptr,n) (void*)((uint32_t*)(ptr) + n)/*ptr + offset*/
#define VOID_PTR_ADD(ptr,n) (void*)((uint8_t*)(ptr) + n)/*ptr + offset*/
#define VOID_PTR_SUB(ptr,n) (void*)((uint8_t*)(ptr) - n)/*ptr - offset*/
#define VOID_PTRS_SUB(ptr1,ptr2) (uint64_t)((uint8_t*)(ptr1) - (uint8_t*)(ptr2)) /*ptr1 - ptr2*/
/*
#define MIN(a, b) ({				\
	typeof(a) tmp1 = (a), tmp2 = (b);	\
	tmp1 < tmp2 ? tmp1 : tmp2; })

#define MAX(a, b) ({				\
	typeof(a) tmp1 = (a), tmp2 = (b);	\
	tmp1 > tmp2 ? tmp1 : tmp2; })
*/
void clear_events_page(void);
void fmm_clear_all_mem(void);
void clear_process_doorbells(void);
