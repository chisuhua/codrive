#include "opu_ip_mc.h"

void opu_mc_get_pde_for_mo(struct opu_mo *mo, int level,
                    uint64_t *addr, uint64_t *flags)
{
    if (!mo->is_dev_mem)
        *addr = mo->dma_addr[0];
    else
        *addr = opu_mem_obj_get_addr(mo, 0);

    OPU_MARK_UNUSED(level);
    OPU_MARK_UNUSED(level);
}

void opu_mc_set_pt_pd_entry(struct opu_device *odev, void *cpu_pt_addr,
                    uint32_t gpu_page_idx, uint64_t addr, uint64_t flags)
{
    void __iommu *ptr = (void*)cpu_pt_addr;
    uint64_t value = addr & 0x0000FFFFFFFFF000ULL;

    value |= flags;
    writeq(value, ptr + (gpu_page_idx * 8));
    return 0;
}


