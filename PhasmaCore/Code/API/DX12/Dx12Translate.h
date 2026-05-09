#pragma once
#include "API/RHITypes.h"

#if defined(PE_WIN32)

namespace pe_dx12
{
    inline uint32_t ctz64(uint64_t v)
    {
#if defined(_MSC_VER)
        unsigned long idx = 0;
        _BitScanForward64(&idx, v);
        return static_cast<uint32_t>(idx);
#else
        return static_cast<uint32_t>(__builtin_ctzll(v));
#endif
    }

    static constexpr DXGI_FORMAT Formats[PE_FORMAT_COUNT] = {
        /* PE_FORMAT_UNDEFINED                */ DXGI_FORMAT_UNKNOWN,
        /* PE_FORMAT_R8_UNORM                 */ DXGI_FORMAT_R8_UNORM,
        /* PE_FORMAT_R8_SINT                  */ DXGI_FORMAT_R8_SINT,
        /* PE_FORMAT_R8_UINT                  */ DXGI_FORMAT_R8_UINT,
        /* PE_FORMAT_R16_SFLOAT               */ DXGI_FORMAT_R16_FLOAT,
        /* PE_FORMAT_R16_SINT                 */ DXGI_FORMAT_R16_SINT,
        /* PE_FORMAT_R16_UINT                 */ DXGI_FORMAT_R16_UINT,
        /* PE_FORMAT_R32_SFLOAT               */ DXGI_FORMAT_R32_FLOAT,
        /* PE_FORMAT_R32_SINT                 */ DXGI_FORMAT_R32_SINT,
        /* PE_FORMAT_R32_UINT                 */ DXGI_FORMAT_R32_UINT,
        /* PE_FORMAT_R16G16_SFLOAT            */ DXGI_FORMAT_R16G16_FLOAT,
        /* PE_FORMAT_R32G32_SFLOAT            */ DXGI_FORMAT_R32G32_FLOAT,
        /* PE_FORMAT_R32G32_SINT              */ DXGI_FORMAT_R32G32_SINT,
        /* PE_FORMAT_R32G32_UINT              */ DXGI_FORMAT_R32G32_UINT,
        /* PE_FORMAT_R16G16B16_SFLOAT         */ DXGI_FORMAT_UNKNOWN, // no DXGI 3-channel float16
        /* PE_FORMAT_R16G16B16_SINT           */ DXGI_FORMAT_UNKNOWN,
        /* PE_FORMAT_R16G16B16_UINT           */ DXGI_FORMAT_UNKNOWN,
        /* PE_FORMAT_R32G32B32_SFLOAT         */ DXGI_FORMAT_R32G32B32_FLOAT,
        /* PE_FORMAT_R32G32B32_SINT           */ DXGI_FORMAT_R32G32B32_SINT,
        /* PE_FORMAT_R32G32B32_UINT           */ DXGI_FORMAT_R32G32B32_UINT,
        /* PE_FORMAT_R8G8B8A8_UNORM           */ DXGI_FORMAT_R8G8B8A8_UNORM,
        /* PE_FORMAT_R8G8B8A8_SRGB            */ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        /* PE_FORMAT_B8G8R8A8_UNORM           */ DXGI_FORMAT_B8G8R8A8_UNORM,
        /* PE_FORMAT_B8G8R8A8_SRGB            */ DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        /* PE_FORMAT_R16G16B16A16_SFLOAT      */ DXGI_FORMAT_R16G16B16A16_FLOAT,
        /* PE_FORMAT_R32G32B32A32_SFLOAT      */ DXGI_FORMAT_R32G32B32A32_FLOAT,
        /* PE_FORMAT_R32G32B32A32_SINT        */ DXGI_FORMAT_R32G32B32A32_SINT,
        /* PE_FORMAT_R32G32B32A32_UINT        */ DXGI_FORMAT_R32G32B32A32_UINT,
        /* PE_FORMAT_A2B10G10R10_UNORM_PACK32 */ DXGI_FORMAT_R10G10B10A2_UNORM,
        /* PE_FORMAT_B10G11R11_UFLOAT_PACK32  */ DXGI_FORMAT_R11G11B10_FLOAT,
        /* PE_FORMAT_D32_SFLOAT               */ DXGI_FORMAT_D32_FLOAT,
        /* PE_FORMAT_D24_UNORM_S8_UINT        */ DXGI_FORMAT_D24_UNORM_S8_UINT,
        /* PE_FORMAT_D32_SFLOAT_S8_UINT       */ DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        /* PE_FORMAT_S8_UINT                  */ DXGI_FORMAT_UNKNOWN, // no standalone S8 in DXGI
        /* PE_FORMAT_BC1_RGBA_UNORM           */ DXGI_FORMAT_BC1_UNORM,
        /* PE_FORMAT_BC1_RGBA_SRGB            */ DXGI_FORMAT_BC1_UNORM_SRGB,
        /* PE_FORMAT_BC2_UNORM                */ DXGI_FORMAT_BC2_UNORM,
        /* PE_FORMAT_BC2_SRGB                 */ DXGI_FORMAT_BC2_UNORM_SRGB,
        /* PE_FORMAT_BC3_UNORM                */ DXGI_FORMAT_BC3_UNORM,
        /* PE_FORMAT_BC3_SRGB                 */ DXGI_FORMAT_BC3_UNORM_SRGB,
        /* PE_FORMAT_BC4_UNORM                */ DXGI_FORMAT_BC4_UNORM,
        /* PE_FORMAT_BC4_SNORM                */ DXGI_FORMAT_BC4_SNORM,
        /* PE_FORMAT_BC5_UNORM                */ DXGI_FORMAT_BC5_UNORM,
        /* PE_FORMAT_BC5_SNORM                */ DXGI_FORMAT_BC5_SNORM,
        /* PE_FORMAT_BC6H_UFLOAT              */ DXGI_FORMAT_BC6H_UF16,
        /* PE_FORMAT_BC6H_SFLOAT              */ DXGI_FORMAT_BC6H_SF16,
        /* PE_FORMAT_BC7_UNORM                */ DXGI_FORMAT_BC7_UNORM,
        /* PE_FORMAT_BC7_SRGB                 */ DXGI_FORMAT_BC7_UNORM_SRGB,
    };

