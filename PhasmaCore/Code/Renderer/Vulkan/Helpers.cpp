#ifdef PE_VULKAN
namespace pe
{
    bool IsDepthAndStencil(Format format)
    {
        switch (format)
        {
        case Format::D24UnormS8UInt:
        case Format::D32SFloatS8UInt:
            return true;
        default:
            return false;
        }
    }

    bool IsDepthOnly(Format format)
    {
        switch (format)
        {
        case Format::D32SFloat:
            return true;
        default:
            return false;
        }
    }

    bool IsStencilOnly(Format format)
    {
        switch (format)
        {
        case Format::S8UInt:
            return true;
        default:
            return false;
        }
    }

    bool HasDepth(Format format)
    {
        return IsDepthOnly(format) || IsDepthAndStencil(format);
    }

    bool HasStencil(Format format)
    {
        return IsStencilOnly(format) || IsDepthAndStencil(format);
    }

    ImageAspectFlags GetAspectMask(Format format)
    {
        ImageAspectFlags flags{};

        if (HasDepth(format))
            flags |= ImageAspect::DepthBit;

        if (HasStencil(format))
            flags |= ImageAspect::StencilBit;

        if (!flags)
            flags = ImageAspect::ColorBit;

        return flags;
    }

    template <>
    VkOffset3D Translate(const Offset3D &value)
    {
        VkOffset3D vkOffset;
        vkOffset.x = value.x;
        vkOffset.y = value.y;
        vkOffset.z = value.z;
        return vkOffset;
    }

    namespace
    {
        std::unordered_map<uint64_t, Format> s_translatorPeFormat{
            {VK_FORMAT_UNDEFINED, Format::Undefined},
            {VK_FORMAT_R8_SINT, Format::R8SInt},
            {VK_FORMAT_R8_UINT, Format::R8UInt},
            {VK_FORMAT_R8_UNORM, Format::R8Unorm},
            {VK_FORMAT_R8G8_UNORM, Format::RG8Unorm},
            {VK_FORMAT_R8G8B8_UNORM, Format::RGB8Unorm},
            {VK_FORMAT_R8G8B8A8_UNORM, Format::RGBA8Unorm},
            {VK_FORMAT_R16_SINT, Format::R16SInt},
            {VK_FORMAT_R16_UINT, Format::R16UInt},
            {VK_FORMAT_R16_SFLOAT, Format::R16SFloat},
            {VK_FORMAT_R16_UNORM, Format::R16Unorm},
            {VK_FORMAT_R16G16_SFLOAT, Format::RG16SFloat},
            {VK_FORMAT_R16G16B16_SINT, Format::RGB16SInt},
            {VK_FORMAT_R16G16B16_UINT, Format::RGB16UInt},
            {VK_FORMAT_R16G16B16_SFLOAT, Format::RGB16SFloat},
            {VK_FORMAT_R16G16B16A16_UINT, Format::RGBA16UInt},
            {VK_FORMAT_R16G16B16A16_SFLOAT, Format::RGBA16SFloat},
            {VK_FORMAT_R32_SINT, Format::R32SInt},
            {VK_FORMAT_R32_UINT, Format::R32UInt},
            {VK_FORMAT_R32_SFLOAT, Format::R32SFloat},
            {VK_FORMAT_R32G32_SINT, Format::RG32SInt},
            {VK_FORMAT_R32G32_UINT, Format::RG32UInt},
            {VK_FORMAT_R32G32_SFLOAT, Format::RG32SFloat},
            {VK_FORMAT_R32G32B32_SINT, Format::RGB32SInt},
            {VK_FORMAT_R32G32B32_UINT, Format::RGB32UInt},
            {VK_FORMAT_R32G32B32_SFLOAT, Format::RGB32SFloat},
            {VK_FORMAT_R32G32B32A32_SINT, Format::RGBA32SInt},
            {VK_FORMAT_R32G32B32A32_UINT, Format::RGBA32UInt},
            {VK_FORMAT_R32G32B32A32_SFLOAT, Format::RGBA32SFloat},
            {VK_FORMAT_B8G8R8A8_UNORM, Format::BGRA8Unorm},
            {VK_FORMAT_D24_UNORM_S8_UINT, Format::D24UnormS8UInt},
            {VK_FORMAT_D32_SFLOAT, Format::D32SFloat},
            {VK_FORMAT_D32_SFLOAT_S8_UINT, Format::D32SFloatS8UInt},
            {VK_FORMAT_S8_UINT, Format::S8UInt}};

