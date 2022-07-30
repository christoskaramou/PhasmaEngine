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
            stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
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
            stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
        default:
            PE_ERROR("Unsupported image layout");
        }
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
            stageFlags = PipelineStage::ComputeShaderBit;
            accessMask = Access::ShaderReadBit | Access::ShaderWriteBit;
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
            stageFlags = PipelineStage::FragmentShaderBit | PipelineStage::ComputeShaderBit;
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

    template <class T, class U>
    U GetFlags(T flags, std::map<T, U> &translator)
    {
        static_assert(std::is_integral_v<T>, "GetFlags: T must be integral");
        static_assert(std::is_integral_v<U>, "GetFlags: U must be integral");

        if (!flags)
            return U{};

        if (translator.find(flags) == translator.end())
        {
            // Emplace a new pair into translator and get the flags ref
            U &transFlags = translator.emplace(flags, U{}).first->second;

            for (auto &pair : translator)
            {
                if ((flags & pair.first) == pair.first)
                    transFlags |= pair.second;
            }

            return transFlags;
        }

        return translator[flags];
    }

    template <>
    ImageLayout Translate(VkImageLayout layout)
    {
        using T = std::underlying_type_t<VkImageLayout>;
        static std::map<T, ImageLayout> s_translator{
            {(T)VK_IMAGE_LAYOUT_UNDEFINED, ImageLayout::Undefined},
            {(T)VK_IMAGE_LAYOUT_GENERAL, ImageLayout::General},
            {(T)VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, ImageLayout::ColorAttachment},
            {(T)VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, ImageLayout::DepthStencilAttachment},
            {(T)VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, ImageLayout::DepthStencilReadOnly},
            {(T)VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ImageLayout::ShaderReadOnly},
            {(T)VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ImageLayout::TransferSrc},
            {(T)VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ImageLayout::TransferDst},
            {(T)VK_IMAGE_LAYOUT_PREINITIALIZED, ImageLayout::Preinitialized},
            {(T)VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL, ImageLayout::DepthReadOnlyStencilAttachment},
            {(T)VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL, ImageLayout::DepthAttachmentStencilReadOnly},
            {(T)VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, ImageLayout::DepthAttachment},
            {(T)VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, ImageLayout::DepthReadOnly},
            {(T)VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL, ImageLayout::StencilAttachment},
            {(T)VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL, ImageLayout::StencilReadOnly},
            {(T)VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, ImageLayout::ReadOnly},
            {(T)VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, ImageLayout::Attachment},
            {(T)VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, ImageLayout::PresentSrc},
            {(T)VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR, ImageLayout::SharedPresent},
            {(T)VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT, ImageLayout::FragmentDensityMap},
            {(T)VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR, ImageLayout::FragmentShadingRateAttachment}};

        return s_translator[(T)layout];
    }

    template <>
    VkImageLayout Translate(ImageLayout layout)
    {
        using T = std::underlying_type_t<ImageLayout>;
        static std::map<T, VkImageLayout> s_translator{
            {(T)ImageLayout::Undefined, VK_IMAGE_LAYOUT_UNDEFINED},
            {(T)ImageLayout::General, VK_IMAGE_LAYOUT_GENERAL},
            {(T)ImageLayout::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::DepthStencilAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::DepthStencilReadOnly, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::ShaderReadOnly, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::TransferSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
            {(T)ImageLayout::TransferDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
            {(T)ImageLayout::Preinitialized, VK_IMAGE_LAYOUT_PREINITIALIZED},
            {(T)ImageLayout::DepthReadOnlyStencilAttachment, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::DepthAttachmentStencilReadOnly, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::DepthReadOnly, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::StencilAttachment, VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::StencilReadOnly, VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::ReadOnly, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL},
            {(T)ImageLayout::Attachment, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL},
            {(T)ImageLayout::PresentSrc, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
            {(T)ImageLayout::SharedPresent, VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR},
            {(T)ImageLayout::FragmentDensityMap, VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT},
            {(T)ImageLayout::FragmentShadingRateAttachment, VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR}};

        return s_translator[(T)layout];
    }

    template <>
    VkShaderStageFlags Translate(ShaderStageFlags flags)
    {
        using T = ShaderStageFlags::Type;
        static std::map<T, VkShaderStageFlags> s_translator{
            {(T)ShaderStage::VertexBit, VK_SHADER_STAGE_VERTEX_BIT},
            {(T)ShaderStage::TesslationControlBit, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
            {(T)ShaderStage::TesslationEvaluationBit, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
            {(T)ShaderStage::GeometryBit, VK_SHADER_STAGE_GEOMETRY_BIT},
            {(T)ShaderStage::FragmentBit, VK_SHADER_STAGE_FRAGMENT_BIT},
            {(T)ShaderStage::ComputeBit, VK_SHADER_STAGE_COMPUTE_BIT},
            {(T)ShaderStage::AllGraphics, VK_SHADER_STAGE_ALL_GRAPHICS},
            {(T)ShaderStage::All, VK_SHADER_STAGE_ALL},
            {(T)ShaderStage::RaygenBit, VK_SHADER_STAGE_RAYGEN_BIT_NV},
            {(T)ShaderStage::AnyHitBit, VK_SHADER_STAGE_ANY_HIT_BIT_NV},
            {(T)ShaderStage::ClosestHitBit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV},
            {(T)ShaderStage::MissBit, VK_SHADER_STAGE_MISS_BIT_NV},
            {(T)ShaderStage::IntersectionBit, VK_SHADER_STAGE_INTERSECTION_BIT_NV},
            {(T)ShaderStage::CallableBit, VK_SHADER_STAGE_CALLABLE_BIT_NV}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkShaderStageFlags Translate(ShaderStage flag)
    {
        return Translate<VkShaderStageFlags, ShaderStageFlags>(flag);
    }

    template <>
    VkFilter Translate(Filter filter)
    {
        using T = std::underlying_type_t<Filter>;
        static std::map<T, VkFilter> s_translator{
            {(T)Filter::Nearest, VK_FILTER_NEAREST},
            {(T)Filter::Linear, VK_FILTER_LINEAR},
            {(T)Filter::Cubic, VK_FILTER_CUBIC_IMG}};

        return s_translator[(T)filter];
    }

    template <>
    VkImageAspectFlags Translate(ImageAspectFlags flags)
    {
        using T = ImageAspectFlags::Type;
        static std::map<T, VkImageAspectFlags> s_translator{
            {(T)ImageAspect::ColorBit, VK_IMAGE_ASPECT_COLOR_BIT},
            {(T)ImageAspect::DepthBit, VK_IMAGE_ASPECT_DEPTH_BIT},
            {(T)ImageAspect::StencilBit, VK_IMAGE_ASPECT_STENCIL_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkImageAspectFlags Translate(ImageAspect flag)
    {
        return Translate<VkImageAspectFlags, ImageAspectFlags>(flag);
    }

    template <>
    VkSamplerMipmapMode Translate(SamplerMipmapMode mode)
    {
        using T = std::underlying_type_t<SamplerMipmapMode>;
        static std::map<T, VkSamplerMipmapMode> s_translator{
            {(T)SamplerMipmapMode::Nearest, VK_SAMPLER_MIPMAP_MODE_NEAREST},
            {(T)SamplerMipmapMode::Linear, VK_SAMPLER_MIPMAP_MODE_LINEAR}};

        return s_translator[(T)mode];
    }

    template <>
    VkSamplerAddressMode Translate(SamplerAddressMode mode)
    {
        using T = std::underlying_type_t<SamplerAddressMode>;
        static std::map<T, VkSamplerAddressMode> s_translator{
            {(T)SamplerAddressMode::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT},
            {(T)SamplerAddressMode::MirroredRepeat, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT},
            {(T)SamplerAddressMode::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
            {(T)SamplerAddressMode::ClampToBorder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER},
            {(T)SamplerAddressMode::MirrorClampToEdge, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE}};

        return s_translator[(T)mode];
    }

    template <>
    VkCompareOp Translate(CompareOp op)
    {
        using T = std::underlying_type_t<CompareOp>;
        static std::map<T, VkCompareOp> s_translator{
            {(T)CompareOp::Never, VK_COMPARE_OP_NEVER},
            {(T)CompareOp::Less, VK_COMPARE_OP_LESS},
            {(T)CompareOp::Equal, VK_COMPARE_OP_EQUAL},
            {(T)CompareOp::LessOrEqual, VK_COMPARE_OP_LESS_OR_EQUAL},
            {(T)CompareOp::Greater, VK_COMPARE_OP_GREATER},
            {(T)CompareOp::NotEqual, VK_COMPARE_OP_NOT_EQUAL},
            {(T)CompareOp::GreaterOrEqual, VK_COMPARE_OP_GREATER_OR_EQUAL},
            {(T)CompareOp::Always, VK_COMPARE_OP_ALWAYS}};

        return s_translator[(T)op];
    }

    template <>
    VkBorderColor Translate(BorderColor color)
    {
        using T = std::underlying_type_t<BorderColor>;
        static std::map<T, VkBorderColor> s_translator{
            {(T)BorderColor::FloatTransparentBlack, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK},
            {(T)BorderColor::IntTransparentBlack, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK},
            {(T)BorderColor::FloatOpaqueBlack, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
            {(T)BorderColor::IntOpaqueBlack, VK_BORDER_COLOR_INT_OPAQUE_BLACK},
            {(T)BorderColor::FloatOpaqueWhite, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
            {(T)BorderColor::IntOpaqueWhite, VK_BORDER_COLOR_INT_OPAQUE_WHITE}};

        return s_translator[(T)color];
    }

    template <>
    VkImageType Translate(ImageType type)
    {
        using T = std::underlying_type_t<ImageType>;
        static std::map<T, VkImageType> s_translator{
            {(T)ImageType::Type1D, VK_IMAGE_TYPE_1D},
            {(T)ImageType::Type2D, VK_IMAGE_TYPE_2D},
            {(T)ImageType::Type3D, VK_IMAGE_TYPE_3D}};

        return s_translator[(T)type];
    }

    template <>
    VkImageViewType Translate(ImageViewType type)
    {
        using T = std::underlying_type_t<ImageViewType>;
        static std::map<T, VkImageViewType> s_translator{
            {(T)ImageViewType::Type1D, VK_IMAGE_VIEW_TYPE_1D},
            {(T)ImageViewType::Type2D, VK_IMAGE_VIEW_TYPE_2D},
            {(T)ImageViewType::Type3D, VK_IMAGE_VIEW_TYPE_3D},
            {(T)ImageViewType::TypeCube, VK_IMAGE_VIEW_TYPE_CUBE},
            {(T)ImageViewType::Type1DArray, VK_IMAGE_VIEW_TYPE_1D_ARRAY},
            {(T)ImageViewType::Type2DArray, VK_IMAGE_VIEW_TYPE_2D_ARRAY},
            {(T)ImageViewType::TypeCubeArray, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY}};

        return s_translator[(T)type];
    }

    template <>
    VkSampleCountFlagBits Translate(SampleCount count)
    {
        using T = std::underlying_type_t<SampleCount>;
        static std::map<T, VkSampleCountFlagBits> s_translator{
            {(T)SampleCount::Count1, VK_SAMPLE_COUNT_1_BIT},
            {(T)SampleCount::Count2, VK_SAMPLE_COUNT_2_BIT},
            {(T)SampleCount::Count4, VK_SAMPLE_COUNT_4_BIT},
            {(T)SampleCount::Count8, VK_SAMPLE_COUNT_8_BIT},
            {(T)SampleCount::Count16, VK_SAMPLE_COUNT_16_BIT},
            {(T)SampleCount::Count32, VK_SAMPLE_COUNT_32_BIT},
            {(T)SampleCount::Count64, VK_SAMPLE_COUNT_64_BIT}};

        return s_translator[(T)count];
    }

    template <>
    VkFormat Translate(Format format)
    {
        using T = std::underlying_type_t<Format>;
        static std::map<T, VkFormat> s_translator{
            {(T)Format::Undefined, VK_FORMAT_UNDEFINED},
            {(T)Format::R8SInt, VK_FORMAT_R8_SINT},
            {(T)Format::R8UInt, VK_FORMAT_R8_UINT},
            {(T)Format::R8Unorm, VK_FORMAT_R8_UNORM},
            {(T)Format::RGBA8Unorm, VK_FORMAT_R8G8B8A8_UNORM},
            {(T)Format::R16SInt, VK_FORMAT_R16_SINT},
            {(T)Format::R16UInt, VK_FORMAT_R16_UINT},
            {(T)Format::R16SFloat, VK_FORMAT_R16_SFLOAT},
            {(T)Format::R16Unorm, VK_FORMAT_R16_UNORM},
            {(T)Format::RG16SFloat, VK_FORMAT_R16G16_SFLOAT},
            {(T)Format::RGB16SInt, VK_FORMAT_R16G16B16_SINT},
            {(T)Format::RGB16UInt, VK_FORMAT_R16G16B16_UINT},
            {(T)Format::RGB16SFloat, VK_FORMAT_R16G16B16_SFLOAT},
            {(T)Format::RGBA16SFloat, VK_FORMAT_R16G16B16A16_SFLOAT},
            {(T)Format::R32SInt, VK_FORMAT_R32_SINT},
            {(T)Format::R32UInt, VK_FORMAT_R32_UINT},
            {(T)Format::R32SFloat, VK_FORMAT_R32_SFLOAT},
            {(T)Format::RG32SInt, VK_FORMAT_R32G32_SINT},
            {(T)Format::RG32UInt, VK_FORMAT_R32G32_UINT},
            {(T)Format::RG32SFloat, VK_FORMAT_R32G32_SFLOAT},
            {(T)Format::RGB32SInt, VK_FORMAT_R32G32B32_SINT},
            {(T)Format::RGB32UInt, VK_FORMAT_R32G32B32_UINT},
            {(T)Format::RGB32SFloat, VK_FORMAT_R32G32B32_SFLOAT},
            {(T)Format::RGBA32SInt, VK_FORMAT_R32G32B32A32_SINT},
            {(T)Format::RGBA32UInt, VK_FORMAT_R32G32B32A32_UINT},
            {(T)Format::RGBA32SFloat, VK_FORMAT_R32G32B32A32_SFLOAT},
            {(T)Format::BGRA8Unorm, VK_FORMAT_B8G8R8A8_UNORM},
            {(T)Format::D24UnormS8UInt, VK_FORMAT_D24_UNORM_S8_UINT},
            {(T)Format::D32SFloat, VK_FORMAT_D32_SFLOAT},
            {(T)Format::D32SFloatS8UInt, VK_FORMAT_D32_SFLOAT_S8_UINT}};

        return s_translator[(T)format];
    }

    template <>
    Format Translate(VkFormat format)
    {
        using T = std::underlying_type_t<VkFormat>;
        static std::map<T, Format> s_translator{
            {(T)VK_FORMAT_UNDEFINED, Format::Undefined},
            {(T)VK_FORMAT_R8_SINT, Format::R8SInt},
            {(T)VK_FORMAT_R8_UINT, Format::R8UInt},
            {(T)VK_FORMAT_R8_UNORM, Format::R8Unorm},
            {(T)VK_FORMAT_R8G8B8A8_UNORM, Format::RGBA8Unorm},
            {(T)VK_FORMAT_R16_SINT, Format::R16SInt},
            {(T)VK_FORMAT_R16_UINT, Format::R16UInt},
            {(T)VK_FORMAT_R16_SFLOAT, Format::R16SFloat},
            {(T)VK_FORMAT_R16_UNORM, Format::R16Unorm},
            {(T)VK_FORMAT_R16G16_SFLOAT, Format::RG16SFloat},
            {(T)VK_FORMAT_R16G16B16_SINT, Format::RGB16SInt},
            {(T)VK_FORMAT_R16G16B16_UINT, Format::RGB16UInt},
            {(T)VK_FORMAT_R16G16B16_SFLOAT, Format::RGB16SFloat},
            {(T)VK_FORMAT_R16G16B16A16_SFLOAT, Format::RGBA16SFloat},
            {(T)VK_FORMAT_R32_SINT, Format::R32SInt},
            {(T)VK_FORMAT_R32_UINT, Format::R32UInt},
            {(T)VK_FORMAT_R32_SFLOAT, Format::R32SFloat},
            {(T)VK_FORMAT_R32G32_SINT, Format::RG32SInt},
            {(T)VK_FORMAT_R32G32_UINT, Format::RG32UInt},
            {(T)VK_FORMAT_R32G32_SFLOAT, Format::RG32SFloat},
            {(T)VK_FORMAT_R32G32B32_SINT, Format::RGB32SInt},
            {(T)VK_FORMAT_R32G32B32_UINT, Format::RGB32UInt},
            {(T)VK_FORMAT_R32G32B32_SFLOAT, Format::RGB32SFloat},
            {(T)VK_FORMAT_R32G32B32A32_SINT, Format::RGBA32SInt},
            {(T)VK_FORMAT_R32G32B32A32_UINT, Format::RGBA32UInt},
            {(T)VK_FORMAT_R32G32B32A32_SFLOAT, Format::RGBA32SFloat},
            {(T)VK_FORMAT_B8G8R8A8_UNORM, Format::BGRA8Unorm},
            {(T)VK_FORMAT_D24_UNORM_S8_UINT, Format::D24UnormS8UInt},
            {(T)VK_FORMAT_D32_SFLOAT, Format::D32SFloat},
            {(T)VK_FORMAT_D32_SFLOAT_S8_UINT, Format::D32SFloatS8UInt}};

        return s_translator[(T)format];
    }

    template <>
    VkImageTiling Translate(ImageTiling tiling)
    {
        using T = std::underlying_type_t<ImageTiling>;
        static std::map<T, VkImageTiling> s_translator{
            {(T)ImageTiling::Optimal, VK_IMAGE_TILING_OPTIMAL},
            {(T)ImageTiling::Linear, VK_IMAGE_TILING_LINEAR}};

        return s_translator[(T)tiling];
    }

    template <>
    VkImageUsageFlags Translate(ImageUsageFlags flags)
    {
        using T = ImageUsageFlags::Type;
        static std::map<T, VkImageUsageFlags> s_translator{
            {(T)ImageUsage::TransferSrcBit, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
            {(T)ImageUsage::TransferDstBit, VK_IMAGE_USAGE_TRANSFER_DST_BIT},
            {(T)ImageUsage::SampledBit, VK_IMAGE_USAGE_SAMPLED_BIT},
            {(T)ImageUsage::StorageBit, VK_IMAGE_USAGE_STORAGE_BIT},
            {(T)ImageUsage::ColorAttachmentBit, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
            {(T)ImageUsage::DepthStencilAttachmentBit, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT},
            {(T)ImageUsage::TransientAttachmentBit, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT},
            {(T)ImageUsage::InputAttachmentBit, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT}};

        if (!flags)
            return 0;

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkImageUsageFlags Translate(ImageUsage flag)
    {
        return Translate<VkImageAspectFlags, ImageUsageFlags>(flag);
    }

    template <>
    VkSharingMode Translate(SharingMode mode)
    {
        using T = std::underlying_type_t<SharingMode>;
        static std::map<T, VkSharingMode> s_translator{
            {(T)SharingMode::Exclusive, VK_SHARING_MODE_EXCLUSIVE},
            {(T)SharingMode::Concurrent, VK_SHARING_MODE_CONCURRENT}};

        return s_translator[(T)mode];
    }

    template <>
    VkImageCreateFlags Translate(ImageCreateFlags flags)
    {
        using T = ImageCreateFlags::Type;
        static std::map<T, VkImageCreateFlags> s_translator{
            {(T)ImageCreate::SparceBindingBit, VK_IMAGE_CREATE_SPARSE_BINDING_BIT},
            {(T)ImageCreate::SparceResidencyBit, VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT},
            {(T)ImageCreate::SparceAliasedBit, VK_IMAGE_CREATE_SPARSE_ALIASED_BIT},
            {(T)ImageCreate::MutableFormatBit, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT},
            {(T)ImageCreate::CubeCompatibleBit, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT},
            {(T)ImageCreate::AliasBit, VK_IMAGE_CREATE_ALIAS_BIT},
            {(T)ImageCreate::SplitInstanceBindRegionsBit, VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT},
            {(T)ImageCreate::Array2DCompatibleBit, VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT},
            {(T)ImageCreate::BlockTexeViewCompatibleBit, VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT},
            {(T)ImageCreate::ExtendedUsageBit, VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
            {(T)ImageCreate::ProtectedBit, VK_IMAGE_CREATE_PROTECTED_BIT},
            {(T)ImageCreate::DisjointBit, VK_IMAGE_CREATE_DISJOINT_BIT},
            {(T)ImageCreate::CornerSampledBit, VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV},
            {(T)ImageCreate::SampleLocationsCompatibleDepthBit, VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT},
            {(T)ImageCreate::SubsampledBit, VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT},
            {(T)ImageCreate::View2DCompatibleBit, VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkImageCreateFlags Translate(ImageCreate flag)
    {
        return Translate<VkImageCreateFlags, ImageCreateFlags>(flag);
    }

    template <>
    VkCommandBufferUsageFlags Translate(CommandBufferUsageFlags flags)
    {
        using T = CommandBufferUsageFlags::Type;
        static std::map<T, VkCommandBufferUsageFlags> s_translator{
            {(T)CommandBufferUsage::OneTimeSubmitBit, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT},
            {(T)CommandBufferUsage::RenderPassContinueBit, VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT},
            {(T)CommandBufferUsage::SimultaneousUseBit, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkCommandBufferUsageFlags Translate(CommandBufferUsage flag)
    {
        return Translate<VkCommandBufferUsageFlags, CommandBufferUsageFlags>(flag);
    }

    template <>
    VkBufferUsageFlags Translate(BufferUsageFlags flags)
    {
        using T = BufferUsageFlags::Type;
        static std::map<T, VkBufferUsageFlags> s_translator{
            {(T)BufferUsage::TransferSrcBit, VK_BUFFER_USAGE_TRANSFER_SRC_BIT},
            {(T)BufferUsage::TransferDstBit, VK_BUFFER_USAGE_TRANSFER_DST_BIT},
            {(T)BufferUsage::UniformTexelBufferBit, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT},
            {(T)BufferUsage::StorageTexelBufferBit, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT},
            {(T)BufferUsage::UniformBufferBit, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
            {(T)BufferUsage::StorageBufferBit, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT},
            {(T)BufferUsage::IndexBufferBit, VK_BUFFER_USAGE_INDEX_BUFFER_BIT},
            {(T)BufferUsage::VertexBufferBit, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT},
            {(T)BufferUsage::IndirectBufferBit, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT},
            {(T)BufferUsage::ShaderDeviceAddressBit, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT},
            {(T)BufferUsage::TransformFeedbackBufferBit, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT},
            {(T)BufferUsage::TransformFeedbackCounterBufferBit, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT},
            {(T)BufferUsage::ConditionalRenderingBit, VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT},
            {(T)BufferUsage::AccelerationStructureBuildInputReadBit, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR},
            {(T)BufferUsage::AccelerationStructureStorageBit, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR},
            {(T)BufferUsage::ShaderBindingTableBit, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkBufferUsageFlags Translate(BufferUsage flag)
    {
        return Translate<VkBufferUsageFlags, BufferUsageFlags>(flag);
    }

    template <>
    VkOffset3D Translate(const Offset3D &offset)
    {
        VkOffset3D vkOffset;
        vkOffset.x = offset.x;
        vkOffset.y = offset.y;
        vkOffset.z = offset.z;
        return vkOffset;
    }

    template <>
    VkColorSpaceKHR Translate(ColorSpace colorSpace)
    {
        using T = std::underlying_type_t<ColorSpace>;
        static std::map<T, VkColorSpaceKHR> s_translator{
            {(T)ColorSpace::SrgbNonLinear, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
            {(T)ColorSpace::DisplayP3NonLinear, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT},
            {(T)ColorSpace::ExtendedSrgbLinear, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
            {(T)ColorSpace::DisplayP3Linear, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT},
            {(T)ColorSpace::DciP3NonLinear, VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT},
            {(T)ColorSpace::BT709Linear, VK_COLOR_SPACE_BT709_LINEAR_EXT},
            {(T)ColorSpace::BT709NonLinear, VK_COLOR_SPACE_BT709_NONLINEAR_EXT},
            {(T)ColorSpace::BT2020Linear, VK_COLOR_SPACE_BT2020_LINEAR_EXT},
            {(T)ColorSpace::HDR10St2084, VK_COLOR_SPACE_HDR10_ST2084_EXT},
            {(T)ColorSpace::DolbyVision, VK_COLOR_SPACE_DOLBYVISION_EXT},
            {(T)ColorSpace::HDR10Hlg, VK_COLOR_SPACE_HDR10_HLG_EXT},
            {(T)ColorSpace::AdobeRgbLinear, VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT},
            {(T)ColorSpace::AdobeRgbNonLinear, VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT},
            {(T)ColorSpace::PassThrough, VK_COLOR_SPACE_PASS_THROUGH_EXT},
            {(T)ColorSpace::ExtendedSrgbNonLinear, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT}};

        return s_translator[(T)colorSpace];
    }

    template <>
    ColorSpace Translate(VkColorSpaceKHR colorSpace)
    {
        using T = std::underlying_type_t<VkColorSpaceKHR>;
        static std::map<T, ColorSpace> s_translator{
            {(T)VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, ColorSpace::SrgbNonLinear},
            {(T)VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, ColorSpace::DisplayP3NonLinear},
            {(T)VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, ColorSpace::ExtendedSrgbLinear},
            {(T)VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT, ColorSpace::DisplayP3Linear},
            {(T)VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT, ColorSpace::DciP3NonLinear},
            {(T)VK_COLOR_SPACE_BT709_LINEAR_EXT, ColorSpace::BT709Linear},
            {(T)VK_COLOR_SPACE_BT709_NONLINEAR_EXT, ColorSpace::BT709NonLinear},
            {(T)VK_COLOR_SPACE_BT2020_LINEAR_EXT, ColorSpace::BT2020Linear},
            {(T)VK_COLOR_SPACE_HDR10_ST2084_EXT, ColorSpace::HDR10St2084},
            {(T)VK_COLOR_SPACE_DOLBYVISION_EXT, ColorSpace::DolbyVision},
            {(T)VK_COLOR_SPACE_HDR10_HLG_EXT, ColorSpace::HDR10Hlg},
            {(T)VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT, ColorSpace::AdobeRgbLinear},
            {(T)VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT, ColorSpace::AdobeRgbNonLinear},
            {(T)VK_COLOR_SPACE_PASS_THROUGH_EXT, ColorSpace::PassThrough},
            {(T)VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, ColorSpace::ExtendedSrgbNonLinear}};

        return s_translator[(T)colorSpace];
    }

    template <>
    VkPresentModeKHR Translate(PresentMode presentMode)
    {
        using T = std::underlying_type_t<PresentMode>;
        static std::map<T, VkPresentModeKHR> s_translator{
            {(T)PresentMode::Immediate, VK_PRESENT_MODE_IMMEDIATE_KHR},
            {(T)PresentMode::Mailbox, VK_PRESENT_MODE_MAILBOX_KHR},
            {(T)PresentMode::Fifo, VK_PRESENT_MODE_FIFO_KHR},
            {(T)PresentMode::FifoRelaxed, VK_PRESENT_MODE_FIFO_RELAXED_KHR},
            {(T)PresentMode::SharedDemandRefresh, VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR},
            {(T)PresentMode::SharedContinuousRefresh, VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR}};

        return s_translator[(T)presentMode];
    }

    template <>
    PresentMode Translate(VkPresentModeKHR presentMode)
    {
        using T = std::underlying_type_t<VkPresentModeKHR>;
        static std::map<T, PresentMode> s_translator{
            {(T)VK_PRESENT_MODE_IMMEDIATE_KHR, PresentMode::Immediate},
            {(T)VK_PRESENT_MODE_MAILBOX_KHR, PresentMode::Mailbox},
            {(T)VK_PRESENT_MODE_FIFO_KHR, PresentMode::Fifo},
            {(T)VK_PRESENT_MODE_FIFO_RELAXED_KHR, PresentMode::FifoRelaxed},
            {(T)VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR, PresentMode::SharedDemandRefresh},
            {(T)VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, PresentMode::SharedContinuousRefresh}};

        return s_translator[(T)presentMode];
    }

    template <>
    VkPipelineStageFlags Translate(PipelineStageFlags flags)
    {
        using T = PipelineStageFlags::Type;
        static std::map<T, VkPipelineStageFlags> s_translator{
            {(T)PipelineStage::TopOfPipeBit, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT},
            {(T)PipelineStage::DrawIndirectBit, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT},
            {(T)PipelineStage::VertexInputBit, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT},
            {(T)PipelineStage::VertexShaderBit, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT},
            {(T)PipelineStage::TessellationControlShaderBit, VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT},
            {(T)PipelineStage::TessellationEvaluationShaderBit, VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT},
            {(T)PipelineStage::GeometryShaderBit, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT},
            {(T)PipelineStage::FragmentShaderBit, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
            {(T)PipelineStage::EarlyFragmentTestsBit, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT},
            {(T)PipelineStage::LateFragmentTestsBit, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT},
            {(T)PipelineStage::ColorAttachmentOutputBit, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
            {(T)PipelineStage::ComputeShaderBit, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
            {(T)PipelineStage::TransferBit, VK_PIPELINE_STAGE_TRANSFER_BIT},
            {(T)PipelineStage::BottomOfPipeBit, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT},
            {(T)PipelineStage::HostBit, VK_PIPELINE_STAGE_HOST_BIT},
            {(T)PipelineStage::AllGraphicsBit, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT},
            {(T)PipelineStage::AllCommandsBit, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT},
            {(T)PipelineStage::TransformFeedbackBit, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT},
            {(T)PipelineStage::ConditionalRenderingBit, VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT},
            {(T)PipelineStage::AccelerationStructureBuildBit, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
            {(T)PipelineStage::RayTracingShaderBit, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR},
            {(T)PipelineStage::TaskShaderBit, VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV},
            {(T)PipelineStage::MeshShaderBit, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV},
            {(T)PipelineStage::FragmentDensityProcessBit, VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT},
            {(T)PipelineStage::FragmentShadingRateAttechmentBit, VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkPipelineStageFlags Translate(PipelineStage flag)
    {
        return Translate<VkPipelineStageFlags, PipelineStageFlags>(flag);
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

    VkImageAspectFlags GetAspectMaskVK(Format format)
    {
        return IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageAspectFlags GetAspectMaskVK(VkFormat format)
    {
        return IsDepthFormatVK(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }
 
    ImageAspectFlags GetAspectMask(Format format)
    {
        return IsDepthFormat(format) ? ImageAspect::DepthBit : ImageAspect::ColorBit;
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

    template <>
    VkAttachmentLoadOp Translate(AttachmentLoadOp loadOp)
    {
        using T = std::underlying_type_t<AttachmentLoadOp>;
        static std::map<T, VkAttachmentLoadOp> s_translator{
            {(T)AttachmentLoadOp::Load, VK_ATTACHMENT_LOAD_OP_LOAD},
            {(T)AttachmentLoadOp::Clear, VK_ATTACHMENT_LOAD_OP_CLEAR},
            {(T)AttachmentLoadOp::DontCare, VK_ATTACHMENT_LOAD_OP_DONT_CARE}};

        return s_translator[(T)loadOp];
    }

    template <>
    VkAttachmentStoreOp Translate(AttachmentStoreOp storeOp)
    {
        using T = std::underlying_type_t<AttachmentStoreOp>;
        static std::map<T, VkAttachmentStoreOp> s_translator{
            {(T)AttachmentStoreOp::Store, VK_ATTACHMENT_STORE_OP_STORE},
            {(T)AttachmentStoreOp::DontCare, VK_ATTACHMENT_STORE_OP_DONT_CARE}};

        return s_translator[(T)storeOp];
    }

    template <>
    VkAccessFlags Translate(AccessFlags flags)
    {
        using T = AccessFlags::Type;
        static std::map<T, VkAccessFlags> s_translator{
            {(T)Access::IndirectCommandReadBit, VK_ACCESS_INDIRECT_COMMAND_READ_BIT},
            {(T)Access::IndexReadBit, VK_ACCESS_INDEX_READ_BIT},
            {(T)Access::VertexAttributeReadBit, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT},
            {(T)Access::UniformReadBit, VK_ACCESS_UNIFORM_READ_BIT},
            {(T)Access::InputAttachmentReadBit, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT},
            {(T)Access::ShaderReadBit, VK_ACCESS_SHADER_READ_BIT},
            {(T)Access::ShaderWriteBit, VK_ACCESS_SHADER_WRITE_BIT},
            {(T)Access::ColorAttachmentReadBit, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT},
            {(T)Access::ColorAttachmentWriteBit, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
            {(T)Access::DepthStencilAttachmentReadBit, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT},
            {(T)Access::DepthStencilAttachmentWriteBit, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
            {(T)Access::TransferReadBit, VK_ACCESS_TRANSFER_READ_BIT},
            {(T)Access::TransferWriteBit, VK_ACCESS_TRANSFER_WRITE_BIT},
            {(T)Access::HostReadBit, VK_ACCESS_HOST_READ_BIT},
            {(T)Access::HostWriteBit, VK_ACCESS_HOST_WRITE_BIT},
            {(T)Access::MemoryReadBit, VK_ACCESS_MEMORY_READ_BIT},
            {(T)Access::MemoryWriteBit, VK_ACCESS_MEMORY_WRITE_BIT},
            {(T)Access::TransformFeedbackWriteBit, VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT},
            {(T)Access::TransformFeedbackCounterReadBit, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT},
            {(T)Access::TransformFeedbackCounterWriteBit, VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT},
            {(T)Access::ConditionalRenderingReadBit, VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT},
            {(T)Access::ColorAttachmentReadNoncoherentBit, VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT},
            {(T)Access::AccelerationStructureReadBitKhr, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR},
            {(T)Access::AccelerationStructureWriteBitKhr, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR},
            {(T)Access::FragmentDensityMapReadBit, VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT},
            {(T)Access::FragmentShadingRateAttachmentReadBitKhr, VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkAccessFlags Translate(Access flag)
    {
        return Translate<VkAccessFlags, AccessFlags>(flag);
    }

    template <>
    VmaAllocationCreateFlags Translate(AllocationCreateFlags flags)
    {
        using T = AllocationCreateFlags::Type;
        static std::map<T, VmaAllocationCreateFlags> s_translator{
            {(T)AllocationCreate::DedicatedMemoryBit, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT},
            {(T)AllocationCreate::NeverAllocateBit, VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT},
            {(T)AllocationCreate::MappedBit, VMA_ALLOCATION_CREATE_MAPPED_BIT},
            {(T)AllocationCreate::UserDataCopyStringBit, VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT},
            {(T)AllocationCreate::UpperAddressBit, VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT},
            {(T)AllocationCreate::DontBindBit, VMA_ALLOCATION_CREATE_DONT_BIND_BIT},
            {(T)AllocationCreate::WithinBudgetBit, VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT},
            {(T)AllocationCreate::CanAliasBit, VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT},
            {(T)AllocationCreate::HostAccessSequentialWriteBit, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT},
            {(T)AllocationCreate::HostAccessRandomBit, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT},
            {(T)AllocationCreate::HostAccessAllowTransferInsteadBit, VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT},
            {(T)AllocationCreate::StrategyMinMemoryBit, VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT},
            {(T)AllocationCreate::StrategyMinTimeBit, VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT},
            {(T)AllocationCreate::StrategyMinOffsetBit, VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VmaAllocationCreateFlags Translate(AllocationCreate flag)
    {
        return Translate<VmaAllocationCreateFlags, AllocationCreateFlags>(flag);
    }

    template <>
    VkBlendFactor Translate(BlendFactor blendFactor)
    {
        using T = std::underlying_type_t<BlendFactor>;
        static std::map<T, VkBlendFactor> s_translator{
            {(T)BlendFactor::Zero, VK_BLEND_FACTOR_ZERO},
            {(T)BlendFactor::One, VK_BLEND_FACTOR_ONE},
            {(T)BlendFactor::SrcColor, VK_BLEND_FACTOR_SRC_COLOR},
            {(T)BlendFactor::OneMinusSrcColor, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR},
            {(T)BlendFactor::DstColor, VK_BLEND_FACTOR_DST_COLOR},
            {(T)BlendFactor::OneMinusDstColor, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR},
            {(T)BlendFactor::SrcAlpha, VK_BLEND_FACTOR_SRC_ALPHA},
            {(T)BlendFactor::OneMinusSrcAlpha, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA},
            {(T)BlendFactor::DstAlpha, VK_BLEND_FACTOR_DST_ALPHA},
            {(T)BlendFactor::OneMinusDstAlpha, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA},
            {(T)BlendFactor::ConstantColor, VK_BLEND_FACTOR_CONSTANT_COLOR},
            {(T)BlendFactor::OneMinusConstantColor, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR},
            {(T)BlendFactor::ConstantAlpha, VK_BLEND_FACTOR_CONSTANT_ALPHA},
            {(T)BlendFactor::OneMinusConstantAlpha, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA},
            {(T)BlendFactor::SrcAlphaSaturate, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE},
            {(T)BlendFactor::Src1Color, VK_BLEND_FACTOR_SRC1_COLOR},
            {(T)BlendFactor::OneMinusSrc1Color, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR},
            {(T)BlendFactor::Src1Alpha, VK_BLEND_FACTOR_SRC1_ALPHA},
            {(T)BlendFactor::OneMinusSrc1Alpha, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA}};

        return s_translator[(T)blendFactor];
    }

    template <>
    VkBlendOp Translate(BlendOp blendOp)
    {
        using T = std::underlying_type_t<BlendOp>;
        static std::map<T, VkBlendOp> s_translator{
            {(T)BlendOp::Add, VK_BLEND_OP_ADD},
            {(T)BlendOp::Subtract, VK_BLEND_OP_SUBTRACT},
            {(T)BlendOp::ReverseSubtract, VK_BLEND_OP_REVERSE_SUBTRACT},
            {(T)BlendOp::Min, VK_BLEND_OP_MIN},
            {(T)BlendOp::Max, VK_BLEND_OP_MAX},
            {(T)BlendOp::Zero, VK_BLEND_OP_ZERO_EXT},
            {(T)BlendOp::Src, VK_BLEND_OP_SRC_EXT},
            {(T)BlendOp::Dst, VK_BLEND_OP_DST_EXT},
            {(T)BlendOp::SrcOver, VK_BLEND_OP_SRC_OVER_EXT},
            {(T)BlendOp::DstOver, VK_BLEND_OP_DST_OVER_EXT},
            {(T)BlendOp::SrcIn, VK_BLEND_OP_SRC_IN_EXT},
            {(T)BlendOp::DstIn, VK_BLEND_OP_DST_IN_EXT},
            {(T)BlendOp::SrcOut, VK_BLEND_OP_SRC_OUT_EXT},
            {(T)BlendOp::DstOut, VK_BLEND_OP_DST_OUT_EXT},
            {(T)BlendOp::SrcAtop, VK_BLEND_OP_SRC_ATOP_EXT},
            {(T)BlendOp::DstAtop, VK_BLEND_OP_DST_ATOP_EXT},
            {(T)BlendOp::Xor, VK_BLEND_OP_XOR_EXT},
            {(T)BlendOp::Multiply, VK_BLEND_OP_MULTIPLY_EXT},
            {(T)BlendOp::Screen, VK_BLEND_OP_SCREEN_EXT},
            {(T)BlendOp::Overlay, VK_BLEND_OP_OVERLAY_EXT},
            {(T)BlendOp::Darken, VK_BLEND_OP_DARKEN_EXT},
            {(T)BlendOp::Lighten, VK_BLEND_OP_LIGHTEN_EXT},
            {(T)BlendOp::Colordodge, VK_BLEND_OP_COLORDODGE_EXT},
            {(T)BlendOp::Colorburn, VK_BLEND_OP_COLORBURN_EXT},
            {(T)BlendOp::Hardlight, VK_BLEND_OP_HARDLIGHT_EXT},
            {(T)BlendOp::Softlight, VK_BLEND_OP_SOFTLIGHT_EXT},
            {(T)BlendOp::Difference, VK_BLEND_OP_DIFFERENCE_EXT},
            {(T)BlendOp::Exclusion, VK_BLEND_OP_EXCLUSION_EXT},
            {(T)BlendOp::Invert, VK_BLEND_OP_INVERT_EXT},
            {(T)BlendOp::InvertRgb, VK_BLEND_OP_INVERT_RGB_EXT},
            {(T)BlendOp::Lineardodge, VK_BLEND_OP_LINEARDODGE_EXT},
            {(T)BlendOp::Linearburn, VK_BLEND_OP_LINEARBURN_EXT},
            {(T)BlendOp::Vividlight, VK_BLEND_OP_VIVIDLIGHT_EXT},
            {(T)BlendOp::Linearlight, VK_BLEND_OP_LINEARLIGHT_EXT},
            {(T)BlendOp::Pinlight, VK_BLEND_OP_PINLIGHT_EXT},
            {(T)BlendOp::Hardmix, VK_BLEND_OP_HARDMIX_EXT},
            {(T)BlendOp::HslHue, VK_BLEND_OP_HSL_HUE_EXT},
            {(T)BlendOp::HslSaturation, VK_BLEND_OP_HSL_SATURATION_EXT},
            {(T)BlendOp::HslColor, VK_BLEND_OP_HSL_COLOR_EXT},
            {(T)BlendOp::HslLuminosity, VK_BLEND_OP_HSL_LUMINOSITY_EXT},
            {(T)BlendOp::Plus, VK_BLEND_OP_PLUS_EXT},
            {(T)BlendOp::PlusClamped, VK_BLEND_OP_PLUS_CLAMPED_EXT},
            {(T)BlendOp::PlusClampedAlpha, VK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT},
            {(T)BlendOp::PlusDarker, VK_BLEND_OP_PLUS_DARKER_EXT},
            {(T)BlendOp::Minus, VK_BLEND_OP_MINUS_EXT},
            {(T)BlendOp::MinusClamped, VK_BLEND_OP_MINUS_CLAMPED_EXT},
            {(T)BlendOp::Contrast, VK_BLEND_OP_CONTRAST_EXT},
            {(T)BlendOp::InvertOvg, VK_BLEND_OP_INVERT_OVG_EXT},
            {(T)BlendOp::Red, VK_BLEND_OP_RED_EXT},
            {(T)BlendOp::Green, VK_BLEND_OP_GREEN_EXT},
            {(T)BlendOp::Blue, VK_BLEND_OP_BLUE_EXT}};

        return s_translator[(T)blendOp];
    }

    template <>
    VkColorComponentFlags Translate(ColorComponentFlags flags)
    {
        using T = ColorComponentFlags::Type;
        static std::map<T, VkColorComponentFlags> s_translator{
            {(T)ColorComponent::RBit, VK_COLOR_COMPONENT_R_BIT},
            {(T)ColorComponent::GBit, VK_COLOR_COMPONENT_G_BIT},
            {(T)ColorComponent::BBit, VK_COLOR_COMPONENT_B_BIT},
            {(T)ColorComponent::ABit, VK_COLOR_COMPONENT_A_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkColorComponentFlags Translate(ColorComponent flag)
    {
        return Translate<VkColorComponentFlags, ColorComponentFlags>(flag);
    }

    template <>
    VkDynamicState Translate(DynamicState state)
    {
        using T = std::underlying_type_t<DynamicState>;
        static std::map<T, VkDynamicState> s_translator{
            {(T)DynamicState::Viewport, VK_DYNAMIC_STATE_VIEWPORT},
            {(T)DynamicState::Scissor, VK_DYNAMIC_STATE_SCISSOR},
            {(T)DynamicState::LineWidth, VK_DYNAMIC_STATE_LINE_WIDTH},
            {(T)DynamicState::DepthBias, VK_DYNAMIC_STATE_DEPTH_BIAS},
            {(T)DynamicState::BlendConstants, VK_DYNAMIC_STATE_BLEND_CONSTANTS},
            {(T)DynamicState::DepthBounds, VK_DYNAMIC_STATE_DEPTH_BOUNDS},
            {(T)DynamicState::StencilCompareMask, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK},
            {(T)DynamicState::StencilWriteMask, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK},
            {(T)DynamicState::StencilReference, VK_DYNAMIC_STATE_STENCIL_REFERENCE},
            {(T)DynamicState::CullMode, VK_DYNAMIC_STATE_CULL_MODE},
            {(T)DynamicState::FrontFace, VK_DYNAMIC_STATE_FRONT_FACE},
            {(T)DynamicState::PrimitiveTopology, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY},
            {(T)DynamicState::ViewportWithCount, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT},
            {(T)DynamicState::ScissorWithCount, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT},
            {(T)DynamicState::VertexInputBindingStride, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT},
            {(T)DynamicState::DepthTestEnable, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT},
            {(T)DynamicState::DepthWriteEnable, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT},
            {(T)DynamicState::DepthCompareOp, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT},
            {(T)DynamicState::DepthBoundsTestEnable, VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT},
            {(T)DynamicState::StencilTestEnable, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT},
            {(T)DynamicState::StencilOp, VK_DYNAMIC_STATE_STENCIL_OP_EXT},
            {(T)DynamicState::RasterizerDiscardEnable, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT},
            {(T)DynamicState::DepthBiasEnable, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE_EXT},
            {(T)DynamicState::PrimitiveRestartEnable, VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE},
            {(T)DynamicState::DiscardRectangle, VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT},
            {(T)DynamicState::SampleLocations, VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT},
            {(T)DynamicState::RayTracingPipelineStackSize, VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR},
            {(T)DynamicState::FragmentShadingRateKHR, VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR},
            {(T)DynamicState::LineStipple, VK_DYNAMIC_STATE_LINE_STIPPLE_EXT},
            {(T)DynamicState::VertexInput, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT},
            {(T)DynamicState::PatchControlPoints, VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT},
            {(T)DynamicState::LogicOp, VK_DYNAMIC_STATE_LOGIC_OP_EXT},
            {(T)DynamicState::ColorWriteEnable, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT}};

        return s_translator[(T)state];
    }

    template <>
    VkDescriptorType Translate(DescriptorType type)
    {
        using T = std::underlying_type_t<DescriptorType>;
        static std::map<T, VkDescriptorType> s_translator{
            {(T)DescriptorType::Sampler, VK_DESCRIPTOR_TYPE_SAMPLER},
            {(T)DescriptorType::CombinedImageSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
            {(T)DescriptorType::SampledImage, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
            {(T)DescriptorType::StorageImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
            {(T)DescriptorType::UniformTexelBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER},
            {(T)DescriptorType::StorageTexelBuffer, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER},
            {(T)DescriptorType::UniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
            {(T)DescriptorType::StorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
            {(T)DescriptorType::UniformBufferDynamic, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
            {(T)DescriptorType::StorageBufferDynamic, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC},
            {(T)DescriptorType::InputAttachment, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT},
            {(T)DescriptorType::InlineUniformBlock, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK},
            {(T)DescriptorType::AccelerationStructure, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}};

        return s_translator[(T)type];
    }

    template <>
    VkCullModeFlags Translate(CullMode mode)
    {
        using T = std::underlying_type_t<CullMode>;
        static std::map<T, VkCullModeFlags> s_translator{
            {(T)CullMode::None, VK_CULL_MODE_NONE},
            {(T)CullMode::Front, VK_CULL_MODE_FRONT_BIT},
            {(T)CullMode::Back, VK_CULL_MODE_BACK_BIT},
            {(T)CullMode::FrontAndBack, VK_CULL_MODE_FRONT_AND_BACK}};

        return s_translator[(T)mode];
    }

    template <>
    VkObjectType Translate(ObjectType type)
    {
        using T = std::underlying_type_t<ObjectType>;
        static std::map<T, VkObjectType> s_translator{
            {(T)ObjectType::Unknown, VK_OBJECT_TYPE_UNKNOWN},
            {(T)ObjectType::Instance, VK_OBJECT_TYPE_INSTANCE},
            {(T)ObjectType::PhysicalDevice, VK_OBJECT_TYPE_PHYSICAL_DEVICE},
            {(T)ObjectType::Device, VK_OBJECT_TYPE_DEVICE},
            {(T)ObjectType::Queue, VK_OBJECT_TYPE_QUEUE},
            {(T)ObjectType::Semaphore, VK_OBJECT_TYPE_SEMAPHORE},
            {(T)ObjectType::CommandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER},
            {(T)ObjectType::Fence, VK_OBJECT_TYPE_FENCE},
            {(T)ObjectType::DeviceMemory, VK_OBJECT_TYPE_DEVICE_MEMORY},
            {(T)ObjectType::Buffer, VK_OBJECT_TYPE_BUFFER},
            {(T)ObjectType::Image, VK_OBJECT_TYPE_IMAGE},
            {(T)ObjectType::Event, VK_OBJECT_TYPE_EVENT},
            {(T)ObjectType::QueryPool, VK_OBJECT_TYPE_QUERY_POOL},
            {(T)ObjectType::BufferView, VK_OBJECT_TYPE_BUFFER_VIEW},
            {(T)ObjectType::ImageView, VK_OBJECT_TYPE_IMAGE_VIEW},
            {(T)ObjectType::ShaderModule, VK_OBJECT_TYPE_SHADER_MODULE},
            {(T)ObjectType::PipelineCache, VK_OBJECT_TYPE_PIPELINE_CACHE},
            {(T)ObjectType::PipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT},
            {(T)ObjectType::RenderPass, VK_OBJECT_TYPE_RENDER_PASS},
            {(T)ObjectType::Pipeline, VK_OBJECT_TYPE_PIPELINE},
            {(T)ObjectType::DescriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT},
            {(T)ObjectType::Sampler, VK_OBJECT_TYPE_SAMPLER},
            {(T)ObjectType::DescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL},
            {(T)ObjectType::DescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET},
            {(T)ObjectType::Framebuffer, VK_OBJECT_TYPE_FRAMEBUFFER},
            {(T)ObjectType::CommandPool, VK_OBJECT_TYPE_COMMAND_POOL},
            {(T)ObjectType::SamplerYcbcrConversion, VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION},
            {(T)ObjectType::DescriptorUpdateTemplate, VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE},
            {(T)ObjectType::PrivateDataSlot, VK_OBJECT_TYPE_PRIVATE_DATA_SLOT},
            {(T)ObjectType::Surface, VK_OBJECT_TYPE_SURFACE_KHR},
            {(T)ObjectType::Swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR},
            {(T)ObjectType::Display, VK_OBJECT_TYPE_DISPLAY_KHR},
            {(T)ObjectType::DisplayMode, VK_OBJECT_TYPE_DISPLAY_MODE_KHR},
            {(T)ObjectType::DebugReportCallback, VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT},
            {(T)ObjectType::DebugUtilsMessenger, VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT},
            {(T)ObjectType::AccelerationStructure, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR},
            {(T)ObjectType::ValidationCache, VK_OBJECT_TYPE_VALIDATION_CACHE_EXT},
            {(T)ObjectType::DeferredOperation, VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR}};

        return s_translator[(T)type];
    }

    template <>
    VkVertexInputRate Translate(VertexInputRate rate)
    {
        using T = std::underlying_type_t<VertexInputRate>;
        static std::map<T, VkVertexInputRate> s_translator{
            {(T)VertexInputRate::Vertex, VK_VERTEX_INPUT_RATE_VERTEX},
            {(T)VertexInputRate::Instance, VK_VERTEX_INPUT_RATE_INSTANCE}};

        return s_translator[(T)rate];
    }

    template <>
    VkAttachmentDescriptionFlags Translate(AttachmentDescriptionFlags flags)
    {
        using T = AttachmentDescriptionFlags::Type;
        static std::map<T, VkAttachmentDescriptionFlags> s_translator{
            {(T)AttachmentDescription::MayAlias, VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkAttachmentDescriptionFlags Translate(AttachmentDescription flag)
    {
        return Translate<VkAttachmentDescriptionFlags, AttachmentDescriptionFlags>(flag);
    }

    template <>
    VkCommandPoolCreateFlags Translate(CommandPoolCreateFlags flags)
    {
        using T = CommandPoolCreateFlags::Type;
        static std::map<T, VkCommandPoolCreateFlags> s_translator{
            {(T)CommandPoolCreate::TransientBit, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT},
            {(T)CommandPoolCreate::ResetCommandBuffer, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT},
            {(T)CommandPoolCreate::Protected, VK_COMMAND_POOL_CREATE_PROTECTED_BIT}};

        return GetFlags(flags.Value(), s_translator);
    }

    template <>
    VkCommandPoolCreateFlags Translate(CommandPoolCreate flag)
    {
        return Translate<VkCommandPoolCreateFlags, CommandPoolCreateFlags>(flag);
    }
}
