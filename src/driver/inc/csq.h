#pragma once
#include <vector>
#include <algorithm>
#include <future>
#include "utils/lang/debug.h"
#include "inc/pps.h"
#include "inc/csq_defines.h"
#include "inc/unpinned_copy_engine.h"
// #include "hc.h"
// #include "inc/csq_accelerator.h"

/////////////////////////////////////////////////
// kernel dispatch speed optimization flags
/////////////////////////////////////////////////

// size of default kernarg buffer in the kernarg pool in HSAContext
#define KERNARG_BUFFER_SIZE (512)

// number of pre-allocated kernarg buffers in HSAContext
// Not required but typically should be greater than HCC_SIGNAL_POOL_SIZE
// (some kernels don't allocate signals but nearly all need kernargs)
#define KERNARG_POOL_SIZE (1024)

//-------------------------------------------
//   HCC enviroment variable move out
//-------------------------------------------
//---
// Environment variables:
extern int HCC_PRINT_ENV;
extern int HCC_SIGNAL_POOL_SIZE;
extern int HCC_UNPINNED_COPY_MODE;
extern int HCC_CHECK_COPY;

// Copy thresholds, in KB.  These are used for "choose-best" copy mode.
extern long int HCC_H2D_STAGING_THRESHOLD    ;
extern long int HCC_H2D_PININPLACE_THRESHOLD ;
extern long int HCC_D2H_PININPLACE_THRESHOLD ;

// Staging buffer size in KB for unpinned copy engines
extern int HCC_STAGING_BUFFER_SIZE ;

// Default GPU device
extern unsigned int HCC_DEFAULT_GPU ;

extern unsigned int HCC_ENABLE_PRINTF ;

// Chicken bits:
extern int HCC_SERIALIZE_KERNEL ;
extern int HCC_SERIALIZE_COPY ;
extern int HCC_FORCE_COMPLETION_FUTURE ;
extern int HCC_FORCE_CROSS_QUEUE_FLUSH;

extern int HCC_OPT_FLUSH;


extern unsigned HCC_DB ;
extern unsigned HCC_DB_SYMBOL_FORMAT;

extern unsigned int HCC_MAX_QUEUES ;

#define HCC_PROFILE_SUMMARY (1<<0)
#define HCC_PROFILE_TRACE   (1<<1)
extern int HCC_PROFILE;


#define HCC_PROFILE_VERBOSE_BASIC                   (1 << 0)   // 0x1
#define HCC_PROFILE_VERBOSE_TIMESTAMP               (1 << 1)   // 0x2
#define HCC_PROFILE_VERBOSE_OPSEQNUM                (1 << 2)   // 0x4
#define HCC_PROFILE_VERBOSE_TID                     (1 << 3)   // 0x8
#define HCC_PROFILE_VERBOSE_BARRIER                 (1 << 4)   // 0x10
extern int HCC_PROFILE_VERBOSE;


// int HCC_PROFILE_VERBOSE=0x1F;
extern char * HCC_PROFILE_FILE;

// Profiler:
// Use str::stream so output is atomic wrt other threads:
#define LOG_PROFILE(op, start, end, type, tag, msg) \
{\
    std::stringstream sstream;\
    sstream << "profile: " << std::setw(7) << type << ";\t" \
                         << std::setw(40) << tag\
                         << ";\t" << std::fixed << std::setw(6) << std::setprecision(1) << (end-start)/1000.0 << " us;";\
    if (HCC_PROFILE_VERBOSE & (HCC_PROFILE_VERBOSE_TIMESTAMP)) {\
            sstream << "\t" << start << ";\t" << end << ";";\
    }\
    if (HCC_PROFILE_VERBOSE & (HCC_PROFILE_VERBOSE_OPSEQNUM)) {\
            sstream << "\t" << *op << ";";\
    }\
    sstream <<  msg << "\n";\
    getDlaContext()->getHccProfileStream() << sstream.str();\
}


//--------------------------------------------

// Maximum number of inflight commands sent to a single queue.
// If limit is exceeded, HCC will force a queue wait to reclaim
// resources (signals, kernarg)
// MUST be a power of 2.
#define MAX_INFLIGHT_COMMANDS_PER_QUEUE  (2*8192)

// threshold to clean up finished kernel in HSAQueue.asyncOps
#define ASYNCOPS_VECTOR_GC_SIZE (2*8192)

// get the file name w/o path
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/')+1 : __FILE__)

