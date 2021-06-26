#include "libhsakmt.h"
#include "inc/queue.h"
#include "common/utils/lang/error.h"
#include "ModelCmdio.h"
#include "ProcessVM.h"
#include "ModelInterface.h"
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
// #include "grid_engine/cpu/lib/cmd_queue.h"
#include "dmlc/logging.h"
#include <unistd.h>
#include "inc/RegDef.h"
#include <assert.h>
#include "inc/Mmu.h"

#define EOK (0)

using namespace std;

static ModelCmdio* g_cmdio;


// this model emulate system: cpu + apu + dgpu,  apu is node 0, dpug is node 1
//
// in realy system, it could be cpu + apu or cpu + apu + dgpu
// the apu is accelerator core, but use cpu's memory

const int32_t num_cpus = 1;

const int32_t num_nodes = 2;

// cpu info
proc_cpuinfo g_cpuinfo[num_cpus] = {{0, 0, "ppu"}};
cpu_cacheinfo_t g_cpu_cacheinfo[num_cpus]; //  = {0, 0, "ppu"};
// cpou cache info
// HsaCacheProperties cache_prop = {0, 1, 64, 4, 1, 20, 1, 1 };

// setup two nodes, 0 for apu, 1 for dgpu
HsaSystemProperties g_sys_prop = {2, 0, 0, 0 };

// below willb e setup in open function
node_props_t g_node_prop[num_nodes];

process_device_apertures g_process_apertures[num_nodes];

ModelCmdio::ModelCmdio()
    : m_model(create_model())
    , m_fragment_allocator(BlockAllocator(*this))
{
    queue_list.resize(CP_MAX_QUEUE_NUM);
    for (int i =0 ; i < queue_list.size(); i++) {
        queue_list[i] = 0;
    }
    assert(m_model != nullptr);
    reg_CP_GLB_CTRL cp_glb_ctrl;
    cp_glb_ctrl.bits.enable = 1;
    RegWrite(mmCP_GLB_REG_ADDR(CP_GLB_CTRL), cp_glb_ctrl.val);

    RegWrite(mmCP_GLB_REG_ADDR(CP_GLB_CTRL), cp_glb_ctrl.val);

    // TODO get from model 's cap
    m_mmu_physical_mode = 1;

    // TODO check 
    m_mmu_dev_pa_offset = (1ULL << 48);
    m_mmu_dev_pa_base = 0;
    m_mmu_dev_pa_top = m_mmu_dev_pa_base + m_mmu_dev_pa_size;

    // first 1G is not used as va, reserved for pa
    // m_mmu_dev_va_base = SVM_USER_BASE;       // should be same as gpuvm_base and limit 
    m_mmu_dev_va_base = 0;
    m_mmu_dev_va_top = (1ULL << 49);
    m_mmu_dev_va_size = m_mmu_dev_va_top - m_mmu_dev_va_base + 1;


    m_dev_heap.~SmallHeap();
    // dev pa allocator, first page is not used
    new (&m_dev_heap) SmallHeap((void*)m_mmu_dev_pa_base + kPAGE_4KB_SIZE_, m_mmu_dev_pa_size);

    m_bar_mem_write = 0;
}

void ModelCmdio::InitVM() {

    int page_entry_num =  PAGE_4KB_SIZE/sizeof(PTE);
    uint64_t total_pte_num = (m_mmu_dev_va_size) / PAGE_4KB_SIZE;
    uint64_t total_pte_pages = (total_pte_num + page_entry_num - 1) / page_entry_num;
    uint32_t total_pde_pages = (total_pte_pages + page_entry_num - 1) /page_entry_num;
    uint32_t total_pd1_pages = (total_pde_pages + page_entry_num - 1) /page_entry_num;
    uint32_t total_pd0_pages = (total_pd1_pages + page_entry_num - 1) /page_entry_num;

    uint32_t total_pages = total_pd0_pages + total_pd1_pages + total_pde_pages + total_pte_pages;
    assert(total_pd0_pages == 2 && "ERROR, pd0_pages is not 1, va range is too large");

    /*
    // but we only setup 2 pde pagae and 20 pte page at init 
    total_pde_pages = 1;
    total_pte_pages = 1;
    uint32_t init_pages = total_pd0_pages + total_pd1_pages + total_pde_pages + total_pte_pages;

    void* pt_base_addr = BlockAlloc(total_pages*PAGE_4KB_SIZE);
    m_pd0_base_addr = (uint64_t)pt_base_addr;
    m_pd1_base_addr = m_pd0_base_addr + PAGE_4KB_SIZE * 2;
    m_pde_base_addr = m_pd1_base_addr + PAGE_4KB_SIZE * total_pd1_pages;
    m_pte_base_addr = m_pde_base_addr + PAGE_4KB_SIZE * total_pde_pages;
    */

    uint64_t pt_base_addr = (uint64_t)BlockAlloc(2*PAGE_4KB_SIZE);

    reg_MMU_CFG     mmu_cfg; reg_MMU_PT_CFG  pt_cfg;
    reg_MMU_PT_BASE pt_base;

    reg_MMU_DEV_PA_RANGE_BASE dev_pa_base;
    reg_MMU_DEV_PA_RANGE_TOP dev_pa_top;

    dev_pa_base.val = m_mmu_dev_pa_base ;
    dev_pa_top.val = m_mmu_dev_pa_top ;

    mmu_cfg.bits.fragment_en = m_mmu_fragment_en;
    pt_base.val = pt_base_addr;           // first page is not used, we use second page for pd0 table
    pt_cfg.bits.physical_mode = m_mmu_physical_mode;
    pt_cfg.bits.pte_bit = m_pt_pte_bit;
    pt_cfg.bits.pde_bit = m_pt_pde_bit;
    pt_cfg.bits.pd1_bit = m_pt_pd1_bit;
    pt_cfg.bits.pd0_bit = m_pt_pd0_bit;

    // setup base MMU table
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_CFG),  mmu_cfg.val);

    RegWrite(mmMMU_GLB_REG_ADDR(MMU_PT_CFG),  pt_cfg.val);
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_PT_BASE_HI), pt_base.bits.hi);
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_PT_BASE_LO), pt_base.bits.lo);

    RegWrite(mmMMU_GLB_REG_ADDR(MMU_DEV_PA_RANGE_BASE_HI), dev_pa_base.bits.hi);
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_DEV_PA_RANGE_BASE_LO), dev_pa_base.bits.lo);
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_DEV_PA_RANGE_TOP_HI), dev_pa_top.bits.hi);
    RegWrite(mmMMU_GLB_REG_ADDR(MMU_DEV_PA_RANGE_TOP_LO), dev_pa_top.bits.lo);

    ProcessVM* vm = new ProcessVM(this, pt_base_addr, m_model, m_bar_mem_write, m_mmu_dev_va_base);
    m_process_vm[m_vmid] = vm;
}

