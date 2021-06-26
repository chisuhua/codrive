#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "inc/Agent.h"
// #include "inc/pps_ext_image.h"

#include "inc/blit_cpdma.h"
#include "inc/cache.h"
#include "inc/Device.h"

// #include "inc/command_queue.h"

#include "inc/MemoryRegion.h"
// #include "inc/interrupt_signal.h"
// #include "inc/isa.h"
#include "inc/Runtime.h"
#include "util/os.h"

#include "CoStream.h"
//#include "inc/device_api.h"

// Size of scratch (private) segment pre-allocated per thread, in bytes.
#define DEFAULT_SCRATCH_BYTES_PER_THREAD 2048
#define MAX_WAVE_SCRATCH 8387584 // See COMPUTE_TMPRING_SIZE.WAVESIZE
#define MAX_NUM_DOORBELLS 0x400

// extern core::IHsaApiTable hsa_internal_api_table_;

// namespace hcs {
GpuAgent::GpuAgent(HSAuint32 node, const HsaCoreProperties* node_props)
    : GpuAgentInt(node)
    , properties_(node_props)
    , current_coherency_type_(HSA_AMD_COHERENCY_TYPE_COHERENT)
    , queues_()
    , local_region_(NULL)
    , trap_code_buf_(NULL)
    , trap_code_buf_size_(0)
    , doorbell_queue_map_(NULL)
    , memory_bus_width_(0)
    , memory_max_frequency_(0)
{
    is_apu_node = (properties_->NumCPUCores > 0);
    profile_ = (is_apu_node) ? HSA_PROFILE_FULL : HSA_PROFILE_BASE;

    device_status_t err = DEVICE->GetClockCounters(node_id(), &t0_);
    t1_ = t0_;
    assert(err == DEVICE_STATUS_SUCCESS && "hsaGetClockCounters error");

    // Set instruction set architecture via node property, only on GPU agent.
    // TODO
    /*
  isa_ = (core::IIsa*)core::IsaRegistry::GetIsa(core::Isa::Version(
      node_props.EngineId.ui32.Major, node_props.EngineId.ui32.Minor,
      node_props.EngineId.ui32.Stepping), profile_ == HSA_PROFILE_FULL);*/

    /*isa_ = (core::IIsa*)core::IsaRegistry::GetIsa(core::Isa::Version(
      node_props->EngineId.ui32.Major, node_props->EngineId.ui32.Minor,
      node_props->EngineId.ui32.Stepping));*/

    current_coherency_type((profile_ == HSA_PROFILE_FULL)
            ? HSA_AMD_COHERENCY_TYPE_COHERENT
            : HSA_AMD_COHERENCY_TYPE_NONCOHERENT);

    max_queues_ = Runtime::runtime_singleton_->flag().max_queues();
#if !defined(HSA_LARGE_MODEL) || !defined(__linux__)
    if (max_queues_ == 0) {
        max_queues_ = 10;
    }
    max_queues_ = std::min(10U, max_queues_);
#else
    if (max_queues_ == 0) {
        max_queues_ = 128;
    }
    max_queues_ = std::min(128U, max_queues_);
#endif

    // Populate region list.
    InitRegionList();

    // Populate cache list.
    InitCacheList();

    // init dma engine
    // schi: it will be init in PostToolsInit
    // InitDma();
}

GpuAgent::~GpuAgent()
{
    for (auto& blit : blits_) {
        if (!blit.empty()) {
            status_t status = blit->Destroy(*this);
            assert(status == SUCCESS);
        }
    }

    if (scratch_pool_.base() != NULL) {
        DEVICE->FreeMemory(scratch_pool_.base(), scratch_pool_.size());
    }

    Runtime::runtime_singleton_->system_deallocator()(doorbell_queue_map_);

    if (trap_code_buf_ != NULL) {
        // schi TODO ReleaseShader(trap_code_buf_, trap_code_buf_size_);
    }

    std::for_each(regions_.begin(), regions_.end(), DeleteObject());
    regions_.clear();
}
/*
void GpuAgent::AssembleShader(const char* func_name, AssembleTarget assemble_target,
                                void*& code_buf, size_t& code_buf_size) const {
  // Select precompiled shader implementation from name/target.
  struct ASICShader {
    const void* code;
    size_t size;
    int num_sgprs;
    int num_vgprs;
  };

  struct CompiledShader {
    ASICShader compute_7;
    ASICShader compute_8;
    ASICShader compute_9;
  };


  auto compiled_shader_it = compiled_shaders.find(func_name);
  assert(compiled_shader_it != compiled_shaders.end() &&
         "Precompiled shader unavailable");

  ASICShader* asic_shader = NULL;

  switch (isa_->GetMajorVersion()) {
    case 7:
      asic_shader = &compiled_shader_it->second.compute_7;
      break;
    case 8:
      asic_shader = &compiled_shader_it->second.compute_8;
      break;
    case 9:
      asic_shader = &compiled_shader_it->second.compute_9;
      break;
    default:
      assert(false && "Precompiled shader unavailable for target");
  }

  // Allocate a GPU-visible buffer for the shader.
  size_t header_size =
      (assemble_target == AssembleTarget::AQL ? sizeof(amd_kernel_code_t) : 0);
  code_buf_size = alignUp(header_size + asic_shader->size, 0x1000);

  code_buf = core::IRuntime::runtime_singleton_->system_allocator()(
      code_buf_size, 0x1000, core::IMemoryRegion::AllocateExecutable);
  assert(code_buf != NULL && "Code buffer allocation failed");

  memset(code_buf, 0, code_buf_size);

  // Populate optional code object header.
  if (assemble_target == AssembleTarget::AQL) {
    amd_kernel_code_t* header = reinterpret_cast<amd_kernel_code_t*>(code_buf);

    int gran_sgprs = std::max(0, (int(asic_shader->num_sgprs) - 1) / 8);
    int gran_vgprs = std::max(0, (int(asic_shader->num_vgprs) - 1) / 4);

    header->kernel_code_entry_byte_offset = sizeof(amd_kernel_code_t);
    HCS_BITS_SET(header->kernel_code_properties,
                     AMD_KERNEL_CODE_PROPERTIES_ENABLE_SGPR_KERNARG_SEGMENT_PTR,
                     1);
    HCS_BITS_SET(header->compute_pgm_rsrc1,
                     AMD_COMPUTE_PGM_RSRC_ONE_GRANULATED_WAVEFRONT_SGPR_COUNT,
                     gran_sgprs);
    HCS_BITS_SET(header->compute_pgm_rsrc1,
                     AMD_COMPUTE_PGM_RSRC_ONE_GRANULATED_WORKITEM_VGPR_COUNT,
                     gran_vgprs);
    HCS_BITS_SET(header->compute_pgm_rsrc1,
                     AMD_COMPUTE_PGM_RSRC_ONE_FLOAT_DENORM_MODE_16_64, 3);
    HCS_BITS_SET(header->compute_pgm_rsrc1,
                     AMD_COMPUTE_PGM_RSRC_ONE_ENABLE_IEEE_MODE, 1);
    HCS_BITS_SET(header->compute_pgm_rsrc2,
                     AMD_COMPUTE_PGM_RSRC_TWO_USER_SGPR_COUNT, 2);
    HCS_BITS_SET(header->compute_pgm_rsrc2,
                     AMD_COMPUTE_PGM_RSRC_TWO_ENABLE_SGPR_WORKGROUP_ID_X, 1);
  }

  // Copy shader code into the GPU-visible buffer.
  memcpy((void*)(uintptr_t(code_buf) + header_size), asic_shader->code,
         asic_shader->size);
}
*/
/*
void GpuAgent::ReleaseShader(void* code_buf, size_t code_buf_size) const {
  core::IRuntime::runtime_singleton_->system_deallocator()(code_buf);
}
*/

