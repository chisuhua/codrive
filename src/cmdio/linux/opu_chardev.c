#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/dma-buf.h>
#include <asm/processor.h>
#include <opu_ioctl.h>
#include "opu_priv.h"
#include "opu_device_queue_manager.h"
#include "opu_dbgmgr.h"
#include "opu_svm.h"
#include "opu.h"
#include "opu_smi_events.h"
static long opu_ioctl(struct file *, unsigned int, unsigned long);
static int opu_open(struct inode *, struct file *);
static int opu_release(struct inode *, struct file *);
static int opu_mmap(struct file *, struct vm_area_struct *);

static const char opu_dev_name[] = "opu";

static const struct file_operations opu_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = opu_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = opu_open,
	.release = opu_release,
	.mmap = opu_mmap,
};

static int opu_char_dev_major = -1;
static struct class *opu_class;
struct device *opu_device;

static char *opu_devnode(struct device *dev, umode_t *mode)
{
    if (!mode)
        return NULL;
    *mode = 0666;
    return NULL;
}

int opu_chardev_init(void)
{
	int err = 0;

	opu_char_dev_major = register_chrdev(0, opu_dev_name, &opu_fops);
	err = opu_char_dev_major;
	if (err < 0)
		goto err_register_chrdev;

	opu_class = class_create(THIS_MODULE, opu_dev_name);
	err = PTR_ERR(opu_class);
	if (IS_ERR(opu_class))
		goto err_class_create;

    opu_class->devnode = opu_devnode;
	opu_device = device_create(opu_class, NULL,
					MKDEV(opu_char_dev_major, 0),
					NULL, opu_dev_name);
	err = PTR_ERR(opu_device);
	if (IS_ERR(opu_device))
		goto err_device_create;

    // opu_cgroup_init();
    // opu_ipc_init();

	return 0;

err_device_create:
	class_destroy(opu_class);
err_class_create:
	unregister_chrdev(opu_char_dev_major, opu_dev_name);
err_register_chrdev:
	return err;
}

void opu_chardev_exit(void)
{
	device_destroy(opu_class, MKDEV(opu_char_dev_major, 0));
	class_destroy(opu_class);
	unregister_chrdev(opu_char_dev_major, opu_dev_name);
	opu_device = NULL;
}

struct device *opu_chardev(void)
{
	return opu_device;
}


static int opu_open(struct inode *inode, struct file *filep)
{
	struct opu_process *process;
	bool is_32bit_user_mode;

	if (iminor(inode) != 0)
		return -ENODEV;

	is_32bit_user_mode = in_compat_syscall();

	if (is_32bit_user_mode) {
		dev_warn(opu_device,
			"Process %d (32-bit) failed to open /dev/opu\n"
			"32-bit processes are not supported by opu\n",
			current->pid);
		return -EPERM;
	}

	process = opu_create_process(filep);
	if (IS_ERR(process))
		return PTR_ERR(process);

	if (opu_is_locked()) {
		dev_dbg(opu_device, "opu is locked!\n"
				"process %d unreferenced", process->pasid);
		opu_unref_process(process);
		return -EAGAIN;
	}

	/* filep now owns the reference returned by opu_create_process */
	filep->private_data = process;
    // opu_unref_process(proces);

	dev_dbg(opu_device, "process %d opened, compat mode (32 bit) - %d\n",
		process->pasid, process->is_32bit_user_mode);

	return 0;
}

static int opu_release(struct inode *inode, struct file *filep)
{
	struct opu_process *process = filep->private_data;

	if (process)
		opu_unref_process(process);

	return 0;
}

static int opu_ioctl_get_version(struct file *filep, struct opu_process *p,
					void *data)
{
	struct opu_ioctl_get_version_args *args = data;

	args->major_version = opu_IOCTL_MAJOR_VERSION;
	args->minor_version = opu_IOCTL_MINOR_VERSION;

	return 0;
}

static int set_queue_properties_from_user(struct queue_properties *q_properties,
				struct opu_ioctl_create_queue_args *args)
{
	if (args->queue_percentage > opu_MAX_QUEUE_PERCENTAGE) {
		pr_err("Queue percentage must be between 0 to opu_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > opu_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to opu_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	if (!access_ok((const void __user *) args->read_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access read pointer\n");
		return -EFAULT;
	}