// insert page from va_base to va_size
void ModelCmdio::MapVA(uint64_t handle) {

    auto itr = m_allocated_bo.find(handle);
    if (itr == m_allocated_bo.end()) {
        assert("invalid handle pass to map_memory_to_gpu");
    }

    assert(handle == itr->second.handle && "invalid handle pass to map_memory_to_gpu");

    uint64_t pa_base = itr->second.handle;
    uint64_t va_base = itr->second.va_addr;
    uint64_t va_size = itr->second.size;
    uint32_t flags = itr->second.flags;

    int readable = 1;
    int writeable = (flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE) != 0 ? 1 : 0;
    int executable = (flags & KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE) != 0 ? 1 : 0;
    int valid = 1;
    int system =  (flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) != 0 ? 1 : 0;

    assert(va_base >= m_mmu_dev_va_base && "ERROR: only setup va large than 1G");
    
    m_process_vm[m_vmid]->mmap(va_base, va_size, pa_base, readable, writeable, executable, valid, system);
}

#if 0
    uint64_t *d_pd0_page = nullptr;
    uint64_t *d_pd1_page = nullptr;
    uint64_t *d_pde_page = nullptr;
    uint64_t *d_pte_page = nullptr;

    uint64_t *guard_page = nullptr;

    int page_entry_num =  PAGE_4KB_SIZE/sizeof(PTE);

    // setup mapping and leave attr setting later
    int total_pte_num = (m_mmu_dev_va_size) / PAGE_4KB_SIZE;

    uint64_t va_start = AlignDown(va_base, PAGE_4KB_SIZE);
    uint64_t va_end = AlignUp(va_base + va_size - 1, PAGE_4KB_SIZE) - 1;

    int pte_start = (va_start - m_mmu_dev_va_base) >> PAGE_4KB_BIT_NUM;
    int pte_end   = (va_end- m_mmu_dev_va_base) >> PAGE_4KB_BIT_NUM;
    int pte_pages = (pte_end - pte_start + 1 + page_entry_num -1) / page_entry_num;

    int pde_start = (va_start - m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM);
    int pde_end   = (va_end- m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM);
    int pde_pages = (pde_end - pde_start + 1 + page_entry_num -1) / page_entry_num;

    int pd1_start = (va_start - m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM);
    int pd1_end   = (va_end- m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM);
    int pd1_pages = (pd1_end - pd1_start + 1 + page_entry_num -1) / page_entry_num;

    int pd0_start = (va_start - m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM + PD1_ADDR_BIT_NUM);
    int pd0_end   = (va_end- m_mmu_dev_va_base) >> (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM + PD1_ADDR_BIT_NUM);

    assert(pd0_start == pd0_end && "va range is too large for mapping");

    printf("[MMU Info]: setup table for va_base=%llx, va_top=%llx, pte_start=%lx, pte_end=%lx, need %d pte pages, pde_start=%lx, pde_end=%lx, need %d pde pages, pd1_start=%lx, pd1_end=%lx, need %d pd1 pages\n", va_start, va_end, pte_start, pte_end, pte_pages, pde_start, pde_end, pde_pages, pd1_start, pd1_end, pd1_pages);


    int readable = 1;
    int writeable = 1;
    int executable = 1;
    int valid = 1;

    uint64_t *host_pd0_data= (uint64_t*)malloc(PAGE_4KB_SIZE);
    uint64_t *host_pd1_data= (uint64_t*)malloc(PAGE_4KB_SIZE);
    uint64_t *host_pde_data= (uint64_t*)malloc(PAGE_4KB_SIZE);
    uint64_t *host_pde_data= (uint64_t*)malloc(PAGE_4KB_SIZE);
    // map va to same pa
    int start_entry_in_page = pd0_start;
    for (int entry = pd0_start; entry <= pd0_end; ++entry) {
        /*
        PDE pde = { 0 };
        pde.info.valid = valid;
        pde.info.system = 0;
        pde.info.executable = executable;
        pde.info.readable = readable;
        pde.info.writeable = writeable;
        pde.info.fragment = 0;
        pde.info.address = m_pd1_base_addr + entry*PAGE_4KB_SIZE;
        pde.info.pte = 0;
        pde.info.pagesize = 0;
        pde.info.level = 2;

        int entry_in_page = entry % page_entry_num;
        *(host_data_to_gpu + entry_in_page) = pde.data;
        */

        if (((entry + 1) % page_entry_num == 0) || entry == pd1_end)  {
            ppuReadMmio(m_model, m_pd0_base_addr + start_entry_in_page*sizeof(pde.data), host_data, (entry - start_entry_in_page + 1)*sizeof(pde.data), m_bar_mem_write);

            PDE *pde;
            for (int i = 0; i <= entry - start_entry_in_page; i++) {
                pde = reinterpret_cast<PDE*>(&host_data[i]);
                if (pde->info.valid == 0) {
                    void* pd1_page = BlockAlloc(PAGE_4KB_SIZE);
                    setupPd1Page(i + start_entry_in_page, va_start, va_end, (uint64_t)pd1_page, readable, writeable, executable, valid, true);
                    pde->info.address = (uint64_t)pd1_page;
                } else {
                    pde->info.valid = valid;
                    pde->info.system = 0;
                    pde->info.executable = executable;
                    pde->info.readable = readable;
                    pde->info.writeable = writeable;
                    pde->info.fragment = 0;
                    pde->info.pte = 0;
                    pde->info.pagesize = 0;
                    pde->info.level = 2;
                    setupPd1Page(entry, va_start, va_end, pde->info.address, readable, writeable, executable, valid, false);
                }
            }

            ppuWriteMmio(m_model, m_pd0_base_addr + start_entry_in_page*sizeof(pde.data), host_data, (entry - start_entry_in_page + 1)*sizeof(pde.data), m_bar_mem_write);
            start_entry_in_page = entry + 1;
        }
    }
    free(host_pd0_data);
    free(host_pd1_data);
    free(host_pde_data);
    free(host_pte_data);
}

