#pragma once
#include <vector>
#include <future>
#include <cstring>
#include <ostream>
#include <iomanip>
#include "utils/lang/lang.h"
#include "utils/lang/error.h"
#include "utils/lang/string.h"

#include "inc/csq.h"
#include "inc/csq_queue.h"
#include "inc/csq_device.h"
// #include "hc_am.h"
// #include "hc.h"

// using namespace csl;
namespace csq {

class AmPointerInfo;
class completion_future;

// class AsyncOp;
// class Queue;

#define HSA_BARRIER_DEP_SIGNAL_CNT (5)


// synchronization for copy commands in the same stream, regardless of command type.
// Add a signal dependencies between async copies -
// so completion signal from prev command used as input dep to next.
// If FORCE_SIGNAL_DEP_BETWEEN_COPIES=0 then data copies of the same kind (H2H, H2D, D2H, D2D)
// are assumed to be implicitly ordered.
// ROCR 1.2 runtime implementation currently provides this guarantee when using SDMA queues and compute shaders.
#define FORCE_SIGNAL_DEP_BETWEEN_COPIES (0)

#define CASE_STRING(X)  case X: case_string = #X ;break;


extern const char* getHcCommandKindString( hcCommandKind k) ;

// Stores the device and queue for op coordinate:
struct CmdOpCoord
{
    CmdOpCoord(DlaQueue *queue);

    int         _deviceId;
    uint64_t    _queueId;
};

// Base class for the other Cmd ops:
class Cmd : public AsyncOp {
public:
    Cmd(Queue *queue, hcCommandKind commandKind) ;

    const CmdOpCoord opCoord() const { return _opCoord; };
    int asyncOpsIndex() const { return _asyncOpsIndex; };

    void asyncOpsIndex(int asyncOpsIndex) { _asyncOpsIndex = asyncOpsIndex; };

    void* getNativeHandle() override { return &_signal; }

    virtual bool barrierNextSyncNeedsSysRelease() const { return 0; };
    virtual bool barrierNextKernelNeedsSysAcquire() const { return 0; };

    DlaQueue *hsaQueue() const;
    // Queue *hsaQueue() const;
    bool isReady() override;
protected:
    uint64_t     apiStartTick;
    CmdOpCoord   _opCoord;
    int          _asyncOpsIndex;

    signal_t _signal;
    int          _signalIndex;

    device_t  _agent;
};
std::ostream& operator<<(std::ostream& os, const Cmd & op);


class CmdCopy : public Cmd {
private:
    bool isSubmitted;
    bool isAsync;          // copy was performed asynchronously
    bool isSingleStepCopy;; // copy was performed on fast-path via a single call to the CMD copy routine
    bool isPeerToPeer;
    uint64_t apiStartTick;

    std::shared_future<void>* future;


    // If copy is dependent on another operation, record reference here.
    // keep a reference which prevents those ops from being deleted until this op is deleted.
    std::shared_ptr<Cmd> depAsyncOp;

    const Device* copyDevice;  // Which device did the copy.

    hsa_wait_state_t waitMode;

    // source pointer
    const void* src;


    // destination pointer
    void* dst;

    // bytes to be copied
    size_t sizeBytes;


public:
    std::shared_future<void>* getFuture() override { return future; }
    const Device* getCopyDevice() const { return copyDevice; } ;  // Which device did the copy.


    void setWaitMode(hcWaitMode mode) override {
        switch (mode) {
            case hcWaitModeBlocked:
                waitMode = HSA_WAIT_STATE_BLOCKED;
            break;
            case hcWaitModeActive:
                waitMode = HSA_WAIT_STATE_ACTIVE;
            break;
        }
    }


    std::string getCopyCommandString()
    {
        std::string s;
        switch (getCommandKind()) {
            case hcMemcpyHostToHost:
                s += "HostToHost";
                break;
            case hcMemcpyHostToDevice:
                s += "HostToDevice";
                break;
            case hcMemcpyDeviceToHost:
                s += "DeviceToHost";
                break;
            case hcMemcpyDeviceToDevice:
                if (isPeerToPeer) {
                    s += "PeerToPeer";
                } else {
                    s += "DeviceToDevice";
                }
                break;
            default:
                s += "UnknownCopy";
                break;
        };
        s += isAsync ? "_async" : "_sync";
        s += isSingleStepCopy ? "_fast" : "_slow";

        return s;

    }


    // Copy mode will be set later on.
    // CMD signals would be waited in HSA_WAIT_STATE_ACTIVE by default for CmdCopy instances
    CmdCopy(Queue *queue, const void* src_, void* dst_, size_t sizeBytes_);


    ~CmdCopy() {
        if (isSubmitted) {
            status_t status = SUCCESS;
            status = waitComplete();
            STATUS_CHECK(status, __LINE__);
        }
        dispose();
    }

