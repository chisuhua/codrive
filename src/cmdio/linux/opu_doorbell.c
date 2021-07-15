#include "opu_priv.h"
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/idr.h>

/*
 * This extension supports a kernel level doorbells management for the
 * kernel queues using the first doorbell page reserved for the kernel.
 */

/*
 * Each device exposes a doorbell aperture, a PCI MMIO aperture that
 * receives 32-bit writes that are passed to queues as wptr values.
 * The doorbells are intended to be written by applications as part
 * of queueing work on user-mode queues.
 * We assign doorbells to applications in PAGE_SIZE-sized and aligned chunks.
 * We map the doorbell address space into user-mode when a process creates
 * its first queue on each device.
 * Although the mapping is done by OPU, it is equivalent to an mmap of
 * the /dev/opu with the particular device encoded in the mmap offset.
 * There will be other uses for mmap of /dev/opu, so only a range of
 * offsets (OPU_MMAP_DOORBELL_START-END) is used for doorbells.
 */

/* # of doorbell bytes allocated for each process. */
size_t opu_doorbell_process_slice(struct opu_dev *opu)
{
	return roundup(opu->device_info->doorbell_size *
			OPU_MAX_NUM_OF_QUEUES_PER_PROCESS,
			PAGE_SIZE);
}

/* Doorbell calculations for device init. */
int opu_doorbell_init(struct opu_dev *opu)
{
	size_t doorbell_start_offset;
	size_t doorbell_aperture_size;
	size_t doorbell_process_limit;

	/*
	 * We start with calculations in bytes because the input data might
	 * only be byte-aligned.
	 * Only after we have done the rounding can we assume any alignment.
	 */

	doorbell_start_offset =
			roundup(opu->shared_resources.doorbell_start_offset,
					opu_doorbell_process_slice(opu));

	doorbell_aperture_size =
			rounddown(opu->shared_resources.doorbell_aperture_size,
					opu_doorbell_process_slice(opu));

	if (doorbell_aperture_size > doorbell_start_offset)
		doorbell_process_limit =
			(doorbell_aperture_size - doorbell_start_offset) /
						opu_doorbell_process_slice(opu);
	else
		return -ENOSPC;

	if (!opu->max_doorbell_slices ||
	    doorbell_process_limit < opu->max_doorbell_slices)
		opu->max_doorbell_slices = doorbell_process_limit;

	opu->doorbell_base = opu->shared_resources.doorbell_physical_address +
				doorbell_start_offset;

	opu->doorbell_base_dw_offset = doorbell_start_offset / sizeof(u32);

	opu->doorbell_kernel_ptr = ioremap(opu->doorbell_base,
					   opu_doorbell_process_slice(opu));

	if (!opu->doorbell_kernel_ptr)
		return -ENOMEM;

	pr_debug("Doorbell initialization:\n");
	pr_debug("doorbell base           == 0x%08lX\n",
			(uintptr_t)opu->doorbell_base);

	pr_debug("doorbell_base_dw_offset      == 0x%08lX\n",
			opu->doorbell_base_dw_offset);

	pr_debug("doorbell_process_limit  == 0x%08lX\n",
			doorbell_process_limit);

	pr_debug("doorbell_kernel_offset  == 0x%08lX\n",
			(uintptr_t)opu->doorbell_base);

	pr_debug("doorbell aperture size  == 0x%08lX\n",
			opu->shared_resources.doorbell_aperture_size);

	pr_debug("doorbell kernel address == %p\n", opu->doorbell_kernel_ptr);

	return 0;
}

void opu_doorbell_fini(struct opu_dev *opu)
{
	if (opu->doorbell_kernel_ptr)
		iounmap(opu->doorbell_kernel_ptr);
}

int opu_doorbell_mmap(struct opu_dev *dev, struct opu_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;
	struct opu_process_device *pdd;

	/*
	 * For simplicitly we only allow mapping of the entire doorbell
	 * allocation of a single device & process.
	 */
	if (vma->vm_end - vma->vm_start != opu_doorbell_process_slice(dev))
		return -EINVAL;

	pdd = opu_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;

	/* Calculate physical address of doorbell */
	address = opu_get_process_doorbells(pdd);
	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("Mapping doorbell page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 (unsigned long long) vma->vm_start, address, vma->vm_flags,
		 opu_doorbell_process_slice(dev));


	return io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				opu_doorbell_process_slice(dev),
				vma->vm_page_prot);
}


