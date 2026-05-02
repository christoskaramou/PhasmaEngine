#pragma once

enum PeGraphicsApi : uint32_t
{
    PE_GRAPHICS_API_VULKAN = 0,
    PE_GRAPHICS_API_DX12,
    PE_GRAPHICS_API_COUNT
};

enum PeFormat : uint32_t
{
    PE_FORMAT_UNDEFINED = 0,

    // Single channel
    PE_FORMAT_R8_UNORM,
    PE_FORMAT_R8_SINT,
    PE_FORMAT_R8_UINT,
    PE_FORMAT_R16_SFLOAT,
    PE_FORMAT_R16_SINT,
    PE_FORMAT_R16_UINT,
    PE_FORMAT_R32_SFLOAT,
    PE_FORMAT_R32_SINT,
    PE_FORMAT_R32_UINT,

    // Two channel
    PE_FORMAT_R16G16_SFLOAT,
    PE_FORMAT_R32G32_SFLOAT,
    PE_FORMAT_R32G32_SINT,
    PE_FORMAT_R32G32_UINT,

    // Three channel
    PE_FORMAT_R16G16B16_SFLOAT,
    PE_FORMAT_R16G16B16_SINT,
    PE_FORMAT_R16G16B16_UINT,
    PE_FORMAT_R32G32B32_SFLOAT,
    PE_FORMAT_R32G32B32_SINT,
    PE_FORMAT_R32G32B32_UINT,

    // Four channel 8-bit
    PE_FORMAT_R8G8B8A8_UNORM,
    PE_FORMAT_R8G8B8A8_SRGB,
    PE_FORMAT_B8G8R8A8_UNORM,
    PE_FORMAT_B8G8R8A8_SRGB,

    // Four channel 16/32-bit
    PE_FORMAT_R16G16B16A16_SFLOAT,
    PE_FORMAT_R32G32B32A32_SFLOAT,
    PE_FORMAT_R32G32B32A32_SINT,
    PE_FORMAT_R32G32B32A32_UINT,

    // Packed
    PE_FORMAT_A2B10G10R10_UNORM_PACK32,
    PE_FORMAT_B10G11R11_UFLOAT_PACK32,

    // Depth / stencil
    PE_FORMAT_D32_SFLOAT,
    PE_FORMAT_D24_UNORM_S8_UINT,
    PE_FORMAT_D32_SFLOAT_S8_UINT,
    PE_FORMAT_S8_UINT,

    // BC compressed
    PE_FORMAT_BC1_RGBA_UNORM,
    PE_FORMAT_BC1_RGBA_SRGB,
    PE_FORMAT_BC2_UNORM,
    PE_FORMAT_BC2_SRGB,
    PE_FORMAT_BC3_UNORM,
    PE_FORMAT_BC3_SRGB,
    PE_FORMAT_BC4_UNORM,
    PE_FORMAT_BC4_SNORM,
    PE_FORMAT_BC5_UNORM,
    PE_FORMAT_BC5_SNORM,
    PE_FORMAT_BC6H_UFLOAT,
    PE_FORMAT_BC6H_SFLOAT,
    PE_FORMAT_BC7_UNORM,
    PE_FORMAT_BC7_SRGB,

    PE_FORMAT_COUNT
};

enum PeImageLayout : uint32_t
{
    PE_IMAGE_LAYOUT_UNDEFINED = 0,
    PE_IMAGE_LAYOUT_GENERAL,
    PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    PE_IMAGE_LAYOUT_PRESENT_SRC,
    PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, // Vulkan 1.3 unified (color or depth)
    PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
    PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
    PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
    PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
    PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
    PE_IMAGE_LAYOUT_COUNT
};

enum PeLoadOp : uint32_t
{
    PE_LOAD_OP_LOAD = 0,
    PE_LOAD_OP_CLEAR,
    PE_LOAD_OP_DONT_CARE,
    PE_LOAD_OP_COUNT
};

enum PeStoreOp : uint32_t
{
    PE_STORE_OP_STORE = 0,
    PE_STORE_OP_DONT_CARE,
    PE_STORE_OP_COUNT
};

enum PePresentMode : uint32_t
{
    PE_PRESENT_MODE_IMMEDIATE = 0, // no vsync, tearing allowed
    PE_PRESENT_MODE_MAILBOX,       // low-latency triple-buffer
    PE_PRESENT_MODE_FIFO,          // vsync (guaranteed available)
    PE_PRESENT_MODE_FIFO_RELAXED,  // vsync with late-frame tear
    PE_PRESENT_MODE_COUNT
};

