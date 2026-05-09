#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;

namespace pwgpu
{
    struct PipelineCreationResult
    {
        PeBackendHandle backendPipeline = 0;
        WGPUErrorType errorType = WGPUErrorType_Internal;
        std::string message;

        bool Succeeded() const { return backendPipeline != 0; }
    };

    struct ComputePipelineBackendDesc
    {
        WGPUDeviceImpl *device = nullptr;
        WGPUPipelineLayoutImpl *layout = nullptr;
        const std::vector<uint32_t> *spirv = nullptr;
        const char *entryPoint = nullptr;
    };

    struct RenderPipelineBackendDesc
    {
        WGPUDeviceImpl *device = nullptr;
        WGPUPipelineLayoutImpl *layout = nullptr;
        const WGPURenderPipelineDescriptor *descriptor = nullptr;
        const std::vector<uint32_t> *vertexSpirv = nullptr;
        const char *vertexEntryPoint = nullptr;
        const std::vector<uint32_t> *fragmentSpirv = nullptr;
        const char *fragmentEntryPoint = nullptr;
        WGPUPrimitiveTopology primitiveTopology = WGPUPrimitiveTopology_TriangleList;
        uint32_t sampleCount = 1;
        bool hasFragment = false;
        bool hasDepthStencil = false;
        bool fragmentWritesFragDepth = false;
    };

    PipelineCreationResult CreateWebGPUComputePipelineBackend(const ComputePipelineBackendDesc &desc);
    PipelineCreationResult CreateWebGPURenderPipelineBackend(const RenderPipelineBackendDesc &desc);
    void DestroyWebGPUPipelineBackend(WGPUDeviceImpl *device, PeBackendHandle backendPipeline);
} // namespace pwgpu
