#ifndef __OPU_IP_MC_H__
#define __OPU_IP_MC_H__

#include <linux/types.h>

struct opu_device;
struct opu_mo;

void opu_mc_get_pde_for_mo(struct opu_mo *mo, int level,
                    uint64_t *addr, uint64_t *flags);

void opu_mc_set_pt_pd_entry(struct opu_device *odev, void *cpu_pt_addr,
                    uint32_t gpu_page_idx, uint64_t addr, uint64_t flags);

#endif
