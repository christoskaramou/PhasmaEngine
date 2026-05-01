#pragma once

#include "API/RHITypes.h"

namespace pe
{
    vk::AttachmentLoadOp ToVkLoadOp(PeLoadOp op);
    PeLoadOp FromVkLoadOp(vk::AttachmentLoadOp op);

    vk::AttachmentStoreOp ToVkStoreOp(PeStoreOp op);
    PeStoreOp FromVkStoreOp(vk::AttachmentStoreOp op);

    vk::PipelineStageFlags2 ToVkPipelineStageFlags(PeBarrierSync stages);
    PeBarrierSync FromVkPipelineStageFlags(vk::PipelineStageFlags2 stages);

    vk::AccessFlags2 ToVkAccessFlags(PeBarrierAccess access);
    PeBarrierAccess FromVkAccessFlags(vk::AccessFlags2 access);

    vk::PrimitiveTopology ToVkTopology(PeTopology topology);
    PeTopology FromVkTopology(vk::PrimitiveTopology topology);

    vk::PolygonMode ToVkPolygonMode(PePolygonMode mode);
    PePolygonMode FromVkPolygonMode(vk::PolygonMode mode);

    vk::CullModeFlags ToVkCullMode(PeCullMode mode);
    PeCullMode FromVkCullMode(vk::CullModeFlags mode);

    vk::CompareOp ToVkCompareOp(PeCompareOp op);
    PeCompareOp FromVkCompareOp(vk::CompareOp op);

    vk::StencilOp ToVkStencilOp(PeStencilOp op);
    PeStencilOp FromVkStencilOp(vk::StencilOp op);

    vk::DynamicState ToVkDynamicState(PeDynamicState state);
    PeDynamicState FromVkDynamicState(vk::DynamicState state);
} // namespace pe
