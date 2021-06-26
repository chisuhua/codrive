#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace core {
    class EventPool;
}

//
// HSA STATUS codes returned by the KFD Interfaces
//

typedef enum _DEVICE_STATUS
{
    DEVICE_STATUS_SUCCESS                      = 0,  // Operation successful
    DEVICE_STATUS_ERROR                        = 1,  // General error return if not otherwise specified
    DEVICE_STATUS_DRIVER_MISMATCH              = 2,  // User mode component is not compatible with kernel HSA driver

    DEVICE_STATUS_INVALID_PARAMETER            = 3,  // KFD identifies input parameters invalid
    DEVICE_STATUS_INVALID_HANDLE               = 4,  // KFD identifies handle parameter invalid
    DEVICE_STATUS_INVALID_NODE_UNIT            = 5,  // KFD identifies node or unit parameter invalid

    DEVICE_STATUS_NO_MEMORY                    = 6,  // No memory available (when allocating queues or memory)
    DEVICE_STATUS_BUFFER_TOO_SMALL             = 7,  // A buffer needed to handle a request is too small

    DEVICE_STATUS_NOT_IMPLEMENTED              = 10, // KFD function is not implemented for this set of paramters
    DEVICE_STATUS_NOT_SUPPORTED                = 11, // KFD function is not supported on this node
    DEVICE_STATUS_UNAVAILABLE                  = 12, // KFD function is not available currently on this node (but
                                                  // may be at a later time)
    DEVICE_STATUS_OUT_OF_RESOURCES             = 13, // KFD function request exceeds the resources currently available.

    DEVICE_STATUS_KERNEL_IO_CHANNEL_NOT_OPENED = 20, // KFD driver path not opened
    DEVICE_STATUS_KERNEL_COMMUNICATION_ERROR   = 21, // user-kernel mode communication failure
    DEVICE_STATUS_KERNEL_ALREADY_OPENED        = 22, // KFD driver path already opened
    DEVICE_STATUS_HSAMMU_UNAVAILABLE           = 23, // ATS/PRI 1.1 (Address Translation Services) not available
                                                  // (IOMMU driver not installed or not-available)

    DEVICE_STATUS_WAIT_FAILURE                 = 30, // The wait operation failed
    DEVICE_STATUS_WAIT_TIMEOUT                 = 31, // The wait operation timed out

    DEVICE_STATUS_MEMORY_ALREADY_REGISTERED    = 35, // Memory buffer already registered
    DEVICE_STATUS_MEMORY_NOT_REGISTERED        = 36, // Memory buffer not registered
    DEVICE_STATUS_MEMORY_ALIGNMENT             = 37, // Memory parameter not aligned

} device_status_t;

//
// HSA sync primitive, Event and HW Exception notification API definitions
// The API functions allow the runtime to define a so-called sync-primitive, a SW object
// combining a user-mode provided "syncvar" and a scheduler event that can be signaled
// through a defined GPU interrupt. A syncvar is a process virtual memory location of
// a certain size that can be accessed by CPU and GPU shader code within the process to set
// and query the content within that memory. The definition of the content is determined by
// the HSA runtime and potentially GPU shader code interfacing with the HSA runtime.
// The syncvar values may be commonly written through an PM4 WRITE_DATA packet in the
// user mode instruction stream.
// The OS scheduler event is typically associated and signaled by an interrupt issued by
// the GPU, but other HSA system interrupt conditions from other HW (e.g. IOMMUv2) may be
// surfaced by the KFD by this mechanism, too.
// these are the new definitions for events
typedef enum _EVENTTYPE
{
    EVENTTYPE_SIGNAL                     = 0, //user-mode generated GPU signal
    EVENTTYPE_NODECHANGE                 = 1, //HSA node change (attach/detach)
    EVENTTYPE_DEVICESTATECHANGE          = 2, //HSA device state change( start/stop )
    EVENTTYPE_HW_EXCEPTION               = 3, //GPU shader exception event
    EVENTTYPE_SYSTEM_EVENT               = 4, //GPU SYSCALL with parameter info
    EVENTTYPE_DEBUG_EVENT                = 5, //GPU signal for debugging
    EVENTTYPE_PROFILE_EVENT              = 6, //GPU signal for profiling
    EVENTTYPE_QUEUE_EVENT                = 7, //GPU signal queue idle state (EOP pm4)
    EVENTTYPE_MEMORY                     = 8, //GPU signal for signaling memory access faults and memory subsystem issues
    //...
    EVENTTYPE_MAXID,
    EVENTTYPE_TYPE_SIZE                  = 0xFFFFFFFF
} EVENTTYPE;