    inline DXGI_FORMAT Format(PeFormat f)
    {
        return Formats[f];
    }

    inline DXGI_FORMAT IndexFormat(PeIndexType indexType)
    {
        switch (indexType)
        {
        case PE_INDEX_TYPE_UINT16:
            return DXGI_FORMAT_R16_UINT;
        case PE_INDEX_TYPE_UINT32:
            return DXGI_FORMAT_R32_UINT;
        default:
            return DXGI_FORMAT_R32_UINT;
        }
    }

    inline PeFormat FromFormat(DXGI_FORMAT f)
    {
        for (int i = 0; i < PE_FORMAT_COUNT; ++i)
            if (Formats[i] == f)
                return static_cast<PeFormat>(i);
        return PE_FORMAT_UNDEFINED;
    }

    inline DXGI_FORMAT DepthToTypeless(DXGI_FORMAT f)
    {
        switch (f)
        {
        case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32G8X24_TYPELESS;
        default:
            return f;
        }
    }

    inline DXGI_FORMAT DepthToSRVFormat(DXGI_FORMAT f)
    {
        switch (f)
        {
        case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:
            return f;
        }
    }

    inline bool IsDepthFormat(DXGI_FORMAT f)
    {
        return f == DXGI_FORMAT_D32_FLOAT ||
               f == DXGI_FORMAT_D16_UNORM ||
               f == DXGI_FORMAT_D24_UNORM_S8_UINT ||
               f == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    }

    static constexpr D3D12_CULL_MODE CullModes[PE_CULL_MODE_COUNT] = {
        /* PE_CULL_MODE_NONE           */ D3D12_CULL_MODE_NONE,
        /* PE_CULL_MODE_FRONT          */ D3D12_CULL_MODE_FRONT,
        /* PE_CULL_MODE_BACK           */ D3D12_CULL_MODE_BACK,
        /* PE_CULL_MODE_FRONT_AND_BACK */ D3D12_CULL_MODE_NONE, // no D3D12 equivalent; use NONE
    };

