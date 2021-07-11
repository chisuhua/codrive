#include "inc/QueueMgr.h"
#include "inc/Device.h"
#include "inc/MemMgr.h"
#include "inc/Topology.h"
#include "util/utils.h"
#include "util/intmath.h"
#include <cstring>

#define WAVES_PER_CU		32
#define CNTL_STACK_BYTES_PER_WAVE	12
#define VGPR_SIZE_PER_CU	 0x80000
#define SGPR_SIZE_PER_CU	0x4000
#define LDS_SIZE_PER_CU		0x10000
#define HWREG_SIZE_PER_CU	0x1000

#define WG_CONTEXT_DATA_SIZE_PER_CU	(VGPR_SIZE_PER_CU + SGPR_SIZE_PER_CU + LDS_SIZE_PER_CU + HWREG_SIZE_PER_CU)



/* The bool return indicate whether the queue needs a context-save-restore area*/
bool QueueMgr::update_ctx_save_restore_size(uint32_t nodeid, struct queue *q)
{
	HsaCoreProperties *node;

	if (device_->GetNodeProperties(nodeid, &node))
		return false;
	if (node->NumFComputeCores && node->NumSIMDPerCU) {
		uint32_t ctl_stack_size, wg_data_size;
		uint32_t cu_num = node->NumFComputeCores / node->NumSIMDPerCU;

		ctl_stack_size = cu_num * WAVES_PER_CU * CNTL_STACK_BYTES_PER_WAVE + 8;
		wg_data_size = cu_num * WG_CONTEXT_DATA_SIZE_PER_CU; // (q->dev_info->asic_family);
		q->ctl_stack_size = alignUp(ctl_stack_size
					+ sizeof(HsaUserContextSaveAreaHeader), mm_->PAGE_SIZE );

		q->ctx_save_restore_size = q->ctl_stack_size
					+ alignUp(wg_data_size, mm_->PAGE_SIZE);
		return true;
	}
	return false;
}

void QueueMgr::free_queue(struct queue *q)
{
	if (q->eop_buffer)
		mm_->free_exec_aligned_memory(q->eop_buffer,
					 q->dev_info->eop_buffer_size,
					 mm_->PAGE_SIZE, q->use_ats);
	if (q->ctx_save_restore)
		mm_->free_exec_aligned_memory(q->ctx_save_restore,
					 q->ctx_save_restore_size,
					 mm_->PAGE_SIZE, q->use_ats);

	mm_->free_exec_aligned_memory((void *)q, sizeof(*q), mm_->PAGE_SIZE, q->use_ats);
}

