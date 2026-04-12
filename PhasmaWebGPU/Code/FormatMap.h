#pragma once

#include <webgpu/webgpu.h>

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

    inline bool IsBCFormat(WGPUTextureFormat f)
    {
        return f >= WGPUTextureFormat_BC1RGBAUnorm && f <= WGPUTextureFormat_BC7RGBAUnormSrgb;
    }

    inline bool IsETC2Format(WGPUTextureFormat f)
    {
        return f >= WGPUTextureFormat_ETC2RGB8Unorm && f <= WGPUTextureFormat_EACRG11Snorm;
    }

    inline bool IsASTCFormat(WGPUTextureFormat f)
    {
        return f >= WGPUTextureFormat_ASTC4x4Unorm && f <= WGPUTextureFormat_ASTC12x12UnormSrgb;
    }

    inline bool IsCompressedFormat(WGPUTextureFormat f)
    {
        return IsBCFormat(f) || IsETC2Format(f) || IsASTCFormat(f);
    }

    inline bool IsDepthStencilFormat(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_Stencil8:
        case WGPUTextureFormat_Depth16Unorm:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return true;
        default:
            return false;
        }
    }

    inline bool HasDepthAspect(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_Depth16Unorm:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return true;
        default:
            return false;
        }
    }

    inline bool HasStencilAspect(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_Stencil8:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return true;
        default:
            return false;
        }
    }

    inline bool IsRenderableFormat(WGPUTextureFormat f)
    {
        if (IsDepthStencilFormat(f))
            return true;
        if (IsCompressedFormat(f))
            return false;
        switch (f)
        {
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGB9E5Ufloat:
            return false;
        default:
            return f != WGPUTextureFormat_Undefined;
        }
    }

    inline bool SupportsMultisampling(WGPUTextureFormat f)
    {
        if (IsCompressedFormat(f))
            return false;
        switch (f)
        {
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_RGB9E5Ufloat:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RGBA8Snorm:
            return false;
        default:
            return f != WGPUTextureFormat_Undefined;
        }
    }

    inline bool SupportsStorageBinding(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_BGRA8Unorm:
            return true;
        default:
            return false;
        }
    }

    inline bool Supports3DTexture(WGPUTextureFormat f)
    {
        if (IsDepthStencilFormat(f))
            return false;
        if (IsCompressedFormat(f))
            return false;
        return f != WGPUTextureFormat_Undefined;
    }

    inline void GetTexelBlockSize(WGPUTextureFormat f, uint32_t &outW, uint32_t &outH)
    {
        outW = 1;
        outH = 1;
        switch (f)
        {
        case WGPUTextureFormat_BC1RGBAUnorm:
        case WGPUTextureFormat_BC1RGBAUnormSrgb:
        case WGPUTextureFormat_BC2RGBAUnorm:
        case WGPUTextureFormat_BC2RGBAUnormSrgb:
        case WGPUTextureFormat_BC3RGBAUnorm:
        case WGPUTextureFormat_BC3RGBAUnormSrgb:
        case WGPUTextureFormat_BC4RUnorm:
        case WGPUTextureFormat_BC4RSnorm:
        case WGPUTextureFormat_BC5RGUnorm:
        case WGPUTextureFormat_BC5RGSnorm:
        case WGPUTextureFormat_BC6HRGBUfloat:
        case WGPUTextureFormat_BC6HRGBFloat:
        case WGPUTextureFormat_BC7RGBAUnorm:
        case WGPUTextureFormat_BC7RGBAUnormSrgb:
        case WGPUTextureFormat_ETC2RGB8Unorm:
        case WGPUTextureFormat_ETC2RGB8UnormSrgb:
        case WGPUTextureFormat_ETC2RGB8A1Unorm:
        case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
        case WGPUTextureFormat_ETC2RGBA8Unorm:
        case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
        case WGPUTextureFormat_EACR11Unorm:
        case WGPUTextureFormat_EACR11Snorm:
        case WGPUTextureFormat_EACRG11Unorm:
        case WGPUTextureFormat_EACRG11Snorm:
            outW = 4;
            outH = 4;
            break;
        case WGPUTextureFormat_ASTC4x4Unorm:
        case WGPUTextureFormat_ASTC4x4UnormSrgb:
            outW = 4;
            outH = 4;
            break;
        case WGPUTextureFormat_ASTC5x4Unorm:
        case WGPUTextureFormat_ASTC5x4UnormSrgb:
            outW = 5;
            outH = 4;
            break;
        case WGPUTextureFormat_ASTC5x5Unorm:
        case WGPUTextureFormat_ASTC5x5UnormSrgb:
            outW = 5;
            outH = 5;
            break;
        case WGPUTextureFormat_ASTC6x5Unorm:
        case WGPUTextureFormat_ASTC6x5UnormSrgb:
            outW = 6;
            outH = 5;
            break;
        case WGPUTextureFormat_ASTC6x6Unorm:
        case WGPUTextureFormat_ASTC6x6UnormSrgb:
            outW = 6;
            outH = 6;
            break;
        case WGPUTextureFormat_ASTC8x5Unorm:
        case WGPUTextureFormat_ASTC8x5UnormSrgb:
            outW = 8;
            outH = 5;
            break;
        case WGPUTextureFormat_ASTC8x6Unorm:
        case WGPUTextureFormat_ASTC8x6UnormSrgb:
            outW = 8;
            outH = 6;
            break;
        case WGPUTextureFormat_ASTC8x8Unorm:
        case WGPUTextureFormat_ASTC8x8UnormSrgb:
            outW = 8;
            outH = 8;
            break;
        case WGPUTextureFormat_ASTC10x5Unorm:
        case WGPUTextureFormat_ASTC10x5UnormSrgb:
            outW = 10;
            outH = 5;
            break;
        case WGPUTextureFormat_ASTC10x6Unorm:
        case WGPUTextureFormat_ASTC10x6UnormSrgb:
            outW = 10;
            outH = 6;
            break;
        case WGPUTextureFormat_ASTC10x8Unorm:
        case WGPUTextureFormat_ASTC10x8UnormSrgb:
            outW = 10;
            outH = 8;
            break;
        case WGPUTextureFormat_ASTC10x10Unorm:
        case WGPUTextureFormat_ASTC10x10UnormSrgb:
            outW = 10;
            outH = 10;
            break;
        case WGPUTextureFormat_ASTC12x10Unorm:
        case WGPUTextureFormat_ASTC12x10UnormSrgb:
            outW = 12;
            outH = 10;
            break;
        case WGPUTextureFormat_ASTC12x12Unorm:
        case WGPUTextureFormat_ASTC12x12UnormSrgb:
            outW = 12;
            outH = 12;
            break;
        default:
            break;
        }
    }

    inline uint32_t MaxMipLevelCount(WGPUTextureDimension dim, uint32_t w, uint32_t h, uint32_t d)
    {
        if (dim == WGPUTextureDimension_1D)
            return 1;
        uint32_t m = (dim == WGPUTextureDimension_3D) ? std::max({w, h, d}) : std::max(w, h);
        if (m == 0)
            return 1;
        uint32_t bits = 0;
        uint32_t v = m;
        while (v >>= 1)
            ++bits;
        return bits + 1;
    }

    inline bool AreViewFormatCompatible(WGPUTextureFormat a, WGPUTextureFormat b)
    {
        if (a == b)
            return true;
        auto stripSrgb = [](WGPUTextureFormat f) -> WGPUTextureFormat
        {
            switch (f)
            {
            case WGPUTextureFormat_RGBA8UnormSrgb:
                return WGPUTextureFormat_RGBA8Unorm;
            case WGPUTextureFormat_BGRA8UnormSrgb:
                return WGPUTextureFormat_BGRA8Unorm;
            case WGPUTextureFormat_BC1RGBAUnormSrgb:
                return WGPUTextureFormat_BC1RGBAUnorm;
            case WGPUTextureFormat_BC2RGBAUnormSrgb:
                return WGPUTextureFormat_BC2RGBAUnorm;
            case WGPUTextureFormat_BC3RGBAUnormSrgb:
                return WGPUTextureFormat_BC3RGBAUnorm;
            case WGPUTextureFormat_BC7RGBAUnormSrgb:
                return WGPUTextureFormat_BC7RGBAUnorm;
            case WGPUTextureFormat_ETC2RGB8UnormSrgb:
                return WGPUTextureFormat_ETC2RGB8Unorm;
            case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
                return WGPUTextureFormat_ETC2RGB8A1Unorm;
            case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
                return WGPUTextureFormat_ETC2RGBA8Unorm;
            case WGPUTextureFormat_ASTC4x4UnormSrgb:
                return WGPUTextureFormat_ASTC4x4Unorm;
            case WGPUTextureFormat_ASTC5x4UnormSrgb:
                return WGPUTextureFormat_ASTC5x4Unorm;
            case WGPUTextureFormat_ASTC5x5UnormSrgb:
                return WGPUTextureFormat_ASTC5x5Unorm;
            case WGPUTextureFormat_ASTC6x5UnormSrgb:
                return WGPUTextureFormat_ASTC6x5Unorm;
            case WGPUTextureFormat_ASTC6x6UnormSrgb:
                return WGPUTextureFormat_ASTC6x6Unorm;
            case WGPUTextureFormat_ASTC8x5UnormSrgb:
                return WGPUTextureFormat_ASTC8x5Unorm;
            case WGPUTextureFormat_ASTC8x6UnormSrgb:
                return WGPUTextureFormat_ASTC8x6Unorm;
            case WGPUTextureFormat_ASTC8x8UnormSrgb:
                return WGPUTextureFormat_ASTC8x8Unorm;
            case WGPUTextureFormat_ASTC10x5UnormSrgb:
                return WGPUTextureFormat_ASTC10x5Unorm;
            case WGPUTextureFormat_ASTC10x6UnormSrgb:
                return WGPUTextureFormat_ASTC10x6Unorm;
            case WGPUTextureFormat_ASTC10x8UnormSrgb:
                return WGPUTextureFormat_ASTC10x8Unorm;
            case WGPUTextureFormat_ASTC10x10UnormSrgb:
                return WGPUTextureFormat_ASTC10x10Unorm;
            case WGPUTextureFormat_ASTC12x10UnormSrgb:
                return WGPUTextureFormat_ASTC12x10Unorm;
            case WGPUTextureFormat_ASTC12x12UnormSrgb:
                return WGPUTextureFormat_ASTC12x12Unorm;
            default:
                return f;
            }
        };
        return stripSrgb(a) == stripSrgb(b);
    }

    inline WGPUTextureFormat ResolveAspectFormat(WGPUTextureFormat texFmt, WGPUTextureAspect aspect)
    {
        if (aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_Undefined)
            return WGPUTextureFormat_Undefined;
        if (aspect == WGPUTextureAspect_DepthOnly)
        {
            switch (texFmt)
            {
            case WGPUTextureFormat_Depth16Unorm:
                return WGPUTextureFormat_Depth16Unorm;
            case WGPUTextureFormat_Depth24Plus:
            case WGPUTextureFormat_Depth24PlusStencil8:
                return WGPUTextureFormat_Depth24Plus;
            case WGPUTextureFormat_Depth32Float:
            case WGPUTextureFormat_Depth32FloatStencil8:
                return WGPUTextureFormat_Depth32Float;
            default:
                return WGPUTextureFormat_Undefined;
            }
        }
        if (aspect == WGPUTextureAspect_StencilOnly)
        {
            switch (texFmt)
            {
            case WGPUTextureFormat_Stencil8:
            case WGPUTextureFormat_Depth24PlusStencil8:
            case WGPUTextureFormat_Depth32FloatStencil8:
                return WGPUTextureFormat_Stencil8;
            default:
                return WGPUTextureFormat_Undefined;
            }
        }
        return WGPUTextureFormat_Undefined;
    }

    inline bool AspectPresentInFormat(WGPUTextureAspect aspect, WGPUTextureFormat fmt)
    {
        if (aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_Undefined)
            return true;
        if (aspect == WGPUTextureAspect_DepthOnly)
            return HasDepthAspect(fmt);
        if (aspect == WGPUTextureAspect_StencilOnly)
            return HasStencilAspect(fmt);
        return false;
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

} // namespace pwgpu