    inline D3D12_CULL_MODE CullMode(PeCullMode m)
    {
        return CullModes[m];
    }

    static constexpr D3D12_FILL_MODE FillModes[PE_POLYGON_MODE_COUNT] = {
        /* PE_POLYGON_MODE_FILL  */ D3D12_FILL_MODE_SOLID,
        /* PE_POLYGON_MODE_LINE  */ D3D12_FILL_MODE_WIREFRAME,
        /* PE_POLYGON_MODE_POINT */ D3D12_FILL_MODE_WIREFRAME, // no point fill in D3D12
    };

    inline D3D12_FILL_MODE FillMode(PePolygonMode m)
    {
        return FillModes[m];
    }

    static constexpr D3D12_PRIMITIVE_TOPOLOGY Topologies[PE_TOPOLOGY_COUNT] = {
        /* PE_TOPOLOGY_POINT_LIST    */ D3D_PRIMITIVE_TOPOLOGY_POINTLIST,
        /* PE_TOPOLOGY_LINE_LIST     */ D3D_PRIMITIVE_TOPOLOGY_LINELIST,
        /* PE_TOPOLOGY_LINE_STRIP    */ D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,
        /* PE_TOPOLOGY_TRIANGLE_LIST */ D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        /* PE_TOPOLOGY_TRIANGLE_STRIP*/ D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
        /* PE_TOPOLOGY_TRIANGLE_FAN  */ D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // no fan in D3D12
        /* PE_TOPOLOGY_PATCH_LIST    */ D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST,
    };

    static constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyTypes[PE_TOPOLOGY_COUNT] = {
        /* PE_TOPOLOGY_POINT_LIST    */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,
        /* PE_TOPOLOGY_LINE_LIST     */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        /* PE_TOPOLOGY_LINE_STRIP    */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        /* PE_TOPOLOGY_TRIANGLE_LIST */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        /* PE_TOPOLOGY_TRIANGLE_STRIP*/ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        /* PE_TOPOLOGY_TRIANGLE_FAN  */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        /* PE_TOPOLOGY_PATCH_LIST    */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH,
    };

    inline D3D12_PRIMITIVE_TOPOLOGY Topology(PeTopology t)
    {
        return Topologies[t];
    }
    inline D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(PeTopology t)
    {
        return TopologyTypes[t];
    }

    static constexpr D3D12_COMPARISON_FUNC CompareOps[PE_COMPARE_OP_COUNT] = {
        /* PE_COMPARE_OP_NEVER            */ D3D12_COMPARISON_FUNC_NEVER,
        /* PE_COMPARE_OP_LESS             */ D3D12_COMPARISON_FUNC_LESS,
        /* PE_COMPARE_OP_EQUAL            */ D3D12_COMPARISON_FUNC_EQUAL,
        /* PE_COMPARE_OP_LESS_OR_EQUAL    */ D3D12_COMPARISON_FUNC_LESS_EQUAL,
        /* PE_COMPARE_OP_GREATER          */ D3D12_COMPARISON_FUNC_GREATER,
        /* PE_COMPARE_OP_NOT_EQUAL        */ D3D12_COMPARISON_FUNC_NOT_EQUAL,
        /* PE_COMPARE_OP_GREATER_OR_EQUAL */ D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        /* PE_COMPARE_OP_ALWAYS           */ D3D12_COMPARISON_FUNC_ALWAYS,
    };

    inline D3D12_COMPARISON_FUNC CompareOp(PeCompareOp op)
    {
        return CompareOps[op];
    }

    static constexpr D3D12_STENCIL_OP StencilOps[PE_STENCIL_OP_COUNT] = {
        /* PE_STENCIL_OP_KEEP                */ D3D12_STENCIL_OP_KEEP,
        /* PE_STENCIL_OP_ZERO                */ D3D12_STENCIL_OP_ZERO,
        /* PE_STENCIL_OP_REPLACE             */ D3D12_STENCIL_OP_REPLACE,
        /* PE_STENCIL_OP_INCREMENT_AND_CLAMP */ D3D12_STENCIL_OP_INCR_SAT,
        /* PE_STENCIL_OP_DECREMENT_AND_CLAMP */ D3D12_STENCIL_OP_DECR_SAT,
        /* PE_STENCIL_OP_INVERT              */ D3D12_STENCIL_OP_INVERT,
        /* PE_STENCIL_OP_INCREMENT_AND_WRAP  */ D3D12_STENCIL_OP_INCR,
        /* PE_STENCIL_OP_DECREMENT_AND_WRAP  */ D3D12_STENCIL_OP_DECR,
    };