        std::unordered_map<uint64_t, ColorSpace> s_translatorPeColorSpace{
            {VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, ColorSpace::SrgbNonLinear},
            {VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, ColorSpace::DisplayP3NonLinear},
            {VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, ColorSpace::ExtendedSrgbLinear},
            {VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT, ColorSpace::DisplayP3Linear},
            {VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT, ColorSpace::DciP3NonLinear},
            {VK_COLOR_SPACE_BT709_LINEAR_EXT, ColorSpace::BT709Linear},
            {VK_COLOR_SPACE_BT709_NONLINEAR_EXT, ColorSpace::BT709NonLinear},
            {VK_COLOR_SPACE_BT2020_LINEAR_EXT, ColorSpace::BT2020Linear},
            {VK_COLOR_SPACE_HDR10_ST2084_EXT, ColorSpace::HDR10St2084},
            {VK_COLOR_SPACE_DOLBYVISION_EXT, ColorSpace::DolbyVision},
            {VK_COLOR_SPACE_HDR10_HLG_EXT, ColorSpace::HDR10Hlg},
            {VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT, ColorSpace::AdobeRgbLinear},
            {VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT, ColorSpace::AdobeRgbNonLinear},
            {VK_COLOR_SPACE_PASS_THROUGH_EXT, ColorSpace::PassThrough},
            {VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, ColorSpace::ExtendedSrgbNonLinear}};

        std::unordered_map<uint64_t, PresentMode> s_translatorPePresentMode{
            {VK_PRESENT_MODE_IMMEDIATE_KHR, PresentMode::Immediate},
            {VK_PRESENT_MODE_MAILBOX_KHR, PresentMode::Mailbox},
            {VK_PRESENT_MODE_FIFO_KHR, PresentMode::Fifo},
            {VK_PRESENT_MODE_FIFO_RELAXED_KHR, PresentMode::FifoRelaxed}};

        uint64_t s_translatorImageLayout[]{
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PREINITIALIZED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
            VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT,
            VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR};

        uint64_t s_translatorShaderStage[]{
            VK_SHADER_STAGE_VERTEX_BIT,
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            VK_SHADER_STAGE_GEOMETRY_BIT,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            VK_SHADER_STAGE_COMPUTE_BIT,
            VK_SHADER_STAGE_ALL_GRAPHICS,
            VK_SHADER_STAGE_ALL,
            VK_SHADER_STAGE_RAYGEN_BIT_NV,
            VK_SHADER_STAGE_ANY_HIT_BIT_NV,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV,
            VK_SHADER_STAGE_MISS_BIT_NV,
            VK_SHADER_STAGE_INTERSECTION_BIT_NV,
            VK_SHADER_STAGE_CALLABLE_BIT_NV};

        uint64_t s_translatorFilter[]{
            VK_FILTER_NEAREST,
            VK_FILTER_LINEAR,
            VK_FILTER_CUBIC_IMG};

        uint64_t s_translatorImageAspect[]{
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_ASPECT_STENCIL_BIT};

        uint64_t s_translatorSamplerMipmapMode[]{
            VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_MIPMAP_MODE_LINEAR};

        uint64_t s_translatorSamplerAddressMode[]{
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE};

        uint64_t s_translatorCompareOp[]{
            VK_COMPARE_OP_NEVER,
            VK_COMPARE_OP_LESS,
            VK_COMPARE_OP_EQUAL,
            VK_COMPARE_OP_LESS_OR_EQUAL,
            VK_COMPARE_OP_GREATER,
            VK_COMPARE_OP_NOT_EQUAL,
            VK_COMPARE_OP_GREATER_OR_EQUAL,
            VK_COMPARE_OP_ALWAYS};

        uint64_t s_translatorBorderColor[]{
            VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            VK_BORDER_COLOR_INT_OPAQUE_WHITE};

        uint64_t s_translatorImageType[]{
            VK_IMAGE_TYPE_1D,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TYPE_3D};

