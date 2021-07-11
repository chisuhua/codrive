#pragma once

#include <functional>

#include "inc/hsakmttypes.h"
#include "core/IDevice.h"
#include "core/IMemoryRegion.h"
#include "inc/DeviceInfo.h"
#include "cmdio/cmdio.h"

class MemMgr;
class Doorbells;
class Debug;
class Process;
class Topology;

using QueueAllocator = std::function<void *(size_t, size_t, uint32_t)>;
using QueueDeallocator = std::function<void(void *)>;

struct gpu_info {
    uint32_t id;
};

class Device : public core::IDevice {
public:
    core::IMemoryRegion *system_region_;
    core::IRuntime* runtime_;
    std::mutex mutex_;
    MemMgr* mm_;
    Doorbells* doorbells_;
    Debug* debug_;
    Process* process_;
    DeviceInfo *device_info_;
    Topology*               topo_;
    bool             zfb_support_ {true};

    QueueAllocator queue_allocator_;
    QueueDeallocator queue_deallocator_;
    uint32_t *all_gpu_id_array {nullptr};
    uint32_t all_gpu_id_array_size {0};
    // std::vector<gpu_info>     gpu_info_table_;
    //
    HsaSystemProperties *system_;
    std::vector<node_props_t*> props_;
    bool is_dgpu_;

    int PAGE_SIZE;
    int PAGE_SHIFT;

    MemMgr* get_mm() {
        return mm_;
    }

    Debug* get_debug() {
        return debug_;
    }

    Topology* get_topo() {
        return topo_;
    }

    Process* get_process() {
        return process_;
    }

    bool is_dgpu(void) {
        return is_dgpu_;
    };

    void set_dgpu(void) {
        is_dgpu_ = true;
    }

    void init_page_size(void);
    device_info *get_device_info_by_dev_id(uint16_t dev_id) ;
    void init_queue_allocator(QueueAllocator queue_allocator, QueueDeallocator queue_deallocator) {
        queue_allocator_ = queue_allocator;
        queue_deallocator_ = queue_deallocator;
    }


    device_status_t Open(void);
    device_status_t Close(void);
    /** Returns the user-kernel interface version supported by KFD.
  Higher major numbers usually add new features to KFD and may break user-kernel
  compatibility; higher minor numbers define additional functionality associated
  within a major number.
  The calling software should validate that it meets the minimum interface version
  as described in the API specification.  */

    device_status_t GetVersion(
        HsaVersionInfo* VersionInfo //OUT
    );

    /** The function takes a "snapshot" of the topology information within the KFD
  to avoid any changes during the enumeration process.  */

    device_status_t AcquireSystemProperties(
        HsaSystemProperties* SystemProperties //OUT
    );

    /** Releases the topology "snapshot" taken by AcquireSystemProperties() */

    device_status_t ReleaseSystemProperties(void);

    /** Retrieves the discoverable sub-properties for a given HSA
  node. The parameters returned allow the application or runtime to size the
  management structures necessary to store the information.  */

    device_status_t GetNodeProperties(
        HSAuint32 NodeId, //IN
        HsaCoreProperties** CoreProperties //OUT
    );

    /** Retrieves the memory properties of a specific HSA node.
  the memory pointer passed as MemoryProperties is sized as
  NumBanks * sizeof(HsaMemoryProperties). NumBanks is retrieved with the
  GetNodeProperties() call.

  Some of the data returned is optional. Not all implementations may return all
  parameters in the hsaMemoryProperties.  */

    device_status_t GetNodeMemoryProperties(
        HSAuint32 NodeId, //IN
        HSAuint32 NumBanks, //IN
        HsaMemoryProperties* MemoryProperties //OUT
    );

    /** Retrieves the cache properties of a specific HSA node and processor ID.
  ProcessorID refers to either a CPU core or a SIMD unit as enumerated earlier
  via the GetNodeProperties() call.
  The memory pointer passed as CacheProperties is sized as
  NumCaches * sizeof(HsaCacheProperties). NumCaches is retrieved with the
  GetNodeProperties() call.

  The data returned is optional. Not all implementations may return all
  parameters in the CacheProperties.  */

    device_status_t GetNodeCacheProperties(
        HSAuint32 NodeId, //IN
        HSAuint32 ProcessorId, //IN
        HSAuint32 NumCaches, //IN
        HsaCacheProperties* CacheProperties //OUT
    );

    /** Retrieves the HSA IO affinity properties of a specific HSA node.
  the memory pointer passed as Properties is sized as
  NumIoLinks * sizeof(HsaIoLinkProperties). NumIoLinks is retrieved with the
  GetNodeProperties() call.

  The data returned is optional. Not all implementations may return all
  parameters in the IoLinkProperties.  */