//
// Definitions for types of pending debug events
//
typedef enum _DEBUG_EVENT_TYPE
{
	DEBUG_EVENT_TYPE_NONE				= 0,
	DEBUG_EVENT_TYPE_TRAP				= 1,
	DEBUG_EVENT_TYPE_VMFAULT			= 2,
	DEBUG_EVENT_TYPE_TRAP_VMFAULT		= 3
} DEBUG_EVENT_TYPE;

typedef uint32_t  EVENTID;


typedef struct _HsaSyncVar
{
    union
    {
        void*       UserData;           //pointer to user mode data
        uint64_t   UserDataPtrValue;   //64bit compatibility of value
    } SyncVar;
    uint64_t       SyncVarSize;
} HsaSyncVar;


//
// Subdefinitions for various event types: NodeChange
//

typedef enum _EVENTTYPE_NODECHANGE_FLAGS
{
    EVENTTYPE_NODECHANGE_ADD     = 0,
    EVENTTYPE_NODECHANGE_REMOVE  = 1,
    EVENTTYPE_NODECHANGE_SIZE    = 0xFFFFFFFF
} EVENTTYPE_NODECHANGE_FLAGS;

typedef struct _HsaNodeChange
{
    EVENTTYPE_NODECHANGE_FLAGS Flags;   // HSA node added/removed on the platform
} HsaNodeChange;


//
// Sub-definitions for various event types: DeviceStateChange
//

typedef enum _EVENTTYPE_DEVICESTATECHANGE_FLAGS
{
    EVENTTYPE_DEVICESTATUSCHANGE_START     = 0, //device started (and available)
    EVENTTYPE_DEVICESTATUSCHANGE_STOP      = 1, //device stopped (i.e. unavailable)
    EVENTTYPE_DEVICESTATUSCHANGE_SIZE      = 0xFFFFFFFF
} EVENTTYPE_DEVICESTATECHANGE_FLAGS;

typedef struct _HsaDeviceStateChange
{
    uint32_t                           NodeId;     // F-NUMA node that contains the device
    // DEVICE                          Device;     // device type: GPU or CPU
    EVENTTYPE_DEVICESTATECHANGE_FLAGS Flags;    // event flags
} HsaDeviceStateChange;


//
// Sub-definitions for various event types: Memory exception
//

typedef enum _EVENTID_MEMORYFLAGS
{
    EVENTID_MEMORY_RECOVERABLE           = 0, //access fault, recoverable after page adjustment
    EVENTID_MEMORY_FATAL_PROCESS         = 1, //memory access requires process context destruction, unrecoverable
    EVENTID_MEMORY_FATAL_VM              = 2, //memory access requires all GPU VA context destruction, unrecoverable
} EVENTID_MEMORYFLAGS;

