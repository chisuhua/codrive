
/*
 * OPU Interrupts.
 *
 * AMD GPUs deliver interrupts by pushing an interrupt description onto the
 * interrupt ring and then sending an interrupt. KGD receives the interrupt
 * in ISR and sends us a pointer to each new entry on the interrupt ring.
 *
 * We generally can't process interrupt-signaled events from ISR, so we call
 * out to each interrupt client module (currently only the scheduler) to ask if
 * each interrupt is interesting. If they return true, then it requires further
 * processing so we copy it to an internal interrupt ring and call each
 * interrupt client again from a work-queue.
 *
 * There's no acknowledgment for the interrupts we use. The hardware simply
 * queues a new interrupt each time without waiting.
 *
 * The fixed-size internal queue means that it's possible for us to lose
 * interrupts because we have no back-pressure to the hardware.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include "opu_priv.h"

#define OPU_IH_NUM_ENTRIES 8192

static void interrupt_wq(struct work_struct *);

int opu_interrupt_init(struct opu_dev *opu)
{
	int r;

	r = kfifo_alloc(&opu->ih_fifo,
		OPU_IH_NUM_ENTRIES * opu->device_info->ih_ring_entry_size,
		GFP_KERNEL);
	if (r) {
		dev_err(opu_chardev(), "Failed to allocate IH fifo\n");
		return r;
	}

	opu->ih_wq = alloc_workqueue("OPU IH", WQ_HIGHPRI, 1);
	if (unlikely(!opu->ih_wq)) {
		kfifo_free(&opu->ih_fifo);
		dev_err(opu_chardev(), "Failed to allocate OPU IH workqueue\n");
		return -ENOMEM;
	}
	spin_lock_init(&opu->interrupt_lock);

	INIT_WORK(&opu->interrupt_work, interrupt_wq);

	opu->interrupts_active = true;

	/*
	 * After this function returns, the interrupt will be enabled. This
	 * barrier ensures that the interrupt running on a different processor
	 * sees all the above writes.
	 */
	smp_wmb();

	return 0;
}

void opu_interrupt_exit(struct opu_dev *opu)
{
	/*
	 * Stop the interrupt handler from writing to the ring and scheduling
	 * workqueue items. The spinlock ensures that any interrupt running
	 * after we have unlocked sees interrupts_active = false.
	 */
	unsigned long flags;

	spin_lock_irqsave(&opu->interrupt_lock, flags);
	opu->interrupts_active = false;
	spin_unlock_irqrestore(&opu->interrupt_lock, flags);

	/*
	 * flush_work ensures that there are no outstanding
	 * work-queue items that will access interrupt_ring. New work items
	 * can't be created because we stopped interrupt handling above.
	 */
	flush_workqueue(opu->ih_wq);

	kfifo_free(&opu->ih_fifo);
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
bool enqueue_ih_ring_entry(struct opu_dev *opu,	const void *ih_ring_entry)
{
	int count;

	count = kfifo_in(&opu->ih_fifo, ih_ring_entry,
				opu->device_info->ih_ring_entry_size);
	if (count != opu->device_info->ih_ring_entry_size) {
		dev_err_ratelimited(opu_chardev(),
			"Interrupt ring overflow, dropping interrupt %d\n",
			count);
		return false;
	}

	return true;
}

/*
 * Assumption: single reader/writer. This function is not re-entrant
 */
static bool dequeue_ih_ring_entry(struct opu_dev *opu, void *ih_ring_entry)
{
	int count;

	count = kfifo_out(&opu->ih_fifo, ih_ring_entry,
				opu->device_info->ih_ring_entry_size);

	WARN_ON(count && count != opu->device_info->ih_ring_entry_size);

	return count == opu->device_info->ih_ring_entry_size;
}

static void interrupt_wq(struct work_struct *work)
{
	struct opu_dev *dev = container_of(work, struct opu_dev,
						interrupt_work);
	uint32_t ih_ring_entry[OPU_MAX_RING_ENTRY_SIZE];

	if (dev->device_info->ih_ring_entry_size > sizeof(ih_ring_entry)) {
		dev_err_once(opu_chardev(), "Ring entry too small\n");
		return;
	}

	while (dequeue_ih_ring_entry(dev, ih_ring_entry))
		dev->device_info->event_interrupt_class->interrupt_wq(dev,
								ih_ring_entry);
}

bool interrupt_is_wanted(struct opu_dev *dev,
			const uint32_t *ih_ring_entry,
			uint32_t *patched_ihre, bool *flag)
{
	/* integer and bitwise OR so there is no boolean short-circuiting */
	unsigned int wanted = 0;

	wanted |= dev->device_info->event_interrupt_class->interrupt_isr(dev,
					 ih_ring_entry, patched_ihre, flag);

	return wanted != 0;
}
