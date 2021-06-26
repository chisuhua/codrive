#pragma once

// C++ headers
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// CPU execution path
#if __KALMAR_ACCELERATOR__ == 2 || __KALMAR_CPU__ == 2
#include <ucontext.h>
#endif

// namespace hc {
// schi  typedef __fp16 half;
// }

//
// work-item related builtin functions
//
/* schi
extern "C" __attribute__((const,hc)) uint32_t hc_get_grid_size(unsigned int n);
extern "C" __attribute__((const,hc)) uint32_t hc_get_workitem_absolute_id(unsigned int n);
extern "C" __attribute__((const,hc)) uint32_t hc_get_group_size(unsigned int n);
extern "C" __attribute__((const,hc)) uint32_t hc_get_workitem_id(unsigned int n);
extern "C" __attribute__((const,hc)) uint32_t hc_get_num_groups(unsigned int n);
extern "C" __attribute__((const,hc)) uint32_t hc_get_group_id(unsigned int n);

extern "C" __attribute__((const,amp)) uint32_t amp_get_global_size(unsigned int n);
extern "C" __attribute__((const,amp)) uint32_t amp_get_global_id(unsigned int n);
extern "C" __attribute__((const,amp)) uint32_t amp_get_local_size(unsigned int n);
extern "C" __attribute__((const,amp)) uint32_t amp_get_local_id(unsigned int n);
extern "C" __attribute__((const,amp)) uint32_t amp_get_num_groups(unsigned int n);
extern "C" __attribute__((const,amp)) uint32_t amp_get_group_id(unsigned int n);

#if __KALMAR_ACCELERATOR__ == 2
#define tile_static thread_local
#else
#define tile_static __attribute__((tile_static))
#endif

extern "C" __attribute__((noduplicate,hc)) void hc_barrier(unsigned int n);
extern "C" __attribute__((noduplicate,amp)) void amp_barrier(unsigned int n) ;

/// macro to set if we want default queue be thread-local or not
#define TLS_QUEUE (1)


#ifndef CLK_LOCAL_MEM_FENCE
#define CLK_LOCAL_MEM_FENCE (1)
#endif

#ifndef CLK_GLOBAL_MEM_FENCE
#define CLK_GLOBAL_MEM_FENCE (2)
#endif
*/

/**
 * @namespace Kalmar
 * namespace for internal classes of Kalmar compiler / runtime
 */
// namespace Kalmar {
// } // namespace Kalmar

// Provide automatic type conversion for void*.
/*
class auto_voidp {
    void *_ptr;
    public:
        auto_voidp (void *ptr) : _ptr (ptr) {}
        template<class T> operator T *() { return (T *) _ptr; }
};
*/

// Valid values for__hcc_backend__ to indicate the
// compiler backend
#define HCC_BACKEND_AMDGPU (1)

namespace csq {

/// access_type is used for accelerator that supports unified memory
/// Such accelerator can use access_type to control whether can access data on
/// it or not
enum access_type
{
    access_type_none = 0,
    access_type_read = (1 << 0),
    access_type_write = (1 << 1),
    access_type_read_write = access_type_read | access_type_write,
    access_type_auto = (1 << 31)
};


enum queuing_mode
{
    queuing_mode_immediate,
    queuing_mode_automatic
};

enum execute_order
{
    execute_in_order,
    execute_any_order
};

/// NOTE: This enum is used for indexing into an array.
/// So any modifications to this need to be done with caution.
enum queue_priority
{
    priority_high = 0,
    priority_normal = 1,
    priority_low = 2
};

// Flags to specify visibility of previous commands after a marker is executed.
enum memory_scope
{
    no_scope=0,           // No release operation applied
    accelerator_scope=1,  // Release to current accelerator
    system_scope=2,       // Release to system (CPU + all accelerators)
};

enum hcCommandKind {
    hcCommandInvalid= -1,
    hcMemcpyHostToHost = 0,
    hcMemcpyHostToDevice = 1,
    hcMemcpyDeviceToHost = 2,
    hcMemcpyDeviceToDevice = 3,
    hcCommandKernel = 4,
    hcCommandMarker = 5,
};

enum hcWaitMode {
    hcWaitModeBlocked = 0,
    hcWaitModeActive = 1
};

static inline memory_scope greater_scope(memory_scope scope1, memory_scope scope2)
{
    if ((scope1==system_scope) || (scope2 == system_scope)) {
        return system_scope;
    } else if ((scope1==accelerator_scope) || (scope2 == accelerator_scope)) {
        return accelerator_scope;
    } else {
        return no_scope;
    }
};



}  // csq namespace
