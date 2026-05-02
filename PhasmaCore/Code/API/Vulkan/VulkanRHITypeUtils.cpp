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

    vk::PrimitiveTopology ToVkTopology(PeTopology topology)
    {
        switch (topology)
        {
        case PE_TOPOLOGY_POINT_LIST:
            return vk::PrimitiveTopology::ePointList;
        case PE_TOPOLOGY_LINE_LIST:
            return vk::PrimitiveTopology::eLineList;
        case PE_TOPOLOGY_LINE_STRIP:
            return vk::PrimitiveTopology::eLineStrip;
        case PE_TOPOLOGY_TRIANGLE_LIST:
            return vk::PrimitiveTopology::eTriangleList;
        case PE_TOPOLOGY_TRIANGLE_STRIP:
            return vk::PrimitiveTopology::eTriangleStrip;
        case PE_TOPOLOGY_TRIANGLE_FAN:
            return vk::PrimitiveTopology::eTriangleFan;
        case PE_TOPOLOGY_PATCH_LIST:
            return vk::PrimitiveTopology::ePatchList;
        default:
            PE_ERROR("Unknown PeTopology: %u", static_cast<uint32_t>(topology));
            return vk::PrimitiveTopology::eTriangleList;
        }
    }

    PeTopology FromVkTopology(vk::PrimitiveTopology topology)
    {
        switch (topology)
        {
        case vk::PrimitiveTopology::ePointList:
            return PE_TOPOLOGY_POINT_LIST;
        case vk::PrimitiveTopology::eLineList:
            return PE_TOPOLOGY_LINE_LIST;
        case vk::PrimitiveTopology::eLineStrip:
            return PE_TOPOLOGY_LINE_STRIP;
        case vk::PrimitiveTopology::eTriangleList:
            return PE_TOPOLOGY_TRIANGLE_LIST;
        case vk::PrimitiveTopology::eTriangleStrip:
            return PE_TOPOLOGY_TRIANGLE_STRIP;
        case vk::PrimitiveTopology::eTriangleFan:
            return PE_TOPOLOGY_TRIANGLE_FAN;
        case vk::PrimitiveTopology::ePatchList:
            return PE_TOPOLOGY_PATCH_LIST;
        default:
            PE_ERROR("Unknown vk::PrimitiveTopology: %u", static_cast<uint32_t>(topology));
            return PE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    vk::PolygonMode ToVkPolygonMode(PePolygonMode mode)
    {
        switch (mode)
        {
        case PE_POLYGON_MODE_FILL:
            return vk::PolygonMode::eFill;
        case PE_POLYGON_MODE_LINE:
            return vk::PolygonMode::eLine;
        case PE_POLYGON_MODE_POINT:
            return vk::PolygonMode::ePoint;
        default:
            PE_ERROR("Unknown PePolygonMode: %u", static_cast<uint32_t>(mode));
            return vk::PolygonMode::eFill;
        }
    }

    PePolygonMode FromVkPolygonMode(vk::PolygonMode mode)
    {
        switch (mode)
        {
        case vk::PolygonMode::eFill:
            return PE_POLYGON_MODE_FILL;
        case vk::PolygonMode::eLine:
            return PE_POLYGON_MODE_LINE;
        case vk::PolygonMode::ePoint:
            return PE_POLYGON_MODE_POINT;
        default:
            PE_ERROR("Unknown vk::PolygonMode: %u", static_cast<uint32_t>(mode));
            return PE_POLYGON_MODE_FILL;
        }
    }

    vk::CullModeFlags ToVkCullMode(PeCullMode mode)
    {
        switch (mode)
        {
        case PE_CULL_MODE_NONE:
            return vk::CullModeFlagBits::eNone;
        case PE_CULL_MODE_FRONT:
            return vk::CullModeFlagBits::eFront;
        case PE_CULL_MODE_BACK:
            return vk::CullModeFlagBits::eBack;
        case PE_CULL_MODE_FRONT_AND_BACK:
            return vk::CullModeFlagBits::eFrontAndBack;
        default:
            PE_ERROR("Unknown PeCullMode: %u", static_cast<uint32_t>(mode));
            return vk::CullModeFlagBits::eBack;
        }
    }

    PeCullMode FromVkCullMode(vk::CullModeFlags mode)
    {
        if (mode == vk::CullModeFlagBits::eNone)
            return PE_CULL_MODE_NONE;
        if (mode == vk::CullModeFlagBits::eFront)
            return PE_CULL_MODE_FRONT;
        if (mode == vk::CullModeFlagBits::eBack)
            return PE_CULL_MODE_BACK;
        if (mode == vk::CullModeFlagBits::eFrontAndBack)
            return PE_CULL_MODE_FRONT_AND_BACK;
        PE_ERROR("Unknown vk::CullModeFlags: %u", static_cast<uint32_t>(VkCullModeFlags(mode)));
        return PE_CULL_MODE_BACK;
    }

    vk::CompareOp ToVkCompareOp(PeCompareOp op)
    {
        switch (op)
        {
        case PE_COMPARE_OP_NEVER:
            return vk::CompareOp::eNever;
        case PE_COMPARE_OP_LESS:
            return vk::CompareOp::eLess;
        case PE_COMPARE_OP_EQUAL:
            return vk::CompareOp::eEqual;
        case PE_COMPARE_OP_LESS_OR_EQUAL:
            return vk::CompareOp::eLessOrEqual;
        case PE_COMPARE_OP_GREATER:
            return vk::CompareOp::eGreater;
        case PE_COMPARE_OP_NOT_EQUAL:
            return vk::CompareOp::eNotEqual;
        case PE_COMPARE_OP_GREATER_OR_EQUAL:
            return vk::CompareOp::eGreaterOrEqual;
        case PE_COMPARE_OP_ALWAYS:
            return vk::CompareOp::eAlways;
        default:
            PE_ERROR("Unknown PeCompareOp: %u", static_cast<uint32_t>(op));
            return vk::CompareOp::eAlways;
        }
    }

    PeCompareOp FromVkCompareOp(vk::CompareOp op)
    {
        switch (op)
        {
        case vk::CompareOp::eNever:
            return PE_COMPARE_OP_NEVER;
        case vk::CompareOp::eLess:
            return PE_COMPARE_OP_LESS;
        case vk::CompareOp::eEqual:
            return PE_COMPARE_OP_EQUAL;
        case vk::CompareOp::eLessOrEqual:
            return PE_COMPARE_OP_LESS_OR_EQUAL;
        case vk::CompareOp::eGreater:
            return PE_COMPARE_OP_GREATER;
        case vk::CompareOp::eNotEqual:
            return PE_COMPARE_OP_NOT_EQUAL;
        case vk::CompareOp::eGreaterOrEqual:
            return PE_COMPARE_OP_GREATER_OR_EQUAL;
        case vk::CompareOp::eAlways:
            return PE_COMPARE_OP_ALWAYS;
        default:
            PE_ERROR("Unknown vk::CompareOp: %u", static_cast<uint32_t>(op));
            return PE_COMPARE_OP_ALWAYS;
        }
    }

    vk::StencilOp ToVkStencilOp(PeStencilOp op)
    {
        switch (op)
        {
        case PE_STENCIL_OP_KEEP:
            return vk::StencilOp::eKeep;
        case PE_STENCIL_OP_ZERO:
            return vk::StencilOp::eZero;
        case PE_STENCIL_OP_REPLACE:
            return vk::StencilOp::eReplace;
        case PE_STENCIL_OP_INCREMENT_AND_CLAMP:
            return vk::StencilOp::eIncrementAndClamp;
        case PE_STENCIL_OP_DECREMENT_AND_CLAMP:
            return vk::StencilOp::eDecrementAndClamp;
        case PE_STENCIL_OP_INVERT:
            return vk::StencilOp::eInvert;
        case PE_STENCIL_OP_INCREMENT_AND_WRAP:
            return vk::StencilOp::eIncrementAndWrap;
        case PE_STENCIL_OP_DECREMENT_AND_WRAP:
            return vk::StencilOp::eDecrementAndWrap;
        default:
            PE_ERROR("Unknown PeStencilOp: %u", static_cast<uint32_t>(op));
            return vk::StencilOp::eKeep;
        }
    }

    PeStencilOp FromVkStencilOp(vk::StencilOp op)
    {
        switch (op)
        {
        case vk::StencilOp::eKeep:
            return PE_STENCIL_OP_KEEP;
        case vk::StencilOp::eZero:
            return PE_STENCIL_OP_ZERO;
        case vk::StencilOp::eReplace:
            return PE_STENCIL_OP_REPLACE;
        case vk::StencilOp::eIncrementAndClamp:
            return PE_STENCIL_OP_INCREMENT_AND_CLAMP;
        case vk::StencilOp::eDecrementAndClamp:
            return PE_STENCIL_OP_DECREMENT_AND_CLAMP;
        case vk::StencilOp::eInvert:
            return PE_STENCIL_OP_INVERT;
        case vk::StencilOp::eIncrementAndWrap:
            return PE_STENCIL_OP_INCREMENT_AND_WRAP;
        case vk::StencilOp::eDecrementAndWrap:
            return PE_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            PE_ERROR("Unknown vk::StencilOp: %u", static_cast<uint32_t>(op));
            return PE_STENCIL_OP_KEEP;
        }
    }

    vk::DynamicState ToVkDynamicState(PeDynamicState state)
    {
        switch (state)
        {
        case PE_DYNAMIC_STATE_VIEWPORT:
            return vk::DynamicState::eViewport;
        case PE_DYNAMIC_STATE_SCISSOR:
            return vk::DynamicState::eScissor;
        case PE_DYNAMIC_STATE_LINE_WIDTH:
            return vk::DynamicState::eLineWidth;
        case PE_DYNAMIC_STATE_DEPTH_BIAS:
            return vk::DynamicState::eDepthBias;
        case PE_DYNAMIC_STATE_BLEND_CONSTANTS:
            return vk::DynamicState::eBlendConstants;
        case PE_DYNAMIC_STATE_DEPTH_BOUNDS:
            return vk::DynamicState::eDepthBounds;
        case PE_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
            return vk::DynamicState::eStencilCompareMask;
        case PE_DYNAMIC_STATE_STENCIL_WRITE_MASK:
            return vk::DynamicState::eStencilWriteMask;
        case PE_DYNAMIC_STATE_STENCIL_REFERENCE:
            return vk::DynamicState::eStencilReference;
        case PE_DYNAMIC_STATE_CULL_MODE:
            return vk::DynamicState::eCullMode;
        case PE_DYNAMIC_STATE_FRONT_FACE:
            return vk::DynamicState::eFrontFace;
        case PE_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
            return vk::DynamicState::ePrimitiveTopology;
        case PE_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
            return vk::DynamicState::eDepthTestEnable;
        case PE_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
            return vk::DynamicState::eDepthWriteEnable;
        case PE_DYNAMIC_STATE_DEPTH_COMPARE_OP:
            return vk::DynamicState::eDepthCompareOp;
        case PE_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
            return vk::DynamicState::eStencilTestEnable;
        default:
            PE_ERROR("Unknown PeDynamicState: %u", static_cast<uint32_t>(state));
            return vk::DynamicState::eViewport;
        }
    }

    PeDynamicState FromVkDynamicState(vk::DynamicState state)
    {
        switch (state)
        {
        case vk::DynamicState::eViewport:
            return PE_DYNAMIC_STATE_VIEWPORT;
        case vk::DynamicState::eScissor:
            return PE_DYNAMIC_STATE_SCISSOR;
        case vk::DynamicState::eLineWidth:
            return PE_DYNAMIC_STATE_LINE_WIDTH;
        case vk::DynamicState::eDepthBias:
            return PE_DYNAMIC_STATE_DEPTH_BIAS;
        case vk::DynamicState::eBlendConstants:
            return PE_DYNAMIC_STATE_BLEND_CONSTANTS;
        case vk::DynamicState::eDepthBounds:
            return PE_DYNAMIC_STATE_DEPTH_BOUNDS;
        case vk::DynamicState::eStencilCompareMask:
            return PE_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        case vk::DynamicState::eStencilWriteMask:
            return PE_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        case vk::DynamicState::eStencilReference:
            return PE_DYNAMIC_STATE_STENCIL_REFERENCE;
        case vk::DynamicState::eCullMode:
            return PE_DYNAMIC_STATE_CULL_MODE;
        case vk::DynamicState::eFrontFace:
            return PE_DYNAMIC_STATE_FRONT_FACE;
        case vk::DynamicState::ePrimitiveTopology:
            return PE_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
        case vk::DynamicState::eDepthTestEnable:
            return PE_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        case vk::DynamicState::eDepthWriteEnable:
            return PE_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        case vk::DynamicState::eDepthCompareOp:
            return PE_DYNAMIC_STATE_DEPTH_COMPARE_OP;
        case vk::DynamicState::eStencilTestEnable:
            return PE_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
        default:
            PE_ERROR("Unknown vk::DynamicState: %u", static_cast<uint32_t>(state));
            return PE_DYNAMIC_STATE_VIEWPORT;
        }
    }

    vk::PresentModeKHR ToVkPresentMode(PePresentMode mode)
    {
        switch (mode)
        {
        case PE_PRESENT_MODE_IMMEDIATE:
            return vk::PresentModeKHR::eImmediate;
        case PE_PRESENT_MODE_MAILBOX:
            return vk::PresentModeKHR::eMailbox;
        case PE_PRESENT_MODE_FIFO:
            return vk::PresentModeKHR::eFifo;
        case PE_PRESENT_MODE_FIFO_RELAXED:
            return vk::PresentModeKHR::eFifoRelaxed;
        default:
            PE_ERROR("Unknown PePresentMode: %u", static_cast<uint32_t>(mode));
            return vk::PresentModeKHR::eFifo;
        }
    }

    PePresentMode FromVkPresentMode(vk::PresentModeKHR mode)
    {
        switch (mode)
        {
        case vk::PresentModeKHR::eImmediate:
            return PE_PRESENT_MODE_IMMEDIATE;
        case vk::PresentModeKHR::eMailbox:
            return PE_PRESENT_MODE_MAILBOX;
        case vk::PresentModeKHR::eFifo:
            return PE_PRESENT_MODE_FIFO;
        case vk::PresentModeKHR::eFifoRelaxed:
            return PE_PRESENT_MODE_FIFO_RELAXED;
        default:
            return PE_PRESENT_MODE_FIFO;
        }
    }
} // namespace pe
