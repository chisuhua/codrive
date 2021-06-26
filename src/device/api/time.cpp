#include "libhsakmt.h"
#include "cmdio.h"

device_status_t DEVICEAPI DeviceGetClockCounters(HSAuint32 NodeId,
					       HsaClockCounters *Counters)
{
	device_status_t result;
	uint32_t gpu_id;
	// struct kfd_ioctl_get_clock_counters_args args = {0};
	// int err;


	result = validate_nodeid(NodeId, &gpu_id);
	if (result != DEVICE_STATUS_SUCCESS)
		return result;
/*
 * TODO schi implement it in CSI
	args.gpu_id = gpu_id;

	err = kmtIoctl(kfd_fd, AMDKFD_IOC_GET_CLOCK_COUNTERS, &args);
	if (err < 0) {
		result = DEVICE_STATUS_ERROR;
	} else {
		Counters->GPUClockCounter = args.gpu_clock_counter;
		Counters->CPUClockCounter = args.cpu_clock_counter;
		Counters->SystemClockCounter = args.system_clock_counter;
		Counters->SystemClockFrequencyHz = args.system_clock_freq;
	}
*/
	return result;
}