    inline D3D12_STENCIL_OP StencilOp(PeStencilOp op)
    {
        return StencilOps[op];
    }

    static constexpr D3D12_BLEND BlendFactors[PE_BLEND_FACTOR_COUNT] = {
        /* PE_BLEND_FACTOR_ZERO                */ D3D12_BLEND_ZERO,
        /* PE_BLEND_FACTOR_ONE                 */ D3D12_BLEND_ONE,
        /* PE_BLEND_FACTOR_SRC_ALPHA           */ D3D12_BLEND_SRC_ALPHA,
        /* PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA */ D3D12_BLEND_INV_SRC_ALPHA,
        /* PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR */ D3D12_BLEND_INV_SRC_COLOR,
    };

    inline D3D12_BLEND BlendFactor(PeBlendFactor f)
    {
        return BlendFactors[f];
    }

    static constexpr D3D12_BLEND_OP BlendOps[PE_BLEND_OP_COUNT] = {
        /* PE_BLEND_OP_ADD             */ D3D12_BLEND_OP_ADD,
        /* PE_BLEND_OP_SUBTRACT        */ D3D12_BLEND_OP_SUBTRACT,
        /* PE_BLEND_OP_REVERSE_SUBTRACT*/ D3D12_BLEND_OP_REV_SUBTRACT,
        /* PE_BLEND_OP_MIN             */ D3D12_BLEND_OP_MIN,
        /* PE_BLEND_OP_MAX             */ D3D12_BLEND_OP_MAX,
    };

    inline D3D12_BLEND_OP BlendOp(PeBlendOp op)
    {
        return BlendOps[op];
    }

    inline uint8_t ColorWriteMask(PeColorComponentFlags f)
    {
        return static_cast<uint8_t>(f);
    }

    inline D3D12_RENDER_TARGET_BLEND_DESC BlendAttachment(const PeBlendAttachmentState &s)
    {
        D3D12_RENDER_TARGET_BLEND_DESC d{};
        d.BlendEnable = s.blendEnable ? TRUE : FALSE;
        d.LogicOpEnable = FALSE;
        d.SrcBlend = BlendFactor(s.srcColorBlendFactor);
        d.DestBlend = BlendFactor(s.dstColorBlendFactor);
        d.BlendOp = BlendOp(s.colorBlendOp);
        d.SrcBlendAlpha = BlendFactor(s.srcAlphaBlendFactor);
        d.DestBlendAlpha = BlendFactor(s.dstAlphaBlendFactor);
        d.BlendOpAlpha = BlendOp(s.alphaBlendOp);
        d.LogicOp = D3D12_LOGIC_OP_NOOP;
        d.RenderTargetWriteMask = ColorWriteMask(s.colorWriteMask);
        return d;
    }

    static constexpr D3D12_FILTER Filters[PE_FILTER_COUNT] = {
        /* PE_FILTER_NEAREST */ D3D12_FILTER_MIN_MAG_MIP_POINT,
        /* PE_FILTER_LINEAR  */ D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    };

    inline D3D12_FILTER Filter(PeFilter f)
    {
        return Filters[f];
    }

    static constexpr D3D12_TEXTURE_ADDRESS_MODE AddressModes[PE_SAMPLER_ADDRESS_MODE_COUNT] = {
        /* PE_SAMPLER_ADDRESS_MODE_REPEAT          */ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        /* PE_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT */ D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
        /* PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        /* PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER */ D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    };

    inline D3D12_TEXTURE_ADDRESS_MODE AddressMode(PeSamplerAddressMode m)
    {
        return AddressModes[m];
    }

