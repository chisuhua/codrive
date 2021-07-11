#include "inc/GpuMemory.h"

GpuMemory::GpuMemory(Device* device) : device_(device)
{
    scratch_physical =  new ReservedVma(device, nullptr, nullptr);
    gpuvm_aperture =  new ReservedVma(device, nullptr, nullptr);
    // gpuvm_aperture =  new MmapVma(0, 0);
}

GpuMemory::~GpuMemory() {
    delete scratch_physical;
    delete gpuvm_aperture;
}