#define NUM_OF_IGPU_HEAPS 3
#define NUM_OF_DGPU_HEAPS 3
void GpuAgent::InitRegionList()
{
    uint32_t num_banks = properties_->NumMemoryBanks;
    if (!is_apu_node)
        num_banks += NUM_OF_DGPU_HEAPS; // reserver for LDS/SCRATCH/MMIO
    else
        num_banks += NUM_OF_IGPU_HEAPS;

    std::vector<HsaMemoryProperties> mem_props(num_banks);
    if (DEVICE_STATUS_SUCCESS == DEVICE->GetNodeMemoryProperties(node_id(), num_banks, &mem_props[0])) {
        for (uint32_t mem_idx = 0; mem_idx < num_banks; ++mem_idx) {
            // Ignore the one(s) with unknown size.
            if (mem_props[mem_idx].SizeInBytes == 0) {
                continue;
            }

            switch (mem_props[mem_idx].HeapType) {
            case HSA_HEAPTYPE_FRAME_BUFFER_PRIVATE:
            case HSA_HEAPTYPE_FRAME_BUFFER_PUBLIC:
                if (!is_apu_node) {
                    mem_props[mem_idx].VirtualBaseAddress = 0;
                }

                memory_bus_width_ = mem_props[mem_idx].Width;
                memory_max_frequency_ = mem_props[mem_idx].MemoryClockMax;
            case HSA_HEAPTYPE_GPU_LDS:
            case HSA_HEAPTYPE_GPU_SCRATCH: {
                MemoryRegion* region = new MemoryRegion(false, false, this, mem_props[mem_idx]);

                regions_.push_back(region);

                if (region->IsLocalMemory()) {
                    local_region_ = region;
                    // Expose VRAM as uncached/fine grain over PCIe (if enabled) or XGMI.
                    /*/ /
            if ((properties_->HiveID != 0) ||
                (core::IRuntime::runtime_singleton_->flag().fine_grain_pcie())) {
              regions_.push_back(new MemoryRegion(true, false, this, mem_props[mem_idx]));
            }
            */
                }
                break;
            }
            case HSA_HEAPTYPE_SYSTEM:
                if (is_apu_node) {
                    memory_bus_width_ = mem_props[mem_idx].Width;
                    memory_max_frequency_ = mem_props[mem_idx].MemoryClockMax;
                }
                break;
            case HSA_HEAPTYPE_MMIO_REMAP:
                // Remap offsets defined in kfd_ioctl.h
                // HDP_flush_.HDP_MEM_FLUSH_CNTL = (uint32_t*)mem_props[mem_idx].VirtualBaseAddress;
                // HDP_flush_.HDP_REG_FLUSH_CNTL = HDP_flush_.HDP_MEM_FLUSH_CNTL + 1;
                break;
            default:
                continue;
            }
        }
    }
}

void GpuAgent::InitScratchPool()
{
    HsaMemFlags flags;
    flags.Value = 0;
    flags.ui32.Scratch = 1;
    flags.ui32.HostAccess = 1;

    scratch_per_thread_ = Runtime::runtime_singleton_->flag().scratch_mem_size();
    if (scratch_per_thread_ == 0)
        scratch_per_thread_ = DEFAULT_SCRATCH_BYTES_PER_THREAD;

    // Scratch length is: waves/CU * threads/wave * queues * #CUs *
    // scratch/thread
    const uint32_t num_cu = properties_->NumFComputeCores / properties_->NumSIMDPerCU;
    queue_scratch_len_ = alignUp(32 * 64 * num_cu * scratch_per_thread_, 65536);
    size_t max_scratch_len = queue_scratch_len_ * max_queues_;

#if defined(HSA_LARGE_MODEL) && defined(__linux__)
    // For 64-bit linux use max queues unless otherwise specified
    if ((max_scratch_len == 0) || (max_scratch_len > 4294967296)) {
        max_scratch_len = 4294967296; // 4GB apeture max
    }
#endif

    void* scratch_base;
    device_status_t err = DEVICE->AllocMemory(node_id(), max_scratch_len, flags, &scratch_base);
    assert(err == DEVICE_STATUS_SUCCESS && "hsaKmtAllocMemory(Scratch) failed");
    assert(isMultipleOf(scratch_base, 0x1000) && "Scratch base is not page aligned!");

    scratch_pool_.~SmallHeap();
    if (DEVICE_STATUS_SUCCESS == err) {
        new (&scratch_pool_) SmallHeap(scratch_base, max_scratch_len);
    } else {
        new (&scratch_pool_) SmallHeap();
    }
}

void GpuAgent::InitCacheList()
{
    // Get GPU cache information.
    // Similar to getting CPU cache but here we use FComputeIdLo.
    cache_props_.resize(properties_->NumCaches);
    if (DEVICE_STATUS_SUCCESS != DEVICE->GetNodeCacheProperties(node_id(), properties_->FComputeIdLo, properties_->NumCaches, &cache_props_[0])) {
        cache_props_.clear();
    } else {
        // Only store GPU D-cache.
        for (size_t cache_id = 0; cache_id < cache_props_.size(); ++cache_id) {
            const HsaCacheType type = cache_props_[cache_id].CacheType;
            if (type.ui32.HSACU != 1 || type.ui32.Instruction == 1) {
                cache_props_.erase(cache_props_.begin() + cache_id);
                --cache_id;
            }
        }
    }

    // Update cache objects
    caches_.clear();
    caches_.resize(cache_props_.size());
    char name[64];
    GetInfo(HSA_AGENT_INFO_NAME, name);
    std::string agentName = name;
    for (size_t i = 0; i < caches_.size(); i++)
        caches_[i].reset(new core::ICache(agentName + " L" + std::to_string(cache_props_[i].CacheLevel),
            cache_props_[i].CacheLevel, cache_props_[i].CacheSize));
}

status_t GpuAgent::IterateRegion(
    status_t (*callback)(const core::IMemoryRegion* region, void* data),
    void* data) const
{
    //TODO schi hack include_peer? return VisitRegion(true, callback, data);
    return VisitRegion(false, callback, data);
}

status_t GpuAgent::IterateCache(status_t (*callback)(ICache* cache, void* data),
    void* data) const
{
    callback_t<decltype(callback)> call(callback);
    for (size_t i = 0; i < caches_.size(); i++) {
        status_t stat = call(caches_[i].get(), data);
        if (stat != SUCCESS) return stat;
    }
    return SUCCESS;
}