        uint64_t s_translatorImageViewType[]{
            VK_IMAGE_VIEW_TYPE_1D,
            VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_VIEW_TYPE_3D,
            VK_IMAGE_VIEW_TYPE_CUBE,
            VK_IMAGE_VIEW_TYPE_1D_ARRAY,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            VK_IMAGE_VIEW_TYPE_CUBE_ARRAY};

        uint64_t s_translatorSampleCount[]{
            VK_SAMPLE_COUNT_1_BIT,
            VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_64_BIT};

        uint64_t s_translatorFormat[]{
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R8_SINT,
            VK_FORMAT_R8_UINT,
            VK_FORMAT_R8_UNORM,
            VK_FORMAT_R8G8_UNORM,
            VK_FORMAT_R8G8B8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16_SINT,
            VK_FORMAT_R16_UINT,
            VK_FORMAT_R16_SFLOAT,
            VK_FORMAT_R16_UNORM,
            VK_FORMAT_R16G16_SFLOAT,
            VK_FORMAT_R16G16B16_SINT,
            VK_FORMAT_R16G16B16_UINT,
            VK_FORMAT_R16G16B16_SFLOAT,
            VK_FORMAT_R16G16B16A16_UINT,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R32_SINT,
            VK_FORMAT_R32_UINT,
            VK_FORMAT_R32_SFLOAT,
            VK_FORMAT_R32G32_SINT,
            VK_FORMAT_R32G32_UINT,
            VK_FORMAT_R32G32_SFLOAT,
            VK_FORMAT_R32G32B32_SINT,
            VK_FORMAT_R32G32B32_UINT,
            VK_FORMAT_R32G32B32_SFLOAT,
            VK_FORMAT_R32G32B32A32_SINT,
            VK_FORMAT_R32G32B32A32_UINT,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_S8_UINT};

        uint64_t s_translatorImageTiling[] = {
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_TILING_LINEAR};

        uint64_t s_translatorImageUsageFlags[] = {
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT};

        uint64_t s_translatorSharingMode[] = {
            VK_SHARING_MODE_EXCLUSIVE,
            VK_SHARING_MODE_CONCURRENT};

        uint64_t s_translatorImageCreateFlags[] = {
            VK_IMAGE_CREATE_SPARSE_BINDING_BIT,
            VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
            VK_IMAGE_CREATE_SPARSE_ALIASED_BIT,
            VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_CREATE_ALIAS_BIT,
            VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT,
            VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT,
            VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT,
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
            VK_IMAGE_CREATE_PROTECTED_BIT,
            VK_IMAGE_CREATE_DISJOINT_BIT,
            VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV,
            VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT,
            VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT,
            VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT};

        uint64_t s_translatorCommandBufferUsageFlags[] = {
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};

        uint64_t s_translatorBufferUsageFlags[]{
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT,
            VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT,
            VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR};

        uint64_t s_translatorColorSpace[]{
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
            VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
            VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT,
            VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT,
            VK_COLOR_SPACE_BT709_LINEAR_EXT,
            VK_COLOR_SPACE_BT709_NONLINEAR_EXT,
            VK_COLOR_SPACE_BT2020_LINEAR_EXT,
            VK_COLOR_SPACE_HDR10_ST2084_EXT,
            VK_COLOR_SPACE_DOLBYVISION_EXT,
            VK_COLOR_SPACE_HDR10_HLG_EXT,
            VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT,
            VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT,
            VK_COLOR_SPACE_PASS_THROUGH_EXT,
            VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT};

        uint64_t s_translatorPresentMode[]{
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
            VK_PRESENT_MODE_FIFO_RELAXED_KHR};

        uint64_t s_translatorPipelineStageFlags[]{
            VK_PIPELINE_STAGE_2_NONE,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT,
            VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT,
            VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};

        uint64_t s_translatorAttachmentLoadOp[]{
            VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE};

        uint64_t s_translatorAttachmentStoreOp[]{
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE};

        uint64_t s_translatorAccessFlags[]{
            VK_ACCESS_2_NONE,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            VK_ACCESS_2_INDEX_READ_BIT,
            VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            VK_ACCESS_2_UNIFORM_READ_BIT,
            VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_ACCESS_2_HOST_READ_BIT,
            VK_ACCESS_2_HOST_WRITE_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT,
            VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        };

        uint64_t s_translatorAllocationCreateFlags[]{
            0,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT,
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT,
            VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_DONT_BIND_BIT,
            VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
            VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT,
            VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT,
            VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT};

