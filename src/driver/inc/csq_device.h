#pragma once
#include <map>
#include "inc/pps.h"
#include "inc/pps_ext.h"
#include "inc/csq.h"
#include "inc/csq_queue.h"
#include "inc/unpinned_copy_engine.h"

namespace csq {

// class DlaQueue;
class DlaKernel;
// class RocrQueue;
//-----
//Structure used to extract information from memory pool
struct pool_iterator
{
    hsa_amd_memory_pool_t _am_memory_pool;
    hsa_amd_memory_pool_t _am_host_memory_pool;
    hsa_amd_memory_pool_t _am_host_coherent_memory_pool;

    hsa_amd_memory_pool_t _kernarg_memory_pool;
    hsa_amd_memory_pool_t _finegrained_system_memory_pool;
    hsa_amd_memory_pool_t _coarsegrained_system_memory_pool;
    hsa_amd_memory_pool_t _local_memory_pool;

    bool        _found_kernarg_memory_pool;
    bool        _found_finegrained_system_memory_pool;
    bool        _found_local_memory_pool;
    bool        _found_coarsegrained_system_memory_pool;

    size_t _local_memory_pool_size;

    pool_iterator() ;
};

class Executable;

class DlaDevice final : public Device
{
public:
    DlaDevice(device_t a, hsa_agent_t host, int x_accSeqNum);
    ~DlaDevice() ;


    friend std::ostream& operator<<(std::ostream& os, const DlaQueue & hav);
private:

    std::map<std::string, DlaKernel *> programs;
    device_t agent;
    size_t max_tile_static_size;

    size_t queue_size;
    std::vector< std::weak_ptr<Queue> > queues;
    std::mutex queues_mutex; // protects access to the queues vector:

    // TODO: Do we need to maintain a different mutex for each queue priority?
    // In which case HSAQueue::dispose needs to handle locking the appropriate mutex
    //std::vector< RocrQueue *>    rocrQueues;
    std::vector< RocrQueue *>    rocrQueues[3];
    std::mutex                  rocrQueuesMutex; // protects rocrQueues

    pool_iterator ri;
    bool useCoarseGrainedRegion;
    /// memory pool for kernargs
    std::vector<void*> kernargPool;
    std::vector<bool> kernargPoolFlag;
    uint32_t kernargCursor;
    std::mutex kernargPoolMutex;

    std::wstring path;
    std::wstring description;

    /*TODO: This is the first CPU which will provide system memory pool
    We might need to modify again in multiple CPU socket scenario. Because
    we must make sure there is pyshycial link between device and host. Currently,
    agent iterate function will push back all of the dGPU on the system, which might
    not be linked directly to the first cpu node, host */
    device_t hostAgent;

    uint16_t versionMajor;
    uint16_t versionMinor;

    int      accSeqNum;     // unique accelerator seq num
    uint64_t queueSeqNums;  // used to assign queue seqnums.



    uint32_t workgroup_max_size;
    uint16_t workgroup_max_dim[3];

    std::map<std::string, Executable*> executables;

    // TODO new hcc remove it hsa_isa_t agentISA;

    // hcAgentProfile profile;


public:
    // Structures to manage unpinnned memory copies
    class UnpinnedCopyEngine      *copy_engine[2]; // one for each direction.
    UnpinnedCopyEngine::CopyMode  copy_mode;

    // Creates or steals a rocrQueue and returns it in theif->rocrQueue
    void createOrstealRocrQueue(DlaQueue *thief, queue_priority priority = priority_normal);

private:

    // NOTE: removeRocrQueue should only be called from HSAQueue::dispose
    // since there's an assumption on a specific locking sequence
    friend void DlaQueue::dispose();
    void removeRocrQueue(RocrQueue *rocrQueue);


public:

    uint32_t getWorkgroupMaxSize() {
        return workgroup_max_size;
    }