enum PeCullMode : uint32_t
{
    PE_CULL_MODE_NONE = 0,
    PE_CULL_MODE_FRONT,
    PE_CULL_MODE_BACK,
    PE_CULL_MODE_FRONT_AND_BACK,
    PE_CULL_MODE_COUNT
};

enum PePolygonMode : uint32_t
{
    PE_POLYGON_MODE_FILL = 0,
    PE_POLYGON_MODE_LINE,
    PE_POLYGON_MODE_POINT,
    PE_POLYGON_MODE_COUNT
};

enum PeTopology : uint32_t
{
    PE_TOPOLOGY_POINT_LIST = 0,
    PE_TOPOLOGY_LINE_LIST,
    PE_TOPOLOGY_LINE_STRIP,
    PE_TOPOLOGY_TRIANGLE_LIST,
    PE_TOPOLOGY_TRIANGLE_STRIP,
    PE_TOPOLOGY_TRIANGLE_FAN,
    PE_TOPOLOGY_PATCH_LIST,
    PE_TOPOLOGY_COUNT
};

enum PeCompareOp : uint32_t
{
    PE_COMPARE_OP_NEVER = 0,
    PE_COMPARE_OP_LESS,
    PE_COMPARE_OP_EQUAL,
    PE_COMPARE_OP_LESS_OR_EQUAL,
    PE_COMPARE_OP_GREATER,
    PE_COMPARE_OP_NOT_EQUAL,
    PE_COMPARE_OP_GREATER_OR_EQUAL,
    PE_COMPARE_OP_ALWAYS,
    PE_COMPARE_OP_COUNT
};

enum PeStencilOp : uint32_t
{
    PE_STENCIL_OP_KEEP = 0,
    PE_STENCIL_OP_ZERO,
    PE_STENCIL_OP_REPLACE,
    PE_STENCIL_OP_INCREMENT_AND_CLAMP,
    PE_STENCIL_OP_DECREMENT_AND_CLAMP,
    PE_STENCIL_OP_INVERT,
    PE_STENCIL_OP_INCREMENT_AND_WRAP,
    PE_STENCIL_OP_DECREMENT_AND_WRAP,
    PE_STENCIL_OP_COUNT
};

enum PeBlendFactor : uint32_t
{
    PE_BLEND_FACTOR_ZERO = 0,
    PE_BLEND_FACTOR_ONE,
    PE_BLEND_FACTOR_SRC_ALPHA,
    PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    PE_BLEND_FACTOR_COUNT
};

enum PeBlendOp : uint32_t
{
    PE_BLEND_OP_ADD = 0,
    PE_BLEND_OP_SUBTRACT,
    PE_BLEND_OP_REVERSE_SUBTRACT,
    PE_BLEND_OP_MIN,
    PE_BLEND_OP_MAX,
    PE_BLEND_OP_COUNT
};

using PeColorComponentFlags = uint32_t;
constexpr PeColorComponentFlags PE_COLOR_COMPONENT_R = 1u << 0;
constexpr PeColorComponentFlags PE_COLOR_COMPONENT_G = 1u << 1;
constexpr PeColorComponentFlags PE_COLOR_COMPONENT_B = 1u << 2;
constexpr PeColorComponentFlags PE_COLOR_COMPONENT_A = 1u << 3;
constexpr PeColorComponentFlags PE_COLOR_COMPONENT_RGBA =
    PE_COLOR_COMPONENT_R | PE_COLOR_COMPONENT_G |
    PE_COLOR_COMPONENT_B | PE_COLOR_COMPONENT_A;

struct PeBlendAttachmentState
{
    bool blendEnable = false;
    PeBlendFactor srcColorBlendFactor = PE_BLEND_FACTOR_ONE;
    PeBlendFactor dstColorBlendFactor = PE_BLEND_FACTOR_ZERO;
    PeBlendOp colorBlendOp = PE_BLEND_OP_ADD;
    PeBlendFactor srcAlphaBlendFactor = PE_BLEND_FACTOR_ONE;
    PeBlendFactor dstAlphaBlendFactor = PE_BLEND_FACTOR_ZERO;
    PeBlendOp alphaBlendOp = PE_BLEND_OP_ADD;
    PeColorComponentFlags colorWriteMask = PE_COLOR_COMPONENT_RGBA;
};

