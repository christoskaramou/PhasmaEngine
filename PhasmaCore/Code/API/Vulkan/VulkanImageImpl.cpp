#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Debug.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/StagingManager.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#endif

namespace pe
{
    vk::Format ToVkFormat(::PeFormat format)
    {
        switch (format)
        {
        case PE_FORMAT_UNDEFINED:
            return vk::Format::eUndefined;
        case PE_FORMAT_R8_UNORM:
            return vk::Format::eR8Unorm;
        case PE_FORMAT_R8_SINT:
            return vk::Format::eR8Sint;
        case PE_FORMAT_R8_UINT:
            return vk::Format::eR8Uint;
        case PE_FORMAT_R16_SFLOAT:
            return vk::Format::eR16Sfloat;
        case PE_FORMAT_R16_SINT:
            return vk::Format::eR16Sint;
        case PE_FORMAT_R16_UINT:
            return vk::Format::eR16Uint;
        case PE_FORMAT_R32_SFLOAT:
            return vk::Format::eR32Sfloat;
        case PE_FORMAT_R32_SINT:
            return vk::Format::eR32Sint;
        case PE_FORMAT_R32_UINT:
            return vk::Format::eR32Uint;
        case PE_FORMAT_R16G16_SFLOAT:
            return vk::Format::eR16G16Sfloat;
        case PE_FORMAT_R32G32_SFLOAT:
            return vk::Format::eR32G32Sfloat;
        case PE_FORMAT_R32G32_SINT:
            return vk::Format::eR32G32Sint;
        case PE_FORMAT_R32G32_UINT:
            return vk::Format::eR32G32Uint;
        case PE_FORMAT_R16G16B16_SFLOAT:
            return vk::Format::eR16G16B16Sfloat;
        case PE_FORMAT_R16G16B16_SINT:
            return vk::Format::eR16G16B16Sint;
        case PE_FORMAT_R16G16B16_UINT:
            return vk::Format::eR16G16B16Uint;
        case PE_FORMAT_R32G32B32_SFLOAT:
            return vk::Format::eR32G32B32Sfloat;
        case PE_FORMAT_R32G32B32_SINT:
            return vk::Format::eR32G32B32Sint;
        case PE_FORMAT_R32G32B32_UINT:
            return vk::Format::eR32G32B32Uint;
        case PE_FORMAT_R8G8B8A8_UNORM:
            return vk::Format::eR8G8B8A8Unorm;
        case PE_FORMAT_R8G8B8A8_SRGB:
            return vk::Format::eR8G8B8A8Srgb;
        case PE_FORMAT_B8G8R8A8_UNORM:
            return vk::Format::eB8G8R8A8Unorm;
        case PE_FORMAT_B8G8R8A8_SRGB:
            return vk::Format::eB8G8R8A8Srgb;
        case PE_FORMAT_R16G16B16A16_SFLOAT:
            return vk::Format::eR16G16B16A16Sfloat;
        case PE_FORMAT_R32G32B32A32_SFLOAT:
            return vk::Format::eR32G32B32A32Sfloat;
        case PE_FORMAT_R32G32B32A32_SINT:
            return vk::Format::eR32G32B32A32Sint;
        case PE_FORMAT_R32G32B32A32_UINT:
            return vk::Format::eR32G32B32A32Uint;
        case PE_FORMAT_A2B10G10R10_UNORM_PACK32:
            return vk::Format::eA2B10G10R10UnormPack32;
        case PE_FORMAT_B10G11R11_UFLOAT_PACK32:
            return vk::Format::eB10G11R11UfloatPack32;
        case PE_FORMAT_D32_SFLOAT:
            return vk::Format::eD32Sfloat;
        case PE_FORMAT_D24_UNORM_S8_UINT:
            return vk::Format::eD24UnormS8Uint;
        case PE_FORMAT_D32_SFLOAT_S8_UINT:
            return vk::Format::eD32SfloatS8Uint;
        case PE_FORMAT_S8_UINT:
            return vk::Format::eS8Uint;
        case PE_FORMAT_BC1_RGBA_UNORM:
            return vk::Format::eBc1RgbaUnormBlock;
        case PE_FORMAT_BC1_RGBA_SRGB:
            return vk::Format::eBc1RgbaSrgbBlock;
        case PE_FORMAT_BC2_UNORM:
            return vk::Format::eBc2UnormBlock;
        case PE_FORMAT_BC2_SRGB:
            return vk::Format::eBc2SrgbBlock;
        case PE_FORMAT_BC3_UNORM:
            return vk::Format::eBc3UnormBlock;
        case PE_FORMAT_BC3_SRGB:
            return vk::Format::eBc3SrgbBlock;
        case PE_FORMAT_BC4_UNORM:
            return vk::Format::eBc4UnormBlock;
        case PE_FORMAT_BC4_SNORM:
            return vk::Format::eBc4SnormBlock;
        case PE_FORMAT_BC5_UNORM:
            return vk::Format::eBc5UnormBlock;
        case PE_FORMAT_BC5_SNORM:
            return vk::Format::eBc5SnormBlock;
        case PE_FORMAT_BC6H_UFLOAT:
            return vk::Format::eBc6HUfloatBlock;
        case PE_FORMAT_BC6H_SFLOAT:
            return vk::Format::eBc6HSfloatBlock;
        case PE_FORMAT_BC7_UNORM:
            return vk::Format::eBc7UnormBlock;
        case PE_FORMAT_BC7_SRGB:
            return vk::Format::eBc7SrgbBlock;
        default:
            return vk::Format::eUndefined;
        }
    }