    const uint16_t* getWorkgroupMaxDim() {
        return &workgroup_max_dim[0];
    }

// Callback for hsa_amd_agent_iterate_memory_pools.
// data is of type pool_iterator,
// we save the pools we care about into this structure.
static status_t get_memory_pools(hsa_amd_memory_pool_t region, void* data)
{
    status_t status;
    hsa_amd_segment_t segment;
    bool is_system_memory;
    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment);
    if (status != SUCCESS) {
      return status;
    }

    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_ACCESSIBLE_BY_ALL, &is_system_memory);
    if (status != SUCCESS) {
      return status;
    }

    if (!is_system_memory && segment == HSA_AMD_SEGMENT_GLOBAL) {
      size_t size = 0;
      status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SIZE, &size);
      if (status != SUCCESS) {
        return status;
      }
      // DBOUT(DB_INIT, "  found memory pool of GPU local memory region=" << region.handle << ", size(MB) = " << (size/(1024*1024)) << std::endl);
      pool_iterator *ri = (pool_iterator*) (data);
      ri->_local_memory_pool = region;
      ri->_found_local_memory_pool = true;
      ri->_local_memory_pool_size = size;

      return HSA_STATUS_INFO_BREAK;
    }

    return SUCCESS;
}

static status_t get_host_pools(hsa_amd_memory_pool_t region, void* data) {
    status_t status;
    hsa_amd_segment_t segment;
    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment);
    STATUS_CHECK(status, __LINE__);

    pool_iterator *ri = (pool_iterator*) (data);

    hsa_amd_memory_pool_global_flag_t flags;
    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flags);
    STATUS_CHECK(status, __LINE__);

    size_t size = 0;
    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SIZE, &size);
    STATUS_CHECK(status, __LINE__);
    size = size/(1024*1024);

    if ((flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED) && (!ri->_found_finegrained_system_memory_pool)) {
        // DBOUT(DB_INIT, "found fine grained memory pool on host memory, size(MB) = " << size << std::endl);
        ri->_finegrained_system_memory_pool = region;
        ri->_found_finegrained_system_memory_pool = true;
    }

    if ((flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_COARSE_GRAINED) && (!ri->_found_coarsegrained_system_memory_pool)) {
        // DBOUT(DB_INIT, "found coarse-grain system memory pool=" << region.handle << " size(MB) = " << size << std::endl);
        ri->_coarsegrained_system_memory_pool = region;
        ri->_found_coarsegrained_system_memory_pool = true;
    }

    // choose coarse grained system for kernarg, if not available, fall back to fine grained system.
    if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT) {
      if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_COARSE_GRAINED) {
        // DBOUT(DB_INIT, "using coarse grained system for kernarg memory, size(MB) = " << size << std::endl);
        ri->_kernarg_memory_pool = region;
        ri->_found_kernarg_memory_pool = true;
      }
      else if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED
               && ri->_found_kernarg_memory_pool == false) {
        // DBOUT(DB_INIT, "using fine grained system for kernarg memory, size(MB) = " << size << std::endl);
        ri->_kernarg_memory_pool = region;
        ri->_found_kernarg_memory_pool = true;
      }
      else {
        // DBOUT(DB_INIT, "Unknown memory pool with kernarg_init flag set!!!, size(MB) = " << size << std::endl);
      }
    }

    return SUCCESS;
}

