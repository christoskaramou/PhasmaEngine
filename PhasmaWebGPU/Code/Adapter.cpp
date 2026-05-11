#include "Adapter.h"
#include "Device.h"
#include "Instance.h"
#include "Utils.h"
#include "WGPULimits.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12RhiImpl.h"
#endif

namespace
{

#if defined(PE_WIN32)
    bool Dx12FormatSupports(ID3D12Device *device,
                            DXGI_FORMAT format,
                            D3D12_FORMAT_SUPPORT1 required1,
                            D3D12_FORMAT_SUPPORT2 required2 = D3D12_FORMAT_SUPPORT2_NONE)
    {
        if (!device || format == DXGI_FORMAT_UNKNOWN)
            return false;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = format;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
            return false;

        const UINT support1 = static_cast<UINT>(support.Support1);
        const UINT support2 = static_cast<UINT>(support.Support2);
        const UINT mask1 = static_cast<UINT>(required1);
        const UINT mask2 = static_cast<UINT>(required2);
        return (support1 & mask1) == mask1 && (support2 & mask2) == mask2;
    }

    bool Dx12FormatStorageReadWrite(ID3D12Device *device, DXGI_FORMAT format)
    {
        constexpr auto required1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURE3D |
            D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
        constexpr auto required2 = static_cast<D3D12_FORMAT_SUPPORT2>(
            D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
        return Dx12FormatSupports(device, format, required1, required2);
    }

    bool Dx12FormatRenderBlend(ID3D12Device *device, DXGI_FORMAT format)
    {
        constexpr auto required1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
            D3D12_FORMAT_SUPPORT1_BLENDABLE | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET);
        return Dx12FormatSupports(device, format, required1);
    }

    bool Dx12FormatResolve(ID3D12Device *device, DXGI_FORMAT format)
    {
        constexpr auto required1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE);
        return Dx12FormatSupports(device, format, required1);
    }

    bool Dx12FormatSampled(ID3D12Device *device, DXGI_FORMAT format)
    {
        constexpr auto required1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
        return Dx12FormatSupports(device, format, required1);
    }

    bool Dx12FormatDepth(ID3D12Device *device, DXGI_FORMAT format)
    {
        constexpr auto required1 = static_cast<D3D12_FORMAT_SUPPORT1>(
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
        return Dx12FormatSupports(device, format, required1);
    }
#endif

    bool AdapterSupportsFeature(const WGPUAdapterImpl &a, WGPUFeatureName feature)
    {
        switch (feature)
        {
        case WGPUFeatureName_CoreFeaturesAndLimits:
            return true;
        case WGPUFeatureName_TextureCompressionBC:
            return a.featureSupport.textureCompressionBC &&
                   a.textureCompressionBcFullySupported;
        case WGPUFeatureName_TextureCompressionBCSliced3D:
            return a.textureCompressionBCSliced3D;
        case WGPUFeatureName_TextureCompressionETC2:
            return a.featureSupport.textureCompressionETC2;
        case WGPUFeatureName_TextureCompressionASTC:
            return a.featureSupport.textureCompressionASTC;
        case WGPUFeatureName_TextureCompressionASTCSliced3D:
            return a.textureCompressionASTCSliced3D;
        case WGPUFeatureName_IndirectFirstInstance:
            return a.featureSupport.drawIndirectFirstInstance;
        case WGPUFeatureName_TimestampQuery:
            return a.featureSupport.timestampQuery;
        case WGPUFeatureName_DualSourceBlending:
            return a.featureSupport.dualSourceBlending;
        case WGPUFeatureName_ClipDistances:
            return a.featureSupport.shaderClipDistance;
        case WGPUFeatureName_ShaderF16:
            return a.featureSupport.shaderFloat16 && a.featureSupport.storageInputOutput16;
        case WGPUFeatureName_DepthClipControl:
            return a.featureSupport.depthClipControl;
        case WGPUFeatureName_BGRA8UnormStorage:
            return a.bgra8UnormStorage;
        case WGPUFeatureName_Float32Filterable:
            return a.float32Filterable;
        case WGPUFeatureName_RG11B10UfloatRenderable:
            return a.rg11b10UfloatRenderable;
        case WGPUFeatureName_TextureFormatsTier1:
            return a.textureFormatsTier1;
        case WGPUFeatureName_TextureFormatsTier2:
            return a.textureFormatsTier2;
        case WGPUFeatureName_Depth32FloatStencil8:
            return a.depth32FloatStencil8;
        case WGPUFeatureName_Float32Blendable:
            return a.float32Blendable;
        case WGPUFeatureName_TextureComponentSwizzle:
            return a.textureComponentSwizzle;
        case WGPUFeatureName_PrimitiveIndex:
            return a.primitiveIndex;
        case WGPUFeatureName_Subgroups:
            // Do not expose until the WGSL frontend can lower `enable subgroups`.
            return false;
        default:
            return false;
        }
    }

    void CollectSupportedFeatures(const WGPUAdapterImpl &a, std::vector<WGPUFeatureName> &out)
    {
        out.push_back(WGPUFeatureName_CoreFeaturesAndLimits);

        static constexpr WGPUFeatureName kCandidates[] = {
            WGPUFeatureName_TextureCompressionBC,
            WGPUFeatureName_TextureCompressionBCSliced3D,
            WGPUFeatureName_TextureCompressionETC2,
            WGPUFeatureName_TextureCompressionASTC,
            WGPUFeatureName_TextureCompressionASTCSliced3D,
            WGPUFeatureName_IndirectFirstInstance,
            WGPUFeatureName_TimestampQuery,
            WGPUFeatureName_ShaderF16,
            WGPUFeatureName_DepthClipControl,
            WGPUFeatureName_DualSourceBlending,
            WGPUFeatureName_ClipDistances,
            WGPUFeatureName_BGRA8UnormStorage,
            WGPUFeatureName_Float32Filterable,
            WGPUFeatureName_RG11B10UfloatRenderable,
            WGPUFeatureName_TextureFormatsTier1,
            WGPUFeatureName_TextureFormatsTier2,
            WGPUFeatureName_Depth32FloatStencil8,
            WGPUFeatureName_Float32Blendable,
            WGPUFeatureName_TextureComponentSwizzle,
            WGPUFeatureName_PrimitiveIndex,
            WGPUFeatureName_Subgroups,
        };
        for (WGPUFeatureName f : kCandidates)
        {
            if (AdapterSupportsFeature(a, f))
                out.push_back(f);
        }
    }

} // namespace