        uint64_t s_translatorBlendFactor[]{
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_SRC_COLOR,
            VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
            VK_BLEND_FACTOR_DST_COLOR,
            VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
            VK_BLEND_FACTOR_SRC_ALPHA,
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            VK_BLEND_FACTOR_DST_ALPHA,
            VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
            VK_BLEND_FACTOR_CONSTANT_COLOR,
            VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
            VK_BLEND_FACTOR_CONSTANT_ALPHA,
            VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
            VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
            VK_BLEND_FACTOR_SRC1_COLOR,
            VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
            VK_BLEND_FACTOR_SRC1_ALPHA,
            VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA};

        uint64_t s_translatorBlendOp[]{
            VK_BLEND_OP_ADD,
            VK_BLEND_OP_SUBTRACT,
            VK_BLEND_OP_REVERSE_SUBTRACT,
            VK_BLEND_OP_MIN,
            VK_BLEND_OP_MAX,
            VK_BLEND_OP_ZERO_EXT,
            VK_BLEND_OP_SRC_EXT,
            VK_BLEND_OP_DST_EXT,
            VK_BLEND_OP_SRC_OVER_EXT,
            VK_BLEND_OP_DST_OVER_EXT,
            VK_BLEND_OP_SRC_IN_EXT,
            VK_BLEND_OP_DST_IN_EXT,
            VK_BLEND_OP_SRC_OUT_EXT,
            VK_BLEND_OP_DST_OUT_EXT,
            VK_BLEND_OP_SRC_ATOP_EXT,
            VK_BLEND_OP_DST_ATOP_EXT,
            VK_BLEND_OP_XOR_EXT,
            VK_BLEND_OP_MULTIPLY_EXT,
            VK_BLEND_OP_SCREEN_EXT,
            VK_BLEND_OP_OVERLAY_EXT,
            VK_BLEND_OP_DARKEN_EXT,
            VK_BLEND_OP_LIGHTEN_EXT,
            VK_BLEND_OP_COLORDODGE_EXT,
            VK_BLEND_OP_COLORBURN_EXT,
            VK_BLEND_OP_HARDLIGHT_EXT,
            VK_BLEND_OP_SOFTLIGHT_EXT,
            VK_BLEND_OP_DIFFERENCE_EXT,
            VK_BLEND_OP_EXCLUSION_EXT,
            VK_BLEND_OP_INVERT_EXT,
            VK_BLEND_OP_INVERT_RGB_EXT,
            VK_BLEND_OP_LINEARDODGE_EXT,
            VK_BLEND_OP_LINEARBURN_EXT,
            VK_BLEND_OP_VIVIDLIGHT_EXT,
            VK_BLEND_OP_LINEARLIGHT_EXT,
            VK_BLEND_OP_PINLIGHT_EXT,
            VK_BLEND_OP_HARDMIX_EXT,
            VK_BLEND_OP_HSL_HUE_EXT,
            VK_BLEND_OP_HSL_SATURATION_EXT,
            VK_BLEND_OP_HSL_COLOR_EXT,
            VK_BLEND_OP_HSL_LUMINOSITY_EXT,
            VK_BLEND_OP_PLUS_EXT,
            VK_BLEND_OP_PLUS_CLAMPED_EXT,
            VK_BLEND_OP_PLUS_CLAMPED_ALPHA_EXT,
            VK_BLEND_OP_PLUS_DARKER_EXT,
            VK_BLEND_OP_MINUS_EXT,
            VK_BLEND_OP_MINUS_CLAMPED_EXT,
            VK_BLEND_OP_CONTRAST_EXT,
            VK_BLEND_OP_INVERT_OVG_EXT,
            VK_BLEND_OP_RED_EXT,
            VK_BLEND_OP_GREEN_EXT,
            VK_BLEND_OP_BLUE_EXT};

        uint64_t s_translatorColorComponentFlags[]{
            VK_COLOR_COMPONENT_R_BIT,
            VK_COLOR_COMPONENT_G_BIT,
            VK_COLOR_COMPONENT_B_BIT,
            VK_COLOR_COMPONENT_A_BIT};