	if (!access_ok((const void __user *) args->write_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access write pointer\n");
		return -EFAULT;
	}

	if (args->eop_buffer_address &&
		!access_ok((const void __user *) args->eop_buffer_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access eop buffer");
		return -EFAULT;
	}

	if (args->ctx_save_restore_address &&
		!access_ok((const void __user *) args->ctx_save_restore_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access ctx save restore buffer");
		return -EFAULT;
	}

	q_properties->is_interop = false;
	q_properties->is_gws = false;
	q_properties->queue_percent = args->queue_percentage;
	q_properties->priority = args->queue_priority;
	q_properties->queue_address = args->ring_base_address;
	q_properties->queue_size = args->ring_size;
	q_properties->read_ptr = (uint32_t *) args->read_pointer_address;
	q_properties->write_ptr = (uint32_t *) args->write_pointer_address;
	q_properties->eop_ring_buffer_address = args->eop_buffer_address;
	q_properties->eop_ring_buffer_size = args->eop_buffer_size;
	q_properties->ctx_save_restore_area_address =
			args->ctx_save_restore_address;
	q_properties->ctx_save_restore_area_size = args->ctx_save_restore_size;
	q_properties->ctl_stack_size = args->ctl_stack_size;
	if (args->queue_type == opu_IOC_QUEUE_TYPE_COMPUTE ||
		args->queue_type == opu_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->type = opu_QUEUE_TYPE_COMPUTE;
	else if (args->queue_type == opu_IOC_QUEUE_TYPE_SDMA)
		q_properties->type = opu_QUEUE_TYPE_SDMA;
	else if (args->queue_type == opu_IOC_QUEUE_TYPE_SDMA_XGMI)
		q_properties->type = opu_QUEUE_TYPE_SDMA_XGMI;
	else
		return -ENOTSUPP;

	if (args->queue_type == opu_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->format = opu_QUEUE_FORMAT_AQL;
	else
		q_properties->format = opu_QUEUE_FORMAT_PM4;

	pr_debug("Queue Percentage: %d, %d\n",
			q_properties->queue_percent, args->queue_percentage);

	pr_debug("Queue Priority: %d, %d\n",
			q_properties->priority, args->queue_priority);

	pr_debug("Queue Address: 0x%llX, 0x%llX\n",
			q_properties->queue_address, args->ring_base_address);

	pr_debug("Queue Size: 0x%llX, %u\n",
			q_properties->queue_size, args->ring_size);

	pr_debug("Queue r/w Pointers: %px, %px\n",
			q_properties->read_ptr,
			q_properties->write_ptr);

	pr_debug("Queue Format: %d\n", q_properties->format);

	pr_debug("Queue EOP: 0x%llX\n", q_properties->eop_ring_buffer_address);

	pr_debug("Queue CTX save area: 0x%llX\n",
			q_properties->ctx_save_restore_area_address);

	return 0;
}

static int opu_ioctl_create_queue(struct file *filep, struct opu_process *p,
					void *data)
{
	struct opu_ioctl_create_queue_args *args = data;
	struct opu_dev *dev;
	int err = 0;
	unsigned int queue_id;
	struct opu_process_device *pdd;
	struct queue_properties q_properties;
	uint32_t doorbell_offset_in_process = 0;

	memset(&q_properties, 0, sizeof(struct queue_properties));

	pr_debug("Creating queue ioctl\n");

	err = set_queue_properties_from_user(&q_properties, args);
	if (err)
		return err;

	pr_debug("Looking for gpu id 0x%x\n", args->gpu_id);
	dev = opu_device_by_id(args->gpu_id);
	if (!dev) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		return -EINVAL;
	}

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto err_bind_process;
	}

	pr_debug("Creating queue for PASID 0x%x on gpu 0x%x\n",
			p->pasid,
			dev->id);

	err = pqm_create_queue(&p->pqm, dev, filep, &q_properties, &queue_id,
			&doorbell_offset_in_process);
	if (err != 0)
		goto err_create_queue;

	args->queue_id = queue_id;


	/* Return gpu_id as doorbell offset for mmap usage */
	args->doorbell_offset = opu_MMAP_TYPE_DOORBELL;
	args->doorbell_offset |= opu_MMAP_GPU_ID(args->gpu_id);
	if (opu_IS_SOC15(dev->device_info->asic_family))
		/* On SOC15 ASICs, include the doorbell offset within the
		 * process doorbell frame, which is 2 pages.
		 */
		args->doorbell_offset |= doorbell_offset_in_process;

	mutex_unlock(&p->mutex);

	pr_debug("Queue id %d was created successfully\n", args->queue_id);

	pr_debug("Ring buffer address == 0x%016llX\n",
			args->ring_base_address);

	pr_debug("Read ptr address    == 0x%016llX\n",
			args->read_pointer_address);

	pr_debug("Write ptr address   == 0x%016llX\n",
			args->write_pointer_address);

	return 0;

err_create_queue:
err_bind_process:
	mutex_unlock(&p->mutex);
	return err;
}

static int opu_ioctl_destroy_queue(struct file *filp, struct opu_process *p,
					void *data)
{
	int retval;
	struct opu_ioctl_destroy_queue_args *args = data;

	pr_debug("Destroying queue id %d for pasid 0x%x\n",
				args->queue_id,
				p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_destroy_queue(&p->pqm, args->queue_id);

	mutex_unlock(&p->mutex);
	return retval;
}

static int opu_ioctl_update_queue(struct file *filp, struct opu_process *p,
					void *data)
{
	int retval;
	struct opu_ioctl_update_queue_args *args = data;
	struct queue_properties properties;

	if (args->queue_percentage > opu_MAX_QUEUE_PERCENTAGE) {
		pr_err("Queue percentage must be between 0 to opu_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > opu_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to opu_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	properties.queue_address = args->ring_base_address;
	properties.queue_size = args->ring_size;
	properties.queue_percent = args->queue_percentage;
	properties.priority = args->queue_priority;

	pr_debug("Updating queue id %d for pasid 0x%x\n",
			args->queue_id, p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_update_queue(&p->pqm, args->queue_id, &properties);

	mutex_unlock(&p->mutex);

	return retval;
}

static int opu_ioctl_set_cu_mask(struct file *filp, struct opu_process *p,
					void *data)
{
	int retval;
	const int max_num_cus = 1024;
	struct opu_ioctl_set_cu_mask_args *args = data;
	struct queue_properties properties;
	uint32_t __user *cu_mask_ptr = (uint32_t __user *)args->cu_mask_ptr;
	size_t cu_mask_size = sizeof(uint32_t) * (args->num_cu_mask / 32);

	if ((args->num_cu_mask % 32) != 0) {
		pr_debug("num_cu_mask 0x%x must be a multiple of 32",
				args->num_cu_mask);
		return -EINVAL;
	}

	properties.cu_mask_count = args->num_cu_mask;
	if (properties.cu_mask_count == 0) {
		pr_debug("CU mask cannot be 0");
		return -EINVAL;
	}

	/* To prevent an unreasonably large CU mask size, set an arbitrary
	 * limit of max_num_cus bits.  We can then just drop any CU mask bits
	 * past max_num_cus bits and just use the first max_num_cus bits.
	 */
	if (properties.cu_mask_count > max_num_cus) {
		pr_debug("CU mask cannot be greater than 1024 bits");
		properties.cu_mask_count = max_num_cus;
		cu_mask_size = sizeof(uint32_t) * (max_num_cus/32);
	}

	properties.cu_mask = kzalloc(cu_mask_size, GFP_KERNEL);
	if (!properties.cu_mask)
		return -ENOMEM;

	retval = copy_from_user(properties.cu_mask, cu_mask_ptr, cu_mask_size);
	if (retval) {
		pr_debug("Could not copy CU mask from userspace");
		kfree(properties.cu_mask);
		return -EFAULT;
	}

	mutex_lock(&p->mutex);

	retval = pqm_set_cu_mask(&p->pqm, args->queue_id, &properties);

	mutex_unlock(&p->mutex);

	if (retval)
		kfree(properties.cu_mask);

	return retval;
}

static int opu_ioctl_get_queue_wave_state(struct file *filep,
					  struct opu_process *p, void *data)
{
	struct opu_ioctl_get_queue_wave_state_args *args = data;
	int r;

	mutex_lock(&p->mutex);

	r = pqm_get_wave_state(&p->pqm, args->queue_id,
			       (void __user *)args->ctl_stack_address,
			       &args->ctl_stack_used_size,
			       &args->save_area_used_size);

	mutex_unlock(&p->mutex);

	return r;
}

static int opu_ioctl_set_memory_policy(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_set_memory_policy_args *args = data;
	struct opu_dev *dev;
	int err = 0;
	struct opu_process_device *pdd;
	enum cache_policy default_policy, alternate_policy;

	if (args->default_policy != opu_IOC_CACHE_POLICY_COHERENT
	    && args->default_policy != opu_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	if (args->alternate_policy != opu_IOC_CACHE_POLICY_COHERENT
	    && args->alternate_policy != opu_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	default_policy = (args->default_policy == opu_IOC_CACHE_POLICY_COHERENT)
			 ? cache_policy_coherent : cache_policy_noncoherent;

	alternate_policy =
		(args->alternate_policy == opu_IOC_CACHE_POLICY_COHERENT)
		   ? cache_policy_coherent : cache_policy_noncoherent;

	if (!dev->dqm->ops.set_cache_memory_policy(dev->dqm,
				&pdd->qpd,
				default_policy,
				alternate_policy,
				(void __user *)args->alternate_aperture_base,
				args->alternate_aperture_size))
		err = -EINVAL;

out:
	mutex_unlock(&p->mutex);

	return err;
}

static int opu_ioctl_set_trap_handler(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_set_trap_handler_args *args = data;
	struct opu_dev *dev;
	int err = 0;
	struct opu_process_device *pdd;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	opu_process_set_trap_handler(&pdd->qpd, args->tba_addr, args->tma_addr);

out:
	mutex_unlock(&p->mutex);

	return err;
}

static int opu_ioctl_dbg_register(struct file *filep,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_dbg_register_args *args = data;
	struct opu_dev *dev;
	struct opu_dbgmgr *dbgmgr_ptr;
	struct opu_process_device *pdd;
	bool create_ok;
	long status = 0;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	mutex_lock(&p->mutex);
	mutex_lock(opu_get_dbgmgr_mutex());

	/*
	 * make sure that we have pdd, if this the first queue created for
	 * this process
	 */
	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		status = PTR_ERR(pdd);
		goto out;
	}

	if (!dev->dbgmgr) {
		/* In case of a legal call, we have no dbgmgr yet */
		create_ok = opu_dbgmgr_create(&dbgmgr_ptr, dev);
		if (create_ok) {
			status = opu_dbgmgr_register(dbgmgr_ptr, p);
			if (status != 0)
				opu_dbgmgr_destroy(dbgmgr_ptr);
			else
				dev->dbgmgr = dbgmgr_ptr;
		}
	} else {
		pr_debug("debugger already registered\n");
		status = -EINVAL;
	}

out:
	mutex_unlock(opu_get_dbgmgr_mutex());
	mutex_unlock(&p->mutex);

	return status;
}

static int opu_ioctl_dbg_unregister(struct file *filep,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_dbg_unregister_args *args = data;
	struct opu_dev *dev;
	long status;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev || !dev->dbgmgr)
		return -EINVAL;

	mutex_lock(opu_get_dbgmgr_mutex());

	status = opu_dbgmgr_unregister(dev->dbgmgr, p);
	if (!status) {
		opu_dbgmgr_destroy(dev->dbgmgr);
		dev->dbgmgr = NULL;
	}

	mutex_unlock(opu_get_dbgmgr_mutex());

	return status;
}

/*
 * Parse and generate variable size data structure for address watch.
 * Total size of the buffer and # watch points is limited in order
 * to prevent kernel abuse. (no bearing to the much smaller HW limitation
 * which is enforced by dbgdev module)
 * please also note that the watch address itself are not "copied from user",
 * since it be set into the HW in user mode values.
 *
 */
static int opu_ioctl_dbg_address_watch(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_dbg_address_watch_args *args = data;
	struct opu_dev *dev;
	struct dbg_address_watch_info aw_info;
	unsigned char *args_buff;
	long status;
	void __user *cmd_from_user;
	uint64_t watch_mask_value = 0;
	unsigned int args_idx = 0;

	memset((void *) &aw_info, 0, sizeof(struct dbg_address_watch_info));

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("opu_ioctl_dbg_wave_control not supported on CZ\n");
		return -EINVAL;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	/* Validate arguments */

	if ((args->buf_size_in_bytes > MAX_ALLOWED_AW_BUFF_SIZE) ||
		(args->buf_size_in_bytes <= sizeof(*args) + sizeof(int) * 2) ||
		(cmd_from_user == NULL))
		return -EINVAL;

	/* this is the actual buffer to work with */
	args_buff = memdup_user(cmd_from_user,
				args->buf_size_in_bytes - sizeof(*args));
	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	aw_info.process = p;

	aw_info.num_watch_points = *((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(aw_info.num_watch_points);

	aw_info.watch_mode = (enum HSA_DBG_WATCH_MODE *) &args_buff[args_idx];
	args_idx += sizeof(enum HSA_DBG_WATCH_MODE) * aw_info.num_watch_points;

	/*
	 * set watch address base pointer to point on the array base
	 * within args_buff
	 */
	aw_info.watch_address = (uint64_t *) &args_buff[args_idx];

	/* skip over the addresses buffer */
	args_idx += sizeof(aw_info.watch_address) * aw_info.num_watch_points;

	if (args_idx >= args->buf_size_in_bytes - sizeof(*args)) {
		status = -EINVAL;
		goto out;
	}

	watch_mask_value = (uint64_t) args_buff[args_idx];

	if (watch_mask_value > 0) {
		/*
		 * There is an array of masks.
		 * set watch mask base pointer to point on the array base
		 * within args_buff
		 */
		aw_info.watch_mask = (uint64_t *) &args_buff[args_idx];

		/* skip over the masks buffer */
		args_idx += sizeof(aw_info.watch_mask) *
				aw_info.num_watch_points;
	} else {
		/* just the NULL mask, set to NULL and skip over it */
		aw_info.watch_mask = NULL;
		args_idx += sizeof(aw_info.watch_mask);
	}

	if (args_idx >= args->buf_size_in_bytes - sizeof(args)) {
		status = -EINVAL;
		goto out;
	}

	/* Currently HSA Event is not supported for DBG */
	aw_info.watch_event = NULL;

	mutex_lock(opu_get_dbgmgr_mutex());

	status = opu_dbgmgr_address_watch(dev->dbgmgr, &aw_info);

	mutex_unlock(opu_get_dbgmgr_mutex());

out:
	kfree(args_buff);

	return status;
}

/* Parse and generate fixed size data structure for wave control */
static int opu_ioctl_dbg_wave_control(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_dbg_wave_control_args *args = data;
	struct opu_dev *dev;
	struct dbg_wave_control_info wac_info;
	unsigned char *args_buff;
	uint32_t computed_buff_size;
	long status;
	void __user *cmd_from_user;
	unsigned int args_idx = 0;

	memset((void *) &wac_info, 0, sizeof(struct dbg_wave_control_info));

	/* we use compact form, independent of the packing attribute value */
	computed_buff_size = sizeof(*args) +
				sizeof(wac_info.mode) +
				sizeof(wac_info.operand) +
				sizeof(wac_info.dbgWave_msg.DbgWaveMsg) +
				sizeof(wac_info.dbgWave_msg.MemoryVA) +
				sizeof(wac_info.trapId);

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("opu_ioctl_dbg_wave_control not supported on CZ\n");
		return -EINVAL;
	}

	/* input size must match the computed "compact" size */
	if (args->buf_size_in_bytes != computed_buff_size) {
		pr_debug("size mismatch, computed : actual %u : %u\n",
				args->buf_size_in_bytes, computed_buff_size);
		return -EINVAL;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	if (cmd_from_user == NULL)
		return -EINVAL;

	/* copy the entire buffer from user */

	args_buff = memdup_user(cmd_from_user,
				args->buf_size_in_bytes - sizeof(*args));
	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	/* move ptr to the start of the "pay-load" area */
	wac_info.process = p;

	wac_info.operand = *((enum HSA_DBG_WAVEOP *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.operand);

	wac_info.mode = *((enum HSA_DBG_WAVEMODE *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.mode);

	wac_info.trapId = *((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.trapId);

	wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value =
					*((uint32_t *)(&args_buff[args_idx]));
	wac_info.dbgWave_msg.MemoryVA = NULL;

	mutex_lock(opu_get_dbgmgr_mutex());

	pr_debug("Calling dbg manager process %p, operand %u, mode %u, trapId %u, message %u\n",
			wac_info.process, wac_info.operand,
			wac_info.mode, wac_info.trapId,
			wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value);

	status = opu_dbgmgr_wave_control(dev->dbgmgr, &wac_info);

	pr_debug("Returned status of dbg manager is %ld\n", status);

	mutex_unlock(opu_get_dbgmgr_mutex());

	kfree(args_buff);

	return status;
}

static int opu_ioctl_get_clock_counters(struct file *filep,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_get_clock_counters_args *args = data;
	struct opu_dev *dev;

	dev = opu_device_by_id(args->gpu_id);
	if (dev)
		/* Reading GPU clock counter from KGD */
		args->gpu_clock_counter = amdgpu_opu_get_gpu_clock_counter(dev->kgd);
	else
		/* Node without GPU resource */
		args->gpu_clock_counter = 0;

	/* No access to rdtsc. Using raw monotonic time */
	args->cpu_clock_counter = ktime_get_raw_ns();
	args->system_clock_counter = ktime_get_boottime_ns();

	/* Since the counter is in nano-seconds we use 1GHz frequency */
	args->system_clock_freq = 1000000000;

	return 0;
}


static int opu_ioctl_get_process_apertures(struct file *filp,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_get_process_apertures_args *args = data;
	struct opu_process_device_apertures *pAperture;
	int i;

	dev_dbg(opu_device, "get apertures for PASID 0x%x", p->pasid);

	args->num_of_nodes = 0;

	mutex_lock(&p->mutex);
	/* Run over all pdd of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct opu_process_device *pdd = p->pdds[i];

		pAperture =
			&args->process_apertures[args->num_of_nodes];
		pAperture->gpu_id = pdd->dev->id;
		pAperture->lds_base = pdd->lds_base;
		pAperture->lds_limit = pdd->lds_limit;
		pAperture->gpuvm_base = pdd->gpuvm_base;
		pAperture->gpuvm_limit = pdd->gpuvm_limit;
		pAperture->scratch_base = pdd->scratch_base;
		pAperture->scratch_limit = pdd->scratch_limit;

		dev_dbg(opu_device,
			"node id %u\n", args->num_of_nodes);
		dev_dbg(opu_device,
			"gpu id %u\n", pdd->dev->id);
		dev_dbg(opu_device,
			"lds_base %llX\n", pdd->lds_base);
		dev_dbg(opu_device,
			"lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(opu_device,
			"gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(opu_device,
			"gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(opu_device,
			"scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(opu_device,
			"scratch_limit %llX\n", pdd->scratch_limit);

		if (++args->num_of_nodes >= NUM_OF_SUPPORTED_GPUS)
			break;
	}
	mutex_unlock(&p->mutex);

	return 0;
}

static int opu_ioctl_get_process_apertures_new(struct file *filp,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_get_process_apertures_new_args *args = data;
	struct opu_process_device_apertures *pa;
	int ret;
	int i;

	dev_dbg(opu_device, "get apertures for PASID 0x%x", p->pasid);

	if (args->num_of_nodes == 0) {
		/* Return number of nodes, so that user space can alloacate
		 * sufficient memory
		 */
		mutex_lock(&p->mutex);
		args->num_of_nodes = p->n_pdds;
		goto out_unlock;
	}

	/* Fill in process-aperture information for all available
	 * nodes, but not more than args->num_of_nodes as that is
	 * the amount of memory allocated by user
	 */
	pa = kzalloc((sizeof(struct opu_process_device_apertures) *
				args->num_of_nodes), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	mutex_lock(&p->mutex);

	if (!p->n_pdds) {
		args->num_of_nodes = 0;
		kfree(pa);
		goto out_unlock;
	}

	/* Run over all pdd of the process */
	for (i = 0; i < min(p->n_pdds, args->num_of_nodes); i++) {
		struct opu_process_device *pdd = p->pdds[i];

		pa[i].gpu_id = pdd->dev->id;
		pa[i].lds_base = pdd->lds_base;
		pa[i].lds_limit = pdd->lds_limit;
		pa[i].gpuvm_base = pdd->gpuvm_base;
		pa[i].gpuvm_limit = pdd->gpuvm_limit;
		pa[i].scratch_base = pdd->scratch_base;
		pa[i].scratch_limit = pdd->scratch_limit;

		dev_dbg(opu_device,
			"gpu id %u\n", pdd->dev->id);
		dev_dbg(opu_device,
			"lds_base %llX\n", pdd->lds_base);
		dev_dbg(opu_device,
			"lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(opu_device,
			"gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(opu_device,
			"gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(opu_device,
			"scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(opu_device,
			"scratch_limit %llX\n", pdd->scratch_limit);
	}
	mutex_unlock(&p->mutex);

	args->num_of_nodes = i;
	ret = copy_to_user(
			(void __user *)args->opu_process_device_apertures_ptr,
			pa,
			(i * sizeof(struct opu_process_device_apertures)));
	kfree(pa);
	return ret ? -EFAULT : 0;

out_unlock:
	mutex_unlock(&p->mutex);
	return 0;
}

static int opu_ioctl_create_event(struct file *filp, struct opu_process *p,
					void *data)
{
	struct opu_ioctl_create_event_args *args = data;
	int err;

	/* For dGPUs the event page is allocated in user mode. The
	 * handle is passed to opu with the first call to this IOCTL
	 * through the event_page_offset field.
	 */
	if (args->event_page_offset) {
		struct opu_dev *opu;
		struct opu_process_device *pdd;
		void *mem, *kern_addr;
		uint64_t size;

		if (p->signal_page) {
			pr_err("Event page is already set\n");
			return -EINVAL;
		}

		opu = opu_device_by_id(GET_GPU_ID(args->event_page_offset));
		if (!opu) {
			pr_err("Getting device by id failed in %s\n", __func__);
			return -EINVAL;
		}

		mutex_lock(&p->mutex);
		pdd = opu_bind_process_to_device(opu, p);
		if (IS_ERR(pdd)) {
			err = PTR_ERR(pdd);
			goto out_unlock;
		}

		mem = opu_process_device_translate_handle(pdd,
				GET_IDR_HANDLE(args->event_page_offset));
		if (!mem) {
			pr_err("Can't find BO, offset is 0x%llx\n",
			       args->event_page_offset);
			err = -EINVAL;
			goto out_unlock;
		}
		mutex_unlock(&p->mutex);

		err = amdgpu_opu_gpuvm_map_gtt_bo_to_kernel(opu->kgd,
						mem, &kern_addr, &size);
		if (err) {
			pr_err("Failed to map event page to kernel\n");
			return err;
		}

		err = opu_event_page_set(p, kern_addr, size);
		if (err) {
			pr_err("Failed to set event page\n");
			return err;
		}
	}

	err = opu_event_create(filp, p, args->event_type,
				args->auto_reset != 0, args->node_id,
				&args->event_id, &args->event_trigger_data,
				&args->event_page_offset,
				&args->event_slot_index);

	return err;

out_unlock:
	mutex_unlock(&p->mutex);
	return err;
}

static int opu_ioctl_destroy_event(struct file *filp, struct opu_process *p,
					void *data)
{
	struct opu_ioctl_destroy_event_args *args = data;

	return opu_event_destroy(p, args->event_id);
}

static int opu_ioctl_set_event(struct file *filp, struct opu_process *p,
				void *data)
{
	struct opu_ioctl_set_event_args *args = data;

	return opu_set_event(p, args->event_id);
}

static int opu_ioctl_reset_event(struct file *filp, struct opu_process *p,
				void *data)
{
	struct opu_ioctl_reset_event_args *args = data;

	return opu_reset_event(p, args->event_id);
}

static int opu_ioctl_wait_events(struct file *filp, struct opu_process *p,
				void *data)
{
	struct opu_ioctl_wait_events_args *args = data;
	int err;

	err = opu_wait_on_events(p, args->num_events,
			(void __user *)args->events_ptr,
			(args->wait_for_all != 0),
			args->timeout, &args->wait_result);

	return err;
}
static int opu_ioctl_set_scratch_backing_va(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_set_scratch_backing_va_args *args = data;
	struct opu_process_device *pdd;
	struct opu_dev *dev;
	long err;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_fail;
	}

	pdd->qpd.sh_hidden_private_base = args->va_addr;

	mutex_unlock(&p->mutex);

	if (dev->dqm->sched_policy == opu_SCHED_POLICY_NO_HWS &&
	    pdd->qpd.vmid != 0 && dev->opu2kgd->set_scratch_backing_va)
		dev->opu2kgd->set_scratch_backing_va(
			dev->kgd, args->va_addr, pdd->qpd.vmid);

	return 0;

bind_process_to_device_fail:
	mutex_unlock(&p->mutex);
	return err;
}

static int opu_ioctl_get_tile_config(struct file *filep,
		struct opu_process *p, void *data)
{
	struct opu_ioctl_get_tile_config_args *args = data;
	struct opu_dev *dev;
	struct tile_config config;
	int err = 0;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	amdgpu_opu_get_tile_config(dev->kgd, &config);

	args->gb_addr_config = config.gb_addr_config;
	args->num_banks = config.num_banks;
	args->num_ranks = config.num_ranks;

	if (args->num_tile_configs > config.num_tile_configs)
		args->num_tile_configs = config.num_tile_configs;
	err = copy_to_user((void __user *)args->tile_config_ptr,
			config.tile_config_ptr,
			args->num_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_tile_configs = 0;
		return -EFAULT;
	}

	if (args->num_macro_tile_configs > config.num_macro_tile_configs)
		args->num_macro_tile_configs =
				config.num_macro_tile_configs;
	err = copy_to_user((void __user *)args->macro_tile_config_ptr,
			config.macro_tile_config_ptr,
			args->num_macro_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_macro_tile_configs = 0;
		return -EFAULT;
	}

	return 0;
}

static int opu_ioctl_acquire_vm(struct file *filep, struct opu_process *p,
				void *data)
{
	struct opu_ioctl_acquire_vm_args *args = data;
	struct opu_process_device *pdd;
	struct opu_dev *dev;
	struct file *drm_file;
	int ret;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	drm_file = fget(args->drm_fd);
	if (!drm_file)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = opu_get_process_device_data(dev, p);
	if (!pdd) {
		ret = -EINVAL;
		goto err_unlock;
	}

	if (pdd->drm_file) {
		ret = pdd->drm_file == drm_file ? 0 : -EBUSY;
		goto err_unlock;
	}

	ret = opu_process_device_init_vm(pdd, drm_file);
	if (ret)
		goto err_unlock;
	/* On success, the PDD keeps the drm_file reference */
	mutex_unlock(&p->mutex);

	return 0;

err_unlock:
	mutex_unlock(&p->mutex);
	fput(drm_file);
	return ret;
}

bool opu_dev_is_large_bar(struct opu_dev *dev)
{
	struct opu_local_mem_info mem_info;

	if (debug_largebar) {
		pr_debug("Simulate large-bar allocation on non large-bar machine\n");
		return true;
	}

	if (dev->use_iommu_v2)
		return false;

	amdgpu_opu_get_local_mem_info(dev->kgd, &mem_info);
	if (mem_info.local_mem_size_private == 0 &&
			mem_info.local_mem_size_public > 0)
		return true;
	return false;
}

static int opu_ioctl_alloc_memory_of_gpu(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_alloc_memory_of_gpu_args *args = data;
	struct opu_process_device *pdd;
	void *mem;
	struct opu_dev *dev;
	int idr_handle;
	long err;
	uint64_t offset = args->mmap_offset;
	uint32_t flags = args->flags;

	if (args->size == 0)
		return -EINVAL;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	if ((flags & opu_IOC_ALLOC_MEM_FLAGS_PUBLIC) &&
		(flags & opu_IOC_ALLOC_MEM_FLAGS_VRAM) &&
		!opu_dev_is_large_bar(dev)) {
		pr_err("Alloc host visible vram on small bar is not allowed\n");
		return -EINVAL;
	}

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto err_unlock;
	}

	if (flags & opu_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
		if (args->size != opu_doorbell_process_slice(dev)) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = opu_get_process_doorbells(pdd);
	} else if (flags & opu_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
		if (args->size != PAGE_SIZE) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = amdgpu_opu_get_mmio_remap_phys_addr(dev->kgd);
		if (!offset) {
			err = -ENOMEM;
			goto err_unlock;
		}
	}

	err = amdgpu_opu_gpuvm_alloc_memory_of_gpu(
		dev->kgd, args->va_addr, args->size,
		pdd->drm_priv, (struct kgd_mem **) &mem, &offset,
		flags);

	if (err)
		goto err_unlock;

	idr_handle = opu_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		err = -EFAULT;
		goto err_free;
	}

	/* Update the VRAM usage count */
	if (flags & opu_IOC_ALLOC_MEM_FLAGS_VRAM)
		WRITE_ONCE(pdd->vram_usage, pdd->vram_usage + args->size);

	mutex_unlock(&p->mutex);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);
	args->mmap_offset = offset;

	/* MMIO is mapped through opu device
	 * Generate a opu mmap offset
	 */
	if (flags & opu_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)
		args->mmap_offset = opu_MMAP_TYPE_MMIO
					| opu_MMAP_GPU_ID(args->gpu_id);

	return 0;

err_free:
	amdgpu_opu_gpuvm_free_memory_of_gpu(dev->kgd, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
	mutex_unlock(&p->mutex);
	return err;
}

static int opu_ioctl_free_memory_of_gpu(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_free_memory_of_gpu_args *args = data;
	struct opu_process_device *pdd;
	void *mem;
	struct opu_dev *dev;
	int ret;
	uint64_t size = 0;

	dev = opu_device_by_id(GET_GPU_ID(args->handle));
	if (!dev)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = opu_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		ret = -EINVAL;
		goto err_unlock;
	}

	mem = opu_process_device_translate_handle(
		pdd, GET_IDR_HANDLE(args->handle));
	if (!mem) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = amdgpu_opu_gpuvm_free_memory_of_gpu(dev->kgd,
				(struct kgd_mem *)mem, pdd->drm_priv, &size);

	/* If freeing the buffer failed, leave the handle in place for
	 * clean-up during process tear-down.
	 */
	if (!ret)
		opu_process_device_remove_obj_handle(
			pdd, GET_IDR_HANDLE(args->handle));

	WRITE_ONCE(pdd->vram_usage, pdd->vram_usage - size);

err_unlock:
	mutex_unlock(&p->mutex);
	return ret;
}

static int opu_ioctl_map_memory_to_gpu(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_map_memory_to_gpu_args *args = data;
	struct opu_process_device *pdd, *peer_pdd;
	void *mem;
	struct opu_dev *dev, *peer;
	long err = 0;
	int i;
	uint32_t *devices_arr = NULL;
	bool table_freed = false;

	dev = opu_device_by_id(GET_GPU_ID(args->handle));
	if (!dev)
		return -EINVAL;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_failed;
	}

	mem = opu_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer = opu_device_by_id(devices_arr[i]);
		if (!peer) {
			pr_debug("Getting device by id failed for 0x%x\n",
				 devices_arr[i]);
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}

		peer_pdd = opu_bind_process_to_device(peer, p);
		if (IS_ERR(peer_pdd)) {
			err = PTR_ERR(peer_pdd);
			goto get_mem_obj_from_handle_failed;
		}
		err = amdgpu_opu_gpuvm_map_memory_to_gpu(
			peer->kgd, (struct kgd_mem *)mem,
			peer_pdd->drm_priv, &table_freed);
		if (err) {
			pr_err("Failed to map to gpu %d/%d\n",
			       i, args->n_devices);
			goto map_memory_to_gpu_failed;
		}
		args->n_success = i+1;
	}

	mutex_unlock(&p->mutex);

	err = amdgpu_opu_gpuvm_sync_memory(dev->kgd, (struct kgd_mem *) mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	/* Flush TLBs after waiting for the page table updates to complete */
	if (table_freed) {
		for (i = 0; i < args->n_devices; i++) {
			peer = opu_device_by_id(devices_arr[i]);
			if (WARN_ON_ONCE(!peer))
				continue;
			peer_pdd = opu_get_process_device_data(peer, p);
			if (WARN_ON_ONCE(!peer_pdd))
				continue;
			opu_flush_tlb(peer_pdd, TLB_FLUSH_LEGACY);
		}
	}
	kfree(devices_arr);

	return err;

bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
map_memory_to_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
sync_memory_failed:
	kfree(devices_arr);

	return err;
}

static int opu_ioctl_unmap_memory_from_gpu(struct file *filep,
					struct opu_process *p, void *data)
{
	struct opu_ioctl_unmap_memory_from_gpu_args *args = data;
	struct opu_process_device *pdd, *peer_pdd;
	void *mem;
	struct opu_dev *dev, *peer;
	long err = 0;
	uint32_t *devices_arr = NULL, i;

	dev = opu_device_by_id(GET_GPU_ID(args->handle));
	if (!dev)
		return -EINVAL;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);

	pdd = opu_get_process_device_data(dev, p);
	if (!pdd) {
		err = -EINVAL;
		goto bind_process_to_device_failed;
	}

	mem = opu_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer = opu_device_by_id(devices_arr[i]);
		if (!peer) {
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}

		peer_pdd = opu_get_process_device_data(peer, p);
		if (!peer_pdd) {
			err = -ENODEV;
			goto get_mem_obj_from_handle_failed;
		}
		err = amdgpu_opu_gpuvm_unmap_memory_from_gpu(
			peer->kgd, (struct kgd_mem *)mem, peer_pdd->drm_priv);
		if (err) {
			pr_err("Failed to unmap from gpu %d/%d\n",
			       i, args->n_devices);
			goto unmap_memory_from_gpu_failed;
		}
		args->n_success = i+1;
	}
	mutex_unlock(&p->mutex);

	err = amdgpu_opu_gpuvm_sync_memory(dev->kgd, (struct kgd_mem *) mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	/* Flush TLBs after waiting for the page table updates to complete */
	for (i = 0; i < args->n_devices; i++) {
		peer = opu_device_by_id(devices_arr[i]);
		if (WARN_ON_ONCE(!peer))
			continue;
		peer_pdd = opu_get_process_device_data(peer, p);
		if (WARN_ON_ONCE(!peer_pdd))
			continue;
		opu_flush_tlb(peer_pdd, TLB_FLUSH_HEAVYWEIGHT);
	}

	kfree(devices_arr);

	return 0;

bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
unmap_memory_from_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
sync_memory_failed:
	kfree(devices_arr);
	return err;
}

static int opu_ioctl_alloc_queue_gws(struct file *filep,
		struct opu_process *p, void *data)
{
	int retval;
	struct opu_ioctl_alloc_queue_gws_args *args = data;
	struct queue *q;
	struct opu_dev *dev;

	mutex_lock(&p->mutex);
	q = pqm_get_user_queue(&p->pqm, args->queue_id);

	if (q) {
		dev = q->device;
	} else {
		retval = -EINVAL;
		goto out_unlock;
	}

	if (!dev->gws) {
		retval = -ENODEV;
		goto out_unlock;
	}

	if (dev->dqm->sched_policy == opu_SCHED_POLICY_NO_HWS) {
		retval = -ENODEV;
		goto out_unlock;
	}

	retval = pqm_set_gws(&p->pqm, args->queue_id, args->num_gws ? dev->gws : NULL);
	mutex_unlock(&p->mutex);

	args->first_gws = 0;
	return retval;

out_unlock:
	mutex_unlock(&p->mutex);
	return retval;
}

static int opu_ioctl_get_dmabuf_info(struct file *filep,
		struct opu_process *p, void *data)
{
	struct opu_ioctl_get_dmabuf_info_args *args = data;
	struct opu_dev *dev = NULL;
	struct kgd_dev *dma_buf_kgd;
	void *metadata_buffer = NULL;
	uint32_t flags;
	unsigned int i;
	int r;

	/* Find a opu GPU device that supports the get_dmabuf_info query */
	for (i = 0; opu_topology_enum_opu_devices(i, &dev) == 0; i++)
		if (dev)
			break;
	if (!dev)
		return -EINVAL;

	if (args->metadata_ptr) {
		metadata_buffer = kzalloc(args->metadata_size, GFP_KERNEL);
		if (!metadata_buffer)
			return -ENOMEM;
	}

	/* Get dmabuf info from KGD */
	r = amdgpu_opu_get_dmabuf_info(dev->kgd, args->dmabuf_fd,
					  &dma_buf_kgd, &args->size,
					  metadata_buffer, args->metadata_size,
					  &args->metadata_size, &flags);
	if (r)
		goto exit;

	/* Reverse-lookup gpu_id from kgd pointer */
	dev = opu_device_by_kgd(dma_buf_kgd);
	if (!dev) {
		r = -EINVAL;
		goto exit;
	}
	args->gpu_id = dev->id;
	args->flags = flags;

	/* Copy metadata buffer to user mode */
	if (metadata_buffer) {
		r = copy_to_user((void __user *)args->metadata_ptr,
				 metadata_buffer, args->metadata_size);
		if (r != 0)
			r = -EFAULT;
	}

exit:
	kfree(metadata_buffer);

	return r;
}

static int opu_ioctl_import_dmabuf(struct file *filep,
				   struct opu_process *p, void *data)
{
	struct opu_ioctl_import_dmabuf_args *args = data;
	struct opu_process_device *pdd;
	struct dma_buf *dmabuf;
	struct opu_dev *dev;
	int idr_handle;
	uint64_t size;
	void *mem;
	int r;

	dev = opu_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	dmabuf = dma_buf_get(args->dmabuf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	mutex_lock(&p->mutex);

	pdd = opu_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		r = PTR_ERR(pdd);
		goto err_unlock;
	}

	r = amdgpu_opu_gpuvm_import_dmabuf(dev->kgd, dmabuf,
					      args->va_addr, pdd->drm_priv,
					      (struct kgd_mem **)&mem, &size,
					      NULL);
	if (r)
		goto err_unlock;

	idr_handle = opu_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		r = -EFAULT;
		goto err_free;
	}

	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);

	return 0;

err_free:
	amdgpu_opu_gpuvm_free_memory_of_gpu(dev->kgd, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);
	return r;
}

/* Handle requests for watching SMI events */
static int opu_ioctl_smi_events(struct file *filep,
				struct opu_process *p, void *data)
{
	struct opu_ioctl_smi_events_args *args = data;
	struct opu_dev *dev;

	dev = opu_device_by_id(args->gpuid);
	if (!dev)
		return -EINVAL;

	return opu_smi_event_open(dev, &args->anon_fd);
}

static int opu_ioctl_set_xnack_mode(struct file *filep,
				    struct opu_process *p, void *data)
{
	struct opu_ioctl_set_xnack_mode_args *args = data;
	int r = 0;

	mutex_lock(&p->mutex);
	if (args->xnack_enabled >= 0) {
		if (!list_empty(&p->pqm.queues)) {
			pr_debug("Process has user queues running\n");
			mutex_unlock(&p->mutex);
			return -EBUSY;
		}
		if (args->xnack_enabled && !opu_process_xnack_mode(p, true))
			r = -EPERM;
		else
			p->xnack_enabled = args->xnack_enabled;
	} else {
		args->xnack_enabled = p->xnack_enabled;
	}
	mutex_unlock(&p->mutex);

	return r;
}

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)
static int opu_ioctl_svm(struct file *filep, struct opu_process *p, void *data)
{
	struct opu_ioctl_svm_args *args = data;
	int r = 0;

	pr_debug("start 0x%llx size 0x%llx op 0x%x nattr 0x%x\n",
		 args->start_addr, args->size, args->op, args->nattr);

	if ((args->start_addr & ~PAGE_MASK) || (args->size & ~PAGE_MASK))
		return -EINVAL;
	if (!args->start_addr || !args->size)
		return -EINVAL;

	mutex_lock(&p->mutex);

	r = svm_ioctl(p, args->op, args->start_addr, args->size, args->nattr,
		      args->attrs);

	mutex_unlock(&p->mutex);

	return r;
}
#else
static int opu_ioctl_svm(struct file *filep, struct opu_process *p, void *data)
{
	return -EPERM;
}
#endif

#define OPU_IOCTL_DEF(ioctl, _func, _flags) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func, .flags = _flags, \
			    .cmd_drv = 0, .name = #ioctl}

typedef int opu_ioctl_t(struct file *filep, struct opu_process *p,
				void *data);

struct opu_ioctl_desc {
	unsigned int cmd;
	int flags;
	opu_ioctl_t *func;
	unsigned int cmd_drv;
	const char *name;
};


/** Ioctl table */
static const struct opu_ioctl_desc opu_chardev_ioctls[] = {
	OPU_IOCTL_DEF(OPU_IOC_GET_VERSION, opu_ioctl_get_version, 0),
	OPU_IOCTL_DEF(OPU_IOC_CREATE_QUEUE, opu_ioctl_create_queue, 0),
	OPU_IOCTL_DEF(OPU_IOC_DESTROY_QUEUE, opu_ioctl_destroy_queue, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_MEMORY_POLICY, opu_ioctl_set_memory_policy, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_CLOCK_COUNTERS, opu_ioctl_get_clock_counters, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_PROCESS_APERTURES, opu_ioctl_get_process_apertures, 0),
	OPU_IOCTL_DEF(OPU_IOC_UPDATE_QUEUE, opu_ioctl_update_queue, 0),
	OPU_IOCTL_DEF(OPU_IOC_CREATE_EVENT, opu_ioctl_create_event, 0),
	OPU_IOCTL_DEF(OPU_IOC_DESTROY_EVENT, opu_ioctl_destroy_event, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_EVENT, opu_ioctl_set_event, 0),
	OPU_IOCTL_DEF(OPU_IOC_RESET_EVENT, opu_ioctl_reset_event, 0),
	OPU_IOCTL_DEF(OPU_IOC_WAIT_EVENTS, opu_ioctl_wait_events, 0),
	OPU_IOCTL_DEF(OPU_IOC_DBG_REGISTER, opu_ioctl_dbg_register, 0),
	OPU_IOCTL_DEF(OPU_IOC_DBG_UNREGISTER, opu_ioctl_dbg_unregister, 0),
	OPU_IOCTL_DEF(OPU_IOC_DBG_ADDRESS_WATCH, opu_ioctl_dbg_address_watch, 0),
	OPU_IOCTL_DEF(OPU_IOC_DBG_WAVE_CONTROL, opu_ioctl_dbg_wave_control, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_SCRATCH_BACKING_VA, opu_ioctl_set_scratch_backing_va, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_TILE_CONFIG, opu_ioctl_get_tile_config, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_TRAP_HANDLER, opu_ioctl_set_trap_handler, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_PROCESS_APERTURES_NEW, opu_ioctl_get_process_apertures_new, 0),
	OPU_IOCTL_DEF(OPU_IOC_ACQUIRE_VM, opu_ioctl_acquire_vm, 0),
	OPU_IOCTL_DEF(OPU_IOC_ALLOC_MEMORY_OF_GPU, opu_ioctl_alloc_memory_of_gpu, 0),
	OPU_IOCTL_DEF(OPU_IOC_FREE_MEMORY_OF_GPU, opu_ioctl_free_memory_of_gpu, 0),
	OPU_IOCTL_DEF(OPU_IOC_MAP_MEMORY_TO_GPU, opu_ioctl_map_memory_to_gpu, 0),
	OPU_IOCTL_DEF(OPU_IOC_UNMAP_MEMORY_FROM_GPU, opu_ioctl_unmap_memory_from_gpu, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_CU_MASK, opu_ioctl_set_cu_mask, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_QUEUE_WAVE_STATE, opu_ioctl_get_queue_wave_state, 0),
	OPU_IOCTL_DEF(OPU_IOC_GET_DMABUF_INFO, opu_ioctl_get_dmabuf_info, 0),
	OPU_IOCTL_DEF(OPU_IOC_IMPORT_DMABUF, opu_ioctl_import_dmabuf, 0),
	OPU_IOCTL_DEF(OPU_IOC_ALLOC_QUEUE_GWS, opu_ioctl_alloc_queue_gws, 0),
	OPU_IOCTL_DEF(OPU_IOC_SMI_EVENTS, opu_ioctl_smi_events, 0),
	OPU_IOCTL_DEF(OPU_IOC_SVM, opu_ioctl_svm, 0),
	OPU_IOCTL_DEF(OPU_IOC_SET_XNACK_MODE, opu_ioctl_set_xnack_mode, 0),
};

#define OPU_CORE_IOCTL_COUNT	ARRAY_SIZE(opu_chardev_ioctls)

static long opu_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct opu_process *process;
	opu_ioctl_t *func;
	const struct opu_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (nr >= OPU_CORE_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= OPU_COMMAND_START) && (nr < OPU_COMMAND_END)) {
		u32 opu_size;

		ioctl = &opu_chardev_ioctls[nr];

		opu_size = _IOC_SIZE(ioctl->cmd);
		usize = asize = _IOC_SIZE(cmd);
		if (opu_size > asize)
			asize = opu_size;

		cmd = ioctl->cmd;
	} else
		goto err_i1;

	dev_dbg(opu_device, "ioctl cmd 0x%x (#0x%x), arg 0x%lx\n", cmd, nr, arg);

	/* Get the process struct from the filep. Only the process
	 * that opened /dev/opu can use the file descriptor. Child
	 * processes need to create their own opu device context.
	 */
	process = filep->private_data;
	if (process->lead_thread != current->group_leader) {
		dev_dbg(opu_device, "Using opu FD in wrong process\n");
		retcode = -EBADF;
		goto err_i1;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(opu_device, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kmalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto err_i1;
			}
		}
		if (asize > usize)
			memset(kdata + usize, 0, asize - usize);
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize) != 0) {
			retcode = -EFAULT;
			goto err_i1;
		}
	} else if (cmd & IOC_OUT) {
		memset(kdata, 0, usize);
	}

	retcode = func(filep, process, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize) != 0)
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(opu_device, "invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(opu_device, "ioctl cmd (#0x%x), arg 0x%lx, ret = %d\n",
				nr, arg, retcode);

	return retcode;
}

