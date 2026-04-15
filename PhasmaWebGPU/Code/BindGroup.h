#pragma once

#include <webgpu/webgpu.h>
#include "API/Descriptor.h"
#include "UsageTracker.h"

struct WGPUDeviceImpl;
struct WGPUTextureViewImpl;

struct WGPUBindGroupLayoutEntryResolved
{
    uint32_t binding = 0;
    WGPUShaderStage visibility = WGPUShaderStage_None;

    WGPUBufferBindingLayout buffer{};
    WGPUSamplerBindingLayout sampler{};
    WGPUTextureBindingLayout texture{};
    WGPUStorageTextureBindingLayout storageTexture{};
    bool hasExternalTexture = false;
};

struct WGPUBindGroupLayoutImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    pe::DescriptorLayout *layout = nullptr;
    std::vector<pe::DescriptorBindingInfo> bindingInfos;
    vk::ShaderStageFlags stage = vk::ShaderStageFlags{};

    std::vector<WGPUBindGroupLayoutEntryResolved> entries;
    uint32_t dynamicOffsetCount = 0;
    bool invalid = false;
    void *exclusivePipeline = nullptr;
};

struct WGPUBindGroupTextureUse
{
    WGPUTextureViewImpl *view = nullptr;
    pwgpu::SubresourceUsageKind kind = pwgpu::SubresourceUsageKind::None;
};

struct WGPUBindGroupImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    pe::Descriptor *descriptor = nullptr;
    WGPUBindGroupLayoutImpl *layout = nullptr;
    bool invalid = false;

    std::vector<WGPUBindGroupTextureUse> textureUses;
    std::vector<WGPUBufferImpl *> bufferUses;

    struct DynamicBinding
    {
        uint32_t binding = 0;
        WGPUBufferImpl *buffer = nullptr;
        uint64_t baseOffset = 0;
        uint64_t bindingSize = 0;
    };
    std::vector<DynamicBinding> dynamicBindings;
};
