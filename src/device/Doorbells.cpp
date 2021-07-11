#include "inc/Doorbells.h"
#include "inc/Device.h"
#include "inc/MemMgr.h"
#include "inc/Topology.h"
#include <sys/mman.h>
#include <stdint.h>

device_status_t Doorbells::init_process_doorbells(unsigned int NumNodes)
{
	device_status_t ret = DEVICE_STATUS_SUCCESS;
	/* doorbells[] is accessed using Topology NodeId. This means doorbells[0],
	 * which corresponds to CPU only Node, might not be used
	 */
	for (uint32_t i = 0; i < NumNodes; i++) {
        doorbells_.push_back(std::make_shared<process_doorbells>(false, 0, nullptr, this));
	}

	num_doorbells_nodes_ = NumNodes;

	return ret;
}

void Doorbells::get_doorbell_map_info(uint16_t dev_id,
				  std::shared_ptr<process_doorbells> doorbell)
{
	const struct device_info *dev_info;

	dev_info = device_->get_device_info_by_dev_id(dev_id);

	/*
	 * GPUVM doorbell on Tonga requires a workaround for VM TLB ACTIVE bit
	 * lookup bug. Remove ASIC check when this is implemented in amdgpu.
	 */
	doorbell->use_gpuvm_ = device_->is_dgpu();  // && dev_info->asic_family !=.
	doorbell->size_ = DOORBELLS_PAGE_SIZE(dev_info->doorbell_size);
}

void Doorbells::destroy_process_doorbells(void)
{
	if (doorbells_.size() == 0)
		return;

	for (uint32_t i = 0; i < num_doorbells_nodes_; i++) {
		if (!doorbells_[i]->size_)
			continue;

		if (doorbells_[i]->use_gpuvm_) {
			mm_->mm_unmap_from_gpu(doorbells_[i]->mapping_);
			mm_->mm_release(doorbells_[i]->mapping_);
		} else
			munmap(doorbells_[i]->mapping_, doorbells_[i]->size_);
	}

	// free(doorbells);
	// doorbells = NULL;
	doorbells_.resize(0);
	num_doorbells_nodes_ = 0;
}

/* This is a special funcion that should be called only from the child process
 * after a fork(). This will clear doorbells duplicated from the parent.
 */
void Doorbells::clear_process_doorbells(void)
{
	if (doorbells_.size() == 0)
		return;

	for (uint32_t i = 0; i < num_doorbells_nodes_; i++) {
		if (!doorbells_[i]->size_)
			continue;

		if (!doorbells_[i]->use_gpuvm_)
			munmap(doorbells_[i]->mapping_, doorbells_[i]->size_);
	}

	// free(doorbells);
	// doorbells = NULL;
	doorbells_.resize(0);
	num_doorbells_nodes_ = 0;
}

device_status_t  Doorbells::map_doorbell_apu(uint32_t NodeId, uint32_t gpu_id,
				      uint64_t doorbell_mmap_offset)
{
	uint64_t* ptr;

	struct ioctl_mmap_args mmap_args = {0};
    mmap_args.start = 0;
    mmap_args.length = doorbells_[NodeId]->size_;
    mmap_args.prot = PROT_WRITE | PROT_READ;
    mmap_args.flags = MAP_SHARED;
    mmap_args.fd = -1;
    mmap_args.offset = doorbell_mmap_offset;

	cmd_mmap(&mmap_args);

	ptr = (uint64_t*)mmap_args.start;
	// ptr = mmap(0, doorbells[NodeId]->size, PROT_READ|PROT_WRITE,
	//	   MAP_SHARED, kfd_fd, doorbell_mmap_offset);
		//		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            // FIXME
		   //MAP_SHARED, -1, doorbell_mmap_offset);

	if (ptr == MAP_FAILED)
		return DEVICE_STATUS_ERROR;

	doorbells_[NodeId]->mapping_ = ptr;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t Doorbells::map_doorbell_dgpu(uint32_t NodeId, uint32_t gpu_id,
				       uint64_t doorbell_mmap_offset)
{
	void *ptr;

	ptr = mm_->mm_allocate_doorbell(gpu_id, doorbells_[NodeId]->size_,
				doorbell_mmap_offset);

	if (!ptr)
		return DEVICE_STATUS_ERROR;

	/* map for GPU access */
	if (mm_->mm_map_to_gpu(ptr, doorbells_[NodeId]->size_, NULL)) {
		mm_->mm_release(ptr);
		return DEVICE_STATUS_ERROR;
	}

	doorbells_[NodeId]->mapping_ = ptr;

	return DEVICE_STATUS_SUCCESS;
	// return  map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);
}

device_status_t Doorbells::map_doorbell(uint32_t NodeId, uint32_t gpu_id,
				  uint64_t doorbell_mmap_offset)
{
	device_status_t status = DEVICE_STATUS_SUCCESS;

    {
        std::lock_guard<std::mutex> lock(doorbells_[NodeId]->mutex_);
	    if (doorbells_[NodeId]->size_) {
		    return DEVICE_STATUS_SUCCESS;
	    }

	    get_doorbell_map_info(top_->get_device_id_by_node_id(NodeId),
			      doorbells_[NodeId]);

        // TODO i use gpu_id for seperate apu and gpu node
	    if (gpu_id /* schi add */ && doorbells_[NodeId]->use_gpuvm_) {
		    status = map_doorbell_dgpu(NodeId, gpu_id, doorbell_mmap_offset);
		    if (status != DEVICE_STATUS_SUCCESS) {
			    /* Fall back to the old method if KFD doesn't
			    * support doorbells in GPUVM
			    */
			    doorbells_[NodeId]->use_gpuvm_ = false;
			    status = map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);
		    }
	    } else
		    status = map_doorbell_apu(NodeId, gpu_id, doorbell_mmap_offset);

	    if (status != DEVICE_STATUS_SUCCESS)
	        doorbells_[NodeId]->size_ = 0;
    }

	return status;
}

