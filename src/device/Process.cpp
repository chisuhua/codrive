#include "inc/Process.h"

device_status_t Process::get_process_apertures(
	process_device_apertures **process_apertures,
	uint32_t *num_of_nodes)
{
    if (apertures_) {
        *process_apertures = apertures_;
		return DEVICE_STATUS_SUCCESS;
    }
    // refer to gpu/drm/amd/amdgpu/amdgpu_vm.c
    // refer to gpu/drm/amd/amdgpu/amdgpu_amdkfd_gpuvm.c
	struct ioctl_get_process_apertures_args args = {0};
	if (!cmd_get_process_apertures(&args)) {
		*num_of_nodes = args.num_of_nodes;
	    *process_apertures = (process_device_apertures*)args.process_device_apertures_ptr;
        apertures_ = *process_apertures;
		return DEVICE_STATUS_SUCCESS;
	}
	return DEVICE_STATUS_ERROR;
#if 0
	process_apertures->gpu_id = 1;
	process_apertures->lds_base = MAKE_LDS_APP_BASE();
	process_apertures->lds_limit = MAKE_LDS_APP_LIMIT(process_apertures->lds_base);
	// process_apertures->gpuvm_base = MAKE_GPUVM_APP_BASE(1+1);
	// process_apertures->gpuvm_limit = MAKE_GPUVM_APP_LIMIT(process_apertures->gpuvm_base);

	process_apertures->scratch_base = MAKE_SCRATCH_APP_BASE();
	process_apertures->scratch_limit = MAKE_SCRATCH_APP_LIMIT(process_apertures->scratch_base);

	process_apertures->gpuvm_base = SVM_USER_BASE;
	//process_apertures->gpuvm_limit = dev->shared_resources.gpuvm_size - 1;
	process_apertures->gpuvm_limit = SVM_MIN_VM_SIZE * 2;   // 8GB
	// process_apertures->qpd.cwsr_base = SVM_CWSR_BASE;
	// process_apertures->qpd.ib_base = SVM_IB_BASE;

	return DEVICE_STATUS_SUCCESS;
#endif
}

