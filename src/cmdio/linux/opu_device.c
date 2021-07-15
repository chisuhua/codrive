
#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "opu_priv.h"
#include "opu_device_queue_manager.h"
#include "opu_pm4_headers_vi.h"
#include "opu_pm4_headers_aldebaran.h"
#include "cwsr_trap_handler.h"
#include "opu_iommu.h"
#include "amdgpu_amdopu.h"
#include "opu_smi_events.h"
#include "opu_migrate.h"

#define MQD_SIZE_ALIGNED 768

/*
 * opu_locked is used to lock the opu driver during suspend or reset
 * once locked, opu driver will stop any further GPU execution.
 * create process (open) will return -EAGAIN.
 */
static atomic_t opu_locked = ATOMIC_INIT(0);

#ifdef CONFIG_DRM_AMDGPU_CIK
extern const struct opu2kgd_calls gfx_v7_opu2kgd;
#endif
extern const struct opu2kgd_calls gfx_v8_opu2kgd;
extern const struct opu2kgd_calls gfx_v9_opu2kgd;
extern const struct opu2kgd_calls arcturus_opu2kgd;
extern const struct opu2kgd_calls aldebaran_opu2kgd;
extern const struct opu2kgd_calls gfx_v10_opu2kgd;
extern const struct opu2kgd_calls gfx_v10_3_opu2kgd;

static const struct opu2kgd_calls *opu2kgd_funcs[] = {
#ifdef OPU_SUPPORT_IOMMU_V2
#ifdef CONFIG_DRM_AMDGPU_CIK
	[CHIP_KAVERI] = &gfx_v7_opu2kgd,
#endif
	[CHIP_CARRIZO] = &gfx_v8_opu2kgd,
	[CHIP_RAVEN] = &gfx_v9_opu2kgd,
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	[CHIP_HAWAII] = &gfx_v7_opu2kgd,
#endif
	[CHIP_TONGA] = &gfx_v8_opu2kgd,
	[CHIP_FIJI] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS10] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS11] = &gfx_v8_opu2kgd,
	[CHIP_POLARIS12] = &gfx_v8_opu2kgd,
	[CHIP_VEGAM] = &gfx_v8_opu2kgd,
	[CHIP_VEGA10] = &gfx_v9_opu2kgd,
	[CHIP_VEGA12] = &gfx_v9_opu2kgd,
	[CHIP_VEGA20] = &gfx_v9_opu2kgd,
	[CHIP_RENOIR] = &gfx_v9_opu2kgd,
	[CHIP_ARCTURUS] = &arcturus_opu2kgd,
	[CHIP_ALDEBARAN] = &aldebaran_opu2kgd,
	[CHIP_NAVI10] = &gfx_v10_opu2kgd,
	[CHIP_NAVI12] = &gfx_v10_opu2kgd,
	[CHIP_NAVI14] = &gfx_v10_opu2kgd,
	[CHIP_SIENNA_CICHLID] = &gfx_v10_3_opu2kgd,
	[CHIP_NAVY_FLOUNDER] = &gfx_v10_3_opu2kgd,
	[CHIP_VANGOGH] = &gfx_v10_3_opu2kgd,
	[CHIP_DIMGREY_CAVEFISH] = &gfx_v10_3_opu2kgd,
	[CHIP_BEIGE_GOBY] = &gfx_v10_3_opu2kgd,
	[CHIP_YELLOW_CARP] = &gfx_v10_3_opu2kgd,
};

