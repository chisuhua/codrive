#pragma once
#include "StreamType.h"
#include "util/flag.h"
#include "util/timer.h"
#include <atomic>
#include <functional>
// #include "Signal.h"
// #include "InterruptSignal.h"
// typedef device_status_t (*CreateQueue)()
//
//
namespace core {

class Agent;


// template file
class Runtime {
public:
    static status_t Acquire();
    static status_t Release();
    static Runtime* runtime_singleton_;
    __forceinline static Runtime& getInstance();


    static bool IsOpen();

    status_t AllocateMemory(size_t size, void** address);

    status_t FreeMemory(void* ptr);

    status_t CopyMemory(void* dst, const void* src, size_t size);

    std::function<void*(size_t, size_t)> system_allocator_;
    std::function<void(void*)> system_deallocator_;

    const Flag& flag() const { return flag_; }

    Runtime();
    Runtime(const Runtime&);
    Runtime& operator=(const Runtime&);
    ~Runtime() { }


    // SharedSignalPool* GetSignalPool() { return signal_pool_; };
    // EventPool* GetEventPool() { return event_pool_; };
    // StreamPool* GetStreamPool() { return stream_pool_; };
    // Device* GetDevice() { return device_; };

    uint64_t sys_clock_freq_;
    std::atomic<uint32_t> ref_count_;
    Flag flag_;

    // SharedSignalPool* signal_pool_;

    // Pools KFD Events for InterruptSignal
    // EventPool* event_pool_;

    // StreamPool* stream_pool_;

    // Device* device_;

    status_t Load();
    void Unload();

    const timer::fast_clock::duration GetTimeout(double timeout)
    {
        uint64_t freq { 0 };
        /* FIXME
        HSA::system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &freq);
        */
        return timer::duration_from_seconds<timer::fast_clock::duration>(double(timeout) / double(freq));
    };

    const timer::fast_clock::time_point GetTimeNow()
    {
        return timer::fast_clock::now();
    };

    void Sleep(uint32_t milisecond)
    {
        os::uSleep(20);
    }

    // void DestroyEvent(HsaEvent* evt);

    bool use_interrupt_wait_;
};

__forceinline Runtime& Runtime::getInstance()
{
    static Runtime obj;
    return obj;
}

}

#define g_runtime core::IRuntime::getInstance()

