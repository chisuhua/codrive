
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/memory.h>
#include "opu_priv.h"
#include "opu_events.h"
#include "opu_iommu.h"
#include <linux/device.h>

/*
 * Wrapper around wait_queue_entry_t
 */
struct opu_event_waiter {
	wait_queue_entry_t wait;
	struct opu_event *event; /* Event to wait for */
	bool activated;		 /* Becomes true when event is signaled */
};

/*
 * Each signal event needs a 64-bit signal slot where the signaler will write
 * a 1 before sending an interrupt. (This is needed because some interrupts
 * do not contain enough spare data bits to identify an event.)
 * We get whole pages and map them to the process VA.
 * Individual signal events use their event_id as slot index.
 */
struct opu_signal_page {
	uint64_t *kernel_address;
	uint64_t __user *user_address;
	bool need_to_free_pages;
};


static uint64_t *page_slots(struct opu_signal_page *page)
{
	return page->kernel_address;
}

static struct opu_signal_page *allocate_signal_page(struct opu_process *p)
{
	void *backing_store;
	struct opu_signal_page *page;

	page = kzalloc(sizeof(*page), GFP_KERNEL);
	if (!page)
		return NULL;

	backing_store = (void *) __get_free_pages(GFP_KERNEL,
					get_order(OPU_SIGNAL_EVENT_LIMIT * 8));
	if (!backing_store)
		goto fail_alloc_signal_store;

	/* Initialize all events to unsignaled */
	memset(backing_store, (uint8_t) UNSIGNALED_EVENT_SLOT,
	       OPU_SIGNAL_EVENT_LIMIT * 8);

	page->kernel_address = backing_store;
	page->need_to_free_pages = true;
	pr_debug("Allocated new event signal page at %p, for process %p\n",
			page, p);

	return page;

fail_alloc_signal_store:
	kfree(page);
	return NULL;
}

static int allocate_event_notification_slot(struct opu_process *p,
					    struct opu_event *ev)
{
	int id;

	if (!p->signal_page) {
		p->signal_page = allocate_signal_page(p);
		if (!p->signal_page)
			return -ENOMEM;
		/* Oldest user mode expects 256 event slots */
		p->signal_mapped_size = 256*8;
	}

	/*
	 * Compatibility with old user mode: Only use signal slots
	 * user mode has mapped, may be less than
	 * OPU_SIGNAL_EVENT_LIMIT. This also allows future increase
	 * of the event limit without breaking user mode.
	 */
	id = idr_alloc(&p->event_idr, ev, 0, p->signal_mapped_size / 8,
		       GFP_KERNEL);
	if (id < 0)
		return id;

	ev->event_id = id;
	page_slots(p->signal_page)[id] = UNSIGNALED_EVENT_SLOT;

	return 0;
}

/*
 * Assumes that p->event_mutex is held and of course that p is not going
 * away (current or locked).
 */
static struct opu_event *lookup_event_by_id(struct opu_process *p, uint32_t id)
{
	return idr_find(&p->event_idr, id);
}

/**
 * lookup_signaled_event_by_partial_id - Lookup signaled event from partial ID
 * @p:     Pointer to struct opu_process
 * @id:    ID to look up
 * @bits:  Number of valid bits in @id
 *
 * Finds the first signaled event with a matching partial ID. If no
 * matching signaled event is found, returns NULL. In that case the
 * caller should assume that the partial ID is invalid and do an
 * exhaustive search of all siglaned events.
 *
 * If multiple events with the same partial ID signal at the same
 * time, they will be found one interrupt at a time, not necessarily
 * in the same order the interrupts occurred. As long as the number of
 * interrupts is correct, all signaled events will be seen by the
 * driver.
 */
static struct opu_event *lookup_signaled_event_by_partial_id(
	struct opu_process *p, uint32_t id, uint32_t bits)
{
	struct opu_event *ev;

	if (!p->signal_page || id >= OPU_SIGNAL_EVENT_LIMIT)
		return NULL;

	/* Fast path for the common case that @id is not a partial ID
	 * and we only need a single lookup.
	 */
	if (bits > 31 || (1U << bits) >= OPU_SIGNAL_EVENT_LIMIT) {
		if (page_slots(p->signal_page)[id] == UNSIGNALED_EVENT_SLOT)
			return NULL;

		return idr_find(&p->event_idr, id);
	}