status_t GpuAgent::VisitRegion(bool include_peer,
    status_t (*callback)(const core::IMemoryRegion* region,
        void* data),
    void* data) const
{
    status_t stat;
    if (include_peer) {
        // Only expose system, local, and LDS memory of the blit agent.
        // if (this->node_id() == core::IRuntime::runtime_singleton_->region_gpu()->node_id()) {
        if (this->node_id() == Runtime::runtime_singleton_->blit_agent()->node_id()) {
            stat = VisitRegion(regions_, callback, data);
            if (stat != SUCCESS) {
                return stat;
            }
        }

        // Also expose system regions accessible by this agent.
        stat = VisitRegion(Runtime::runtime_singleton_->system_regions_fine(),
            callback, data);
        if (stat != SUCCESS) {
            return stat;
        }

        return VisitRegion(Runtime::runtime_singleton_->system_regions_coarse(),
            callback, data);
    }

    // Only expose system, local, and LDS memory of this agent.
    stat = VisitRegion(regions_, callback, data);
    return stat;
}

status_t GpuAgent::VisitRegion(
    const std::vector<const core::IMemoryRegion*>& regions,
    status_t (*callback)(const core::IMemoryRegion* region, void* data),
    void* data) const
{
    callback_t<decltype(callback)> call(callback);
    for (const core::IMemoryRegion* region : regions) {
        const MemoryRegion* hcs_region = reinterpret_cast<const MemoryRegion*>(region);

        // Only expose system, local, and LDS memory.
        if (hcs_region->IsSystem() || hcs_region->IsLocalMemory() || hcs_region->IsLDS()) {
            status_t status = call(hcs_region, data);
            if (status != SUCCESS) {
                return status;
            }
        }
    }

    return SUCCESS;
}

queue_t* GpuAgent::CreateInterceptibleQueue()
{
    // Disabled intercept of internal queues pending tools updates.
    // core::IQueue* queue = nullptr;
    queue_t* queue_handle = nullptr;
    QueueCreate(minAqlSize_, QUEUE_TYPE_MULTI, NULL, NULL, 0, 0, queue_handle);
    /*
  if (queue != nullptr)
    core::IRuntime::runtime_singleton_->InternalQueueCreateNotify(core::Queue::Handle(queue),
                                                                 this->public_handle());
                                                                 */
    return queue_handle;
}

/*
core::IBlit* GpuAgent::CreateBlitSdma(bool h2d) {
  core::IBlit* sdma;

  if (isa_->GetMajorVersion() <= 8) {
    sdma = new BlitSdmaV2V3(h2d);
  } else {
    sdma = new BlitSdmaV4(h2d);
  }

  if (sdma->Initialize(*this) != SUCCESS) {
    sdma->Destroy(*this);
    delete sdma;
    sdma = NULL;
  }

  return sdma;
}
*/

// core::IBlit* GpuAgent::CreateCpDma(core::Queue* queue) {
core::IBlit* GpuAgent::CreateCpDma(queue_t* queue)
{
    BlitCpDma* dma_eng = new BlitCpDma(queue);

    if (dma_eng->Initialize(*this) != SUCCESS) {
        dma_eng->Destroy(*this);
        delete dma_eng;
        dma_eng = NULL;
    }

    return dma_eng;
}

void GpuAgent::InitDma()
{
    // Setup lazy init pointers on queues and blits.
    auto queue_lambda = [this]() {
        auto ret = CreateInterceptibleQueue();
        if (ret == nullptr)
            throw co_exception(ERROR_OUT_OF_RESOURCES,
                "Internal queue creation failed.");
        return ret;
    };
    // Dedicated compute queue for host-to-agent blits.
    queues_[QueueBlitOnly].reset(queue_lambda);
    // Share utility queue with agent-to-host blits.
    queues_[QueueUtility].reset(queue_lambda);

    // Decide which engine to use for blits.
    auto blit_lambda = [this](bool use_c2c, lazy_ptr<queue_t>& queue) {
        auto ret = CreateCpDma((*queue).get());
        if (ret == nullptr)
            throw co_exception(ERROR_OUT_OF_RESOURCES, "Blit creation failed.");
        return ret;
    };
    uint32_t blit_cnt_ = DefaultBlitCount; //  + properties_->NumSdmaXgmiEngines;
    blits_.resize(blit_cnt_);
    // Initialize blit objects used for D2D, H2D, D2H, and
    // P2P copy operations.
    // -- Blit at index BlitDevToDev(0) deals with copies within
    //    local framebuffer and always engages a Blit Kernel
    // -- Blit at index BlitHostToDev(1) deals with copies from
    //    Host to DEVICE-> (H2D) and could engage either a Blit
    //    Kernel or sDMA
    // -- Blit at index BlitDevToHost(2) deals with copies from
    //    DEVICE-> to Host (D2H) and Peer to Peer (P2P) over PCIe.
    //    It could engage either a Blit Kernel or sDMA
    // -- Blit at index DefaultBlitCount(3) and beyond deal
    //    exclusively P2P over xGMI links

    blits_[BlitHostToDev].reset(
        [blit_lambda, this]() { return blit_lambda(false, queues_[QueueBlitOnly]); });
    blits_[BlitDevToHost].reset(
        [blit_lambda, this]() { return blit_lambda(false, queues_[QueueUtility]); });
    blits_[BlitDevToDev].reset(
        [blit_lambda, this]() { return blit_lambda(false, queues_[QueueUtility]); });

    // XGMI engines.
    /*
  for (uint32_t idx = DefaultBlitCount; idx < blit_cnt_; idx++) {
    blits_[idx].reset([blit_lambda, this]() { return blit_lambda(true, queues_[QueueUtility]); });
  }
  */

    // blits_[DMA_H2D].reset([blit_lambda, this]() { return blit_lambda(true, queues_[DMA_H2D]); });
    // blits_[DMA_D2H].reset([blit_lambda, this]() { return blit_lambda(false, queues_[DMA_D2H]); });
    // blits_[DMA_D2D].reset([blit_lambda, this]() { return blit_lambda(false, queues_[DMA_H2D]); });

    // GWS queues.
    // InitGWS();
}

#if 0
void GpuAgent::InitGWS() {
  gws_queue_.queue_.reset([this]() {
    if (properties_->NumGws == 0) return (core::IQueue*)nullptr;
    std::unique_ptr<core::IQueue> queue(CreateInterceptibleQueue());
    if (queue == nullptr)
      throw AMD::hsa_exception(ERROR_OUT_OF_RESOURCES,
                               "Internal queue creation failed.");

    auto err = static_cast<AqlQueue*>(queue.get())->EnableGWS(1);
    if (err != SUCCESS) throw AMD::hsa_exception(err, "GWS allocation failed.");

    gws_queue_.ref_ct_ = 0;
    return queue.release();
  });
}

void GpuAgent::GWSRelease() {
  ScopedAcquire<KernelMutex> lock(&gws_queue_.lock_);
  gws_queue_.ref_ct_--;
  if (gws_queue_.ref_ct_ != 0) return;
  InitGWS();
}
#endif

void GpuAgent::PreloadBlits()
{
    for (auto& blit : blits_) {
        blit.touch();
    }
    /*
  blits_[DMA_H2D].touch();
  blits_[DMA_D2H].touch();
  blits_[DMA_D2D].touch();
  blits_[DMA_H2H].touch();
  */
}

status_t GpuAgent::PostToolsInit()
{
    // Defer memory allocation until agents have been discovered.
    InitScratchPool();
    BindTrapHandler();
    InitDma();

    return SUCCESS;
}

