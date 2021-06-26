#include "ProcessVM.h"
#include "ModelCmdio.h"
#include "inc/Mmu.h"

void ProcessVM::SetupPd0Page(uint64_t va_start, uint64_t va_end, uint64_t pa_start) {

    uint64_t pd0_start = va_start >> PD0_BIT_SHIFT;
    uint64_t pd0_end   = va_end >> PD0_BIT_SHIFT;

    // host pd0_page_ is always same as gpu memory
    PDE *pde;
    for (uint64_t i = pd0_start; i <= pd0_end; ++i) {
        pde = &pd0_page_[i];

        pde->info.system = system_;
        pde->info.executable = executable_;
        pde->info.readable = readable_;
        pde->info.writeable = writeable_;
        pde->info.fragment = 0;
        pde->info.pte = 0;
        pde->info.pagesize = 0;
        pde->info.level = 0;

        bool new_page = false;

        if (pde->info.valid == 0) {
            uint64_t pd1_page = (uint64_t)ctx_->BlockAlloc(PAGE_4KB_SIZE);
            pde->info.address = pd1_page >> PAGE_4KB_BIT_NUM;
            new_page = true;
        }

        uint64_t pd1_page_va_base = i << PD0_BIT_SHIFT;
        uint64_t pd1_page_va_top  = ((i + 1) << PD0_BIT_SHIFT) -1 ;
        SetupPd1Page(pd1_page_va_base, pd1_page_va_top, va_start, va_end, pa_start, pde->info.address << PAGE_4KB_BIT_NUM, new_page);
        pde->info.valid = valid_;
    }

    write_mmio(model_, pt_base_addr_ + pd0_start * sizeof(PDE), pd0_page_ + pd0_start * sizeof(PDE), (pd0_end - pd0_start + 1) * sizeof(PDE), mmio_bar_);
}

void ProcessVM::SetupPd1Page(uint64_t pd1_page_va_base, uint64_t pd1_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page) {

    if ( pd1_page_va_base > va_start ) {
        pa_start += pd1_page_va_base - va_start;
        va_start = pd1_page_va_base;
    }
    if ( pd1_page_va_top < va_end ) {
        va_end = pd1_page_va_top;
    }

    uint64_t pd1_start = va_start >> PD1_BIT_SHIFT;
    uint64_t pd1_end   = va_end >> PD1_BIT_SHIFT;

    if (!new_page) {
        read_mmio(model_, page_address, pd1_page_, PAGE_4KB_SIZE, mmio_bar_);
    } else {
        memset(pd1_page_, 0, PAGE_4KB_SIZE);
    }

    PDE *pde;
    int page_entry_num =  PAGE_4KB_SIZE/sizeof(PDE);
    for (uint64_t i = pd1_start; i <= pd1_end; ++i) {
        uint64_t entry_in_page = i % page_entry_num;
        pde = &pd1_page_[entry_in_page];

        pde->info.system = system_;
        pde->info.executable = executable_;
        pde->info.readable = readable_;
        pde->info.writeable = writeable_;
        pde->info.fragment = 0;
        pde->info.pte = 0;
        pde->info.pagesize = 0;
        pde->info.level = 1;
        bool new_page = false;

        if (pde->info.valid == 0) {
            uint64_t pde_page = (uint64_t)ctx_->BlockAlloc(PAGE_4KB_SIZE);
            pde->info.address = pde_page >> PAGE_4KB_BIT_NUM;
            new_page = true;
        }

        uint64_t pde_page_va_base = pd1_page_va_base + (entry_in_page << PD1_BIT_SHIFT);
        uint64_t pde_page_va_top  = pd1_page_va_base + ((entry_in_page + 1) << PD1_BIT_SHIFT) -1 ;

        SetupPdePage(pde_page_va_base, pde_page_va_top, va_start, va_end, pa_start, pde->info.address << PAGE_4KB_BIT_NUM, new_page);
        pde->info.valid = valid_;
    }

    write_mmio(model_, page_address, pd1_page_, PAGE_4KB_SIZE, mmio_bar_);
}