	/* General case for partial IDs: Iterate over all matching IDs
	 * and find the first one that has signaled.
	 */
	for (ev = NULL; id < OPU_SIGNAL_EVENT_LIMIT && !ev; id += 1U << bits) {
		if (page_slots(p->signal_page)[id] == UNSIGNALED_EVENT_SLOT)
			continue;

		ev = idr_find(&p->event_idr, id);
	}

	return ev;
}

static int create_signal_event(struct file *devopu,
				struct opu_process *p,
				struct opu_event *ev)
{
	int ret;

	if (p->signal_mapped_size &&
	    p->signal_event_count == p->signal_mapped_size / 8) {
		if (!p->signal_event_limit_reached) {
			pr_debug("Signal event wasn't created because limit was reached\n");
			p->signal_event_limit_reached = true;
		}
		return -ENOSPC;
	}

	ret = allocate_event_notification_slot(p, ev);
	if (ret) {
		pr_warn("Signal event wasn't created because out of kernel memory\n");
		return ret;
	}

	p->signal_event_count++;

	ev->user_signal_address = &p->signal_page->user_address[ev->event_id];
	pr_debug("Signal event number %zu created with id %d, address %p\n",
			p->signal_event_count, ev->event_id,
			ev->user_signal_address);

	return 0;
}

static int create_other_event(struct opu_process *p, struct opu_event *ev)
{
	/* Cast OPU_LAST_NONSIGNAL_EVENT to uint32_t. This allows an
	 * intentional integer overflow to -1 without a compiler
	 * warning. idr_alloc treats a negative value as "maximum
	 * signed integer".
	 */
	int id = idr_alloc(&p->event_idr, ev, OPU_FIRST_NONSIGNAL_EVENT_ID,
			   (uint32_t)OPU_LAST_NONSIGNAL_EVENT_ID + 1,
			   GFP_KERNEL);

	if (id < 0)
		return id;
	ev->event_id = id;

	return 0;
}

void opu_event_init_process(struct opu_process *p)
{
	mutex_init(&p->event_mutex);
	idr_init(&p->event_idr);
	p->signal_page = NULL;
	p->signal_event_count = 0;
}

static void destroy_event(struct opu_process *p, struct opu_event *ev)
{
	struct opu_event_waiter *waiter;

	/* Wake up pending waiters. They will return failure */
	list_for_each_entry(waiter, &ev->wq.head, wait.entry)
		waiter->event = NULL;
	wake_up_all(&ev->wq);

	if (ev->type == OPU_EVENT_TYPE_SIGNAL ||
	    ev->type == OPU_EVENT_TYPE_DEBUG)
		p->signal_event_count--;

	idr_remove(&p->event_idr, ev->event_id);
	kfree(ev);
}

static void destroy_events(struct opu_process *p)
{
	struct opu_event *ev;
	uint32_t id;

	idr_for_each_entry(&p->event_idr, ev, id)
		destroy_event(p, ev);
	idr_destroy(&p->event_idr);
}

/*
 * We assume that the process is being destroyed and there is no need to
 * unmap the pages or keep bookkeeping data in order.
 */
static void shutdown_signal_page(struct opu_process *p)
{
	struct opu_signal_page *page = p->signal_page;

	if (page) {
		if (page->need_to_free_pages)
			free_pages((unsigned long)page->kernel_address,
				   get_order(OPU_SIGNAL_EVENT_LIMIT * 8));
		kfree(page);
	}
}

void opu_event_free_process(struct opu_process *p)
{
	destroy_events(p);
	shutdown_signal_page(p);
}

static bool event_can_be_gpu_signaled(const struct opu_event *ev)
{
	return ev->type == OPU_EVENT_TYPE_SIGNAL ||
					ev->type == OPU_EVENT_TYPE_DEBUG;
}

static bool event_can_be_cpu_signaled(const struct opu_event *ev)
{
	return ev->type == OPU_EVENT_TYPE_SIGNAL;
}

