#pragma once

#include <assert.h>
#include <iostream>
#include <map>
#include <set>
#include "util/utils.h"
// #include "utils/lang/types.h"
/*
#define INVALID_MEM_OBJ(memobj) \
    memobj.handle == 0


enum {
    ALLOC_MEM_ONLY_DEV   = 1 << 0,
    ALLOC_MEM_ALLOC_SYS  = 1 << 1,
    ALLOC_MEM_WITH_SYS   = 1 << 2
};

typedef uint64_t MemPhyAddr;
typedef uint8_t* MemVirAddr;
typedef uint64_t MemHandle;

struct MemObj
{
    MemHandle    handle;      // Hal Mem handler
    MemPhyAddr   dev_addr;    // device address
    MemVirAddr   sys_addr;    // system virtual address, TODO MemVirAddr is not good name
    uint64_t     size;        // mem size

    MemHandle GetMemHandle(void)
    {
        return handle;
    }
};
*/

struct AddrRange {
    void*   start;
    size_t  size;
};



struct AddrRangeCmp
{
    bool operator() (const AddrRange& lhs, const AddrRange& rhs)
    {
        if ((lhs.start + lhs.size) < rhs.start) {
            return true;
        }
        return false;
    }
};

class Allocator {
    public:
    struct Node;
    /*
    struct Cmp {
        bool operator() (void* const &lhs, void* const &rhs) {
            if (lhs <= rhs) {
                return true;
            } else return false;
        }
    };
    typedef std::map<void*, Node, Cmp> memory_t;
    */
    typedef std::map<void*, Node> memory_t;
    typedef memory_t::iterator iterator_t;

    struct Node {
        size_t len;
        iterator_t next;
        iterator_t prior;
    };
    Allocator(const Allocator& rhs) = delete;
    Allocator& operator=(const Allocator& rhs) = delete;

    void* const pool;
    const size_t length;

    size_t total_free;
    memory_t memory;
    std::set<void*> high;

    __forceinline bool isfree(const Node& node) const { return node.next != memory.begin(); }
    __forceinline bool islastfree(const Node& node) const { return node.next == memory.end(); }
    __forceinline bool isfirstfree(const Node& node) const { return node.prior == memory.end(); }
    __forceinline void setlastfree(Node& node) { node.next = memory.end(); }
    __forceinline void setfirstfree(Node& node) { node.prior = memory.end(); }
    __forceinline void setused(Node& node) { node.next = memory.begin(); }

    __forceinline iterator_t firstfree() { return memory.begin()->second.next; }
    __forceinline iterator_t lastfree() { return memory.rbegin()->second.prior; }
    void insertafter(iterator_t place, iterator_t node);
    void remove(iterator_t node);
    iterator_t merge(iterator_t low, iterator_t high);

 public:
    Allocator() : pool(nullptr), length(0), total_free(0) {}
    Allocator(void* base, size_t length)
        : pool(base), length(length), total_free(length) {
        assert(pool != nullptr && "Invalid base address.");
        assert(pool != (void*)0xFFFFFFFFFFFFFFFFull && "Invalid base address.");
        assert((char*)pool + length != (char*)0xFFFFFFFFFFFFFFFFull && "Invalid pool bounds.");
  
        Node& start = memory[0];
        Node& node = memory[pool];
        Node& end = memory[(void*)0xFFFFFFFFFFFFFFFFull];
  
        start.len = 0;
        start.next = memory.find(pool);
        setfirstfree(start);
  
        node.len = length;
        node.prior = memory.begin();
        node.next = --memory.end();
  
        end.len = 0;
        end.prior = start.next;
        setlastfree(end);
  
        high.insert((void*)0xFFFFFFFFFFFFFFFFull);
    }

    void* alloc(void* address, size_t bytes, bool fix_address = false);
    void* alloc_high(size_t bytes);
    bool free(void* ptr, size_t bytes);

    void* base() const { return pool; }
    size_t size() const { return length; }
    size_t remaining() const { return total_free; }
    void* high_split() const { return *high.begin(); }
};

#if 0
struct MemAddrRange
{
	void *start;
	void *userptr;
	uint64_t userptr_size;
	uint64_t size; /* size allocated on GPU. When the user requests a random
			* size, Thunk aligns it to page size and allocates this
			* aligned size on GPU
			*/

    MemAddrRange(void* start, uint64_t size, void* userptr, uint64_t userptr_size):
        start(start), size(size), userptr(userptr), userptr_size(userptr_size) {}

    void* GetStart(void) const {
        return start;
    }
    void* GetEnd(void) const {
        return start + size;
    }

    void* GetUserStart(void) const {
        return userptr;
    }
    void* GetUserEnd(void) const {
        return userptr + userptr_size;
    }

    bool Contain(const MemAddrRange& sub)
    {
        // FIXME: only compare VA for now
        // FIXME: only compare VA less case to make test pass for now
        //        need to compare VA in range case
        //if ((VA <= sub.VA) && 
        //    ((VA + Bytes) >= (sub.VA + sub.Bytes)))
        if (start <= sub.start)
        {
            //std::cout << "found range" << std::endl;
            return true;
        }
        return false;
    }
    friend std::ostream& operator<< (std::ostream &stream, const MemAddrRange& range);
};

std::ostream& operator<< (std::ostream &stream, const MemAddrRange& range);

