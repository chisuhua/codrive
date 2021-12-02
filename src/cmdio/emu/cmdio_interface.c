


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