    static constexpr D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeTypes[PE_DESCRIPTOR_TYPE_COUNT] = {
        /* PE_DESCRIPTOR_TYPE_SAMPLER                */ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
        /* PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // split at bind time
        /* PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE          */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        /* PE_DESCRIPTOR_TYPE_STORAGE_IMAGE          */ D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        /* PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER   */ D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        /* PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER         */ D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
        /* PE_DESCRIPTOR_TYPE_STORAGE_BUFFER         */ D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        /* PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC */ D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
        /* PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC */ D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        /* PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT        */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        /* PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE  */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        /* PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER       */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        /* PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER     */ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    };

    inline D3D12_DESCRIPTOR_RANGE_TYPE DescriptorRangeType(PeDescriptorType t)
    {
        return DescriptorRangeTypes[t];
    }

    // Order MUST match the bit positions in PePipelineStageFlags (RHITypes.h:347+).
    // ctz64(flag) returns the bit index, used as the table index.
    static constexpr D3D12_BARRIER_SYNC StageBits[PE_STAGE_BIT_COUNT] = {
        /* bit  0: PE_STAGE_TOP_OF_PIPE                       */ D3D12_BARRIER_SYNC_NONE, // Vulkan barrier-model anchor; no DX12 equivalent
        /* bit  1: PE_STAGE_VERTEX_SHADER                     */ D3D12_BARRIER_SYNC_VERTEX_SHADING,
        /* bit  2: PE_STAGE_FRAGMENT_SHADER                   */ D3D12_BARRIER_SYNC_PIXEL_SHADING,
        /* bit  3: PE_STAGE_COMPUTE_SHADER                    */ D3D12_BARRIER_SYNC_COMPUTE_SHADING,
        /* bit  4: PE_STAGE_TRANSFER                          */ D3D12_BARRIER_SYNC_COPY,
        /* bit  5: PE_STAGE_COLOR_ATTACHMENT_OUTPUT           */ D3D12_BARRIER_SYNC_RENDER_TARGET,
        /* bit  6: PE_STAGE_EARLY_FRAGMENT_TESTS              */ D3D12_BARRIER_SYNC_DEPTH_STENCIL,
        /* bit  7: PE_STAGE_LATE_FRAGMENT_TESTS               */ D3D12_BARRIER_SYNC_DEPTH_STENCIL,
        /* bit  8: PE_STAGE_VERTEX_INPUT                      */ D3D12_BARRIER_SYNC_INDEX_INPUT, // Vulkan VERTEX_INPUT covers index+attrs; DX12 splits — see INDEX_INPUT/VERTEX_ATTRIBUTE_INPUT bits
        /* bit  9: PE_STAGE_DRAW_INDIRECT                     */ D3D12_BARRIER_SYNC_EXECUTE_INDIRECT,
        /* bit 10: PE_STAGE_ALL_GRAPHICS                      */ D3D12_BARRIER_SYNC_ALL_SHADING,
        /* bit 11: PE_STAGE_ALL_COMMANDS                      */ D3D12_BARRIER_SYNC_ALL,
        /* bit 12: PE_STAGE_BOTTOM_OF_PIPE                    */ D3D12_BARRIER_SYNC_ALL,
        /* bit 13: PE_STAGE_RAY_TRACING_SHADER_KHR            */ D3D12_BARRIER_SYNC_RAYTRACING,
        /* bit 14: PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR  */ D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
        /* bit 15: PE_STAGE_CLEAR                             */ D3D12_BARRIER_SYNC_RENDER_TARGET, // ClearRenderTargetView is render-target work in DX12
        /* bit 16: PE_STAGE_COPY                              */ D3D12_BARRIER_SYNC_COPY,
        /* bit 17: PE_STAGE_HOST                              */ D3D12_BARRIER_SYNC_NONE, // DX12 has no explicit host stage
        /* bit 18: PE_STAGE_INDEX_INPUT                       */ D3D12_BARRIER_SYNC_INDEX_INPUT,
        /* bit 19: PE_STAGE_VERTEX_ATTRIBUTE_INPUT            */ D3D12_BARRIER_SYNC_VERTEX_SHADING, // DX12 folds vertex-attribute fetch into vertex shading
    };

