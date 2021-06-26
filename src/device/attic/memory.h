#pragma once

#include <map>
#include "utils/pattern/Singleton.h"
#include "cmdio.h"
#include "mem_obj.h"
#include "inc/hsakmttypes.h"
// #include "memory/NodeList.h"
#include <new>

#define INVALID_Memory_HANDLE 0xFFFFFFFFFFFFFFFFull

// for future improve:
// 1. create various surface which contain memory object
//       the surface class have different usage and address tiling
//       the surface class have binding function to generation device cmb buffer
// 2. surface can contain one or more memory object
// 3. if hw register is point to a surface, declare such register class here too

// using namespace mem;

namespace device {

class DeviceMemoryManager;

DeviceMemoryManager*  GetMemoryManager();

class DeviceMemoryObject {
public:
    DeviceMemoryObject(DeviceMemoryManager* mm)
    :alignment_(0x100),
    manager_(mm)
    {
    }

    DeviceMemoryObject& operator = (DeviceMemoryObject const&){ return *this;}

	void Alloc(uint32_t byteSize, HSA_HEAPTYPE space = HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC, uint32_t alignment = 0x100, uint32_t** usr_ptr = 0);
#if 0
	void Flush(uint32_t byteSize = 0, uint32_t offset = 0);
	bool Fetch(uint32_t bytesize = 0, uint32_t offset = 0);
#endif
	void* GetHostPtr(uint32_t byteOffset = 0);
	void* GetDevicePtr(uint32_t byteOffset = 0);
	uint32_t GetByteSize() const;
	uint32_t GetBaseAlign() const;

	void SetMem(MemObj& mem, bool ownMem =true);
	MemObj& GetMem();

    void Destroy();

    ~DeviceMemoryObject()
    {
//      Release();
        Destroy();
    }
/*
    inline DeviceMemoryObject *AddRef(void) {
        ref_cnt++;
        return this;
    }

    inline void Release(void) {
        if (ref_cnt)
        {
            ref_cnt--;
            if (!ref_cnt)
            {
                Destroy();
            }
        }
    }
*/

private:
    MemObj mem_;
    // DeviceMemoryObject *next_, *prev_;
    // unsigned int ref_cnt;

    uint64_t byte_size_;
	bool mem_owner_;
	uint32_t alignment_;
    uint64_t aperture_base;  // it is for segement memory mangement, it is used to adjust dev_addr when it mapped to system address space
    DeviceMemoryManager*  manager_;
};


// TODO expect to used by DeviceAllocMemory/FreeMemory keep track of allocated memory inside region
// keep track of all allocated MemObj::handle
// it can be used to find out MemObj by address
class DeviceMemoryManager {
    typedef std::map<MemAddrRange, DeviceMemoryObject*, MemAddrRangeCompare> addr_map_type;
    typedef addr_map_type::value_type addr_map_value;;
public:
    DeviceMemoryManager() {};  // create a DeviceMemoryManager per csi

    void Alloc(uint32_t bytesize, HSA_HEAPTYPE space, uint32_t alignment, uint32_t** usr_ptr);

    void insert(DeviceMemoryObject* mem) {
        //std::pair<addr_map_type::iterator, bool> value = addr_map_.insert(std::make_pair(MemAddrRange(mem->GetMem()), mem));
        //std::cout << "DeviceMemoryManager::insert" << MemAddrRange(mem->GetMem()) << " " << value.second << std::endl;
        const MemObj &mem_obj = mem->GetMem();

        if (mem_obj.sys_addr != nullptr) {
            sys_addr_map_.insert(std::make_pair(MemAddrRange(
                              reinterpret_cast<uint64_t>(mem_obj.sys_addr), mem_obj.size), mem));
        }

        if (mem_obj.dev_addr != 0x0) {
            dev_addr_map_.insert(std::make_pair(MemAddrRange(mem_obj.dev_addr, mem_obj.size), mem));
        }
    };

    void free(DeviceMemoryObject* mem) {

        const MemObj &mem_obj = mem->GetMem();

        if (mem_obj.sys_addr != nullptr) {
            auto itr = sys_addr_map_.find(MemAddrRange(
                              reinterpret_cast<uint64_t>(mem_obj.sys_addr), mem_obj.size));
            if (itr != sys_addr_map_.end()) {
                sys_addr_map_.erase(itr);
            }
        }

        if (mem_obj.dev_addr != 0x0) {
            auto itr = dev_addr_map_.find(MemAddrRange(mem_obj.dev_addr, mem_obj.size));
            if (itr != dev_addr_map_.end()) {
                dev_addr_map_.erase(itr);
            }
        }
    };

    DeviceMemoryObject* find(const MemAddrRange& mem_range, bool sys_addr = true) {
        auto begin_itr = sys_addr ? sys_addr_map_.begin() : dev_addr_map_.begin();
        auto end_itr = sys_addr ? sys_addr_map_.end() : dev_addr_map_.end();
        auto itr = std::find_if(begin_itr, end_itr,
                [&](addr_map_value& node) {
                    MemAddrRange node_range = node.first;
                    return node_range.Contain(mem_range);
                });
        if(itr == end_itr) {
            return nullptr;
        }
        return itr->second;
    };

    // NodeList::addr_iter find(uint32_t address);
private:
    addr_map_type sys_addr_map_;
    addr_map_type dev_addr_map_;
    // CSI*    csi_;
    // NodeList nodelist_;
};




} // namespace device
