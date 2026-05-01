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
} // namespace pe