#ifdef OPU_SUPPORT_IOMMU_V2
static const struct opu_device_info kaveri_device_info = {
	.asic_family = CHIP_KAVERI,
	.asic_name = "kaveri",
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info carrizo_device_info = {
	.asic_family = CHIP_CARRIZO,
	.asic_name = "carrizo",
	.max_pasid_bits = 16,
	/* max num of queues for CZ.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};
#endif

static const struct opu_device_info raven_device_info = {
	.asic_family = CHIP_RAVEN,
	.asic_name = "raven",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 1,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info hawaii_device_info = {
	.asic_family = CHIP_HAWAII,
	.asic_name = "hawaii",
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info tonga_device_info = {
	.asic_family = CHIP_TONGA,
	.asic_name = "tonga",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = false,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info fiji_device_info = {
	.asic_family = CHIP_FIJI,
	.asic_name = "fiji",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info fiji_vf_device_info = {
	.asic_family = CHIP_FIJI,
	.asic_name = "fiji",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};


static const struct opu_device_info polaris10_device_info = {
	.asic_family = CHIP_POLARIS10,
	.asic_name = "polaris10",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info polaris10_vf_device_info = {
	.asic_family = CHIP_POLARIS10,
	.asic_name = "polaris10",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info polaris11_device_info = {
	.asic_family = CHIP_POLARIS11,
	.asic_name = "polaris11",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info polaris12_device_info = {
	.asic_family = CHIP_POLARIS12,
	.asic_name = "polaris12",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info vegam_device_info = {
	.asic_family = CHIP_VEGAM,
	.asic_name = "vegam",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info vega10_device_info = {
	.asic_family = CHIP_VEGA10,
	.asic_name = "vega10",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info vega10_vf_device_info = {
	.asic_family = CHIP_VEGA10,
	.asic_name = "vega10",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info vega12_device_info = {
	.asic_family = CHIP_VEGA12,
	.asic_name = "vega12",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info vega20_device_info = {
	.asic_family = CHIP_VEGA20,
	.asic_name = "vega20",
	.max_pasid_bits = 16,
	.max_no_of_hqd	= 24,
	.doorbell_size	= 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info arcturus_device_info = {
	.asic_family = CHIP_ARCTURUS,
	.asic_name = "arcturus",
	.max_pasid_bits = 16,
	.max_no_of_hqd	= 24,
	.doorbell_size	= 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 6,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info aldebaran_device_info = {
	.asic_family = CHIP_ALDEBARAN,
	.asic_name = "aldebaran",
	.max_pasid_bits = 16,
	.max_no_of_hqd	= 24,
	.doorbell_size	= 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 3,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info renoir_device_info = {
	.asic_family = CHIP_RENOIR,
	.asic_name = "renoir",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.supports_cwsr = true,
	.needs_iommu_device = false,
	.needs_pci_atomics = false,
	.num_sdma_engines = 1,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info navi10_device_info = {
	.asic_family = CHIP_NAVI10,
	.asic_name = "navi10",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info navi12_device_info = {
	.asic_family = CHIP_NAVI12,
	.asic_name = "navi12",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info navi14_device_info = {
	.asic_family = CHIP_NAVI14,
	.asic_name = "navi14",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info sienna_cichlid_device_info = {
	.asic_family = CHIP_SIENNA_CICHLID,
	.asic_name = "sienna_cichlid",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 4,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info navy_flounder_device_info = {
	.asic_family = CHIP_NAVY_FLOUNDER,
	.asic_name = "navy_flounder",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info vangogh_device_info = {
	.asic_family = CHIP_VANGOGH,
	.asic_name = "vangogh",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 1,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

static const struct opu_device_info dimgrey_cavefish_device_info = {
	.asic_family = CHIP_DIMGREY_CAVEFISH,
	.asic_name = "dimgrey_cavefish",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 2,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info beige_goby_device_info = {
	.asic_family = CHIP_BEIGE_GOBY,
	.asic_name = "beige_goby",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
	.num_sdma_engines = 1,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 8,
};

static const struct opu_device_info yellow_carp_device_info = {
	.asic_family = CHIP_YELLOW_CARP,
	.asic_name = "yellow_carp",
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.needs_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = false,
	.num_sdma_engines = 1,
	.num_xgmi_sdma_engines = 0,
	.num_sdma_queues_per_engine = 2,
};

/* For each entry, [0] is regular and [1] is virtualisation device. */
static const struct opu_device_info *opu_supported_devices[][2] = {
#ifdef OPU_SUPPORT_IOMMU_V2
	[CHIP_KAVERI] = {&kaveri_device_info, NULL},
	[CHIP_CARRIZO] = {&carrizo_device_info, NULL},
#endif
	[CHIP_RAVEN] = {&raven_device_info, NULL},
	[CHIP_HAWAII] = {&hawaii_device_info, NULL},
	[CHIP_TONGA] = {&tonga_device_info, NULL},
	[CHIP_FIJI] = {&fiji_device_info, &fiji_vf_device_info},
	[CHIP_POLARIS10] = {&polaris10_device_info, &polaris10_vf_device_info},
	[CHIP_POLARIS11] = {&polaris11_device_info, NULL},
	[CHIP_POLARIS12] = {&polaris12_device_info, NULL},
	[CHIP_VEGAM] = {&vegam_device_info, NULL},
	[CHIP_VEGA10] = {&vega10_device_info, &vega10_vf_device_info},
	[CHIP_VEGA12] = {&vega12_device_info, NULL},
	[CHIP_VEGA20] = {&vega20_device_info, NULL},
	[CHIP_RENOIR] = {&renoir_device_info, NULL},
	[CHIP_ARCTURUS] = {&arcturus_device_info, &arcturus_device_info},
	[CHIP_ALDEBARAN] = {&aldebaran_device_info, &aldebaran_device_info},
	[CHIP_NAVI10] = {&navi10_device_info, NULL},
	[CHIP_NAVI12] = {&navi12_device_info, &navi12_device_info},
	[CHIP_NAVI14] = {&navi14_device_info, NULL},
	[CHIP_SIENNA_CICHLID] = {&sienna_cichlid_device_info, &sienna_cichlid_device_info},
	[CHIP_NAVY_FLOUNDER] = {&navy_flounder_device_info, &navy_flounder_device_info},
	[CHIP_VANGOGH] = {&vangogh_device_info, NULL},
	[CHIP_DIMGREY_CAVEFISH] = {&dimgrey_cavefish_device_info, &dimgrey_cavefish_device_info},
	[CHIP_BEIGE_GOBY] = {&beige_goby_device_info, &beige_goby_device_info},
	[CHIP_YELLOW_CARP] = {&yellow_carp_device_info, NULL},
};

static int opu_gtt_sa_init(struct opu_dev *opu, unsigned int buf_size,
				unsigned int chunk_size);
static void opu_gtt_sa_fini(struct opu_dev *opu);

static int opu_resume(struct opu_dev *opu);

struct opu_dev *kgd2opu_probe(struct kgd_dev *kgd,
	struct pci_dev *pdev, unsigned int asic_type, bool vf)
{
	struct opu_dev *opu;
	const struct opu_device_info *device_info;
	const struct opu2kgd_calls *f2g;

	if (asic_type >= sizeof(opu_supported_devices) / (sizeof(void *) * 2)
		|| asic_type >= sizeof(opu2kgd_funcs) / sizeof(void *)) {
		dev_err(opu_device, "asic_type %d out of range\n", asic_type);
		return NULL; /* asic_type out of range */
	}

	device_info = opu_supported_devices[asic_type][vf];
	f2g = opu2kgd_funcs[asic_type];

	if (!device_info || !f2g) {
		dev_err(opu_device, "%s %s not supported in opu\n",
			amdgpu_asic_name[asic_type], vf ? "VF" : "");
		return NULL;
	}

	opu = kzalloc(sizeof(*opu), GFP_KERNEL);
	if (!opu)
		return NULL;

	/* Allow BIF to recode atomics to PCIe 3.0 AtomicOps.
	 * 32 and 64-bit requests are possible and must be
	 * supported.
	 */
	opu->pci_atomic_requested = amdgpu_amdopu_have_atomics_support(kgd);
	if (device_info->needs_pci_atomics &&
	    !opu->pci_atomic_requested) {
		dev_info(opu_device,
			 "skipped device %x:%x, PCI rejects atomics\n",
			 pdev->vendor, pdev->device);
		kfree(opu);
		return NULL;
	}

	opu->kgd = kgd;
	opu->device_info = device_info;
	opu->pdev = pdev;
	opu->init_complete = false;
	opu->opu2kgd = f2g;
	atomic_set(&opu->compute_profile, 0);

	mutex_init(&opu->doorbell_mutex);
	memset(&opu->doorbell_available_index, 0,
		sizeof(opu->doorbell_available_index));

	atomic_set(&opu->sram_ecc_flag, 0);

	ida_init(&opu->doorbell_ida);

	return opu;
}

static void opu_cwsr_init(struct opu_dev *opu)
{
	if (cwsr_enable && opu->device_info->supports_cwsr) {
		if (opu->device_info->asic_family < CHIP_VEGA10) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx8_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx8_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx8_hex);
		} else if (opu->device_info->asic_family == CHIP_ARCTURUS) {
			BUILD_BUG_ON(sizeof(cwsr_trap_arcturus_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_arcturus_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_arcturus_hex);
		} else if (opu->device_info->asic_family == CHIP_ALDEBARAN) {
			BUILD_BUG_ON(sizeof(cwsr_trap_aldebaran_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_aldebaran_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_aldebaran_hex);
		} else if (opu->device_info->asic_family < CHIP_NAVI10) {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx9_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx9_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx9_hex);
		} else if (opu->device_info->asic_family < CHIP_SIENNA_CICHLID) {
			BUILD_BUG_ON(sizeof(cwsr_trap_nv1x_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_nv1x_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_nv1x_hex);
		} else {
			BUILD_BUG_ON(sizeof(cwsr_trap_gfx10_hex) > PAGE_SIZE);
			opu->cwsr_isa = cwsr_trap_gfx10_hex;
			opu->cwsr_isa_size = sizeof(cwsr_trap_gfx10_hex);
		}

		opu->cwsr_enabled = true;
	}
}

static int opu_gws_init(struct opu_dev *opu)
{
	int ret = 0;

	if (opu->dqm->sched_policy == OPU_SCHED_POLICY_NO_HWS)
		return 0;

	if (hws_gws_support
		|| (opu->device_info->asic_family == CHIP_VEGA10
			&& opu->mec2_fw_version >= 0x81b3)
		|| (opu->device_info->asic_family >= CHIP_VEGA12
			&& opu->device_info->asic_family <= CHIP_RAVEN
			&& opu->mec2_fw_version >= 0x1b3)
		|| (opu->device_info->asic_family == CHIP_ARCTURUS
			&& opu->mec2_fw_version >= 0x30)
		|| (opu->device_info->asic_family == CHIP_ALDEBARAN
			&& opu->mec2_fw_version >= 0x28))
		ret = amdgpu_amdopu_alloc_gws(opu->kgd,
				amdgpu_amdopu_get_num_gws(opu->kgd), &opu->gws);

	return ret;
}

static void opu_smi_init(struct opu_dev *dev) {
	INIT_LIST_HEAD(&dev->smi_clients);
	spin_lock_init(&dev->smi_lock);
}

bool kgd2opu_device_init(struct opu_dev *opu,
			 struct drm_device *ddev,
			 const struct kgd2opu_shared_resources *gpu_resources)
{
	unsigned int size, map_process_packet_size;

	opu->ddev = ddev;
	opu->mec_fw_version = amdgpu_amdopu_get_fw_version(opu->kgd,
			KGD_ENGINE_MEC1);
	opu->mec2_fw_version = amdgpu_amdopu_get_fw_version(opu->kgd,
			KGD_ENGINE_MEC2);
	opu->sdma_fw_version = amdgpu_amdopu_get_fw_version(opu->kgd,
			KGD_ENGINE_SDMA1);
	opu->shared_resources = *gpu_resources;

	opu->vm_info.first_vmid_opu = ffs(gpu_resources->compute_vmid_bitmap)-1;
	opu->vm_info.last_vmid_opu = fls(gpu_resources->compute_vmid_bitmap)-1;
	opu->vm_info.vmid_num_opu = opu->vm_info.last_vmid_opu
			- opu->vm_info.first_vmid_opu + 1;

	/* Verify module parameters regarding mapped process number*/
	if ((hws_max_conc_proc < 0)
			|| (hws_max_conc_proc > opu->vm_info.vmid_num_opu)) {
		dev_err(opu_device,
			"hws_max_conc_proc %d must be between 0 and %d, use %d instead\n",
			hws_max_conc_proc, opu->vm_info.vmid_num_opu,
			opu->vm_info.vmid_num_opu);
		opu->max_proc_per_quantum = opu->vm_info.vmid_num_opu;
	} else
		opu->max_proc_per_quantum = hws_max_conc_proc;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_queues_per_device *
			opu->device_info->mqd_size_aligned;

	/*
	 * calculate max size of runlist packet.
	 * There can be only 2 packets at once
	 */
	map_process_packet_size =
			opu->device_info->asic_family == CHIP_ALDEBARAN ?
				sizeof(struct pm4_mes_map_process_aldebaran) :
					sizeof(struct pm4_mes_map_process);
	size += (OPU_MAX_NUM_OF_PROCESSES * map_process_packet_size +
		max_num_of_queues_per_device * sizeof(struct pm4_mes_map_queues)
		+ sizeof(struct pm4_mes_runlist)) * 2;

	/* Add size of HIQ & DIQ */
	size += OPU_KERNEL_QUEUE_SIZE * 2;

	/* add another 512KB for all other allocations on gart (HPD, fences) */
	size += 512 * 1024;

	if (amdgpu_amdopu_alloc_gtt_mem(
			opu->kgd, size, &opu->gtt_mem,
			&opu->gtt_start_gpu_addr, &opu->gtt_start_cpu_ptr,
			false)) {
		dev_err(opu_device, "Could not allocate %d bytes\n", size);
		goto alloc_gtt_mem_failure;
	}

	dev_info(opu_device, "Allocated %d bytes on gart\n", size);

	/* Initialize GTT sa with 512 byte chunk size */
	if (opu_gtt_sa_init(opu, size, 512) != 0) {
		dev_err(opu_device, "Error initializing gtt sub-allocator\n");
		goto opu_gtt_sa_init_error;
	}

	if (opu_doorbell_init(opu)) {
		dev_err(opu_device,
			"Error initializing doorbell aperture\n");
		goto opu_doorbell_error;
	}

	opu->hive_id = amdgpu_amdopu_get_hive_id(opu->kgd);

	opu->noretry = amdgpu_amdopu_get_noretry(opu->kgd);

	if (opu_interrupt_init(opu)) {
		dev_err(opu_device, "Error initializing interrupts\n");
		goto opu_interrupt_error;
	}

	opu->dqm = device_queue_manager_init(opu);
	if (!opu->dqm) {
		dev_err(opu_device, "Error initializing queue manager\n");
		goto device_queue_manager_error;
	}

	/* If supported on this device, allocate global GWS that is shared
	 * by all OPU processes
	 */
	if (opu_gws_init(opu)) {
		dev_err(opu_device, "Could not allocate %d gws\n",
			amdgpu_amdopu_get_num_gws(opu->kgd));
		goto gws_error;
	}

	/* If CRAT is broken, won't set iommu enabled */
	opu_double_confirm_iommu_support(opu);

	if (opu_iommu_device_init(opu)) {
		dev_err(opu_device, "Error initializing iommuv2\n");
		goto device_iommu_error;
	}

	opu_cwsr_init(opu);

	svm_migrate_init((struct amdgpu_device *)opu->kgd);

	if (opu_resume(opu))
		goto opu_resume_error;

	opu->dbgmgr = NULL;

	if (opu_topology_add_device(opu)) {
		dev_err(opu_device, "Error adding device to topology\n");
		goto opu_topology_add_device_error;
	}

	opu_smi_init(opu);

	opu->init_complete = true;
	dev_info(opu_device, "added device %x:%x\n", opu->pdev->vendor,
		 opu->pdev->device);

	pr_debug("Starting opu with the following scheduling policy %d\n",
		opu->dqm->sched_policy);

	goto out;

opu_topology_add_device_error:
opu_resume_error:
device_iommu_error:
gws_error:
	device_queue_manager_uninit(opu->dqm);
device_queue_manager_error:
	opu_interrupt_exit(opu);
opu_interrupt_error:
	opu_doorbell_fini(opu);
opu_doorbell_error:
	opu_gtt_sa_fini(opu);
opu_gtt_sa_init_error:
	amdgpu_amdopu_free_gtt_mem(opu->kgd, opu->gtt_mem);
alloc_gtt_mem_failure:
	if (opu->gws)
		amdgpu_amdopu_free_gws(opu->kgd, opu->gws);
	dev_err(opu_device,
		"device %x:%x NOT added due to errors\n",
		opu->pdev->vendor, opu->pdev->device);
out:
	return opu->init_complete;
}

void kgd2opu_device_exit(struct opu_dev *opu)
{
	if (opu->init_complete) {
		svm_migrate_fini((struct amdgpu_device *)opu->kgd);
		device_queue_manager_uninit(opu->dqm);
		opu_interrupt_exit(opu);
		opu_topology_remove_device(opu);
		opu_doorbell_fini(opu);
		ida_destroy(&opu->doorbell_ida);
		opu_gtt_sa_fini(opu);
		amdgpu_amdopu_free_gtt_mem(opu->kgd, opu->gtt_mem);
		if (opu->gws)
			amdgpu_amdopu_free_gws(opu->kgd, opu->gws);
	}

	kfree(opu);
}

int kgd2opu_pre_reset(struct opu_dev *opu)
{
	if (!opu->init_complete)
		return 0;

	opu_smi_event_update_gpu_reset(opu, false);

	opu->dqm->ops.pre_reset(opu->dqm);

	kgd2opu_suspend(opu, false);

	opu_signal_reset_event(opu);
	return 0;
}

/*
 * Fix me. OPU won't be able to resume existing process for now.
 * We will keep all existing process in a evicted state and
 * wait the process to be terminated.
 */

int kgd2opu_post_reset(struct opu_dev *opu)
{
	int ret;

	if (!opu->init_complete)
		return 0;

	ret = opu_resume(opu);
	if (ret)
		return ret;
	atomic_dec(&opu_locked);

	atomic_set(&opu->sram_ecc_flag, 0);

	opu_smi_event_update_gpu_reset(opu, true);

	return 0;
}

bool opu_is_locked(void)
{
	return  (atomic_read(&opu_locked) > 0);
}

void kgd2opu_suspend(struct opu_dev *opu, bool run_pm)
{
	if (!opu->init_complete)
		return;

	/* for runtime suspend, skip locking opu */
	if (!run_pm) {
		/* For first OPU device suspend all the OPU processes */
		if (atomic_inc_return(&opu_locked) == 1)
			opu_suspend_all_processes();
	}

	opu->dqm->ops.stop(opu->dqm);
	opu_iommu_suspend(opu);
}

int kgd2opu_resume(struct opu_dev *opu, bool run_pm)
{
	int ret, count;

	if (!opu->init_complete)
		return 0;

	ret = opu_resume(opu);
	if (ret)
		return ret;

	/* for runtime resume, skip unlocking opu */
	if (!run_pm) {
		count = atomic_dec_return(&opu_locked);
		WARN_ONCE(count < 0, "OPU suspend / resume ref. error");
		if (count == 0)
			ret = opu_resume_all_processes();
	}

	return ret;
}

static int opu_resume(struct opu_dev *opu)
{
	int err = 0;

	err = opu_iommu_resume(opu);
	if (err) {
		dev_err(opu_device,
			"Failed to resume IOMMU for device %x:%x\n",
			opu->pdev->vendor, opu->pdev->device);
		return err;
	}

	err = opu->dqm->ops.start(opu->dqm);
	if (err) {
		dev_err(opu_device,
			"Error starting queue manager for device %x:%x\n",
			opu->pdev->vendor, opu->pdev->device);
		goto dqm_start_error;
	}

	return err;

dqm_start_error:
	opu_iommu_suspend(opu);
	return err;
}

static inline void opu_queue_work(struct workqueue_struct *wq,
				  struct work_struct *work)
{
	int cpu, new_cpu;

	cpu = new_cpu = smp_processor_id();
	do {
		new_cpu = cpumask_next(new_cpu, cpu_online_mask) % nr_cpu_ids;
		if (cpu_to_node(new_cpu) == numa_node_id())
			break;
	} while (cpu != new_cpu);

	queue_work_on(new_cpu, wq, work);
}

/* This is called directly from KGD at ISR. */
void kgd2opu_interrupt(struct opu_dev *opu, const void *ih_ring_entry)
{
	uint32_t patched_ihre[OPU_MAX_RING_ENTRY_SIZE];
	bool is_patched = false;
	unsigned long flags;

	if (!opu->init_complete)
		return;

	if (opu->device_info->ih_ring_entry_size > sizeof(patched_ihre)) {
		dev_err_once(opu_device, "Ring entry too small\n");
		return;
	}

	spin_lock_irqsave(&opu->interrupt_lock, flags);

	if (opu->interrupts_active
	    && interrupt_is_wanted(opu, ih_ring_entry,
				   patched_ihre, &is_patched)
	    && enqueue_ih_ring_entry(opu,
				     is_patched ? patched_ihre : ih_ring_entry))
		opu_queue_work(opu->ih_wq, &opu->interrupt_work);

	spin_unlock_irqrestore(&opu->interrupt_lock, flags);
}

int kgd2opu_quiesce_mm(struct mm_struct *mm)
{
	struct opu_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	WARN(debug_evictions, "Evicting pid %d", p->lead_thread->pid);
	r = opu_process_evict_queues(p);

	opu_unref_process(p);
	return r;
}

int kgd2opu_resume_mm(struct mm_struct *mm)
{
	struct opu_process *p;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, opu_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ESRCH;

	r = opu_process_restore_queues(p);

	opu_unref_process(p);
	return r;
}

/** kgd2opu_schedule_evict_and_restore_process - Schedules work queue that will
 *   prepare for safe eviction of OPU BOs that belong to the specified
 *   process.
 *
 * @mm: mm_struct that identifies the specified OPU process
 * @fence: eviction fence attached to OPU process BOs
 *
 */
int kgd2opu_schedule_evict_and_restore_process(struct mm_struct *mm,
					       struct dma_fence *fence)
{
	struct opu_process *p;
	unsigned long active_time;
	unsigned long delay_jiffies = msecs_to_jiffies(PROCESS_ACTIVE_TIME_MS);

	if (!fence)
		return -EINVAL;

	if (dma_fence_is_signaled(fence))
		return 0;

	p = opu_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (fence->seqno == p->last_eviction_seqno)
		goto out;

	p->last_eviction_seqno = fence->seqno;

	/* Avoid OPU process starvation. Wait for at least
	 * PROCESS_ACTIVE_TIME_MS before evicting the process again
	 */
	active_time = get_jiffies_64() - p->last_restore_timestamp;
	if (delay_jiffies > active_time)
		delay_jiffies -= active_time;
	else
		delay_jiffies = 0;

	/* During process initialization eviction_work.dwork is initialized
	 * to opu_evict_bo_worker
	 */
	WARN(debug_evictions, "Scheduling eviction of pid %d in %ld jiffies",
	     p->lead_thread->pid, delay_jiffies);
	schedule_delayed_work(&p->eviction_work, delay_jiffies);
out:
	opu_unref_process(p);
	return 0;
}

static int opu_gtt_sa_init(struct opu_dev *opu, unsigned int buf_size,
				unsigned int chunk_size)
{
	unsigned int num_of_longs;

	if (WARN_ON(buf_size < chunk_size))
		return -EINVAL;
	if (WARN_ON(buf_size == 0))
		return -EINVAL;
	if (WARN_ON(chunk_size == 0))
		return -EINVAL;

	opu->gtt_sa_chunk_size = chunk_size;
	opu->gtt_sa_num_of_chunks = buf_size / chunk_size;

	num_of_longs = (opu->gtt_sa_num_of_chunks + BITS_PER_LONG - 1) /
		BITS_PER_LONG;

	opu->gtt_sa_bitmap = kcalloc(num_of_longs, sizeof(long), GFP_KERNEL);

	if (!opu->gtt_sa_bitmap)
		return -ENOMEM;

	pr_debug("gtt_sa_num_of_chunks = %d, gtt_sa_bitmap = %p\n",
			opu->gtt_sa_num_of_chunks, opu->gtt_sa_bitmap);

	mutex_init(&opu->gtt_sa_lock);

	return 0;

}

static void opu_gtt_sa_fini(struct opu_dev *opu)
{
	mutex_destroy(&opu->gtt_sa_lock);
	kfree(opu->gtt_sa_bitmap);
}

static inline uint64_t opu_gtt_sa_calc_gpu_addr(uint64_t start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return start_addr + bit_num * chunk_size;
}

static inline uint32_t *opu_gtt_sa_calc_cpu_addr(void *start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return (uint32_t *) ((uint64_t) start_addr + bit_num * chunk_size);
}

int opu_gtt_sa_allocate(struct opu_dev *opu, unsigned int size,
			struct opu_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;

	if (size == 0)
		return -EINVAL;

	if (size > opu->gtt_sa_num_of_chunks * opu->gtt_sa_chunk_size)
		return -ENOMEM;

	*mem_obj = kzalloc(sizeof(struct opu_mem_obj), GFP_KERNEL);
	if (!(*mem_obj))
		return -ENOMEM;

	pr_debug("Allocated mem_obj = %p for size = %d\n", *mem_obj, size);

	start_search = 0;

	mutex_lock(&opu->gtt_sa_lock);

opu_gtt_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(opu->gtt_sa_bitmap,
					opu->gtt_sa_num_of_chunks,
					start_search);

	pr_debug("Found = %d\n", found);

	/* If there wasn't any free chunk, bail out */
	if (found == opu->gtt_sa_num_of_chunks)
		goto opu_gtt_no_free_chunk;

	/* Update fields of mem_obj */
	(*mem_obj)->range_start = found;
	(*mem_obj)->range_end = found;
	(*mem_obj)->gpu_addr = opu_gtt_sa_calc_gpu_addr(
					opu->gtt_start_gpu_addr,
					found,
					opu->gtt_sa_chunk_size);
	(*mem_obj)->cpu_ptr = opu_gtt_sa_calc_cpu_addr(
					opu->gtt_start_cpu_ptr,
					found,
					opu->gtt_sa_chunk_size);

	pr_debug("gpu_addr = %p, cpu_addr = %p\n",
			(uint64_t *) (*mem_obj)->gpu_addr, (*mem_obj)->cpu_ptr);

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= opu->gtt_sa_chunk_size) {
		pr_debug("Single bit\n");
		set_bit(found, opu->gtt_sa_bitmap);
		goto opu_gtt_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - opu->gtt_sa_chunk_size;
	do {
		(*mem_obj)->range_end =
			find_next_zero_bit(opu->gtt_sa_bitmap,
					opu->gtt_sa_num_of_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mem_obj)->range_end != found) {
			start_search = found;
			goto opu_gtt_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == opu->gtt_sa_num_of_chunks)
			goto opu_gtt_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= opu->gtt_sa_chunk_size)
			cur_size = 0;
		else
			cur_size -= opu->gtt_sa_chunk_size;

	} while (cur_size > 0);

	pr_debug("range_start = %d, range_end = %d\n",
		(*mem_obj)->range_start, (*mem_obj)->range_end);

	/* Mark the chunks as allocated */
	for (found = (*mem_obj)->range_start;
		found <= (*mem_obj)->range_end;
		found++)
		set_bit(found, opu->gtt_sa_bitmap);

opu_gtt_out:
	mutex_unlock(&opu->gtt_sa_lock);
	return 0;

opu_gtt_no_free_chunk:
	pr_debug("Allocation failed with mem_obj = %p\n", *mem_obj);
	mutex_unlock(&opu->gtt_sa_lock);
	kfree(*mem_obj);
	return -ENOMEM;
}

