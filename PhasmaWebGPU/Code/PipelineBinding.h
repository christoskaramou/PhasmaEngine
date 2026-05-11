#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "API/RHITypes.h"

namespace pe
{
    class Buffer;
    class CommandBuffer;
} // namespace pe

struct WGPUBindGroupImpl;
struct WGPUComputePipelineImpl;
struct WGPUPipelineLayoutImpl;
struct WGPURenderPipelineImpl;

namespace pwgpu
{
    enum class PipelineBindingPoint
    {
        Render,
        Compute
    };

    struct WebGPUBindGroupCacheEntry
    {
        WGPUPipelineLayoutImpl *layout = nullptr;
        WGPUBindGroupImpl *group = nullptr;
        std::vector<uint32_t> dynamicOffsets;
    };

    struct WebGPUVertexBufferCacheEntry
    {
        pe::Buffer *buffer = nullptr;
        size_t offset = 0;
        uint32_t bindingCount = 0;
    };

    struct WebGPUBindingCache
    {
        WGPURenderPipelineImpl *renderPipeline = nullptr;
        WGPUComputePipelineImpl *computePipeline = nullptr;
        bool descriptorBufferBound = false;
        bool hasDescriptorBufferModel = false;
        bool descriptorBufferModel = false;
        std::vector<WebGPUBindGroupCacheEntry> renderBindGroups;
        std::vector<WebGPUBindGroupCacheEntry> computeBindGroups;
        std::vector<WebGPUVertexBufferCacheEntry> vertexBuffers;
        pe::Buffer *indexBuffer = nullptr;
        size_t indexBufferOffset = 0;
        PeIndexType indexBufferType = PE_INDEX_TYPE_UINT32;
    };

    bool BindWebGPURenderPipeline(pe::CommandBuffer *cmd,
                                  WGPURenderPipelineImpl *pipeline,
                                  WebGPUBindingCache *cache = nullptr);
    bool BindWebGPUComputePipeline(pe::CommandBuffer *cmd,
                                   WGPUComputePipelineImpl *pipeline,
                                   WebGPUBindingCache *cache = nullptr);
    bool BindWebGPUBindGroup(pe::CommandBuffer *cmd,
                             PipelineBindingPoint point,
                             WGPUPipelineLayoutImpl *layout,
                             uint32_t groupIndex,
                             WGPUBindGroupImpl *group,
                             size_t dynamicOffsetCount,
                             const uint32_t *dynamicOffsets,
                             WebGPUBindingCache *cache = nullptr);
    bool DispatchWebGPUCompute(pe::CommandBuffer *cmd, uint32_t x, uint32_t y, uint32_t z);
    bool BindWebGPUVertexBuffer(pe::CommandBuffer *cmd,
                                pe::Buffer *buffer,
                                size_t offset,
                                uint32_t firstBinding,
                                uint32_t bindingCount,
                                WebGPUBindingCache *cache = nullptr);
    bool BindWebGPUIndexBuffer(pe::CommandBuffer *cmd,
                               pe::Buffer *buffer,
                               size_t offset,
                               PeIndexType indexType,
                               WebGPUBindingCache *cache = nullptr);
    void DrawWebGPU(pe::CommandBuffer *cmd,
                    uint32_t vertexCount,
                    uint32_t instanceCount,
                    uint32_t firstVertex,
                    uint32_t firstInstance);
    void DrawIndexedWebGPU(pe::CommandBuffer *cmd,
                           uint32_t indexCount,
                           uint32_t instanceCount,
                           uint32_t firstIndex,
                           int32_t vertexOffset,
                           uint32_t firstInstance);
    void RebindWebGPUCompatibleBindGroups(
        pe::CommandBuffer *cmd,
        PipelineBindingPoint point,
        WGPUPipelineLayoutImpl *layout,
        const std::vector<WGPUBindGroupImpl *> &currentBindGroups,
        const std::vector<std::vector<uint32_t>> *currentDynamicOffsets = nullptr,
        WebGPUBindingCache *cache = nullptr);
} // namespace pwgpu
