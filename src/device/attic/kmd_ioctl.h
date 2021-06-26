#ifndef KMD_IOCTL_H_INCLUDED
#define KMD_IOCTL_H_INCLUDED


// temp add file, should move to KMD CSI 


#include <linux/types.h>
#include <linux/ioctl.h>

/* Matching HSA_EVENTTYPE */
#define KFD_IOC_EVENT_SIGNAL		0
#define KFD_IOC_EVENT_NODECHANGE	1
#define KFD_IOC_EVENT_DEVICESTATECHANGE	2
#define KFD_IOC_EVENT_HW_EXCEPTION	3
#define KFD_IOC_EVENT_SYSTEM_EVENT	4
#define KFD_IOC_EVENT_DEBUG_EVENT	5
#define KFD_IOC_EVENT_PROFILE_EVENT	6
#define KFD_IOC_EVENT_QUEUE_EVENT	7
#define KFD_IOC_EVENT_MEMORY		8

#define KFD_IOC_WAIT_RESULT_COMPLETE	0
#define KFD_IOC_WAIT_RESULT_TIMEOUT	1
#define KFD_IOC_WAIT_RESULT_FAIL	2


#define KMD_SIGNAL_EVENT_LIMIT		4096

struct kmd_ioctl_create_event_args {
	uint64_t event_page_offset;	/* from KFD */
	uint32_t event_trigger_data;	/* from KFD - signal events only */
	uint32_t event_type;		/* to KFD */
	uint32_t auto_reset;		/* to KFD */
	uint32_t node_id;		/* to KFD - only valid for certain
							event types */
	uint32_t event_id;		/* from KFD */
	uint32_t event_slot_index;	/* from KFD */
};

struct kmd_ioctl_destroy_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct kmd_ioctl_set_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct kmd_ioctl_reset_event_args {
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct kmd_memory_exception_failure {
	uint32_t NotPresent;	/* Page not present or supervisor privilege */
	uint32_t ReadOnly;	/* Write access to a read-only page */
	uint32_t NoExecute;	/* Execute access to a page marked NX */
	uint32_t imprecise;	/* Can't determine the	exact fault address */
};


/* memory exception data */
struct kmd_hsa_memory_exception_data {
	struct kmd_memory_exception_failure failure;
	uint64_t va;
	uint32_t gpu_id;
	uint32_t pad;
};


/* Event data */
struct kmd_event_data {
	union {
		struct kmd_hsa_memory_exception_data memory_exception_data;
	};				/* From KFD */
	uint64_t kmd_event_data_ext;	/* pointer to an extension structure
	 	 	 	 	   for future exception types */
	uint32_t event_id;		/* to KFD */
	uint32_t pad;
};

struct kmd_ioctl_wait_events_args {
	uint64_t events_ptr;		/* pointed to struct
					   kfd_event_data array, to KFD */
	uint32_t num_events;		/* to KFD */
	uint32_t wait_for_all;		/* to KFD */
	uint32_t timeout;		/* to KFD */
	uint32_t wait_result;		/* from KFD */
};

#endif