    inline D3D12_BARRIER_SYNC Stage(PePipelineStageFlags flags)
    {
        if (!flags)
            return D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_SYNC r = D3D12_BARRIER_SYNC_NONE;
        uint64_t f = flags;
        while (f)
        {
            r |= StageBits[ctz64(f)];
            f &= f - 1;
        }
        return r;
    }

    // Order MUST match the bit positions in PeAccessFlags (RHITypes.h:371+).
    // ctz64(flag) returns the bit index, used as the table index.
    static constexpr D3D12_BARRIER_ACCESS AccessBits[PE_ACCESS_BIT_COUNT] = {
        /* bit  0: PE_ACCESS_SHADER_READ                       */ D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        /* bit  1: PE_ACCESS_SHADER_WRITE                      */ D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
        /* bit  2: PE_ACCESS_SHADER_SAMPLED_READ               */ D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        /* bit  3: PE_ACCESS_COLOR_ATTACHMENT_READ             */ D3D12_BARRIER_ACCESS_RENDER_TARGET,
        /* bit  4: PE_ACCESS_COLOR_ATTACHMENT_WRITE            */ D3D12_BARRIER_ACCESS_RENDER_TARGET,
        /* bit  5: PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ     */ D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
        /* bit  6: PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE    */ D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
        /* bit  7: PE_ACCESS_TRANSFER_READ                     */ D3D12_BARRIER_ACCESS_COPY_SOURCE,
        /* bit  8: PE_ACCESS_TRANSFER_WRITE                    */ D3D12_BARRIER_ACCESS_COPY_DEST,
        /* bit  9: PE_ACCESS_INDEX_READ                        */ D3D12_BARRIER_ACCESS_INDEX_BUFFER,
        /* bit 10: PE_ACCESS_VERTEX_ATTRIBUTE_READ             */ D3D12_BARRIER_ACCESS_VERTEX_BUFFER,
        /* bit 11: PE_ACCESS_INDIRECT_COMMAND_READ             */ D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT,
        /* bit 12: PE_ACCESS_MEMORY_READ                       */ D3D12_BARRIER_ACCESS_COMMON,
        /* bit 13: PE_ACCESS_MEMORY_WRITE                      */ D3D12_BARRIER_ACCESS_COMMON,
        /* bit 14: PE_ACCESS_HOST_WRITE                        */ D3D12_BARRIER_ACCESS_COMMON,
        /* bit 15: PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR   */ D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ,
        /* bit 16: PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR  */ D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE,
        /* bit 17: PE_ACCESS_UNIFORM_READ                      */ D3D12_BARRIER_ACCESS_CONSTANT_BUFFER,
        /* bit 18: PE_ACCESS_SHADER_STORAGE_READ               */ D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        /* bit 19: PE_ACCESS_SHADER_STORAGE_WRITE              */ D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
    };

    inline D3D12_BARRIER_ACCESS Access(PeAccessFlags flags)
    {
        if (!flags)
            return D3D12_BARRIER_ACCESS_NO_ACCESS;
        D3D12_BARRIER_ACCESS r = D3D12_BARRIER_ACCESS_NO_ACCESS;
        uint64_t f = flags;
        while (f)
        {
            r |= AccessBits[ctz64(f)];
            f &= f - 1;
        }
        return r;
    }

    static constexpr D3D12_BARRIER_LAYOUT ImageLayouts[PE_IMAGE_LAYOUT_COUNT] = {
        /* PE_IMAGE_LAYOUT_UNDEFINED                                  */ D3D12_BARRIER_LAYOUT_UNDEFINED,
        /* PE_IMAGE_LAYOUT_GENERAL                                    */ D3D12_BARRIER_LAYOUT_COMMON,
        /* PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL                   */ D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        /* PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL           */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
        /* PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL                   */ D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
        /* PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL                       */ D3D12_BARRIER_LAYOUT_COPY_SOURCE,
        /* PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL                       */ D3D12_BARRIER_LAYOUT_COPY_DEST,
        /* PE_IMAGE_LAYOUT_PRESENT_SRC                                */ D3D12_BARRIER_LAYOUT_PRESENT,
        /* PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL                         */ D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        /* PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL            */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
        /* PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, // depth-write + stencil-read; DX12 has no split layout, use the looser write-capable layout
        /* PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, // depth-read + stencil-write; same reasoning
        /* PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL                    */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
        /* PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL                   */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
        /* PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL                  */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
        /* PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL                 */ D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
    };