void SetupPd1Page(uint64_t pd0_entry, uint64_t va_start, uint64_t va_end, uint64_t page_address, int readable, int writeable, int executable, int valid, bool new_page) {

    if ((pd0_entry << PD0_BIT_SHIFT) > (va_start - m_mmu_dev_va_base)) va_start = pd0_entry << PD0_BIT_SHIFT;
    if (((pd0_entry + 1） << PD0_BIT_SHIFT) < (va_start - m_mmu_dev_va_base)) va_end = pd0_entry << PD0_BIT_SHIFT;

    int pd1_start = (va_start - m_mmu_dev_va_base) >> PD1_BIT_SHIFT;
    int pd1_end   = (va_end- m_mmu_dev_va_base) >> PD1_BIT_SHIFT
    int pd1_pages = (pd1_end - pd1_start + 1 + page_entry_num -1) / page_entry_num;


    uint64_t *host_data= (uint64_t*)malloc(PAGE_4KB_SIZE);

    if (!new_page) {
        ppuReadMmio(m_model, page_address, host_data, PAGE_4KB_SIZE, m_bar_mem_write);
    } else {
        memset(host_data, 0);
    }

    PDE *pde;
    for (int i = pd1_start; i <= pd1_end; ++i) {
        pde = reinterpret_cast<PDE*>(&host_data[i]);

        if (pde->info.valid == 0) {
            void* pde_page = BlockAlloc(PAGE_4KB_SIZE);
            setupPdePage(pd1_start + i, va_start, va_end, (uint64_t)pde_page, readable, writeable, executable, valid, true);
            pde->info.address = (uint64_t)pde_page;
        } else {
            pde->info.valid = valid;
            pde->info.system = 0;
            pde->info.executable = executable;
            pde->info.readable = readable;
            pde->info.writeable = writeable;
            pde->info.fragment = 0;
            pde->info.pte = 0;
            pde->info.pagesize = 0;
            pde->info.level = 2;
            setupPdePage(pd1_start + i, va_start, va_end, pde->info.address, readable, writeable, executable, valid, false);
        }
    }

    ppuWriteMmio(m_model, page_address, host_data, PAGE_4KB_SIZE, m_bar_mem_write);
}

void SetupPdePage(uint64_t pd1_entry, uint64_t va_start, uint64_t va_end, uint64_t page_address, int readable, int writeable, int executable, int valid, bool new_page) {

    if ((pd1_entry << PD1_BIT_SHIFT) > (va_start - m_mmu_dev_va_base)) va_start = pd1_entry << PD1_BIT_SHIFT;
    if (((pd1_entry + 1） << PD1_BIT_SHIFT) < (va_start - m_mmu_dev_va_base)) va_end = pd1_entry << PD1_BIT_SHIFT;

    int pde_start = (va_start - m_mmu_dev_va_base) >> PD1_BIT_SHIFT;
    int pde_end   = (va_end- m_mmu_dev_va_base) >> PD1_BIT_SHIFT


    if (!new_page) {
        ppuReadMmio(m_model, page_address, host_pde_data, PAGE_4KB_SIZE, m_bar_mem_write);
    } else {
        memset(host_pde_data, 0);
    }

    PDE *pde;
    for (int i = pde_start; i < pde_end; ++i) {
        pde = reinterpret_cast<PDE*>(&host_pde_data[i]);

        if (pde->info.valid == 0) {
            void* pde_page = BlockAlloc(PAGE_4KB_SIZE);
            setupPtePage(pde_start + i, va_start, va_end, pa_start, (uint64_t)pde_page, readable, writeable, executable, valid, true);
            pde->info.address = (uint64_t)pde_page;
        } else {
            pde->info.valid = valid;
            pde->info.system = 0;
            pde->info.executable = executable;
            pde->info.readable = readable;
            pde->info.writeable = writeable;
            pde->info.fragment = 0;
            pde->info.pte = 0;
            pde->info.pagesize = 0;
            pde->info.level = 2;
            setupPtePage(pde_start + i, va_start, va_end, pa_start, pde->info.address, readable, writeable, executable, valid, false);
        }
    }

    ppuWriteMmio(m_model, page_address, host_pde_data, PAGE_4KB_SIZE, m_bar_mem_write);
}

void SetupPtePage(uint64_t pde_entry, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, int readable, int writeable, int executable, int valid, bool new_page) {

    if ((pde_entry << PDE_BIT_SHIFT) > (va_start - m_mmu_dev_va_base)) va_start = pde_entry << PDE_BIT_SHIFT;
    if (((pde_entry + 1） << PDE_BIT_SHIFT) < (va_start - m_mmu_dev_va_base))) va_end = pde_entry << PDE_BIT_SHIFT;

    int pte_start = (va_start - m_mmu_dev_va_base) >> PTE_BIT_SHIFT;
    int pte_end   = (va_end- m_mmu_dev_va_base) >> PTE_BIT_SHIFT


    if (!new_page) {
        ppuReadMmio(m_model, page_address, host_pte_data, PAGE_4KB_SIZE, m_bar_mem_write);
    } else {
        memset(host_pte_data, 0);
    }

    PTE *pte;
    for (int i = pte_start; i < pte_end; ++i) {
        pte = reinterpret_cast<PTE*>(&host_pte_data[i]);

        pte->info.valid = valid;
        pte->info.system = 0;
        pte->info.executable = executable;
        pte->info.readable = readable;
        pte->info.writeable = writeable;
        pte->info.fragment = 0;
        pte->info.address = pa_start >> PAGE_4KB_BIT_NUM + i - pte_start;
        pte->info.pagesize = 0;
        pte->info.level = 3;
    }

    ppuWriteMmio(m_model, page_address, host_pte_data, PAGE_4KB_SIZE, m_bar_mem_write);
}

    for (int entry = pde_start; entry <= pde_end; ++entry) {
        readable = 1;
        writeable = 1;
        executable = 1;
        valid = 1;
        PDE pde = { 0 };
        pde.info.valid = valid;
        pde.info.system = 0;
        pde.info.executable = executable;
        pde.info.readable = readable;
        pde.info.writeable = writeable;
        pde.info.fragment = 0;
        pde.info.address = m_pte_base_addr + entry*PAGE_4KB_SIZE;
        pde.info.pte = 0;
        pde.info.pagesize = 0;
        pde.info.level = 2;

        int entry_in_page = entry % page_entry_num;
        *(host_data + entry_in_page) = pde.data;

        if (((entry + 1) % page_entry_num == 0) || entry == pde_end)  {
            ppuWriteMmio(m_model, m_pde_base_addr + entry*sizeof(pde.data), host_data, (entry - start_entry_in_page + 1)*sizeof(pde.data), m_bar_mem_write);
            start_entry_in_page = entry + 1;
        }
    }

    start_entry_in_page = pte_start;
    uint64_t pa_pfn = pa_base >> PAGE_4KB_BIT_NUM;
    for (int entry = pte_start; entry <= pte_end; ++entry) {
        readable = 1;
        writeable = 1;
        executable = 1;
        valid = 1;
        PTE pte = { 0 };
        pte.info.valid = valid;
        pte.info.system = 0;
        pte.info.executable = executable;
        pte.info.readable = readable;
        pte.info.writeable = writeable;
        pte.info.fragment = 0;
        pte.info.address = pa_pfn++;
        pte.info.pagesize = 0;
        pte.info.level = 3;

        int entry_in_page = entry % page_entry_num;
        *(host_data + entry_in_page) = pte.data;

        if (((entry + 1) % page_entry_num == 0) || entry == pte_end)  {
            ppuWriteMmio(m_model, m_pte_base_addr + entry*sizeof(pte.data), host_data, (entry - start_entry_in_page +1)*sizeof(pte.data), m_bar_mem_write);
            start_entry_in_page = entry + 1;
        }
    }
#endif

ModelCmdio::~ModelCmdio()
{

    // RegWrite(0xdeadbeaf, 0x0);
    // sleep(1);
    clear_queue();

    reg_CP_GLB_CTRL cp_glb_ctrl;
    RegRead(mmCP_GLB_REG_ADDR(CP_GLB_CTRL), cp_glb_ctrl.val);
    cp_glb_ctrl.bits.enable = 0;
    RegWrite(mmCP_GLB_REG_ADDR(CP_GLB_CTRL), cp_glb_ctrl.val);

    Sync();

}

void ModelCmdio::CreateQueue(ioctl_create_queue_args &args)
{

    int32_t valid_queue_id = -1;

    for (int i = 0; i < queue_list.size(); i++) {
        if (queue_list[i] == 0 ) {
            valid_queue_id = i;
            break;
        }
    }

    if (valid_queue_id == -1) {
        assert("Not Queue Slot avaiable");
    }
    reg_CP_QUEUE_CTRL               queue_ctrl;
    reg_CP_QUEUE_RB_BASE            queue_base;
    reg_CP_QUEUE_RB_SIZE            queue_size;
    reg_CP_QUEUE_RB_RPTR            queue_rptr;
    reg_CP_QUEUE_RB_WPTR            queue_wptr;
    reg_CP_QUEUE_DOORBELL_BASE      doorbell_base;
    reg_CP_QUEUE_DOORBELL_OFFSET    doorbell_offset;

    queue_base.val = args.ring_base_address;
    queue_size.val = args.ring_size;
    queue_rptr.val = args.read_pointer_address;
    queue_wptr.val = args.write_pointer_address;
    // uint32_t read_dispatch_id = 0;
    // uint32_t write_dispatch_id = 0;
    doorbell_base.val = args.doorbell_base;
    doorbell_offset.val = args.doorbell_offset;
    // cp_queue->queue_priority = args.queue_priority;
    queue_ctrl.bits.priority = args.queue_priority;
    queue_ctrl.bits.enable = 0x1;

    args.queue_id = valid_queue_id;

    // TODO we use 24bit for RegSpace, and mmu will translate address reg to va
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_BASE_HI), queue_base.bits.hi);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_BASE_LO), queue_base.bits.lo);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_SIZE), queue_size.val);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_RPTR_HI), queue_rptr.bits.hi);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_RPTR_LO), queue_rptr.bits.lo);

    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_WPTR_HI), queue_wptr.bits.hi);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_RB_WPTR_LO), queue_wptr.bits.lo);

    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_DOORBELL_BASE_HI), doorbell_base.bits.hi);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_DOORBELL_BASE_LO), doorbell_base.bits.lo);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_DOORBELL_OFFSET), doorbell_offset.val);
    write_register(m_model, mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_CTRL), queue_ctrl.val);

    queue_ctrl.bits.enable = 0x0;
    RegRead(mmCP_QUEUE_REG_ADDR(valid_queue_id, CP_QUEUE_CTRL), queue_ctrl.val);
    assert(queue_ctrl.bits.enable == 1);
    queue_list[valid_queue_id] = 1;
    // cp_queue->hsa_queue.base_address = (void*)args.ring_base_address;
    // cp_queue->hsa_queue.doorbell_signal = args.doorbell_offset;
    // cp_queue->hsa_queue.size = args.ring_size;
    // cp_queue->read_dispatch_id = args.read_pointer_address;
    // cp_queue->write_dispatch_id = args.write_pointer_address;
    // cp_queue->queue_priority = args.queue_priority;

}


