#include "inc/EventMgr.h"
#include "inc/Device.h"
#include "inc/MemMgr.h"
#include "inc/Topology.h"
#include <sys/mman.h>
#include <cstring>

device_status_t EventMgr::CreateEvent(HsaEventDescriptor *EventDesc,
					  bool ManualReset, bool IsSignaled, HsaEvent **Event)
{
	unsigned int event_limit = KFD_SIGNAL_EVENT_LIMIT;

	if (EventDesc->EventType >= EVENTTYPE_MAXID)
		return DEVICE_STATUS_INVALID_PARAMETER;

	HsaEvent *e = (HsaEvent*)malloc(sizeof(HsaEvent));

	if (!e)
		return DEVICE_STATUS_ERROR;

	memset(e, 0, sizeof(*e));

	struct ioctl_create_event_args args = {0};


	args.event_type = EventDesc->EventType;
	args.node_id = EventDesc->NodeId;
	args.auto_reset = !ManualReset;

	/* dGPU code */
    {
        ScopedAcquire<KernelMutex> lock(&event_lock_);
	    if (device_->is_dgpu() && !events_page) {
		    events_page = (uint64_t*)mm_->allocate_exec_aligned_memory_gpu( KFD_SIGNAL_EVENT_LIMIT * 8, mm_->PAGE_SIZE, 0, true, false);
		    if (!events_page) {
			    return DEVICE_STATUS_ERROR;
		    }
		    mm_->mm_get_handle(events_page, &args.event_page_offset);
        }

	    if (cmd_create_event(&args) != 0) {
		    free(e);
		    *Event = NULL;
		    return DEVICE_STATUS_ERROR;
	    }

	    e->EventId = args.event_id;

	    if (!events_page && args.event_page_offset > 0) {
	        struct ioctl_mmap_args mmap_args = {0};
            mmap_args.length = event_limit * 8;
            mmap_args.prot = PROT_WRITE | PROT_READ;
            mmap_args.flags = MAP_SHARED;
            mmap_args.offset = args.event_page_offset;
		    cmd_mmap(&mmap_args);
		    events_page = (uint64_t*)mmap_args.start;
#if 0
		    if (events_page == MAP_FAILED) {
			    // old kernels only support 256 events 
			    event_limit = 256;
                mmap_args.length = PAGE_SIZE;
			    events_page = cmd_mmap(&mmap_args);
		    }
#endif
		    if (events_page == MAP_FAILED) {
			    events_page = NULL;
			    DestroyEvent(e);
			    return DEVICE_STATUS_ERROR;
		    }
	    }
	}

	if (args.event_page_offset > 0 && args.event_slot_index < event_limit)
		e->EventData.HWData2 = (uint64_t)&events_page[args.event_slot_index];

	e->EventData.EventType = EventDesc->EventType;
	e->EventData.HWData1 = args.event_id;

	e->EventData.HWData3 = args.event_trigger_data;
	e->EventData.EventData.SyncVar.SyncVar.UserData = EventDesc->SyncVar.SyncVar.UserData;
	e->EventData.EventData.SyncVar.SyncVarSize = EventDesc->SyncVar.SyncVarSize;

	if (IsSignaled && !IsSystemEventType(e->EventData.EventType)) {
		struct ioctl_set_event_args set_args = {0};

		set_args.event_id = args.event_id;

		cmd_set_event(&set_args);
	}
	*Event = e;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t EventMgr::DestroyEvent(HsaEvent *Event)
{

	if (!Event)
		return DEVICE_STATUS_INVALID_HANDLE;

	struct ioctl_destroy_event_args args = {0};

	args.event_id = Event->EventId;

	if (cmd_destroy_event(&args) != 0)
		return DEVICE_STATUS_ERROR;

	free(Event);
	return DEVICE_STATUS_SUCCESS;
}

device_status_t EventMgr::SetEvent(HsaEvent *Event)
{

	if (!Event)
		return DEVICE_STATUS_INVALID_HANDLE;

	/* Although the spec is doesn't say, don't allow system-defined events
	 * to be signaled.
	 */
	if (IsSystemEventType(Event->EventData.EventType))
		return DEVICE_STATUS_ERROR;

	struct ioctl_set_event_args args = {0};

	args.event_id = Event->EventId;

	if (cmd_set_event(&args) == -1)
		return DEVICE_STATUS_ERROR;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t EventMgr::ResetEvent(HsaEvent *Event)
{

	if (!Event)
		return DEVICE_STATUS_INVALID_HANDLE;

	/* Although the spec is doesn't say, don't allow system-defined events
	 * to be signaled.
	 */
	if (IsSystemEventType(Event->EventData.EventType))
		return DEVICE_STATUS_ERROR;

	struct ioctl_reset_event_args args = {0};

	args.event_id = Event->EventId;

	if (cmd_reset_event(&args) == -1)
		return DEVICE_STATUS_ERROR;

	return DEVICE_STATUS_SUCCESS;
}

device_status_t EventMgr::QueryEventState(HsaEvent *Event)
{
	if (!Event)
		return DEVICE_STATUS_INVALID_HANDLE;

	return DEVICE_STATUS_SUCCESS;
}


device_status_t EventMgr::WaitOnEvent(HsaEvent *Event,
		HSAuint32 Milliseconds)
{
	if (!Event)
		return DEVICE_STATUS_INVALID_HANDLE;

	return WaitOnMultipleEvents(&Event, 1, true, Milliseconds);
}

device_status_t EventMgr::WaitOnMultipleEvents(HsaEvent *Events[],
						   HSAuint32 NumEvents,
						   bool WaitOnAll,
						   HSAuint32 Milliseconds)
{
	if (!Events)
		return DEVICE_STATUS_INVALID_HANDLE;

	struct cmd_event_data *event_data = (cmd_event_data*)calloc(NumEvents, sizeof(struct cmd_event_data));

	for (HSAuint32 i = 0; i < NumEvents; i++) {
		event_data[i].event_id = Events[i]->EventId;
		event_data[i].cmd_event_data_ext = (uint64_t)(uintptr_t)NULL;
	}

	struct ioctl_wait_events_args args = {0};

	args.wait_for_all = WaitOnAll;
	args.timeout = Milliseconds;
	args.num_events = NumEvents;
	args.events_ptr = (uint64_t)(uintptr_t)event_data;

	device_status_t result = DEVICE_STATUS_SUCCESS;

	if (cmd_wait_events(&args) == -1)
		result = DEVICE_STATUS_ERROR;
	else if (args.wait_result == KFD_IOC_WAIT_RESULT_TIMEOUT)
		result = DEVICE_STATUS_WAIT_TIMEOUT;
	else {
		result = DEVICE_STATUS_SUCCESS;
		for (HSAuint32 i = 0; i < NumEvents; i++) {
			if (Events[i]->EventData.EventType == EVENTTYPE_MEMORY &&
			    event_data[i].memory_exception_data.gpu_id) {
				Events[i]->EventData.EventData.MemoryAccessFault.VirtualAddress = event_data[i].memory_exception_data.va;
				result = device_->get_topo()->gpuid_to_nodeid(event_data[i].memory_exception_data.gpu_id, &Events[i]->EventData.EventData.MemoryAccessFault.NodeId);
				if (result != DEVICE_STATUS_SUCCESS)
					goto out;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.NotPresent = event_data[i].memory_exception_data.failure.NotPresent;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.ReadOnly = event_data[i].memory_exception_data.failure.ReadOnly;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.NoExecute = event_data[i].memory_exception_data.failure.NoExecute;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.Imprecise = event_data[i].memory_exception_data.failure.imprecise;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.ErrorType = event_data[i].memory_exception_data.ErrorType;
				Events[i]->EventData.EventData.MemoryAccessFault.Failure.ECC =
						((event_data[i].memory_exception_data.ErrorType == 1) || (event_data[i].memory_exception_data.ErrorType == 2)) ? 1 : 0;
				Events[i]->EventData.EventData.MemoryAccessFault.Flags = EVENTID_MEMORY_FATAL_PROCESS;
				// TODO analysis_memory_exception(&event_data[i].memory_exception_data);
			}
		}
	}
out:
	free(event_data);

	return result;
}