static status_t find_group_memory(hsa_amd_memory_pool_t region, void* data) {
  hsa_amd_segment_t segment;
  size_t size = 0;
  // bool flag = false;

  status_t status = SUCCESS;

  // get segment information
  status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment);
  STATUS_CHECK(status, __LINE__);

  if (segment == HSA_AMD_SEGMENT_GROUP) {
    // found group segment, get its size
    status = hsa_amd_memory_pool_get_info(region, HSA_AMD_MEMORY_POOL_INFO_SIZE, &size);
    STATUS_CHECK(status, __LINE__);

    // save the result to data
    size_t* result = (size_t*)data;
    *result = size;

    return HSA_STATUS_INFO_BREAK;
  }

  // continue iteration
  return SUCCESS;
}

    device_t& getAgent() {
        return agent;
    }

    device_t& getHostAgent() {
        return hostAgent;
    }

    // Returns true if specified agent has access to the specified pool.
    // Typically used to detect when a CPU agent has access to GPU device memory via large-bar:
    int hasAccess(device_t agent, hsa_amd_memory_pool_t pool)
    {
        status_t err;
        hsa_amd_memory_pool_access_t access;
        err = hsa_amd_agent_memory_pool_get_info(agent, pool, HSA_AMD_AGENT_MEMORY_POOL_INFO_ACCESS, &access);
        STATUS_CHECK(err, __LINE__);
        return access;
    }


    uint32_t node;

    std::wstring get_path() const override { return path; }
    std::wstring get_description() const override { return description; }
    size_t get_mem() const override { return ri._local_memory_pool_size; }
    bool is_double() const override { return true; }
    bool is_lim_double() const override { return true; }
    bool is_unified() const override {
        return (useCoarseGrainedRegion == false);
    }
    bool is_emulated() const override { return false; }
    uint32_t get_version() const { return ((static_cast<unsigned int>(versionMajor) << 16) | versionMinor); }

    // bool has_cpu_accessible_am() const override { return cpu_accessible_am; }

    void* create(size_t count, struct rw_info* key) override ;

    void release(void *ptr, struct rw_info* key ) override;

    /*
     * TODO TODO
    // calculate MD5 checksum
    std::string kernel_checksum(size_t size, void* source);

    void BuildProgram(void* size, void* source) override;

    inline std::string get_isa_name_from_triple(std::string triple);

    bool IsCompatibleKernel(void* size, void* source) override ;

    void* CreateKernel(const char* fun, Queue *queue) override ;
    */

    std::shared_ptr<Queue> createQueue(execute_order order = execute_in_order, queue_priority priority= priority_normal) override;
/*
    size_t GetMaxTileStaticSize() override {
        return max_tile_static_size;
    }
    */

    std::vector< std::shared_ptr<Queue> > get_all_queues() override ;

    hsa_amd_memory_pool_t& getHSAKernargRegion() {
        return ri._kernarg_memory_pool;
    }

    hsa_amd_memory_pool_t& getHSAAMHostRegion() {
        return ri._am_host_memory_pool;
    }

    hsa_amd_memory_pool_t& getHSACoherentAMHostRegion() {
        return ri._am_host_coherent_memory_pool;
    }

    hsa_amd_memory_pool_t& getDlaAMRegion() {
        return ri._am_memory_pool;
    }

    bool hasHSAKernargRegion() const {
      return ri._found_kernarg_memory_pool;
    }

    bool hasHSAFinegrainedRegion() const {
      return ri._found_finegrained_system_memory_pool;
    }

    bool hasHSACoarsegrainedRegion() const {
      return ri._found_local_memory_pool;
    }
    bool is_peer(const Device* other) override ;

    unsigned int get_compute_unit_count() override;
   

    int get_seqnum() const override {
        return this->accSeqNum;
    }

    bool has_cpu_accessible_am() override {
        return cpu_accessible_am;
    };

    void releaseKernargBuffer(void* kernargBuffer, int kernargBufferIndex);

    void growKernargBuffer();

     std::pair<void*, int> getKernargBuffer(int size) ;

    void* getSymbolAddress(const char* symbolName) override ;

    void memcpySymbol(void* symbolAddr, void* hostptr, size_t count, size_t offset = 0, enum hcCommandKind kind = hcMemcpyHostToDevice) override ;

    void memcpySymbol(const char* symbolName, void* hostptr, size_t count, size_t offset = 0, enum hcCommandKind kind = hcMemcpyHostToDevice) override ;

    void* getDlaAgent() override;

    // hcAgentProfile getProfile() override { return profile; }

private:

    void BuildOfflineFinalizedProgramImpl(void* kernelBuffer, int kernelSize) ;
};


} // namespace csl

