#pragma once
#include <stdint.h>
#include <vector>
#include "inc/Vma.h"
#include "inc/MemObj.h"
#include "inc/Device.h"

enum svm_aperture_type {
	SVM_DEFAULT = 0,
	SVM_COHERENT,
	SVM_APERTURE_NUM
};


class GpuMemory {
public:
        GpuMemory(Device *device);
        ~GpuMemory();
	uint32_t gpu_id;
	uint32_t device_id;
	uint32_t node_id;
	uint64_t local_mem_size;
	AddrRange lds_aperture {0,0};
	AddrRange scratch_aperture {0, 0};
	AddrRange mmio_aperture {0,0};
	Vma* scratch_physical; /* For dGPU, scratch physical is allocated from
						 * dgpu_aperture. When requested by RT, each
						 * GPU will get a differnt range
						 */
	Vma* gpuvm_aperture;   /* used for GPUVM on APU, outsidethe canonical address range */
	int drm_render_fd;
    Device* device_;
};

/* The main structure for dGPU Shared Virtual Memory Management */
class GpuSVM {
    public:
        GpuSVM(Device *device)
            : device_(device)
            , dgpu_aperture(nullptr)
            , dgpu_alt_aperture(nullptr)
            , userptr_for_paged_mem(false)
            , check_userptr(false)
            , disable_cache(false)
        {
            for (uint32_t i = 0; i < SVM_APERTURE_NUM; ++i) {
                apertures.push_back(std::make_shared<MmapVma>(device, nullptr, nullptr));
            }
        }
        ~GpuSVM() {}

    Device* device_;

	/* Two apertures can have different MTypes (for coherency) */
    std::vector<std::shared_ptr<Vma>> apertures;

	/* Pointers to apertures, may point to the same aperture
     * on when MType is not base on apertures
     */
    std::shared_ptr<Vma> dgpu_aperture;
    std::shared_ptr<Vma> dgpu_alt_aperture;

	/* whether to use userptr for paged memory */
	bool userptr_for_paged_mem;
	/* whether to check userptrs on registration */
	bool check_userptr;
	/* whether to check reserve svm on registration */
	bool reserve_svm;
	/* whether all memory is coherent (GPU cache disabled) */
	bool disable_cache;
};