void pwgpu_PopulateAdapterFeatureCache(WGPUAdapterImpl &a)
{
    if (a.gpu)
    {
        auto fmtSupportsDepth = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        };

        // WebGPU depth24plus has an opaque representation and may be backed by
        // depth32float. Prefer that core PeFormat-compatible path until the RHI
        // grows a neutral X8_D24_UNORM_PACK32 format.
        if (fmtSupportsDepth(VK_FORMAT_D32_SFLOAT))
            a.resolvedDepth24Plus = VK_FORMAT_D32_SFLOAT;
        else if (fmtSupportsDepth(VK_FORMAT_X8_D24_UNORM_PACK32))
            a.resolvedDepth24Plus = VK_FORMAT_X8_D24_UNORM_PACK32;
        else
            a.resolvedDepth24Plus = VK_FORMAT_D32_SFLOAT;

        if (fmtSupportsDepth(VK_FORMAT_D24_UNORM_S8_UINT))
            a.resolvedDepth24PlusStencil8 = VK_FORMAT_D24_UNORM_S8_UINT;
        else
            a.resolvedDepth24PlusStencil8 = VK_FORMAT_D32_SFLOAT_S8_UINT;

        // §24.1.1 stencil8: prefer standalone S8_UINT when the driver advertises
        // depth-stencil-attachment for it (NVIDIA / desktop AMD); otherwise fall
        // back to a combined depth-stencil VkFormat (Intel iGPU does not advertise
        // S8_UINT). The WebGPU layer keeps the texel size at 1 byte and uses
        // stencil-aspect-only views/copies — the depth aspect of the combined
        // VkImage is unused storage.
        if (fmtSupportsDepth(VK_FORMAT_S8_UINT))
            a.resolvedStencil8 = VK_FORMAT_S8_UINT;
        else if (fmtSupportsDepth(VK_FORMAT_D24_UNORM_S8_UINT))
            a.resolvedStencil8 = VK_FORMAT_D24_UNORM_S8_UINT;
        else
            a.resolvedStencil8 = VK_FORMAT_D32_SFLOAT_S8_UINT;

        a.depth32FloatStencil8 = fmtSupportsDepth(VK_FORMAT_D32_SFLOAT_S8_UINT);
        a.primitiveIndex = a.featureSupport.geometryShader;

        auto fmtBlendable = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) &&
                   (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
        };
        a.float32Blendable = fmtBlendable(VK_FORMAT_R32_SFLOAT) &&
                             fmtBlendable(VK_FORMAT_R32G32_SFLOAT) &&
                             fmtBlendable(VK_FORMAT_R32G32B32A32_SFLOAT);

        VkFormatProperties bgraProps{};
        vkGetPhysicalDeviceFormatProperties(a.gpu, VK_FORMAT_B8G8R8A8_UNORM, &bgraProps);
        a.bgra8UnormStorage =
            (bgraProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;

        auto supportsLinearFilter = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures &
                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        };
        a.float32Filterable = supportsLinearFilter(VK_FORMAT_R32_SFLOAT) &&
                              supportsLinearFilter(VK_FORMAT_R32G32_SFLOAT) &&
                              supportsLinearFilter(VK_FORMAT_R32G32B32A32_SFLOAT);

        VkFormatProperties rg11Props{};
        vkGetPhysicalDeviceFormatProperties(a.gpu, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &rg11Props);
        a.rg11b10UfloatRenderable =
            (rg11Props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) &&
            (rg11Props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);

        auto fmtStorage = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
        };
        auto fmtRenderBlend = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) &&
                   (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
        };
        auto fmtResolve = [&](VkFormat fmt) -> bool
        {
            VkImageFormatProperties props{};
            VkResult r = vkGetPhysicalDeviceImageFormatProperties(
                a.gpu, fmt, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                0, &props);
            return r == VK_SUCCESS && (props.sampleCounts & VK_SAMPLE_COUNT_1_BIT);
        };
        a.textureFormatsTier1 =
            fmtStorage(VK_FORMAT_R8_UNORM) && fmtStorage(VK_FORMAT_R8_SNORM) &&
            fmtStorage(VK_FORMAT_R8_UINT) && fmtStorage(VK_FORMAT_R8_SINT) &&
            fmtStorage(VK_FORMAT_R8G8_UNORM) && fmtStorage(VK_FORMAT_R8G8_SNORM) &&
            fmtStorage(VK_FORMAT_R8G8_UINT) && fmtStorage(VK_FORMAT_R8G8_SINT) &&
            fmtStorage(VK_FORMAT_R16_UINT) && fmtStorage(VK_FORMAT_R16_SINT) &&
            fmtStorage(VK_FORMAT_R16_SFLOAT) && fmtStorage(VK_FORMAT_R16G16_UINT) &&
            fmtStorage(VK_FORMAT_R16G16_SINT) && fmtStorage(VK_FORMAT_R16G16_SFLOAT) &&
            fmtStorage(VK_FORMAT_A2B10G10R10_UINT_PACK32) &&
            fmtStorage(VK_FORMAT_A2B10G10R10_UNORM_PACK32) &&
            fmtStorage(VK_FORMAT_B10G11R11_UFLOAT_PACK32) &&
            fmtRenderBlend(VK_FORMAT_R8_SNORM) && fmtRenderBlend(VK_FORMAT_R8G8_SNORM) &&
            fmtRenderBlend(VK_FORMAT_R8G8B8A8_SNORM) &&
            fmtStorage(VK_FORMAT_R16_UNORM) && fmtStorage(VK_FORMAT_R16_SNORM) &&
            fmtStorage(VK_FORMAT_R16G16_UNORM) && fmtStorage(VK_FORMAT_R16G16_SNORM) &&
            fmtStorage(VK_FORMAT_R16G16B16A16_UNORM) &&
            fmtStorage(VK_FORMAT_R16G16B16A16_SNORM) &&
            fmtRenderBlend(VK_FORMAT_R16_UNORM) && fmtRenderBlend(VK_FORMAT_R16_SNORM) &&
            fmtRenderBlend(VK_FORMAT_R16G16_UNORM) && fmtRenderBlend(VK_FORMAT_R16G16_SNORM) &&
            fmtRenderBlend(VK_FORMAT_R16G16B16A16_UNORM) &&
            fmtRenderBlend(VK_FORMAT_R16G16B16A16_SNORM) &&
            fmtResolve(VK_FORMAT_R8_SNORM) && fmtResolve(VK_FORMAT_R8G8_SNORM) &&
            fmtResolve(VK_FORMAT_R8G8B8A8_SNORM);

        a.textureFormatsTier2 =
            a.textureFormatsTier1 &&
            fmtStorage(VK_FORMAT_R8_UNORM) && fmtStorage(VK_FORMAT_R8_UINT) &&
            fmtStorage(VK_FORMAT_R8_SINT) && fmtStorage(VK_FORMAT_R8G8B8A8_UNORM) &&
            fmtStorage(VK_FORMAT_R8G8B8A8_UINT) && fmtStorage(VK_FORMAT_R8G8B8A8_SINT) &&
            fmtStorage(VK_FORMAT_R16_UINT) && fmtStorage(VK_FORMAT_R16_SINT) &&
            fmtStorage(VK_FORMAT_R16_SFLOAT) &&
            fmtStorage(VK_FORMAT_R16G16B16A16_UINT) &&
            fmtStorage(VK_FORMAT_R16G16B16A16_SINT) &&
            fmtStorage(VK_FORMAT_R16G16B16A16_SFLOAT) &&
            fmtStorage(VK_FORMAT_R32G32B32A32_UINT) &&
            fmtStorage(VK_FORMAT_R32G32B32A32_SINT) &&
            fmtStorage(VK_FORMAT_R32G32B32A32_SFLOAT);

        // W3C spec: texture-compression-bc must support every BC VkFormat; some
        // Vulkan drivers advertise textureCompressionBC without BC6H/BC7.
        auto fmtSampled = [&](VkFormat fmt) -> bool
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(a.gpu, fmt, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        };
        a.textureCompressionBcFullySupported =
            fmtSampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC1_RGBA_SRGB_BLOCK) &&
            fmtSampled(VK_FORMAT_BC2_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC2_SRGB_BLOCK) &&
            fmtSampled(VK_FORMAT_BC3_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC3_SRGB_BLOCK) &&
            fmtSampled(VK_FORMAT_BC4_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC4_SNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC5_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC5_SNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC6H_UFLOAT_BLOCK) &&
            fmtSampled(VK_FORMAT_BC6H_SFLOAT_BLOCK) &&
            fmtSampled(VK_FORMAT_BC7_UNORM_BLOCK) &&
            fmtSampled(VK_FORMAT_BC7_SRGB_BLOCK);

        auto supports3DSampled = [&](VkFormat fmt) -> bool
        {
            VkImageFormatProperties props{};
            VkResult r = vkGetPhysicalDeviceImageFormatProperties(
                a.gpu, fmt, VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT, 0, &props);
            return r == VK_SUCCESS;
        };
        a.textureCompressionBCSliced3D =
            a.featureSupport.textureCompressionBC &&
            a.textureCompressionBcFullySupported &&
            supports3DSampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        a.textureCompressionASTCSliced3D =
            a.featureSupport.textureCompressionASTC &&
            supports3DSampled(VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
    }