void ModelCmdio::clear_queue()
{
}

void ModelCmdio::RegWrite(uint32_t index, uint32_t value)
{
    write_register(m_model, index, value);
}

void ModelCmdio::RegRead(uint32_t index, uint32_t& data)
{
    data = read_register(m_model, index);
}

bool ModelCmdio::Sync(void)
{
    //TODO: not sure which pal module for this interface
    // m_model->WaitToIdle();
    bool busy = true;
    reg_CP_GLB_STATUS cp_glb_status;
    while (busy) {
        busy = false;
        RegRead(mmCP_GLB_REG_ADDR(CP_GLB_STATUS), cp_glb_status.val);
        if (cp_glb_status.bits.busy == 1 ) {
            busy = true;
            sleep(1);
        };
    }
    return true;
}

void* ModelCmdio::BlockAllocator::alloc(size_t request_size, size_t& allocated_size) const {
  assert(request_size <= block_size() && "BlockAllocator alloc request exceeds block size.");

  void* ret;
  size_t bsize = block_size();
  bool err = region_.BlockAlloc(bsize);
  if (err != true)
  {
    // throw ::hcs::hsa_exception(err, "ModelCmdio::BlockAllocator::alloc failed.");
    throw utils::Error("ModelCmdio::BlockAllocator::alloc failed.");
  }
  assert(ret != nullptr && "Region returned nullptr on success.");

  allocated_size = block_size();
  return ret;
}