int opu_event_page_set(struct opu_process *p, void *kernel_address,
		       uint64_t size)
{
	struct opu_signal_page *page;

	if (p->signal_page)
		return -EBUSY;

	page = kzalloc(sizeof(*page), GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	/* Initialize all events to unsignaled */
	memset(kernel_address, (uint8_t) UNSIGNALED_EVENT_SLOT,
	       OPU_SIGNAL_EVENT_LIMIT * 8);

	page->kernel_address = kernel_address;

	p->signal_page = page;
	p->signal_mapped_size = size;

	return 0;
}

int opu_event_create(struct file *devopu, struct opu_process *p,
		     uint32_t event_type, bool auto_reset, uint32_t node_id,
		     uint32_t *event_id, uint32_t *event_trigger_data,
		     uint64_t *event_page_offset, uint32_t *event_slot_index)
{
	int ret = 0;
	struct opu_event *ev = kzalloc(sizeof(*ev), GFP_KERNEL);

	if (!ev)
		return -ENOMEM;

	ev->type = event_type;
	ev->auto_reset = auto_reset;
	ev->signaled = false;

	init_waitqueue_head(&ev->wq);

	*event_page_offset = 0;

	mutex_lock(&p->event_mutex);

	switch (event_type) {
	case OPU_EVENT_TYPE_SIGNAL:
	case OPU_EVENT_TYPE_DEBUG:
		ret = create_signal_event(devopu, p, ev);
		if (!ret) {
			*event_page_offset = OPU_MMAP_TYPE_EVENTS;
			*event_slot_index = ev->event_id;
		}
		break;
	default:
		ret = create_other_event(p, ev);
		break;
	}

	if (!ret) {
		*event_id = ev->event_id;
		*event_trigger_data = ev->event_id;
	} else {
		kfree(ev);
	}

	mutex_unlock(&p->event_mutex);

	return ret;
}

/* Assumes that p is current. */
int opu_event_destroy(struct opu_process *p, uint32_t event_id)
{
	struct opu_event *ev;
	int ret = 0;

	mutex_lock(&p->event_mutex);

	ev = lookup_event_by_id(p, event_id);

	if (ev)
		destroy_event(p, ev);
	else
		ret = -EINVAL;

	mutex_unlock(&p->event_mutex);
	return ret;
}

static void set_event(struct opu_event *ev)
{
	struct opu_event_waiter *waiter;

	/* Auto reset if the list is non-empty and we're waking
	 * someone. waitqueue_active is safe here because we're
	 * protected by the p->event_mutex, which is also held when
	 * updating the wait queues in opu_wait_on_events.
	 */
	ev->signaled = !ev->auto_reset || !waitqueue_active(&ev->wq);

	list_for_each_entry(waiter, &ev->wq.head, wait.entry)
		waiter->activated = true;

	wake_up_all(&ev->wq);
}

/* Assumes that p is current. */
int opu_set_event(struct opu_process *p, uint32_t event_id)
{
	int ret = 0;
	struct opu_event *ev;

	mutex_lock(&p->event_mutex);

	ev = lookup_event_by_id(p, event_id);

	if (ev && event_can_be_cpu_signaled(ev))
		set_event(ev);
	else
		ret = -EINVAL;

	mutex_unlock(&p->event_mutex);
	return ret;
}

static void reset_event(struct opu_event *ev)
{
	ev->signaled = false;
}

/* Assumes that p is current. */
int opu_reset_event(struct opu_process *p, uint32_t event_id)
{
	int ret = 0;
	struct opu_event *ev;

	mutex_lock(&p->event_mutex);

	ev = lookup_event_by_id(p, event_id);

	if (ev && event_can_be_cpu_signaled(ev))
		reset_event(ev);
	else
		ret = -EINVAL;

	mutex_unlock(&p->event_mutex);
	return ret;

}

static void acknowledge_signal(struct opu_process *p, struct opu_event *ev)
{
	page_slots(p->signal_page)[ev->event_id] = UNSIGNALED_EVENT_SLOT;
}

static void set_event_from_interrupt(struct opu_process *p,
					struct opu_event *ev)
{
	if (ev && event_can_be_gpu_signaled(ev)) {
		acknowledge_signal(p, ev);
		set_event(ev);
	}
}

void opu_signal_event_interrupt(u32 pasid, uint32_t partial_id,
				uint32_t valid_id_bits)
{
	struct opu_event *ev = NULL;

	/*
	 * Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	struct opu_process *p = opu_lookup_process_by_pasid(pasid);

	if (!p)
		return; /* Presumably process exited. */

	mutex_lock(&p->event_mutex);

	if (valid_id_bits)
		ev = lookup_signaled_event_by_partial_id(p, partial_id,
							 valid_id_bits);
	if (ev) {
		set_event_from_interrupt(p, ev);
	} else if (p->signal_page) {
		/*
		 * Partial ID lookup failed. Assume that the event ID
		 * in the interrupt payload was invalid and do an
		 * exhaustive search of signaled events.
		 */
		uint64_t *slots = page_slots(p->signal_page);
		uint32_t id;

		if (valid_id_bits)
			pr_debug_ratelimited("Partial ID invalid: %u (%u valid bits)\n",
					     partial_id, valid_id_bits);

		if (p->signal_event_count < OPU_SIGNAL_EVENT_LIMIT / 64) {
			/* With relatively few events, it's faster to
			 * iterate over the event IDR
			 */
			idr_for_each_entry(&p->event_idr, ev, id) {
				if (id >= OPU_SIGNAL_EVENT_LIMIT)
					break;

				if (slots[id] != UNSIGNALED_EVENT_SLOT)
					set_event_from_interrupt(p, ev);
			}
		} else {
			/* With relatively many events, it's faster to
			 * iterate over the signal slots and lookup
			 * only signaled events from the IDR.
			 */
			for (id = 0; id < OPU_SIGNAL_EVENT_LIMIT; id++)
				if (slots[id] != UNSIGNALED_EVENT_SLOT) {
					ev = lookup_event_by_id(p, id);
					set_event_from_interrupt(p, ev);
				}
		}
	}

	mutex_unlock(&p->event_mutex);
	opu_unref_process(p);
}

static struct opu_event_waiter *alloc_event_waiters(uint32_t num_events)
{
	struct opu_event_waiter *event_waiters;
	uint32_t i;

	event_waiters = kmalloc_array(num_events,
					sizeof(struct opu_event_waiter),
					GFP_KERNEL);

	for (i = 0; (event_waiters) && (i < num_events) ; i++) {
		init_wait(&event_waiters[i].wait);
		event_waiters[i].activated = false;
	}

	return event_waiters;
}

static int init_event_waiter_get_status(struct opu_process *p,
		struct opu_event_waiter *waiter,
		uint32_t event_id)
{
	struct opu_event *ev = lookup_event_by_id(p, event_id);

	if (!ev)
		return -EINVAL;

	waiter->event = ev;
	waiter->activated = ev->signaled;
	ev->signaled = ev->signaled && !ev->auto_reset;

	return 0;
}

static void init_event_waiter_add_to_waitlist(struct opu_event_waiter *waiter)
{
	struct opu_event *ev = waiter->event;

	/* Only add to the wait list if we actually need to
	 * wait on this event.
	 */
	if (!waiter->activated)
		add_wait_queue(&ev->wq, &waiter->wait);
}

/* test_event_condition - Test condition of events being waited for
 * @all:           Return completion only if all events have signaled
 * @num_events:    Number of events to wait for
 * @event_waiters: Array of event waiters, one per event
 *
 * Returns OPU_IOC_WAIT_RESULT_COMPLETE if all (or one) event(s) have
 * signaled. Returns OPU_IOC_WAIT_RESULT_TIMEOUT if no (or not all)
 * events have signaled. Returns OPU_IOC_WAIT_RESULT_FAIL if any of
 * the events have been destroyed.
 */
static uint32_t test_event_condition(bool all, uint32_t num_events,
				struct opu_event_waiter *event_waiters)
{
	uint32_t i;
	uint32_t activated_count = 0;

	for (i = 0; i < num_events; i++) {
		if (!event_waiters[i].event)
			return OPU_IOC_WAIT_RESULT_FAIL;

		if (event_waiters[i].activated) {
			if (!all)
				return OPU_IOC_WAIT_RESULT_COMPLETE;

			activated_count++;
		}
	}

	return activated_count == num_events ?
		OPU_IOC_WAIT_RESULT_COMPLETE : OPU_IOC_WAIT_RESULT_TIMEOUT;
}

/*
 * Copy event specific data, if defined.
 * Currently only memory exception events have additional data to copy to user
 */
static int copy_signaled_event_data(uint32_t num_events,
		struct opu_event_waiter *event_waiters,
		struct opu_event_data __user *data)
{
	struct opu_hsa_memory_exception_data *src;
	struct opu_hsa_memory_exception_data __user *dst;
	struct opu_event_waiter *waiter;
	struct opu_event *event;
	uint32_t i;

	for (i = 0; i < num_events; i++) {
		waiter = &event_waiters[i];
		event = waiter->event;
		if (waiter->activated && event->type == OPU_EVENT_TYPE_MEMORY) {
			dst = &data[i].memory_exception_data;
			src = &event->memory_exception_data;
			if (copy_to_user(dst, src,
				sizeof(struct opu_hsa_memory_exception_data)))
				return -EFAULT;
		}
	}

	return 0;

}



static long user_timeout_to_jiffies(uint32_t user_timeout_ms)
{
	if (user_timeout_ms == OPU_EVENT_TIMEOUT_IMMEDIATE)
		return 0;

	if (user_timeout_ms == OPU_EVENT_TIMEOUT_INFINITE)
		return MAX_SCHEDULE_TIMEOUT;

	/*
	 * msecs_to_jiffies interprets all values above 2^31-1 as infinite,
	 * but we consider them finite.
	 * This hack is wrong, but nobody is likely to notice.
	 */
	user_timeout_ms = min_t(uint32_t, user_timeout_ms, 0x7FFFFFFF);

	return msecs_to_jiffies(user_timeout_ms) + 1;
}

static void free_waiters(uint32_t num_events, struct opu_event_waiter *waiters)
{
	uint32_t i;

	for (i = 0; i < num_events; i++)
		if (waiters[i].event)
			remove_wait_queue(&waiters[i].event->wq,
					  &waiters[i].wait);

	kfree(waiters);
}

int opu_wait_on_events(struct opu_process *p,
		       uint32_t num_events, void __user *data,
		       bool all, uint32_t user_timeout_ms,
		       uint32_t *wait_result)
{
	struct opu_event_data __user *events =
			(struct opu_event_data __user *) data;
	uint32_t i;
	int ret = 0;

	struct opu_event_waiter *event_waiters = NULL;
	long timeout = user_timeout_to_jiffies(user_timeout_ms);

	event_waiters = alloc_event_waiters(num_events);
	if (!event_waiters) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&p->event_mutex);

	for (i = 0; i < num_events; i++) {
		struct opu_event_data event_data;

		if (copy_from_user(&event_data, &events[i],
				sizeof(struct opu_event_data))) {
			ret = -EFAULT;
			goto out_unlock;
		}

		ret = init_event_waiter_get_status(p, &event_waiters[i],
				event_data.event_id);
		if (ret)
			goto out_unlock;
	}

	/* Check condition once. */
	*wait_result = test_event_condition(all, num_events, event_waiters);
	if (*wait_result == OPU_IOC_WAIT_RESULT_COMPLETE) {
		ret = copy_signaled_event_data(num_events,
					       event_waiters, events);
		goto out_unlock;
	} else if (WARN_ON(*wait_result == OPU_IOC_WAIT_RESULT_FAIL)) {
		/* This should not happen. Events shouldn't be
		 * destroyed while we're holding the event_mutex
		 */
		goto out_unlock;
	}

	/* Add to wait lists if we need to wait. */
	for (i = 0; i < num_events; i++)
		init_event_waiter_add_to_waitlist(&event_waiters[i]);

	mutex_unlock(&p->event_mutex);

	while (true) {
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (signal_pending(current)) {
			/*
			 * This is wrong when a nonzero, non-infinite timeout
			 * is specified. We need to use
			 * ERESTARTSYS_RESTARTBLOCK, but struct restart_block
			 * contains a union with data for each user and it's
			 * in generic kernel code that I don't want to
			 * touch yet.
			 */
			ret = -ERESTARTSYS;
			break;
		}

		/* Set task state to interruptible sleep before
		 * checking wake-up conditions. A concurrent wake-up
		 * will put the task back into runnable state. In that
		 * case schedule_timeout will not put the task to
		 * sleep and we'll get a chance to re-check the
		 * updated conditions almost immediately. Otherwise,
		 * this race condition would lead to a soft hang or a
		 * very long sleep.
		 */
		set_current_state(TASK_INTERRUPTIBLE);

		*wait_result = test_event_condition(all, num_events,
						    event_waiters);
		if (*wait_result != OPU_IOC_WAIT_RESULT_TIMEOUT)
			break;

		if (timeout <= 0)
			break;

		timeout = schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);

	/* copy_signaled_event_data may sleep. So this has to happen
	 * after the task state is set back to RUNNING.
	 */
	if (!ret && *wait_result == OPU_IOC_WAIT_RESULT_COMPLETE)
		ret = copy_signaled_event_data(num_events,
					       event_waiters, events);

	mutex_lock(&p->event_mutex);
out_unlock:
	free_waiters(num_events, event_waiters);
	mutex_unlock(&p->event_mutex);
out:
	if (ret)
		*wait_result = OPU_IOC_WAIT_RESULT_FAIL;
	else if (*wait_result == OPU_IOC_WAIT_RESULT_FAIL)
		ret = -EIO;

	return ret;
}