typedef struct _HsaAccessAttributeFailure
{
    unsigned int NotPresent  : 1;  // Page not present or supervisor privilege
    unsigned int ReadOnly    : 1;  // Write access to a read-only page
    unsigned int NoExecute   : 1;  // Execute access to a page marked NX
    unsigned int GpuAccess   : 1;  // Host access only
    unsigned int ECC         : 1;  // RAS ECC failure (notification of DRAM ECC - non-recoverable - error, if supported by HW)
    unsigned int Imprecise   : 1;  // Can't determine the exact fault address
    unsigned int ErrorType   : 3;  // Indicates RAS errors or other errors causing the access to GPU to fail
                                      // 0 = no RAS error, 1 = ECC_SRAM, 2 = Link_SYNFLOOD (poison), 3 = GPU hang (not attributable to a specific cause), other values reserved
} HsaAccessAttributeFailure;

// data associated with EVENTID_MEMORY
typedef struct _HsaMemoryAccessFault
{
    uint32_t                       NodeId;             // H-NUMA node that contains the device where the memory access occurred
    uint64_t                       VirtualAddress;     // virtual address this occurred on
    HsaAccessAttributeFailure       Failure;            // failure attribute
    EVENTID_MEMORYFLAGS         Flags;              // event flags
} HsaMemoryAccessFault;


typedef struct _HsaEventData
{
    EVENTTYPE   EventType;      //event type

    union
    {
        // return data associated with EVENTTYPE_SIGNAL and other events
        HsaSyncVar              SyncVar;

        // data associated with EVENTTYPE_NODE_CHANGE
        HsaNodeChange           NodeChangeState;

        // data associated with EVENTTYPE_DEVICE_STATE_CHANGE
        HsaDeviceStateChange    DeviceState;

        // data associated with EVENTTYPE_MEMORY
        HsaMemoryAccessFault    MemoryAccessFault;

    } EventData;

    // the following data entries are internal to the KFD & thunk itself.

    uint64_t       HWData1;                    // internal thunk store for Event data  (OsEventHandle)
    uint64_t       HWData2;                    // internal thunk store for Event data  (HWAddress)
    uint32_t       HWData3;                    // internal thunk store for Event data  (HWData)
} HsaEventData;

typedef struct _HsaEventDescriptor
{
    EVENTTYPE   EventType;                  // event type to allocate
    uint32_t       NodeId;                     // H-NUMA node containing GPU device that is event source
    HsaSyncVar      SyncVar;                    // pointer to user mode syncvar data, syncvar->UserDataPtrValue may be NULL
} HsaEventDescriptor;


typedef struct _HsaEvent
{
    EVENTID     EventId;
    HsaEventData   EventData;
    core::EventPool*     event_pool_;
} HsaEvent;

typedef enum _HsaEventTimeout
{
    EVENTTIMEOUT_IMMEDIATE  = 0,
    EVENTTIMEOUT_INFINITE   = 0xFFFFFFFF
} HsaEventTimeOut;

typedef enum _HSA_QUEUE_PRIORITY
{
    HSA_QUEUE_PRIORITY_MINIMUM        = -3,
    HSA_QUEUE_PRIORITY_LOW            = -2,
    HSA_QUEUE_PRIORITY_BELOW_NORMAL   = -1,
    HSA_QUEUE_PRIORITY_NORMAL         =  0,
    HSA_QUEUE_PRIORITY_ABOVE_NORMAL   =  1,
    HSA_QUEUE_PRIORITY_HIGH           =  2,
    HSA_QUEUE_PRIORITY_MAXIMUM        =  3,
    HSA_QUEUE_PRIORITY_NUM_PRIORITY,
    HSA_QUEUE_PRIORITY_SIZE           = 0xFFFFFFFF
} HSA_QUEUE_PRIORITY;

