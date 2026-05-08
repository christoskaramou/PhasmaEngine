#pragma once

#include <webgpu/webgpu.h>
#include "BindGroup.h"
#include "API/Vulkan/VulkanHeaders.h"

struct WGPUPipelineLayoutImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    VkPipelineLayout vkLayout = VK_NULL_HANDLE;
    std::vector<WGPUBindGroupLayoutImpl *> bindGroupLayouts;
    std::vector<vk::DescriptorSetLayout> ownedEmptySetLayouts;
    bool invalid = false;
};