    status_t enqueueAsyncCopyCommand(const Device *copyDevice, const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo);

    // wait for the async copy to complete
    status_t waitComplete();

    void dispose();

    uint64_t getTimestampFrequency() override {
        // get system tick frequency
        uint64_t timestamp_frequency_hz = 0L;
        hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &timestamp_frequency_hz);
        return timestamp_frequency_hz;
    }

    uint64_t getBeginTimestamp() override;

    uint64_t getEndTimestamp() override;

    // synchronous version of copy
    void syncCopy();
    void syncCopyExt(hcCommandKind copyDir,
                     const AmPointerInfo &srcPtrInfo, const AmPointerInfo &dstPtrInfo,
                     const DlaDevice *copyDevice, bool forceUnpinnedCopy);


private:
  status_t hcc_memory_async_copy(hcCommandKind copyKind, const Device *copyDevice,
                                      const AmPointerInfo &dstPtrInfo, const AmPointerInfo &srcPtrInfo,
                                      size_t sizeBytes, int depSignalCnt, const signal_t *depSignals,
                                      signal_t completion_signal);

}; // end of CmdCopy

class CmdBarrier : public Cmd {
private:
    bool isDispatched;
    std::shared_future<void>* future;

    memory_scope _acquire_scope;

    // capture the state of _nextSyncNeedsSysRelease and _nextKernelNeedsSysAcquire after
    // the barrier is issued.  Cross-queue synchronziation commands which synchronize
    // with the barrier (create_blocking_marker) then can transer the correct "needs" flags.
    bool    _barrierNextSyncNeedsSysRelease;
    bool    _barrierNextKernelNeedsSysAcquire;

    hsa_wait_state_t waitMode;
    // prior dependencies
    // maximum up to 5 prior dependencies could be associated with one
    // CmdBarrier instance
    int depCount;

public:
    uint16_t  header;  // stores header of AQL packet.  Preserve so we can see flushes associated with this barrier.

    // array of all operations that this op depends on.
    // This array keeps a reference which prevents those ops from being deleted until this op is deleted.
    std::shared_ptr<Cmd> depAsyncOps [HSA_BARRIER_DEP_SIGNAL_CNT];

public:
    std::shared_future<void>* getFuture() override { return future; }
    void acquire_scope(memory_scope acquireScope) { _acquire_scope = acquireScope;};

    bool barrierNextSyncNeedsSysRelease() const override { return _barrierNextSyncNeedsSysRelease; };
    bool barrierNextKernelNeedsSysAcquire() const override { return _barrierNextKernelNeedsSysAcquire; };


    void setWaitMode(hcWaitMode mode) override {
        switch (mode) {
            case hcWaitModeBlocked:
                waitMode = HSA_WAIT_STATE_BLOCKED;
            break;
            case hcWaitModeActive:
                waitMode = HSA_WAIT_STATE_ACTIVE;
            break;
        }
    }


    // constructor with 1 prior dependency
    CmdBarrier(Queue *queue, std::shared_ptr <AsyncOp> dependent_op) :
        Cmd(queue, hcCommandMarker),
        isDispatched(false),
        future(nullptr),
        _acquire_scope(no_scope),
        _barrierNextSyncNeedsSysRelease(false),
        _barrierNextKernelNeedsSysAcquire(false),
        waitMode(HSA_WAIT_STATE_BLOCKED)
    {

        if (dependent_op != nullptr) {
            assert (dependent_op->getCommandKind() == hcCommandMarker);

            depAsyncOps[0] = std::static_pointer_cast<Cmd> (dependent_op);
            depCount = 1;
        } else {
            depCount = 0;
        }
    }

    // constructor with at most 5 prior dependencies
    CmdBarrier(Queue *queue, int count, std::shared_ptr <AsyncOp> *dependent_op_array) :
        Cmd(queue, hcCommandMarker),
        isDispatched(false),
        future(nullptr),
        _acquire_scope(no_scope),
        _barrierNextSyncNeedsSysRelease(false),
        _barrierNextKernelNeedsSysAcquire(false),
        waitMode(HSA_WAIT_STATE_BLOCKED),
        depCount(0)
    {
        if ((count >= 0) && (count <= 5)) {
            for (int i = 0; i < count; ++i) {
                if (dependent_op_array[i]) {
                    // squish null ops
                    depAsyncOps[depCount] = std::static_pointer_cast<Cmd> (dependent_op_array[i]);
                    depCount++;
                }
            }
        } else {
            // throw an exception
            throw utils::Error(utils::fmt("Incorrect number of dependent signals passed to CmdBarrier constructor, %d", count));
        }
    }

    ~CmdBarrier() {
        if (isDispatched) {
            status_t status = SUCCESS;
            status = waitComplete();
            STATUS_CHECK(status, __LINE__);
        }
        dispose();
    }


