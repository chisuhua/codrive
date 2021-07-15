
#ifndef OPU_EVENTS_H_INCLUDED
#define OPU_EVENTS_H_INCLUDED

#include <linux/kernel.h>
#include <linux/hashtable.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>
#include "opu_priv.h"
#include <uapi/linux/opu_ioctl.h>

/*
 * IDR supports non-negative integer IDs. Small IDs are used for
 * signal events to match their signal slot. Use the upper half of the
 * ID space for non-signal events.
 */
#define OPU_FIRST_NONSIGNAL_EVENT_ID ((INT_MAX >> 1) + 1)
#define OPU_LAST_NONSIGNAL_EVENT_ID INT_MAX

/*
 * Written into opu_signal_slot_t to indicate that the event is not signaled.
 * Since the event protocol may need to write the event ID into memory, this
 * must not be a valid event ID.
 * For the sake of easy memset-ing, this must be a byte pattern.
 */
#define UNSIGNALED_EVENT_SLOT ((uint64_t)-1)

struct opu_event_waiter;
struct signal_page;

struct opu_event {
	u32 event_id;

	bool signaled;
	bool auto_reset;

	int type;

	wait_queue_head_t wq; /* List of event waiters. */

	/* Only for signal events. */
	uint64_t __user *user_signal_address;

	/* type specific data */
	union {
		struct opu_hsa_memory_exception_data memory_exception_data;
		struct opu_hsa_hw_exception_data hw_exception_data;
	};
};

#define OPU_EVENT_TIMEOUT_IMMEDIATE 0
#define OPU_EVENT_TIMEOUT_INFINITE 0xFFFFFFFFu

/* Matching HSA_EVENTTYPE */
#define OPU_EVENT_TYPE_SIGNAL 0
#define OPU_EVENT_TYPE_HW_EXCEPTION 3
#define OPU_EVENT_TYPE_DEBUG 5
#define OPU_EVENT_TYPE_MEMORY 8

extern void opu_signal_event_interrupt(u32 pasid, uint32_t partial_id,
				       uint32_t valid_id_bits);

#endif
