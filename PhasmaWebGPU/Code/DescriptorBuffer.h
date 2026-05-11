#pragma once

#include "API/RHITypes.h"

struct WGPUDeviceImpl;
struct WGPUBindGroupLayoutImpl;
struct WGPUPipelineLayoutImpl;

namespace pwgpu
{
    uint64_t AlignDescriptorBufferOffset(uint64_t value, uint64_t alignment);
    size_t DescriptorBufferDescriptorSize(const WGPUDeviceImpl *device, PeBindingType type);

    bool CreateDescriptorBufferLayout(WGPUBindGroupLayoutImpl *bgl);
    bool CreateWebGPUPipelineLayoutBackend(WGPUPipelineLayoutImpl *pl);
    void DestroyDescriptorBufferLayout(WGPUBindGroupLayoutImpl *bgl);

    bool AllocateDescriptorBufferSlice(WGPUDeviceImpl *device,
                                       uint64_t requestedSize,
                                       uint64_t &outOffset,
                                       uint64_t &outSize);
    void FreeDescriptorBufferSlice(WGPUDeviceImpl *device, uint64_t offset, uint64_t size);
} // namespace pwgpu
