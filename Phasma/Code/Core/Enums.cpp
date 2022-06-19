/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

namespace pe
{
    void GetVkInfoFromLayout(ImageLayout layout, VkPipelineStageFlags &stageFlags, VkAccessFlags &accessMask)
    {
        switch (layout)
        {
        case ImageLayout::Undefined:
            stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        case ImageLayout::General:
            stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        case ImageLayout::ColorAttachment:
            stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case ImageLayout::DepthStencilAttachment:
            stageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case ImageLayout::DepthStencilReadOnly:
            stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case ImageLayout::ShaderReadOnly:
            stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case ImageLayout::TransferSrc:
            stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case ImageLayout::TransferDst:
            stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case ImageLayout::Preinitialized:
            stageFlags = VK_PIPELINE_STAGE_HOST_BIT;
            accessMask = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case ImageLayout::PresentSrc:
            stageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            accessMask = 0;
            break;
        }

        PE_ERROR("Unsupported image layout");
    }

    void GetInfoFromLayout(ImageLayout layout, PipelineStageFlags &stageFlags, AccessFlags &accessMask)
    {
        switch (layout)
        {
        case ImageLayout::Undefined:
            stageFlags = PipelineStage::TopOfPipeBit;
            accessMask = Access::None;
            break;
        case ImageLayout::General:
            stageFlags = PipelineStage::TopOfPipeBit;
            accessMask = Access::None;
            break;
        case ImageLayout::ColorAttachment:
            stageFlags = PipelineStage::ColorAttachmentOutputBit;
            accessMask = Access::ColorAttachmentWriteBit;
            break;
        case ImageLayout::DepthStencilAttachment:
            stageFlags = PipelineStage::EarlyFragmentTestsBit | PipelineStage::LateFragmentTestsBit;
            accessMask = Access::DepthStencilAttachmentWriteBit;
            break;
        case ImageLayout::DepthStencilReadOnly:
            stageFlags = PipelineStage::FragmentShaderBit;
            accessMask = Access::ShaderReadBit;
            break;
        case ImageLayout::ShaderReadOnly:
            stageFlags = PipelineStage::FragmentShaderBit;
            accessMask = Access::ShaderReadBit;
            break;
        case ImageLayout::TransferSrc:
            stageFlags = PipelineStage::TransferBit;
            accessMask = Access::TransferReadBit;
            break;
        case ImageLayout::TransferDst:
            stageFlags = PipelineStage::TransferBit;
            accessMask = Access::TransferWriteBit;
            break;
        case ImageLayout::Preinitialized:
            stageFlags = PipelineStage::HostBit;
            accessMask = Access::HostWriteBit;
            break;
        case ImageLayout::PresentSrc:
            stageFlags = PipelineStage::BottomOfPipeBit;
            accessMask = Access::None;
            break;
        default:
            PE_ERROR("Unsupported image layout");
        }
    }