int opu_gtt_sa_free(struct opu_dev *opu, struct opu_mem_obj *mem_obj)
{
	unsigned int bit;

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("Free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&opu->gtt_sa_lock);

	/* Mark the chunks as free */
	for (bit = mem_obj->range_start;
		bit <= mem_obj->range_end;
		bit++)
		clear_bit(bit, opu->gtt_sa_bitmap);

	mutex_unlock(&opu->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}

void kgd2opu_set_sram_ecc_flag(struct opu_dev *opu)
{
	if (opu)
		atomic_inc(&opu->sram_ecc_flag);
}

void opu_inc_compute_active(struct opu_dev *opu)
{
	if (atomic_inc_return(&opu->compute_profile) == 1)
		amdgpu_amdopu_set_compute_idle(opu->kgd, false);
}

void opu_dec_compute_active(struct opu_dev *opu)
{
	int count = atomic_dec_return(&opu->compute_profile);

	if (count == 0)
		amdgpu_amdopu_set_compute_idle(opu->kgd, true);
	WARN_ONCE(count < 0, "Compute profile ref. count error");
}

void kgd2opu_smi_event_throttle(struct opu_dev *opu, uint32_t throttle_bitmask)
{
	if (opu && opu->init_complete)
		opu_smi_event_update_thermal_throttling(opu, throttle_bitmask);
}

#if defined(CONFIG_DEBUG_FS)

/* This function will send a package to HIQ to hang the HWS
 * which will trigger a GPU reset and bring the HWS back to normal state
 */
int opu_debugfs_hang_hws(struct opu_dev *dev)
{
	int r = 0;

	if (dev->dqm->sched_policy != OPU_SCHED_POLICY_HWS) {
		pr_err("HWS is not enabled");
		return -EINVAL;
	}

	r = pm_debugfs_hang_hws(&dev->dqm->packets);
	if (!r)
		r = dqm_debugfs_execute_queues(dev->dqm);

	return r;
}

#endif