void* ModelCmdio::BlockAlloc(size_t bsize) {
    return m_dev_heap.alloc(bsize);
}

void ModelCmdio::AllocMemory(ioctl_alloc_memory_args &args)
{
    uint64_t size = args.size;
    uint64_t allocated_size;
/*
    if (size > max_single_alloc_size_) {
        return false;
    }
*/
    // size = AlignUp(size, kPAGE_4KB_SIZE_);
    // if (size <= fragment_allocator_.max_alloc());
    // args.va_addr = reinterpret_cast<uint64_t>(m_fragment_allocator.alloc(size));
    args.handle = reinterpret_cast<uint64_t>(m_dev_heap.alloc(size)); // , allocated_size));
    // args.size = allocated_size;
    args.size = size;
    m_allocated_bo.insert(std::make_pair(args.handle, args));
}

void ModelCmdio::FreeMemory(ioctl_free_memory_args &args)
{
    uint64_t handle = args.handle;

    auto itr = m_allocated_bo.find(handle);
    if (itr != m_allocated_bo.end()) {
        m_allocated_bo.erase(itr);
    } else {
        printf("FATAL: handle %lx is not valid in ModelCmdio::FreeMemory", handle);
        assert("FreeMemory can't find bo of handle");
    }
    m_dev_heap.free((void*)handle);
}

//  cmdio interface api

int cmd_read_register(uint32_t index, uint32_t *value){
    g_cmdio->RegRead(index, *value);
}

int cmd_write_register(uint32_t index, uint32_t value){
    g_cmdio->RegWrite(index, value);
}

