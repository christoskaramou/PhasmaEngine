#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pe
{
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

    bool BindWebGPURenderPipeline(pe::CommandBuffer *cmd, WGPURenderPipelineImpl *pipeline);
    bool BindWebGPUComputePipeline(pe::CommandBuffer *cmd, WGPUComputePipelineImpl *pipeline);
    bool BindWebGPUBindGroup(pe::CommandBuffer *cmd,
                             PipelineBindingPoint point,
                             WGPUPipelineLayoutImpl *layout,
                             uint32_t groupIndex,
                             WGPUBindGroupImpl *group,
                             size_t dynamicOffsetCount,
                             const uint32_t *dynamicOffsets);
    void RebindWebGPUCompatibleBindGroups(
        pe::CommandBuffer *cmd,
        PipelineBindingPoint point,
        WGPUPipelineLayoutImpl *layout,
        const std::vector<WGPUBindGroupImpl *> &currentBindGroups,
        const std::vector<std::vector<uint32_t>> *currentDynamicOffsets = nullptr);
} // namespace pwgpu
