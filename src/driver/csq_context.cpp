#include "utils/lang/lang.h"
#include "inc/csq_device.h"
#include "inc/csq_context.h"
// #include "inc/csq.h"
#include "inc/csq_cmd.h"

namespace csq {

class context;

// static DlaContext ctx;
static DlaContext *ctx {nullptr};

Context* getContext() {
    // return dynamic_cast<Context*>(&ctx);

    if (ctx == nullptr)
    {
        ctx = new DlaContext();
    }
    return dynamic_cast<Context*>(ctx);
}

/// Determines if the given agent is of type HSA_DEVICE_TYPE_GPU
/// If so, cache to input data
status_t DlaContext::find_gpu(device_t agent, void *data) {
    // status_t status;
    hsa_device_type_t device_type;
    std::vector<device_t>* pAgents = nullptr;

    if (data == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    } else {
        pAgents = static_cast<std::vector<device_t>*>(data);
    }

    status_t stat = hsa_device_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
    if (stat != SUCCESS) {
        return stat;
    }

    if (device_type == HSA_DEVICE_TYPE_GPU)  {
        pAgents->push_back(agent);
    }

    return SUCCESS;
}



status_t DlaContext::find_host(device_t agent, void* data) {
    status_t status;
    hsa_device_type_t device_type;
    if(data == nullptr)
        return ERROR_INVALID_ARGUMENT;
    status = hsa_device_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
    STATUS_CHECK(status, __LINE__);

    if(HSA_DEVICE_TYPE_CPU == device_type) {
        *(device_t*)data = agent;
        return HSA_STATUS_INFO_BREAK;
    }
    return SUCCESS;
}


DlaContext::DlaContext()  :
    Context(), signalPool(), signalPoolFlag(), signalCursor(0), signalPoolMutex()     {
    host.handle = (uint64_t)-1;

    ReadHccEnv();

    status_t status;
    status = hsa_init();
    if (status != SUCCESS)
      return;

    // Iterate over the agents to find out gpu device
    status = hsa_iterate_agents(&DlaContext::find_gpu, &agents);

    // Iterate over agents to find out the first cpu device as host
    status = hsa_iterate_agents(&DlaContext::find_host, &host);
    STATUS_CHECK(status, __LINE__);

    // The Devices vector is not empty here since CPU devices have
    // been added to this vector already.  This provides the index
    // to first GPU device that will be added to Devices vector
    int first_gpu_index = Devices.size();

    //int first_gpu_index = Devices.size();

    // device_t agent = agents[0];
    // Devices[0] = new DlaDevice(agent, host, 0);

        Devices.resize(Devices.size() + agents.size());
        for (uint32_t i = 0; i < agents.size(); ++i) {
            device_t agent = agents[i];
            int gpu_index = first_gpu_index + i;
            Devices[first_gpu_index + i] = new DlaDevice(agent, host, i);
            agentToDeviceMap_.insert(std::pair<uint64_t, DlaDevice*>(agent.handle, (DlaDevice*)Devices[gpu_index]));
        }
        def = Devices[first_gpu_index + HCC_DEFAULT_GPU];


    // def = Devices[0];

    signalPoolMutex.lock();

    // pre-allocate signals
    // DBOUT(DB_SIG,  " pre-allocate " << HCC_SIGNAL_POOL_SIZE << " signals\n");
    for (int i = 0; i < HCC_SIGNAL_POOL_SIZE; ++i) {
      signal_t signal;
      status = hsa_signal_create(1, 0, NULL, &signal);
      STATUS_CHECK(status, __LINE__);
      signalPool.push_back(signal);
      signalPoolFlag.push_back(false);
    }

    signalPoolMutex.unlock();

    init_success = true;
};

DlaContext::~DlaContext() {
    // status_t status = SUCCESS;
    // DBOUT(DB_INIT, "HSAContext::~HSAContext() in\n");

    if (!init_success)
      return;
    // TODO assert need fix
    // destroy all KalmarDevices associated with this context
    for (auto dev : Devices)
        delete dev;
    Devices.clear();
    def = nullptr;

    // status = hsa_shut_down();
    hsa_shut_down();
};

// Global free function to read HCC_ENV vars.  Really this should be called once per process not once-per-event.
// Global so HCC clients or debuggers can force a re-read of the environment variables.
void DlaContext::ReadHccEnv()
{
/*
    GET_ENV_INT(HCC_PRINT_ENV, "Print values of HCC environment variables");

   // 0x1=pre-serialize, 0x2=post-serialize , 0x3= pre- and post- serialize.
   // HCC_SERIALIZE_KERNEL serializes PFE, GL, and dispatch_hsa_kernel calls.
   // HCC_SERIALIZE_COPY serializes av::copy_async operations.  (array_view copies are not currently impacted))
    GET_ENV_INT(HCC_SERIALIZE_KERNEL,
                 "0x1=pre-serialize before each kernel launch, 0x2=post-serialize after each kernel launch, 0x3=both");
    GET_ENV_INT(HCC_SERIALIZE_COPY,
                 "0x1=pre-serialize before each data copy, 0x2=post-serialize after each data copy, 0x3=both");

    GET_ENV_INT(HCC_FORCE_COMPLETION_FUTURE, "Force all kernel commands to allocate a completion signal.");


    GET_ENV_INT(HCC_DB, "Enable HCC trace debug");
    GET_ENV_INT(HCC_DB_SYMBOL_FORMAT, "Select format of symbol (kernel) name used in debug.  0=short,1=mangled,1=demangled.  Bit 0x10 removes arguments.");

    GET_ENV_INT(HCC_OPT_FLUSH, "Perform system-scope acquire/release only at CPU sync boundaries (rather than after each kernel)");
    GET_ENV_INT(HCC_FORCE_CROSS_QUEUE_FLUSH, "create_blocking_marker will force need for sys acquire (0x1) and release (0x2) queue where the marker is created. 0x3 sets need for both flags.");
    GET_ENV_INT(HCC_MAX_QUEUES, "Set max number of HSA queues this process will use.  accelerator_views will share the allotted queues and steal from each other as necessary");

    GET_ENV_INT(HCC_SIGNAL_POOL_SIZE, "Number of pre-allocated HSA signals.  Signals are precious resource so manage carefully");

    GET_ENV_INT(HCC_UNPINNED_COPY_MODE, "Select algorithm for unpinned copies. 0=ChooseBest(see thresholds), 1=PinInPlace, 2=StagingBuffer, 3=Memcpy");

    GET_ENV_INT(HCC_CHECK_COPY, "Check dst == src after each copy operation.  Only works on large-bar systems.");


    // Select thresholds to use for unpinned copies
    GET_ENV_INT (HCC_H2D_STAGING_THRESHOLD,    "Min size (in KB) to use staging buffer algorithm for H2D copy if ChooseBest algorithm selected");
    GET_ENV_INT (HCC_H2D_PININPLACE_THRESHOLD, "Min size (in KB) to use pin-in-place algorithm for H2D copy if ChooseBest algorithm selected");
    GET_ENV_INT (HCC_D2H_PININPLACE_THRESHOLD, "Min size (in KB) to use pin-in-place for D2H copy if ChooseBest algorithm selected");

    GET_ENV_INT (HCC_STAGING_BUFFER_SIZE, "Unpinned copy engine staging buffer size in KB");

    // Change the default GPU
    GET_ENV_INT (HCC_DEFAULT_GPU, "Change the default GPU (Default is device 0)");

    // Enable printf support
    GET_ENV_INT (HCC_ENABLE_PRINTF, "Enable hc::printf");

    GET_ENV_INT    (HCC_PROFILE,         "Enable HCC kernel and data profiling.  1=summary, 2=trace");
    GET_ENV_INT    (HCC_PROFILE_VERBOSE, "Bitmark to control profile verbosity and format. 0x1=default, 0x2=show begin/end, 0x4=show barrier");
    GET_ENV_STRING (HCC_PROFILE_FILE,    "Set file name for HCC_PROFILE mode.  Default=stderr");
*/
/*
    if (HCC_PROFILE) {
        if (HCC_PROFILE_FILE==nullptr || !strcmp(HCC_PROFILE_FILE, "stderr")) {
            ctx.hccProfileStream = &std::cerr;
        } else if (!strcmp(HCC_PROFILE_FILE, "stdout")) {
            ctx.hccProfileStream = &std::cout;
        } else {
            ctx.hccProfileFile.open(HCC_PROFILE_FILE, std::ios::out);
            assert (!ctx.hccProfileFile.fail());

            ctx.hccProfileStream = &ctx.hccProfileFile;
        }
    }
    */
};




void DlaContext::releaseSignal(signal_t signal, int signalIndex) {

    if (signal.handle) {

        // DBOUT(DB_SIG, "  releaseSignal: 0x" << std::hex << signal.handle << std::dec << " and restored value to 1\n");
        // status_t status = SUCCESS;
        signalPoolMutex.lock();

        // restore signal to the initial value 1
        hsa_signal_store_screlease(signal, 1);

        // mark the signal pointed by signalIndex as available
        signalPoolFlag[signalIndex] = false;

        signalPoolMutex.unlock();
    }
}

std::pair<signal_t, int> DlaContext::getSignal() {
    signal_t ret;

    signalPoolMutex.lock();
    unsigned int cursor = signalCursor;

    if (signalPoolFlag[cursor] == false) {
        // the cursor is valid, use it
        ret = signalPool[cursor];

        // set the signal as used
        signalPoolFlag[cursor] = true;

        // simply move the cursor to the next index
        ++signalCursor;
        if (signalCursor == signalPool.size()) signalCursor = 0;
    } else {
        // the cursor is not valid, sequentially find the next available slot
        bool found = false;
        unsigned int startingCursor = cursor;
        do {
            ++cursor;
            if (cursor == signalPool.size()) cursor = 0;

            if (signalPoolFlag[cursor] == false) {
                // the cursor is valid, use it
                ret = signalPool[cursor];

                // set the signal as used
                signalPoolFlag[cursor] = true;

                // simply move the cursor to the next index
                signalCursor = cursor + 1;
                if (signalCursor == signalPool.size()) signalCursor = 0;

                // break from the loop
                found = true;
                break;
            }
        } while(cursor != startingCursor); // ensure we at most scan the vector once

        if (found == false) {
            status_t status = SUCCESS;

            // increase signal pool on demand by HCC_SIGNAL_POOL_SIZE

            // keep track of the size of signal pool before increasing it
            unsigned int oldSignalPoolSize = signalPool.size();
            unsigned int oldSignalPoolFlagSize = signalPoolFlag.size();
            assert(oldSignalPoolSize == oldSignalPoolFlagSize);

            // DBOUTL(DB_RESOURCE, "Growing signal pool from " << signalPool.size() << " to " << signalPool.size() + HCC_SIGNAL_POOL_SIZE);

            // increase signal pool on demand for another HCC_SIGNAL_POOL_SIZE
            for (int i = 0; i < HCC_SIGNAL_POOL_SIZE; ++i) {
                signal_t signal;
                status = hsa_signal_create(1, 0, NULL, &signal);
                STATUS_CHECK(status, __LINE__);
                signalPool.push_back(signal);
                signalPoolFlag.push_back(false);
            }

            // DBOUT(DB_SIG,  "grew signal pool to size=" << signalPool.size() << "\n");

            assert(signalPool.size() == oldSignalPoolSize + HCC_SIGNAL_POOL_SIZE);
            assert(signalPoolFlag.size() == oldSignalPoolFlagSize + HCC_SIGNAL_POOL_SIZE);

            // set return values, after the pool has been increased

            // use the first item in the newly allocated pool
            cursor = oldSignalPoolSize;

            // access the new item through the newly assigned cursor
            ret = signalPool[cursor];

            // mark the item as used
            signalPoolFlag[cursor] = true;

            // simply move the cursor to the next index
            signalCursor = cursor + 1;
            if (signalCursor == signalPool.size()) signalCursor = 0;

            found = true;
        }
    }

    signalPoolMutex.unlock();
    return std::make_pair(ret, cursor);
}



};