        uint64_t s_translatorDynamicState[]{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_CULL_MODE,
            VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT,
            VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_OP_EXT,
            VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE_EXT,
            VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
            VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT,
            VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT,
            VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR,
            VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR,
            VK_DYNAMIC_STATE_LINE_STIPPLE_EXT,
            VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
            VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT,
            VK_DYNAMIC_STATE_LOGIC_OP_EXT,
            VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT};

        uint64_t s_translatorDescriptorType[]{
            VK_DESCRIPTOR_TYPE_SAMPLER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
            VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR};

        uint64_t s_translatorCullMode[]{
            VK_CULL_MODE_NONE,
            VK_CULL_MODE_FRONT_BIT,
            VK_CULL_MODE_BACK_BIT,
            VK_CULL_MODE_FRONT_AND_BACK};

        uint64_t s_translatorObjectType[]{
            VK_OBJECT_TYPE_UNKNOWN,
            VK_OBJECT_TYPE_INSTANCE,
            VK_OBJECT_TYPE_PHYSICAL_DEVICE,
            VK_OBJECT_TYPE_DEVICE,
            VK_OBJECT_TYPE_QUEUE,
            VK_OBJECT_TYPE_SEMAPHORE,
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            VK_OBJECT_TYPE_FENCE,
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            VK_OBJECT_TYPE_BUFFER,
            VK_OBJECT_TYPE_IMAGE,
            VK_OBJECT_TYPE_EVENT,
            VK_OBJECT_TYPE_QUERY_POOL,
            VK_OBJECT_TYPE_BUFFER_VIEW,
            VK_OBJECT_TYPE_IMAGE_VIEW,
            VK_OBJECT_TYPE_SHADER_MODULE,
            VK_OBJECT_TYPE_PIPELINE_CACHE,
            VK_OBJECT_TYPE_PIPELINE_LAYOUT,
            VK_OBJECT_TYPE_RENDER_PASS,
            VK_OBJECT_TYPE_PIPELINE,
            VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
            VK_OBJECT_TYPE_SAMPLER,
            VK_OBJECT_TYPE_DESCRIPTOR_POOL,
            VK_OBJECT_TYPE_DESCRIPTOR_SET,
            VK_OBJECT_TYPE_FRAMEBUFFER,
            VK_OBJECT_TYPE_COMMAND_POOL,
            VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION,
            VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE,
            VK_OBJECT_TYPE_PRIVATE_DATA_SLOT,
            VK_OBJECT_TYPE_SURFACE_KHR,
            VK_OBJECT_TYPE_SWAPCHAIN_KHR,
            VK_OBJECT_TYPE_DISPLAY_KHR,
            VK_OBJECT_TYPE_DISPLAY_MODE_KHR,
            VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT,
            VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT,
            VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_OBJECT_TYPE_VALIDATION_CACHE_EXT,
            VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR};

        uint64_t s_translatorVertexInputRate[]{
            VK_VERTEX_INPUT_RATE_VERTEX,
            VK_VERTEX_INPUT_RATE_INSTANCE};

        uint64_t s_translatorAttachmentDescriptionFlags[]{
            VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT};

        uint64_t s_translatorCommandPoolCreateFlags[]{
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            VK_COMMAND_POOL_CREATE_PROTECTED_BIT};

        uint64_t s_translatorPrimitiveTopology[]{
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
            VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
            VK_PRIMITIVE_TOPOLOGY_PATCH_LIST};

        uint64_t s_translatorPolygonMode[]{
            VK_POLYGON_MODE_FILL,
            VK_POLYGON_MODE_LINE,
            VK_POLYGON_MODE_POINT};

        uint64_t s_translatorStencilOp[]{
            VK_STENCIL_OP_KEEP,
            VK_STENCIL_OP_ZERO,
            VK_STENCIL_OP_REPLACE,
            VK_STENCIL_OP_INCREMENT_AND_CLAMP,
            VK_STENCIL_OP_DECREMENT_AND_CLAMP,
            VK_STENCIL_OP_INVERT,
            VK_STENCIL_OP_INCREMENT_AND_WRAP,
            VK_STENCIL_OP_DECREMENT_AND_WRAP};
    } // namespace