int opu_event_mmap(struct opu_process *p, struct vm_area_struct *vma)
{
	unsigned long pfn;
	struct opu_signal_page *page;
	int ret;

	/* check required size doesn't exceed the allocated size */
	if (get_order(OPU_SIGNAL_EVENT_LIMIT * 8) <
			get_order(vma->vm_end - vma->vm_start)) {
		pr_err("Event page mmap requested illegal size\n");
		return -EINVAL;
	}

	page = p->signal_page;
	if (!page) {
		/* Probably OPU bug, but mmap is user-accessible. */
		pr_debug("Signal page could not be found\n");
		return -EINVAL;
	}

	pfn = __pa(page->kernel_address);
	pfn >>= PAGE_SHIFT;

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE
		       | VM_DONTDUMP | VM_PFNMAP;

	pr_debug("Mapping signal page\n");
	pr_debug("     start user address  == 0x%08lx\n", vma->vm_start);
	pr_debug("     end user address    == 0x%08lx\n", vma->vm_end);
	pr_debug("     pfn                 == 0x%016lX\n", pfn);
	pr_debug("     vm_flags            == 0x%08lX\n", vma->vm_flags);
	pr_debug("     size                == 0x%08lX\n",
			vma->vm_end - vma->vm_start);

	page->user_address = (uint64_t __user *)vma->vm_start;

	/* mapping the page to user process */
	ret = remap_pfn_range(vma, vma->vm_start, pfn,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (!ret)
		p->signal_mapped_size = vma->vm_end - vma->vm_start;

	return ret;
}

/*
 * Assumes that p->event_mutex is held and of course
 * that p is not going away (current or locked).
 */
static void lookup_events_by_type_and_signal(struct opu_process *p,
		int type, void *event_data)
{
	struct opu_hsa_memory_exception_data *ev_data;
	struct opu_event *ev;
	uint32_t id;
	bool send_signal = true;

	ev_data = (struct opu_hsa_memory_exception_data *) event_data;

	id = OPU_FIRST_NONSIGNAL_EVENT_ID;
	idr_for_each_entry_continue(&p->event_idr, ev, id)
		if (ev->type == type) {
			send_signal = false;
			dev_dbg(opu_device,
					"Event found: id %X type %d",
					ev->event_id, ev->type);
			set_event(ev);
			if (ev->type == OPU_EVENT_TYPE_MEMORY && ev_data)
				ev->memory_exception_data = *ev_data;
		}

	if (type == OPU_EVENT_TYPE_MEMORY) {
		dev_warn(opu_device,
			"Sending SIGSEGV to process %d (pasid 0x%x)",
				p->lead_thread->pid, p->pasid);
		send_sig(SIGSEGV, p->lead_thread, 0);
	}

	/* Send SIGTERM no event of type "type" has been found*/
	if (send_signal) {
		if (send_sigterm) {
			dev_warn(opu_device,
				"Sending SIGTERM to process %d (pasid 0x%x)",
					p->lead_thread->pid, p->pasid);
			send_sig(SIGTERM, p->lead_thread, 0);
		} else {
			dev_err(opu_device,
				"Process %d (pasid 0x%x) got unhandled exception",
				p->lead_thread->pid, p->pasid);
		}
	}
}

#ifdef OPU_SUPPORT_IOMMU_V2
void opu_signal_iommu_event(struct opu_dev *dev, u32 pasid,
		unsigned long address, bool is_write_requested,
		bool is_execute_requested)
{
	struct opu_hsa_memory_exception_data memory_exception_data;
	struct vm_area_struct *vma;

	/*
	 * Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	struct opu_process *p = opu_lookup_process_by_pasid(pasid);
	struct mm_struct *mm;

	if (!p)
		return; /* Presumably process exited. */

	/* Take a safe reference to the mm_struct, which may otherwise
	 * disappear even while the opu_process is still referenced.
	 */
	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		opu_unref_process(p);
		return; /* Process is exiting */
	}

	memset(&memory_exception_data, 0, sizeof(memory_exception_data));

	mmap_read_lock(mm);
	vma = find_vma(mm, address);

	memory_exception_data.gpu_id = dev->id;
	memory_exception_data.va = address;
	/* Set failure reason */
	memory_exception_data.failure.NotPresent = 1;
	memory_exception_data.failure.NoExecute = 0;
	memory_exception_data.failure.ReadOnly = 0;
	if (vma && address >= vma->vm_start) {
		memory_exception_data.failure.NotPresent = 0;

		if (is_write_requested && !(vma->vm_flags & VM_WRITE))
			memory_exception_data.failure.ReadOnly = 1;
		else
			memory_exception_data.failure.ReadOnly = 0;

		if (is_execute_requested && !(vma->vm_flags & VM_EXEC))
			memory_exception_data.failure.NoExecute = 1;
		else
			memory_exception_data.failure.NoExecute = 0;
	}

	mmap_read_unlock(mm);
	mmput(mm);

	pr_debug("notpresent %d, noexecute %d, readonly %d\n",
			memory_exception_data.failure.NotPresent,
			memory_exception_data.failure.NoExecute,
			memory_exception_data.failure.ReadOnly);

	/* Workaround on Raven to not kill the process when memory is freed
	 * before IOMMU is able to finish processing all the excessive PPRs
	 */
	if (dev->device_info->asic_family != CHIP_RAVEN &&
	    dev->device_info->asic_family != CHIP_RENOIR) {
		mutex_lock(&p->event_mutex);

		/* Lookup events by type and signal them */
		lookup_events_by_type_and_signal(p, OPU_EVENT_TYPE_MEMORY,
				&memory_exception_data);

		mutex_unlock(&p->event_mutex);
	}

	opu_unref_process(p);
}
#endif /* OPU_SUPPORT_IOMMU_V2 */