enum PeDynamicState : uint32_t
{
    PE_DYNAMIC_STATE_VIEWPORT = 0,
    PE_DYNAMIC_STATE_SCISSOR,
    PE_DYNAMIC_STATE_LINE_WIDTH,
    PE_DYNAMIC_STATE_DEPTH_BIAS,
    PE_DYNAMIC_STATE_BLEND_CONSTANTS,
    PE_DYNAMIC_STATE_DEPTH_BOUNDS,
    PE_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    PE_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    PE_DYNAMIC_STATE_STENCIL_REFERENCE,
    PE_DYNAMIC_STATE_CULL_MODE,
    PE_DYNAMIC_STATE_FRONT_FACE,
    PE_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    PE_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    PE_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    PE_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    PE_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
    PE_DYNAMIC_STATE_COUNT
};

enum PeFilter : uint32_t
{
    PE_FILTER_NEAREST = 0,
    PE_FILTER_LINEAR,
    PE_FILTER_COUNT
};

enum PeSamplerAddressMode : uint32_t
{
    PE_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    PE_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    PE_SAMPLER_ADDRESS_MODE_COUNT
};

enum PeSamplerMipmapMode : uint32_t
{
    PE_SAMPLER_MIPMAP_MODE_NEAREST = 0,
    PE_SAMPLER_MIPMAP_MODE_LINEAR,
    PE_SAMPLER_MIPMAP_MODE_COUNT
};

enum PeSamplerBorderColor : uint32_t
{
    PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK = 0,
    PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    PE_SAMPLER_BORDER_COLOR_COUNT
};

enum PeImageViewType : uint32_t
{
    PE_IMAGE_VIEW_TYPE_1D = 0,
    PE_IMAGE_VIEW_TYPE_2D,
    PE_IMAGE_VIEW_TYPE_3D,
    PE_IMAGE_VIEW_TYPE_CUBE,
    PE_IMAGE_VIEW_TYPE_1D_ARRAY,
    PE_IMAGE_VIEW_TYPE_2D_ARRAY,
    PE_IMAGE_VIEW_TYPE_CUBE_ARRAY,
    PE_IMAGE_VIEW_TYPE_COUNT
};

using PeImageAspectFlags = uint32_t;
constexpr PeImageAspectFlags PE_IMAGE_ASPECT_NONE = 0u;
constexpr PeImageAspectFlags PE_IMAGE_ASPECT_COLOR = 1u << 0;
constexpr PeImageAspectFlags PE_IMAGE_ASPECT_DEPTH = 1u << 1;
constexpr PeImageAspectFlags PE_IMAGE_ASPECT_STENCIL = 1u << 2;

enum PeComponentSwizzle : uint32_t
{
    PE_COMPONENT_SWIZZLE_IDENTITY = 0,
    PE_COMPONENT_SWIZZLE_ZERO,
    PE_COMPONENT_SWIZZLE_ONE,
    PE_COMPONENT_SWIZZLE_R,
    PE_COMPONENT_SWIZZLE_G,
    PE_COMPONENT_SWIZZLE_B,
    PE_COMPONENT_SWIZZLE_A,
    PE_COMPONENT_SWIZZLE_COUNT
};

enum PeSampleCount : uint32_t
{
    PE_SAMPLE_COUNT_1 = 0,
    PE_SAMPLE_COUNT_2,
    PE_SAMPLE_COUNT_4,
    PE_SAMPLE_COUNT_8,
    PE_SAMPLE_COUNT_16,
    PE_SAMPLE_COUNT_COUNT
};

enum PeDescriptorType : uint32_t
{
    PE_DESCRIPTOR_TYPE_SAMPLER = 0,
    PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    PE_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
    PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    PE_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
    PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
    PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE,
    PE_DESCRIPTOR_TYPE_COUNT
};

using PeShaderStageFlags = uint32_t;
constexpr PeShaderStageFlags PE_SHADER_STAGE_VERTEX = 1u << 0;
constexpr PeShaderStageFlags PE_SHADER_STAGE_TESS_CONTROL = 1u << 1;
constexpr PeShaderStageFlags PE_SHADER_STAGE_TESS_EVALUATION = 1u << 2;
constexpr PeShaderStageFlags PE_SHADER_STAGE_GEOMETRY = 1u << 3;
constexpr PeShaderStageFlags PE_SHADER_STAGE_FRAGMENT = 1u << 4;
constexpr PeShaderStageFlags PE_SHADER_STAGE_COMPUTE = 1u << 5;
constexpr PeShaderStageFlags PE_SHADER_STAGE_TASK_EXT = 1u << 6;
constexpr PeShaderStageFlags PE_SHADER_STAGE_MESH_EXT = 1u << 7;
constexpr PeShaderStageFlags PE_SHADER_STAGE_RAYGEN_KHR = 1u << 8;
constexpr PeShaderStageFlags PE_SHADER_STAGE_ANY_HIT_KHR = 1u << 9;
constexpr PeShaderStageFlags PE_SHADER_STAGE_CLOSEST_HIT_KHR = 1u << 10;
constexpr PeShaderStageFlags PE_SHADER_STAGE_MISS_KHR = 1u << 11;
constexpr PeShaderStageFlags PE_SHADER_STAGE_INTERSECTION_KHR = 1u << 12;
constexpr PeShaderStageFlags PE_SHADER_STAGE_CALLABLE_KHR = 1u << 13;
constexpr PeShaderStageFlags PE_SHADER_STAGE_ALL = ~0u;
constexpr uint32_t PE_SHADER_STAGE_BIT_COUNT = 14;

