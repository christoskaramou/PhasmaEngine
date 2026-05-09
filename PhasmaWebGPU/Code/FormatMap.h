#pragma once

#include "FormatInfo.h"
#include "API/Vulkan/VulkanHeaders.h"

namespace pwgpu
{

    inline VkFormat ToVkFormat(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
            return VK_FORMAT_R8_UNORM;
        case WGPUTextureFormat_R8Snorm:
            return VK_FORMAT_R8_SNORM;
        case WGPUTextureFormat_R8Uint:
            return VK_FORMAT_R8_UINT;
        case WGPUTextureFormat_R8Sint:
            return VK_FORMAT_R8_SINT;
        case WGPUTextureFormat_R16Uint:
            return VK_FORMAT_R16_UINT;
        case WGPUTextureFormat_R16Sint:
            return VK_FORMAT_R16_SINT;
        case WGPUTextureFormat_R16Float:
            return VK_FORMAT_R16_SFLOAT;
        case WGPUTextureFormat_R16Unorm:
            return VK_FORMAT_R16_UNORM;
        case WGPUTextureFormat_R16Snorm:
            return VK_FORMAT_R16_SNORM;
        case WGPUTextureFormat_RG16Unorm:
            return VK_FORMAT_R16G16_UNORM;
        case WGPUTextureFormat_RG16Snorm:
            return VK_FORMAT_R16G16_SNORM;
        case WGPUTextureFormat_RGBA16Unorm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case WGPUTextureFormat_RGBA16Snorm:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case WGPUTextureFormat_RG8Unorm:
            return VK_FORMAT_R8G8_UNORM;
        case WGPUTextureFormat_RG8Snorm:
            return VK_FORMAT_R8G8_SNORM;
        case WGPUTextureFormat_RG8Uint:
            return VK_FORMAT_R8G8_UINT;
        case WGPUTextureFormat_RG8Sint:
            return VK_FORMAT_R8G8_SINT;
        case WGPUTextureFormat_R32Float:
            return VK_FORMAT_R32_SFLOAT;
        case WGPUTextureFormat_R32Uint:
            return VK_FORMAT_R32_UINT;
        case WGPUTextureFormat_R32Sint:
            return VK_FORMAT_R32_SINT;
        case WGPUTextureFormat_RG16Uint:
            return VK_FORMAT_R16G16_UINT;
        case WGPUTextureFormat_RG16Sint:
            return VK_FORMAT_R16G16_SINT;
        case WGPUTextureFormat_RG16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case WGPUTextureFormat_RGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case WGPUTextureFormat_RGBA8Snorm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case WGPUTextureFormat_RGBA8Uint:
            return VK_FORMAT_R8G8B8A8_UINT;
        case WGPUTextureFormat_RGBA8Sint:
            return VK_FORMAT_R8G8B8A8_SINT;
        case WGPUTextureFormat_BGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case WGPUTextureFormat_RGB10A2Uint:
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case WGPUTextureFormat_RGB10A2Unorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case WGPUTextureFormat_RG11B10Ufloat:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case WGPUTextureFormat_RGB9E5Ufloat:
            return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case WGPUTextureFormat_RG32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case WGPUTextureFormat_RG32Uint:
            return VK_FORMAT_R32G32_UINT;
        case WGPUTextureFormat_RG32Sint:
            return VK_FORMAT_R32G32_SINT;
        case WGPUTextureFormat_RGBA16Uint:
            return VK_FORMAT_R16G16B16A16_UINT;
        case WGPUTextureFormat_RGBA16Sint:
            return VK_FORMAT_R16G16B16A16_SINT;
        case WGPUTextureFormat_RGBA16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case WGPUTextureFormat_RGBA32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case WGPUTextureFormat_RGBA32Uint:
            return VK_FORMAT_R32G32B32A32_UINT;
        case WGPUTextureFormat_RGBA32Sint:
            return VK_FORMAT_R32G32B32A32_SINT;
        case WGPUTextureFormat_Stencil8:
            return VK_FORMAT_S8_UINT;
        case WGPUTextureFormat_Depth16Unorm:
            return VK_FORMAT_D16_UNORM;
        case WGPUTextureFormat_Depth24Plus:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case WGPUTextureFormat_Depth24PlusStencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case WGPUTextureFormat_Depth32Float:
            return VK_FORMAT_D32_SFLOAT;
        case WGPUTextureFormat_Depth32FloatStencil8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case WGPUTextureFormat_BC1RGBAUnorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case WGPUTextureFormat_BC1RGBAUnormSrgb:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case WGPUTextureFormat_BC2RGBAUnorm:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case WGPUTextureFormat_BC2RGBAUnormSrgb:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case WGPUTextureFormat_BC3RGBAUnorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case WGPUTextureFormat_BC3RGBAUnormSrgb:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case WGPUTextureFormat_BC4RUnorm:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case WGPUTextureFormat_BC4RSnorm:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case WGPUTextureFormat_BC5RGUnorm:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case WGPUTextureFormat_BC5RGSnorm:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case WGPUTextureFormat_BC6HRGBUfloat:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case WGPUTextureFormat_BC6HRGBFloat:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case WGPUTextureFormat_BC7RGBAUnorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case WGPUTextureFormat_BC7RGBAUnormSrgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case WGPUTextureFormat_ETC2RGB8Unorm:
            return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case WGPUTextureFormat_ETC2RGB8UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case WGPUTextureFormat_ETC2RGB8A1Unorm:
            return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
        case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
        case WGPUTextureFormat_ETC2RGBA8Unorm:
            return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
            return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case WGPUTextureFormat_EACR11Unorm:
            return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case WGPUTextureFormat_EACR11Snorm:
            return VK_FORMAT_EAC_R11_SNORM_BLOCK;
        case WGPUTextureFormat_EACRG11Unorm:
            return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        case WGPUTextureFormat_EACRG11Snorm:
            return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
        case WGPUTextureFormat_ASTC4x4Unorm:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC4x4UnormSrgb:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC5x4Unorm:
            return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC5x4UnormSrgb:
            return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC5x5Unorm:
            return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC5x5UnormSrgb:
            return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC6x5Unorm:
            return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC6x5UnormSrgb:
            return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC6x6Unorm:
            return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC6x6UnormSrgb:
            return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC8x5Unorm:
            return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC8x5UnormSrgb:
            return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC8x6Unorm:
            return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC8x6UnormSrgb:
            return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC8x8Unorm:
            return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC8x8UnormSrgb:
            return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC10x5Unorm:
            return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC10x5UnormSrgb:
            return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC10x6Unorm:
            return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC10x6UnormSrgb:
            return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC10x8Unorm:
            return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC10x8UnormSrgb:
            return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC10x10Unorm:
            return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC10x10UnormSrgb:
            return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC12x10Unorm:
            return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC12x10UnormSrgb:
            return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
        case WGPUTextureFormat_ASTC12x12Unorm:
            return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
        case WGPUTextureFormat_ASTC12x12UnormSrgb:
            return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    inline WGPUTextureFormat FromVkFormat(VkFormat f)
    {
        switch (f)
        {
        case VK_FORMAT_R8_UNORM:
            return WGPUTextureFormat_R8Unorm;
        case VK_FORMAT_R8_SNORM:
            return WGPUTextureFormat_R8Snorm;
        case VK_FORMAT_R8_UINT:
            return WGPUTextureFormat_R8Uint;
        case VK_FORMAT_R8_SINT:
            return WGPUTextureFormat_R8Sint;
        case VK_FORMAT_R16_UINT:
            return WGPUTextureFormat_R16Uint;
        case VK_FORMAT_R16_SINT:
            return WGPUTextureFormat_R16Sint;
        case VK_FORMAT_R16_SFLOAT:
            return WGPUTextureFormat_R16Float;
        case VK_FORMAT_R8G8_UNORM:
            return WGPUTextureFormat_RG8Unorm;
        case VK_FORMAT_R8G8_SNORM:
            return WGPUTextureFormat_RG8Snorm;
        case VK_FORMAT_R8G8_UINT:
            return WGPUTextureFormat_RG8Uint;
        case VK_FORMAT_R8G8_SINT:
            return WGPUTextureFormat_RG8Sint;
        case VK_FORMAT_R32_SFLOAT:
            return WGPUTextureFormat_R32Float;
        case VK_FORMAT_R32_UINT:
            return WGPUTextureFormat_R32Uint;
        case VK_FORMAT_R32_SINT:
            return WGPUTextureFormat_R32Sint;
        case VK_FORMAT_R16G16_UINT:
            return WGPUTextureFormat_RG16Uint;
        case VK_FORMAT_R16G16_SINT:
            return WGPUTextureFormat_RG16Sint;
        case VK_FORMAT_R16G16_SFLOAT:
            return WGPUTextureFormat_RG16Float;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return WGPUTextureFormat_RGBA8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return WGPUTextureFormat_RGBA8UnormSrgb;
        case VK_FORMAT_R8G8B8A8_SNORM:
            return WGPUTextureFormat_RGBA8Snorm;
        case VK_FORMAT_R8G8B8A8_UINT:
            return WGPUTextureFormat_RGBA8Uint;
        case VK_FORMAT_R8G8B8A8_SINT:
            return WGPUTextureFormat_RGBA8Sint;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return WGPUTextureFormat_BGRA8Unorm;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return WGPUTextureFormat_BGRA8UnormSrgb;
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            return WGPUTextureFormat_RGB10A2Uint;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return WGPUTextureFormat_RGB10A2Unorm;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            return WGPUTextureFormat_RG11B10Ufloat;
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
            return WGPUTextureFormat_RGB9E5Ufloat;
        case VK_FORMAT_R32G32_SFLOAT:
            return WGPUTextureFormat_RG32Float;
        case VK_FORMAT_R32G32_UINT:
            return WGPUTextureFormat_RG32Uint;
        case VK_FORMAT_R32G32_SINT:
            return WGPUTextureFormat_RG32Sint;
        case VK_FORMAT_R16G16B16A16_UINT:
            return WGPUTextureFormat_RGBA16Uint;
        case VK_FORMAT_R16G16B16A16_SINT:
            return WGPUTextureFormat_RGBA16Sint;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return WGPUTextureFormat_RGBA16Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return WGPUTextureFormat_RGBA32Float;
        case VK_FORMAT_R32G32B32A32_UINT:
            return WGPUTextureFormat_RGBA32Uint;
        case VK_FORMAT_R32G32B32A32_SINT:
            return WGPUTextureFormat_RGBA32Sint;
        case VK_FORMAT_S8_UINT:
            return WGPUTextureFormat_Stencil8;
        case VK_FORMAT_D16_UNORM:
            return WGPUTextureFormat_Depth16Unorm;
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return WGPUTextureFormat_Depth24Plus;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return WGPUTextureFormat_Depth24PlusStencil8;
        case VK_FORMAT_D32_SFLOAT:
            return WGPUTextureFormat_Depth32Float;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return WGPUTextureFormat_Depth32FloatStencil8;
        default:
            return WGPUTextureFormat_Undefined;
        }
    }

    inline vk::ImageAspectFlags ToVkAspect(WGPUTextureAspect aspect, WGPUTextureFormat fmt)
    {
        if (aspect == WGPUTextureAspect_DepthOnly)
            return vk::ImageAspectFlagBits::eDepth;
        if (aspect == WGPUTextureAspect_StencilOnly)
            return vk::ImageAspectFlagBits::eStencil;
        vk::ImageAspectFlags flags{};
        if (HasDepthAspect(fmt))
            flags |= vk::ImageAspectFlagBits::eDepth;
        if (HasStencilAspect(fmt))
            flags |= vk::ImageAspectFlagBits::eStencil;
        if (!flags)
            flags = vk::ImageAspectFlagBits::eColor;
        return flags;
    }

    inline VkFormat VertexFormatToVk(WGPUVertexFormat f)
    {
        switch (f)
        {
        case WGPUVertexFormat_Uint8:
            return VK_FORMAT_R8_UINT;
        case WGPUVertexFormat_Uint8x2:
            return VK_FORMAT_R8G8_UINT;
        case WGPUVertexFormat_Uint8x4:
            return VK_FORMAT_R8G8B8A8_UINT;
        case WGPUVertexFormat_Sint8:
            return VK_FORMAT_R8_SINT;
        case WGPUVertexFormat_Sint8x2:
            return VK_FORMAT_R8G8_SINT;
        case WGPUVertexFormat_Sint8x4:
            return VK_FORMAT_R8G8B8A8_SINT;
        case WGPUVertexFormat_Unorm8:
            return VK_FORMAT_R8_UNORM;
        case WGPUVertexFormat_Unorm8x2:
            return VK_FORMAT_R8G8_UNORM;
        case WGPUVertexFormat_Unorm8x4:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case WGPUVertexFormat_Snorm8:
            return VK_FORMAT_R8_SNORM;
        case WGPUVertexFormat_Snorm8x2:
            return VK_FORMAT_R8G8_SNORM;
        case WGPUVertexFormat_Snorm8x4:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case WGPUVertexFormat_Uint16:
            return VK_FORMAT_R16_UINT;
        case WGPUVertexFormat_Uint16x2:
            return VK_FORMAT_R16G16_UINT;
        case WGPUVertexFormat_Uint16x4:
            return VK_FORMAT_R16G16B16A16_UINT;
        case WGPUVertexFormat_Sint16:
            return VK_FORMAT_R16_SINT;
        case WGPUVertexFormat_Sint16x2:
            return VK_FORMAT_R16G16_SINT;
        case WGPUVertexFormat_Sint16x4:
            return VK_FORMAT_R16G16B16A16_SINT;
        case WGPUVertexFormat_Unorm16:
            return VK_FORMAT_R16_UNORM;
        case WGPUVertexFormat_Unorm16x2:
            return VK_FORMAT_R16G16_UNORM;
        case WGPUVertexFormat_Unorm16x4:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case WGPUVertexFormat_Snorm16:
            return VK_FORMAT_R16_SNORM;
        case WGPUVertexFormat_Snorm16x2:
            return VK_FORMAT_R16G16_SNORM;
        case WGPUVertexFormat_Snorm16x4:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case WGPUVertexFormat_Float16:
            return VK_FORMAT_R16_SFLOAT;
        case WGPUVertexFormat_Float16x2:
            return VK_FORMAT_R16G16_SFLOAT;
        case WGPUVertexFormat_Float16x4:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case WGPUVertexFormat_Float32:
            return VK_FORMAT_R32_SFLOAT;
        case WGPUVertexFormat_Float32x2:
            return VK_FORMAT_R32G32_SFLOAT;
        case WGPUVertexFormat_Float32x3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case WGPUVertexFormat_Float32x4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case WGPUVertexFormat_Uint32:
            return VK_FORMAT_R32_UINT;
        case WGPUVertexFormat_Uint32x2:
            return VK_FORMAT_R32G32_UINT;
        case WGPUVertexFormat_Uint32x3:
            return VK_FORMAT_R32G32B32_UINT;
        case WGPUVertexFormat_Uint32x4:
            return VK_FORMAT_R32G32B32A32_UINT;
        case WGPUVertexFormat_Sint32:
            return VK_FORMAT_R32_SINT;
        case WGPUVertexFormat_Sint32x2:
            return VK_FORMAT_R32G32_SINT;
        case WGPUVertexFormat_Sint32x3:
            return VK_FORMAT_R32G32B32_SINT;
        case WGPUVertexFormat_Sint32x4:
            return VK_FORMAT_R32G32B32A32_SINT;
        case WGPUVertexFormat_Unorm10_10_10_2:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case WGPUVertexFormat_Unorm8x4BGRA:
            return VK_FORMAT_B8G8R8A8_UNORM;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    inline vk::PrimitiveTopology ToVkTopology(WGPUPrimitiveTopology t)
    {
        switch (t)
        {
        case WGPUPrimitiveTopology_PointList:
            return vk::PrimitiveTopology::ePointList;
        case WGPUPrimitiveTopology_LineList:
            return vk::PrimitiveTopology::eLineList;
        case WGPUPrimitiveTopology_LineStrip:
            return vk::PrimitiveTopology::eLineStrip;
        case WGPUPrimitiveTopology_TriangleStrip:
            return vk::PrimitiveTopology::eTriangleStrip;
        case WGPUPrimitiveTopology_TriangleList:
        default:
            return vk::PrimitiveTopology::eTriangleList;
        }
    }

    inline vk::FrontFace ToVkFrontFace(WGPUFrontFace f)
    {
        return f == WGPUFrontFace_CW ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
    }

    inline vk::CullModeFlags ToVkCullMode(WGPUCullMode m)
    {
        switch (m)
        {
        case WGPUCullMode_Front:
            return vk::CullModeFlagBits::eFront;
        case WGPUCullMode_Back:
            return vk::CullModeFlagBits::eBack;
        case WGPUCullMode_None:
        default:
            return vk::CullModeFlagBits::eNone;
        }
    }

    inline vk::CompareOp ToVkCompareOp(WGPUCompareFunction f)
    {
        switch (f)
        {
        case WGPUCompareFunction_Never:
            return vk::CompareOp::eNever;
        case WGPUCompareFunction_Less:
            return vk::CompareOp::eLess;
        case WGPUCompareFunction_Equal:
            return vk::CompareOp::eEqual;
        case WGPUCompareFunction_LessEqual:
            return vk::CompareOp::eLessOrEqual;
        case WGPUCompareFunction_Greater:
            return vk::CompareOp::eGreater;
        case WGPUCompareFunction_NotEqual:
            return vk::CompareOp::eNotEqual;
        case WGPUCompareFunction_GreaterEqual:
            return vk::CompareOp::eGreaterOrEqual;
        case WGPUCompareFunction_Always:
        default:
            return vk::CompareOp::eAlways;
        }
    }

    inline vk::StencilOp ToVkStencilOp(WGPUStencilOperation op)
    {
        switch (op)
        {
        case WGPUStencilOperation_Zero:
            return vk::StencilOp::eZero;
        case WGPUStencilOperation_Replace:
            return vk::StencilOp::eReplace;
        case WGPUStencilOperation_Invert:
            return vk::StencilOp::eInvert;
        case WGPUStencilOperation_IncrementClamp:
            return vk::StencilOp::eIncrementAndClamp;
        case WGPUStencilOperation_DecrementClamp:
            return vk::StencilOp::eDecrementAndClamp;
        case WGPUStencilOperation_IncrementWrap:
            return vk::StencilOp::eIncrementAndWrap;
        case WGPUStencilOperation_DecrementWrap:
            return vk::StencilOp::eDecrementAndWrap;
        case WGPUStencilOperation_Keep:
        default:
            return vk::StencilOp::eKeep;
        }
    }

    inline vk::BlendFactor ToVkBlendFactor(WGPUBlendFactor f)
    {
        switch (f)
        {
        case WGPUBlendFactor_Zero:
            return vk::BlendFactor::eZero;
        case WGPUBlendFactor_One:
            return vk::BlendFactor::eOne;
        case WGPUBlendFactor_Src:
            return vk::BlendFactor::eSrcColor;
        case WGPUBlendFactor_OneMinusSrc:
            return vk::BlendFactor::eOneMinusSrcColor;
        case WGPUBlendFactor_SrcAlpha:
            return vk::BlendFactor::eSrcAlpha;
        case WGPUBlendFactor_OneMinusSrcAlpha:
            return vk::BlendFactor::eOneMinusSrcAlpha;
        case WGPUBlendFactor_Dst:
            return vk::BlendFactor::eDstColor;
        case WGPUBlendFactor_OneMinusDst:
            return vk::BlendFactor::eOneMinusDstColor;
        case WGPUBlendFactor_DstAlpha:
            return vk::BlendFactor::eDstAlpha;
        case WGPUBlendFactor_OneMinusDstAlpha:
            return vk::BlendFactor::eOneMinusDstAlpha;
        case WGPUBlendFactor_SrcAlphaSaturated:
            return vk::BlendFactor::eSrcAlphaSaturate;
        case WGPUBlendFactor_Constant:
            return vk::BlendFactor::eConstantColor;
        case WGPUBlendFactor_OneMinusConstant:
            return vk::BlendFactor::eOneMinusConstantColor;
        case WGPUBlendFactor_Src1:
            return vk::BlendFactor::eSrc1Color;
        case WGPUBlendFactor_OneMinusSrc1:
            return vk::BlendFactor::eOneMinusSrc1Color;
        case WGPUBlendFactor_Src1Alpha:
            return vk::BlendFactor::eSrc1Alpha;
        case WGPUBlendFactor_OneMinusSrc1Alpha:
            return vk::BlendFactor::eOneMinusSrc1Alpha;
        default:
            return vk::BlendFactor::eOne;
        }
    }

    inline vk::BlendOp ToVkBlendOp(WGPUBlendOperation op)
    {
        switch (op)
        {
        case WGPUBlendOperation_Add:
            return vk::BlendOp::eAdd;
        case WGPUBlendOperation_Subtract:
            return vk::BlendOp::eSubtract;
        case WGPUBlendOperation_ReverseSubtract:
            return vk::BlendOp::eReverseSubtract;
        case WGPUBlendOperation_Min:
            return vk::BlendOp::eMin;
        case WGPUBlendOperation_Max:
            return vk::BlendOp::eMax;
        default:
            return vk::BlendOp::eAdd;
        }
    }

    inline vk::SampleCountFlagBits ToVkSampleCount(uint32_t count)
    {
        switch (count)
        {
        case 4:
            return vk::SampleCountFlagBits::e4;
        default:
            return vk::SampleCountFlagBits::e1;
        }
    }

} // namespace pwgpu