    device_status_t GetNodeIoLinkProperties(
        HSAuint32 NodeId, //IN
        HSAuint32 NumIoLinks, //IN
        HsaIoLinkProperties* IoLinkProperties //OUT
    );

    /** Creates an operating system event associated with a HSA event ID */

    device_status_t CreateEvent(
        HsaEventDescriptor* EventDesc, //IN
        bool ManualReset, //IN
        bool IsSignaled, //IN
        HsaEvent** Event //OUT
    );

    /** Destroys an operating system event associated with a HSA event ID */

    device_status_t DestroyEvent(
        HsaEvent* Event //IN
    );

    /** Sets the specified event object to the signaled state */

    device_status_t SetEvent(
        HsaEvent* Event //IN
    );

    /** Sets the specified event object to the non-signaled state */

    device_status_t ResetEvent(
        HsaEvent* Event //IN
    );

    /** Queries the state of the specified event object */

    device_status_t QueryEventState(
        HsaEvent* Event //IN
    );

    /** Checks the current state of the event object. If the object's state is
  nonsignaled, the calling thread enters the wait state.

 The function returns when one of the following occurs:
- The specified event object is in the signaled state.
- The time-out interval elapses.  */

    device_status_t
    WaitOnEvent(
        HsaEvent* Event, //IN
        HSAuint32 Milliseconds //IN
    );

    /** Checks the current state of multiple event objects.

 The function returns when one of the following occurs:
- Either any one or all of the specified objects are in the signaled state
  - if "WaitOnAll" is "true" the function returns when the state of all
    objects in array is signaled
  - if "WaitOnAll" is "false" the function returns when the state of any
    one of the objects is set to signaled
- The time-out interval elapses.  */

    device_status_t
    WaitOnMultipleEvents(
        HsaEvent* Events[], //IN
        HSAuint32 NumEvents, //IN
        bool WaitOnAll, //IN
        HSAuint32 Milliseconds //IN
    );

    /** new TEMPORARY function definition - to be used only on "Triniti + Southern Islands" platform
  If used on other platforms the function will return device_status_t_ERROR */

    device_status_t
    ReportQueue(
        HSA_QUEUEID QueueId, //IN
        HsaQueueReport* QueueReport //OUT
    );

    /** Creates a GPU queue with user-mode access rights */
    device_status_t
    CreateQueue(
        HSAuint32 NodeId, //IN
        HSA_QUEUE_TYPE Type, //IN
        HSAuint32 QueuePercentage, //IN
        HSA_QUEUE_PRIORITY Priority, //IN
        void* QueueAddress, //IN
        HSAuint64 QueueSizeInBytes, //IN
        HsaEvent* Event, //IN
        // co_queue_t*         cp_queue
        HsaQueueResource* QueueResource //OUT
    );

    /** Updates a queue */

    device_status_t
    UpdateQueue(
        HSA_QUEUEID QueueId, //IN
        HSAuint32 QueuePercentage, //IN
        HSA_QUEUE_PRIORITY Priority, //IN
        void* QueueAddress, //IN
        HSAuint64 QueueSize, //IN
        HsaEvent* Event //IN
    );

    /** Destroys a queue */

    device_status_t
    DestroyQueue(
        HSA_QUEUEID QueueId //IN
    );

    /** Set cu mask for a queue */

    device_status_t
    SetQueueCUMask(
        HSA_QUEUEID QueueId, //IN
        HSAuint32 CUMaskCount, //IN
        HSAuint32* QueueCUMask //IN
    );

    device_status_t
    GetQueueInfo(
        HSA_QUEUEID QueueId, //IN
        HsaQueueInfo* QueueInfo //IN
    );

    /** Allows an HSA process to set/change the default and alternate memory coherency, before starting to dispatch.  */

    device_status_t
    SetMemoryPolicy(
        HSAuint32 Node, //IN
        HSAuint32 DefaultPolicy, //IN
        HSAuint32 AlternatePolicy, //IN
        void* MemoryAddressAlternate, //IN (page-aligned)
        HSAuint64 MemorySizeInBytes //IN (page-aligned)
    );
    /** Allocates a memory buffer that may be accessed by the GPU */

    device_status_t
    AllocMemory(
        HSAuint32 PreferredNode, //IN
        HSAuint64 SizeInBytes, //IN  (multiple of page size)
        HsaMemFlags MemFlags, //IN
        void** MemoryAddress // ,          //OUT (page-aligned)
        // bool            isSystem = false        // TODO schi , I added for system alloc throught device alloc
    );

    /** Frees a memory buffer */

    device_status_t
    FreeMemory(
        void* MemoryAddress, //IN (page-aligned)
        HSAuint64 SizeInBytes //IN
    );