typedef enum _HSA_QUEUE_TYPE
{
    HSA_QUEUE_COMPUTE            = 1,  // AMD PM4 compatible Compute Queue
    HSA_QUEUE_SDMA               = 2,  // PCIe optimized SDMA Queue, used for data transport and format conversion (e.g. (de-)tiling, etc).
    HSA_QUEUE_MULTIMEDIA_DECODE  = 3,  // reserved, for HSA multimedia decode queue
    HSA_QUEUE_MULTIMEDIA_ENCODE  = 4,  // reserved, for HSA multimedia encode queue
    HSA_QUEUE_SDMA_XGMI          = 5,  // XGMI optimized SDMA Queue

    // the following values indicate a queue type permitted to reference OS graphics
    // resources through the interoperation API. See [5] "HSA Graphics Interoperation
    // specification" for more details on use of such resources.

    HSA_QUEUE_COMPUTE_OS           = 11, // AMD PM4 compatible Compute Queue
    HSA_QUEUE_SDMA_OS              = 12, // SDMA Queue, used for data transport and format conversion (e.g. (de-)tiling, etc).
    HSA_QUEUE_MULTIMEDIA_DECODE_OS = 13, // reserved, for HSA multimedia decode queue
    HSA_QUEUE_MULTIMEDIA_ENCODE_OS = 14,  // reserved, for HSA multimedia encode queue

    HSA_QUEUE_COMPUTE_AQL          = 21, // HSA AQL packet compatible Compute Queue
    HSA_QUEUE_DMA_AQL              = 22, // HSA AQL packet compatible DMA Queue
    HSA_QUEUE_DMA_AQL_XGMI         = 23, // HSA AQL packet compatible XGMI optimized DMA Queue

    // more types in the future

    HSA_QUEUE_TYPE_SIZE            = 0xFFFFFFFF     //aligns to 32bit enum
} HSA_QUEUE_TYPE;

typedef union {
    uint32_t Value;
    struct
    {
        unsigned int HotPluggable : 1; // the node may be removed by some system action
            // (event will be sent)
        unsigned int HSAMMUPresent : 1; // This node has an ATS/PRI 1.1 compatible
            // translation agent in the system (e.g. IOMMUv2)
        unsigned int SharedWithGraphics : 1; // this HSA nodes' GPU function is also used for OS primary
            // graphics render (= UI)
        unsigned int QueueSizePowerOfTwo : 1; // This node GPU requires the queue size to be a power of 2 value
        unsigned int QueueSize32bit : 1; // This node GPU requires the queue size to be less than 4GB
        unsigned int QueueIdleEvent : 1; // This node GPU supports notification on Queue Idle
        unsigned int VALimit : 1; // This node GPU has limited VA range for platform
            // (typical 40bit). Affects shared VM use for 64bit apps
        unsigned int WatchPointsSupported : 1; // Indicates if Watchpoints are available on the node.
        unsigned int WatchPointsTotalBits : 4; // ld(Watchpoints) available. To determine the number use 2^value

        unsigned int DoorbellType : 2; // 0: This node has pre-1.0 doorbell characteristic
            // 1: This node has 1.0 doorbell characteristic
            // 2,3: reserved for future use
        unsigned int AQLQueueDoubleMap : 1; // The unit needs a VA “double map”
        unsigned int DebugTrapSupported : 1; // Indicates if Debug Trap is supported on the node.
        unsigned int WaveLaunchTrapOverrideSupported : 1; // Indicates if Wave Launch Trap Override is supported on the node.
        unsigned int WaveLaunchModeSupported : 1; // Indicates if Wave Launch Mode is supported on the node.
        unsigned int PreciseMemoryOperationsSupported : 1; // Indicates if Precise Memory Operations are supported on the node.
        unsigned int SRAM_EDCSupport : 1; // Indicates if GFX internal SRAM EDC/ECC functionality is active
        unsigned int Mem_EDCSupport : 1; // Indicates if GFX internal DRAM/HBM EDC/ECC functionality is active
        unsigned int RASEventNotify : 1; // Indicates if GFX extended RASFeatures and RAS EventNotify status is available
        unsigned int ASICRevision : 4; // Indicates the ASIC revision of the chip on this node.
        unsigned int Reserved : 6;
    } ui32;
} HSA_CAPABILITY;

