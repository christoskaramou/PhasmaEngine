#if PE_VULKAN
#include "Renderer/Image.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"
#include "GUI/GUI.h"
#include "Utilities/Downsampler.h"

namespace pe
{
    SamplerCreateInfo::SamplerCreateInfo()
    {
        magFilter = Filter::Linear;
        minFilter = Filter::Linear;
        mipmapMode = SamplerMipmapMode::Linear;
        addressModeU = SamplerAddressMode::Repeat;
        addressModeV = SamplerAddressMode::Repeat;
        addressModeW = SamplerAddressMode::Repeat;
        mipLodBias = 0.f;
        anisotropyEnable = 1;
        maxAnisotropy = 16.0f;
        compareEnable = 0;
        compareOp = CompareOp::Less;
        minLod = -1000.f;
        maxLod = 1000.f;
        borderColor = BorderColor::FloatOpaqueBlack;
        unnormalizedCoordinates = 0;
    }

    ImageCreateInfo::ImageCreateInfo()
    {
        width = 0;
        height = 0;
        depth = 1;
        tiling = ImageTiling::Optimal;
        usage = ImageUsage::None;
        properties = MemoryProperty::DeviceLocalBit;
        format = Format::Undefined;
        imageFlags = ImageCreate::None;
        imageType = ImageType::Type2D;
        mipLevels = 1;
        arrayLayers = 1;
        samples = SampleCount::Count1;
        sharingMode = SharingMode::Exclusive;
        queueFamilyIndexCount = 0;
        pQueueFamilyIndices = nullptr;
        initialLayout = ImageLayout::Undefined;
    }

    bool LinearFilterSupport(Format format, ImageTiling tiling)
    {
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(format), &fProps);

        VkFormatFeatureFlags flags = tiling == ImageTiling::Optimal ? fProps.optimalTilingFeatures : fProps.linearTilingFeatures;
        if (flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
            return true;

        return false;
    }