// status_t GpuAgent::DmaCopy(void* dst, const void* src, size_t size, uint8_t dir) {
status_t GpuAgent::DmaCopy(void* dst, const void* src, size_t size)
{
    // This operation is not a P2P operation - uses BlitKernel
    // return blits_[BlitDevToDev]->SubmitLinearCopyCommand(false, dst, src, size);
    status_t stat;

    stat = blits_[BlitDevToDev]->SubmitLinearCopyCommand(dst, src, size);
    return stat;

    // return SUCCESS;
}

status_t GpuAgent::DmaCopy(void* dst, core::IAgent& dst_agent,
    const void* src, core::IAgent& src_agent,
    size_t size,
    std::vector<signal_t>& dep_signals,
    signal_t out_signal)
{
    // core::ISignal& out_signal) {

    // Bind the Blit object that will drive this copy operation
    lazy_ptr<core::IBlit>& blit = GetBlitObject(dst_agent, src_agent);

    /*
  if (profiling_enabled()) {
    out_signal.async_copy_agent(core::IAgent::Convert(this->public_handle()));
  }
*/

    status_t stat;
    stat = blit->SubmitLinearCopyCommand(dst, src, size, dep_signals, out_signal);
    return stat;
}

#if 0
status_t GpuAgent::DmaCopyRect(const hsa_pitched_ptr_t* dst, const hsa_dim3_t* dst_offset,
                                   const hsa_pitched_ptr_t* src, const hsa_dim3_t* src_offset,
                                   const hsa_dim3_t* range, hsa_amd_copy_direction_t dir,
                                   std::vector<core::ISignal*>& dep_signals,
                                   core::ISignal& out_signal) {
  if (isa_->GetMajorVersion() < 9) return ERROR_INVALID_AGENT;

  lazy_ptr<core::IBlit>& blit =
      (dir == hsaHostToAgent) ? blits_[BlitHostToDev] : blits_[BlitDevToHost];

  if (!blit->isSDMA()) return ERROR_OUT_OF_RESOURCES;

  if (profiling_enabled()) {
    // Track the agent so we could translate the resulting timestamp to system
    // domain correctly.
    out_signal.async_copy_agent(core::IAgent::Convert(this->public_handle()));
  }

  BlitSdmaBase* sdmaBlit = static_cast<BlitSdmaBase*>((*blit).get());
  status_t stat = sdmaBlit->SubmitCopyRectCommand(dst, dst_offset, src, src_offset, range,
                                                      dep_signals, out_signal);

  return stat;
}
#endif

status_t GpuAgent::DmaFill(void* ptr, uint32_t value, size_t count)
{
    // return blits_[BlitDevToDev]->SubmitLinearFillCommand(ptr, value, count);
    return SUCCESS;
}

status_t GpuAgent::EnableDmaProfiling(bool enable)
{
    /*
  for (int i = 0; i < BlitCount; ++i) {
    if (blits_[i] != NULL) {
      const status_t stat = blits_[i]->EnableProfiling(enable);
      if (stat != SUCCESS) {
        return stat;
      }
    }
  }
  */

    return SUCCESS;
}