typedef struct _HsaNodeProperties {
    uint32_t NumCPUCores; // # of latency (= CPU) cores present on this HSA node.
        // This value is 0 for a HSA node with no such cores,
        // e.g a "discrete HSA GPU"
    uint32_t NumFComputeCores; // # of HSA throughtput (= GPU) FCompute cores ("SIMD") present in a node.
        // This value is 0 if no FCompute cores are present (e.g. pure "CPU node").
    uint32_t NumMemoryBanks; // # of discoverable memory bank affinity properties on this "H-NUMA" node.
    uint32_t NumCaches; // # of discoverable cache affinity properties on this "H-NUMA"  node.

    uint32_t NumIOLinks; // # of discoverable IO link affinity properties of this node
        // connecting to other nodes.

    uint32_t CComputeIdLo; // low value of the logical processor ID of the latency (= CPU)
        // cores available on this node
    uint32_t FComputeIdLo; // low value of the logical processor ID of the throughput (= GPU)
        // units available on this node

    HSA_CAPABILITY Capability; // see above

    uint32_t MaxWavesPerSIMD; // This identifies the max. number of launched waves per SIMD.
        // If NumFComputeCores is 0, this value is ignored.
    uint32_t LDSSizeInKB; // Size of Local Data Store in Kilobytes per SIMD Wavefront
    uint32_t GDSSizeInKB; // Size of Global Data Store in Kilobytes shared across SIMD Wavefronts

    uint32_t WaveFrontSize; // Number of SIMD cores per wavefront executed, typically 64,
        // may be 32 or a different value for some HSA based architectures

    uint32_t NumShaderBanks; // Number of Shader Banks or Shader Engines, typical values are 1 or 2

    uint32_t NumArrays; // Number of SIMD arrays per engine
    uint32_t NumCUPerArray; // Number of Compute Units (CU) per SIMD array
    uint32_t NumSIMDPerCU; // Number of SIMD representing a Compute Unit (CU)

    uint32_t MaxSlotsScratchCU; // Number of temp. memory ("scratch") wave slots available to access,
        // may be 0 if HW has no restrictions

    // HSA_ENGINE_ID   EngineId;          // Identifier (rev) of the GPU uEngine or Firmware, may be 0

    uint16_t VendorId; // GPU vendor id; 0 on latency (= CPU)-only nodes
    uint16_t DeviceId; // GPU device id; 0 on latency (= CPU)-only nodes

    uint32_t LocationId; // GPU BDF (Bus/Device/function number) - identifies the device
        // location in the overall system
    uint64_t LocalMemSize; // Local memory size
    uint32_t MaxEngineClockMhzFCompute; // maximum engine clocks for CPU and
    uint32_t MaxEngineClockMhzCCompute; // GPU function, including any boost caopabilities,
    int32_t DrmRenderMinor; // DRM render device minor device number
    //uint16_t       MarketingName[HSA_PUBLIC_NAME_SIZE];   // Public name of the "device" on the node (board or APU name).
    // Unicode string
    //uint8_t        AMDName[HSA_PUBLIC_NAME_SIZE];   //CAL Name of the "device", ASCII
    // HSA_ENGINE_VERSION uCodeEngineVersions;
    // HSA_DEBUG_PROPERTIES DebugProperties; // Debug properties of this node.
    uint64_t HiveID; // XGMI Hive the GPU node belongs to in the system. It is an opaque and static
        // number hash created by the PSP
    uint32_t NumSdmaEngines; // number of PCIe optimized SDMA engines
    uint32_t NumSdmaXgmiEngines; // number of XGMI optimized SDMA engines

    uint8_t NumSdmaQueuesPerEngine; // number of SDMA queue per one engine
    uint8_t NumCpQueues; // number of Compute queues
    uint8_t NumGws; // number of GWS barriers
    uint8_t Reserved2;

    uint32_t Domain; // PCI domain of the GPU
    uint64_t UniqueID; // Globally unique immutable id
    uint8_t Reserved[20];
} HsaNodeProperties;

#ifdef __cplusplus
}   //extern "C"
#endif

