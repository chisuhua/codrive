#include "libhsakmt.h"
#include "inc/queue.h"
#include "common/utils/lang/error.h"
#include "cpu_cmdio.h"
#include "grid_engine/grid_engine.h"
#include "grid_engine/grid_engine_manager.h"
#include "grid_engine/cpu/aasim_reg.h"
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
// #include "grid_engine/cpu/lib/cmd_queue.h"
#include "dmlc/logging.h"
#include <unistd.h>


// TODO mlvm #include "sim/asim/Simulator.h"
// TODO mlvm #include "sim/aasim/aam_enginereg.h"

#define EOK (0)

using namespace std;

static CpuEngineCmdio* g_cmdio;


CpuEngineCmdio::CpuEngineCmdio()
{
    // TODO 1. create variouis hw related object(ComputeCore, Memory)
    //      2 create varios heap region and allocator
    //      3. hw ring infomatin

    // FIXME choose cpu or ppu
    m_engine = GridEngineManager::create_engine("cpu");
    // m_engine = GridEngineManager::create_engine("ppu");
    assert(m_engine != nullptr);

    // now simulator is going to run
    m_engine->Run(true);
    // m_engine->WaitToActive();
}

CpuEngineCmdio::~CpuEngineCmdio()
{

    RegWrite(0xdeadbeaf, 0x0);
    sleep(1);
    m_engine->Finish();

    clear_queue();
}

void CpuEngineCmdio::CreateQueue(ioctl_create_queue_args &args)
{
    // co_queue_t *cp_queue;
    int queue_id = m_engine->CreateQueue(args);
    // cp_queue->hsa_queue.base_address = (void*)args.ring_base_address;
    // cp_queue->hsa_queue.doorbell_signal = args.doorbell_offset;
    // cp_queue->hsa_queue.size = args.ring_size;
    // cp_queue->read_dispatch_id = args.read_pointer_address;
    // cp_queue->write_dispatch_id = args.write_pointer_address;
    // cp_queue->queue_priority = args.queue_priority;

    args.queue_id = queue_id;
}
#if 0
//    it might be better place , but i just set here to reduce code time
//    i call regWrite at device_tools OnLoad
// uint32_t CpuEngineCmdio::CreateQueue(bool dev_mem, uint64_t svm_base, uint64_t svm_limit)
// void CpuEngineCmdio::CreateQueue(bool dev_mem)
void CpuEngineCmdio::CreateQueue(co_queue_t* cmd_queue)
{
    //TODO: need to specify size and type
    // uint32_t size = 1024;
    // uint8_t type = 0;

    // m_engine_queue = m_engine->CreateQueue(size, type);
    m_engine->CreateQueue(cmd_queue);
    // assert(m_engine_queue != nullptr);
}
#endif 

void CpuEngineCmdio::clear_queue()
{
}

bool CpuEngineCmdio::RegWrite(uint32_t index, uint32_t value)
{
    return m_engine->RegWrite(index, value);
}

bool CpuEngineCmdio::RegRead(uint32_t index, uint32_t& data)
{
    return m_engine->RegRead(index, data);
}
#if 0
// only for local memory
bool CpuEngineCmdio::MemAlloc(uint64_t size_in_bytes,
    uint32_t align,
    uint32_t flag,
    MemObj& mem_obj,
    uint8_t* host_ptr)
{
    // TODO mlvm
    // auto local_memory = m_engine->MemAlloc(flag, host_ptr))
    // mem_obj.handle = handle; // hal_mem->GetMemHandle();
    // mem_obj.sys_addr = hal_mem->GetSysMemPtr();
    // mem_obj.dev_addr = local_memory->GetDevMemAddr();
    // mem_obj.size = size_in_bytes;
    return true;
}

bool CpuEngineCmdio::MemFree(MemHandle handle)
{
    if (handle == 0) {
        printf("Error:Device Mem is already free");
        return true;
    }

    // do some device specific free in this called, free sys_ptr if it is owner

    return true;
}
#endif

/*
bool CpuEngineCmdio::MemCopy(MemHandle handle, uint64_t bytes,
                       uint8_t dir, uint8_t *host_ptr,
                       uint64_t offset)
{

    // TODO: for DMA copy, it need to call DMA copy which need to locked pages firstly
    //    we can add lock function in pasim hal_mem
    HalMem *hal_mem = (HalMem *)handle;
    // FIXME: check mem copy
    if (!check_mem_copy(bytes, offset, dir, host_ptr)) {
        return false;
    }
    return hal_mem->MemCopy(bytes, offset, dir, host_ptr);
}
*/

bool CpuEngineCmdio::Sync(void)
{
    //TODO: not sure which pal module for this interface
    m_engine->WaitToIdle();
    return true;
}
#if 0
bool run_barrier(const cmd_barrier_and_packet_t* cmd)
{
    // TODO need to check all dep signal is done
    *(uint64_t*)(cmd->completion_signal + 8) = 0;
    return true;
}