    template <>
    Format Translate(VkFormat format) { return s_translatorPeFormat[(uint64_t)format]; }
    template <>
    ColorSpace Translate(VkColorSpaceKHR value) { return s_translatorPeColorSpace[(uint64_t)value]; }
    template <>
    PresentMode Translate(VkPresentModeKHR value) { return s_translatorPePresentMode[(uint64_t)value]; }
    template <>
    VkImageLayout Translate(ImageLayout value) { return (VkImageLayout)s_translatorImageLayout[(uint64_t)value]; }
    template <>
    VkShaderStageFlags Translate(ShaderStageFlags value) { return (VkShaderStageFlags)GetFlags(value.Value(), s_translatorShaderStage); }
    template <>
    VkShaderStageFlags Translate(ShaderStage value) { return Translate<VkShaderStageFlags, ShaderStageFlags>(value); }
    template <>
    VkFilter Translate(Filter value) { return (VkFilter)s_translatorFilter[(uint64_t)value]; }
    template <>
    VkImageAspectFlags Translate(ImageAspectFlags value) { return (VkImageAspectFlags)GetFlags(value.Value(), s_translatorImageAspect); }
    template <>
    VkImageAspectFlags Translate(ImageAspect value) { return Translate<VkImageAspectFlags, ImageAspectFlags>(value); }
    template <>
    VkSamplerMipmapMode Translate(SamplerMipmapMode value) { return (VkSamplerMipmapMode)s_translatorSamplerMipmapMode[(uint64_t)value]; }
    template <>
    VkSamplerAddressMode Translate(SamplerAddressMode value) { return (VkSamplerAddressMode)s_translatorSamplerAddressMode[(uint64_t)value]; }
    template <>
    VkCompareOp Translate(CompareOp value) { return (VkCompareOp)s_translatorCompareOp[(uint64_t)value]; }
    template <>
    VkBorderColor Translate(BorderColor value) { return (VkBorderColor)s_translatorBorderColor[(uint64_t)value]; }
    template <>
    VkImageType Translate(ImageType value) { return (VkImageType)s_translatorImageType[(uint64_t)value]; }
    template <>
    VkImageViewType Translate(ImageViewType value) { return (VkImageViewType)s_translatorImageViewType[(uint64_t)value]; }
    template <>
    VkSampleCountFlagBits Translate(SampleCount value) { return (VkSampleCountFlagBits)s_translatorSampleCount[(uint64_t)value]; }
    template <>
    VkFormat Translate(Format value) { return (VkFormat)s_translatorFormat[(uint64_t)value]; }
    template <>
    VkImageTiling Translate(ImageTiling value) { return (VkImageTiling)s_translatorImageTiling[(uint64_t)value]; }
    template <>
    VkImageUsageFlags Translate(ImageUsageFlags value) { return (VkImageUsageFlags)GetFlags(value.Value(), s_translatorImageUsageFlags); }
    template <>
    VkImageUsageFlags Translate(ImageUsage value) { return Translate<VkImageUsageFlags, ImageUsageFlags>(value); }
    template <>
    VkSharingMode Translate(SharingMode value) { return (VkSharingMode)s_translatorSharingMode[(uint64_t)value]; }
    template <>
    VkImageCreateFlags Translate(ImageCreateFlags value) { return (VkImageCreateFlags)GetFlags(value.Value(), s_translatorImageCreateFlags); }
    template <>
    VkImageCreateFlags Translate(ImageCreate value) { return Translate<VkImageCreateFlags, ImageCreateFlags>(value); }
    template <>
    VkCommandBufferUsageFlags Translate(CommandBufferUsageFlags value) { return (VkCommandBufferUsageFlags)GetFlags(value.Value(), s_translatorCommandBufferUsageFlags); }
    template <>
    VkCommandBufferUsageFlags Translate(CommandBufferUsage value) { return Translate<VkCommandBufferUsageFlags, CommandBufferUsageFlags>(value); }
    template <>
    VkBufferUsageFlags Translate(BufferUsageFlags value) { return (VkBufferUsageFlags)GetFlags(value.Value(), s_translatorBufferUsageFlags); }
    template <>
    VkBufferUsageFlags Translate(BufferUsage value) { return Translate<VkBufferUsageFlags, BufferUsageFlags>(value); }
    template <>
    VkColorSpaceKHR Translate(ColorSpace value) { return (VkColorSpaceKHR)s_translatorColorSpace[(uint64_t)value]; }
    template <>
    VkPresentModeKHR Translate(PresentMode value) { return (VkPresentModeKHR)s_translatorPresentMode[(uint64_t)value]; }
    template <>
    VkPipelineStageFlags2 Translate(PipelineStageFlags value) { return (VkPipelineStageFlags2)GetFlags(value.Value(), s_translatorPipelineStageFlags); }
    template <>
    VkPipelineStageFlags2 Translate(PipelineStage value) { return Translate<VkPipelineStageFlags2, PipelineStageFlags>(value); }
    template <>
    VkAttachmentLoadOp Translate(AttachmentLoadOp value) { return (VkAttachmentLoadOp)s_translatorAttachmentLoadOp[(uint64_t)value]; }
    template <>
    VkAttachmentStoreOp Translate(AttachmentStoreOp value) { return (VkAttachmentStoreOp)s_translatorAttachmentStoreOp[(uint64_t)value]; }
    template <>
    VkAccessFlags2 Translate(AccessFlags value) { return (VkAccessFlags2)GetFlags(value.Value(), s_translatorAccessFlags); }
    template <>
    VkAccessFlags2 Translate(Access value) { return Translate<VkAccessFlags2, AccessFlags>(value); }
    template <>
    VmaAllocationCreateFlags Translate(AllocationCreateFlags value) { return (VmaAllocationCreateFlags)GetFlags(value.Value(), s_translatorAllocationCreateFlags); }
    template <>
    VmaAllocationCreateFlags Translate(AllocationCreate value) { return Translate<VmaAllocationCreateFlags, AllocationCreateFlags>(value); }
    template <>
    VkBlendFactor Translate(BlendFactor value) { return (VkBlendFactor)s_translatorBlendFactor[(uint64_t)value]; }
    template <>
    VkBlendOp Translate(BlendOp value) { return (VkBlendOp)s_translatorBlendOp[(uint64_t)value]; }
    template <>
    VkColorComponentFlags Translate(ColorComponentFlags value) { return (VkColorComponentFlags)GetFlags(value.Value(), s_translatorColorComponentFlags); }
    template <>
    VkColorComponentFlags Translate(ColorComponent value) { return Translate<VkColorComponentFlags, ColorComponentFlags>(value); }
    template <>
    VkDynamicState Translate(DynamicState value) { return (VkDynamicState)s_translatorDynamicState[(uint64_t)value]; }
    template <>
    VkDescriptorType Translate(DescriptorType value) { return (VkDescriptorType)s_translatorDescriptorType[(uint64_t)value]; }
    template <>
    VkCullModeFlags Translate(CullMode value) { return (VkCullModeFlags)s_translatorCullMode[(uint64_t)value]; }
    template <>
    VkObjectType Translate(ObjectType value) { return (VkObjectType)s_translatorObjectType[(uint64_t)value]; }
    template <>
    VkVertexInputRate Translate(VertexInputRate value) { return (VkVertexInputRate)s_translatorVertexInputRate[(uint64_t)value]; }
    template <>
    VkAttachmentDescriptionFlags Translate(AttachmentDescriptionFlags value) { return (VkAttachmentDescriptionFlags)GetFlags(value.Value(), s_translatorAttachmentDescriptionFlags); }
    template <>
    VkAttachmentDescriptionFlags Translate(AttachmentDescription value) { return Translate<VkAttachmentDescriptionFlags, AttachmentDescriptionFlags>(value); }
    template <>
    VkCommandPoolCreateFlags Translate(CommandPoolCreateFlags value) { return (VkCommandPoolCreateFlags)GetFlags(value.Value(), s_translatorCommandPoolCreateFlags); }
    template <>
    VkCommandPoolCreateFlags Translate(CommandPoolCreate value) { return Translate<VkCommandPoolCreateFlags, CommandPoolCreateFlags>(value); }
    template <>
    VkPrimitiveTopology Translate(PrimitiveTopology value) { return (VkPrimitiveTopology)s_translatorPrimitiveTopology[(uint64_t)value]; }
    template <>
    VkPolygonMode Translate(PolygonMode value) { return (VkPolygonMode)s_translatorPolygonMode[(uint64_t)value]; }
    template <>
    VkStencilOp Translate(StencilOp value) { return (VkStencilOp)s_translatorStencilOp[(uint64_t)value]; }
}
#endif