    /** Registers with KFD a memory buffer that may be accessed by the GPU */

    device_status_t
    RegisterMemory(
        void* MemoryAddress, //IN (cache-aligned)
        HSAuint64 MemorySizeInBytes //IN (cache-aligned)
    );

    /** Registers with KFD a memory buffer that may be accessed by specific GPUs */

    device_status_t
    RegisterMemoryToNodes(
        void* MemoryAddress, // IN (cache-aligned)
        HSAuint64 MemorySizeInBytes, // IN (cache-aligned)
        HSAuint64 NumberOfNodes, // IN
        HSAuint32* NodeArray // IN
    );

    /** Registers with KFD a memory buffer with memory attributes */

    device_status_t
    RegisterMemoryWithFlags(
        void* MemoryAddress, // IN (cache-aligned)
        HSAuint64 MemorySizeInBytes, // IN (cache-aligned)
        HsaMemFlags MemFlags // IN
    );

    /** Registers with KFD a graphics buffer and returns graphics metadata */

    device_status_t
    RegisterGraphicsHandleToNodes(
        HSAuint64 GraphicsResourceHandle, //IN
        HsaGraphicsResourceInfo* GraphicsResourceInfo, //OUT
        HSAuint64 NumberOfNodes, //IN
        HSAuint32* NodeArray //IN
    );

    /** Export a memory buffer for sharing with other processes

 NOTE: for the current revision of the thunk spec, SizeInBytes
 must match whole allocation.  */
    device_status_t
    ShareMemory(
        void* MemoryAddress, // IN
        HSAuint64 SizeInBytes, // IN
        HsaSharedMemoryHandle* SharedMemoryHandle // OUT
    );

    /** Register shared memory handle */
    device_status_t
    RegisterSharedHandle(
        const HsaSharedMemoryHandle* SharedMemoryHandle, // IN
        void** MemoryAddress, // OUT
        HSAuint64* SizeInBytes // OUT
    );

    /** Register shared memory handle to specific nodes only */
    device_status_t
    RegisterSharedHandleToNodes(
        const HsaSharedMemoryHandle* SharedMemoryHandle, // IN
        void** MemoryAddress, // OUT
        HSAuint64* SizeInBytes, // OUT
        HSAuint64 NumberOfNodes, // OUT
        HSAuint32* NodeArray // OUT
    );

    /** Copy data from the GPU address space of the process identified
 by Pid. Size Copied will return actual amount of data copied.
 If return is not SUCCESS, partial copies could have happened.  */
    device_status_t
    ProcessVMRead(
        HSAuint32 Pid, // IN
        HsaMemoryRange* LocalMemoryArray, // IN
        HSAuint64 LocalMemoryArrayCount, // IN
        HsaMemoryRange* RemoteMemoryArray, // IN
        HSAuint64 RemoteMemoryArrayCount, // IN
        HSAuint64* SizeCopied // OUT
    );

    /** Write data to the GPU address space of the process identified
 by Pid. See also ProcessVMRead.  */
    device_status_t
    ProcessVMWrite(
        HSAuint32 Pid, // IN
        HsaMemoryRange* LocalMemoryArray, // IN
        HSAuint64 LocalMemoryArrayCount, // IN
        HsaMemoryRange* RemoteMemoryArray, // IN
        HSAuint64 RemoteMemoryArrayCount, // IN
        HSAuint64* SizeCopied // OUT
    );

    /** Unregisters with KFD a memory buffer */

    device_status_t
    DeregisterMemory(
        void* MemoryAddress //IN
    );

    /** Ensures that the memory is resident and can be accessed by GPU */

    device_status_t
    MapMemoryToGPU(
        void* MemoryAddress, //IN (page-aligned)
        HSAuint64 MemorySizeInBytes, //IN (page-aligned)
        HSAuint64* AlternateVAGPU //OUT (page-aligned)
    );

    /** Ensures that the memory is resident and can be accessed by GPUs */

    device_status_t
    MapMemoryToGPUNodes(
        void* MemoryAddress, //IN (page-aligned)
        HSAuint64 MemorySizeInBytes, //IN (page-aligned)
        HSAuint64* AlternateVAGPU, //OUT (page-aligned)
        HsaMemMapFlags MemMapFlags, //IN
        HSAuint64 NumberOfNodes, //IN
        HSAuint32* NodeArray //IN
    );

    /** Releases the residency of the memory */

    device_status_t
    UnmapMemoryToGPU(
        void* MemoryAddress //IN (page-aligned)
    );

    /** Notifies the kernel driver that a process wants to use GPU debugging facilities */