// TODO: each device have a CSI interface
int cmd_open(ioctl_open_args *args)
{
    g_cmdio  = new ModelCmdio();

    size_t mem_banks;
    size_t num_caches;
    size_t num_links;

    // cpu info
    // g_cpuinfo[0].proc_num = 0;
    // g_cpuinfo[0].apicid = 0;
    // g_cpuinfo[0].model_name = "ppu";
    // g_cpuinfo[0] = {0, 0, "ppu"};

    g_cpu_cacheinfo[0].len = 0;
    g_cpu_cacheinfo[0].proc_num = 0;

    num_caches  = 1;
    g_cpu_cacheinfo[0].num_caches = num_caches;
    g_cpu_cacheinfo[0].cache_prop = new HsaCacheProperties[num_caches];
    g_cpu_cacheinfo[0].cache_prop[0].ProcessorIdLow = 1u;
    g_cpu_cacheinfo[0].cache_prop[0].CacheLevel = 2;
    g_cpu_cacheinfo[0].cache_prop[0].CacheSize = 2048000;
    g_cpu_cacheinfo[0].cache_prop[0].CacheLineSize = 64;
    g_cpu_cacheinfo[0].cache_prop[0].CacheLinesPerTag = 4u;
    g_cpu_cacheinfo[0].cache_prop[0].CacheAssociativity = 1u;
    g_cpu_cacheinfo[0].cache_prop[0].CacheLatency = 100u;
    g_cpu_cacheinfo[0].cache_prop[0].CacheType.Value = 8u;
    g_cpu_cacheinfo[0].cache_prop[0].SiblingMap[0] = 1u;

    // all node have cpu core and compute core,
    // if node is host, then it is apu, it must have both cpu and compute core
    //      gpu_id must be 0
    // if node is device, then it is gpu, it only have comptue core, the cpu core is used as cp


	uint16_t fw_version = (uint16_t)1;

    // Node0 is gpu
    g_node_prop[0].core  = new HsaCoreProperties;
    g_node_prop[0].gpu_id  = 1;


	g_node_prop[0].core->NumCPUCores = (uint32_t)0;        // the ppu device don't have cpu but have computecore
	g_node_prop[0].core->NumFComputeCores = (uint32_t)8;
	g_node_prop[0].core->NumMemoryBanks = (uint32_t)1;
	g_node_prop[0].core->NumCaches = (uint32_t)1;
	g_node_prop[0].core->NumIOLinks = (uint32_t)0;        // TODO should we need iolink
	g_node_prop[0].core->CComputeIdLo = (uint32_t)1;
	g_node_prop[0].core->FComputeIdLo = (uint32_t)1;
	g_node_prop[0].core->Capability.Value = (uint32_t)1;
	g_node_prop[0].core->MaxWavesPerSIMD = (uint32_t)64;
	g_node_prop[0].core->LDSSizeInKB = (uint32_t)32;
	g_node_prop[0].core->GDSSizeInKB = (uint32_t)32;
	g_node_prop[0].core->WaveFrontSize = (uint32_t)32;
	g_node_prop[0].core->NumShaderBanks = (uint32_t)32;
	g_node_prop[0].core->NumArrays = (uint32_t)1;
	g_node_prop[0].core->NumCUPerArray = (uint32_t)1;
	g_node_prop[0].core->NumSIMDPerCU = (uint32_t)4;
	g_node_prop[0].core->MaxSlotsScratchCU = (uint32_t)1;
	g_node_prop[0].core->VendorId = (uint32_t)0xaabb;  // use same value in Simulator.h
	g_node_prop[0].core->DeviceId = (uint32_t)0xccee;
	g_node_prop[0].core->LocationId = (uint32_t)0x1;
	g_node_prop[0].core->MaxEngineClockMhzFCompute = (uint32_t)10000;
	g_node_prop[0].core->MaxEngineClockMhzCCompute = (uint32_t)10000;
	g_node_prop[0].core->LocalMemSize = 10000;
	g_node_prop[0].core->DrmRenderMinor = (int32_t)128;

	g_node_prop[0].core->EngineId.ui32.uCode = fw_version & 0x3ff;
	g_node_prop[0].core->EngineId.ui32.Major = 0;
	g_node_prop[0].core->EngineId.ui32.Minor = 0;
	g_node_prop[0].core->EngineId.ui32.Stepping = 0;

    mem_banks = g_node_prop[0].core->NumMemoryBanks;
	assert(mem_banks == 1);
    g_node_prop[0].mem = new HsaMemoryProperties[mem_banks];

    g_node_prop[0].mem[0].HeapType = HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC;
    g_node_prop[0].mem[0].SizeInBytes = 0x80000000ull;  // 512M
    g_node_prop[0].mem[0].Flags.MemoryProperty = 0u;
    g_node_prop[0].mem[0].Width = 64ull;
    g_node_prop[0].mem[0].MemoryClockMax = 100000u;

    num_caches = g_node_prop[0].core->NumCaches;
	assert(num_caches >= 1);
    g_node_prop[0].cache = new HsaCacheProperties[num_caches];
    g_node_prop[0].cache[0].ProcessorIdLow = 1u;
    g_node_prop[0].cache[0].CacheLevel = 2;
    g_node_prop[0].cache[0].CacheSize = 2048000;
    g_node_prop[0].cache[0].CacheLineSize = 64;
    g_node_prop[0].cache[0].CacheLinesPerTag = 4u;
    g_node_prop[0].cache[0].CacheAssociativity = 1u;
    g_node_prop[0].cache[0].CacheLatency = 100u;
    g_node_prop[0].cache[0].CacheType.Value = 8u;
    g_node_prop[0].cache[0].SiblingMap[0] = 1u;

    num_links = g_node_prop[0].core->NumIOLinks;
    if (num_links > 0) {
        g_node_prop[0].link = new HsaIoLinkProperties[num_links];
        g_node_prop[0].link[0].IoLinkType = HSA_IOLINKTYPE_PCIEXPRESS;
	    g_node_prop[0].link[0].VersionMajor = (uint32_t)1;
	    g_node_prop[0].link[0].VersionMinor = (uint32_t)0;
	    g_node_prop[0].link[0].NodeFrom = (uint32_t)0;
	    g_node_prop[0].link[0].NodeTo = (uint32_t)0;
	    g_node_prop[0].link[0].Weight = (uint32_t)1;
	    g_node_prop[0].link[0].MinimumLatency = (uint32_t)500;   // in ns
	    g_node_prop[0].link[0].MaximumLatency = (uint32_t)2000;
	    g_node_prop[0].link[0].MinimumBandwidth = (uint32_t)1000;  // in MB/s
	    g_node_prop[0].link[0].MaximumBandwidth = (uint32_t)10000;
	    g_node_prop[0].link[0].RecTransferSize = (uint32_t)0;
	    g_node_prop[0].link[0].Flags.LinkProperty = (uint32_t)0x1E;   // not allow cc, atmoic, p2p
    }



    // APU node
    g_node_prop[1].core  = new HsaCoreProperties;
    g_node_prop[1].gpu_id  = 0;  // FIXME gpu_id > 0?

	g_node_prop[1].core->NumCPUCores = (uint32_t)1;        // the pasim device don't have cpu but have computecore
	g_node_prop[1].core->NumFComputeCores = (uint32_t)1;
	g_node_prop[1].core->NumMemoryBanks = (uint32_t)1;
	g_node_prop[1].core->NumCaches = (uint32_t)1;
	g_node_prop[1].core->NumIOLinks = (uint32_t)0;        // TODO should we need iolink
	g_node_prop[1].core->CComputeIdLo = (uint32_t)0;
	g_node_prop[1].core->FComputeIdLo = (uint32_t)0;
	g_node_prop[1].core->Capability.Value = (uint32_t)0;
	g_node_prop[1].core->MaxWavesPerSIMD = (uint32_t)0;
	g_node_prop[1].core->LDSSizeInKB = (uint32_t)32;
	g_node_prop[1].core->GDSSizeInKB = (uint32_t)0;
	g_node_prop[1].core->WaveFrontSize = (uint32_t)0;
	g_node_prop[1].core->NumShaderBanks = (uint32_t)0;
	g_node_prop[1].core->NumArrays = (uint32_t)0;
	g_node_prop[1].core->NumCUPerArray = (uint32_t)0;
	g_node_prop[1].core->NumSIMDPerCU = (uint32_t)1;
	g_node_prop[1].core->MaxSlotsScratchCU = (uint32_t)0;
	g_node_prop[1].core->VendorId = (uint32_t)0xaabb;  // use same value in Simulator.h
	g_node_prop[1].core->DeviceId = (uint32_t)0xccee;
	g_node_prop[1].core->LocationId = (uint32_t)0x1;
	g_node_prop[1].core->MaxEngineClockMhzFCompute = (uint32_t)10000;
	g_node_prop[1].core->MaxEngineClockMhzCCompute = (uint32_t)10000;
	g_node_prop[1].core->LocalMemSize = 10000;
	g_node_prop[1].core->DrmRenderMinor = (int32_t)129;
	g_node_prop[1].core->EngineId.ui32.uCode = fw_version & 0x3ff;
	g_node_prop[1].core->EngineId.ui32.Major = 0;
	g_node_prop[1].core->EngineId.ui32.Minor = 0;
	g_node_prop[1].core->EngineId.ui32.Stepping = 0;

    mem_banks = g_node_prop[1].core->NumMemoryBanks;
	assert(mem_banks == 1);
    g_node_prop[1].mem = new HsaMemoryProperties[mem_banks];

    g_node_prop[1].mem[0].HeapType = HSA_HEAPTYPE_SYSTEM;
    g_node_prop[1].mem[0].SizeInBytes = 0x80000000ull;  // 512M
    g_node_prop[1].mem[0].Flags.MemoryProperty = 0u;
    g_node_prop[1].mem[0].Width = 64ull;
    g_node_prop[1].mem[0].MemoryClockMax = 100000u;

    num_caches = g_node_prop[1].core->NumCaches;
	assert(num_caches >= 1);
    g_node_prop[1].cache = new HsaCacheProperties[num_caches];
    g_node_prop[1].cache[0].ProcessorIdLow = 1u;
    g_node_prop[1].cache[0].CacheLevel = 2;
    g_node_prop[1].cache[0].CacheSize = 2048000;
    g_node_prop[1].cache[0].CacheLineSize = 64;
    g_node_prop[1].cache[0].CacheLinesPerTag = 4u;
    g_node_prop[1].cache[0].CacheAssociativity = 1u;
    g_node_prop[1].cache[0].CacheLatency = 100u;
    g_node_prop[1].cache[0].CacheType.Value = 8u;
    g_node_prop[1].cache[0].SiblingMap[0] = 1u;

    num_links = g_node_prop[0].core->NumIOLinks;
    if (num_links > 0) {
        g_node_prop[1].link = new HsaIoLinkProperties[num_links];
        g_node_prop[1].link[0].IoLinkType = HSA_IOLINKTYPE_PCIEXPRESS;
	    g_node_prop[1].link[0].VersionMajor = (uint32_t)1;
	    g_node_prop[1].link[0].VersionMinor = (uint32_t)0;
	    g_node_prop[1].link[0].NodeFrom = (uint32_t)0;
	    g_node_prop[1].link[0].NodeTo = (uint32_t)0;
	    g_node_prop[1].link[0].Weight = (uint32_t)1;
	    g_node_prop[1].link[0].MinimumLatency = (uint32_t)500;   // in ns
	    g_node_prop[1].link[0].MaximumLatency = (uint32_t)2000;
	    g_node_prop[1].link[0].MinimumBandwidth = (uint32_t)1000;  // in MB/s
	    g_node_prop[1].link[0].MaximumBandwidth = (uint32_t)10000;
	    g_node_prop[1].link[0].RecTransferSize = (uint32_t)0;
	    g_node_prop[1].link[0].Flags.LinkProperty = (uint32_t)0x1E;   // not allow cc, atmoic, p2p
    }

#if 0
				while (sys_link_id < temp_props[i].node.NumIOLinks &&
					link_id < sys_props.NumNodes - 1) {
					ret = topology_sysfs_get_iolink_props(i, sys_link_id++,
									      &temp_props[i].link[link_id]);
					if (ret == HSAKMT_STATUS_NOT_SUPPORTED) {
						ret = HSAKMT_STATUS_SUCCESS;
						continue;
					} else if (ret != HSAKMT_STATUS_SUCCESS) {
						free_properties(temp_props, i + 1);
						goto err;
					}
					link_id++;
				}
#endif

	/* All direct IO links are created in the kernel. Here we need to
	 * connect GPU<->GPU or GPU<->CPU indirect IO links.
	 */

	// TODO topology_create_indirect_gpu_links(&sys_props, temp_nodes);

    // process aperture for gpu
    g_process_apertures[0].gpu_id = 1;
    g_process_apertures[0].lds_base = MAKE_LDS_APP_BASE();
    g_process_apertures[0].lds_limit = MAKE_LDS_APP_LIMIT(g_process_apertures[0].lds_base);

    g_process_apertures[0].gpuvm_base = SVM_USER_BASE; // MAKE_GPUVM_APP_BASE(g_process_apertures[0].gpu_id);
    g_process_apertures[0].gpuvm_limit = SVM_MIN_VM_SIZE*2; // MAKE_GPUVM_APP_LIMIT(g_process_apertures[0].gpuvm_base, 1 << 47);

    g_process_apertures[0].scratch_base = MAKE_SCRATCH_APP_BASE();
    g_process_apertures[0].scratch_limit = MAKE_SCRATCH_APP_LIMIT(g_process_apertures[0].scratch_base);


    // process aperture for apu
    g_process_apertures[1].gpu_id = 0;
    g_process_apertures[1].lds_base = MAKE_LDS_APP_BASE();
    g_process_apertures[1].lds_limit = MAKE_LDS_APP_LIMIT(g_process_apertures[0].lds_base);

    g_process_apertures[1].scratch_base = MAKE_SCRATCH_APP_BASE();
    g_process_apertures[1].scratch_limit = MAKE_SCRATCH_APP_LIMIT(g_process_apertures[0].scratch_base);

	g_process_apertures[1].gpuvm_base = SVM_USER_BASE;
	//process_apertures->gpuvm_limit = dev->shared_resources.gpuvm_size - 1;
	g_process_apertures[1].gpuvm_limit = SVM_MIN_VM_SIZE * 2;   // 8GB
	// process_apertures->qpd.cwsr_base = SVM_CWSR_BASE;
	// process_apertures->qpd.ib_base = SVM_IB_BASE;


    // args->handle = (void*)g_cmdio;
    args->handle = (uint64_t)g_cmdio;
    return 0;
}


