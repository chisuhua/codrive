#pragma once

#include "inc/hsakmttypes.h"

// * @brief Properties of the relationship between an agent a memory pool.
typedef enum {
    HSA_AMD_LINK_INFO_TYPE_HYPERTRANSPORT = 0, // * Hyper-transport bus type.
    HSA_AMD_LINK_INFO_TYPE_QPI = 1, // * QPI bus type.
    HSA_AMD_LINK_INFO_TYPE_PCIE = 2,
    HSA_AMD_LINK_INFO_TYPE_INFINBAND = 3,
    HSA_AMD_LINK_INFO_TYPE_XGMI = 4
} hsa_amd_link_info_type_t;

// * @brief Link properties when accessing the memory pool from the specified agent.
typedef struct hsa_amd_memory_pool_link_info_s {
    uint32_t min_latency; // * Minimum transfer latency (rounded to ns).
    uint32_t max_latency;
    uint32_t min_bandwidth;
    uint32_t max_bandwidth;
    bool atomic_support_32bit; // * Support for 32-bit atomic transactions.
    bool atomic_support_64bit; // * Support for 64-bit atomic transactions.
    bool coherent_support; // * Support for cache coherent transactions.
    hsa_amd_link_info_type_t link_type; // * The type of bus/link.
    uint32_t numa_distance; // * NUMA distance of memory pool relative to querying agent
} hsa_amd_memory_pool_link_info_t;

typedef enum {
  /**
  * Access to buffers located in the memory pool. The type of this attribute
  * is ::hsa_amd_memory_pool_access_t.
  *
  * An agent can always directly access buffers currently located in a memory
  * pool that is associated (the memory_pool is one of the values returned by
  * ::hsa_amd_agent_iterate_memory_pools on the agent) with that agent. If the
  * buffer is currently located in a memory pool that is not associated with
  * the agent, and the value returned by this function for the given
  * combination of agent and memory pool is not
  * HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED, the application still needs to invoke
  * ::hsa_amd_agents_allow_access in order to gain direct access to the buffer.
  *
  * If the given agent can directly access buffers the pool, the result is not
  * HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED. If the memory pool is associated with
  * the agent, or it is of fined-grained type, the result must not be
  * HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED. If the memory pool is not associated
  * with the agent, and does not reside in the global segment, the result must
  * be HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED.
  */
  HSA_AMD_AGENT_MEMORY_POOL_INFO_ACCESS = 0,

  /**
  * Number of links to hop when accessing the memory pool from the specified
  * agent. The value of this attribute is zero if the memory pool is associated
  * with the agent, or if the access type is
  * HSA_AMD_MEMORY_POOL_ACCESS_NEVER_ALLOWED. The type of this attribute is
  * uint32_t.
  */
  HSA_AMD_AGENT_MEMORY_POOL_INFO_NUM_LINK_HOPS = 1,

  /**
  * Details of each link hop when accessing the memory pool starting from the
  * specified agent. The type of this attribute is an array size of
  * HSA_AMD_AGENT_MEMORY_POOL_INFO_NUM_LINK_HOPS with each element containing
  * ::hsa_amd_memory_pool_link_info_t.
  */
  HSA_AMD_AGENT_MEMORY_POOL_INFO_LINK_INFO = 2

} hsa_amd_agent_memory_pool_info_t;


/// @brief Structure to describe connectivity between agents.
struct LinkInfo {
    LinkInfo()
        : num_hop(0)
        , info { 0 }
    {
    }

    uint32_t num_hop;
    hsa_amd_memory_pool_link_info_t info;
};

class GpuAgent;

class Platform {
    public:
    /// Should not be called directly, must be called only from Runtime::Acquire()
    bool Load();

    /// Should not be called directly, must be called only from Runtime::Release()
    bool Unload();

    // CpuAgent* DiscoverCpu(HSAuint32 node_id, HsaCoreProperties* node_prop);
    GpuAgent* DiscoverGpu(uint32_t node_id, HsaCoreProperties* node_prop) ;

    void RegisterLinkInfo(uint32_t node_id, uint32_t num_link) ;
    void BuildTopology() ;

};