#define STATUS_CHECK(s,line) if (s != SUCCESS && s != HSA_STATUS_INFO_BREAK ) {\
    const char* error_string = getHSAErrorString(s);\
        printf("### STATUS_CHECK Error: %s (0x%x) at file:%s line:%d\n", error_string, s, __FILENAME__, line);\
        abort();\
    }

extern const char* getHSAErrorString(status_t s) ;


namespace csq {

class AmPointerInfo;
class completion_future;

// Commands sent to copy queues:
/*
static inline bool isCopyCommand(hcCommandKind k)
{
    switch (k) {
        case hcMemcpyHostToHost:
        case hcMemcpyHostToDevice:
        case hcMemcpyDeviceToHost:
        case hcMemcpyDeviceToDevice:
            return true;
        default:
            return false;
    };
};

// Commands sent to compute queue:
static inline bool isComputeQueueCommand(hcCommandKind k) {
    return (k == hcCommandKernel) || (k == hcCommandMarker);
};
*/
enum HCCRuntimeStatus{
  HCCRT_STATUS_SUCCESS = 0x0, 						// No error
  HCCRT_STATUS_ERROR = 0x2000, 						// A generic error
  HCCRT_STATUS_ERROR_COMMAND_QUEUE_OVERFLOW = 0x2001 // The maximum number of outstanding AQL packets in a queue has been reached
};

const char* getHCCRuntimeStatusMessage(const HCCRuntimeStatus status) ;
void checkHCCRuntimeStatus(const HCCRuntimeStatus status, const unsigned int line, queue_t* q=nullptr) ;

/// forward declaration
class Device;
class Queue;

/// AsyncOp
///
/// This is an abstraction of all asynchronous operations
class AsyncOp {
public:
  AsyncOp(Queue *xqueue, hcCommandKind xCommandKind) : queue(xqueue), commandKind(xCommandKind), seqNum(0) {}

  virtual ~AsyncOp() {}
  virtual std::shared_future<void>* getFuture() { return nullptr; }
  virtual void* getNativeHandle() { return nullptr;}
  // * @return An implementaion-defined timestamp.
  virtual uint64_t getBeginTimestamp() { return 0L; }
  // * @return An implementation-defined timestamp.
  virtual uint64_t getEndTimestamp() { return 0L; }
  // * @return An implementation-defined frequency for the asynchronous operation.
  virtual uint64_t getTimestampFrequency() { return 0L; }
  /**
   * Get if the async operations has been completed.
   *
   * @return True if the async operation has been completed, false if not.
   */
  virtual bool isReady() { return false; }

  /**
   * Set the wait mode of the async operation.
   *
   * @param mode[in] wait mode, must be one of the value in hcWaitMode enum.
   */
  virtual void setWaitMode(hcWaitMode mode) {}

  void setSeqNumFromQueue();

  uint64_t getSeqNum () const { return seqNum;};

  hcCommandKind getCommandKind() const { return commandKind; };
  void          setCommandKind(hcCommandKind xCommandKind) { commandKind = xCommandKind; };

  Queue  *getQueue() const { return queue; };

private:
  Queue    *queue;

  // Kind of this command - copy, kernel, barrier, etc:
  hcCommandKind  commandKind;


  // Sequence number of this op in the queue it is dispatched into.
  uint64_t       seqNum;

};




/// Queue
/// This is the implementation of accelerator_view
/// KalamrQueue is responsible for data operations and launch kernel
class Queue
{
public:

  Queue(Device* pDev, queuing_mode mode = queuing_mode_automatic, execute_order order = execute_in_order, queue_priority priority = priority_normal)
      : pDev(pDev), mode(mode), order(order), priority(priority), opSeqNums(0) {}

  virtual ~Queue() {}
  virtual void flush() {}
  virtual void wait(hcWaitMode mode = hcWaitModeBlocked) {}
/*
  // sync kernel launch with dynamic group memory
  virtual void LaunchKernelWithDynamicGroupMemory(void *kernel, size_t dim_ext, size_t *ext, size_t *local_size, size_t dynamic_group_size) {}

  // async kernel launch with dynamic group memory
  virtual std::shared_ptr<KalmarAsyncOp> LaunchKernelWithDynamicGroupMemoryAsync(void *kernel, size_t dim_ext, size_t *ext, size_t *local_size, size_t dynamic_group_size) { return nullptr; }

  // sync kernel launch
  virtual void LaunchKernel(void *kernel, size_t dim_ext, size_t *ext, size_t *local_size) {}

  // async kernel launch
  virtual std::shared_ptr<KalmarAsyncOp> LaunchKernelAsync(void *kernel, size_t dim_ext, size_t *ext, size_t *local_size) { return LaunchKernelWithDynamicGroupMemoryAsync(kernel, dim_ext, ext, local_size, 0); }

  /// read data from device to host
  virtual void read(void* device, void* dst, size_t count, size_t offset) = 0;

  /// wrtie data from host to device
  virtual void write(void* device, const void* src, size_t count, size_t offset, bool blocking) = 0;

  /// copy data between two device pointers
  virtual void copy(void* src, void* dst, size_t count, size_t src_offset, size_t dst_offset, bool blocking) = 0;

  /// map host accessible pointer from device
  virtual void* map(void* device, size_t count, size_t offset, bool modify) = 0;

  /// unmap host accessible pointer
  virtual void unmap(void* device, void* addr, size_t count, size_t offset, bool modify) = 0;

  /// push device pointer to kernel argument list
  virtual void Push(void *kernel, int idx, void* device, bool modify) = 0;
*/

