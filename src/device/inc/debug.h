#pragma once
#include "inc/hsakmttypes.h"
#include "device_type.h"

// int debug_get_reg_status(uint32_t node_id, bool *is_debugged);
uint32_t *convert_queue_ids(uint32_t NumQueues, HSA_QUEUEID *Queues);
class Device;

class Debug {
public:
    Debug(Device* device) : device_(device) {
    };
    device_status_t init_device_debugging_memory(unsigned int NumNodes);
    void destroy_device_debugging_memory(void);

    device_status_t DbgRegister(uint32_t NodeId);
    device_status_t DbgUnregister(uint32_t NodeId);

    device_status_t DbgWavefrontControl(uint32_t NodeId,
						  HSA_DBG_WAVEOP Operand,
						  HSA_DBG_WAVEMODE Mode,
						  uint32_t TrapId,
						  HsaDbgWaveMessage *DbgWaveMsgRing);

    device_status_t DbgAddressWatch(uint32_t NodeId,
					      uint32_t NumWatchPoints,
					      HSA_DBG_WATCH_MODE WatchMode[],
					      void *WatchAddress[],
					      uint64_t WatchMask[],
					      HsaEvent *WatchEvent[]);

    device_status_t SetAddressWatch(
		uint32_t		NodeId,
		uint32_t		Pid,
		HSA_DBG_WATCH_MODE	WatchMode,
		void			*WatchAddress,
		uint64_t		WatchAddrMask,
		uint32_t		*WatchId
		);

    device_status_t ClearAddressWatch(
		uint32_t NodeId,
		uint32_t Pid,
		uint32_t WatchId
		);

    device_status_t SetTrapHandler(HSAuint32 Node,
					     void *TrapHandlerBaseAddress,
					     HSAuint64 TrapHandlerSizeInBytes,
					     void *TrapBufferBaseAddress,
					     HSAuint64 TrapBufferSizeInBytes);

    int debug_get_reg_status(uint32_t node_id, bool *is_debugged);

    device_status_t debug_trap(uint32_t NodeId,
				uint32_t op,
				uint32_t data1,
				uint32_t data2,
				uint32_t data3,
				uint32_t pid,
				uint64_t pointer,
				struct ioctl_dbg_trap_args *argout);

    device_status_t EnableDebugTrapWithPollFd(uint32_t   NodeId,
							HSA_QUEUEID QueueId,
							HSAint32 *PollFd); //OUT

    device_status_t DeviceSetTrapHandler(HSAuint32 Node,
					     void *TrapHandlerBaseAddress,
					     HSAuint64 TrapHandlerSizeInBytes,
					     void *TrapBufferBaseAddress,
					     HSAuint64 TrapBufferSizeInBytes);

    device_status_t EnableDebugTrap(uint32_t   NodeId,
					      HSA_QUEUEID QueueId);

    device_status_t DisableDebugTrap(uint32_t NodeId);

    device_status_t SetWaveLaunchTrapOverride(
					uint32_t NodeId,
					HSA_DBG_TRAP_OVERRIDE TrapOverride,
					HSA_DBG_TRAP_MASK     TrapMask);

    device_status_t SetWaveLaunchMode(
				uint32_t NodeId,
				HSA_DBG_WAVE_LAUNCH_MODE WaveLaunchMode);

    device_status_t QueueSuspend(
		uint32_t    Pid,         // IN
		uint32_t    NumQueues,   // IN
		HSA_QUEUEID *Queues,      // IN
		uint32_t    GracePeriod, // IN
		uint32_t    Flags);       // IN

    device_status_t QueueResume(
		uint32_t    Pid,         // IN
		uint32_t    NumQueues,   // IN
		HSA_QUEUEID *Queues,      // IN
		uint32_t    Flags);       // IN

    device_status_t QueryDebugEvent(
		uint32_t		NodeId, //IN
		uint32_t		Pid, // IN
		uint32_t		*QueueId, // IN/OUT
		bool			ClearEvents, // IN
		DEBUG_EVENT_TYPE	*EventsReceived, // OUT
		bool			*IsSuspended, // OUT
		bool			*IsNew // OUT
		);

    device_status_t GetKernelDebugTrapVersionInfo(
        uint32_t *Major,  //Out
        uint32_t *Minor   //Out
    );

    device_status_t GetQueueSnapshot(
		uint32_t	NodeId, //IN
		uint32_t	Pid, // IN
		bool		ClearEvents, //IN
		void		*SnapshotBuf, //IN
		uint32_t	*QssEntries //IN/OUT
		);

public:
    Device *device_;
    bool *is_device_debugged;
};