status_t GpuAgent::GetInfo(agent_info_t attribute, void* value) const
{
    // agent, and vendor name size limit
    const size_t attribute_u = static_cast<size_t>(attribute);

    switch (attribute_u) {
    // Build agent name by concatenating the Major, Minor and Stepping Ids
    // of agents compute capability with a prefix of "gfx"
    case HSA_AGENT_INFO_NAME: {
        // std::stringstream name;
        std::string name = "ppu";
        std::memset(value, 0, HSA_PUBLIC_NAME_SIZE);
        char* temp = reinterpret_cast<char*>(value);
        // TODO we don't have isa definition name << "gfx" << isa_->GetMajorVersion() << isa_->GetMinorVersion() << isa_->GetStepping();
        // std::strcpy(temp, name.str().c_str());
        std::strcpy(temp, name.c_str());
        break;
    }
    case HSA_AGENT_INFO_VENDOR_NAME:
        std::memset(value, 0, HSA_PUBLIC_NAME_SIZE);
        std::memcpy(value, "AMD", sizeof("AMD"));
        break;
    case HSA_AGENT_INFO_FEATURE:
        *((hsa_agent_feature_t*)value) = HSA_AGENT_FEATURE_KERNEL_DISPATCH;
        break;
    case HSA_AGENT_INFO_MACHINE_MODEL:
#if defined(HSA_LARGE_MODEL)
        *((hsa_machine_model_t*)value) = HSA_MACHINE_MODEL_LARGE;
#else
        *((hsa_machine_model_t*)value) = HSA_MACHINE_MODEL_SMALL;
#endif
        break;
    // case HSA_AGENT_INFO_BASE_PROFILE_DEFAULT_FLOAT_ROUNDING_MODES:
    case HSA_AGENT_INFO_DEFAULT_FLOAT_ROUNDING_MODE:
        *((hsa_default_float_rounding_mode_t*)value) = HSA_DEFAULT_FLOAT_ROUNDING_MODE_NEAR;
        break;
    case HSA_AGENT_INFO_FAST_F16_OPERATION:
        *((bool*)value) = false;
        break;
    case HSA_AGENT_INFO_PROFILE:
        *((profile_t*)value) = profile_;
        break;
    case HSA_AGENT_INFO_WAVEFRONT_SIZE:
        *((uint32_t*)value) = properties_->WaveFrontSize;
        break;
    case HSA_AGENT_INFO_WORKGROUP_MAX_DIM: {
        // TODO: must be per-agent
        const uint16_t group_size[3] = { 1024, 1024, 1024 };
        std::memcpy(value, group_size, sizeof(group_size));
    } break;
    case HSA_AGENT_INFO_WORKGROUP_MAX_SIZE:
        // TODO: must be per-agent
        *((uint32_t*)value) = 1024;
        break;
    case HSA_AGENT_INFO_GRID_MAX_DIM: {
        const hsa_dim3_t grid_size = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
        std::memcpy(value, &grid_size, sizeof(hsa_dim3_t));
    } break;
    case HSA_AGENT_INFO_GRID_MAX_SIZE:
        *((uint32_t*)value) = UINT32_MAX;
        break;
        /*
    case HSA_AGENT_INFO_FBARRIER_MAX_SIZE:
      // TODO: to confirm
      *((uint32_t*)value) = 32;
      break;
    case HSA_AGENT_INFO_QUEUES_MAX:
      *((uint32_t*)value) = max_queues_;
      break;
      */
    case HSA_AGENT_INFO_QUEUE_MIN_SIZE:
        *((uint32_t*)value) = minAqlSize_;
        break;
    case HSA_AGENT_INFO_QUEUE_MAX_SIZE:
        *((uint32_t*)value) = maxAqlSize_;
        break;
    case HSA_AGENT_INFO_QUEUE_TYPE:
        *((queue_type32_t*)value) = QUEUE_TYPE_MULTI;
        break;
    case HSA_AGENT_INFO_NODE:
        // TODO: associate with OS NUMA support (numactl / GetNumaProcessorNode).
        *((uint32_t*)value) = node_id();
        break;
    case HSA_AGENT_INFO_TYPE:
        *((hsa_agent_type_t*)value) = HSA_AGENT_TYPE_GPU;
        break;
    case HSA_AGENT_INFO_CACHE_SIZE:
        std::memset(value, 0, sizeof(uint32_t) * 4);
        // TODO: no GPU cache info from KFD. Hardcode for now.
        // GCN whitepaper: L1 data cache is 16KB.
        ((uint32_t*)value)[0] = 16 * 1024;
        break;
        /*
    case HSA_AGENT_INFO_ISA:
      *((hsa_isa_t*)value) = core::IIsa::Handle(isa_);
      break;
    case HSA_AGENT_INFO_EXTENSIONS: {
      memset(value, 0, sizeof(uint8_t) * 128);

      auto setFlag = [&](uint32_t bit) {
        assert(bit < 128 * 8 && "Extension value exceeds extension bitmask");
        uint index = bit / 8;
        uint subBit = bit % 8;
        ((uint8_t*)value)[index] |= 1 << subBit;
      };
      */
        /*
      if (core::Ihsa_internal_api_table_.finalizer_api.hsa_ext_program_finalize_fn != NULL) {
        setFlag(HSA_EXTENSION_FINALIZER);
      }

      if (core::Ihsa_internal_api_table_.image_api.hsa_ext_image_create_fn != NULL) {
        setFlag(HSA_EXTENSION_IMAGES);
      }

      if (os::LibHandle lib = os::LoadLib(kAqlProfileLib)) {
        os::CloseLib(lib);
        setFlag(HSA_EXTENSION_AMD_AQLPROFILE);
      }
      setFlag(HSA_EXTENSION_AMD_PROFILER);

      break;
    }
*/
    case HSA_AGENT_INFO_VERSION_MAJOR:
        *((uint16_t*)value) = 1;
        break;
    case HSA_AGENT_INFO_VERSION_MINOR:
        *((uint16_t*)value) = 1;
        break;
    case HSA_AGENT_INFO_IS_APU_NODE:
        *((bool*)value) = is_apu_node;
        break;
        /*
    case HSA_EXT_AGENT_INFO_IMAGE_1D_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_1DA_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_1DB_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_2D_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_2DA_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_2DDEPTH_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_2DADEPTH_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_3D_MAX_ELEMENTS:
    case HSA_EXT_AGENT_INFO_IMAGE_ARRAY_MAX_LAYERS:
      // return hsa_amd_image_get_info_max_dim(public_handle(), attribute, value);
      break;
    case HSA_EXT_AGENT_INFO_MAX_IMAGE_RD_HANDLES:
      // TODO: hardcode based on OCL constants.
      *((uint32_t*)value) = 128;
      break;
    case HSA_EXT_AGENT_INFO_MAX_IMAGE_RORW_HANDLES:
      // TODO: hardcode based on OCL constants.
      *((uint32_t*)value) = 64;
      break;
    case HSA_EXT_AGENT_INFO_MAX_SAMPLER_HANDLERS:
      // TODO: hardcode based on OCL constants.
      *((uint32_t*)value) = 16;
    case HSA_AMD_AGENT_INFO_CHIP_ID:
      *((uint32_t*)value) = properties_->AgentId;
      break;
    case HSA_AMD_AGENT_INFO_CACHELINE_SIZE:
      // TODO: hardcode for now.
      // GCN whitepaper: cache line size is 64 byte long.
      *((uint32_t*)value) = 64;
      break;
    case HSA_AMD_AGENT_INFO_COMPUTE_UNIT_COUNT:
      *((uint32_t*)value) =
          (properties_->NumFComputeCores / properties_->NumSIMDPerCU);
      break;
    case HSA_AMD_AGENT_INFO_MAX_CLOCK_FREQUENCY:
      *((uint32_t*)value) = properties_->MaxEngineClockMhzFCompute;
      break;
    case HSA_AMD_AGENT_INFO_DRIVER_NODE_ID:
      *((uint32_t*)value) = node_id();
      break;
      */
    // TODO schi add copy this case branch from hcs_cpu_agent
    /*
    case HSA_AMD_AGENT_INFO_MAX_ADDRESS_WATCH_POINTS:
      *((uint32_t*)value) = static_cast<uint32_t>(
          1 << properties_->Capability.ui32.WatchPointsTotalBits);
      break;
    case HSA_AMD_AGENT_INFO_BDFID:
      *((uint32_t*)value) = static_cast<uint32_t>(properties_->LocationId);
      break;
    case HSA_AMD_AGENT_INFO_MEMORY_WIDTH:
      *((uint32_t*)value) = memory_bus_width_;
      break;
    case HSA_AMD_AGENT_INFO_MEMORY_MAX_FREQUENCY:
      *((uint32_t*)value) = memory_max_frequency_;
      break;

    // The code copies HsaCoreProperties.MarketingName a Unicode string
    // which is encoded in UTF-16 as a 7-bit ASCII string
    case HSA_AMD_AGENT_INFO_PRODUCT_NAME: {
      std::memset(value, 0, HSA_PUBLIC_NAME_SIZE);
      char* temp = reinterpret_cast<char*>(value);
      for (uint32_t idx = 0;
           properties_->MarketingName[idx] != 0 && idx < HSA_PUBLIC_NAME_SIZE - 1; idx++) {
        temp[idx] = (uint8_t)properties_->MarketingName[idx];
      }
      break;
    }
    case HSA_AMD_AGENT_INFO_MAX_WAVES_PER_CU:
      *((uint32_t*)value) = static_cast<uint32_t>(
          properties_->NumSIMDPerCU * properties_->MaxWavesPerSIMD);
      break;
    case HSA_AMD_AGENT_INFO_NUM_SIMDS_PER_CU:
      *((uint32_t*)value) = properties_->NumSIMDPerCU;
      break;
    case HSA_AMD_AGENT_INFO_NUM_SHADER_ENGINES:
      *((uint32_t*)value) = properties_->NumShaderBanks;
      break;
    case HSA_AMD_AGENT_INFO_NUM_SHADER_ARRAYS_PER_SE:
      *((uint32_t*)value) = properties_->NumArrays;
      break;
    case HSA_AMD_AGENT_INFO_HDP_FLUSH:
      *((hsa_amd_hdp_flush_t*)value) = HDP_flush_;
      break;
    case HSA_AMD_AGENT_INFO_DOMAIN:
      *((uint32_t*)value) = static_cast<uint32_t>(properties_->Domain);
      break;
    case HSA_AMD_AGENT_INFO_COOPERATIVE_QUEUES:
      *((bool*)value) = properties_->NumGws != 0;
      break;
    case HSA_AMD_AGENT_INFO_UUID: {
      uint64_t uuid_value = static_cast<uint64_t>(properties_->UniqueID);

      // Either agent does not support UUID e.g. a Gfx8 agent,
      // or runtime is using an older thunk library that does not
      // support UUID's
      if (uuid_value == 0) {
        char uuid_tmp[] = "GPU-XX";
        snprintf((char*)value, sizeof(uuid_tmp), "%s", uuid_tmp);
        break;
      }

      // DEVICE-> supports UUID, build UUID string to return
      std::stringstream ss;
      ss << "GPU-" << std::setfill('0') << std::setw(sizeof(uint64_t) * 2) << std::hex
         << uuid_value;
      snprintf((char*)value, (ss.str().length() + 1), "%s", (char*)ss.str().c_str());
      break;
    }
    case HSA_AMD_AGENT_INFO_ASIC_REVISION:
      *((uint32_t*)value) = static_cast<uint32_t>(properties_->Capability.ui32.ASICRevision);
      break;
      */
    default:
        return ERROR_INVALID_ARGUMENT;
        break;
    }
    return SUCCESS;
}