  Device* getDev() const { return pDev; }
  queuing_mode get_mode() const { return mode; }
  void set_mode(queuing_mode mod) { mode = mod; }
  execute_order get_execute_order() const { return order; }
  queue_priority get_queue_priority() const { return priority; }

  /// get number of pending async operations in the queue
  virtual int getPendingAsyncOps() { return 0; }

  /// Is the queue empty?  Same as getPendingAsyncOps but may be faster.
  virtual bool isEmpty() { return 0; }


  /// get underlying native queue handle
  virtual void* getDlaQueue() { return nullptr; }
  /// get underlying native agent handle
  virtual void* getDlaAgent() { return nullptr; }
  virtual void* getDlaAMRegion() { return nullptr; }

  virtual void* getDlaAMHostRegion() { return nullptr; }
  virtual void* getDlaCoherentAMHostRegion() { return nullptr; }
  /// get kernarg region handle
  virtual void* getDlaKernargRegion() { return nullptr; }

  /// enqueue marker
  virtual std::shared_ptr<AsyncOp> EnqueueMarker(memory_scope) { return nullptr; }
  virtual std::shared_ptr<AsyncOp> EnqueueMarkerWithDependency(int count, std::shared_ptr <AsyncOp> *depOps, memory_scope scope) { return nullptr; }
  virtual std::shared_ptr<AsyncOp> detectStreamDeps(hcCommandKind commandKind, AsyncOp *newCopyOp) { return nullptr; };

  /// copy src to dst asynchronously
  virtual std::shared_ptr<AsyncOp> EnqueueAsyncCopy(const void* src, void* dst, size_t size_bytes) { return nullptr; }
  virtual std::shared_ptr<AsyncOp> EnqueueAsyncCopyExt(const void* src, void* dst, size_t size_bytes,
                            hcCommandKind copyDir, const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, const Device *copyDevice) { return nullptr; };



  // Copy src to dst synchronously
  virtual void copy(const void *src, void *dst, size_t size_bytes) { };

  /// copy src to dst, with caller providing extended information about the pointers.
  //// TODO - remove me, this form is deprecated.
  virtual void copy_ext(const void *src, void *dst, size_t size_bytes, hcCommandKind copyDir, const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, bool forceUnpinnedCopy) { };

  virtual void copy_ext(const void *src, void *dst, size_t size_bytes, hcCommandKind copyDir, const AmPointerInfo &srcInfo, const AmPointerInfo &dstInfo, const Device *copyDev, bool forceUnpinnedCopy) { };

  /// cleanup internal resource
  /// this function is usually called by dtor of the implementation classes
  /// in rare occasions it may be called by other functions to ensure proper
  /// resource clean up sequence
  virtual void dispose() {};

  virtual void dispatch_hsa_kernel(const hsa_kernel_dispatch_packet_t *aql,
                                   const void * args, size_t argsize,
                                   completion_future *cf, const char *kernel_name)  { };

  /// set CU affinity of this queue.
  /// the setting is permanent until the queue is destroyed or another setting
  /// is called.
  virtual bool set_cu_mask(const std::vector<bool>& cu_mask) { return false; };

  uint64_t assign_op_seq_num() { return ++opSeqNums; };

private:
  Device* pDev;
  queuing_mode mode;
  execute_order order;
  queue_priority priority;

  uint64_t      opSeqNums; // last seqnum assigned to an op in this queue

};

/// This is the base implementation of accelerator
/// Device is responsible for create/release memory on device
class Device
{
private:
    access_type cpu_type;
    /// default Queue
    std::shared_ptr<Queue> def;
    /// make sure KalamrQueue is created only once
    std::once_flag flag;
protected:
    // True if the device memory is mapped into CPU address space and can be
    // directly accessed with CPU memory operations.
    bool cpu_accessible_am;


public:
    Device()
        : def(), flag()
    {};
    virtual ~Device() {};