#if defined(PE_WIN32)
    else if (a.rhi && a.rhi->GetApi() == PE_GRAPHICS_API_DX12)
    {
        auto *dx12 = static_cast<pe::Dx12RhiImpl *>(a.rhi->GetImpl());
        ID3D12Device *device = dx12 ? dx12->GetDevice() : nullptr;

        a.depth32FloatStencil8 = Dx12FormatDepth(device, DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
        a.resolvedDepth24Plus = VK_FORMAT_D32_SFLOAT;
        a.resolvedDepth24PlusStencil8 =
            Dx12FormatDepth(device, DXGI_FORMAT_D24_UNORM_S8_UINT)
                ? VK_FORMAT_D24_UNORM_S8_UINT
                : VK_FORMAT_D32_SFLOAT_S8_UINT;
        a.resolvedStencil8 =
            Dx12FormatDepth(device, DXGI_FORMAT_D24_UNORM_S8_UINT)
                ? VK_FORMAT_D24_UNORM_S8_UINT
                : VK_FORMAT_D32_SFLOAT_S8_UINT;
        a.float32Blendable = Dx12FormatRenderBlend(device, DXGI_FORMAT_R32_FLOAT) &&
                             Dx12FormatRenderBlend(device, DXGI_FORMAT_R32G32_FLOAT) &&
                             Dx12FormatRenderBlend(device, DXGI_FORMAT_R32G32B32A32_FLOAT);
        a.bgra8UnormStorage = Dx12FormatStorageReadWrite(device, DXGI_FORMAT_B8G8R8A8_UNORM);
        a.float32Filterable = Dx12FormatSampled(device, DXGI_FORMAT_R32_FLOAT) &&
                              Dx12FormatSampled(device, DXGI_FORMAT_R32G32_FLOAT) &&
                              Dx12FormatSampled(device, DXGI_FORMAT_R32G32B32A32_FLOAT);
        a.rg11b10UfloatRenderable = Dx12FormatRenderBlend(device, DXGI_FORMAT_R11G11B10_FLOAT);

        a.textureFormatsTier1 =
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_SNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8_SNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_FLOAT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16_FLOAT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R10G10B10A2_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R10G10B10A2_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R11G11B10_FLOAT) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R8_SNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R8G8_SNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R8G8B8A8_SNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_SNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16_SNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16B16A16_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16B16A16_SNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16_UNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16_SNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16G16_UNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16G16_SNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16G16B16A16_UNORM) &&
            Dx12FormatRenderBlend(device, DXGI_FORMAT_R16G16B16A16_SNORM) &&
            Dx12FormatResolve(device, DXGI_FORMAT_R8_SNORM) &&
            Dx12FormatResolve(device, DXGI_FORMAT_R8G8_SNORM) &&
            Dx12FormatResolve(device, DXGI_FORMAT_R8G8B8A8_SNORM);

        a.textureFormatsTier2 =
            a.textureFormatsTier1 &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8B8A8_UNORM) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8B8A8_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R8G8B8A8_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16_FLOAT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16B16A16_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16B16A16_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R32G32B32A32_UINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R32G32B32A32_SINT) &&
            Dx12FormatStorageReadWrite(device, DXGI_FORMAT_R32G32B32A32_FLOAT);

        a.textureCompressionBcFullySupported =
            a.featureSupport.textureCompressionBC &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC1_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC1_UNORM_SRGB) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC2_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC2_UNORM_SRGB) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC3_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC3_UNORM_SRGB) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC4_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC4_SNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC5_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC5_SNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC6H_UF16) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC6H_SF16) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC7_UNORM) &&
            Dx12FormatSampled(device, DXGI_FORMAT_BC7_UNORM_SRGB);
        a.textureCompressionBCSliced3D =
            a.textureCompressionBcFullySupported &&
            Dx12FormatSupports(device, DXGI_FORMAT_BC1_UNORM, D3D12_FORMAT_SUPPORT1_TEXTURE3D);
        a.textureCompressionASTCSliced3D = false;
        // D3D12 pixel shaders can consume SV_PrimitiveID directly, which is
        // enough for WebGPU's optional primitive-index feature.
        a.primitiveIndex = true;
    }