    ImageLayout GetLayoutFromVK(VkImageLayout layout)
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return ImageLayout::Undefined;
        case VK_IMAGE_LAYOUT_GENERAL:
            return ImageLayout::General;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return ImageLayout::ColorAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return ImageLayout::DepthStencilAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return ImageLayout::DepthStencilReadOnly;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return ImageLayout::ShaderReadOnly;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return ImageLayout::TransferSrc;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return ImageLayout::TransferDst;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return ImageLayout::Preinitialized;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            return ImageLayout::DepthReadOnlyStencilAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            return ImageLayout::DepthAttachmentStencilReadOnly;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            return ImageLayout::DepthAttachment;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            return ImageLayout::DepthReadOnly;
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
            return ImageLayout::StencilAttachment;
        case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
            return ImageLayout::StencilReadOnly;
        case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
            return ImageLayout::ReadOnly;
        case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
            return ImageLayout::Attachment;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return ImageLayout::PresentSrc;
        case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
            return ImageLayout::SharedPresent;
        case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
            return ImageLayout::FragmentDensityMap;
        case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
            return ImageLayout::FragmentShadingRateAttachment;
        }

        PE_ERROR("Unknown image layout");
        return ImageLayout::Undefined;
    }

    VkImageLayout GetImageLayoutVK(ImageLayout layout)
    {
        switch (layout)
        {
        case ImageLayout::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case ImageLayout::General:
            return VK_IMAGE_LAYOUT_GENERAL;
        case ImageLayout::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::ShaderReadOnly:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ImageLayout::TransferSrc:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ImageLayout::TransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ImageLayout::Preinitialized:
            return VK_IMAGE_LAYOUT_PREINITIALIZED;
        case ImageLayout::DepthReadOnlyStencilAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthAttachmentStencilReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::DepthAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case ImageLayout::StencilAttachment:
            return VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::StencilReadOnly:
            return VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::ReadOnly:
            return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        case ImageLayout::Attachment:
            return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        case ImageLayout::PresentSrc:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case ImageLayout::SharedPresent:
            return VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR;
        case ImageLayout::FragmentDensityMap:
            return VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
        case ImageLayout::FragmentShadingRateAttachment:
            return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
        }

        PE_ERROR("Unknown image layout");
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    VkShaderStageFlags GetShaderStageVK(ShaderStageFlags flags)
    {
        if (!flags)
            return 0;

        VkShaderStageFlags vkFlags = 0;

        if (flags & ShaderStage::VertexBit)
            vkFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (flags & ShaderStage::TesslationControlBit)
            vkFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        if (flags & ShaderStage::TesslationEvaluationBit)
            vkFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        if (flags & ShaderStage::GeometryBit)
            vkFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (flags & ShaderStage::FragmentBit)
            vkFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (flags & ShaderStage::ComputeBit)
            vkFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        if (flags & ShaderStage::AllGraphics)
            vkFlags |= VK_SHADER_STAGE_ALL_GRAPHICS;
        if (flags & ShaderStage::All)
            vkFlags |= VK_SHADER_STAGE_ALL;
        if (flags & ShaderStage::RaygenBit)
            vkFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
        if (flags & ShaderStage::AnyHitBit)
            vkFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_NV;
        if (flags & ShaderStage::ClosestHitBit)
            vkFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
        if (flags & ShaderStage::MissBit)
            vkFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
        if (flags & ShaderStage::IntersectionBit)
            vkFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_NV;
        if (flags & ShaderStage::CallableBit)
            vkFlags |= VK_SHADER_STAGE_CALLABLE_BIT_NV;

        return vkFlags;
    }

    VkFilter GetFilterVK(Filter filter)
    {
        switch (filter)
        {
        case Filter::Nearest:
            return VK_FILTER_NEAREST;
        case Filter::Linear:
            return VK_FILTER_LINEAR;
        case Filter::Cubic:
            return VK_FILTER_CUBIC_IMG;
        }

        PE_ERROR("Unknown filter");
        return VK_FILTER_NEAREST;
    }

    VkImageAspectFlags GetImageAspectVK(ImageAspectFlags flags)
    {
        if (!flags)
            return 0;

        VkImageAspectFlags vkFlags = 0;

        if (flags & ImageAspect::ColorBit)
            vkFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
        if (flags & ImageAspect::DepthBit)
            vkFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (flags & ImageAspect::StencilBit)
            vkFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;

        return vkFlags;
    }

    VkSamplerMipmapMode GetSamplerMipmapModeVK(SamplerMipmapMode mode)
    {
        switch (mode)
        {
        case SamplerMipmapMode::Nearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case SamplerMipmapMode::Linear:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        PE_ERROR("Unknown mipmap mode");
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    VkSamplerAddressMode GetSamplerAddressMode(SamplerAddressMode mode)
    {
        switch (mode)
        {
        case SamplerAddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case SamplerAddressMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case SamplerAddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SamplerAddressMode::ClampToBorder:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case SamplerAddressMode::MirrorClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        }

        PE_ERROR("Unknown address mode");
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkCompareOp GetCompareOpVK(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
        }

        PE_ERROR("Unknown compare op");
        return VK_COMPARE_OP_NEVER;
    }

    VkBorderColor GetBorderColorVK(BorderColor color)
    {
        switch (color)
        {
        case BorderColor::FloatTransparentBlack:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case BorderColor::IntTransparentBlack:
            return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        case BorderColor::FloatOpaqueBlack:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case BorderColor::IntOpaqueBlack:
            return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        case BorderColor::FloatOpaqueWhite:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        case BorderColor::IntOpaqueWhite:
            return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        }

        PE_ERROR("Unknown border color");
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }

    VkImageType GetImageTypeVK(ImageType type)
    {
        switch (type)
        {
        case ImageType::Type1D:
            return VK_IMAGE_TYPE_1D;
        case ImageType::Type2D:
            return VK_IMAGE_TYPE_2D;
        case ImageType::Type3D:
            return VK_IMAGE_TYPE_3D;
        }

        PE_ERROR("Unknown image type");
        return VK_IMAGE_TYPE_1D;
    }

    VkImageViewType GetImageViewTypeVK(ImageViewType type)
    {
        switch (type)
        {
        case ImageViewType::Type1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case ImageViewType::Type2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case ImageViewType::Type3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case ImageViewType::TypeCube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case ImageViewType::Type1DArray:
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case ImageViewType::Type2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case ImageViewType::TypeCubeArray:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        }

        PE_ERROR("Unknown image view type");
        return VK_IMAGE_VIEW_TYPE_1D;
    }

    VkSampleCountFlagBits GetSampleCountVK(SampleCount count)
    {
        switch (count)
        {
        case SampleCount::Count1:
            return VK_SAMPLE_COUNT_1_BIT;
        case SampleCount::Count2:
            return VK_SAMPLE_COUNT_2_BIT;
        case SampleCount::Count4:
            return VK_SAMPLE_COUNT_4_BIT;
        case SampleCount::Count8:
            return VK_SAMPLE_COUNT_8_BIT;
        case SampleCount::Count16:
            return VK_SAMPLE_COUNT_16_BIT;
        case SampleCount::Count32:
            return VK_SAMPLE_COUNT_32_BIT;
        case SampleCount::Count64:
            return VK_SAMPLE_COUNT_64_BIT;
        }

        PE_ERROR("Unknown sample count");
        return VK_SAMPLE_COUNT_1_BIT;
    }

    VkFormat GetFormatVK(Format format)
    {
        switch (format)
        {
        case Format::Undefined:
            return VK_FORMAT_UNDEFINED;
        case Format::R8SInt:
            return VK_FORMAT_R8_SINT;
        case Format::R8UInt:
            return VK_FORMAT_R8_UINT;
        case Format::R8Unorm:
            return VK_FORMAT_R8_UNORM;
        case Format::RGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::R16SInt:
            return VK_FORMAT_R16_SINT;
        case Format::R16UInt:
            return VK_FORMAT_R16_UINT;
        case Format::R16SFloat:
            return VK_FORMAT_R16_SFLOAT;
        case Format::R16Unorm:
            return VK_FORMAT_R16_UNORM;
        case Format::RG16SFloat:
            return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGB16SInt:
            return VK_FORMAT_R16G16B16_SINT;
        case Format::RGB16UInt:
            return VK_FORMAT_R16G16B16_UINT;
        case Format::RGB16SFloat:
            return VK_FORMAT_R16G16B16_SFLOAT;
        case Format::RGBA16SFloat:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32SInt:
            return VK_FORMAT_R32_SINT;
        case Format::R32UInt:
            return VK_FORMAT_R32_UINT;
        case Format::R32SFloat:
            return VK_FORMAT_R32_SFLOAT;
        case Format::RG32SInt:
            return VK_FORMAT_R32G32_SINT;
        case Format::RG32UInt:
            return VK_FORMAT_R32G32_UINT;
        case Format::RG32SFloat:
            return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32SInt:
            return VK_FORMAT_R32G32B32_SINT;
        case Format::RGB32UInt:
            return VK_FORMAT_R32G32B32_UINT;
        case Format::RGB32SFloat:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32SInt:
            return VK_FORMAT_R32G32B32A32_SINT;
        case Format::RGBA32UInt:
            return VK_FORMAT_R32G32B32A32_UINT;
        case Format::RGBA32SFloat:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::BGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::D24UnormS8UInt:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32SFloat:
            return VK_FORMAT_D32_SFLOAT;
        case Format::D32SFloatS8UInt:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        }

        PE_ERROR("Unknown format");
        return VK_FORMAT_UNDEFINED;
    }

    Format GetFormatFromVK(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_UNDEFINED:
            return Format::Undefined;
        case VK_FORMAT_R8_SINT:
            return Format::R8SInt;
        case VK_FORMAT_R8_UINT:
            return Format::R8UInt;
        case VK_FORMAT_R8_UNORM:
            return Format::R8Unorm;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return Format::RGBA8Unorm;
        case VK_FORMAT_R16_SINT:
            return Format::R16SInt;
        case VK_FORMAT_R16_UINT:
            return Format::R16UInt;
        case VK_FORMAT_R16_SFLOAT:
            return Format::R16SFloat;
        case VK_FORMAT_R16_UNORM:
            return Format::R16Unorm;
        case VK_FORMAT_R16G16_SFLOAT:
            return Format::RG16SFloat;
        case VK_FORMAT_R16G16B16_SINT:
            return Format::RGB16SInt;
        case VK_FORMAT_R16G16B16_UINT:
            return Format::RGB16UInt;
        case VK_FORMAT_R16G16B16_SFLOAT:
            return Format::RGB16SFloat;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return Format::RGBA16SFloat;
        case VK_FORMAT_R32_SINT:
            return Format::R32SInt;
        case VK_FORMAT_R32_UINT:
            return Format::R32UInt;
        case VK_FORMAT_R32_SFLOAT:
            return Format::R32SFloat;
        case VK_FORMAT_R32G32_SINT:
            return Format::RG32SInt;
        case VK_FORMAT_R32G32_UINT:
            return Format::RG32UInt;
        case VK_FORMAT_R32G32_SFLOAT:
            return Format::RG32SFloat;
        case VK_FORMAT_R32G32B32_SINT:
            return Format::RGB32SInt;
        case VK_FORMAT_R32G32B32_UINT:
            return Format::RGB32UInt;
        case VK_FORMAT_R32G32B32_SFLOAT:
            return Format::RGB32SFloat;
        case VK_FORMAT_R32G32B32A32_SINT:
            return Format::RGBA32SInt;
        case VK_FORMAT_R32G32B32A32_UINT:
            return Format::RGBA32UInt;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return Format::RGBA32SFloat;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return Format::BGRA8Unorm;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return Format::D24UnormS8UInt;
        case VK_FORMAT_D32_SFLOAT:
            return Format::D32SFloat;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return Format::D32SFloatS8UInt;
        }

        PE_ERROR("Unknown format");
        return Format::Undefined;
    }

    VkImageTiling GetImageTilingVK(ImageTiling tiling)
    {
        switch (tiling)
        {
        case ImageTiling::Optimal:
            return VK_IMAGE_TILING_OPTIMAL;
        case ImageTiling::Linear:
            return VK_IMAGE_TILING_LINEAR;
        }

        PE_ERROR("Unknown tiling");
        return VK_IMAGE_TILING_OPTIMAL;
    }

    VkImageUsageFlags GetImageUsageVK(ImageUsageFlags flags)
    {
        if (!flags)
            return 0;

        VkImageUsageFlags vkFlags = 0;

        if (flags & ImageUsage::TransferSrcBit)
            vkFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (flags & ImageUsage::TransferDstBit)
            vkFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (flags & ImageUsage::SampledBit)
            vkFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (flags & ImageUsage::StorageBit)
            vkFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (flags & ImageUsage::ColorAttachmentBit)
            vkFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (flags & ImageUsage::DepthStencilAttachmentBit)
            vkFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (flags & ImageUsage::TransientAttachmentBit)
            vkFlags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        if (flags & ImageUsage::InputAttachmentBit)
            vkFlags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

        return vkFlags;
    }

    VkSharingMode GetSharingModeVK(SharingMode mode)
    {
        switch (mode)
        {
        case SharingMode::Exclusive:
            return VK_SHARING_MODE_EXCLUSIVE;
        case SharingMode::Concurrent:
            return VK_SHARING_MODE_CONCURRENT;
        }

        PE_ERROR("Unknown sharing mode");
        return VK_SHARING_MODE_EXCLUSIVE;
    }

    VkImageCreateFlags GetImageCreateFlagsVK(ImageCreateFlags flags)
    {
        VkImageCreateFlags vkFlags = 0;

        if (!flags)
            return vkFlags;

        if (flags & ImageCreate::SparceBindingBit)
            vkFlags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
        if (flags & ImageCreate::SparceResidencyBit)
            vkFlags |= VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
        if (flags & ImageCreate::SparceAliasedBit)
            vkFlags |= VK_IMAGE_CREATE_SPARSE_ALIASED_BIT;
        if (flags & ImageCreate::MutableFormatBit)
            vkFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (flags & ImageCreate::CubeCompatibleBit)
            vkFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        if (flags & ImageCreate::AliasBit)
            vkFlags |= VK_IMAGE_CREATE_ALIAS_BIT;
        if (flags & ImageCreate::SplitInstanceBindRegionsBit)
            vkFlags |= VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT;
        if (flags & ImageCreate::Array2DCompatibleBit)
            vkFlags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        if (flags & ImageCreate::BlockTexeViewCompatibleBit)
            vkFlags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
        if (flags & ImageCreate::ExtendedUsageBit)
            vkFlags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        if (flags & ImageCreate::ProtectedBit)
            vkFlags |= VK_IMAGE_CREATE_PROTECTED_BIT;
        if (flags & ImageCreate::DisjointBit)
            vkFlags |= VK_IMAGE_CREATE_DISJOINT_BIT;
        if (flags & ImageCreate::CornerSampledBit)
            vkFlags |= VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV;
        if (flags & ImageCreate::SampleLocationsCompatibleDepthBit)
            vkFlags |= VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;
        if (flags & ImageCreate::SubsampledBit)
            vkFlags |= VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT;
        if (flags & ImageCreate::View2DCompatibleBit)
            vkFlags |= VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT;

        return vkFlags;
    }

    VkCommandBufferUsageFlags GetCommandBufferUsageFlagsVK(CommandBufferUsageFlags flags)
    {
        if (!flags)
            return 0;

        VkCommandBufferUsageFlags vkFlags = 0;

        if (flags & CommandBufferUsage::OneTimeSubmitBit)
            vkFlags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (flags & CommandBufferUsage::RenderPassContinueBit)
            vkFlags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        if (flags & CommandBufferUsage::SimultaneousUseBit)
            vkFlags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        return vkFlags;
    }

    VkBufferUsageFlags GetBufferUsageFlagsVK(BufferUsageFlags flags)
    {
        if (!flags)
            return 0;

        VkBufferUsageFlags vkFlags = 0;

        if (flags & BufferUsage::TransferSrcBit)
            vkFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (flags & BufferUsage::TransferDstBit)
            vkFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (flags & BufferUsage::UniformTexelBufferBit)
            vkFlags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        if (flags & BufferUsage::StorageTexelBufferBit)
            vkFlags |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
        if (flags & BufferUsage::UniformBufferBit)
            vkFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (flags & BufferUsage::StorageBufferBit)
            vkFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (flags & BufferUsage::IndexBufferBit)
            vkFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (flags & BufferUsage::VertexBufferBit)
            vkFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (flags & BufferUsage::IndirectBufferBit)
            vkFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (flags & BufferUsage::ShaderDeviceAddressBit)
            vkFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (flags & BufferUsage::TransformFeedbackBufferBit)
            vkFlags |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
        if (flags & BufferUsage::TransformFeedbackCounterBufferBit)
            vkFlags |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
        if (flags & BufferUsage::ConditionalRenderingBit)
            vkFlags |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
        if (flags & BufferUsage::AccelerationStructureBuildInputReadBit)
            vkFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        if (flags & BufferUsage::AccelerationStructureStorageBit)
            vkFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        if (flags & BufferUsage::ShaderBindingTableBit)
            vkFlags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

        return vkFlags;
    }

    VkOffset3D GetOffset3DVK(const Offset3D &offset)
    {
        VkOffset3D vkOffset;
        vkOffset.x = offset.x;
        vkOffset.y = offset.y;
        vkOffset.z = offset.z;
        return vkOffset;
    }

    VkColorSpaceKHR GetColorSpaceVK(ColorSpace colorSpace)
    {
        switch (colorSpace)
        {
        case ColorSpace::SrgbNonLinear:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case ColorSpace::DisplayP3NonLinear:
            return VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
        case ColorSpace::ExtendedSrgbLinear:
            return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        case ColorSpace::DisplayP3Linear:
            return VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT;
        case ColorSpace::DciP3NonLinear:
            return VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT;
        case ColorSpace::BT709Linear:
            return VK_COLOR_SPACE_BT709_LINEAR_EXT;
        case ColorSpace::BT709NonLinear:
            return VK_COLOR_SPACE_BT709_NONLINEAR_EXT;
        case ColorSpace::BT2020Linear:
            return VK_COLOR_SPACE_BT2020_LINEAR_EXT;
        case ColorSpace::HDR10St2084:
            return VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case ColorSpace::DolbyVision:
            return VK_COLOR_SPACE_DOLBYVISION_EXT;
        case ColorSpace::HDR10Hlg:
            return VK_COLOR_SPACE_HDR10_HLG_EXT;
        case ColorSpace::AdobeRgbLinear:
            return VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT;
        case ColorSpace::AdobeRgbNonLinear:
            return VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT;
        case ColorSpace::PassThrough:
            return VK_COLOR_SPACE_PASS_THROUGH_EXT;
        case ColorSpace::ExtendedSrgbNonLinear:
            return VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT;
        }

        PE_ERROR("Unknown color space");
        return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }

    ColorSpace GetColorSpaceFromVK(VkColorSpaceKHR colorSpace)
    {
        switch (colorSpace)
        {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            return ColorSpace::SrgbNonLinear;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
            return ColorSpace::DisplayP3NonLinear;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            return ColorSpace::ExtendedSrgbLinear;
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
            return ColorSpace::DisplayP3Linear;
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:
            return ColorSpace::DciP3NonLinear;
        case VK_COLOR_SPACE_BT709_LINEAR_EXT:
            return ColorSpace::BT709Linear;
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
            return ColorSpace::BT709NonLinear;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
            return ColorSpace::BT2020Linear;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
            return ColorSpace::HDR10St2084;
        case VK_COLOR_SPACE_DOLBYVISION_EXT:
            return ColorSpace::DolbyVision;
        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            return ColorSpace::HDR10Hlg;
        case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT:
            return ColorSpace::AdobeRgbLinear;
        case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:
            return ColorSpace::AdobeRgbNonLinear;
        case VK_COLOR_SPACE_PASS_THROUGH_EXT:
            return ColorSpace::PassThrough;
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
            return ColorSpace::ExtendedSrgbNonLinear;
        }

        PE_ERROR("Unknown color space");
        return ColorSpace::SrgbNonLinear;
    }

    VkPresentModeKHR GetPresentModeVK(PresentMode presentMode)
    {
        switch (presentMode)
        {
        case PresentMode::Immediate:
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        case PresentMode::Mailbox:
            return VK_PRESENT_MODE_MAILBOX_KHR;
        case PresentMode::Fifo:
            return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::FifoRelaxed:
            return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        case PresentMode::SharedDemandRefresh:
            return VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
        case PresentMode::SharedContinuousRefresh:
            return VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
        }

        PE_ERROR("Unknown present mode");
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    PresentMode GetPresentModeFromVK(VkPresentModeKHR presentMode)
    {
        switch (presentMode)
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return PresentMode::Immediate;
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return PresentMode::Mailbox;
        case VK_PRESENT_MODE_FIFO_KHR:
            return PresentMode::Fifo;
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return PresentMode::FifoRelaxed;
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
            return PresentMode::SharedDemandRefresh;
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
            return PresentMode::SharedContinuousRefresh;
        }

        PE_ERROR("Unknown present mode");
        return PresentMode::Fifo;
    }

    VkPipelineStageFlags GetPipelineStageFlagsVK(PipelineStageFlags flags)
    {
        if (!flags)
            return 0;

        VkPipelineStageFlags vkFlags = 0;

        if (flags & PipelineStage::TopOfPipeBit)
            vkFlags |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (flags & PipelineStage::DrawIndirectBit)
            vkFlags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        if (flags & PipelineStage::VertexInputBit)
            vkFlags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        if (flags & PipelineStage::VertexShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        if (flags & PipelineStage::TessellationControlShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
        if (flags & PipelineStage::TessellationEvaluationShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        if (flags & PipelineStage::GeometryShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        if (flags & PipelineStage::FragmentShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (flags & PipelineStage::EarlyFragmentTestsBit)
            vkFlags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        if (flags & PipelineStage::LateFragmentTestsBit)
            vkFlags |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        if (flags & PipelineStage::ColorAttachmentOutputBit)
            vkFlags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (flags & PipelineStage::ComputeShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (flags & PipelineStage::TransferBit)
            vkFlags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (flags & PipelineStage::BottomOfPipeBit)
            vkFlags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        if (flags & PipelineStage::HostBit)
            vkFlags |= VK_PIPELINE_STAGE_HOST_BIT;
        if (flags & PipelineStage::AllGraphicsBit)
            vkFlags |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        if (flags & PipelineStage::AllCommandsBit)
            vkFlags |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (flags & PipelineStage::TransformFeedbackBit)
            vkFlags |= VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
        if (flags & PipelineStage::ConditionalRenderingBit)
            vkFlags |= VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT;
        if (flags & PipelineStage::AccelerationStructureBuildBit)
            vkFlags |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        if (flags & PipelineStage::RayTracingShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        if (flags & PipelineStage::TaskShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV;
        if (flags & PipelineStage::MeshShaderBit)
            vkFlags |= VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
        if (flags & PipelineStage::FragmentDensityProcessBit)
            vkFlags |= VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT;
        if (flags & PipelineStage::FragmentShadingRateAttechmentBit)
            vkFlags |= VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

        return vkFlags;
    }

    bool IsDepthFormatVK(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D32_SFLOAT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    bool IsDepthFormat(Format format)
    {
        return format == Format::D32SFloatS8UInt ||
               format == Format::D32SFloat ||
               format == Format::D24UnormS8UInt;
    }

    bool HasStencilVK(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    bool HasStencil(Format format)
    {
        return format == Format::D32SFloatS8UInt ||
               format == Format::D24UnormS8UInt;
    }

    VkAttachmentLoadOp GetAttachmentLoadOpVK(AttachmentLoadOp loadOp)
    {
        switch (loadOp)
        {
        case AttachmentLoadOp::Load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case AttachmentLoadOp::Clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case AttachmentLoadOp::DontCare:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        PE_ERROR("Unknown attachment load op");
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    }

    VkAttachmentStoreOp GetAttachmentStoreOpVK(AttachmentStoreOp storeOp)
    {
        switch (storeOp)
        {
        case AttachmentStoreOp::Store:
            return VK_ATTACHMENT_STORE_OP_STORE;
        case AttachmentStoreOp::DontCare:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

        PE_ERROR("Unknown attachment store op");
        return VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkAccessFlags GetAccessFlagsVK(AccessFlags flags)
    {
        if (!flags)
            return 0;

        VkAccessFlags vkFlags = 0;

        if (flags & Access::IndirectCommandReadBit)
            vkFlags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        if (flags & Access::IndexReadBit)
            vkFlags |= VK_ACCESS_INDEX_READ_BIT;
        if (flags & Access::VertexAttributeReadBit)
            vkFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        if (flags & Access::UniformReadBit)
            vkFlags |= VK_ACCESS_UNIFORM_READ_BIT;
        if (flags & Access::InputAttachmentReadBit)
            vkFlags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        if (flags & Access::ShaderReadBit)
            vkFlags |= VK_ACCESS_SHADER_READ_BIT;
        if (flags & Access::ShaderWriteBit)
            vkFlags |= VK_ACCESS_SHADER_WRITE_BIT;
        if (flags & Access::ColorAttachmentReadBit)
            vkFlags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        if (flags & Access::ColorAttachmentWriteBit)
            vkFlags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        if (flags & Access::DepthStencilAttachmentReadBit)
            vkFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        if (flags & Access::DepthStencilAttachmentWriteBit)
            vkFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        if (flags & Access::TransferReadBit)
            vkFlags |= VK_ACCESS_TRANSFER_READ_BIT;
        if (flags & Access::TransferWriteBit)
            vkFlags |= VK_ACCESS_TRANSFER_WRITE_BIT;
        if (flags & Access::HostReadBit)
            vkFlags |= VK_ACCESS_HOST_READ_BIT;
        if (flags & Access::HostWriteBit)
            vkFlags |= VK_ACCESS_HOST_WRITE_BIT;
        if (flags & Access::MemoryReadBit)
            vkFlags |= VK_ACCESS_MEMORY_READ_BIT;
        if (flags & Access::MemoryWriteBit)
            vkFlags |= VK_ACCESS_MEMORY_WRITE_BIT;
        if (flags & Access::TransformFeedbackWriteBit)
            vkFlags |= VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
        if (flags & Access::TransformFeedbackCounterReadBit)
            vkFlags |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;
        if (flags & Access::TransformFeedbackCounterWriteBit)
            vkFlags |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
        if (flags & Access::ConditionalRenderingReadBit)
            vkFlags |= VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
        if (flags & Access::ColorAttachmentReadNoncoherentBit)
            vkFlags |= VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT;
        if (flags & Access::AccelerationStructureReadBitKhr)
            vkFlags |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        if (flags & Access::AccelerationStructureWriteBitKhr)
            vkFlags |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        if (flags & Access::FragmentDensityMapReadBit)
            vkFlags |= VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT;
        if (flags & Access::FragmentShadingRateAttachmentReadBitKhr)
            vkFlags |= VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;

        return vkFlags;
    }

    VmaAllocationCreateFlags GetAllocationCreateFlagsVMA(AllocationCreateFlags flags)
    {
        if (!flags)
            return 0;

        VmaAllocationCreateFlags vmaFlags = 0;

        if (flags & AllocationCreate::DedicatedMemoryBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        if (flags & AllocationCreate::NeverAllocateBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
        if (flags & AllocationCreate::MappedBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (flags & AllocationCreate::UserDataCopyStringBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
        if (flags & AllocationCreate::UpperAddressBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
        if (flags & AllocationCreate::DontBindBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
        if (flags & AllocationCreate::WithinBudgetBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
        if (flags & AllocationCreate::CanAliasBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        if (flags & AllocationCreate::HostAccessSequentialWriteBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        if (flags & AllocationCreate::HostAccessRandomBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        if (flags & AllocationCreate::HostAccessAllowTransferInsteadBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
        if (flags & AllocationCreate::StrategyMinMemoryBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
        if (flags & AllocationCreate::StrategyMinTimeBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
        if (flags & AllocationCreate::StrategyMinOffsetBit)
            vmaFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT;

        return vmaFlags;
    }

    VkBlendFactor GetBlendFactorVK(BlendFactor blendFactor)
    {
        switch (blendFactor)
        {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case BlendFactor::Src1Color:
            return VK_BLEND_FACTOR_SRC1_COLOR;
        case BlendFactor::OneMinusSrc1Color:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case BlendFactor::Src1Alpha:
            return VK_BLEND_FACTOR_SRC1_ALPHA;
        case BlendFactor::OneMinusSrc1Alpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        }

        PE_ERROR("Unknown blend factor");
        return VK_BLEND_FACTOR_ZERO;
    }

    VkBlendOp GetBlendOpVK(BlendOp blendOp)
    {
        switch (blendOp)
        {
        case BlendOp::Add:
            return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return VK_BLEND_OP_MIN;
        case BlendOp::Max:
            return VK_BLEND_OP_MAX;
        case BlendOp::Zero:
            return VK_BLEND_OP_ZERO_EXT;
        case BlendOp::Src:
            return VK_BLEND_OP_SRC_EXT;
        case BlendOp::Dst:
            return VK_BLEND_OP_DST_EXT;
        case BlendOp::SrcOver:
            return VK_BLEND_OP_SRC_OVER_EXT;
        case BlendOp::DstOver:
            return VK_BLEND_OP_DST_OVER_EXT;
        case BlendOp::SrcIn:
            return VK_BLEND_OP_SRC_IN_EXT;
        case BlendOp::DstIn:
            return VK_BLEND_OP_DST_IN_EXT;
        case BlendOp::SrcOut:
            return VK_BLEND_OP_SRC_OUT_EXT;
        case BlendOp::DstOut:
            return VK_BLEND_OP_DST_OUT_EXT;
        case BlendOp::SrcAtop:
            return VK_BLEND_OP_SRC_ATOP_EXT;
        case BlendOp::DstAtop:
            return VK_BLEND_OP_DST_ATOP_EXT;
        case BlendOp::Xor:
            return VK_BLEND_OP_XOR_EXT;
        case BlendOp::Multiply:
            return VK_BLEND_OP_MULTIPLY_EXT;
        case BlendOp::Screen:
            return VK_BLEND_OP_SCREEN_EXT;
        case BlendOp::Overlay:
            return VK_BLEND_OP_OVERLAY_EXT;
        case BlendOp::Darken:
            return VK_BLEND_OP_DARKEN_EXT;
        case BlendOp::Lighten:
            return VK_BLEND_OP_LIGHTEN_EXT;
        case BlendOp::Colordodge:
            return VK_BLEND_OP_COLORDODGE_EXT;
        case BlendOp::Colorburn:
            return VK_BLEND_OP_COLORBURN_EXT;
        case BlendOp::Hardlight:
            return VK_BLEND_OP_HARDLIGHT_EXT;
        case BlendOp::Softlight:
            return VK_BLEND_OP_SOFTLIGHT_EXT;
        case BlendOp::Difference:
            return VK_BLEND_OP_DIFFERENCE_EXT;
        case BlendOp::Exclusion:
            return VK_BLEND_OP_EXCLUSION_EXT;
        case BlendOp::Invert:
            return VK_BLEND_OP_INVERT_EXT;
        case BlendOp::InvertRgb:
            return VK_BLEND_OP_INVERT_RGB_EXT;
        case BlendOp::Lineardodge:
            return VK_BLEND_OP_LINEARDODGE_EXT;
        case BlendOp::Linearburn:
            return VK_BLEND_OP_LINEARBURN_EXT;
        case BlendOp::Vividlight:
            return VK_BLEND_OP_VIVIDLIGHT_EXT;
        case BlendOp::Linearlight:
            return VK_BLEND_OP_LINEARLIGHT_EXT;
        case BlendOp::Pinlight:
            return VK_BLEND_OP_PINLIGHT_EXT;
        case BlendOp::Hardmix:
            return VK_BLEND_OP_HARDMIX_EXT;
        case BlendOp::HslHue:
            return VK_BLEND_OP_HSL_HUE_EXT;
        case BlendOp::HslSaturation:
            return VK_BLEND_OP_HSL_SATURATION_EXT;
        case BlendOp::HslColor:
            return VK_BLEND_OP_HSL_COLOR_EXT;
        case BlendOp::HslLuminosity:
            return VK_BLEND_OP_HSL_LUMINOSITY_EXT;
        case BlendOp::Plus:
            return VK_BLEND_OP_PLUS_EXT;
        case BlendOp::PlusClamped:
            return VK_BLEND_OP_PLUS_CLAMPED_EXT;
        case BlendOp::PlusClampedAlpha:
            return VK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT;
        case BlendOp::PlusDarker:
            return VK_BLEND_OP_PLUS_DARKER_EXT;
        case BlendOp::Minus:
            return VK_BLEND_OP_MINUS_EXT;
        case BlendOp::MinusClamped:
            return VK_BLEND_OP_MINUS_CLAMPED_EXT;
        case BlendOp::Contrast:
            return VK_BLEND_OP_CONTRAST_EXT;
        case BlendOp::InvertOvg:
            return VK_BLEND_OP_INVERT_OVG_EXT;
        case BlendOp::Red:
            return VK_BLEND_OP_RED_EXT;
        case BlendOp::Green:
            return VK_BLEND_OP_GREEN_EXT;
        case BlendOp::Blue:
            return VK_BLEND_OP_BLUE_EXT;
        }

        PE_ERROR("Unknown blend op");
        return VK_BLEND_OP_ADD;
    }

    VkColorComponentFlags GetColorComponentFlagsVK(ColorComponentFlags flags)
    {
        if (!flags)
            return 0;

        VkColorComponentFlags vkFlags = 0;

        if (flags & ColorComponent::RBit)
            vkFlags |= VK_COLOR_COMPONENT_R_BIT;
        if (flags & ColorComponent::GBit)
            vkFlags |= VK_COLOR_COMPONENT_G_BIT;
        if (flags & ColorComponent::BBit)
            vkFlags |= VK_COLOR_COMPONENT_B_BIT;
        if (flags & ColorComponent::ABit)
            vkFlags |= VK_COLOR_COMPONENT_A_BIT;

        return vkFlags;
    }

    VkDynamicState GetDynamicStateVK(DynamicState state)
    {
        switch (state)
        {
        case DynamicState::Viewport:
            return VK_DYNAMIC_STATE_VIEWPORT;
        case DynamicState::Scissor:
            return VK_DYNAMIC_STATE_SCISSOR;
        case DynamicState::LineWidth:
            return VK_DYNAMIC_STATE_LINE_WIDTH;
        case DynamicState::DepthBias:
            return VK_DYNAMIC_STATE_DEPTH_BIAS;
        case DynamicState::BlendConstants:
            return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
        case DynamicState::DepthBounds:
            return VK_DYNAMIC_STATE_DEPTH_BOUNDS;
        case DynamicState::StencilCompareMask:
            return VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        case DynamicState::StencilWriteMask:
            return VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        case DynamicState::StencilReference:
            return VK_DYNAMIC_STATE_STENCIL_REFERENCE;
        case DynamicState::CullMode:
            return VK_DYNAMIC_STATE_CULL_MODE;
        case DynamicState::FrontFace:
            return VK_DYNAMIC_STATE_FRONT_FACE;
        case DynamicState::PrimitiveTopology:
            return VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
        case DynamicState::ViewportWithCount:
            return VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT;
        case DynamicState::ScissorWithCount:
            return VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT;
        case DynamicState::VertexInputBindingStride:
            return VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT;
        case DynamicState::DepthTestEnable:
            return VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT;
        case DynamicState::DepthWriteEnable:
            return VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT;
        case DynamicState::DepthCompareOp:
            return VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT;
        case DynamicState::DepthBoundsTestEnable:
            return VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT;
        case DynamicState::StencilTestEnable:
            return VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT;
        case DynamicState::StencilOp:
            return VK_DYNAMIC_STATE_STENCIL_OP_EXT;
        case DynamicState::RasterizerDiscardEnable:
            return VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT;
        case DynamicState::DepthBiasEnable:
            return VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE_EXT;
        case DynamicState::PrimitiveRestartEnable:
            return VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
        case DynamicState::DiscardRectangle:
            return VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT;
        case DynamicState::SampleLocations:
            return VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT;
        case DynamicState::RayTracingPipelineStackSize:
            return VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR;
        case DynamicState::FragmentShadingRateKHR:
            return VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR;
        case DynamicState::LineStipple:
            return VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;
        case DynamicState::VertexInput:
            return VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
        case DynamicState::PatchControlPoints:
            return VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT;
        case DynamicState::LogicOp:
            return VK_DYNAMIC_STATE_LOGIC_OP_EXT;
        case DynamicState::ColorWriteEnable:
            return VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;
        }

        PE_ERROR("Unknown dynamic state");
        return VK_DYNAMIC_STATE_VIEWPORT;
    }

    VkDescriptorType GetDescriptorTypeVK(DescriptorType type)
    {
        switch (type)
        {
        case DescriptorType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::StorageImage:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformTexelBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case DescriptorType::StorageTexelBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case DescriptorType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::UniformBufferDynamic:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::StorageBufferDynamic:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DescriptorType::InputAttachment:
            return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case DescriptorType::InlineUniformBlock:
            return VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
        case DescriptorType::AccelerationStructure:
            return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        }

        PE_ERROR("Unknown descriptor type");
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    }

    VkCullModeFlags GetCullModeVK(CullMode mode)
    {
        switch (mode)
        {
        case CullMode::None:
            return VK_CULL_MODE_NONE;
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack:
            return VK_CULL_MODE_FRONT_AND_BACK;
        }

        PE_ERROR("Unknown cull mode");
        return VK_CULL_MODE_NONE;
    }

    VkObjectType GetObjectTypeVK(ObjectType type)
    {
        switch (type)
        {
        case ObjectType::Unknown:
            return VK_OBJECT_TYPE_UNKNOWN;
        case ObjectType::Instance:
            return VK_OBJECT_TYPE_INSTANCE;
        case ObjectType::PhysicalDevice:
            return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
        case ObjectType::Device:
            return VK_OBJECT_TYPE_DEVICE;
        case ObjectType::Queue:
            return VK_OBJECT_TYPE_QUEUE;
        case ObjectType::Semaphore:
            return VK_OBJECT_TYPE_SEMAPHORE;
        case ObjectType::CommandBuffer:
            return VK_OBJECT_TYPE_COMMAND_BUFFER;
        case ObjectType::Fence:
            return VK_OBJECT_TYPE_FENCE;
        case ObjectType::DeviceMemory:
            return VK_OBJECT_TYPE_DEVICE_MEMORY;
        case ObjectType::Buffer:
            return VK_OBJECT_TYPE_BUFFER;
        case ObjectType::Image:
            return VK_OBJECT_TYPE_IMAGE;
        case ObjectType::Event:
            return VK_OBJECT_TYPE_EVENT;
        case ObjectType::QueryPool:
            return VK_OBJECT_TYPE_QUERY_POOL;
        case ObjectType::BufferView:
            return VK_OBJECT_TYPE_BUFFER_VIEW;
        case ObjectType::ImageView:
            return VK_OBJECT_TYPE_IMAGE_VIEW;
        case ObjectType::ShaderModule:
            return VK_OBJECT_TYPE_SHADER_MODULE;
        case ObjectType::PipelineCache:
            return VK_OBJECT_TYPE_PIPELINE_CACHE;
        case ObjectType::PipelineLayout:
            return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        case ObjectType::RenderPass:
            return VK_OBJECT_TYPE_RENDER_PASS;
        case ObjectType::Pipeline:
            return VK_OBJECT_TYPE_PIPELINE;
        case ObjectType::DescriptorSetLayout:
            return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
        case ObjectType::Sampler:
            return VK_OBJECT_TYPE_SAMPLER;
        case ObjectType::DescriptorPool:
            return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
        case ObjectType::DescriptorSet:
            return VK_OBJECT_TYPE_DESCRIPTOR_SET;
        case ObjectType::Framebuffer:
            return VK_OBJECT_TYPE_FRAMEBUFFER;
        case ObjectType::CommandPool:
            return VK_OBJECT_TYPE_COMMAND_POOL;
        case ObjectType::SamplerYcbcrConversion:
            return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
        case ObjectType::DescriptorUpdateTemplate:
            return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
        case ObjectType::PrivateDataSlot:
            return VK_OBJECT_TYPE_PRIVATE_DATA_SLOT;
        case ObjectType::Surface:
            return VK_OBJECT_TYPE_SURFACE_KHR;
        case ObjectType::Swapchain:
            return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        case ObjectType::Display:
            return VK_OBJECT_TYPE_DISPLAY_KHR;
        case ObjectType::DisplayMode:
            return VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
        case ObjectType::DebugReportCallback:
            return VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT;
        case ObjectType::DebugUtilsMessenger:
            return VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT;
        case ObjectType::AccelerationStructure:
            return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        case ObjectType::ValidationCache:
            return VK_OBJECT_TYPE_VALIDATION_CACHE_EXT;
        case ObjectType::DeferredOperation:
            return VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR;
        }

        PE_ERROR("Unknown object type:");
        return VK_OBJECT_TYPE_UNKNOWN;
    }

    VkVertexInputRate GetVertexInputRateVK(VertexInputRate rate)
    {
        switch (rate)
        {
        case VertexInputRate::Vertex:
            return VK_VERTEX_INPUT_RATE_VERTEX;
        case VertexInputRate::Instance:
            return VK_VERTEX_INPUT_RATE_INSTANCE;
        }

        PE_ERROR("Unknown vertex input rate");
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }

    VkAttachmentDescriptionFlags GetAttachmentDescriptionVK(AttachmentDescriptionFlags flags)
    {
        if (!flags)
            return 0;

        VkAttachmentDescriptionFlags vkFlags = 0;

        if (flags & AttachmentDescription::MayAlias)
            vkFlags |= VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;

        return vkFlags;
    }
}