    access_type get_access() const { return cpu_type; }
    void set_access(access_type type) { cpu_type = type; }

    virtual std::wstring get_path() const = 0;
    virtual std::wstring get_description() const = 0;
    virtual size_t get_mem() const = 0;
    virtual bool is_double() const = 0;
    virtual bool is_lim_double() const = 0;
    virtual bool is_unified() const = 0;
    virtual bool is_emulated() const = 0;

    virtual uint32_t get_version() const = 0;

    /// create buffer
    /// @key on device that supports shared memory
    //       key can used to avoid duplicate allocation
    virtual void* create(size_t count, struct rw_info* key) = 0;

    /// release buffer
    /// @key: used to avoid duplicate release
    virtual void release(void* ptr, struct rw_info* key) = 0;

    /// build program
    virtual void BuildProgram(void* size, void* source) {}

    /// create kernel
    virtual void* CreateKernel(const char* fun, Queue *queue) { return nullptr; }

    /// check if a given kernel is compatible with the device
    virtual bool IsCompatibleKernel(void* size, void* source) { return true; }

    /// check the dimension information is correct
    virtual bool check(size_t* size, size_t dim_ext) { return true; }


    /// create Queue from current device
    virtual std::shared_ptr<Queue> createQueue(execute_order order = execute_in_order, queue_priority priority = priority_normal) = 0;

    virtual std::vector< std::shared_ptr<Queue> > get_all_queues() { return std::vector< std::shared_ptr<Queue> >(); }

    std::shared_ptr<Queue> get_default_queue() {
        std::call_once(flag, [&]() {
            def = createQueue();
        });
        return def;
    }
    virtual void memcpySymbol(const char* symbolName, void* hostptr, size_t count, size_t offset = 0, hcCommandKind kind = hcMemcpyHostToDevice) {}

    virtual void memcpySymbol(void* symbolAddr, void* hostptr, size_t count, size_t offset = 0, hcCommandKind kind = hcMemcpyHostToDevice) {}

    virtual void* getSymbolAddress(const char* symbolName) { return nullptr; }


    virtual void* getDlaAgent() { return nullptr; }

    /// check if @p other can access to this device's device memory, return true if so, false otherwise
    virtual bool is_peer(const Device* other) {return false;}

    virtual unsigned int get_compute_unit_count() {return 0;}

    virtual int get_seqnum() const {return -1;}
    virtual bool has_cpu_accessible_am() {return false;}
};

/// This is responsible for managing all devices
/// User will need to add their customize devices
class Context
{
private:
    //We might be able to assume that only the first device is CPU, or we only mimic one cpu
    //device when constructing Context.
    Device* get_default_dev() {
        if (!def) {
            if (Devices.size() <= 1) {
                fprintf(stderr, "There is no device can be used to do the computation\n");
                exit(-1);
            }
            def = Devices[1];
        }
        return def;
    }
protected:
    /// default device
    Device* def;
    std::vector<Device*> Devices;
    Context() : def(nullptr), Devices() { }

    bool init_success = false;

public:
    virtual ~Context() {}

    std::vector<Device*> getDevices() { return Devices; }
    Device* getDevice(std::wstring path = L"") {
        if (path == L"default" || path == L"")
            return get_default_dev();
        auto result = std::find_if(std::begin(Devices), std::end(Devices),
                                   [&] (const Device* dev)
                                   { return dev->get_path() == path; });
        if (result != std::end(Devices))
            return *result;
        else
            return get_default_dev();
    }

    /// set default device by path
    bool set_default(const std::wstring& path) {
        auto result = std::find_if(std::begin(Devices), std::end(Devices),
                                   [&] (const Device* pDev)
                                   { return pDev->get_path() == path; });
        if (result == std::end(Devices))
            return false;
        else {
            def = *result;
            return true;
        }

    }

    /// get system ticks
    virtual uint64_t getSystemTicks() { return 0L; };

    /// get tick frequency
    virtual uint64_t getSystemTickFrequency() { return 0L; };


    // initialize the printf buffer
    virtual void initPrintfBuffer() {};

    // flush the device printf buffer
    virtual void flushPrintfBuffer() {};

    // get the locked printf buffer VA
    virtual void* getPrintfBufferPointerVA() { return nullptr; };
};

Context *getContext();

static utils::Debug CSL_DBG;
};

/* TODO: veed to discuss for the following interface */
// extern CSI* CreateDeviceCSI(uint32_t device_num=0);