using PePipelineStageFlags = uint64_t;
constexpr PePipelineStageFlags PE_STAGE_NONE = 0ULL;
constexpr PePipelineStageFlags PE_STAGE_TOP_OF_PIPE = 1ULL << 0;
constexpr PePipelineStageFlags PE_STAGE_VERTEX_SHADER = 1ULL << 1;
constexpr PePipelineStageFlags PE_STAGE_FRAGMENT_SHADER = 1ULL << 2;
constexpr PePipelineStageFlags PE_STAGE_COMPUTE_SHADER = 1ULL << 3;
constexpr PePipelineStageFlags PE_STAGE_TRANSFER = 1ULL << 4;
constexpr PePipelineStageFlags PE_STAGE_COLOR_ATTACHMENT_OUTPUT = 1ULL << 5;
constexpr PePipelineStageFlags PE_STAGE_EARLY_FRAGMENT_TESTS = 1ULL << 6;
constexpr PePipelineStageFlags PE_STAGE_LATE_FRAGMENT_TESTS = 1ULL << 7;
constexpr PePipelineStageFlags PE_STAGE_VERTEX_INPUT = 1ULL << 8;
constexpr PePipelineStageFlags PE_STAGE_DRAW_INDIRECT = 1ULL << 9;
constexpr PePipelineStageFlags PE_STAGE_ALL_GRAPHICS = 1ULL << 10;
constexpr PePipelineStageFlags PE_STAGE_ALL_COMMANDS = 1ULL << 11;
constexpr PePipelineStageFlags PE_STAGE_BOTTOM_OF_PIPE = 1ULL << 12;
constexpr PePipelineStageFlags PE_STAGE_RAY_TRACING_SHADER_KHR = 1ULL << 13;
constexpr PePipelineStageFlags PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR = 1ULL << 14;
constexpr PePipelineStageFlags PE_STAGE_CLEAR = 1ULL << 15;
constexpr PePipelineStageFlags PE_STAGE_COPY = 1ULL << 16;
constexpr PePipelineStageFlags PE_STAGE_HOST = 1ULL << 17;
constexpr PePipelineStageFlags PE_STAGE_INDEX_INPUT = 1ULL << 18;
constexpr PePipelineStageFlags PE_STAGE_VERTEX_ATTRIBUTE_INPUT = 1ULL << 19;
constexpr uint32_t PE_STAGE_BIT_COUNT = 20;

using PeAccessFlags = uint64_t;
constexpr PeAccessFlags PE_ACCESS_NONE = 0ULL;
constexpr PeAccessFlags PE_ACCESS_SHADER_READ = 1ULL << 0;
constexpr PeAccessFlags PE_ACCESS_SHADER_WRITE = 1ULL << 1;
constexpr PeAccessFlags PE_ACCESS_SHADER_SAMPLED_READ = 1ULL << 2;
constexpr PeAccessFlags PE_ACCESS_COLOR_ATTACHMENT_READ = 1ULL << 3;
constexpr PeAccessFlags PE_ACCESS_COLOR_ATTACHMENT_WRITE = 1ULL << 4;
constexpr PeAccessFlags PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ = 1ULL << 5;
constexpr PeAccessFlags PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE = 1ULL << 6;
constexpr PeAccessFlags PE_ACCESS_TRANSFER_READ = 1ULL << 7;
constexpr PeAccessFlags PE_ACCESS_TRANSFER_WRITE = 1ULL << 8;
constexpr PeAccessFlags PE_ACCESS_INDEX_READ = 1ULL << 9;
constexpr PeAccessFlags PE_ACCESS_VERTEX_ATTRIBUTE_READ = 1ULL << 10;
constexpr PeAccessFlags PE_ACCESS_INDIRECT_COMMAND_READ = 1ULL << 11;
constexpr PeAccessFlags PE_ACCESS_MEMORY_READ = 1ULL << 12;
constexpr PeAccessFlags PE_ACCESS_MEMORY_WRITE = 1ULL << 13;
constexpr PeAccessFlags PE_ACCESS_HOST_WRITE = 1ULL << 14;
constexpr PeAccessFlags PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR = 1ULL << 15;
constexpr PeAccessFlags PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR = 1ULL << 16;
constexpr PeAccessFlags PE_ACCESS_UNIFORM_READ = 1ULL << 17;
constexpr PeAccessFlags PE_ACCESS_SHADER_STORAGE_READ = 1ULL << 18;
constexpr PeAccessFlags PE_ACCESS_SHADER_STORAGE_WRITE = 1ULL << 19;
constexpr uint32_t PE_ACCESS_BIT_COUNT = 20;