bool run_barrier(const cmd_barrier_or_packet_t* cmd)
{
    // TODO need to check any dep signal is done
    *(uint64_t*)(cmd->completion_signal + 8) = 0;
    return true;
}

bool run_kernel(CmdQueue* cmd_queue, const cmd_dispatch_packet_t* cmb)
{
    const cmd_dispatch_packet_t* cmd = cmb;
    // aasim::CmdDispatchPacket* dispatch_packet = queue_->getWritePacket();
    cmd_dispatch_packet_t* dispatch_packet = reinterpret_cast<cmd_dispatch_packet_t*>(cmd_queue->getWritePacket());

    *dispatch_packet = *cmd;

    cmd_queue->setWriteIndex(cmd_queue->getWriteIndex() + 1);

    printf("try to run a bc format kernel, address = %lx, arg = %lx\n", (uint64_t)cmd->kernel_object, (uint64_t)cmd->kernarg_address);
    // asim_->Run(false);
    // sim_->Run(false);  the sim is already run in CpuEngineCmdio constructor
    // FIXME  remote wait here, use barrier instead
    //        the completion signal is set by device side
    /*
   sim_->WaitToIdle();

   if (cmd->completion_signal)
   {
       *(uint64_t *)(cmd->completion_signal + 8) = 0;
   }
   */
   return true;
}

bool Enqueue(CmdQueue* cmd_queue, const core::IAqlPacket* pkt)
{
    // aasim::CmdDispatchPacket *src = (aasim::CmdDispatchPacket*)pkt;
    //aasim::CmdDispatchPacket* dst = queue_->getWritePacket();
    cmd_dispatch_packet_t* src = (cmd_dispatch_packet_t*)pkt;
    cmd_dispatch_packet_t* dst = cmd_queue->getWritePacket();
    memcpy(dst, src, sizeof(cmd_dispatch_packet_t));
    cmd_queue->setWriteIndex(cmd_queue->getWriteIndex() + 1);
    return true;
}

bool Enqueue(CmdQueue* cmd_queue, const void* cmd)
{
    //peek the first cmd type
    uint16_t header = *((uint16_t*)(cmd));
    switch (header >> HSA_PACKET_HEADER_TYPE) {
    case HSA_PACKET_TYPE_KERNEL_DISPATCH:
        return run_kernel(cmd_queue, static_cast<const cmd_dispatch_packet_t*>(cmd));
        break;
    case HSA_PACKET_TYPE_DMA_COPY:
        // return run_dma_engine(static_cast<const aasim::cmd_dma_copy_packet_t *>(cmd));
        break;
    case HSA_PACKET_TYPE_BARRIER_AND:
        return run_barrier(static_cast<const cmd_barrier_and_packet_t*>(cmd));
        break;
    case HSA_PACKET_TYPE_BARRIER_OR:
        return run_barrier(static_cast<const cmd_barrier_or_packet_t*>(cmd));
        break;
    default:
        return false;
    }
    return true;
}
#endif

// TODO: each device have a CSI interface
void* cmd_open(uint32_t device_num)
{
    g_cmdio  = new CpuEngineCmdio();
    return (void*)g_cmdio;
}


/*
int cmdio_open(const char*, uint32_t flags) {
	auto device = dla_driver::CreateDeviceCSI();
    csi_[0] = device;
	return 1;
};


int drm_open(const char*, uint32_t flags) {
    drm_device* dev = new drm_device();
    drm_[0] = dev;
	return 1;
};

int kmtIoctl(int fd, unsigned long request, void* arg){
    CHECK(false);
};
*/
int cmd_create_queue(ioctl_create_queue_args *args){
    g_cmdio->CreateQueue(*args);
    return 0;
}

int cmd_alloc_memory(ioctl_alloc_memory_args *args){
	void *mem = (void*)args->va_addr;
    uint64_t size = args->size;
    uint64_t mmap_offset = args->mmap_offset;
    uint32_t flags = args->flags;

	// vm_object_t *vm_obj;
	int mmap_prot = PROT_READ | PROT_WRITE;

	//if (flags.ui32.ExecuteAccess)
    //		mmap_prot |= PROT_EXEC;

	/* mmap will return a pointer with alignment equal to
	 * sysconf(_SC_PAGESIZE).
	 */
	mem = mmap(mem, size, mmap_prot,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, mmap_offset);

	if (mem == MAP_FAILED)
		return -1;

    args->va_addr = (uint64_t)mem;
    return 0;
}

int cmd_read_register(uint32_t index, uint32_t *value){
    g_cmdio->RegRead(index, *value);
}

int cmd_write_register(uint32_t index, uint32_t value){
    g_cmdio->RegWrite(index, value);
}
