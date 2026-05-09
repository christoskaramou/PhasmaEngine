#pragma once

#include <string>
#include <webgpu/webgpu.h>

namespace pe
{
    struct GpuLimits;
}

namespace pwgpu
{

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

    void FillLimitsFromCore(WGPULimits &out, const pe::GpuLimits &limits);

    // Spec §4.2.1 default-or-better sweep: maximum limits must be ≥ default,
    // alignment limits must be ≤ default and power-of-two.
    WGPUStatus ValidateAdapterLimits(const WGPULimits &adapterLim,
                                     std::string &outFirstBadLimit);

    WGPULimits ResolveDeviceLimits(const WGPULimits &adapterLim, const WGPULimits *requested);

} // namespace pwgpu