int QueueMgr::handle_concrete_asic(struct queue *q,
				struct ioctl_create_queue_args *args,
				uint32_t NodeId)
{
	const struct device_info *dev_info = q->dev_info;
	bool ret;

	if (!dev_info)
		return DEVICE_STATUS_SUCCESS;

	if (dev_info->eop_buffer_size > 0) {
		q->eop_buffer =
				mm_->allocate_exec_aligned_memory(q->dev_info->eop_buffer_size,
				q->use_ats,
				NodeId, true);
		if (!q->eop_buffer)
			return DEVICE_STATUS_NO_MEMORY;

		args->eop_buffer_address = (uintptr_t)q->eop_buffer;
		args->eop_buffer_size = dev_info->eop_buffer_size;
	}

	ret = update_ctx_save_restore_size(NodeId, q);

	if (ret) {
		args->ctx_save_restore_size = q->ctx_save_restore_size;
		args->ctl_stack_size = q->ctl_stack_size;
		q->ctx_save_restore =
			mm_->allocate_exec_aligned_memory(q->ctx_save_restore_size,
							 q->use_ats,
							 NodeId, false);
		if (!q->ctx_save_restore)
			return DEVICE_STATUS_NO_MEMORY;

		args->ctx_save_restore_address = (uintptr_t)q->ctx_save_restore;
	}

	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::CreateQueue(uint32_t NodeId,
					  HSA_QUEUE_TYPE Type,
					  uint32_t QueuePercentage,
					  HSA_QUEUE_PRIORITY Priority,
					  void *QueueAddress,
					  uint64_t QueueSizeInBytes,
					  HsaEvent *Event,
					  HsaQueueResource *QueueResource)
{
	device_status_t result;
	uint32_t gpu_id;
	uint16_t dev_id;
	uint64_t doorbell_mmap_offset;
	unsigned int doorbell_offset;
	const struct device_info *dev_info;
	int err;
	HsaCoreProperties *props;
	uint32_t cu_num, i;
	bool use_ats;


	if (Priority < HSA_QUEUE_PRIORITY_MINIMUM ||
		Priority > HSA_QUEUE_PRIORITY_MAXIMUM)
		return DEVICE_STATUS_INVALID_PARAMETER;

	result = topo_->validate_nodeid(NodeId, &gpu_id);
	if (result != DEVICE_STATUS_SUCCESS)
		return result;

	use_ats = true; // TODO prefer_ats(NodeId);

	dev_id = topo_->get_device_id_by_node_id(NodeId);
	dev_info = device_->get_device_info_by_dev_id(dev_id);

	struct queue *q = (queue*)mm_->allocate_exec_aligned_memory(sizeof(*q), use_ats, NodeId, false);
	if (!q)
		return DEVICE_STATUS_NO_MEMORY;

	memset(q, 0, sizeof(*q));

	q->use_ats = use_ats;
	q->dev_info = dev_info;

	/* By default, CUs are all turned on. Initialize cu_mask to '1
	 * for all CU bits.
	 */
	if (device_->GetNodeProperties(NodeId, &props))
		q->cu_mask_count = 0;
	else {
		cu_num = props->NumFComputeCores / props->NumSIMDPerCU;
		/* cu_mask_count counts bits. It must be multiple of 32 */
		q->cu_mask_count = alignUp(cu_num, 32);
		for (i = 0; i < cu_num; i++)
			q->cu_mask[i/32] |= (1 << (i % 32));
	}

	struct ioctl_create_queue_args args = {0};

	args.gpu_id = gpu_id;

	switch (Type) {
	case HSA_QUEUE_COMPUTE:
		args.queue_type = KFD_IOC_QUEUE_TYPE_COMPUTE;
		break;
        /*
	case HSA_QUEUE_SDMA:
		args.queue_type = KFD_IOC_QUEUE_TYPE_SDMA;
		break;
	case HSA_QUEUE_SDMA_XGMI:
		args.queue_type = KFD_IOC_QUEUE_TYPE_SDMA_XGMI;
		break;
	case HSA_QUEUE_COMPUTE_AQL:
		args.queue_type = KFD_IOC_QUEUE_TYPE_COMPUTE_AQL;
		break;
        */
	default:
		return DEVICE_STATUS_INVALID_PARAMETER;
	}
/*
	if (Type != HSA_QUEUE_COMPUTE_AQL) {
		QueueResource->QueueRptrValue = (uintptr_t)&q->rptr;
		QueueResource->QueueWptrValue = (uintptr_t)&q->wptr;
	}
*/
	err = handle_concrete_asic(q, &args, NodeId);
	if (err != DEVICE_STATUS_SUCCESS) {
		free_queue(q);
		return DEVICE_STATUS_ERROR;
	}

    // after doorbel is created, the doorbel address is knon pass to emulated cp
	args.read_pointer_address = QueueResource->QueueRptrValue;
	args.write_pointer_address = QueueResource->QueueWptrValue;
	args.ring_base_address = (uintptr_t)QueueAddress;
	args.ring_size = QueueSizeInBytes;
	args.queue_percentage = QueuePercentage;
	// args.queue_priority = priority_map[Priority+3];
	//args.doorbell_base = (uint64_t)doorbells[NodeId].mapping;
	//args.doorbell_offset = doorbell_offset;

	err = cmd_create_queue(&args);
	if (err == -1) {
		free_queue(q);
		return DEVICE_STATUS_ERROR;
	}
	q->queue_id = args.queue_id;

	/* On older chips, the doorbell offset within the
	 * doorbell page is based on the queue ID.
	 */
	doorbell_mmap_offset = args.doorbell_offset & ~(uint64_t)(doorbells_->get_doorbell(NodeId)->size_ - 1);
	doorbell_offset = args.doorbell_offset & (doorbells_->get_doorbell(NodeId)->size_ - 1);
	// doorbell_offset = q->queue_id * dev_info->doorbell_size;

	err = doorbells_->map_doorbell(NodeId, gpu_id, doorbell_mmap_offset);

	if (err != DEVICE_STATUS_SUCCESS) {
		device_->DestroyQueue(q->queue_id);
		free_queue(q);
		return DEVICE_STATUS_ERROR;
	}


	QueueResource->QueueId = (uint64_t)(q);
	QueueResource->Queue_DoorBell = (uint32_t*)VOID_PTR_ADD(doorbells_->get_doorbell(NodeId)->mapping_,
						     doorbell_offset);

	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::UpdateQueue(HSA_QUEUEID QueueId,
					  uint32_t QueuePercentage,
					  HSA_QUEUE_PRIORITY Priority,
					  void *QueueAddress,
					  uint64_t QueueSize,
					  HsaEvent *Event)
{
	struct ioctl_update_queue_args arg = {0};
	struct queue *q = (queue*)(QueueId);


	if (Priority < HSA_QUEUE_PRIORITY_MINIMUM ||
		Priority > HSA_QUEUE_PRIORITY_MAXIMUM)
		return DEVICE_STATUS_INVALID_PARAMETER;

	if (!q)
		return DEVICE_STATUS_INVALID_PARAMETER;
	arg.queue_id = (uint32_t)q->queue_id;
	arg.ring_base_address = (uintptr_t)QueueAddress;
	arg.ring_size = QueueSize;
	arg.queue_percentage = QueuePercentage;
	//arg.queue_priority = priority_map[Priority+3];

	int err = cmd_update_queue(&arg);

	if (err == -1)
		return DEVICE_STATUS_ERROR;
	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::DestroyQueue(HSA_QUEUEID QueueId)
{

	struct queue *q = (queue*)(QueueId);
	struct ioctl_destroy_queue_args args = {0};

	if (!q)
		return DEVICE_STATUS_INVALID_PARAMETER;

	args.queue_id = q->queue_id;

	int err = cmd_destroy_queue(&args);

	if (err == -1)
		return DEVICE_STATUS_ERROR;

	free_queue(q);
	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::SetQueueCUMask(HSA_QUEUEID QueueId,
					     uint32_t CUMaskCount,
					     uint32_t *QueueCUMask)
{
	struct queue *q = (queue*)(QueueId);
	struct ioctl_set_cu_mask_args args = {0};

	if (CUMaskCount == 0 || !QueueCUMask || ((CUMaskCount % 32) != 0))
		return DEVICE_STATUS_INVALID_PARAMETER;

	args.queue_id = q->queue_id;
	args.num_cu_mask = CUMaskCount;
	args.cu_mask_ptr = (uintptr_t)QueueCUMask;

	int err = cmd_set_cu_mask(&args);

	if (err == -1)
		return DEVICE_STATUS_ERROR;

	memcpy(q->cu_mask, QueueCUMask, CUMaskCount / 8);
	q->cu_mask_count = CUMaskCount;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::GetQueueInfo(HSA_QUEUEID QueueId, HsaQueueInfo *QueueInfo)
{
	struct queue *q = (queue*)(QueueId);
	struct ioctl_get_queue_wave_state_args args = {0};

	if (QueueInfo == NULL || q == NULL)
		return DEVICE_STATUS_INVALID_PARAMETER;

	if (q->ctx_save_restore == NULL)
		return DEVICE_STATUS_ERROR;

	args.queue_id = q->queue_id;
	args.ctl_stack_address = (uintptr_t)q->ctx_save_restore;

	if (cmd_get_queue_wave_state(&args) < 0)
		return DEVICE_STATUS_ERROR;

	QueueInfo->ControlStackTop = (uint32_t*)(args.ctl_stack_address +
				q->ctl_stack_size - args.ctl_stack_used_size);
	QueueInfo->UserContextSaveArea = (uint32_t *) (args.ctl_stack_address + q->ctl_stack_size);
	QueueInfo->SaveAreaSizeInBytes = args.save_area_used_size;
	QueueInfo->ControlStackUsedInBytes = args.ctl_stack_used_size;
	QueueInfo->NumCUAssigned = q->cu_mask_count;
	QueueInfo->CUMaskInfo = q->cu_mask;
	QueueInfo->QueueDetailError = 0;
	QueueInfo->QueueTypeExtended = 0;
	QueueInfo->SaveAreaHeader = (HsaUserContextSaveAreaHeader *)q->ctx_save_restore;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t QueueMgr::AllocQueueGWS(HSA_QUEUEID        QueueId,
                uint32_t          nGWS,
                uint32_t          *firstGWS)
{
	struct ioctl_alloc_queue_gws_args args = {0};
	struct queue *q = (queue*)(QueueId);

	args.queue_id = (uint32_t)q->queue_id;
	args.num_gws = nGWS;

	int err = cmd_alloc_queue_gws(&args);

	if (!err && firstGWS)
		*firstGWS = args.first_gws;

	if (!err)
		return DEVICE_STATUS_SUCCESS;
	else if (err == -EINVAL)
		return DEVICE_STATUS_INVALID_PARAMETER;
	else if (err == -EBUSY)
		return DEVICE_STATUS_OUT_OF_RESOURCES;
	else if (err == -ENODEV)
		return DEVICE_STATUS_NOT_SUPPORTED;
	else
		return DEVICE_STATUS_ERROR;
}

uint32_t *QueueMgr::convert_queue_ids(uint32_t NumQueues, HSA_QUEUEID *Queues)
{
	uint32_t *queue_ids_ptr;
	unsigned int i;

	queue_ids_ptr = (uint32_t*)malloc(NumQueues * sizeof(uint32_t));
	if (!queue_ids_ptr)
		return NULL;

	for (i = 0; i < NumQueues; i++) {
		struct queue *q = (queue*)(Queues[i]);

		queue_ids_ptr[i] = q->queue_id;
	}
	return queue_ids_ptr;
}