    status_t enqueueAsync(memory_scope memory_scope);

    // wait for the barrier to complete
    status_t waitComplete();

    void dispose();

    uint64_t getTimestampFrequency() override {
        // get system tick frequency
        uint64_t timestamp_frequency_hz = 0L;
        hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &timestamp_frequency_hz);
        return timestamp_frequency_hz;
    }

    uint64_t getBeginTimestamp() override;

    uint64_t getEndTimestamp() override;

}; // end of CmdBarrier


/// modeling of HSA executable
class CslExecutable {
/*
private:
    hsa_code_object_reader_t hsaCodeObjectReader;
    hsa_executable_t hsaExecutable;
    friend class HSAKernel;
    friend class Kalmar::HSADevice;

public:
    HSAExecutable(hsa_executable_t _hsaExecutable,
                  hsa_code_object_reader_t _hsaCodeObjectReader) :
        hsaExecutable(_hsaExecutable),
        hsaCodeObjectReader(_hsaCodeObjectReader) {}

    ~HSAExecutable() {
      status_t status;

      DBOUT(DB_INIT, "HSAExecutable::~HSAExecutable\n");

      status = hsa_executable_destroy(hsaExecutable);
      STATUS_CHECK(status, __LINE__);

      status = hsa_code_object_reader_destroy(hsaCodeObjectReader);
      STATUS_CHECK(status, __LINE__);
    }
*/
};

class CslKernel {
public:
    std::string kernelName;
    std::string shortKernelName; // short handle, format selectable with HCC_DB_KERNEL_NAME
    uint64_t kernelCodeHandle;
    CslKernel(std::string &_kernelName, const std::string &x_shortKernelName,
              uint64_t _kernelCodeHandle) :
        kernelName(_kernelName),
        shortKernelName(x_shortKernelName),
        kernelCodeHandle(_kernelCodeHandle) {
        if (shortKernelName.empty()) {
            shortKernelName = "<unknown_kernel>";
        }
    }

    const std::string &getKernelName() const { return shortKernelName; }
    const std::string &getLongKernelName() const { return kernelName; }

    ~CslKernel() {};
};
/*
private:
    std::string kernelName;
    std::string shortKernelName; // short handle, format selectable with HCC_DB_KERNEL_NAME
    HSAExecutable* executable;
    uint64_t kernelCodeHandle;
    hsa_executable_symbol_t hsaExecutableSymbol;
    uint32_t static_group_segment_size;
    uint32_t private_segment_size;
    uint16_t workitem_vgpr_count;
    friend class HSADispatch;

public:
    HSAKernel(std::string &_kernelName, const std::string &x_shortKernelName, HSAExecutable* _executable,
              hsa_executable_symbol_t _hsaExecutableSymbol,
              uint64_t _kernelCodeHandle) :
        kernelName(_kernelName),
        shortKernelName(x_shortKernelName),
        executable(_executable),
        hsaExecutableSymbol(_hsaExecutableSymbol),
        kernelCodeHandle(_kernelCodeHandle) {

        if (shortKernelName.empty()) {
            shortKernelName = "<unknown_kernel>";
        }

        status_t status =
            hsa_executable_symbol_get_info(
                _hsaExecutableSymbol,
                HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE,
                &this->static_group_segment_size);
        STATUS_CHECK(status, __LINE__);

        status =
            hsa_executable_symbol_get_info(
                _hsaExecutableSymbol,
                HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE,
                &this->private_segment_size);
        STATUS_CHECK(status, __LINE__);

        workitem_vgpr_count = 0;

        hsa_ven_amd_loader_1_00_pfn_t ext_table = {nullptr};
        status = hsa_system_get_extension_table(HSA_EXTENSION_AMD_LOADER, 1, 0, &ext_table);
        STATUS_CHECK(status, __LINE__);

        if (nullptr != ext_table.hsa_ven_amd_loader_query_host_address) {
            const hcs_kernel_code_t* akc = nullptr;
            status = ext_table.hsa_ven_amd_loader_query_host_address(reinterpret_cast<const void*>(kernelCodeHandle), reinterpret_cast<const void**>(&akc));
            STATUS_CHECK(status, __LINE__);

            workitem_vgpr_count = akc->workitem_vgpr_count;
        }

        DBOUTL(DB_CODE, "Create kernel " << shortKernelName << " vpr_cnt=" << this->workitem_vgpr_count
                << " static_group_segment_size=" << this->static_group_segment_size
                << " private_segment_size=" << this->private_segment_size );

    }

    //TODO - fix this so all Kernels set the _kernelName to something sensible.
    const std::string &getKernelName() const { return shortKernelName; }
    const std::string &getLongKernelName() const { return kernelName; }

    ~HSAKernel() {
        DBOUT(DB_INIT, "HSAKernel::~HSAKernel\n");
    }
}; // end of H
*/

