#pragma once

#include <iostream>
#include <bitset>
#include "utils/pattern/Singleton.h"
#include "../cmdio.h"
#include "grid_engine/grid_engine.h"
#include "inc/pps.h"


class CpuEngineCmdio // : public cmdio
{
public:

    /* register handling */
    virtual bool RegWrite(uint32_t index, uint32_t value);
    virtual bool RegRead(uint32_t index, uint32_t &value);
#if 0
    /* memory handling */
    virtual bool MemAlloc(uint64_t size,
                          uint32_t align,
                          uint32_t flag,
                          MemObj& mem_obj,
                          uint8_t *sys_ptr);

    virtual bool MemFree(MemHandle handle);
/*
    virtual bool MemCopy(MemHandle handle, uint64_t bytes,
                         uint8_t dir, uint8_t *host_ptr,
                         uint64_t offset);
                         */
#endif
    /* support both Block and Non-Block mode */
    virtual bool Sync();


    virtual ~CpuEngineCmdio();
    CpuEngineCmdio();

    virtual void CreateQueue(ioctl_create_queue_args &args);
#if 0
    virtual void UpdateQueue(ioctl_update_queue_args &args);
    virtual void DestroyQueue(ioctl_destroy_queue_args &args);

    virtual void CreateEvent(ioctl_create_event_args &args);
    virtual void DestroyEvent(ioctl_destroy_event_args &args);
    virtual void WaitOnEvent(ioctl_wait_events_args &args);

    virtual void GetProcessApertures(ioctl_get_process_apertures_args &args);

    virtual void AllocMemory(ioctl_alloc_memory_args &args);
    virtual void FreeMemory(ioctl_free_memory_args &args);
    virtual void MapMemory(ioctl_map_memory_to_gpu_args &args);
    virtual void UnMapMemory(ioctl_unmap_memory_from_gpu_args &args);
#endif

/*
    virtual bool GetDeviceInfo(const std::string& name, uint16_t &info)
    {
        return get_device_info<uint16_t>(name, info);
    }
    virtual bool GetDeviceInfo(const std::string& name, uint32_t &info)
    {
        return get_device_info<uint32_t>(name, info);
    }
    virtual bool GetDeviceInfo(const std::string& name, uint64_t &info)
    {
        return get_device_info<uint64_t>(name, info);
    }
    virtual bool GetDeviceInfo(const std::string& name, std::string &info)
    {
        return get_device_info<std::string>(name, info);
    }
*/

private:
    /* TODO mlvm
    uint32_t get_free_queue_num(void)
    {
        for (uint32_t index = 0; index < hal_queues_.size(); index++)
        {
            cout << "get_free_queue_num" << hal_queues_[index] << endl;
            if (hal_queues_[index] == nullptr)
            {
                return index;
            }
        }
        return INVALID_QUEUE_ID;
    }
    */
    void init_queue(uint32_t size);
    void clear_queue(void);
/*
    template <typename T>
    bool get_device_info(const std::string& name, T &info);
    bool check_mem_copy(uint64_t bytes, uint64_t offset,
                        uint8_t dir, uint8_t *host_ptr);
*/


private:
    // TODO mlvm HalDev*   hal_dev_;
    // TODO mlvm HalReg*   hal_reg_;
    std::unique_ptr<GridEngine> m_engine;
    CmdQueue* m_engine_queue;
    // TODO mlvm std::vector<HalQueue*> hal_queues_;
    /*
    uint64_t  svm_base;
    uint32_t  svm_limit;
    */
};