void opu_signal_hw_exception_event(u32 pasid)
{
	/*
	 * Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	struct opu_process *p = opu_lookup_process_by_pasid(pasid);

	if (!p)
		return; /* Presumably process exited. */

	mutex_lock(&p->event_mutex);

	/* Lookup events by type and signal them */
	lookup_events_by_type_and_signal(p, OPU_EVENT_TYPE_HW_EXCEPTION, NULL);

	mutex_unlock(&p->event_mutex);
	opu_unref_process(p);
}

void opu_signal_vm_fault_event(struct opu_dev *dev, u32 pasid,
				struct opu_vm_fault_info *info)
{
	struct opu_event *ev;
	uint32_t id;
	struct opu_process *p = opu_lookup_process_by_pasid(pasid);
	struct opu_hsa_memory_exception_data memory_exception_data;

	if (!p)
		return; /* Presumably process exited. */
	memset(&memory_exception_data, 0, sizeof(memory_exception_data));
	memory_exception_data.gpu_id = dev->id;
	memory_exception_data.failure.imprecise = true;
	/* Set failure reason */
	if (info) {
		memory_exception_data.va = (info->page_addr) << PAGE_SHIFT;
		memory_exception_data.failure.NotPresent =
			info->prot_valid ? 1 : 0;
		memory_exception_data.failure.NoExecute =
			info->prot_exec ? 1 : 0;
		memory_exception_data.failure.ReadOnly =
			info->prot_write ? 1 : 0;
		memory_exception_data.failure.imprecise = 0;
	}
	mutex_lock(&p->event_mutex);

