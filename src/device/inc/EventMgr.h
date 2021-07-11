#pragma once
#include "inc/Vma.h"
#include "inc/GpuMemory.h"
#include "device_type.h"
#include <stdint.h>

class Device;
class MemMgr;
class EventMgr {
public:
    void clear_events_page(void) {
	    events_page = NULL;
    }
    bool IsSystemEventType(EVENTTYPE type) {
	    return (type != EVENTTYPE_SIGNAL && type != EVENTTYPE_DEBUG_EVENT);
    }

    device_status_t CreateEvent(HsaEventDescriptor *EventDesc,
					  bool ManualReset, bool IsSignaled, HsaEvent **Event);
    device_status_t DestroyEvent(HsaEvent *Event);
    device_status_t SetEvent(HsaEvent *Event);
    device_status_t ResetEvent(HsaEvent *Event);
    device_status_t QueryEventState(HsaEvent *Event);

    device_status_t WaitOnEvent(HsaEvent *Event,
		uint32_t Milliseconds);

    device_status_t WaitOnMultipleEvents(HsaEvent *Events[],
						   uint32_t NumEvents,
						   bool WaitOnAll,
						   uint32_t Milliseconds);
public:
    Device* device_;
    MemMgr* mm_;
    uint64_t *events_page = NULL;
    KernelMutex event_lock_;
};
