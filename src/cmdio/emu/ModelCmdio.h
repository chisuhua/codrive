#pragma once

#include <iostream>
#include <bitset>
#include "utils/pattern/Singleton.h"
#include "inc/RegDef.h"
#include "inc/Mmu.h"
#include "../cmdio.h"
#include "inc/pps.h"
#include "util/simple_heap.h"
#include "util/small_heap.h"
#include "ProcessVM.h"
#include <map>

// #define PD0_PAGE_SHIFT (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM + PD1_ADDR_BIT_NUM + PD0_ADDR_BIT_NUM)
#define PD0_BIT_SHIFT (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM + PD1_ADDR_BIT_NUM)
#define PD1_BIT_SHIFT (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM + PDE_ADDR_BIT_NUM )
#define PDE_BIT_SHIFT (PAGE_4KB_BIT_NUM + PTE_ADDR_BIT_NUM )
#define PTE_BIT_SHIFT (PAGE_4KB_BIT_NUM)

class ModelCmdio // : public cmdio
{
public:

    virtual void RegWrite(uint32_t index, uint32_t value);
    virtual void RegRead(uint32_t index, uint32_t &value);
    virtual bool Sync();

    virtual ~ModelCmdio();
    ModelCmdio();

    virtual void CreateQueue(ioctl_create_queue_args &args);
    virtual void AllocMemory(ioctl_alloc_memory_args &args);
    virtual void FreeMemory(ioctl_free_memory_args &args);
    void MapVA(uint64_t handle);
    void*    BlockAlloc(size_t bsize) ;
    void InitVM() ;

private:
    void init_queue(uint32_t size);
    void clear_queue(void);


private:
    void* m_model;
    std::vector<uint32_t> queue_list;

    uint64_t m_mmu_dev_pa_size = 0x80000000ull;
    uint64_t m_mmu_dev_pa_top;
    uint64_t m_mmu_dev_pa_base;
    uint64_t m_mmu_dev_pa_offset;

    uint64_t m_mmu_dev_va_size;
    uint64_t m_mmu_dev_va_top;
    uint64_t m_mmu_dev_va_base;


    uint64_t m_bar_mem_write;

    uint32_t m_mmu_fragment_en = 0;
    uint32_t m_mmu_physical_mode = 1;
/*
    uint64_t m_pd0_base_addr;
    uint64_t m_pd1_base_addr;
    uint64_t m_pde_base_addr;
    uint64_t m_pte_base_addr;
*/

    uint32_t m_pt_pte_bit = PTE_ADDR_BIT_NUM;
    uint32_t m_pt_pde_bit = PDE_ADDR_BIT_NUM;
    uint32_t m_pt_pd1_bit = PD1_ADDR_BIT_NUM;
    uint32_t m_pt_pd0_bit = PD0_ADDR_BIT_NUM;

    static const size_t kPAGE_4KB_SIZE_ = PAGE_4KB_SIZE;
    bool    m_suballoc_enable = false;

    bool Free(void* address, size_t size) const {
        if (m_fragment_allocator.free(address)) return true;
        return false;
    }

    class BlockAllocator {
    private:
        ModelCmdio& region_;
        static const size_t block_size_ = 2 * 1024 * 1024;  // 2MB blocks.
    public:
        explicit BlockAllocator(ModelCmdio& region) : region_(region) {}
        void* alloc(size_t request_size, size_t& allocated_size) const;
        void free(void* ptr, size_t length) const { region_.Free(ptr, length); }
        size_t block_size() const { return block_size_; }
    };

    mutable SimpleHeap<BlockAllocator> m_fragment_allocator;

    SmallHeap   m_dev_heap;

    std::map<uint64_t, ioctl_alloc_memory_args> m_allocated_bo;
    std::map<int, ProcessVM*> m_process_vm;
    int  m_vmid = 0;


    /*
    uint64_t  svm_base;
    uint32_t  svm_limit;
    */
};



