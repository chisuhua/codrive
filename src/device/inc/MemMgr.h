#pragma once
#include "inc/Vma.h"
#include "inc/GpuMemory.h"
#include "cmdio/cmdio.h"

class Device;
class Topology;

class MemMgr {
public:
    MemMgr(Device* device);
    void *allocate_exec_aligned_memory_cpu(uint32_t size);

    void *allocate_exec_aligned_memory_gpu(uint32_t size, uint32_t align,
				       uint32_t NodeId, bool nonPaged,
				       bool DeviceLocal);

    void free_exec_aligned_memory_gpu(void *addr, uint32_t size, uint32_t align);

    void *allocate_exec_aligned_memory(uint32_t size,
					  bool use_ats,
					  uint32_t NodeId,
					  bool DeviceLocal);

    void free_exec_aligned_memory(void *addr, uint32_t size, uint32_t align,
				     bool use_ats);

    device_status_t get_process_apertures(
                process_device_apertures **process_apertures,
	            uint32_t *num_of_nodes);


	device_status_t mm_init_process_apertures(unsigned int NumNodes, node_props_t *node_props);
    device_status_t init_svm_apertures(uint64_t base, uint64_t limit, uint32_t align, uint32_t guard_pages);
    // device_status_t init_svm_apertures(uint64_t base, uint64_t limit, uint32_t align, uint32_t guard_pages);

	void mm_destroy_process_apertures(void);
	//
	/* Memory interface */
	void *mm_allocate_scratch(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes);
	void *mm_allocate_device(uint32_t gpu_id, void *address, uint64_t MemorySizeInBytes, HsaMemFlags flags);
	void *mm_allocate_doorbell(uint32_t gpu_id, uint64_t MemorySizeInBytes, uint64_t doorbell_offset);
	void *mm_allocate_host(uint32_t node_id, void *address, uint64_t MemorySizeInBytes,
				HsaMemFlags flags);
    void *mm_allocate_host_cpu(void *address, uint64_t MemorySizeInBytes,
				HsaMemFlags flags);
    void *mm_allocate_host_gpu(uint32_t node_id, void *address,
				   uint64_t MemorySizeInBytes, HsaMemFlags flags);

	//void mm_print(uint32_t node);
	device_status_t mm_release(void *address);
    void mm_release_scratch(uint32_t gpu_id);

	int mm_map_to_gpu(void *address, uint64_t size, uint64_t *gpuvm_address);
	int mm_unmap_from_gpu(void *address);
	bool mm_get_handle(void *address, uint64_t *handle);
	device_status_t mm_get_mem_info(const void *address, HsaPointerInfo *info);
	device_status_t mm_set_mem_user_data(const void *mem, void *usr_data);

	///* Topology interface*/
    //FIXME
	device_status_t mm_node_added(uint32_t gpu_id);
	device_status_t mm_node_removed(uint32_t gpu_id);

	device_status_t mm_get_aperture_base_and_limit(aperture_type_e aperture_type, uint32_t gpu_id,
			uint64_t *aperture_base, uint64_t *aperture_limit);

	device_status_t mm_register_memory(void *address, uint64_t size_in_bytes,
									  uint32_t *gpu_id_array,
									  uint32_t gpu_id_array_size,
									  bool coarse_grain);
	device_status_t mm_register_graphics_handle(uint64_t GraphicsResourceHandle,
						   HsaGraphicsResourceInfo *GraphicsResourceInfo,
						   uint32_t *gpu_id_array,
						   uint32_t gpu_id_array_size);
	device_status_t mm_deregister_memory(void *address);
	device_status_t mm_share_memory(void *MemoryAddress,
				       uint64_t SizeInBytes,
				       HsaSharedMemoryHandle *SharedMemoryHandle);
	device_status_t mm_register_shared_memory(const HsaSharedMemoryHandle *SharedMemoryHandle,
						 uint64_t *SizeInBytes,
						 void **MemoryAddress,
						 uint32_t *gpu_id_array,
						 uint32_t gpu_id_array_size);
	device_status_t mm_map_to_gpu_nodes(void *address, uint64_t size,
			uint32_t *nodes_to_map, uint64_t num_of_nodes, uint64_t *gpuvm_address);

	void mm_clear_all_mem(void);

    int32_t gpu_mem_find_by_gpu_id(uint32_t gpu_id);
    std::shared_ptr<GpuMemory>   get_gpu_mem(uint32_t gpu_id);

    device_status_t init_mmap_apertures(uint64_t base, uint64_t limit,
					 uint32_t align, uint32_t guard_pages);
    void *reserve_address(void *addr, unsigned long long int len);


    void *map_mmio(uint32_t node_id, uint32_t gpu_id, int mmap_fd);
    void release_mmio(void);

    uint32_t mm_translate_hsa_to_ioc_flags(HsaMemFlags flags);

    int open_drm_render_device(int minor);
    device_status_t acquire_vm(uint32_t gpu_id, int fd);

    MemObj *vm_find_object(const void *addr, uint64_t size, Vma **out_aper);

    uint8_t mm_check_user_memory(const void *addr, uint64_t size);
    device_status_t mm_register_user_memory(void *addr, HSAuint64 size,
				  MemObj **obj_ret, bool coarse_grain);

    Vma *mm_find_aperture(const void *address, HsaApertureInfo *info);
    Vma *mm_get_aperture(HsaApertureInfo info);
    Vma *mm_is_scratch_aperture(const void *address);

private:
    Device*                 device_;
    Topology*               topo_;
    std::vector<std::shared_ptr<GpuMemory>> gpu_mem_;
    std::shared_ptr<GpuSVM>      svm_;

    uint32_t                gpu_mem_count_;
    std::shared_ptr<GpuMemory>   first_gpu_mem_;

    int        GPU_HUGE_PAGE_SIZE;
    int        GPU_BIGK_PAGE_SIZE;
    int        PAGE_SIZE;
    int        PAGE_SHIFT;

    void *dgpu_shared_aperture_base_;
    void *dgpu_shared_aperture_limit_;
    bool hsa_debug_;

/* The VMs from DRM render nodes are used by KFD for the lifetime of
 * the process. Therefore we have to keep using the same FDs for the
 * lifetime of the process, even when we close and reopen KFD. There
 * are up to 128 render nodes that we cache in this array.
 */
    int drm_render_fds[DRM_LAST_RENDER_NODE + 1 - DRM_FIRST_RENDER_NODE];

// below from fmm.c , they are better in ppu_cmdio
/* On APU, for memory allocated on the system memory that GPU doesn't access
 * via GPU driver, they are not managed by GPUVM. cpuvm_aperture keeps track
 * of this part of memory.
 */
    Vma* cpuvm_aperture; //  {nullptr, nullptr, &reserved_aperture_ops};
    process_device_apertures *apertures_ {nullptr};

friend class Vma;
friend class QueueMgr;
friend class EventMgr;

};

uint32_t PageSizeFromFlags(unsigned int pageSizeFlags);