int cmd_create_queue(ioctl_create_queue_args *args){
    g_cmdio->CreateQueue(*args);
    return 0;
}

int cmd_acquire_vm(ioctl_acquire_vm_args *args){
    g_cmdio->InitVM();
    return 0;
}

int cmd_alloc_memory(ioctl_alloc_memory_args *args){
    uint64_t size = args->size;
    uint64_t mmap_offset = args->mmap_offset;

    g_cmdio->AllocMemory(*args);
	// void *mem = (void*)args->va_addr;
    // g_cmdio->MapVA(args->handle);

    return 0;
}

int cmd_free_memory(ioctl_free_memory_args *args){

    g_cmdio->FreeMemory(*args);
    return 0;
}

int cmd_get_system_prop(ioctl_get_system_prop_args *args){
    // args->num_caches = num_cpu_caches;
    args->num_cpus = num_cpus;
    args->num_nodes = num_nodes;
    args->cpuinfo = &g_cpuinfo[0];
    args->cacheinfo = &g_cpu_cacheinfo[0];
    args->sys_prop = &g_sys_prop;
    args->node_prop = &g_node_prop[0];
    return 0;
}

int cmd_open_drm(ioctl_open_drm_args *args){
    // args->num_caches = num_cpu_caches;
    args->drm_fd = args->drm_render_minor + 1;
    return 0;
/*
	char path[128];
	sprintf(path, "/dev/dri/renderD%d", minor);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		if (errno != ENOENT && errno != EPERM) {
			pr_err("Failed to open %s: %s\n", path, strerror(errno));
			if (errno == EACCES)
				pr_info("Check user is in \"video\" group\n");
		}
		return -errno;
	}
*/
}