    ::PeFormat FromVkFormat(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eUndefined:
            return PE_FORMAT_UNDEFINED;
        case vk::Format::eR8Unorm:
            return PE_FORMAT_R8_UNORM;
        case vk::Format::eR8Sint:
            return PE_FORMAT_R8_SINT;
        case vk::Format::eR8Uint:
            return PE_FORMAT_R8_UINT;
        case vk::Format::eR16Sfloat:
            return PE_FORMAT_R16_SFLOAT;
        case vk::Format::eR16Sint:
            return PE_FORMAT_R16_SINT;
        case vk::Format::eR16Uint:
            return PE_FORMAT_R16_UINT;
        case vk::Format::eR32Sfloat:
            return PE_FORMAT_R32_SFLOAT;
        case vk::Format::eR32Sint:
            return PE_FORMAT_R32_SINT;
        case vk::Format::eR32Uint:
            return PE_FORMAT_R32_UINT;
        case vk::Format::eR16G16Sfloat:
            return PE_FORMAT_R16G16_SFLOAT;
        case vk::Format::eR32G32Sfloat:
            return PE_FORMAT_R32G32_SFLOAT;
        case vk::Format::eR32G32Sint:
            return PE_FORMAT_R32G32_SINT;
        case vk::Format::eR32G32Uint:
            return PE_FORMAT_R32G32_UINT;
        case vk::Format::eR16G16B16Sfloat:
            return PE_FORMAT_R16G16B16_SFLOAT;
        case vk::Format::eR16G16B16Sint:
            return PE_FORMAT_R16G16B16_SINT;
        case vk::Format::eR16G16B16Uint:
            return PE_FORMAT_R16G16B16_UINT;
        case vk::Format::eR32G32B32Sfloat:
            return PE_FORMAT_R32G32B32_SFLOAT;
        case vk::Format::eR32G32B32Sint:
            return PE_FORMAT_R32G32B32_SINT;
        case vk::Format::eR32G32B32Uint:
            return PE_FORMAT_R32G32B32_UINT;
        case vk::Format::eR8G8B8A8Unorm:
            return PE_FORMAT_R8G8B8A8_UNORM;
        case vk::Format::eR8G8B8A8Srgb:
            return PE_FORMAT_R8G8B8A8_SRGB;
        case vk::Format::eB8G8R8A8Unorm:
            return PE_FORMAT_B8G8R8A8_UNORM;
        case vk::Format::eB8G8R8A8Srgb:
            return PE_FORMAT_B8G8R8A8_SRGB;
        case vk::Format::eR16G16B16A16Sfloat:
            return PE_FORMAT_R16G16B16A16_SFLOAT;
        case vk::Format::eR32G32B32A32Sfloat:
            return PE_FORMAT_R32G32B32A32_SFLOAT;
        case vk::Format::eR32G32B32A32Sint:
            return PE_FORMAT_R32G32B32A32_SINT;
        case vk::Format::eR32G32B32A32Uint:
            return PE_FORMAT_R32G32B32A32_UINT;
        case vk::Format::eA2B10G10R10UnormPack32:
            return PE_FORMAT_A2B10G10R10_UNORM_PACK32;
        case vk::Format::eB10G11R11UfloatPack32:
            return PE_FORMAT_B10G11R11_UFLOAT_PACK32;
        case vk::Format::eD32Sfloat:
            return PE_FORMAT_D32_SFLOAT;
        case vk::Format::eD24UnormS8Uint:
            return PE_FORMAT_D24_UNORM_S8_UINT;
        case vk::Format::eD32SfloatS8Uint:
            return PE_FORMAT_D32_SFLOAT_S8_UINT;
        case vk::Format::eS8Uint:
            return PE_FORMAT_S8_UINT;
        case vk::Format::eBc1RgbaUnormBlock:
            return PE_FORMAT_BC1_RGBA_UNORM;
        case vk::Format::eBc1RgbaSrgbBlock:
            return PE_FORMAT_BC1_RGBA_SRGB;
        case vk::Format::eBc2UnormBlock:
            return PE_FORMAT_BC2_UNORM;
        case vk::Format::eBc2SrgbBlock:
            return PE_FORMAT_BC2_SRGB;
        case vk::Format::eBc3UnormBlock:
            return PE_FORMAT_BC3_UNORM;
        case vk::Format::eBc3SrgbBlock:
            return PE_FORMAT_BC3_SRGB;
        case vk::Format::eBc4UnormBlock:
            return PE_FORMAT_BC4_UNORM;
        case vk::Format::eBc4SnormBlock:
            return PE_FORMAT_BC4_SNORM;
        case vk::Format::eBc5UnormBlock:
            return PE_FORMAT_BC5_UNORM;
        case vk::Format::eBc5SnormBlock:
            return PE_FORMAT_BC5_SNORM;
        case vk::Format::eBc6HUfloatBlock:
            return PE_FORMAT_BC6H_UFLOAT;
        case vk::Format::eBc6HSfloatBlock:
            return PE_FORMAT_BC6H_SFLOAT;
        case vk::Format::eBc7UnormBlock:
            return PE_FORMAT_BC7_UNORM;
        case vk::Format::eBc7SrgbBlock:
            return PE_FORMAT_BC7_SRGB;
        default:
            return PE_FORMAT_UNDEFINED;
        }
    }

    vk::SampleCountFlagBits ToVkSampleCount(PeSampleCount samples)
    {
        switch (samples)
        {
        case PE_SAMPLE_COUNT_1:
            return vk::SampleCountFlagBits::e1;
        case PE_SAMPLE_COUNT_2:
            return vk::SampleCountFlagBits::e2;
        case PE_SAMPLE_COUNT_4:
            return vk::SampleCountFlagBits::e4;
        case PE_SAMPLE_COUNT_8:
            return vk::SampleCountFlagBits::e8;
        case PE_SAMPLE_COUNT_16:
            return vk::SampleCountFlagBits::e16;
        default:
            return vk::SampleCountFlagBits::e1;
        }
    }

    PeSampleCount FromVkSampleCount(vk::SampleCountFlagBits samples)
    {
        switch (samples)
        {
        case vk::SampleCountFlagBits::e1:
            return PE_SAMPLE_COUNT_1;
        case vk::SampleCountFlagBits::e2:
            return PE_SAMPLE_COUNT_2;
        case vk::SampleCountFlagBits::e4:
            return PE_SAMPLE_COUNT_4;
        case vk::SampleCountFlagBits::e8:
            return PE_SAMPLE_COUNT_8;
        case vk::SampleCountFlagBits::e16:
            return PE_SAMPLE_COUNT_16;
        default:
            return PE_SAMPLE_COUNT_1;
        }
    }

    vk::ImageViewType ToVkImageViewType(PeImageViewType type)
    {
        switch (type)
        {
        case PE_IMAGE_VIEW_TYPE_1D:
            return vk::ImageViewType::e1D;
        case PE_IMAGE_VIEW_TYPE_2D:
            return vk::ImageViewType::e2D;
        case PE_IMAGE_VIEW_TYPE_3D:
            return vk::ImageViewType::e3D;
        case PE_IMAGE_VIEW_TYPE_CUBE:
            return vk::ImageViewType::eCube;
        case PE_IMAGE_VIEW_TYPE_1D_ARRAY:
            return vk::ImageViewType::e1DArray;
        case PE_IMAGE_VIEW_TYPE_2D_ARRAY:
            return vk::ImageViewType::e2DArray;
        case PE_IMAGE_VIEW_TYPE_CUBE_ARRAY:
            return vk::ImageViewType::eCubeArray;
        default:
            return vk::ImageViewType::e2D;
        }
    }

    PeImageViewType FromVkImageViewType(vk::ImageViewType type)
    {
        switch (type)
        {
        case vk::ImageViewType::e1D:
            return PE_IMAGE_VIEW_TYPE_1D;
        case vk::ImageViewType::e2D:
            return PE_IMAGE_VIEW_TYPE_2D;
        case vk::ImageViewType::e3D:
            return PE_IMAGE_VIEW_TYPE_3D;
        case vk::ImageViewType::eCube:
            return PE_IMAGE_VIEW_TYPE_CUBE;
        case vk::ImageViewType::e1DArray:
            return PE_IMAGE_VIEW_TYPE_1D_ARRAY;
        case vk::ImageViewType::e2DArray:
            return PE_IMAGE_VIEW_TYPE_2D_ARRAY;
        case vk::ImageViewType::eCubeArray:
            return PE_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            return PE_IMAGE_VIEW_TYPE_2D;
        }
    }

    vk::ImageType ToVkImageType(PeImageType type)
    {
        switch (type)
        {
        case PE_IMAGE_TYPE_1D:
            return vk::ImageType::e1D;
        case PE_IMAGE_TYPE_2D:
            return vk::ImageType::e2D;
        case PE_IMAGE_TYPE_3D:
            return vk::ImageType::e3D;
        default:
            return vk::ImageType::e2D;
        }
    }

    PeImageType FromVkImageType(vk::ImageType type)
    {
        switch (type)
        {
        case vk::ImageType::e1D:
            return PE_IMAGE_TYPE_1D;
        case vk::ImageType::e2D:
            return PE_IMAGE_TYPE_2D;
        case vk::ImageType::e3D:
            return PE_IMAGE_TYPE_3D;
        default:
            return PE_IMAGE_TYPE_2D;
        }
    }

    vk::ImageLayout ToVkImageLayout(PeImageLayout layout)
    {
        switch (layout)
        {
        case PE_IMAGE_LAYOUT_UNDEFINED:
            return vk::ImageLayout::eUndefined;
        case PE_IMAGE_LAYOUT_GENERAL:
            return vk::ImageLayout::eGeneral;
        case PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return vk::ImageLayout::eTransferSrcOptimal;
        case PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return vk::ImageLayout::eTransferDstOptimal;
        case PE_IMAGE_LAYOUT_PRESENT_SRC:
            return vk::ImageLayout::ePresentSrcKHR;
        case PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eAttachmentOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            return vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            return vk::ImageLayout::eDepthReadOnlyOptimal;
        case PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eDepthAttachmentOptimal;
        case PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
            return vk::ImageLayout::eStencilReadOnlyOptimal;
        case PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
            return vk::ImageLayout::eStencilAttachmentOptimal;
        default:
            return vk::ImageLayout::eUndefined;
        }
    }

    PeImageLayout FromVkImageLayout(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            return PE_IMAGE_LAYOUT_UNDEFINED;
        case vk::ImageLayout::eGeneral:
            return PE_IMAGE_LAYOUT_GENERAL;
        case vk::ImageLayout::eColorAttachmentOptimal:
            return PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case vk::ImageLayout::eTransferSrcOptimal:
            return PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case vk::ImageLayout::eTransferDstOptimal:
            return PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case vk::ImageLayout::ePresentSrcKHR:
            return PE_IMAGE_LAYOUT_PRESENT_SRC;
        case vk::ImageLayout::eAttachmentOptimal:
            return PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        case vk::ImageLayout::eDepthReadOnlyOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case vk::ImageLayout::eDepthAttachmentOptimal:
            return PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case vk::ImageLayout::eStencilReadOnlyOptimal:
            return PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
        case vk::ImageLayout::eStencilAttachmentOptimal:
            return PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        default:
            return PE_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    vk::ImageUsageFlags ToVkImageUsage(PeImageUsageFlags usage)
    {
        vk::ImageUsageFlags vkUsage{};
        if (usage & PE_IMAGE_USAGE_TRANSFER_SRC)
            vkUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        if (usage & PE_IMAGE_USAGE_TRANSFER_DST)
            vkUsage |= vk::ImageUsageFlagBits::eTransferDst;
        if (usage & PE_IMAGE_USAGE_SAMPLED)
            vkUsage |= vk::ImageUsageFlagBits::eSampled;
        if (usage & PE_IMAGE_USAGE_STORAGE)
            vkUsage |= vk::ImageUsageFlagBits::eStorage;
        if (usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
            vkUsage |= vk::ImageUsageFlagBits::eColorAttachment;
        if (usage & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT)
            vkUsage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        if (usage & PE_IMAGE_USAGE_INPUT_ATTACHMENT)
            vkUsage |= vk::ImageUsageFlagBits::eInputAttachment;
        if (usage & PE_IMAGE_USAGE_TRANSIENT_ATTACHMENT)
            vkUsage |= vk::ImageUsageFlagBits::eTransientAttachment;
        return vkUsage;
    }

    PeImageUsageFlags FromVkImageUsage(vk::ImageUsageFlags usage)
    {
        PeImageUsageFlags peUsage = PE_IMAGE_USAGE_NONE;
        if (usage & vk::ImageUsageFlagBits::eTransferSrc)
            peUsage |= PE_IMAGE_USAGE_TRANSFER_SRC;
        if (usage & vk::ImageUsageFlagBits::eTransferDst)
            peUsage |= PE_IMAGE_USAGE_TRANSFER_DST;
        if (usage & vk::ImageUsageFlagBits::eSampled)
            peUsage |= PE_IMAGE_USAGE_SAMPLED;
        if (usage & vk::ImageUsageFlagBits::eStorage)
            peUsage |= PE_IMAGE_USAGE_STORAGE;
        if (usage & vk::ImageUsageFlagBits::eColorAttachment)
            peUsage |= PE_IMAGE_USAGE_COLOR_ATTACHMENT;
        if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
            peUsage |= PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT;
        if (usage & vk::ImageUsageFlagBits::eInputAttachment)
            peUsage |= PE_IMAGE_USAGE_INPUT_ATTACHMENT;
        if (usage & vk::ImageUsageFlagBits::eTransientAttachment)
            peUsage |= PE_IMAGE_USAGE_TRANSIENT_ATTACHMENT;
        return peUsage;
    }

    namespace
    {
        bool ImageTilingSupportedForFormat(vk::Format format, vk::ImageTiling tiling)
        {
            vk::FormatProperties fProps = VulkanRhi::Gpu().getFormatProperties(format);
            if (tiling == vk::ImageTiling::eOptimal)
                return !!fProps.optimalTilingFeatures;
            if (tiling == vk::ImageTiling::eLinear)
                return !!fProps.linearTilingFeatures;
            return false;
        }
    } // namespace

    VulkanImageImpl::VulkanImageImpl(Image *owner, const ImageDesc &desc)
        : m_owner{owner}
    {
        const vk::Format vkFormat = ToVkFormat(desc.format);
        m_vkFormat = vkFormat;

        const vk::ImageTiling tiling = desc.tilingLinear ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
        PE_ERROR_IF(!ImageTilingSupportedForFormat(vkFormat, tiling), "VulkanImageImpl: image tiling support error.");

        vk::ImageUsageFlags vkUsage = ToVkImageUsage(desc.usage);
        // Auto-add eTransferSrc if the format supports it (matches legacy
        // Image.cpp behavior — used by Downsampler/blit paths).
        {
            vk::FormatProperties fProps = VulkanRhi::Gpu().getFormatProperties(vkFormat);
            if (fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferSrc)
                vkUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        }

        vk::ImageCreateInfo info{};
        info.imageType = ToVkImageType(desc.imageType);
        info.format = vkFormat;
        info.extent = vk::Extent3D{desc.width, desc.height, desc.depth};
        info.mipLevels = desc.mipLevels;
        info.arrayLayers = desc.arrayLayers;
        info.samples = ToVkSampleCount(desc.samples);
        info.tiling = tiling;
        info.usage = vkUsage;
        info.sharingMode = vk::SharingMode::eExclusive;
        info.initialLayout = ToVkImageLayout(desc.initialLayout);
        if (desc.cubeCompatible)
            info.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
        if (desc.array2DCompatible)
            info.flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
        if (desc.mutableFormat)
            info.flags |= vk::ImageCreateFlagBits::eMutableFormat;

        VkImageCreateInfo ci = static_cast<VkImageCreateInfo>(info);
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK = VK_NULL_HANDLE;
        VmaAllocationInfo allocInfo{};
        VkResult vr = vmaCreateImage(VulkanRhi::Allocator(), &ci, &aci, &imageVK, &m_allocation, &allocInfo);
        PE_CHECK(vr);
        m_image = imageVK;

        vmaSetAllocationName(VulkanRhi::Allocator(), m_allocation, owner->m_name.c_str());
        Debug::SetObjectName(m_image, owner->m_name);
    }

    VulkanImageImpl::VulkanImageImpl(Image *owner, vk::Image externalImage)
        : m_owner{owner}, m_image{externalImage}, m_externallyOwned{true}
    {
    }

    VulkanImageImpl::~VulkanImageImpl()
    {
        if (m_externallyOwned)
            return;
        if (m_image)
            vmaDestroyImage(VulkanRhi::Allocator(), m_image, m_allocation);
    }

    void VulkanImageImpl::CopyImage(CommandBuffer *cmd, Image *src)
    {
        Image *dst = m_owner;
        PE_ERROR_IF(dst->GetWidth() != src->GetWidth() && dst->GetHeight() != src->GetHeight(),
                    "Image sizes are different");

        cmd->BeginDebugRegion("CopyImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = src;
        barriers[0].stageFlags = PE_STAGE_TRANSFER;
        barriers[0].accessMask = PE_ACCESS_TRANSFER_READ;
        barriers[0].layout = PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[1].image = dst;
        barriers[1].stageFlags = PE_STAGE_TRANSFER;
        barriers[1].accessMask = PE_ACCESS_TRANSFER_WRITE;
        barriers[1].layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Image::Barriers(cmd, barriers);

        vk::ImageCopy2 region{};
        region.srcSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_vkFormat);
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcSubresource.mipLevel = 0;
        region.srcOffset = vk::Offset3D{0, 0, 0};
        region.dstSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_vkFormat);
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstSubresource.mipLevel = 0;
        region.dstOffset = vk::Offset3D{0, 0, 0};
        region.extent = vk::Extent3D{dst->GetWidth(), dst->GetHeight(), 1};

        vk::CopyImageInfo2 copyInfo{};
        copyInfo.srcImage = From(src)->m_image;
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstImage = m_image;
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->BeginDebugRegion(src->GetName() + " -> " + dst->GetName());
        cmd->ApiHandle().copyImage2(copyInfo);
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void VulkanImageImpl::CopyToBuffer(CommandBuffer *cmd, Buffer *dst)
    {
        Image *src = m_owner;
        cmd->BeginDebugRegion("CopyImageToBuffer");

        ImageBarrierInfo barrier{};
        barrier.image = src;
        barrier.stageFlags = PE_STAGE_TRANSFER;
        barrier.accessMask = PE_ACCESS_TRANSFER_READ;
        barrier.layout = PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        Image::Barrier(cmd, barrier);

        BufferBarrierInfo bufBarrier{};
        bufBarrier.buffer = dst;
        bufBarrier.stageMask = PE_STAGE_TRANSFER;
        bufBarrier.accessMask = PE_ACCESS_TRANSFER_WRITE;
        cmd->BufferBarrier(bufBarrier);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_vkFormat);
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{src->GetWidth(), src->GetHeight(), 1};

        vk::CopyImageToBufferInfo2 copyInfo{};
        copyInfo.srcImage = m_image;
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstBuffer = pe::GetVulkanBuffer(dst);
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyImageToBuffer2(copyInfo);
        cmd->EndDebugRegion();
    }

    void VulkanImageImpl::Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter)
    {
        Image *dst = m_owner;
        cmd->BeginDebugRegion("BlitImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = dst;
        barriers[0].stageFlags = PE_STAGE_TRANSFER;
        barriers[0].accessMask = PE_ACCESS_TRANSFER_WRITE;
        barriers[0].layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].baseArrayLayer = region.srcSubresource.baseArrayLayer;
        barriers[0].arrayLayers = region.srcSubresource.layerCount;
        barriers[0].baseMipLevel = region.srcSubresource.mipLevel;
        barriers[0].mipLevels = 1;

        barriers[1].image = src;
        barriers[1].stageFlags = PE_STAGE_TRANSFER;
        barriers[1].accessMask = PE_ACCESS_TRANSFER_READ;
        barriers[1].layout = PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[1].baseArrayLayer = region.dstSubresource.baseArrayLayer;
        barriers[1].arrayLayers = region.dstSubresource.layerCount;
        barriers[1].baseMipLevel = region.dstSubresource.mipLevel;
        barriers[1].mipLevels = 1;

        Image::Barriers(cmd, barriers);

        const vk::ImageBlit vkRegion = ToVkImageBlit(region);
        cmd->BeginDebugRegion(src->GetName() + " -> " + dst->GetName());
        cmd->ApiHandle().blitImage(From(src)->m_image, vk::ImageLayout::eTransferSrcOptimal,
                                   m_image, vk::ImageLayout::eTransferDstOptimal,
                                   1, &vkRegion, ToVkFilter(filter));
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void VulkanImageImpl::CopyDataToImageStaged(CommandBuffer *cmd,
                                                void *data,
                                                size_t size,
                                                uint32_t baseArrayLayer,
                                                uint32_t layerCount,
                                                uint32_t mipLevel)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!cmd, "Image::CopyDataToImageStaged(): no command buffer specified.");

        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(size);
        std::memcpy(alloc.data, data, size);
        alloc.buffer->Flush(size, 0);

        ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.stageFlags = PE_STAGE_TRANSFER;
        barrier.accessMask = PE_ACCESS_TRANSFER_WRITE;
        barrier.layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.baseArrayLayer = baseArrayLayer;
        barrier.arrayLayers = layerCount ? layerCount : image->GetArrayLayers();
        barrier.baseMipLevel = mipLevel;
        barrier.mipLevels = image->GetMipLevels();
        Image::Barrier(cmd, barrier);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_vkFormat);
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : image->GetArrayLayers();
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{image->GetWidth(), image->GetHeight(), image->m_depth};

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = pe::GetVulkanBuffer(alloc.buffer);
        copyInfo.dstImage = m_image;
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyBufferToImage2(copyInfo);

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                  { RHII.GetStagingManager()->SetUnused(alloc); });
    }

    void VulkanImageImpl::CreateRTV()
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_COLOR_ATTACHMENT ||
                      image->GetUsage() & PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT),
                    "Image was not created with ColorAttachment or DepthStencilAttachment for RTV usage");

        if (image->m_rtv)
        {
            PE_INFO("Image::CreateRTV: RTV already exists, recreating.");
            ImageView::Destroy(image->m_rtv);
        }

        ImageViewDesc viewInfo{};
        viewInfo.viewType = PE_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = FromVkImageAspect(VulkanHelpers::GetAspectMask(m_vkFormat));
        viewInfo.baseMipLevel = 0;
        viewInfo.levelCount = image->GetMipLevels();
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();
        image->m_rtv = ImageView::Create(image, viewInfo, image->GetName() + "_RTV");
    }

    void VulkanImageImpl::CreateSRV(PeImageViewType type, int mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_SAMPLED), "Image was not created with Sampled usage for SRV");

        ImageViewDesc viewInfo{};
        viewInfo.viewType = type;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = FromVkImageAspect(VulkanHelpers::GetAspectMask(m_vkFormat));
        viewInfo.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfo.levelCount = mip == -1 ? image->GetMipLevels() : 1;
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();
        ImageView *view = ImageView::Create(image, viewInfo, image->GetName() + "_SRV");

        if (mip == -1)
        {
            if (image->m_srv)
            {
                PE_INFO("Image::CreateSRV: SRV already exists, recreating.");
                ImageView::Destroy(image->m_srv);
            }
            image->m_srv = view;
        }
        else
        {
            if (image->m_srvs[mip])
            {
                PE_INFO("Image::CreateSRV: SRV for mip {} already exists, recreating.", mip);
                ImageView::Destroy(image->m_srvs[mip]);
            }
            image->m_srvs[mip] = view;
        }
    }

    void VulkanImageImpl::CreateUAV(PeImageViewType type, uint32_t mip)
    {
        Image *image = m_owner;
        PE_ERROR_IF(!(image->GetUsage() & PE_IMAGE_USAGE_STORAGE), "Image was not created with Storage usage for UAV");
        if (image->m_uavs[mip])
        {
            PE_INFO("Image::CreateUAV: UAV for mip {} already exists, recreating.", mip);
            ImageView::Destroy(image->m_uavs[mip]);
        }

        ImageViewDesc viewInfo{};
        viewInfo.viewType = type;
        viewInfo.format = image->GetFormat();
        viewInfo.aspectMask = FromVkImageAspect(VulkanHelpers::GetAspectMask(m_vkFormat));
        viewInfo.baseMipLevel = mip;
        viewInfo.levelCount = 1;
        viewInfo.baseArrayLayer = 0;
        viewInfo.layerCount = image->GetArrayLayers();
        image->m_uavs[mip] = ImageView::Create(image, viewInfo, image->GetName() + "_UAV");
    }

    Image::Impl *CreateImageImpl(Image *owner, const ImageDesc &desc)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            return new Dx12ImageImpl(owner, desc);