void ProcessVM::SetupPdePage(uint64_t pde_page_va_base, uint64_t pde_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page) {

    if ( pde_page_va_base > va_start ) {
        pa_start += pde_page_va_base - va_start;
        va_start = pde_page_va_base;
    }
    if ( pde_page_va_top < va_end ) {
        va_end = pde_page_va_top;
    }

    uint64_t pde_start = va_start >> PDE_BIT_SHIFT;
    uint64_t pde_end   = va_end >> PDE_BIT_SHIFT;

    if (!new_page) {
        read_mmio(model_, page_address, pde_page_, PAGE_4KB_SIZE, mmio_bar_);
    } else {
        memset(pde_page_, 0, PAGE_4KB_SIZE);
    }

    PDE *pde;
    int page_entry_num =  PAGE_4KB_SIZE/sizeof(PDE);
    for (uint64_t i = pde_start; i <= pde_end; ++i) {
        uint64_t entry_in_page = i % page_entry_num;
        pde = &pde_page_[entry_in_page];

        pde->info.system = system_;
        pde->info.executable = executable_;
        pde->info.readable = readable_;
        pde->info.writeable = writeable_;
        pde->info.fragment = 0;
        pde->info.pte = 0;
        pde->info.pagesize = 0;
        pde->info.level = 2;
        bool new_page = false;

        if (pde->info.valid == 0) {
            uint64_t pte_page = (uint64_t)ctx_->BlockAlloc(PAGE_4KB_SIZE);
            pde->info.address = pte_page >> PAGE_4KB_BIT_NUM;
            new_page = true;
        }

        uint64_t pte_page_va_base = pde_page_va_base + (entry_in_page << PDE_BIT_SHIFT);
        uint64_t pte_page_va_top  = pde_page_va_base + ((entry_in_page + 1) << PDE_BIT_SHIFT) -1 ;

        SetupPtePage(pte_page_va_base, pte_page_va_top, va_start, va_end, pa_start, pde->info.address << PAGE_4KB_BIT_NUM, new_page);
        pde->info.valid = valid_;
    }

    write_mmio(model_, page_address, pde_page_, PAGE_4KB_SIZE, mmio_bar_);
}

void ProcessVM::SetupPtePage(uint64_t pte_page_va_base, uint64_t pte_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page) {

    if ( pte_page_va_base > va_start ) {
        pa_start += pte_page_va_base - va_start;
        va_start = pte_page_va_base;
    }
    if ( pte_page_va_top < va_end ) {
        va_end = pte_page_va_top;
    }

    uint64_t pte_start = va_start >> PTE_BIT_SHIFT;
    uint64_t pte_end   = va_end >> PTE_BIT_SHIFT;


    if (!new_page) {
        read_mmio(model_, page_address, pte_page_, PAGE_4KB_SIZE, mmio_bar_);
    } else {
        memset(pte_page_, 0, PAGE_4KB_SIZE);
    }

    PTE *pte;
    int page_entry_num =  PAGE_4KB_SIZE/sizeof(PTE);
    for (uint64_t i = pte_start; i <= pte_end; ++i) {
        uint64_t entry_in_page = i % page_entry_num;
        pte = &pte_page_[entry_in_page];

        pte->info.system = system_;
        pte->info.executable = executable_;
        pte->info.readable = readable_;
        pte->info.writeable = writeable_;
        pte->info.fragment = 0;
        pte->info.pagesize = 0;
        pte->info.level = 3;

        if (pte->info.valid == 0 && malloc_pa_) {
            uint64_t pa_page = (uint64_t)ctx_->BlockAlloc(PAGE_4KB_SIZE);
            pte->info.address = pa_page >> PAGE_4KB_BIT_NUM;
        } else {
            pte->info.address = (pa_start >> PAGE_4KB_BIT_NUM) + i - pte_start;
        }
        pte->info.valid = valid_;
    }

    write_mmio(model_, page_address, pte_page_, PAGE_4KB_SIZE, mmio_bar_);
}