int cmd_get_process_apertures(ioctl_get_process_apertures_args *args){

    args->num_of_nodes = 2;
    args->process_device_apertures_ptr = (uint64_t)&g_process_apertures[0];

    return 0;
}

int cmd_mmap(ioctl_mmap_args *args){

    // TODD, we need figure out fd to different gpu_mem_id;s drm_fd
    //  args->fd = -1 is kfd_fd otherwise is drm_fd
    if (args->fd == - 1) {
        void* ret = mmap((void*)args->start, args->length, args->prot,
            args->flags | MAP_ANONYMOUS, args->fd,
            args->offset);
	    if (ret == MAP_FAILED) {
            return 1;
        }
        args->start = (uint64_t)ret;
    }
    return 0;
}

int cmd_map_memory_to_gpu(ioctl_map_memory_to_gpu_args *args){

    uint64_t handle = args->handle;

    g_cmdio->MapVA(handle);

    /*
    void* ret = mmap((void*)args->start, args->length, args->prot,
             args->flags, args->fd,
             args->offset);
	if (ret == MAP_FAILED) {
        return 1;
    }
    */
    return 0;
}

int cmd_set_scratch_backing_va(ioctl_set_scratch_backing_va_args *args) {

    // uint64_t handle = args->handle;
    // TODO
    // i guess, it shouild allocate system mem and ma
    // need to check kfd

    // g_cmdio->MapVA(handle);

    /*
    void* ret = mmap((void*)args->start, args->length, args->prot,
             args->flags, args->fd,
             args->offset);
	if (ret == MAP_FAILED) {
        return 1;
    }
    */
    return 0;
}

