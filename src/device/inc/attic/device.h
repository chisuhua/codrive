
/* 1024 doorbells, 4 or 8 bytes each doorbell depending on ASIC generation */
#define DOORBELL_SIZE 8
#define DOORBELLS_PAGE_SIZE(ds) (1024 * (ds))



struct device_info {
	enum asic_family_type asic_family;
	uint32_t eop_buffer_size;
	uint32_t doorbell_size;
};

struct process_doorbells {
	bool use_gpuvm;
	uint32_t size;
	void *mapping;
	pthread_mutex_t mutex;
};


const struct device_info *get_device_info_by_dev_id(uint16_t dev_id);
extern struct process_doorbells *doorbells;
device_status_t map_doorbell(HSAuint32 NodeId, HSAuint32 gpu_id, HSAuint64 doorbell_mmap_offset);