#endif
        return new VulkanImageImpl(owner, desc);
    }

    Image::Impl *CreateSwapchainImageImpl(Image *owner, vk::Image externalImage)
    {
        return new VulkanImageImpl(owner, externalImage);
    }

    void Image_Barrier_Backend(CommandBuffer *cmd, const ImageBarrierInfo &info)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Image::Barrier: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->ImageBarrier(info);
            return;
        }
#endif
        PE_ERROR_IF(!info.image, "Image::Barrier: no image specified.");
        Image &image = *info.image;
        VulkanImageImpl *impl = VulkanImageImpl::From(&image);

        uint32_t mipLevels = info.mipLevels ? info.mipLevels : image.GetMipLevels();
        uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : image.GetArrayLayers();
        const ImageTrackInfo &oldInfo = image.GetCurrentInfo(info.baseArrayLayer, info.baseMipLevel);

        bool requestRead = IsReadOnlyAccess(info.accessMask);
        bool previousRead = IsReadOnlyAccess(oldInfo.accessMask);
        bool sameState = oldInfo.layout == info.layout &&
                         oldInfo.stageFlags == info.stageFlags &&
                         oldInfo.accessMask == info.accessMask &&
                         oldInfo.queueFamilyId == info.queueFamilyId;
        if (requestRead && previousRead && sameState)
            return;

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = ToVkPipelineStageFlags(oldInfo.stageFlags);
        barrier.dstStageMask = ToVkPipelineStageFlags(info.stageFlags);
        barrier.srcAccessMask = ToVkAccessFlags(oldInfo.accessMask);
        barrier.dstAccessMask = ToVkAccessFlags(info.accessMask);
        barrier.oldLayout = ToVkImageLayout(oldInfo.layout);
        barrier.newLayout = ToVkImageLayout(info.layout);
        if (barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone)
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcQueueFamilyIndex = oldInfo.queueFamilyId;
        barrier.dstQueueFamilyIndex = info.queueFamilyId;
        barrier.image = impl->m_image;
        barrier.subresourceRange.aspectMask = image.GetAspectMaskOverride()
                                                  ? ToVkImageAspect(image.GetAspectMaskOverride())
                                                  : VulkanHelpers::GetAspectMask(impl->m_vkFormat);
        barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
        barrier.subresourceRange.layerCount = arrayLayers;

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd->BeginDebugRegion("ImageBarrier");
        cmd->ApiHandle().pipelineBarrier2(depInfo);
        cmd->EndDebugRegion();

        for (uint32_t i = 0; i < arrayLayers; i++)
            for (uint32_t j = 0; j < mipLevels; j++)
                image.SetCurrentInfo(info, info.baseArrayLayer + i, info.baseMipLevel + j);
    }

    void Image_Barriers_Backend(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Image::Barriers: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->ImageBarriers(infos);
            return;
        }
