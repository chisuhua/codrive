#pragma once
#include "ModelInterface.h"
#include "inc/Mmu.h"
#include "util/utils.h"
#include <memory.h>
class ModelCmdio;

class ProcessVM {
public:
    ProcessVM(ModelCmdio* ctx, uint64_t pt_base_addr, void* model, uint64_t mmio_bar, uint64_t dev_va_base)
        : ctx_(ctx)
        , pt_base_addr_(pt_base_addr)
        , model_(model)
        , mmio_bar_(mmio_bar)
        , dev_va_base_(dev_va_base)
    {
        pd0_page_ = (PDE*)malloc(PAGE_4KB_SIZE*2);
        pd1_page_ = (PDE*)malloc(PAGE_4KB_SIZE);
        pde_page_ = (PDE*)malloc(PAGE_4KB_SIZE);
        pte_page_ = (PTE*)malloc(PAGE_4KB_SIZE);
        memset(pd0_page_, 0, PAGE_4KB_SIZE*2);
        memset(pd1_page_, 0, PAGE_4KB_SIZE);
        memset(pde_page_, 0, PAGE_4KB_SIZE);
        memset(pte_page_, 0, PAGE_4KB_SIZE);
        page_entry_num_ =  PAGE_4KB_SIZE/sizeof(PTE);

        // setup pt_base_addr and 2 pd0 page
        write_mmio(model_, pt_base_addr_, pd0_page_, PAGE_4KB_SIZE*2, mmio_bar_);

        printf("[MMU Info]: setup table for at %llx\n", pt_base_addr_);
    };

    ~ProcessVM() {};

    void mmap(uint64_t va_base, uint64_t va_size, uint64_t pa_base, int readable, int writeable, int executable, int valid, int system) {
        // std::lock_guard
        uint64_t va_start = AlignDown(va_base, PAGE_4KB_SIZE);
        uint64_t va_end = AlignUp(va_base + va_size - 1, PAGE_4KB_SIZE) - 1;
        uint64_t pa_start = AlignDown(pa_base, PAGE_4KB_SIZE);
        // if user passe pa is 0, will allocate pa address
        if (pa_base == 0) malloc_pa_ = true;
        readable_ = readable;
        writeable_ = writeable;
        executable_ = executable;
        valid_ = valid;
        system_ = system;

        va_start -= dev_va_base_;
        va_end -= dev_va_base_;

        SetupPd0Page(va_start, va_end, pa_start);
    }

    void SetupPd0Page(uint64_t va_start, uint64_t va_end, uint64_t pa_start) ;
    void SetupPd1Page(uint64_t pd1_page_va_base, uint64_t pd1_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page);
    void SetupPdePage(uint64_t pde_page_va_base, uint64_t pde_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page);
    void SetupPtePage(uint64_t pte_page_va_base, uint64_t pte_page_va_top, uint64_t va_start, uint64_t va_end, uint64_t pa_start, uint64_t page_address, bool new_page);


    public:
    ModelCmdio* ctx_;
    uint64_t pt_base_addr_;
    int    vmid;
    void* model_;
    int     mmio_bar_;
    uint64_t dev_va_base_;
    bool  mutex;
    int   readable_;
    int   writeable_;
    int   executable_;
    int   valid_;
    int   system_;
    PDE*  pd0_page_;
    PDE*  pd1_page_;
    PDE*  pde_page_;
    PTE*  pte_page_;
    int   page_entry_num_;
    bool  malloc_pa_;
};