static int opu_mmio_mmap(struct opu_dev *dev, struct opu_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;
	int ret;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	address = amdgpu_opu_get_mmio_remap_phys_addr(dev->kgd);

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("pasid 0x%x mapping mmio page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 process->pasid, (unsigned long long) vma->vm_start,
		 address, vma->vm_flags, PAGE_SIZE);

	ret = io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				PAGE_SIZE,
				vma->vm_page_prot);
	return ret;
}


static int opu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct opu_process *process;
	struct opu_dev *dev = NULL;
	unsigned long mmap_offset;
	unsigned int gpu_id;

	process = opu_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	mmap_offset = vma->vm_pgoff << PAGE_SHIFT;
	gpu_id = opu_MMAP_GET_GPU_ID(mmap_offset);
	if (gpu_id)
		dev = opu_device_by_id(gpu_id);

	switch (mmap_offset & opu_MMAP_TYPE_MASK) {
	case opu_MMAP_TYPE_DOORBELL:
		if (!dev)
			return -ENODEV;
		return opu_doorbell_mmap(dev, process, vma);

	case opu_MMAP_TYPE_EVENTS:
		return opu_event_mmap(process, vma);

	case opu_MMAP_TYPE_RESERVED_MEM:
		if (!dev)
			return -ENODEV;
		return opu_reserved_mem_mmap(dev, process, vma);
	case opu_MMAP_TYPE_MMIO:
		if (!dev)
			return -ENODEV;
		return opu_mmio_mmap(dev, process, vma);
	}

	return -EFAULT;
}