#endif
    else
    {
        a.textureCompressionBcFullySupported = a.featureSupport.textureCompressionBC;
        a.primitiveIndex = a.featureSupport.geometryShader;
    }

    a.supportedFeatures.clear();
    CollectSupportedFeatures(a, a.supportedFeatures);
}

extern "C"
{

    void wgpuAdapterAddRef(WGPUAdapter adapter)
    {
        if (adapter)
            adapter->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuAdapterRelease(WGPUAdapter adapter)
    {
        if (!adapter)
            return;
        if (adapter->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            WGPUInstance inst = adapter->instance;
            delete adapter;
            if (inst)
                wgpuInstanceRelease(inst);
        }
    }

    void wgpuAdapterGetFeatures(WGPUAdapter adapter, WGPUSupportedFeatures *features)
    {
        if (!features)
            return;
        features->featureCount = 0;
        features->features = nullptr;
        if (!adapter)
            return;

        if (adapter->supportedFeatures.empty())
            pwgpu_PopulateAdapterFeatureCache(*adapter);

        features->featureCount = adapter->supportedFeatures.size();
        features->features = adapter->supportedFeatures.empty()
                                 ? nullptr
                                 : adapter->supportedFeatures.data();
    }

    WGPUStatus wgpuAdapterGetInfo(WGPUAdapter adapter, WGPUAdapterInfo *info)
    {
        if (!adapter || !info)
            return WGPUStatus_Error;

        info->nextInChain = nullptr;
        info->vendor = pwgpu::ToStringView(adapter->vendorName);
        info->architecture = pwgpu::ToStringView(adapter->architecture);
        info->device = pwgpu::ToStringView(adapter->deviceName);
        info->description = pwgpu::ToStringView(adapter->driverDescription);
        info->backendType = adapter->backendType;
        info->adapterType = adapter->adapterType;
        info->vendorID = adapter->adapterInfo.vendorId;
        info->deviceID = adapter->adapterInfo.deviceId;
        info->subgroupMinSize = adapter->chainedCaps.subgroupMinSize;
        info->subgroupMaxSize = adapter->chainedCaps.subgroupMaxSize;

        return WGPUStatus_Success;
    }

    WGPUStatus wgpuAdapterGetLimits(WGPUAdapter adapter, WGPULimits *limits)
    {
        if (!adapter || !adapter->rhi || !limits)
            return WGPUStatus_Error;
        pwgpu::FillLimitsFromCore(*limits, adapter->rhi->GetGpuLimits());
        return WGPUStatus_Success;
    }

    WGPUBool wgpuAdapterHasFeature(WGPUAdapter adapter, WGPUFeatureName feature)
    {
        if (!adapter)
            return WGPU_FALSE;
        return AdapterSupportsFeature(*adapter, feature) ? WGPU_TRUE : WGPU_FALSE;
    }

    WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter adapter,
                                        WGPUDeviceDescriptor const *descriptor,
                                        WGPURequestDeviceCallbackInfo callbackInfo)
    {
        WGPUInstanceImpl *inst = adapter ? adapter->instance : nullptr;

        // Helper: register an error callback into the instance's future registry.
        auto trackError = [&](const char *msg) -> WGPUFuture
        {
            if (!inst || !callbackInfo.callback)
                return inst ? inst->futures.NextId() : WGPUFuture{0};
            auto cb = callbackInfo.callback;
            auto u1 = callbackInfo.userdata1;
            auto u2 = callbackInfo.userdata2;
            std::string captured(msg);
            return inst->futures.TrackEvent(callbackInfo.mode,
                                            [cb, u1, u2, captured]()
                                            {
                                                WGPUStringView sv{captured.c_str(), captured.size()};
                                                cb(WGPURequestDeviceStatus_Error, nullptr, sv, u1, u2);
                                            });
        };

        if (!adapter || adapter->consumed)
            return trackError("PhasmaWebGPU: adapter is null, expired, or already consumed by a prior requestDevice");
        if (!adapter->rhi)
            return trackError("PhasmaWebGPU: adapter has no backing pe::RHI");

        if (adapter->supportedFeatures.empty())
            pwgpu_PopulateAdapterFeatureCache(*adapter);

        if (descriptor && descriptor->requiredFeatureCount > 0 && descriptor->requiredFeatures)
        {
            for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i)
            {
                WGPUFeatureName req = descriptor->requiredFeatures[i];
                if (!AdapterSupportsFeature(*adapter, req))
                    return trackError("PhasmaWebGPU: requiredFeatures contains a feature not supported by this adapter");
            }
        }

        WGPULimits adapterLim{};
        pwgpu::FillLimitsFromCore(adapterLim, adapter->rhi->GetGpuLimits());

        if (descriptor && descriptor->requiredLimits)
        {
            std::string bad;
            if (pwgpu::ValidateRequestedLimits(adapterLim, *descriptor->requiredLimits, bad) != WGPUStatus_Success)
            {
                std::string what = "PhasmaWebGPU: requiredLimits violation on '" + bad + "'";
                return trackError(what.c_str());
            }
        }

        const bool canFulfill = adapter->rhi && adapter->rhi->GetMainQueue();

        auto *dev = new WGPUDeviceImpl();
        dev->instance = inst;
        if (inst)
            wgpuInstanceAddRef(inst);
        dev->rhi = adapter->rhi;
        dev->peQueue = canFulfill ? adapter->rhi->GetMainQueue() : nullptr;

        auto appendFeatureUnique = [](std::vector<WGPUFeatureName> &v, WGPUFeatureName f)
        {
            for (WGPUFeatureName existing : v)
                if (existing == f)
                    return;
            v.push_back(f);
        };
        if (descriptor && descriptor->requiredFeatureCount > 0 && descriptor->requiredFeatures)
        {
            for (size_t i = 0; i < descriptor->requiredFeatureCount; ++i)
                appendFeatureUnique(dev->features, descriptor->requiredFeatures[i]);
        }
        appendFeatureUnique(dev->features, WGPUFeatureName_CoreFeaturesAndLimits);

        auto deviceHas = [&](WGPUFeatureName f)
        {
            for (WGPUFeatureName existing : dev->features)
                if (existing == f)
                    return true;
            return false;
        };
        if (deviceHas(WGPUFeatureName_TextureFormatsTier2))
            appendFeatureUnique(dev->features, WGPUFeatureName_TextureFormatsTier1);
        if (deviceHas(WGPUFeatureName_TextureFormatsTier1))
            appendFeatureUnique(dev->features, WGPUFeatureName_RG11B10UfloatRenderable);

        dev->limits = pwgpu::ResolveDeviceLimits(adapterLim,
                                                 descriptor ? descriptor->requiredLimits : nullptr);

        dev->adapterVendor = adapter->vendorName;
        dev->adapterArchitecture = adapter->architecture;
        dev->adapterDeviceName = adapter->deviceName;
        dev->adapterDescription = adapter->driverDescription;
        dev->adapterType = adapter->adapterType;
        dev->adapterBackend = adapter->backendType;
        dev->adapterVendorID = adapter->adapterInfo.vendorId;
        dev->adapterDeviceID = adapter->adapterInfo.deviceId;
        dev->resolvedDepth24Plus = adapter->resolvedDepth24Plus;
        dev->resolvedDepth24PlusStencil8 = adapter->resolvedDepth24PlusStencil8;
        dev->resolvedStencil8 = adapter->resolvedStencil8;
        dev->supportsDepthClamp = adapter->featureSupport.depthClamp;
        dev->supportsDepthClipEnable = adapter->featureSupport.depthClipControl;

        if (descriptor)
        {
            if (descriptor->label.data)
                dev->label = pwgpu::ToString(descriptor->label);
            if (descriptor->deviceLostCallbackInfo.callback)
                dev->deviceLostCallbackInfo = descriptor->deviceLostCallbackInfo;
            if (descriptor->uncapturedErrorCallbackInfo.callback)
                dev->uncapturedErrorCallbackInfo = descriptor->uncapturedErrorCallbackInfo;
        }

        adapter->consumed = true;

        if (!canFulfill)
        {
            // Device is born lost: success callback must fire BEFORE the lost
            // callback so the consumer receives its handle before the loss notification.
            // Born-lost fires immediately regardless of mode — spec requires ordered delivery.
            dev->destroyed = true;
            try
            {
                dev->lostPromise.set_value();
            }
            catch (const std::future_error &)
            {
            }
            if (callbackInfo.callback)
                callbackInfo.callback(WGPURequestDeviceStatus_Success, dev, {nullptr, 0},
                                      callbackInfo.userdata1, callbackInfo.userdata2);
            if (dev->deviceLostCallbackInfo.callback)
            {
                WGPUStringView lostMsg = pwgpu::ToStringView(
                    "PhasmaWebGPU: adapter has no backing pe::RHI device; device lost at creation");
                WGPUDevice selfHandle = dev;
                dev->deviceLostCallbackInfo.callback(&selfHandle,
                                                     WGPUDeviceLostReason_Unknown,
                                                     lostMsg,
                                                     dev->deviceLostCallbackInfo.userdata1,
                                                     dev->deviceLostCallbackInfo.userdata2);
            }
            return inst->futures.NextId();
        }

        dev->queue = new WGPUQueueImpl();
        dev->queue->device = dev;
        dev->queue->peQueue = dev->peQueue;

        if (!inst || !callbackInfo.callback)
            return inst->futures.NextId();

        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        return inst->futures.TrackEvent(callbackInfo.mode,
                                        [cb, u1, u2, dev]()
                                        {
                                            cb(WGPURequestDeviceStatus_Success, dev, {nullptr, 0}, u1, u2);
                                        });
    }

} // extern "C"
