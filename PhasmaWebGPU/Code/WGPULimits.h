#pragma once

#include <webgpu/webgpu.h>
#include "API/Vulkan/VulkanHeaders.h"

namespace pwgpu
{

    inline void FillLimits(WGPULimits &out, const VkPhysicalDeviceLimits &vk)
    {
        out.nextInChain = nullptr;
        out.maxTextureDimension1D = vk.maxImageDimension1D;
        out.maxTextureDimension2D = vk.maxImageDimension2D;
        out.maxTextureDimension3D = vk.maxImageDimension3D;
        out.maxTextureArrayLayers = vk.maxImageArrayLayers;
        out.maxBindGroups = vk.maxBoundDescriptorSets;
        out.maxDynamicUniformBuffersPerPipelineLayout = vk.maxDescriptorSetUniformBuffersDynamic;
        out.maxDynamicStorageBuffersPerPipelineLayout = vk.maxDescriptorSetStorageBuffersDynamic;
        out.maxSampledTexturesPerShaderStage = vk.maxPerStageDescriptorSampledImages;
        out.maxSamplersPerShaderStage = vk.maxPerStageDescriptorSamplers;
        out.maxStorageBuffersPerShaderStage = vk.maxPerStageDescriptorStorageBuffers;
        out.maxStorageTexturesPerShaderStage = vk.maxPerStageDescriptorStorageImages;
        out.maxUniformBuffersPerShaderStage = vk.maxPerStageDescriptorUniformBuffers;
        out.maxUniformBufferBindingSize = vk.maxUniformBufferRange;
        out.maxStorageBufferBindingSize = vk.maxStorageBufferRange;
        out.minUniformBufferOffsetAlignment = static_cast<uint32_t>(vk.minUniformBufferOffsetAlignment);
        out.minStorageBufferOffsetAlignment = static_cast<uint32_t>(vk.minStorageBufferOffsetAlignment);
        out.maxVertexBuffers = vk.maxVertexInputBindings;
        out.maxBufferSize = vk.maxStorageBufferRange;
        out.maxVertexAttributes = vk.maxVertexInputAttributes;
        out.maxVertexBufferArrayStride = vk.maxVertexInputBindingStride;
        out.maxInterStageShaderVariables = vk.maxVertexOutputComponents / 4;
        out.maxColorAttachments = vk.maxColorAttachments;
        // Vulkan has no direct analogue; 64 is the Dawn/wgpu-native value (≥ spec default 32).
        out.maxColorAttachmentBytesPerSample = 64;
        out.maxComputeWorkgroupStorageSize = vk.maxComputeSharedMemorySize;
        out.maxComputeInvocationsPerWorkgroup = vk.maxComputeWorkGroupInvocations;
        out.maxComputeWorkgroupSizeX = vk.maxComputeWorkGroupSize[0];
        out.maxComputeWorkgroupSizeY = vk.maxComputeWorkGroupSize[1];
        out.maxComputeWorkgroupSizeZ = vk.maxComputeWorkGroupSize[2];
        // Scalar per-dimension limit: min across axes so every axis reaches this value.
        {
            const uint32_t cx = vk.maxComputeWorkGroupCount[0];
            const uint32_t cy = vk.maxComputeWorkGroupCount[1];
            const uint32_t cz = vk.maxComputeWorkGroupCount[2];
            out.maxComputeWorkgroupsPerDimension = std::min(cx, std::min(cy, cz));
        }
        out.maxImmediateSize = 0;

        // ×2 = render pipeline shader stage count (spec §4.2.1 derived quantity).
        const uint32_t perStage = out.maxSampledTexturesPerShaderStage +
                                  out.maxSamplersPerShaderStage +
                                  out.maxStorageBuffersPerShaderStage +
                                  out.maxStorageTexturesPerShaderStage +
                                  out.maxUniformBuffersPerShaderStage;
        out.maxBindingsPerBindGroup = perStage * 2u;

        out.maxBindGroupsPlusVertexBuffers = out.maxBindGroups + out.maxVertexBuffers;
    }

    inline WGPULimits DefaultLimits()
    {
        WGPULimits lim{};
        lim.maxTextureDimension1D = 8192;
        lim.maxTextureDimension2D = 8192;
        lim.maxTextureDimension3D = 2048;
        lim.maxTextureArrayLayers = 256;
        lim.maxBindGroups = 4;
        lim.maxBindGroupsPlusVertexBuffers = 24;
        lim.maxBindingsPerBindGroup = 1000;
        lim.maxDynamicUniformBuffersPerPipelineLayout = 8;
        lim.maxDynamicStorageBuffersPerPipelineLayout = 4;
        lim.maxSampledTexturesPerShaderStage = 16;
        lim.maxSamplersPerShaderStage = 16;
        lim.maxStorageBuffersPerShaderStage = 8;
        lim.maxStorageTexturesPerShaderStage = 4;
        lim.maxUniformBuffersPerShaderStage = 12;
        lim.maxUniformBufferBindingSize = 65536;
        lim.maxStorageBufferBindingSize = 134217728;
        lim.minUniformBufferOffsetAlignment = 256;
        lim.minStorageBufferOffsetAlignment = 256;
        lim.maxVertexBuffers = 8;
        lim.maxBufferSize = 268435456;
        lim.maxVertexAttributes = 16;
        lim.maxVertexBufferArrayStride = 2048;
        lim.maxInterStageShaderVariables = 16;
        lim.maxColorAttachments = 8;
        lim.maxColorAttachmentBytesPerSample = 32;
        lim.maxComputeWorkgroupStorageSize = 16384;
        lim.maxComputeInvocationsPerWorkgroup = 256;
        lim.maxComputeWorkgroupSizeX = 256;
        lim.maxComputeWorkgroupSizeY = 256;
        lim.maxComputeWorkgroupSizeZ = 64;
        lim.maxComputeWorkgroupsPerDimension = 65535;
        lim.maxImmediateSize = 0;
        return lim;
    }

    WGPUStatus ValidateRequestedLimits(const WGPULimits &adapterLim,
                                       const WGPULimits &requested,
                                       std::string &outFirstBadLimit);

    // Spec §4.2.1 default-or-better sweep: maximum limits must be ≥ default,
    // alignment limits must be ≤ default and power-of-two.
    WGPUStatus ValidateAdapterLimits(const WGPULimits &adapterLim,
                                     std::string &outFirstBadLimit);

    WGPULimits ResolveDeviceLimits(const WGPULimits &adapterLim, const WGPULimits *requested);

} // namespace pwgpu
