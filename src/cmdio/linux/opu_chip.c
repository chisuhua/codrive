#include "opu_chip.h"
#include "opu.h"

#include "core_v1_0.h"
#include "dma_v1_0.h"
#include "mc_v1_0.h"
#include "topo_v1_0.h"

#define EFUSE 0

static int opu_chip_reset(struct opu_device *odev,
        enum opu_chip_reset_type type)
{
#ifdef QEMU
    int pos = 0;
    unsigned short devctl = 0;

    DRM_DEBUG("Chip device exit reset ...\n");
    pos = pci_find_capability(odev->pdev, PCI_CAP_ID_EXP);
    if (!pos) {
        DRM_ERROR("Device doesn't support PCIe capabliity, skip reset\n");
        return -ENOENT;
    }

    pci_read_config_word(odev->pdev, )
    devctl != PCI_EXP_DEVCTL_BCR_FLR;
    pci_write_config_word(odev->pdev, pos + PCI_EXP_DEVCTL, devctl);

    DRM_DEBUG("Chip device exit reset ...\n");
#else
    DRM_WARN("Chip reset not implemented!\n");
#endif
    return 0;
}

static int opu_chip_heart_beat(struct opu_device *odev)
{
    /* add heart beat to PF via mail box */
    return 0;
}

static uint8_t opu_chip_common_init(void *handle)
{
    int err = 0;
    struct opu_device *odev = (struct opu_device*) handle;

    odev->chip_funcs.heat_beat = opu_chip_heart_beat;
    odev->chip_funcs.chip_reset = opu_chip_reset;

    err = odev->chip_funcs.chip_reset(odev, OPU_FULL_RESET);
    if (err)
        DRM_ERROR("OPU chip reset failed (%d)!\n", err);

}

static uint8_t opu_chip_common_reinit(void *handle)
{
    int err = 0;
    struct opu_device *odev = (struct opu_device*) handle;
    return err;
}

static uint8_t opu_chip_common_fini(void *handle)
{
    int err = 0;
    struct opu_device *odev = (struct opu_device*) handle;
    return err;
}

const struct opu_ip_funcs chip_common_funcs = {
    .init = opu_chip_common_init,
    .fini = opu_chip_common_fini,
    .reinit = opu_chip_common_reinit
};

static void chip_funcs_init_dummy(struct opu_device *odev)
{
}


int opu_chip_setup(void *handle)
{
    int err = 0;
    struct opu_device *odev = (struct opu_device *)handle;

    if (odev->chip_type != OPU_CHIP) {
        DRM_ERROR("chip type invalide");
        return -EINVAL
    }

    odev->ips[odev->num_ips].type = OPU_COMMON;
    odev->ips[odev->num_ips].funcs = &chip_common_funcs;
    odev->num_ips++;

    odev->ips[odev->num_ips].type = OPU_IRQ;
#ifdef QEMU
    odev->ips[odev->num_ips].funcs = &opu_irq_funcs; // ip_irq_ppu_funcs and opu_interrupt
#else
    odev->ips[odev->num_ips].funcs = &opu_irq_funcs;
#endif
    odev->ips_type2idx[OPU_IRQ] = odev->num_ips;
    odev->num_ips++;

    odev->ips[odev->num_ips].type = OPU_CORE;
    odev->ips[odev->num_ips].funcs = &core_v1_0_funcs;
    odev->ips_type2idx[OPU_CORE] = odev->num_ips;
    odev->num_ips++;

    chip_funcs_init_dummy(odev);
    return err;
}

