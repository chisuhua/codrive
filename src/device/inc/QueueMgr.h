#pragma once
#include <stdint.h>
#include "inc/hsakmttypes.h"
#include "inc/Device.h"
#include "inc/Doorbells.h"

struct queue {
	uint32_t queue_id;
	uint64_t wptr;
	uint64_t rptr;
	void *eop_buffer;
	void *ctx_save_restore;
	uint32_t ctx_save_restore_size;
	uint32_t ctl_stack_size;
	const struct device_info *dev_info;
	bool use_ats;
	/* This queue structure is allocated from GPU with page aligned size
	 * but only small bytes are used. We use the extra space in the end for
	 * cu_mask bits array.
	 */
	uint32_t cu_mask_count; /* in bits */
	uint32_t cu_mask[0];
};

typedef uint64_t          HSA_QUEUEID;

class Device;
class MemMgr;
class Topology;

class QueueMgr {
    QueueMgr(Device* device, MemMgr* mm)
        : device_(device)
        , mm_(mm) {
            topo_ = device_->get_topo();
            doorbells_ = device_->doorbells_;
        };
    bool update_ctx_save_restore_size(uint32_t nodeid, struct queue *q);
    void free_queue(struct queue *q);
    int handle_concrete_asic(struct queue *q,
				struct ioctl_create_queue_args *args,
				uint32_t NodeId);
   // uint32_t *convert_queue_ids(uint32_t NumQueues, HSA_QUEUEID *Queues);

    device_status_t CreateQueue(uint32_t NodeId,
					  HSA_QUEUE_TYPE Type,
					  uint32_t QueuePercentage,
					  HSA_QUEUE_PRIORITY Priority,
					  void *QueueAddress,
					  uint64_t QueueSizeInBytes,
					  HsaEvent *Event,
					  HsaQueueResource *QueueResource);

    device_status_t UpdateQueue(HSA_QUEUEID QueueId,
					  uint32_t QueuePercentage,
					  HSA_QUEUE_PRIORITY Priority,
					  void *QueueAddress,
					  uint64_t QueueSize,
					  HsaEvent *Event);

    device_status_t DestroyQueue(HSA_QUEUEID QueueId);
    device_status_t AllocQueueGWS(HSA_QUEUEID        QueueId,
                uint32_t          nGWS,
                uint32_t          *firstGWS);
    device_status_t SetQueueCUMask(HSA_QUEUEID QueueId,
					     uint32_t CUMaskCount,
					     uint32_t *QueueCUMask);

    device_status_t GetQueueInfo(HSA_QUEUEID QueueId, HsaQueueInfo *QueueInfo);

public:
    Device* device_;
    MemMgr* mm_;
    Topology* topo_;
    Doorbells* doorbells_;

/* A map to translate thunk queue priority (-3 to +3)
 * to KFD queue priority (0 to 15)
 * Indexed by thunk_queue_priority+3
 */
    // uint32_t priority_map[] = {0, 3, 5, 7, 9, 11, 15};
};