	id = OPU_FIRST_NONSIGNAL_EVENT_ID;
	idr_for_each_entry_continue(&p->event_idr, ev, id)
		if (ev->type == OPU_EVENT_TYPE_MEMORY) {
			ev->memory_exception_data = memory_exception_data;
			set_event(ev);
		}

	mutex_unlock(&p->event_mutex);
	opu_unref_process(p);
}

void opu_signal_reset_event(struct opu_dev *dev)
{
	struct opu_hsa_hw_exception_data hw_exception_data;
	struct opu_hsa_memory_exception_data memory_exception_data;
	struct opu_process *p;
	struct opu_event *ev;
	unsigned int temp;
	uint32_t id, idx;
	int reset_cause = atomic_read(&dev->sram_ecc_flag) ?
			OPU_HW_EXCEPTION_ECC :
			OPU_HW_EXCEPTION_GPU_HANG;

	/* Whole gpu reset caused by GPU hang and memory is lost */
	memset(&hw_exception_data, 0, sizeof(hw_exception_data));
	hw_exception_data.gpu_id = dev->id;
	hw_exception_data.memory_lost = 1;
	hw_exception_data.reset_cause = reset_cause;

	memset(&memory_exception_data, 0, sizeof(memory_exception_data));
	memory_exception_data.ErrorType = OPU_MEM_ERR_SRAM_ECC;
	memory_exception_data.gpu_id = dev->id;
	memory_exception_data.failure.imprecise = true;

	idx = srcu_read_lock(&opu_processes_srcu);
	hash_for_each_rcu(opu_processes_table, temp, p, opu_processes) {
		mutex_lock(&p->event_mutex);
		id = OPU_FIRST_NONSIGNAL_EVENT_ID;
		idr_for_each_entry_continue(&p->event_idr, ev, id) {
			if (ev->type == OPU_EVENT_TYPE_HW_EXCEPTION) {
				ev->hw_exception_data = hw_exception_data;
				set_event(ev);
			}
			if (ev->type == OPU_EVENT_TYPE_MEMORY &&
			    reset_cause == OPU_HW_EXCEPTION_ECC) {
				ev->memory_exception_data = memory_exception_data;
				set_event(ev);
			}
		}
		mutex_unlock(&p->event_mutex);
	}
	srcu_read_unlock(&opu_processes_srcu, idx);
}

