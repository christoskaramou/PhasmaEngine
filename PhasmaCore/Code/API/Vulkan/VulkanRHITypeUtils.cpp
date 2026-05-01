#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    vk::AttachmentLoadOp ToVkLoadOp(PeLoadOp op)
    {
        switch (op)
        {
        case PE_LOAD_OP_LOAD:
            return vk::AttachmentLoadOp::eLoad;
        case PE_LOAD_OP_CLEAR:
            return vk::AttachmentLoadOp::eClear;
        case PE_LOAD_OP_DONT_CARE:
            return vk::AttachmentLoadOp::eDontCare;
        default:
            PE_ERROR("Unknown PeLoadOp: %u", static_cast<uint32_t>(op));
            return vk::AttachmentLoadOp::eDontCare;
        }
    }

    PeLoadOp FromVkLoadOp(vk::AttachmentLoadOp op)
    {
        switch (op)
        {
        case vk::AttachmentLoadOp::eLoad:
            return PE_LOAD_OP_LOAD;
        case vk::AttachmentLoadOp::eClear:
            return PE_LOAD_OP_CLEAR;
        case vk::AttachmentLoadOp::eDontCare:
            return PE_LOAD_OP_DONT_CARE;
        default:
            PE_ERROR("Unknown vk::AttachmentLoadOp: %u", static_cast<uint32_t>(op));
            return PE_LOAD_OP_DONT_CARE;
        }
    }

    vk::AttachmentStoreOp ToVkStoreOp(PeStoreOp op)
    {
        switch (op)
        {
        case PE_STORE_OP_STORE:
            return vk::AttachmentStoreOp::eStore;
        case PE_STORE_OP_DONT_CARE:
            return vk::AttachmentStoreOp::eDontCare;
        default:
            PE_ERROR("Unknown PeStoreOp: %u", static_cast<uint32_t>(op));
            return vk::AttachmentStoreOp::eDontCare;
        }
    }

    PeStoreOp FromVkStoreOp(vk::AttachmentStoreOp op)
    {
        switch (op)
        {
        case vk::AttachmentStoreOp::eStore:
            return PE_STORE_OP_STORE;
        case vk::AttachmentStoreOp::eDontCare:
            return PE_STORE_OP_DONT_CARE;
        default:
            PE_ERROR("Unknown vk::AttachmentStoreOp: %u", static_cast<uint32_t>(op));
            return PE_STORE_OP_DONT_CARE;
        }
    }

    vk::PipelineStageFlags2 ToVkPipelineStageFlags(PeBarrierSync stages)
    {
        vk::PipelineStageFlags2 flags{};
        if (stages & PE_STAGE_TOP_OF_PIPE)
            flags |= vk::PipelineStageFlagBits2::eTopOfPipe;
        if (stages & PE_STAGE_VERTEX_SHADER)
            flags |= vk::PipelineStageFlagBits2::eVertexShader;
        if (stages & PE_STAGE_FRAGMENT_SHADER)
            flags |= vk::PipelineStageFlagBits2::eFragmentShader;
        if (stages & PE_STAGE_COMPUTE_SHADER)
            flags |= vk::PipelineStageFlagBits2::eComputeShader;
        if (stages & PE_STAGE_TRANSFER)
            flags |= vk::PipelineStageFlagBits2::eTransfer;
        if (stages & PE_STAGE_COLOR_ATTACHMENT_OUTPUT)
            flags |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        if (stages & PE_STAGE_EARLY_FRAGMENT_TESTS)
            flags |= vk::PipelineStageFlagBits2::eEarlyFragmentTests;
        if (stages & PE_STAGE_LATE_FRAGMENT_TESTS)
            flags |= vk::PipelineStageFlagBits2::eLateFragmentTests;
        if (stages & PE_STAGE_VERTEX_INPUT)
            flags |= vk::PipelineStageFlagBits2::eVertexInput;
        if (stages & PE_STAGE_DRAW_INDIRECT)
            flags |= vk::PipelineStageFlagBits2::eDrawIndirect;
        if (stages & PE_STAGE_ALL_GRAPHICS)
            flags |= vk::PipelineStageFlagBits2::eAllGraphics;
        if (stages & PE_STAGE_ALL_COMMANDS)
            flags |= vk::PipelineStageFlagBits2::eAllCommands;
        if (stages & PE_STAGE_BOTTOM_OF_PIPE)
            flags |= vk::PipelineStageFlagBits2::eBottomOfPipe;
        if (stages & PE_STAGE_RAY_TRACING_SHADER_KHR)
            flags |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        if (stages & PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR)
            flags |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        if (stages & PE_STAGE_CLEAR)
            flags |= vk::PipelineStageFlagBits2::eClear;
        if (stages & PE_STAGE_COPY)
            flags |= vk::PipelineStageFlagBits2::eCopy;
        if (stages & PE_STAGE_HOST)
            flags |= vk::PipelineStageFlagBits2::eHost;
        if (stages & PE_STAGE_INDEX_INPUT)
            flags |= vk::PipelineStageFlagBits2::eIndexInput;
        if (stages & PE_STAGE_VERTEX_ATTRIBUTE_INPUT)
            flags |= vk::PipelineStageFlagBits2::eVertexAttributeInput;
        return flags;
    }

    PeBarrierSync FromVkPipelineStageFlags(vk::PipelineStageFlags2 stages)
    {
        PeBarrierSync flags = PE_STAGE_NONE;
        if (stages & vk::PipelineStageFlagBits2::eTopOfPipe)
            flags |= PE_STAGE_TOP_OF_PIPE;
        if (stages & vk::PipelineStageFlagBits2::eVertexShader)
            flags |= PE_STAGE_VERTEX_SHADER;
        if (stages & vk::PipelineStageFlagBits2::eFragmentShader)
            flags |= PE_STAGE_FRAGMENT_SHADER;
        if (stages & vk::PipelineStageFlagBits2::eComputeShader)
            flags |= PE_STAGE_COMPUTE_SHADER;
        if (stages & vk::PipelineStageFlagBits2::eTransfer)
            flags |= PE_STAGE_TRANSFER;
        if (stages & vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            flags |= PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
        if (stages & vk::PipelineStageFlagBits2::eEarlyFragmentTests)
            flags |= PE_STAGE_EARLY_FRAGMENT_TESTS;
        if (stages & vk::PipelineStageFlagBits2::eLateFragmentTests)
            flags |= PE_STAGE_LATE_FRAGMENT_TESTS;
        if (stages & vk::PipelineStageFlagBits2::eVertexInput)
            flags |= PE_STAGE_VERTEX_INPUT;
        if (stages & vk::PipelineStageFlagBits2::eDrawIndirect)
            flags |= PE_STAGE_DRAW_INDIRECT;
        if (stages & vk::PipelineStageFlagBits2::eAllGraphics)
            flags |= PE_STAGE_ALL_GRAPHICS;
        if (stages & vk::PipelineStageFlagBits2::eAllCommands)
            flags |= PE_STAGE_ALL_COMMANDS;
        if (stages & vk::PipelineStageFlagBits2::eBottomOfPipe)
            flags |= PE_STAGE_BOTTOM_OF_PIPE;
        if (stages & vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
            flags |= PE_STAGE_RAY_TRACING_SHADER_KHR;
        if (stages & vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR)
            flags |= PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        if (stages & vk::PipelineStageFlagBits2::eClear)
            flags |= PE_STAGE_CLEAR;
        if (stages & vk::PipelineStageFlagBits2::eCopy)
            flags |= PE_STAGE_COPY;
        if (stages & vk::PipelineStageFlagBits2::eHost)
            flags |= PE_STAGE_HOST;
        if (stages & vk::PipelineStageFlagBits2::eIndexInput)
            flags |= PE_STAGE_INDEX_INPUT;
        if (stages & vk::PipelineStageFlagBits2::eVertexAttributeInput)
            flags |= PE_STAGE_VERTEX_ATTRIBUTE_INPUT;
        return flags;
    }

    vk::AccessFlags2 ToVkAccessFlags(PeBarrierAccess access)
    {
        vk::AccessFlags2 flags{};
        if (access & PE_ACCESS_SHADER_READ)
            flags |= vk::AccessFlagBits2::eShaderRead;
        if (access & PE_ACCESS_SHADER_WRITE)
            flags |= vk::AccessFlagBits2::eShaderWrite;
        if (access & PE_ACCESS_SHADER_SAMPLED_READ)
            flags |= vk::AccessFlagBits2::eShaderSampledRead;
        if (access & PE_ACCESS_COLOR_ATTACHMENT_READ)
            flags |= vk::AccessFlagBits2::eColorAttachmentRead;
        if (access & PE_ACCESS_COLOR_ATTACHMENT_WRITE)
            flags |= vk::AccessFlagBits2::eColorAttachmentWrite;
        if (access & PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ)
            flags |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        if (access & PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE)
            flags |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        if (access & PE_ACCESS_TRANSFER_READ)
            flags |= vk::AccessFlagBits2::eTransferRead;
        if (access & PE_ACCESS_TRANSFER_WRITE)
            flags |= vk::AccessFlagBits2::eTransferWrite;
        if (access & PE_ACCESS_INDEX_READ)
            flags |= vk::AccessFlagBits2::eIndexRead;
        if (access & PE_ACCESS_VERTEX_ATTRIBUTE_READ)
            flags |= vk::AccessFlagBits2::eVertexAttributeRead;
        if (access & PE_ACCESS_INDIRECT_COMMAND_READ)
            flags |= vk::AccessFlagBits2::eIndirectCommandRead;
        if (access & PE_ACCESS_MEMORY_READ)
            flags |= vk::AccessFlagBits2::eMemoryRead;
        if (access & PE_ACCESS_MEMORY_WRITE)
            flags |= vk::AccessFlagBits2::eMemoryWrite;
        if (access & PE_ACCESS_HOST_WRITE)
            flags |= vk::AccessFlagBits2::eHostWrite;
        if (access & PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR)
            flags |= vk::AccessFlagBits2::eAccelerationStructureReadKHR;
        if (access & PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR)
            flags |= vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        if (access & PE_ACCESS_UNIFORM_READ)
            flags |= vk::AccessFlagBits2::eUniformRead;
        if (access & PE_ACCESS_SHADER_STORAGE_READ)
            flags |= vk::AccessFlagBits2::eShaderStorageRead;
        if (access & PE_ACCESS_SHADER_STORAGE_WRITE)
            flags |= vk::AccessFlagBits2::eShaderStorageWrite;
        return flags;
    }

    PeBarrierAccess FromVkAccessFlags(vk::AccessFlags2 access)
    {
        PeBarrierAccess flags = PE_ACCESS_NONE;
        if (access & vk::AccessFlagBits2::eShaderRead)
            flags |= PE_ACCESS_SHADER_READ;
        if (access & vk::AccessFlagBits2::eShaderWrite)
            flags |= PE_ACCESS_SHADER_WRITE;
        if (access & vk::AccessFlagBits2::eShaderSampledRead)
            flags |= PE_ACCESS_SHADER_SAMPLED_READ;
        if (access & vk::AccessFlagBits2::eColorAttachmentRead)
            flags |= PE_ACCESS_COLOR_ATTACHMENT_READ;
        if (access & vk::AccessFlagBits2::eColorAttachmentWrite)
            flags |= PE_ACCESS_COLOR_ATTACHMENT_WRITE;
        if (access & vk::AccessFlagBits2::eDepthStencilAttachmentRead)
            flags |= PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ;
        if (access & vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
            flags |= PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
        if (access & vk::AccessFlagBits2::eTransferRead)
            flags |= PE_ACCESS_TRANSFER_READ;
        if (access & vk::AccessFlagBits2::eTransferWrite)
            flags |= PE_ACCESS_TRANSFER_WRITE;
        if (access & vk::AccessFlagBits2::eIndexRead)
            flags |= PE_ACCESS_INDEX_READ;
        if (access & vk::AccessFlagBits2::eVertexAttributeRead)
            flags |= PE_ACCESS_VERTEX_ATTRIBUTE_READ;
        if (access & vk::AccessFlagBits2::eIndirectCommandRead)
            flags |= PE_ACCESS_INDIRECT_COMMAND_READ;
        if (access & vk::AccessFlagBits2::eMemoryRead)
            flags |= PE_ACCESS_MEMORY_READ;
        if (access & vk::AccessFlagBits2::eMemoryWrite)
            flags |= PE_ACCESS_MEMORY_WRITE;
        if (access & vk::AccessFlagBits2::eHostWrite)
            flags |= PE_ACCESS_HOST_WRITE;
        if (access & vk::AccessFlagBits2::eAccelerationStructureReadKHR)
            flags |= PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR;
        if (access & vk::AccessFlagBits2::eAccelerationStructureWriteKHR)
            flags |= PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        if (access & vk::AccessFlagBits2::eUniformRead)
            flags |= PE_ACCESS_UNIFORM_READ;
        if (access & vk::AccessFlagBits2::eShaderStorageRead)
            flags |= PE_ACCESS_SHADER_STORAGE_READ;
        if (access & vk::AccessFlagBits2::eShaderStorageWrite)
            flags |= PE_ACCESS_SHADER_STORAGE_WRITE;
        return flags;
    }
} // namespace pe