class CmdDispatch : public Cmd {
private:
    DlaDevice* device;

    const char *kernel_name;
    const CslKernel* kernel;

    std::vector<uint8_t> arg_vec;
    uint32_t arg_count;
    size_t prevArgVecCapacity;
    void* kernargMemory;
    int kernargMemoryIndex;


    hsa_kernel_dispatch_packet_t aql;
    bool isDispatched;
    hsa_wait_state_t waitMode;


    std::shared_future<void>* future;

public:
    std::shared_future<void>* getFuture() override { return future; }

    void setKernelName(const char *x_kernel_name) { kernel_name = x_kernel_name;};
    const char *getKernelName() { return kernel_name ? kernel_name : (kernel ? kernel->shortKernelName.c_str() : "<unknown_kernel>"); };
    const char *getLongKernelName() { return (kernel ? kernel->getLongKernelName().c_str() : "<unknown_kernel>"); };


    void setWaitMode(hcWaitMode mode) override {
        switch (mode) {
            case hcWaitModeBlocked:
                waitMode = HSA_WAIT_STATE_BLOCKED;
            break;
            case hcWaitModeActive:
                waitMode = HSA_WAIT_STATE_ACTIVE;
            break;
        }
    }


    ~CmdDispatch() {

        if (isDispatched) {
            status_t status = SUCCESS;
            status = waitComplete();
            STATUS_CHECK(status, __LINE__);
        }
        dispose();
    }

    CmdDispatch(DlaDevice* _device, Queue* _queue, CslKernel* _kernel, const hsa_kernel_dispatch_packet_t *aql=nullptr);

    status_t pushFloatArg(float f) { return pushArgPrivate(f); }
    status_t pushIntArg(int i) { return pushArgPrivate(i); }
    status_t pushBooleanArg(unsigned char z) { return pushArgPrivate(z); }
    status_t pushByteArg(char b) { return pushArgPrivate(b); }
    status_t pushLongArg(long j) { return pushArgPrivate(j); }
    status_t pushDoubleArg(double d) { return pushArgPrivate(d); }
    status_t pushShortArg(short s) { return pushArgPrivate(s); }
    status_t pushPointerArg(void *addr) { return pushArgPrivate(addr); }

    status_t clearArgs() {
        arg_count = 0;
        arg_vec.clear();
        return SUCCESS;
    }


    void overrideAcquireFenceIfNeeded();
    status_t setLaunchConfiguration(const int dims, size_t *globalDims, size_t *localDims,
                                        const int dynamicGroupSize);

    status_t dispatchKernelWaitComplete();

    status_t dispatchKernelAsyncFromOp();
    status_t dispatchKernelAsync(const void *hostKernarg, int hostKernargSize, bool allocSignal);

    // dispatch a kernel asynchronously
    status_t dispatchKernel(queue_t* lockedHsaQueue, const void *hostKernarg,
                               int hostKernargSize, bool allocSignal);

    // wait for the kernel to finish execution
    status_t waitComplete();

    void dispose();

    uint64_t getTimestampFrequency() override {
        // get system tick frequency
        uint64_t timestamp_frequency_hz = 0L;
        hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &timestamp_frequency_hz);
        return timestamp_frequency_hz;
    }

    uint64_t getBeginTimestamp() override;

    uint64_t getEndTimestamp() override;

    const hsa_kernel_dispatch_packet_t &getAql() const { return aql; };

private:
    template <typename T>
    status_t pushArgPrivate(T val) {
        /* add padding if necessary */
        // int padding_size = (arg_vec.size() % sizeof(T)) ? (sizeof(T) - (arg_vec.size() % sizeof(T))) : 0;
        uint32_t padding_size = (arg_vec.size() % sizeof(T)) ? (sizeof(T) - (arg_vec.size() % sizeof(T))) : 0;
        CSL_DBG << "KERNARG: push " << (sizeof(T) + padding_size) << " bytes into kernarg: ";

        for (size_t i = 0; i < padding_size; ++i) {
            arg_vec.push_back((uint8_t)0x00);
            CSL_DBG << "KERNARG," << std::hex << std::setw(2) << std::setfill('0') << 0x00 << " ";
        }
        uint8_t* ptr = static_cast<uint8_t*>(static_cast<void*>(&val));
        for (size_t i = 0; i < sizeof(T); ++i) {
            arg_vec.push_back(ptr[i]);
            CSL_DBG << "KERNARG, " << std::hex << std::setw(2) << std::setfill('0') << +ptr[i] << " ";
        }
        CSL_DBG << "KERNARG,\n"; // << std::endl;

        arg_count++;
        return SUCCESS;
    }

}; // end of CmdDispatch


};