void opu_signal_poison_consumed_event(struct opu_dev *dev, u32 pasid)
{
	struct opu_process *p = opu_lookup_process_by_pasid(pasid);
	struct opu_hsa_memory_exception_data memory_exception_data;
	struct opu_hsa_hw_exception_data hw_exception_data;
	struct opu_event *ev;
	uint32_t id = OPU_FIRST_NONSIGNAL_EVENT_ID;

	if (!p)
		return; /* Presumably process exited. */

	memset(&hw_exception_data, 0, sizeof(hw_exception_data));
	hw_exception_data.gpu_id = dev->id;
	hw_exception_data.memory_lost = 1;
	hw_exception_data.reset_cause = OPU_HW_EXCEPTION_ECC;

	memset(&memory_exception_data, 0, sizeof(memory_exception_data));
	memory_exception_data.ErrorType = OPU_MEM_ERR_POISON_CONSUMED;
	memory_exception_data.gpu_id = dev->id;
	memory_exception_data.failure.imprecise = true;

	mutex_lock(&p->event_mutex);
	idr_for_each_entry_continue(&p->event_idr, ev, id) {
		if (ev->type == OPU_EVENT_TYPE_HW_EXCEPTION) {
			ev->hw_exception_data = hw_exception_data;
			set_event(ev);
		}

		if (ev->type == OPU_EVENT_TYPE_MEMORY) {
			ev->memory_exception_data = memory_exception_data;
			set_event(ev);
		}
	}
	mutex_unlock(&p->event_mutex);

	/* user application will handle SIGBUS signal */
	send_sig(SIGBUS, p->lead_thread, 0);

	opu_unref_process(p);
}
