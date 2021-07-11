#pragma once

class GpuVma {
    public:
        gpu_mem_t();
        ~gpu_mem_t();
	uint32_t gpu_id;
	uint32_t device_id;
	uint32_t node_id;
	uint64_t local_mem_size;
	Vma lds_aperture;
	Vma scratch_aperture;
	Vma mmio_aperture;
	Vma* scratch_physical; /* For dGPU, scratch physical is allocated from
						 * dgpu_aperture. When requested by RT, each
						 * GPU will get a differnt range
						 */
	Vma* gpuvm_aperture;   /* used for GPUVM on APU, outsidethe canonical address range */
	int drm_render_fd;
};