    Image::Image(const ImageCreateInfo &info)
    {
        if (!LinearFilterSupport(info.format, info.tiling))
            PE_ERROR("Image::generateMipMaps(): Linear filter is not supported.");

        imageInfo = info;

        sampler = {};
        samplerInfo = {};
        blendAttachment = {};

        m_rtv = {};
        m_srvs.resize(imageInfo.mipLevels);
        for (uint32_t i = 0; i < imageInfo.mipLevels; i++)
            m_srvs[i] = {};
        m_uavs.resize(imageInfo.mipLevels);
        for (uint32_t i = 0; i < imageInfo.mipLevels; i++)
            m_uavs[i] = {};

        m_layouts.resize(imageInfo.arrayLayers);
        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            m_layouts[i] = std::vector<ImageLayout>(imageInfo.mipLevels);
            for (uint32_t j = 0; j < imageInfo.mipLevels; j++)
                m_layouts[i][j] = imageInfo.initialLayout;
        }

        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(imageInfo.format), &fProps);

        if (imageInfo.tiling == ImageTiling::Optimal)
        {
            if (!fProps.optimalTilingFeatures)
                PE_ERROR("Image(): no optimal tiling features supported");
        }
        else if (imageInfo.tiling == ImageTiling::Linear)
        {
            if (!fProps.linearTilingFeatures)
                PE_ERROR("Image(): no linear tiling features supported");
        }
        else
        {
            PE_ERROR("Image(): tiling not supported.");
        }

        width_f = static_cast<float>(imageInfo.width);
        height_f = static_cast<float>(imageInfo.height);

        VkImageCreateInfo imageInfoVK{};
        imageInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfoVK.flags = Translate<VkImageCreateFlags>(imageInfo.imageFlags);
        imageInfoVK.imageType = Translate<VkImageType>(imageInfo.imageType);
        imageInfoVK.format = Translate<VkFormat>(imageInfo.format);
        imageInfoVK.extent = VkExtent3D{imageInfo.width, imageInfo.height, imageInfo.depth};
        imageInfoVK.mipLevels = imageInfo.mipLevels;
        imageInfoVK.arrayLayers = imageInfo.arrayLayers;
        imageInfoVK.samples = Translate<VkSampleCountFlagBits>(imageInfo.samples);
        imageInfoVK.tiling = Translate<VkImageTiling>(imageInfo.tiling);
        imageInfoVK.usage = Translate<VkImageUsageFlags>(imageInfo.usage | ImageUsage::TransferSrcBit); // All images can be copied
        imageInfoVK.sharingMode = Translate<VkSharingMode>(imageInfo.sharingMode);
        imageInfoVK.initialLayout = Translate<VkImageLayout>(imageInfo.initialLayout);

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateImage(RHII.GetAllocator(), &imageInfoVK, &allocationCreateInfo, &imageVK, &allocationVK, &allocationInfo));
        m_handle = imageVK;
        allocation = allocationVK;

        Debug::SetObjectName(m_handle, ObjectType::Image, imageInfo.name);
    }

    Image::~Image()
    {
        if (m_rtv)
        {
            vkDestroyImageView(RHII.GetDevice(), m_rtv, nullptr);
            m_rtv = {};
        }

        for (auto &view : m_srvs)
        {
            if (view)
            {
                vkDestroyImageView(RHII.GetDevice(), view, nullptr);
                view = {};
            }
        }

        for (auto &view : m_uavs)
        {
            if (view)
            {
                vkDestroyImageView(RHII.GetDevice(), view, nullptr);
                view = {};
            }
        }

        if (m_handle)
        {
            vmaDestroyImage(RHII.GetAllocator(), m_handle, allocation);
            m_handle = {};
        }

        if (VkSampler(sampler))
        {
            vkDestroySampler(RHII.GetDevice(), sampler, nullptr);
            sampler = {};
        }
    }

    void Image::TransitionImageLayout(CommandBuffer *cmd,
                                      ImageLayout oldLayout,
                                      ImageLayout newLayout,
                                      PipelineStageFlags oldStageMask,
                                      PipelineStageFlags newStageMask,
                                      AccessFlags srcMask,
                                      AccessFlags dstMask,
                                      uint32_t baseArrayLayer,
                                      uint32_t arrayLayers,
                                      uint32_t baseMipLevel,
                                      uint32_t mipLevels)
    {
        // If no cmd exists, start the general cmd
        if (!cmd)
            PE_ERROR("Image::TransitionImageLayout(): no command buffer specified.");

        if (mipLevels == 0)
            mipLevels = imageInfo.mipLevels;

        if (arrayLayers == 0)
            arrayLayers = imageInfo.arrayLayers;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = Translate<VkAccessFlags>(srcMask);
        barrier.dstAccessMask = Translate<VkAccessFlags>(dstMask);
        barrier.image = m_handle;
        barrier.oldLayout = Translate<VkImageLayout>(oldLayout);
        barrier.newLayout = Translate<VkImageLayout>(newLayout);
        barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
        barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
        barrier.subresourceRange.aspectMask = GetAspectMaskVK(imageInfo.format);
        barrier.subresourceRange.baseMipLevel = baseMipLevel;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
        barrier.subresourceRange.layerCount = arrayLayers;
        if (HasStencil(imageInfo.format))
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        vkCmdPipelineBarrier(
            cmd->Handle(),
            Translate<VkPipelineStageFlags>(oldStageMask),
            Translate<VkPipelineStageFlags>(newStageMask),
            VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        for (uint32_t i = 0; i < arrayLayers; i++)
            for (uint32_t j = 0; j < mipLevels; j++)
                m_layouts[baseArrayLayer + i][baseMipLevel + j] = newLayout;
    }

    void Image::CreateRTV()
    {
        if (!(imageInfo.usage & ImageUsage::ColorAttachmentBit ||
              imageInfo.usage & ImageUsage::DepthStencilAttachmentBit))
            PE_ERROR("Image was not created with ColorAttachmentBit or DepthStencilAttachmentBit");

        m_rtv = CreateImageView(ImageViewType::Type2D, 0);
    }

    void Image::CreateSRV(ImageViewType type, int mip)
    {
        if (!(imageInfo.usage & ImageUsage::SampledBit))
            PE_ERROR("Image was not created with SampledBit");

        ImageViewHandle view = CreateImageView(type, mip);
        if (mip == -1)
            m_srv = view;
        else
            m_srvs[mip] = view;
    }

    void Image::CreateUAV(ImageViewType type, uint32_t mip)
    {
        if (!(imageInfo.usage & ImageUsage::StorageBit))
            PE_ERROR("Image was not created with StorageBit");

        m_uavs[mip] = CreateImageView(type, static_cast<int>(mip));
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;

        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    ImageViewHandle Image::CreateImageView(ImageViewType type, int mip)
    {
        VkImageViewCreateInfo viewInfoVK{};
        viewInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfoVK.image = m_handle;
        viewInfoVK.viewType = Translate<VkImageViewType>(type);
        viewInfoVK.format = Translate<VkFormat>(imageInfo.format);
        viewInfoVK.subresourceRange.aspectMask = GetAspectMaskVK(imageInfo.format);
        viewInfoVK.subresourceRange.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfoVK.subresourceRange.levelCount = mip == -1 ? imageInfo.mipLevels : 1;
        viewInfoVK.subresourceRange.baseArrayLayer = 0;
        viewInfoVK.subresourceRange.layerCount = imageInfo.arrayLayers;
        viewInfoVK.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        VkImageView vkView;
        PE_CHECK(vkCreateImageView(RHII.GetDevice(), &viewInfoVK, nullptr, &vkView));
        ImageViewHandle view = vkView;
        Debug::SetObjectName(view, ObjectType::ImageView, imageInfo.name);

        return view;
    }

    void Image::Barrier(CommandBuffer *cmd,
                        ImageLayout newLayout,
                        uint32_t baseArrayLayer,
                        uint32_t arrayLayers,
                        uint32_t baseMipLevel,
                        uint32_t mipLevels)
    {
        ImageLayout oldLayout = m_layouts[baseArrayLayer][baseMipLevel];
        if (newLayout != oldLayout)
        {
            PipelineStageFlags oldPipelineStage;
            AccessFlags oldAccess;
            GetInfoFromLayout(m_layouts[baseArrayLayer][baseMipLevel], oldPipelineStage, oldAccess);

            PipelineStageFlags newPipelineStage;
            AccessFlags newAccess;
            GetInfoFromLayout(newLayout, newPipelineStage, newAccess);

            TransitionImageLayout(cmd,
                                  oldLayout,
                                  newLayout,
                                  oldPipelineStage,
                                  newPipelineStage,
                                  oldAccess,
                                  newAccess,
                                  baseArrayLayer,
                                  arrayLayers,
                                  baseMipLevel,
                                  mipLevels);
        }
    }

    void Image::CopyDataToImageStaged(CommandBuffer *cmd,
                                      void *data,
                                      size_t size,
                                      uint32_t baseArrayLayer,
                                      uint32_t layerCount,
                                      uint32_t mipLevel)
    {
        if (!cmd)
            PE_ERROR("Image::CopyDataToImageStaged(): no command buffer specified.");

        // Staging buffer
        Buffer *staging = Buffer::Create(
            size,
            BufferUsage::TransferSrcBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "staging_buffer");

        // Copy data to staging buffer
        MemoryRange mr{};
        mr.data = data;
        mr.size = size;
        staging->Copy(1, &mr, false);

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = GetAspectMaskVK(imageInfo.format);
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : imageInfo.arrayLayers;
        region.imageOffset = VkOffset3D{0, 0, 0};
        region.imageExtent = VkExtent3D{imageInfo.width, imageInfo.height, imageInfo.depth};

        Barrier(cmd, ImageLayout::TransferDst, baseArrayLayer, layerCount, mipLevel, imageInfo.mipLevels - mipLevel);

        vkCmdCopyBufferToImage(cmd->Handle(),
                               staging->Handle(),
                               m_handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);
        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer::Destroy(staging); });
    }

    void Image::CopyImage(CommandBuffer *cmd, Image *src)
    {
        PE_ERROR_IF(imageInfo.width != src->imageInfo.width && imageInfo.height != src->imageInfo.height,
                    "Image sizes are different");

        cmd->InsertDebugLabel("CopyImage: " + src->imageInfo.name);

        VkImageCopy region{};
        // Source
        region.srcSubresource.aspectMask = GetAspectMaskVK(imageInfo.format);
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = imageInfo.arrayLayers;
        region.srcOffset = VkOffset3D{0, 0, 0};
        // Destination
        region.dstSubresource.aspectMask = GetAspectMaskVK(imageInfo.format);
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = src->imageInfo.arrayLayers;
        region.dstOffset = VkOffset3D{0, 0, 0};

        region.extent = VkExtent3D{src->imageInfo.width,
                                   src->imageInfo.height,
                                   src->imageInfo.depth};

        cmd->ImageBarrier(this, ImageLayout::TransferDst);
        cmd->ImageBarrier(src, ImageLayout::TransferSrc);
        vkCmdCopyImage(
            cmd->Handle(),
            src->m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
    }

    void Image::GenerateMipMaps(CommandBuffer *cmd)
    {
        if (m_mipmapsGenerated || imageInfo.mipLevels < 2)
            return;

        if (!cmd)
            PE_ERROR("Image::GenerateMipMaps(): No command buffer specified.");

        Downsampler::Dispatch(cmd, this);

        m_mipmapsGenerated = true;

        return;

        auto mipWidth = static_cast<int32_t>(imageInfo.width);
        auto mipHeight = static_cast<int32_t>(imageInfo.height);

        ImageBlit region;
        region.srcOffsets[0] = Offset3D{0, 0, 0};
        region.srcOffsets[1] = Offset3D{mipWidth, mipHeight, 1};
        region.srcSubresource.aspectMask = GetAspectMask(imageInfo.format);
        region.srcSubresource.layerCount = 1;
        region.srcSubresource.mipLevel = 0;

        region.dstOffsets[0] = Offset3D{0, 0, 0};
        region.dstSubresource.aspectMask = GetAspectMask(imageInfo.format);
        region.dstSubresource.layerCount = 1;

        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            cmd->ImageBarrier(this, ImageLayout::TransferSrc, i, 1, 0, 1);
            region.srcSubresource.baseArrayLayer = i;
            region.dstSubresource.baseArrayLayer = i;
            for (uint32_t j = 1; j < imageInfo.mipLevels; j++)
            {
                mipWidth = std::max(1, mipWidth / 2);
                mipHeight = std::max(1, mipHeight / 2);
                region.dstOffsets[1] = Offset3D{mipWidth, mipHeight, 1};
                region.dstSubresource.mipLevel = j;

                cmd->ImageBarrier(this, ImageLayout::TransferDst, i, 1, j, 1);
                cmd->BlitImage(this, this, &region, Filter::Linear);
            }
        }

        for (uint32_t i = 0; i < imageInfo.arrayLayers; i++)
        {
            for (uint32_t j = 0; j < imageInfo.mipLevels; j++)
                cmd->ImageBarrier(this, ImageLayout::ShaderReadOnly, i, 1, j, 1);
        }
    }

    void Image::CreateSampler(const SamplerCreateInfo &info)
    {
        samplerInfo = info;

        VkSamplerCreateInfo samplerInfoVK{};
        samplerInfoVK.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfoVK.pNext = nullptr;
        samplerInfoVK.flags = 0;
        samplerInfoVK.magFilter = Translate<VkFilter>(samplerInfo.magFilter);
        samplerInfoVK.minFilter = Translate<VkFilter>(samplerInfo.minFilter);
        samplerInfoVK.mipmapMode = Translate<VkSamplerMipmapMode>(samplerInfo.mipmapMode);
        samplerInfoVK.addressModeU = Translate<VkSamplerAddressMode>(samplerInfo.addressModeU);
        samplerInfoVK.addressModeV = Translate<VkSamplerAddressMode>(samplerInfo.addressModeV);
        samplerInfoVK.addressModeW = Translate<VkSamplerAddressMode>(samplerInfo.addressModeW);
        samplerInfoVK.mipLodBias = log2(GUI::renderTargetsScale) - 1.0f;
        samplerInfoVK.anisotropyEnable = samplerInfo.anisotropyEnable;
        samplerInfoVK.maxAnisotropy = samplerInfo.maxAnisotropy;
        samplerInfoVK.compareEnable = samplerInfo.compareEnable;
        samplerInfoVK.compareOp = Translate<VkCompareOp>(samplerInfo.compareOp);
        samplerInfoVK.minLod = samplerInfo.minLod;
        samplerInfoVK.maxLod = samplerInfo.maxLod;
        samplerInfoVK.borderColor = Translate<VkBorderColor>(samplerInfo.borderColor);
        samplerInfoVK.unnormalizedCoordinates = samplerInfo.unnormalizedCoordinates;

        VkSampler vkSampler;
        PE_CHECK(vkCreateSampler(RHII.GetDevice(), &samplerInfoVK, nullptr, &vkSampler));
        sampler = vkSampler;

        Debug::SetObjectName(sampler, ObjectType::Sampler, imageInfo.name);
    }

    void Image::BlitImage(CommandBuffer *cmd, Image *src, ImageBlit *region, Filter filter)
    {
        if (!cmd)
            PE_ERROR("BlitImage(): Command buffer is null.");

        VkImageBlit regionVK{};
        regionVK.srcSubresource.aspectMask = region->srcSubresource.aspectMask;
        regionVK.srcSubresource.baseArrayLayer = region->srcSubresource.baseArrayLayer;
        regionVK.srcSubresource.layerCount = region->srcSubresource.layerCount;
        regionVK.srcSubresource.mipLevel = region->srcSubresource.mipLevel;
        regionVK.srcOffsets[0] = Translate<VkOffset3D, const Offset3D &>(region->srcOffsets[0]);
        regionVK.srcOffsets[1] = Translate<VkOffset3D, const Offset3D &>(region->srcOffsets[1]);

        regionVK.dstSubresource.aspectMask = region->dstSubresource.aspectMask;
        regionVK.dstSubresource.baseArrayLayer = region->dstSubresource.baseArrayLayer;
        regionVK.dstSubresource.layerCount = region->dstSubresource.layerCount;
        regionVK.dstSubresource.mipLevel = region->dstSubresource.mipLevel;
        regionVK.dstOffsets[0] = Translate<VkOffset3D, const Offset3D &>(region->dstOffsets[0]);
        regionVK.dstOffsets[1] = Translate<VkOffset3D, const Offset3D &>(region->dstOffsets[1]);

        ImageLayout srcLayout = src->m_layouts[region->srcSubresource.baseArrayLayer][region->srcSubresource.mipLevel];
        ImageLayout dstLayout = m_layouts[region->dstSubresource.baseArrayLayer][region->dstSubresource.mipLevel];

        vkCmdBlitImage(cmd->Handle(),
                       src->Handle(), Translate<VkImageLayout>(srcLayout),
                       Handle(), Translate<VkImageLayout>(dstLayout),
                       1, &regionVK,
                       Translate<VkFilter>(filter));
    }
}
#endif
