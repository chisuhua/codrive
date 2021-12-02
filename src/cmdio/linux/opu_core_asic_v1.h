#ifdef __OPU_CORE_ASIC_V1_H__
#define __OPU_CORE_ASIC_V1_H__

void core_asic_v1_program_sh_mem_settings(struct opu_device *kgd, uint32_t vmid,
		uint32_t sh_mem_config,
		uint32_t sh_mem_ape1_base, uint32_t sh_mem_ape1_limit,
		uint32_t sh_mem_bases);
int core_asic_v1_set_pasid_vmid_mapping(struct opu_device *kgd, u32 pasid,
		unsigned int vmid);
int core_asic_v1_init_interrupts(struct opu_device *kgd, uint32_t pipe_id);
int core_asic_v1_hqd_load(struct opu_device *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm);
int core_asic_v1_hiq_mqd_load(struct opu_device *kgd, void *mqd,
			    uint32_t pipe_id, uint32_t queue_id,
			    uint32_t doorbell_off);
int core_asic_v1_hqd_dump(struct opu_device *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);
bool core_asic_v1_hqd_is_occupied(struct opu_device *kgd, uint64_t queue_address,
		uint32_t pipe_id, uint32_t queue_id);
int core_asic_v1_hqd_destroy(struct opu_device *kgd, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id);
int core_asic_v1_address_watch_disable(struct opu_device *kgd);
int core_asic_v1_address_watch_execute(struct opu_device *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
int core_asic_v1_wave_control_execute(struct opu_device *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
uint32_t core_asic_v1_address_watch_get_offset(struct opu_device *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);

bool core_asic_v1_get_atc_vmid_pasid_mapping_info(struct opu_device *kgd,
					uint8_t vmid, uint16_t *p_pasid);

void core_asic_v1_set_vm_context_page_table_base(struct opu_device *kgd,
			uint32_t vmid, uint64_t page_table_base);
void core_asic_v1_get_cu_occupancy(struct opu_device *kgd, int pasid,
		int *pasid_wave_cnt, int *max_waves_per_cu);

#endif
