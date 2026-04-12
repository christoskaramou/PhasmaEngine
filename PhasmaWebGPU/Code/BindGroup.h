#pragma once

#include <webgpu/webgpu.h>
#include "API/Descriptor.h"

struct WGPUBindGroupLayoutImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::DescriptorLayout *layout = nullptr;
    std::vector<pe::DescriptorBindingInfo> bindingInfos;
    vk::ShaderStageFlags stage = vk::ShaderStageFlags{};
};

struct WGPUBindGroupImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Descriptor *descriptor = nullptr;
    WGPUBindGroupLayoutImpl *layout = nullptr;
};