    device_status_t
    MapGraphicHandle(
        HSAuint32 NodeId, //IN
        HSAuint64 GraphicDeviceHandle, //IN
        HSAuint64 GraphicResourceHandle, //IN
        HSAuint64 GraphicResourceOffset, //IN
        HSAuint64 GraphicResourceSize, //IN
        HSAuint64* FlatMemoryAddress //OUT
    );

    /** Stub for Unmap Graphic Handle */

    device_status_t
    UnmapGraphicHandle(
        HSAuint32 NodeId, //IN
        HSAuint64 FlatMemoryAddress, //IN
        HSAuint64 SizeInBytes //IN
    );

    /** Notifies the kernel driver that a process wants to use GPU debugging facilities */

    device_status_t
    DbgRegister(
        HSAuint32 NodeId //IN
    );

    /** Detaches the debugger process from the HW debug established by DbgRegister() API */

    device_status_t
    DbgUnregister(
        HSAuint32 NodeId //IN
    );

    /** Controls a wavefront */

    device_status_t
    DbgWavefrontControl(
        HSAuint32 NodeId, //IN
        HSA_DBG_WAVEOP Operand, //IN
        HSA_DBG_WAVEMODE Mode, //IN
        HSAuint32 TrapId, //IN
        HsaDbgWaveMessage* DbgWaveMsgRing //IN
    );

    /** Sets watch points on memory address ranges to generate exception events when the
  watched addresses are  accessed */

    device_status_t
    DbgAddressWatch(
        HSAuint32 NodeId, //IN
        HSAuint32 NumWatchPoints, //IN
        HSA_DBG_WATCH_MODE WatchMode[], //IN
        void* WatchAddress[], //IN
        HSAuint64 WatchMask[], //IN, optional
        HsaEvent* WatchEvent[] //IN, optional
    );

    /** Gets GPU and CPU clock counters for particular Node */

    device_status_t
    GetClockCounters(
        HSAuint32 NodeId, //IN
        HsaClockCounters* Counters //OUT
    );
#if 0
    /** Retrieves information on the available HSA counters */
    device_status_t
    PmcGetCounterProperties(
        HSAuint32 NodeId, //IN
        HsaCounterProperties** CounterProperties //OUT
    );

    /** Registers a set of (HW) counters to be used for tracing/profiling */

    device_status_t
    PmcRegisterTrace(
        HSAuint32 NodeId, //IN
        HSAuint32 NumberOfCounters, //IN
        HsaCounter* Counters, //IN
        HsaPmcTraceRoot* TraceRoot //OUT
    );

    /** Unregisters a set of (HW) counters used for tracing/profiling */

    device_status_t
    PmcUnregisterTrace(
        HSAuint32 NodeId, //IN
        HSATraceId TraceId //IN
    );

    /** Allows a user mode process to get exclusive access to the defined set of (HW) counters
  used for tracing/profiling */

    device_status_t
    PmcAcquireTraceAccess(
        HSAuint32 NodeId, //IN
        HSATraceId TraceId //IN
    );

    /** Allows a user mode process to release exclusive access to the defined set of (HW) counters
  used for tracing/profiling */

    device_status_t
    PmcReleaseTraceAccess(
        HSAuint32 NodeId, //IN
        HSATraceId TraceId //IN
    );

    /** Starts tracing operation on a previously established set of performance counters */

    device_status_t
    PmcStartTrace(
        HSATraceId TraceId, //IN
        void* TraceBuffer, //IN (page aligned)
        HSAuint64 TraceBufferSizeBytes //IN (page aligned)
    );

    /**
   Forces an update of all the counters that a previously started trace operation has registered */

    device_status_t
    PmcQueryTrace(
        HSATraceId TraceId //IN
    );

    /** Stops tracing operation on a previously established set of performance counters */

    device_status_t
    PmcStopTrace(
        HSATraceId TraceId //IN
    );
#endif
    /** Sets trap handler and trap buffer to be used for all queues associated with the specified NodeId within this process context */

    device_status_t
    SetTrapHandler(
        HSAuint32 NodeId, //IN
        void* TrapHandlerBaseAddress, //IN
        HSAuint64 TrapHandlerSizeInBytes, //IN
        void* TrapBufferBaseAddress, //IN
        HSAuint64 TrapBufferSizeInBytes //IN
    );

    /** Gets image tile configuration.  */
    device_status_t
    GetTileConfig(
        HSAuint32 NodeId, // IN
        HsaGpuTileConfig* config // IN & OUT
    );

    /** Returns information about pointers */
    device_status_t
    QueryPointerInfo(
        const void* Pointer, //IN
        HsaPointerInfo* PointerInfo //OUT
    );

    /** Associates user data with a memory allocation */
    device_status_t
    SetMemoryUserData(
        const void* Pointer, //IN
        void* UserData //IN
    );
};
