#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include "inc/pps.h"
#include "inc/csq.h"

namespace csq {
class DlaDevice;

class DlaContext final : public Context
{
public:
    std::map<uint64_t, DlaDevice *> agentToDeviceMap_;
private:
    /// memory pool for signals
    std::vector<signal_t> signalPool;
    std::vector<bool> signalPoolFlag;
    unsigned int signalCursor;
    std::mutex signalPoolMutex;
    /* TODO: Modify properly when supporing multi-gpu.
    When using memory pool api, each agent will only report memory pool
    which is attached with the agent itself physically, eg, GPU won't
    report system memory pool anymore. In order to change as little
    as possbile, will choose the first CPU as default host and hack the
    HSADevice class to assign it the host memory pool to GPU agent.
    */
    device_t host;

    // GPU devices
    std::vector<device_t> agents;

public:
    std::ofstream hccProfileFile; // if using a file open it here
    std::ostream *hccProfileStream = nullptr; // point at file or default stream

    /// Determines if the given agent is of type HSA_DEVICE_TYPE_GPU
    /// If so, cache to input data
    static status_t find_gpu(device_t agent, void *data) ;
    // static status_t find_host(device_t agent, void* data) ;

    static status_t find_host(device_t agent, void* data) ;

public:
    void ReadHccEnv() ;
    std::ostream &getHccProfileStream() const { return *hccProfileStream; };
    void releaseSignal(signal_t signal, int signalIndex) ;

    std::pair<signal_t, int> getSignal() ;

    uint64_t getSystemTicks() override {
        // get system tick
        uint64_t timestamp = 0L;
        hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP, &timestamp);
        return timestamp;
    }

    uint64_t getSystemTickFrequency() override {
        // get system tick frequency
        uint64_t timestamp_frequency_hz = 0L;
        hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &timestamp_frequency_hz);
        return timestamp_frequency_hz;
    }


    // DlaContext();
    DlaContext();
    ~DlaContext();
};

} // namespace csq