// Spec-named aliases (Phase 0 additive). The cherry-picked names above are
// Vulkan-flavored; the Phase 0 design spec refers to these flag groups by
// barrier-centric and binding-centric names. Keep both spellings available so
// downstream pImpl rewrites can pick whichever reads better at the call site.
using PeBarrierSync = PePipelineStageFlags;
using PeBarrierAccess = PeAccessFlags;
using PeBindingType = PeDescriptorType;

inline bool IsReadOnlyAccess(PeAccessFlags accessMask)
{
    constexpr PeAccessFlags kWriteAccessMask =
        PE_ACCESS_SHADER_WRITE |
        PE_ACCESS_SHADER_STORAGE_WRITE |
        PE_ACCESS_COLOR_ATTACHMENT_WRITE |
        PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE |
        PE_ACCESS_TRANSFER_WRITE |
        PE_ACCESS_HOST_WRITE |
        PE_ACCESS_MEMORY_WRITE |
        PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;

    if (accessMask == PE_ACCESS_NONE)
        return false;

    return (accessMask & kWriteAccessMask) == PE_ACCESS_NONE;
}

using PeImageUsageFlags = uint32_t;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_NONE = 0u;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_TRANSFER_SRC = 1u << 0;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_TRANSFER_DST = 1u << 1;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_SAMPLED = 1u << 2;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_STORAGE = 1u << 3;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_COLOR_ATTACHMENT = 1u << 4;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT = 1u << 5;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_INPUT_ATTACHMENT = 1u << 6;
constexpr PeImageUsageFlags PE_IMAGE_USAGE_TRANSIENT_ATTACHMENT = 1u << 7;
constexpr uint32_t PE_IMAGE_USAGE_BIT_COUNT = 8;

enum PeImageType : uint32_t
{
    PE_IMAGE_TYPE_1D = 0,
    PE_IMAGE_TYPE_2D,
    PE_IMAGE_TYPE_3D,
    PE_IMAGE_TYPE_COUNT
};

using PeBufferUsageFlags = uint64_t;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_NONE = 0ULL;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_TRANSFER_SRC = 1ULL << 0;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_TRANSFER_DST = 1ULL << 1;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_UNIFORM_BUFFER = 1ULL << 2;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_STORAGE_BUFFER = 1ULL << 3;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_INDEX_BUFFER = 1ULL << 4;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_VERTEX_BUFFER = 1ULL << 5;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_INDIRECT_BUFFER = 1ULL << 6;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS = 1ULL << 7;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR = 1ULL << 8;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR = 1ULL << 9;
constexpr PeBufferUsageFlags PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR = 1ULL << 10;
constexpr uint32_t PE_BUFFER_USAGE_BIT_COUNT = 11;

enum PeMemoryUsage : uint32_t
{
    PE_MEMORY_USAGE_GPU_ONLY = 0,          // device-local, never host-visible
    PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,    // device-local, dedicated allocation (AS / RT buffers)
    PE_MEMORY_USAGE_CPU_TO_GPU,            // host-visible, sequential write, mapped on demand
    PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT, // host-visible, sequential write, persistently mapped
    PE_MEMORY_USAGE_GPU_TO_CPU,            // host-visible, random read (readback)
    PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT, // host-visible, random read, persistently mapped (WebGPU MapRead)
    PE_MEMORY_USAGE_COUNT
};

constexpr uint32_t PE_QUEUE_FAMILY_IGNORED = ~0u;
constexpr size_t PE_WHOLE_SIZE = ~static_cast<size_t>(0);
