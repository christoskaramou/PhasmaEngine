#pragma once

#include "API/RHITypes.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // PeFormat <-> WGPUTextureFormat. Three-channel formats have no WebGPU texture format and map to Undefined.
    inline WGPUTextureFormat ToWgpuFormat(::PeFormat format)
    {
        switch (format)
        {
            // clang-format off
        case PE_FORMAT_R8_UNORM: return WGPUTextureFormat_R8Unorm;
        case PE_FORMAT_R8_SNORM: return WGPUTextureFormat_R8Snorm;
        case PE_FORMAT_R8_UINT: return WGPUTextureFormat_R8Uint;
        case PE_FORMAT_R8_SINT: return WGPUTextureFormat_R8Sint;
        case PE_FORMAT_R16_UNORM: return WGPUTextureFormat_R16Unorm;
        case PE_FORMAT_R16_SNORM: return WGPUTextureFormat_R16Snorm;
        case PE_FORMAT_R16_UINT: return WGPUTextureFormat_R16Uint;
        case PE_FORMAT_R16_SINT: return WGPUTextureFormat_R16Sint;
        case PE_FORMAT_R16_SFLOAT: return WGPUTextureFormat_R16Float;
        case PE_FORMAT_R8G8_UNORM: return WGPUTextureFormat_RG8Unorm;
        case PE_FORMAT_R8G8_SNORM: return WGPUTextureFormat_RG8Snorm;
        case PE_FORMAT_R8G8_UINT: return WGPUTextureFormat_RG8Uint;
        case PE_FORMAT_R8G8_SINT: return WGPUTextureFormat_RG8Sint;
        case PE_FORMAT_R32_SFLOAT: return WGPUTextureFormat_R32Float;
        case PE_FORMAT_R32_UINT: return WGPUTextureFormat_R32Uint;
        case PE_FORMAT_R32_SINT: return WGPUTextureFormat_R32Sint;
        case PE_FORMAT_R16G16_UNORM: return WGPUTextureFormat_RG16Unorm;
        case PE_FORMAT_R16G16_SNORM: return WGPUTextureFormat_RG16Snorm;
        case PE_FORMAT_R16G16_UINT: return WGPUTextureFormat_RG16Uint;
        case PE_FORMAT_R16G16_SINT: return WGPUTextureFormat_RG16Sint;
        case PE_FORMAT_R16G16_SFLOAT: return WGPUTextureFormat_RG16Float;
        case PE_FORMAT_R8G8B8A8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
        case PE_FORMAT_R8G8B8A8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
        case PE_FORMAT_R8G8B8A8_SNORM: return WGPUTextureFormat_RGBA8Snorm;
        case PE_FORMAT_R8G8B8A8_UINT: return WGPUTextureFormat_RGBA8Uint;
        case PE_FORMAT_R8G8B8A8_SINT: return WGPUTextureFormat_RGBA8Sint;
        case PE_FORMAT_B8G8R8A8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
        case PE_FORMAT_B8G8R8A8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
        case PE_FORMAT_A2B10G10R10_UNORM_PACK32: return WGPUTextureFormat_RGB10A2Unorm;
        case PE_FORMAT_A2B10G10R10_UINT_PACK32: return WGPUTextureFormat_RGB10A2Uint;
        case PE_FORMAT_B10G11R11_UFLOAT_PACK32: return WGPUTextureFormat_RG11B10Ufloat;
        case PE_FORMAT_R32G32_SFLOAT: return WGPUTextureFormat_RG32Float;
        case PE_FORMAT_R32G32_UINT: return WGPUTextureFormat_RG32Uint;
        case PE_FORMAT_R32G32_SINT: return WGPUTextureFormat_RG32Sint;
        case PE_FORMAT_R16G16B16A16_UNORM: return WGPUTextureFormat_RGBA16Unorm;
        case PE_FORMAT_R16G16B16A16_SNORM: return WGPUTextureFormat_RGBA16Snorm;
        case PE_FORMAT_R16G16B16A16_UINT: return WGPUTextureFormat_RGBA16Uint;
        case PE_FORMAT_R16G16B16A16_SINT: return WGPUTextureFormat_RGBA16Sint;
        case PE_FORMAT_R16G16B16A16_SFLOAT: return WGPUTextureFormat_RGBA16Float;
        case PE_FORMAT_R32G32B32A32_SFLOAT: return WGPUTextureFormat_RGBA32Float;
        case PE_FORMAT_R32G32B32A32_UINT: return WGPUTextureFormat_RGBA32Uint;
        case PE_FORMAT_R32G32B32A32_SINT: return WGPUTextureFormat_RGBA32Sint;
        case PE_FORMAT_S8_UINT: return WGPUTextureFormat_Stencil8;
        case PE_FORMAT_D32_SFLOAT: return WGPUTextureFormat_Depth32Float;
        case PE_FORMAT_D24_UNORM_S8_UINT: return WGPUTextureFormat_Depth24PlusStencil8;
        case PE_FORMAT_D32_SFLOAT_S8_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
        case PE_FORMAT_BC1_RGBA_UNORM: return WGPUTextureFormat_BC1RGBAUnorm;
        case PE_FORMAT_BC1_RGBA_SRGB: return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case PE_FORMAT_BC2_UNORM: return WGPUTextureFormat_BC2RGBAUnorm;
        case PE_FORMAT_BC2_SRGB: return WGPUTextureFormat_BC2RGBAUnormSrgb;
        case PE_FORMAT_BC3_UNORM: return WGPUTextureFormat_BC3RGBAUnorm;
        case PE_FORMAT_BC3_SRGB: return WGPUTextureFormat_BC3RGBAUnormSrgb;
        case PE_FORMAT_BC4_UNORM: return WGPUTextureFormat_BC4RUnorm;
        case PE_FORMAT_BC4_SNORM: return WGPUTextureFormat_BC4RSnorm;
        case PE_FORMAT_BC5_UNORM: return WGPUTextureFormat_BC5RGUnorm;
        case PE_FORMAT_BC5_SNORM: return WGPUTextureFormat_BC5RGSnorm;
        case PE_FORMAT_BC6H_UFLOAT: return WGPUTextureFormat_BC6HRGBUfloat;
        case PE_FORMAT_BC6H_SFLOAT: return WGPUTextureFormat_BC6HRGBFloat;
        case PE_FORMAT_BC7_UNORM: return WGPUTextureFormat_BC7RGBAUnorm;
        case PE_FORMAT_BC7_SRGB: return WGPUTextureFormat_BC7RGBAUnormSrgb;
        // clang-format on
        default:
            return WGPUTextureFormat_Undefined;
        }
    }

    struct WgpuTexelBlock
    {
        uint32_t width = 1; // texels per block edge: 4 for BC, 1 otherwise
        uint32_t bytes = 0; // bytes per block; 0 when the format cannot be copied (depth-stencil, stencil)
    };

    // Copy footprint of one texel block, for buffer <-> texture copies (bytesPerRow).
    inline WgpuTexelBlock TexelBlockOf(WGPUTextureFormat format)
    {
        switch (format)
        {
            // clang-format off
        case WGPUTextureFormat_R8Unorm: case WGPUTextureFormat_R8Snorm: case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint: case WGPUTextureFormat_Stencil8:
            return {1, 1};
        case WGPUTextureFormat_R16Unorm: case WGPUTextureFormat_R16Snorm: case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint: case WGPUTextureFormat_R16Float: case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm: case WGPUTextureFormat_RG8Uint: case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_Depth16Unorm:
            return {1, 2};
        case WGPUTextureFormat_R32Float: case WGPUTextureFormat_R32Uint: case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG16Unorm: case WGPUTextureFormat_RG16Snorm: case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint: case WGPUTextureFormat_RG16Float: case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb: case WGPUTextureFormat_RGBA8Snorm: case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint: case WGPUTextureFormat_BGRA8Unorm: case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Uint: case WGPUTextureFormat_RGB10A2Unorm: case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat: case WGPUTextureFormat_Depth32Float:
            return {1, 4};
        case WGPUTextureFormat_RG32Float: case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA16Unorm: case WGPUTextureFormat_RGBA16Snorm: case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint: case WGPUTextureFormat_RGBA16Float:
            return {1, 8};
        case WGPUTextureFormat_RGBA32Float: case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint:
            return {1, 16};
        case WGPUTextureFormat_BC1RGBAUnorm: case WGPUTextureFormat_BC1RGBAUnormSrgb: case WGPUTextureFormat_BC4RUnorm:
        case WGPUTextureFormat_BC4RSnorm:
            return {4, 8};
        case WGPUTextureFormat_BC2RGBAUnorm: case WGPUTextureFormat_BC2RGBAUnormSrgb: case WGPUTextureFormat_BC3RGBAUnorm:
        case WGPUTextureFormat_BC3RGBAUnormSrgb: case WGPUTextureFormat_BC5RGUnorm: case WGPUTextureFormat_BC5RGSnorm:
        case WGPUTextureFormat_BC6HRGBUfloat: case WGPUTextureFormat_BC6HRGBFloat: case WGPUTextureFormat_BC7RGBAUnorm:
        case WGPUTextureFormat_BC7RGBAUnormSrgb:
            return {4, 16};
        // clang-format on
        default:
            return {};
        }
    }

    inline bool IsDepthOrStencil(WGPUTextureFormat format)
    {
        return format == WGPUTextureFormat_Stencil8 ||
               format == WGPUTextureFormat_Depth16Unorm ||
               format == WGPUTextureFormat_Depth24Plus ||
               format == WGPUTextureFormat_Depth24PlusStencil8 ||
               format == WGPUTextureFormat_Depth32Float ||
               format == WGPUTextureFormat_Depth32FloatStencil8;
    }

    // Formats the WGSL blit can render into and sample with a filtering sampler: color-renderable and float-sampleable.
    // The 32-bit float formats filter only with the float32-filterable feature.
    inline bool IsBlittable(WGPUTextureFormat format, bool float32Filterable)
    {
        switch (format)
        {
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGB10A2Unorm:
            return true;
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RGBA32Float:
            return float32Filterable;
        default:
            return false;
        }
    }

    // Only the canvas formats come back from WebGPU (wgpuSurfaceGetCapabilities).
    inline ::PeFormat FromWgpuSurfaceFormat(WGPUTextureFormat format)
    {
        switch (format)
        {
        case WGPUTextureFormat_BGRA8Unorm:
            return PE_FORMAT_B8G8R8A8_UNORM;
        case WGPUTextureFormat_RGBA8Unorm:
            return PE_FORMAT_R8G8B8A8_UNORM;
        case WGPUTextureFormat_RGBA16Float:
            return PE_FORMAT_R16G16B16A16_SFLOAT;
        default:
            return PE_FORMAT_UNDEFINED;
        }
    }
} // namespace pe