struct MemAddrRangeCmp
{
    bool operator() (const MemAddrRange& lhs, const MemAddrRange& rhs)
    {
        if (lhs.GetEnd() <= rhs.GetStart())
        {
            return true;
        }
        return false;
    }
};

struct MemAddrUserPtrCmp
{
    bool operator() (const MemAddrRange& lhs, const MemAddrRange& rhs)
    {
        if (lhs.GetUserEnd() <= rhs.GetUserStart())
        {
            return true;
        }
        return false;
    }
};

#endif


class MemObj
{
    public:
	void *start;
	void *userptr;
	uint64_t userptr_size;
	uint64_t size; /* size allocated on GPU. When the user requests a random*/
    // MemAddrRange address_range;
    // MemAddrRange usrptr_range;
    // using MemAddrRange::start;
    // using MemAddrRange::size;
    // using MemAddrRange::userptr;
    // using MemAddrRange::userptr_size;
    MemObj(void *start, size_t size, uint64_t handle, uint32_t flags, void* userptr = nullptr)
            : start(start)
            , size(size)
            , userptr(userptr)
            , userptr_size(0)
            , handle(handle)
		    , registered_device_id_array_size(0)
		    , mapped_device_id_array_size(0)
		    , registered_device_id_array(nullptr)
		    , mapped_device_id_array(nullptr)
		    , registered_node_id_array(nullptr)
		    , mapped_node_id_array(nullptr)
		    , registration_count(0)
		    , mapping_count(0)
		    , flags(flags)
		    , metadata(nullptr)
		    , user_data(nullptr)
		    , is_imported_kfd_bo(false)
    {
        if (userptr != nullptr ) { userptr_size = size;}
    }
        ~MemObj() {
            if (registered_device_id_array) free(registered_device_id_array);
            if (mapped_device_id_array) free(mapped_device_id_array);
            if (metadata) free(metadata);
            if (registered_node_id_array) free(registered_node_id_array);
            if (mapped_node_id_array) free(mapped_node_id_array);
    }

    AddrRange range() {
        return {start, size};
    }

    AddrRange userptr_range() {
        return {userptr, userptr_size};
    }

    void remove_device_ids_from_mapped_array(
		uint32_t *ids_array, uint32_t ids_array_size); 

    void add_device_ids_to_mapped_array(
		uint32_t *ids_array, uint32_t ids_array_size);

    uint64_t handle;
	uint32_t node_id;
	uint32_t flags; /* memory allocation flags */
	/* Registered nodes to map on SVM mGPU */
	uint32_t registered_device_id_array_size;
	uint32_t registration_count; /* the same memory region can be registered multiple times */
    uint32_t *registered_device_id_array;
    uint32_t *registered_node_id_array;

	/* Nodes that mapped already */
	uint32_t mapped_device_id_array_size;
	uint32_t mapping_count;
    uint32_t *mapped_device_id_array;
    uint32_t *mapped_node_id_array;
	/* Metadata of imported graphics buffers */
	void *metadata;
	/* User data associated with the memory */
	void *user_data;
	/* Flag to indicate imported KFD buffer */
	bool is_imported_kfd_bo;

};

#define container_of(ptr, type, member) ({			\
		char *__mptr = (char *)(ptr);			\
		((type *)(__mptr - offsetof(type, member))); })


class MemObjPool {
    public:

    std::map<AddrRange, MemObj*, AddrRangeCmp> address_set;
    std::map<AddrRange, MemObj*, AddrRangeCmp> userptr_set;
    void* cur_address;


    void insert(MemObj* obj) {
        address_set.insert(std::make_pair(obj->range(), obj));
	    if (obj->userptr) userptr_set.insert(std::make_pair(obj->userptr_range(), obj));
    }

    void erase(MemObj* obj) {
        auto itr = address_set.find(obj->range());
        if (itr != address_set.end()) {
            address_set.erase(itr);
        }
	    if (obj->userptr) {
            auto itr = userptr_set.find(obj->userptr_range());
            if (itr != userptr_set.end())
                userptr_set.erase(itr);
        }
    }

    MemObj* find(AddrRange range, bool is_userptr) {
        if (is_userptr) {
            auto itr = userptr_set.find(range);
            if (itr != userptr_set.end())
                return itr->second;
                // return container_of(*itr, MemObj, userptr);
        } else {
            auto itr = address_set.find(range);
            if (itr != address_set.end())
                return itr->second;
                // return container_of(*itr, MemObj, start);
        }
        return nullptr;
    }

    MemObj* find(void* address, bool is_userptr) {
        AddrRange range {address, 0};
        return find(range, is_userptr);
    }

    using Iterator = std::map<AddrRange, MemObj*>::iterator;
    class PoolIterator {
        MemObjPool & pool;
        Iterator itr;
        public:
        PoolIterator(MemObjPool &pool, Iterator itr)
            : pool(pool)
            , itr(itr)
        {}
        MemObj* operator*() {
            return pool.find(itr->first, false);
        }

        PoolIterator& operator++() {
            itr++;
            return *this;
        }

        PoolIterator& operator++(int) {
            PoolIterator clone(*this);
            ++itr;
            return clone;
        }

        bool operator!=(PoolIterator rhs) {
            return (itr != rhs.itr);
        }
    };


    PoolIterator begin() {
        auto itr = address_set.begin();
        return PoolIterator(*this, itr);
    }

    PoolIterator end() {
        auto itr = address_set.begin();
        return PoolIterator(*this, itr);
    }
};