status_t GpuAgent::QueueCreate(size_t size, queue_type32_t queue_type,
    core::HsaEventCallback event_callback,
    void* data, uint32_t private_segment_size,
    uint32_t group_segment_size,
    queue_t* queue)
{
    // core::IQueue** queue) {
    // AQL queues must be a power of two in length.
    if (!IsPowerOfTwo(size)) {
        return ERROR_INVALID_ARGUMENT;
    }

    // Enforce max size
    if (size > maxAqlSize_) {
        return ERROR_OUT_OF_RESOURCES;
    }

    // Allocate scratch memory
    ScratchInfo scratch = { 0 };
    if (private_segment_size == UINT_MAX) {
        private_segment_size = (profile_ == HSA_PROFILE_BASE) ? 0 : scratch_per_thread_;
    }

    if (private_segment_size > 262128) {
        return ERROR_OUT_OF_RESOURCES;
    }

    scratch.lanes_per_wave = 64;
    scratch.size_per_thread = alignUp(private_segment_size, 1024 / scratch.lanes_per_wave);
    if (scratch.size_per_thread > 262128) {
        return ERROR_OUT_OF_RESOURCES;
    }
    scratch.size_per_thread = private_segment_size;

    const uint32_t num_cu = properties_->NumFComputeCores / properties_->NumSIMDPerCU;
    scratch.size = scratch.size_per_thread * 32 * scratch.lanes_per_wave * num_cu;
    scratch.queue_base = nullptr;
    scratch.queue_process_offset = 0;

    MAKE_NAMED_SCOPE_GUARD(scratchGuard, [&]() { ReleaseQueueScratch(scratch); });

    if (scratch.size != 0) {
        AcquireQueueScratch(scratch);
        if (scratch.queue_base == nullptr) {
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    // Ensure utility queue has been created.
    // Deferring longer risks exhausting queue count before ISA upload and invalidation capability is
    // ensured.
    queues_[QueueUtility].touch();

    // Create an HW AQL queue
    // *queue = new AqlQueue(this, size, node_id(), scratch, event_callback, data );
    // Create an CSP queue
    // CreateCSPQueue(size, queue_type, event_callback, data, queue);

    // Calculate index of the queue doorbell within the doorbell aperture.
    /* FIXME
  auto doorbell_addr = uintptr_t(aql_queue->signal_.hardware_doorbell_ptr);
  auto doorbell_idx = (doorbell_addr >> 3) & (MAX_NUM_DOORBELLS - 1);
  doorbell_queue_map_[doorbell_idx] = &aql_queue->amd_queue_;
  */

    scratchGuard.Dismiss();
    return SUCCESS;
}

// Creates a agent queue that accepts Pm4 commands while surfacing
// a proxy queue for user to submit kernel dispatch Aql packets
#if 0
status_t GpuAgent::CreateCSPQueue(size_t size, queue_type32_t queue_type,
                     core::IHsaEventCallback event_callback, void* data, queue_t queue) {

    // AQL queues must be a power of two in length.
    if (!IsPowerOfTwo(size)) return ERROR_INVALID_ARGUMENT;

    // Enforce max size
    uint32_t max;
    GetInfo(HSA_AGENT_INFO_QUEUE_MAX_SIZE, &max);
    if (size > max) return ERROR_OUT_OF_RESOURCES;

    // Allocate scratch memory
    hcs::ScratchInfo scratch;
    scratch.size = 0;
    // TODO why need scratch? AcquireQueueScratch(scratch);
    // if (scratch.queue_base == NULL) return ERROR_OUT_OF_RESOURCES;
    // MAKE_NAMED_SCOPE_GUARD(scratchGuard, [&]() { ReleaseQueueScratch(scratch); });

    // Create an emulated AQL queue
    // *queue = new CslProcessor(this, size, node_id(), scratch, event_callback, data );
    agent::CommandQueue* csp_queue = agent::CreateCmdProcessor(
        this, static_cast<uint32_t>(size), this->node_id(), this->properties(), queue_type,
        scratch, event_callback, data);
    if (csp_queue && csp_queue->IsValid()) {
      hsa_memory_register(csp_queue, sizeof(agent::CommandQueue));
      *queue = csp_queue;
      // scratchGuard.Dismiss();
      return SUCCESS;
    }

    delete csp_queue;
    // ReleaseQueueScratch(scratch);
    return ERROR_OUT_OF_RESOURCES;
}
#endif

void GpuAgent::AcquireQueueScratch(ScratchInfo& scratch)
{
    // bool need_queue_scratch_base = (isa_->GetMajorVersion() > 8);   TODO disable scratch since isa_ is not defined
    bool need_queue_scratch_base = false;

    if (scratch.size == 0) {
        scratch.size = queue_scratch_len_;
        scratch.size_per_thread = scratch_per_thread_;
    }
    scratch.retry = false;

    // Fail scratch allocation if per wave limits are exceeded.
    uint64_t size_per_wave = alignUp(scratch.size_per_thread * properties_->WaveFrontSize, 1024);
    if (size_per_wave > MAX_WAVE_SCRATCH) return;

    ScopedAcquire<KernelMutex> lock(&scratch_lock_);
    // Limit to 1/8th of scratch pool for small scratch and 1/4 of that for a single queue.
    size_t small_limit = scratch_pool_.size() >> 3;
    // size_t single_limit = small_limit >> 2;
    size_t single_limit = 146800640; //small_limit >> 2;
    bool large = (scratch.size > single_limit) || (scratch_pool_.size() - scratch_pool_.remaining() + scratch.size > small_limit);
    // large = (isa_->GetMajorVersion() < 8) ? false : large; TODO need fix isa_ is not defined
    large = false;
    if (large)
        scratch.queue_base = scratch_pool_.alloc_high(scratch.size);
    else
        scratch.queue_base = scratch_pool_.alloc(scratch.size);
    large |= scratch.queue_base > scratch_pool_.high_split();
    scratch.large = large;

    scratch.queue_process_offset = (need_queue_scratch_base)
        ? uintptr_t(scratch.queue_base)
        : uintptr_t(scratch.queue_base) - uintptr_t(scratch_pool_.base());

    if (scratch.queue_base != nullptr) {
        if (profile_ == HSA_PROFILE_FULL) return;
        if (profile_ == HSA_PROFILE_BASE) {
            HSAuint64 alternate_va;
            if (DEVICE->MapMemoryToGPU(scratch.queue_base, scratch.size, &alternate_va) == DEVICE_STATUS_SUCCESS) {
                if (large) scratch_used_large_ += scratch.size;
                return;
            }
        }
    }

    // Scratch request failed allocation or mapping.
    scratch_pool_.free(scratch.queue_base);
    scratch.queue_base = nullptr;

    // Retry if large may yield needed space.
    if (scratch_used_large_ != 0) {
        if (AddScratchNotifier(scratch.queue_retry, 0x8000000000000000ull)) scratch.retry = true;
        // scratch.retry = true;
        return;
    }

    // Attempt to trim the maximum number of concurrent waves to allow scratch to fit.
    if (Runtime::runtime_singleton_->flag().enable_queue_fault_message())
        debug_print("Failed to map requested scratch - reducing queue occupancy.\n");
    uint64_t num_cus = properties_->NumFComputeCores / properties_->NumSIMDPerCU;
    uint64_t total_waves = scratch.size / size_per_wave;
    uint64_t waves_per_cu = total_waves / num_cus;
    while (waves_per_cu != 0) {
        size_t size = waves_per_cu * num_cus * size_per_wave;
        void* base = scratch_pool_.alloc(size);
        HSAuint64 alternate_va;
        if ((base != nullptr) && ((profile_ == HSA_PROFILE_FULL) || (DEVICE->MapMemoryToGPU(base, size, &alternate_va) == DEVICE_STATUS_SUCCESS))) {
            // Scratch allocated and either full profile or map succeeded.
            scratch.queue_base = base;
            scratch.size = size;
            scratch.queue_process_offset = (need_queue_scratch_base)
                ? uintptr_t(scratch.queue_base)
                : uintptr_t(scratch.queue_base) - uintptr_t(scratch_pool_.base());
            scratch.large = true;
            scratch_used_large_ += scratch.size;
            return;
        }
        scratch_pool_.free(base);
        waves_per_cu--;
    }

    // Failed to allocate minimal scratch
    assert(scratch.queue_base == nullptr && "bad scratch data");
    if (Runtime::runtime_singleton_->flag().enable_queue_fault_message())
        debug_print("Could not allocate scratch for one wave per CU.\n");
}

void GpuAgent::ReleaseQueueScratch(ScratchInfo& scratch)
{
    if (scratch.queue_base == nullptr) {
        return;
    }

    ScopedAcquire<KernelMutex> lock(&scratch_lock_);
    if (profile_ == HSA_PROFILE_BASE) {
        if (DEVICE_STATUS_SUCCESS != DEVICE->UnmapMemoryToGPU(scratch.queue_base)) {
            assert(false && "Unmap scratch subrange failed!");
        }
    }
    scratch_pool_.free(scratch.queue_base);
    scratch.queue_base = nullptr;

    if (scratch.large) scratch_used_large_ -= scratch.size;

    // Notify waiters that additional scratch may be available.
    for (auto notifier : scratch_notifiers_) {
        // hsa_signal_or_relaxed(notifier.first, notifier.second);
        // Runtime::runtime_singleton_->co_stream_->signal_or_relaxed({notifier.first, notifier.second});
        Runtime::runtime_singleton_->co_stream_->signal_or_relaxed(signal_t{notifier.first}, notifier.second);
    }
    ClearScratchNotifiers();
}

#if 0
void GpuAgent::TranslateTime(core::ISignal* signal, hsa_amd_profiling_dispatch_time_t& time) {
  uint64_t start, end;
  /*
  signal->GetRawTs(false, start, end);
  // Order is important, we want to translate the end time first to ensure that packet duration is
  // not impacted by clock measurement latency jitter.
  time.end = TranslateTime(end);
  time.start = TranslateTime(start);

  if ((start == 0) || (end == 0) || (start > t1_.GPUClockCounter) || (end > t1_.GPUClockCounter) ||
      (start < t0_.GPUClockCounter) || (end < t0_.GPUClockCounter))
    debug_print("Signal %p time stamps may be invalid.", &signal->signal_);
    */
}
void GpuAgent::TranslateTime(core::ISignal* signal, hsa_amd_profiling_async_copy_time_t& time) {
  uint64_t start, end;
  signal->GetRawTs(true, start, end);
  // Order is important, we want to translate the end time first to ensure that packet duration is
  // not impacted by clock measurement latency jitter.
  time.end = TranslateTime(end);
  time.start = TranslateTime(start);

  if ((start == 0) || (end == 0) || (start > t1_.GPUClockCounter) || (end > t1_.GPUClockCounter) ||
      (start < t0_.GPUClockCounter) || (end < t0_.GPUClockCounter))
    debug_print("Signal %p time stamps may be invalid.", &signal->signal_);
}
#endif

/*
Times during program execution are interpolated to adjust for relative clock drift.
Interval timing may appear as ticks well before process start, leading to large errors due to
frequency adjustment (ie the profiling with NTP problem).  This is fixed by using a fixed frequency
for early times.
Intervals larger than t0_ will be frequency adjusted.  This admits a numerical error of not more
than twice the frequency stability (~10^-5).
*/
#if 0
uint64_t GpuAgent::TranslateTime(uint64_t tick) {
  // Ensure interpolation for times during program execution.
  ScopedAcquire<KernelMutex> lock(&t1_lock_);
  if ((t1_.GPUClockCounter < tick) || (t1_.GPUClockCounter == t0_.GPUClockCounter)) SyncClocks();

  // Good for ~300 yrs
  // uint64_t sysdelta = t1_.SystemClockCounter - t0_.SystemClockCounter;
  // uint64_t gpudelta = t1_.GPUClockCounter - t0_.GPUClockCounter;
  // int64_t offtick = int64_t(tick - t1_.GPUClockCounter);
  //__int128 num = __int128(sysdelta)*__int128(offtick) +
  //__int128(gpudelta)*__int128(t1_.SystemClockCounter);
  //__int128 sysLarge = num / __int128(gpudelta);
  // return sysLarge;

  // Good for ~3.5 months.
  uint64_t system_tick = 0;
  double ratio = double(t1_.SystemClockCounter - t0_.SystemClockCounter) /
      double(t1_.GPUClockCounter - t0_.GPUClockCounter);
  system_tick = uint64_t(ratio * double(int64_t(tick - t1_.GPUClockCounter))) + t1_.SystemClockCounter;

  // tick predates HSA startup - extrapolate with fixed clock ratio
  if (tick < t0_.GPUClockCounter) {
    if (historical_clock_ratio_ == 0.0) historical_clock_ratio_ = ratio;
    system_tick = uint64_t(historical_clock_ratio_ * double(int64_t(tick - t0_.GPUClockCounter))) +
        t0_.SystemClockCounter;
  }

  return system_tick;
}
#endif

bool GpuAgent::current_coherency_type(hsa_amd_coherency_type_t type)
{
    current_coherency_type_ = type;
    return true;
}

uint16_t GpuAgent::GetMicrocodeVersion() const
{
    return (properties_->EngineId.ui32.uCode);
}

uint16_t GpuAgent::GetSdmaMicrocodeVersion() const
{
    // return (properties_->uCodeEngineVersions.uCodeSDMA);
    return 0;
}

void GpuAgent::SyncClocks()
{
    device_status_t err = DEVICE->GetClockCounters(node_id(), &t1_);
    assert(err == DEVICE_STATUS_SUCCESS && "hsaGetClockCounters error");
}
void GpuAgent::BindTrapHandler()
{
    // Make an empty map from doorbell index to queue.
    // The trap handler uses this to retrieve a wave's co_queue_t*.
    auto doorbell_queue_map_size = MAX_NUM_DOORBELLS * sizeof(co_queue_t*);

    doorbell_queue_map_ = (co_queue_t**)Runtime::runtime_singleton_->system_allocator()(
        doorbell_queue_map_size, 0x1000, 0);
    assert(doorbell_queue_map_ != NULL && "Doorbell queue map allocation failed");

    memset(doorbell_queue_map_, 0, doorbell_queue_map_size);

    /*
  const char* src_sp3 = R"(
    var s_trap_info_lo = ttmp0
    var s_trap_info_hi = ttmp1
    var s_tmp0         = ttmp2
    var s_tmp1         = ttmp3
    var s_tmp2         = ttmp4
    var s_tmp3         = ttmp5

    shader TrapHandler
      type(CS)

      // Retrieve the queue inactive signal.
      s_load_dwordx2       [s_tmp0, s_tmp1], s[0:1], 0xC0
      s_waitcnt            lgkmcnt(0)

      // Mask all but one lane of the wavefront.
      s_mov_b64            exec, 0x1

      // Set queue signal value to unhandled exception error.
      s_add_u32            s_tmp0, s_tmp0, 0x8
      s_addc_u32           s_tmp1, s_tmp1, 0x0
      v_mov_b32            v0, s_tmp0
      v_mov_b32            v1, s_tmp1
      v_mov_b32            v2, 0x80000000
      v_mov_b32            v3, 0x0
      flat_atomic_swap_x2  v[0:1], v[0:1], v[2:3]
      s_waitcnt            vmcnt(0)

      // Skip event if the signal was already set to unhandled exception.
      v_cmp_eq_u64         vcc, v[0:1], v[2:3]
      s_cbranch_vccnz      L_SIGNAL_DONE

      // Check for a non-NULL signal event mailbox.
      s_load_dwordx2       [s_tmp2, s_tmp3], [s_tmp0, s_tmp1], 0x8
      s_waitcnt            lgkmcnt(0)
      s_and_b64            [s_tmp2, s_tmp3], [s_tmp2, s_tmp3], [s_tmp2, s_tmp3]
      s_cbranch_scc0       L_SIGNAL_DONE

      // Load the signal event value.
      s_add_u32            s_tmp0, s_tmp0, 0x10
      s_addc_u32           s_tmp1, s_tmp1, 0x0
      s_load_dword         s_tmp0, [s_tmp0, s_tmp1], 0x0
      s_waitcnt            lgkmcnt(0)

      // Write the signal event value to the mailbox.
      v_mov_b32            v0, s_tmp2
      v_mov_b32            v1, s_tmp3
      v_mov_b32            v2, s_tmp0
      flat_store_dword     v[0:1], v2
      s_waitcnt            vmcnt(0)

      // Send an interrupt to trigger event notification.
      s_sendmsg            sendmsg(MSG_INTERRUPT)

    L_SIGNAL_DONE:
      // Halt wavefront and exit trap.
      s_sethalt            1
      s_rfe_b64            [s_trap_info_lo, s_trap_info_hi]
    end
  )";
  */

    // Assemble the trap handler source code.
    // AssembleShader("TrapHandler", AssembleTarget::ISA, trap_code_buf_, trap_code_buf_size_);

    // Bind the trap handler to this node.
    // device_status_t err = DEVICE->SetTrapHandler(node_id(), trap_code_buf_, trap_code_buf_size_,
    //                                         doorbell_queue_map_, doorbell_queue_map_size);
    //assert(err == SUCCESS && "hsaKmtSetTrapHandler() failed");
}

void GpuAgent::InvalidateCodeCaches()
{
    // Submit the command to the utility queue and wait for it to complete.
    // queues_[QueueUtility]->ExecutePM4(cache_inv, sizeof(cache_inv));
}
lazy_ptr<core::IBlit>& GpuAgent::GetXgmiBlit(const core::IAgent& dst_agent)
{
    // Determine if destination is a member xgmi peers list
    uint32_t xgmi_engine_cnt = properties_->NumSdmaXgmiEngines;
    assert((xgmi_engine_cnt > 0) && ("Illegal condition, should not happen"));

    for (uint32_t idx = 0; idx < xgmi_peer_list_.size(); idx++) {
        const core::IAgent* dst_handle = &dst_agent; // .public_handle().handle;
        const core::IAgent* peer_handle = xgmi_peer_list_[idx]; // ->public_handle().handle;
        if (peer_handle == dst_handle) {
            return blits_[(idx % xgmi_engine_cnt) + DefaultBlitCount];
        }
    }

    // Add agent to the xGMI neighbours list
    xgmi_peer_list_.push_back(&dst_agent);
    return blits_[((xgmi_peer_list_.size() - 1) % xgmi_engine_cnt) + DefaultBlitCount];
}

lazy_ptr<core::IBlit>& GpuAgent::GetPcieBlit(const core::IAgent& dst_agent,
    const core::IAgent& src_agent)
{
    lazy_ptr<core::IBlit>& blit = (src_agent.agent_type() == core::IAgent::kCpu && dst_agent.agent_type() == core::IAgent::kGpu)
        ? blits_[BlitHostToDev]
        : (src_agent.agent_type() == core::IAgent::kGpu && dst_agent.agent_type() == core::IAgent::kCpu)
            ? blits_[BlitDevToHost]
            : blits_[BlitDevToHost];
    return blit;
}

lazy_ptr<core::IBlit>& GpuAgent::GetBlitObject(const core::IAgent& dst_agent,
    const core::IAgent& src_agent)
{
    // At this point it is guaranteed that one of
    // the two agents is a GPU, potentially both
    assert(((src_agent.agent_type() == core::IAgent::kGpu) || (dst_agent.agent_type() == core::IAgent::kGpu)) && ("Both agents are CPU agents which is not expected"));

    // Determine if Src and Dst agents are same
    if ((&src_agent) == (&dst_agent)) {
        return blits_[BlitDevToDev];
    }

    return GetPcieBlit(dst_agent, src_agent);
#if 0
  // Acquire Hive Id of Src and Dst agents
  uint64_t src_hive_id = src_agent.HiveId();
  uint64_t dst_hive_id = dst_agent.HiveId();

  // Bind to a PCIe facing Blit object if the two
  // agents have different Hive Ids. This can occur
  // for following scenarios:
  //
  //  Neither agent claims membership in a Hive
  //   srcId = 0 <-> dstId = 0;
  //
  //  Src agent claims membership in a Hive
  //   srcId = 0x1926 <-> dstId = 0;
  //
  //  Dst agent claims membership in a Hive
  //   srcId = 0 <-> dstId = 0x1123;
  //
  //  Both agent claims membership in a Hive
  //  and the  Hives are different
  //   srcId = 0x1926 <-> dstId = 0x1123;
  //
  if ((dst_hive_id != src_hive_id) || (dst_hive_id == 0)) {
    return GetPcieBlit(dst_agent, src_agent);
  }

  // Accommodates platforms where agents have xGMI
  // links but without sdmaXgmiEngines e.g. Vega 20
  if (properties_->NumSdmaXgmiEngines == 0) {
    return GetPcieBlit(dst_agent, src_agent);
  }

  return GetXgmiBlit(dst_agent);
#endif
}

// }  // namespace