/* get kernel iomem pointer for a doorbell */
void __iomem *opu_get_kernel_doorbell(struct opu_dev *opu,
					unsigned int *doorbell_off)
{
	u32 inx;

	mutex_lock(&opu->doorbell_mutex);
	inx = find_first_zero_bit(opu->doorbell_available_index,
					OPU_MAX_NUM_OF_QUEUES_PER_PROCESS);

	__set_bit(inx, opu->doorbell_available_index);
	mutex_unlock(&opu->doorbell_mutex);

	if (inx >= OPU_MAX_NUM_OF_QUEUES_PER_PROCESS)
		return NULL;

	inx *= opu->device_info->doorbell_size / sizeof(u32);

	/*
	 * Calculating the kernel doorbell offset using the first
	 * doorbell page.
	 */
	*doorbell_off = opu->doorbell_base_dw_offset + inx;

	pr_debug("Get kernel queue doorbell\n"
			"     doorbell offset   == 0x%08X\n"
			"     doorbell index    == 0x%x\n",
		*doorbell_off, inx);

	return opu->doorbell_kernel_ptr + inx;
}

void opu_release_kernel_doorbell(struct opu_dev *opu, u32 __iomem *db_addr)
{
	unsigned int inx;

	inx = (unsigned int)(db_addr - opu->doorbell_kernel_ptr)
		* sizeof(u32) / opu->device_info->doorbell_size;

	mutex_lock(&opu->doorbell_mutex);
	__clear_bit(inx, opu->doorbell_available_index);
	mutex_unlock(&opu->doorbell_mutex);
}

void write_kernel_doorbell(void __iomem *db, u32 value)
{
	if (db) {
		writel(value, db);
		pr_debug("Writing %d to doorbell address %p\n", value, db);
	}
}

void write_kernel_doorbell64(void __iomem *db, u64 value)
{
	if (db) {
		WARN(((unsigned long)db & 7) != 0,
		     "Unaligned 64-bit doorbell");
		writeq(value, (u64 __iomem *)db);
		pr_debug("writing %llu to doorbell address %p\n", value, db);
	}
}

unsigned int opu_get_doorbell_dw_offset_in_bar(struct opu_dev *opu,
					struct opu_process_device *pdd,
					unsigned int doorbell_id)
{
	/*
	 * doorbell_base_dw_offset accounts for doorbells taken by KGD.
	 * index * opu_doorbell_process_slice/sizeof(u32) adjusts to
	 * the process's doorbells. The offset returned is in dword
	 * units regardless of the ASIC-dependent doorbell size.
	 */
	return opu->doorbell_base_dw_offset +
		pdd->doorbell_index
		* opu_doorbell_process_slice(opu) / sizeof(u32) +
		doorbell_id * opu->device_info->doorbell_size / sizeof(u32);
}

uint64_t opu_get_number_elems(struct opu_dev *opu)
{
	uint64_t num_of_elems = (opu->shared_resources.doorbell_aperture_size -
				opu->shared_resources.doorbell_start_offset) /
					opu_doorbell_process_slice(opu) + 1;

	return num_of_elems;

}

phys_addr_t opu_get_process_doorbells(struct opu_process_device *pdd)
{
	return pdd->dev->doorbell_base +
		pdd->doorbell_index * opu_doorbell_process_slice(pdd->dev);
}

int opu_alloc_process_doorbells(struct opu_dev *opu, unsigned int *doorbell_index)
{
	int r = ida_simple_get(&opu->doorbell_ida, 1, opu->max_doorbell_slices,
				GFP_KERNEL);
	if (r > 0)
		*doorbell_index = r;

	return r;
}

void opu_free_process_doorbells(struct opu_dev *opu, unsigned int doorbell_index)
{
	if (doorbell_index)
		ida_simple_remove(&opu->doorbell_ida, doorbell_index);
}
