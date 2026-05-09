#pragma once

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;
struct WGPUBindGroupLayoutImpl;

struct VertexBufferLayoutInfo
{
    bool used = false;
    uint64_t arrayStride = 0;
    WGPUVertexStepMode stepMode = WGPUVertexStepMode_Vertex;
    uint64_t lastStride = 0;
};

struct VertexBufferBinding
{
    bool bound = false;
    uint64_t size = 0;
};

struct WGPURenderPipelineImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    PeBackendHandle backendPipeline = 0;
    WGPUPipelineLayoutImpl *layout = nullptr;

    bool writesDepth = false;
    bool writesStencil = false;
    uint32_t sampleCount = 1;
    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;

    std::vector<VertexBufferLayoutInfo> vertexBufferLayouts;
};

// Bind-presence without stride arithmetic: indirect draws read counts from a GPU buffer.
inline bool ValidateDrawBindPresence(const std::vector<VertexBufferLayoutInfo> &layouts,
                                     const std::vector<VertexBufferBinding> &bindings)
{
    for (size_t slot = 0; slot < layouts.size(); ++slot)
    {
        const auto &l = layouts[slot];
        if (!l.used)
            continue;
        bool hasBinding = (slot < bindings.size() && bindings[slot].bound);
        if (!hasBinding)
            return false;
    }
    return true;
}

inline bool ValidateDrawVertexState(const std::vector<VertexBufferLayoutInfo> &layouts,
                                    const std::vector<VertexBufferBinding> &bindings,
                                    uint32_t firstVertex, uint32_t vertexCount,
                                    uint32_t firstInstance, uint32_t instanceCount,
                                    bool indexed)
{
    for (size_t slot = 0; slot < layouts.size(); ++slot)
    {
        const auto &l = layouts[slot];
        if (!l.used)
            continue;
        bool hasBinding = (slot < bindings.size() && bindings[slot].bound);
        if (!hasBinding)
            return false;
        if (indexed && l.stepMode != WGPUVertexStepMode_Instance)
            continue;
        uint64_t strideCount = (l.stepMode == WGPUVertexStepMode_Instance)
                                   ? static_cast<uint64_t>(firstInstance) + instanceCount
                                   : static_cast<uint64_t>(firstVertex) + vertexCount;
        if (strideCount == 0)
            continue;
        uint64_t required = (strideCount - 1) * l.arrayStride + l.lastStride;
        if (required > bindings[slot].size)
            return false;
    }
    return true;
}
