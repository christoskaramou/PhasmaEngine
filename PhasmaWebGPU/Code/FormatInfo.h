#pragma once

#include <webgpu/webgpu.h>

#include <cstddef>

namespace pwgpu
{

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

    inline bool IsStencilOnlyFormat(WGPUTextureFormat f)
    {
        return HasStencilAspect(f) && !HasDepthAspect(f);
    }

    inline bool IsDepthOnlyFormat(WGPUTextureFormat f)
    {
        return HasDepthAspect(f) && !HasStencilAspect(f);
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
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
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
        // Tier1 formats: not multisampleable in base spec (gated by SupportsMultisamplingTier1).
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return false;
        default:
            return f != WGPUTextureFormat_Undefined;
        }
    }

    inline bool SupportsResolve(WGPUTextureFormat f)
    {
        if (!SupportsMultisampling(f))
            return false;
        switch (f)
        {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RGB10A2Uint:
            return false;
        default:
            return true;
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

    inline bool IsRenderableFormatTier1(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return true;
        default:
            return false;
        }
    }

    inline bool SupportsStorageBindingTier1(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return true;
        default:
            return false;
        }
    }

    inline bool SupportsMultisamplingTier1(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return true;
        default:
            return false;
        }
    }

    inline bool SupportsResolveTier1(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RGBA8Snorm:
            return true;
        default:
            return false;
        }
    }

    inline bool SupportsReadWriteStorageTier2(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_RGBA32Float:
            return true;
        default:
            return false;
        }
    }

    inline bool Supports3DTexture(WGPUTextureFormat f)
    {
        if (IsDepthStencilFormat(f))
            return false;
        if (IsETC2Format(f))
            return false;
        return f != WGPUTextureFormat_Undefined;
    }

    enum class ColorSampleType
    {
        Float,
        Uint,
        Sint,
        Unknown
    };

    inline ColorSampleType GetColorFormatSampleType(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA32Uint:
            return ColorSampleType::Uint;
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA32Sint:
            return ColorSampleType::Sint;
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return ColorSampleType::Float;
        case WGPUTextureFormat_RGB10A2Uint:
            return ColorSampleType::Uint;
        default:
            return ColorSampleType::Unknown;
        }
    }

    inline uint32_t GetColorFormatChannelCount(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
            return 1;
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
            return 2;
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat:
            return 3;
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RGB10A2Uint:
            return 4;
        default:
            return 0;
        }
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

    inline uint32_t TexelBlockCopyFootprint(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_Stencil8:
            return 1;
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_Depth16Unorm:
            return 2;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth24Plus:
            return 4;
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
        case WGPUTextureFormat_BC1RGBAUnorm:
        case WGPUTextureFormat_BC1RGBAUnormSrgb:
        case WGPUTextureFormat_BC4RUnorm:
        case WGPUTextureFormat_BC4RSnorm:
        case WGPUTextureFormat_ETC2RGB8Unorm:
        case WGPUTextureFormat_ETC2RGB8UnormSrgb:
        case WGPUTextureFormat_ETC2RGB8A1Unorm:
        case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
        case WGPUTextureFormat_EACR11Unorm:
        case WGPUTextureFormat_EACR11Snorm:
            return 8;
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_BC2RGBAUnorm:
        case WGPUTextureFormat_BC2RGBAUnormSrgb:
        case WGPUTextureFormat_BC3RGBAUnorm:
        case WGPUTextureFormat_BC3RGBAUnormSrgb:
        case WGPUTextureFormat_BC5RGUnorm:
        case WGPUTextureFormat_BC5RGSnorm:
        case WGPUTextureFormat_BC6HRGBUfloat:
        case WGPUTextureFormat_BC6HRGBFloat:
        case WGPUTextureFormat_BC7RGBAUnorm:
        case WGPUTextureFormat_BC7RGBAUnormSrgb:
        case WGPUTextureFormat_ETC2RGBA8Unorm:
        case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
        case WGPUTextureFormat_EACRG11Unorm:
        case WGPUTextureFormat_EACRG11Snorm:
            return 16;
        case WGPUTextureFormat_ASTC4x4Unorm:
        case WGPUTextureFormat_ASTC4x4UnormSrgb:
        case WGPUTextureFormat_ASTC5x4Unorm:
        case WGPUTextureFormat_ASTC5x4UnormSrgb:
        case WGPUTextureFormat_ASTC5x5Unorm:
        case WGPUTextureFormat_ASTC5x5UnormSrgb:
        case WGPUTextureFormat_ASTC6x5Unorm:
        case WGPUTextureFormat_ASTC6x5UnormSrgb:
        case WGPUTextureFormat_ASTC6x6Unorm:
        case WGPUTextureFormat_ASTC6x6UnormSrgb:
        case WGPUTextureFormat_ASTC8x5Unorm:
        case WGPUTextureFormat_ASTC8x5UnormSrgb:
        case WGPUTextureFormat_ASTC8x6Unorm:
        case WGPUTextureFormat_ASTC8x6UnormSrgb:
        case WGPUTextureFormat_ASTC8x8Unorm:
        case WGPUTextureFormat_ASTC8x8UnormSrgb:
        case WGPUTextureFormat_ASTC10x5Unorm:
        case WGPUTextureFormat_ASTC10x5UnormSrgb:
        case WGPUTextureFormat_ASTC10x6Unorm:
        case WGPUTextureFormat_ASTC10x6UnormSrgb:
        case WGPUTextureFormat_ASTC10x8Unorm:
        case WGPUTextureFormat_ASTC10x8UnormSrgb:
        case WGPUTextureFormat_ASTC10x10Unorm:
        case WGPUTextureFormat_ASTC10x10UnormSrgb:
        case WGPUTextureFormat_ASTC12x10Unorm:
        case WGPUTextureFormat_ASTC12x10UnormSrgb:
        case WGPUTextureFormat_ASTC12x12Unorm:
        case WGPUTextureFormat_ASTC12x12UnormSrgb:
            return 16;
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return 0;
        default:
            return 0;
        }
    }

    inline uint32_t TexelBlockCopyFootprint(WGPUTextureFormat f, WGPUTextureAspect aspect)
    {
        if (aspect == WGPUTextureAspect_StencilOnly)
            return 1;
        if (aspect == WGPUTextureAspect_DepthOnly)
        {
            switch (f)
            {
            case WGPUTextureFormat_Depth16Unorm:
                return 2;
            case WGPUTextureFormat_Depth24Plus:
            case WGPUTextureFormat_Depth24PlusStencil8:
            case WGPUTextureFormat_Depth32Float:
            case WGPUTextureFormat_Depth32FloatStencil8:
                return 4;
            default:
                break;
            }
        }
        return TexelBlockCopyFootprint(f);
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

    // ---- Vertex format byte sizes (section 10.3.7.1) ----

    inline uint32_t VertexFormatByteSize(WGPUVertexFormat f)
    {
        switch (f)
        {
        case WGPUVertexFormat_Uint8:
        case WGPUVertexFormat_Sint8:
        case WGPUVertexFormat_Unorm8:
        case WGPUVertexFormat_Snorm8:
            return 1;
        case WGPUVertexFormat_Uint8x2:
        case WGPUVertexFormat_Sint8x2:
        case WGPUVertexFormat_Unorm8x2:
        case WGPUVertexFormat_Snorm8x2:
        case WGPUVertexFormat_Uint16:
        case WGPUVertexFormat_Sint16:
        case WGPUVertexFormat_Unorm16:
        case WGPUVertexFormat_Snorm16:
        case WGPUVertexFormat_Float16:
            return 2;
        case WGPUVertexFormat_Uint8x4:
        case WGPUVertexFormat_Sint8x4:
        case WGPUVertexFormat_Unorm8x4:
        case WGPUVertexFormat_Snorm8x4:
        case WGPUVertexFormat_Uint16x2:
        case WGPUVertexFormat_Sint16x2:
        case WGPUVertexFormat_Unorm16x2:
        case WGPUVertexFormat_Snorm16x2:
        case WGPUVertexFormat_Float16x2:
        case WGPUVertexFormat_Float32:
        case WGPUVertexFormat_Uint32:
        case WGPUVertexFormat_Sint32:
        case WGPUVertexFormat_Unorm10_10_10_2:
        case WGPUVertexFormat_Unorm8x4BGRA:
            return 4;
        case WGPUVertexFormat_Uint16x4:
        case WGPUVertexFormat_Sint16x4:
        case WGPUVertexFormat_Unorm16x4:
        case WGPUVertexFormat_Snorm16x4:
        case WGPUVertexFormat_Float16x4:
        case WGPUVertexFormat_Float32x2:
        case WGPUVertexFormat_Uint32x2:
        case WGPUVertexFormat_Sint32x2:
            return 8;
        case WGPUVertexFormat_Float32x3:
        case WGPUVertexFormat_Uint32x3:
        case WGPUVertexFormat_Sint32x3:
            return 12;
        case WGPUVertexFormat_Float32x4:
        case WGPUVertexFormat_Uint32x4:
        case WGPUVertexFormat_Sint32x4:
            return 16;
        default:
            return 0;
        }
    }

    inline bool IsStripTopology(WGPUPrimitiveTopology t)
    {
        return t == WGPUPrimitiveTopology_LineStrip || t == WGPUPrimitiveTopology_TriangleStrip;
    }

    inline bool IsLineOrPointTopology(WGPUPrimitiveTopology t)
    {
        return t == WGPUPrimitiveTopology_PointList || t == WGPUPrimitiveTopology_LineList || t == WGPUPrimitiveTopology_LineStrip;
    }

    inline bool IsBlendableFormat(WGPUTextureFormat f, bool float32BlendableEnabled = false,
                                  bool tier1Enabled = false)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
            return false;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RGBA32Float:
            return float32BlendableEnabled;
        default:
            return IsRenderableFormat(f) || (tier1Enabled && IsRenderableFormatTier1(f));
        }
    }

    inline bool FormatHasAlphaChannel(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
            return true;
        default:
            return false;
        }
    }

    inline uint32_t FormatBytesPerSample(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
            return 1;
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
            return 2;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat:
            return 4;
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return 8;
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
            return 16;
        default:
            return 0;
        }
    }

    struct RenderTargetCost
    {
        uint32_t byteCost;
        uint32_t alignment;
    };

    inline RenderTargetCost RenderTargetByteCost(WGPUTextureFormat f)
    {
        switch (f)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
            return {1, 1};
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_R16Unorm:
        case WGPUTextureFormat_R16Snorm:
            return {2, 2};
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
            return {2, 1};
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_R32Float:
            return {4, 4};
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RG16Unorm:
        case WGPUTextureFormat_RG16Snorm:
            return {4, 2};
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return {8, 1};
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
            return {4, 1};
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
            return {8, 4};
        case WGPUTextureFormat_RG11B10Ufloat:
            return {8, 4};
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RG32Float:
            return {8, 4};
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Unorm:
        case WGPUTextureFormat_RGBA16Snorm:
            return {8, 2};
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
        case WGPUTextureFormat_RGBA32Float:
            return {16, 4};
        default:
            return {0, 1};
        }
    }

    inline uint32_t ComputeBytesPerSampleFromFormats(const WGPUTextureFormat *formats, size_t count)
    {
        uint32_t bytes = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (formats[i] == WGPUTextureFormat_Undefined)
                continue;
            auto rc = RenderTargetByteCost(formats[i]);
            uint32_t align = rc.alignment ? rc.alignment : 1;
            bytes = ((bytes + align - 1) / align) * align;
            bytes += rc.byteCost;
        }
        return bytes;
    }

} // namespace pwgpu