    inline D3D12_BARRIER_LAYOUT ImageLayout(PeImageLayout l)
    {
        return ImageLayouts[l];
    }

    // Legacy resource-state mapping for ID3D12GraphicsCommandList::ResourceBarrier (transition barriers).
    // Used by the conservative T10b path until the enhanced-barrier path lands; mirrors the layout
    // intent of each PeImageLayout. Order MUST match the PeImageLayout enum.
    static constexpr D3D12_RESOURCE_STATES ResourceStates[PE_IMAGE_LAYOUT_COUNT] = {
        /* PE_IMAGE_LAYOUT_UNDEFINED                                  */ D3D12_RESOURCE_STATE_COMMON,
        /* PE_IMAGE_LAYOUT_GENERAL                                    */ D3D12_RESOURCE_STATE_COMMON,
        /* PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL                   */ D3D12_RESOURCE_STATE_RENDER_TARGET,
        /* PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL           */ D3D12_RESOURCE_STATE_DEPTH_WRITE,
        /* PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL                   */ D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        /* PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL                       */ D3D12_RESOURCE_STATE_COPY_SOURCE,
        /* PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL                       */ D3D12_RESOURCE_STATE_COPY_DEST,
        /* PE_IMAGE_LAYOUT_PRESENT_SRC                                */ D3D12_RESOURCE_STATE_PRESENT,
        /* PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL                         */ D3D12_RESOURCE_STATE_RENDER_TARGET,
        /* PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL            */ D3D12_RESOURCE_STATE_DEPTH_READ,
        /* PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL */ D3D12_RESOURCE_STATE_DEPTH_WRITE, // depth-write + stencil-read; collapse to write
        /* PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL */ D3D12_RESOURCE_STATE_DEPTH_WRITE, // depth-read + stencil-write; same reasoning
        /* PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL                    */ D3D12_RESOURCE_STATE_DEPTH_READ,
        /* PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL                   */ D3D12_RESOURCE_STATE_DEPTH_WRITE,
        /* PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL                  */ D3D12_RESOURCE_STATE_DEPTH_READ,
        /* PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL                 */ D3D12_RESOURCE_STATE_DEPTH_WRITE,
    };

    inline D3D12_RESOURCE_STATES ToD3D12ResourceState(PeImageLayout l)
    {
        return ResourceStates[l];
    }

    static constexpr D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE LoadOps[PE_LOAD_OP_COUNT] = {
        /* PE_LOAD_OP_LOAD      */ D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
        /* PE_LOAD_OP_CLEAR     */ D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
        /* PE_LOAD_OP_DONT_CARE */ D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD,
    };

    inline D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE LoadOp(PeLoadOp op)
    {
        return LoadOps[op];
    }

    static constexpr D3D12_RENDER_PASS_ENDING_ACCESS_TYPE StoreOps[PE_STORE_OP_COUNT] = {
        /* PE_STORE_OP_STORE     */ D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE,
        /* PE_STORE_OP_DONT_CARE */ D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD,
    };

    inline D3D12_RENDER_PASS_ENDING_ACCESS_TYPE StoreOp(PeStoreOp op)
    {
        return StoreOps[op];
    }

    inline D3D12_SHADER_VISIBILITY ShaderVisibility(PeShaderStageFlags flags)
    {
        switch (flags)
        {
        case PE_SHADER_STAGE_VERTEX:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case PE_SHADER_STAGE_FRAGMENT:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        case PE_SHADER_STAGE_GEOMETRY:
            return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case PE_SHADER_STAGE_TESS_CONTROL:
            return D3D12_SHADER_VISIBILITY_HULL;
        case PE_SHADER_STAGE_TESS_EVALUATION:
            return D3D12_SHADER_VISIBILITY_DOMAIN;
        default:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

} // namespace pe_dx12

#endif // PE_WIN32