#endif
        if (infos.empty())
            return;

        std::vector<vk::ImageMemoryBarrier2> barriers;
        barriers.reserve(infos.size());

        for (auto &info : infos)
        {
            PE_ERROR_IF(!info.image, "Image::Barriers: no image specified.");

            Image *image = info.image;
            VulkanImageImpl *impl = VulkanImageImpl::From(image);

            uint32_t mipLevels = info.mipLevels ? info.mipLevels : image->GetMipLevels();
            uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : image->GetArrayLayers();

            const ImageTrackInfo &oldInfo = image->GetCurrentInfo(info.baseArrayLayer, info.baseMipLevel);

            bool requestRead = IsReadOnlyAccess(info.accessMask);
            bool previousRead = IsReadOnlyAccess(oldInfo.accessMask);
            bool sameState = oldInfo.layout == info.layout &&
                             oldInfo.stageFlags == info.stageFlags &&
                             oldInfo.accessMask == info.accessMask &&
                             oldInfo.queueFamilyId == info.queueFamilyId;
            if (requestRead && previousRead && sameState)
                continue;

            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = ToVkPipelineStageFlags(oldInfo.stageFlags);
            barrier.dstStageMask = ToVkPipelineStageFlags(info.stageFlags);
            barrier.srcAccessMask = ToVkAccessFlags(oldInfo.accessMask);
            barrier.dstAccessMask = ToVkAccessFlags(info.accessMask);
            barrier.oldLayout = ToVkImageLayout(oldInfo.layout);
            barrier.newLayout = ToVkImageLayout(info.layout);
            if (barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone)
                barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = impl->m_image;
            barrier.subresourceRange.aspectMask = image->GetAspectMaskOverride()
                                                      ? ToVkImageAspect(image->GetAspectMaskOverride())
                                                      : VulkanHelpers::GetAspectMask(impl->m_vkFormat);
            barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
            barrier.subresourceRange.layerCount = arrayLayers;
            barriers.push_back(barrier);

            for (uint32_t i = 0; i < arrayLayers; i++)
                for (uint32_t j = 0; j < mipLevels; j++)
                    image->SetCurrentInfo(info, info.baseArrayLayer + i, info.baseMipLevel + j);
        }

        if (barriers.empty())
            return;

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        depInfo.pImageMemoryBarriers = barriers.data();

        cmd->BeginDebugRegion("ImageGroupBarrier");
        cmd->ApiHandle().pipelineBarrier2(depInfo);
        cmd->EndDebugRegion();
    }
} // namespace pe
