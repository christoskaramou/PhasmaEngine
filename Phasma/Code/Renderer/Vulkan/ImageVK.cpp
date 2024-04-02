#if PE_VULKAN
#include "Renderer/Image.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"
#include "GUI/GUI.h"
#include "Utilities/Downsampler.h"
#include "tinygltf/stb_image.h"

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
        name = "";
    }

    void SamplerCreateInfo::UpdateHash()
    {
        m_hash = {};
        m_hash.Combine(static_cast<int>(magFilter));
        m_hash.Combine(static_cast<int>(minFilter));
        m_hash.Combine(static_cast<int>(mipmapMode));
        m_hash.Combine(static_cast<int>(addressModeU));
        m_hash.Combine(static_cast<int>(addressModeV));
        m_hash.Combine(static_cast<int>(addressModeW));
        m_hash.Combine(mipLodBias);
        m_hash.Combine(anisotropyEnable);
        m_hash.Combine(maxAnisotropy);
        m_hash.Combine(compareEnable);
        m_hash.Combine(static_cast<int>(compareOp));
        m_hash.Combine(minLod);
        m_hash.Combine(maxLod);
        m_hash.Combine(static_cast<int>(borderColor));
        m_hash.Combine(unnormalizedCoordinates);
    }

    ImageCreateInfo::ImageCreateInfo()
    {
        width = 0;
        height = 0;
        depth = 1;
        tiling = ImageTiling::Optimal;
        usage = ImageUsage::None;
        format = Format::Undefined;
        initialLayout = ImageLayout::Undefined;
        imageFlags = ImageCreate::None;
        imageType = ImageType::Type2D;
        mipLevels = 1;
        arrayLayers = 1;
        samples = SampleCount::Count1;
        sharingMode = SharingMode::Exclusive;
        queueFamilyIndexCount = 0;
        pQueueFamilyIndices = nullptr;
        clearColor = Color::Transparent;
    }

    bool ImageTilingSupport(Format format, ImageTiling tiling)
    {
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(format), &fProps);

        if (tiling == ImageTiling::Optimal)
        {
            return fProps.optimalTilingFeatures;
        }
        else if (tiling == ImageTiling::Linear)
        {
            return fProps.linearTilingFeatures;
        }

        return false;
    }

    Sampler::Sampler(const SamplerCreateInfo &info)
    {
        this->info = info;
        auto &gSettings = Settings::Get<GlobalSettings>();

        VkSamplerCreateInfo infoVK{};
        infoVK.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        infoVK.pNext = nullptr;
        infoVK.flags = 0;
        infoVK.magFilter = Translate<VkFilter>(info.magFilter);
        infoVK.minFilter = Translate<VkFilter>(info.minFilter);
        infoVK.mipmapMode = Translate<VkSamplerMipmapMode>(info.mipmapMode);
        infoVK.addressModeU = Translate<VkSamplerAddressMode>(info.addressModeU);
        infoVK.addressModeV = Translate<VkSamplerAddressMode>(info.addressModeV);
        infoVK.addressModeW = Translate<VkSamplerAddressMode>(info.addressModeW);
        infoVK.mipLodBias = log2(gSettings.render_scale) - 1.0f;
        infoVK.anisotropyEnable = info.anisotropyEnable;
        infoVK.maxAnisotropy = info.maxAnisotropy;
        infoVK.compareEnable = info.compareEnable;
        infoVK.compareOp = Translate<VkCompareOp>(info.compareOp);
        infoVK.minLod = info.minLod;
        infoVK.maxLod = info.maxLod;
        infoVK.borderColor = Translate<VkBorderColor>(info.borderColor);
        infoVK.unnormalizedCoordinates = info.unnormalizedCoordinates;

        VkSampler vkSampler;
        PE_CHECK(vkCreateSampler(RHII.GetDevice(), &infoVK, nullptr, &vkSampler));
        m_handle = vkSampler;

        Debug::SetObjectName(m_handle, info.name);
    }

    Sampler::~Sampler()
    {
        if (m_handle)
            vkDestroySampler(RHII.GetDevice(), m_handle, nullptr);
    }

    Image *Image::LoadRGBA8(CommandBuffer *cmd, const std::string &path)
    {
        int texWidth, texHeight, texChannels;
        unsigned char *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        PE_ERROR_IF(!pixels, "No pixel data loaded");

        ImageCreateInfo info{};
        info.format = Format::RGBA8Unorm;
        info.mipLevels = Image::CalculateMips(texWidth, texHeight);
        info.width = texWidth;
        info.height = texHeight;
        info.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit | ImageUsage::StorageBit;
        info.initialLayout = ImageLayout::Preinitialized;
        info.name = path;
        Image *image = Image::Create(info);

        image->CreateSRV(ImageViewType::Type2D);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.maxLod = static_cast<float>(info.mipLevels);
        image->m_sampler = Sampler::Create(samplerInfo);

        cmd->CopyDataToImageStaged(image, pixels, texWidth * texHeight * STBI_rgb_alpha);
        cmd->GenerateMipMaps(image);

        ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;
        cmd->ImageBarrier(barrier);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBA8Cubemap(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSize)
    {
        ImageCreateInfo info{};
        info.format = Format::RGBA8Unorm;
        info.width = imageSize;
        info.height = imageSize;
        info.arrayLayers = 6;
        info.imageFlags = ImageCreate::CubeCompatibleBit;
        info.usage = ImageUsage::SampledBit | ImageUsage::TransferDstBit;
        info.initialLayout = ImageLayout::Undefined;
        info.name = "cube_map";
        Image *cubeMap = Image::Create(info);

        cubeMap->CreateSRV(ImageViewType::TypeCube);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
        cubeMap->m_sampler = Sampler::Create(samplerInfo);

        for (uint32_t i = 0; i < cubeMap->m_imageInfo.arrayLayers; ++i)
        {
            int texWidth, texHeight, texChannels;
            stbi_uc *pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            PE_ERROR_IF(!pixels, "No pixel data loaded");

            size_t size = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * STBI_rgb_alpha;
            cmd->CopyDataToImageStaged(cubeMap, pixels, size, i, 1);
            cmd->AddAfterWaitCallback([pixels]()
                                      { stbi_image_free(pixels); });
        }

        return cubeMap;
    }

    Image::Image(const ImageCreateInfo &info)
    {
        PE_ERROR_IF(!ImageTilingSupport(info.format, info.tiling), "Image::Image(const ImageCreateInfo &info): Image tiling support error.");

        m_imageInfo = info;

        m_sampler = nullptr;
        m_blendAttachment = {};

        m_rtv = {};
        m_srvs.resize(m_imageInfo.mipLevels);
        for (uint32_t i = 0; i < m_imageInfo.mipLevels; i++)
            m_srvs[i] = {};
        m_uavs.resize(m_imageInfo.mipLevels);
        for (uint32_t i = 0; i < m_imageInfo.mipLevels; i++)
            m_uavs[i] = {};

        m_infos.resize(m_imageInfo.arrayLayers);
        for (uint32_t i = 0; i < m_imageInfo.arrayLayers; i++)
        {
            m_infos[i] = std::vector<ImageBarrierInfo>(m_imageInfo.mipLevels);
            for (uint32_t j = 0; j < m_imageInfo.mipLevels; j++)
            {
                ImageBarrierInfo info{};
                info.image = this;
                info.layout = m_imageInfo.initialLayout;
                m_infos[i][j] = info;
            }
        }

        VkImageCreateInfo imageInfoVK{};
        imageInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfoVK.flags = Translate<VkImageCreateFlags>(m_imageInfo.imageFlags);
        imageInfoVK.imageType = Translate<VkImageType>(m_imageInfo.imageType);
        imageInfoVK.format = Translate<VkFormat>(m_imageInfo.format);
        imageInfoVK.extent = VkExtent3D{m_imageInfo.width, m_imageInfo.height, m_imageInfo.depth};
        imageInfoVK.mipLevels = m_imageInfo.mipLevels;
        imageInfoVK.arrayLayers = m_imageInfo.arrayLayers;
        imageInfoVK.samples = Translate<VkSampleCountFlagBits>(m_imageInfo.samples);
        imageInfoVK.tiling = Translate<VkImageTiling>(m_imageInfo.tiling);
        imageInfoVK.usage = Translate<VkImageUsageFlags>(m_imageInfo.usage | ImageUsage::TransferSrcBit); // All images can be copied
        imageInfoVK.sharingMode = Translate<VkSharingMode>(m_imageInfo.sharingMode);
        imageInfoVK.initialLayout = Translate<VkImageLayout>(m_imageInfo.initialLayout);

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateImage(RHII.GetAllocator(), &imageInfoVK, &allocationCreateInfo, &imageVK, &allocationVK, &allocationInfo));
        m_handle = imageVK;
        m_allocation = allocationVK;

        Debug::SetObjectName(m_handle, m_imageInfo.name);
    }

    Image::~Image()
    {
        if (m_rtv)
        {
            vkDestroyImageView(RHII.GetDevice(), m_rtv, nullptr);
            m_rtv = {};
        }
        if (m_srv)
        {
            vkDestroyImageView(RHII.GetDevice(), m_srv, nullptr);
            m_srv = {};
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
            vmaDestroyImage(RHII.GetAllocator(), m_handle, m_allocation);
            m_handle = {};
        }

        if (m_sampler)
        {
            Sampler::Destroy(m_sampler);
            m_sampler = nullptr;
        }
    }

    void Image::Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info)
    {
        PE_ERROR_IF(!cmd, "Image::Barrier(): no command buffer specified.");

        uint mipLevels = info.mipLevels ? info.mipLevels : m_imageInfo.mipLevels;
        uint arrayLayers = info.arrayLayers ? info.arrayLayers : m_imageInfo.arrayLayers;
        const ImageBarrierInfo &oldInfo = m_infos[info.baseArrayLayer][info.baseMipLevel];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = Translate<VkPipelineStageFlags2>(oldInfo.stageFlags);
        barrier.dstStageMask = Translate<VkPipelineStageFlags2>(info.stageFlags);
        barrier.srcAccessMask = Translate<VkAccessFlags2>(oldInfo.accessMask);
        barrier.dstAccessMask = Translate<VkAccessFlags2>(info.accessMask);
        barrier.oldLayout = Translate<VkImageLayout>(oldInfo.layout);
        barrier.newLayout = Translate<VkImageLayout>(info.layout);
        barrier.srcQueueFamilyIndex = oldInfo.queueFamilyId;
        barrier.dstQueueFamilyIndex = info.queueFamilyId;
        barrier.image = m_handle;
        barrier.subresourceRange.aspectMask = GetAspectMaskVK(m_imageInfo.format);
        barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
        barrier.subresourceRange.layerCount = arrayLayers;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd->BeginDebugRegion("ImageBarrier: " + m_imageInfo.name);
        vkCmdPipelineBarrier2(cmd->Handle(), &depInfo);
        cmd->EndDebugRegion();

        for (uint32_t i = 0; i < arrayLayers; i++)
            for (uint32_t j = 0; j < mipLevels; j++)
                m_infos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
    }

    void Image::Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos)
    {
        PE_ERROR_IF(!cmd, "Image::GroupBarrier(): no command buffer specified.");

        if (infos.empty())
            return;

        std::string names;

        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.reserve(infos.size());

        for (auto &info : infos)
        {
            if (info.image == nullptr)
                continue;

            Image *image = info.image;
            const ImageCreateInfo &imageInfo = image->m_imageInfo;
            
            // Get the correct mip levels and array layers if they are not specified (zero)
            uint mipLevels = info.mipLevels ? info.mipLevels : imageInfo.mipLevels;
            uint arrayLayers = info.arrayLayers ? info.arrayLayers : imageInfo.arrayLayers;

            const ImageBarrierInfo &oldInfo = image->m_infos[info.baseArrayLayer][info.baseMipLevel];

            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = Translate<VkPipelineStageFlags2>(oldInfo.stageFlags);
            barrier.dstStageMask = Translate<VkPipelineStageFlags2>(info.stageFlags);
            barrier.srcAccessMask = Translate<VkAccessFlags2>(oldInfo.accessMask);
            barrier.dstAccessMask = Translate<VkAccessFlags2>(info.accessMask);
            barrier.oldLayout = Translate<VkImageLayout>(oldInfo.layout);
            barrier.newLayout = Translate<VkImageLayout>(info.layout);
            barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
            barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
            barrier.image = image->m_handle;
            barrier.subresourceRange.aspectMask = GetAspectMaskVK(imageInfo.format);
            barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
            barrier.subresourceRange.layerCount = arrayLayers;
            barriers.push_back(barrier);

            names += imageInfo.name + ", ";

            for (uint32_t i = 0; i < arrayLayers; i++)
                for (uint32_t j = 0; j < mipLevels; j++)
                    image->m_infos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
        }

        if (!barriers.empty())
        {
            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            depInfo.pImageMemoryBarriers = barriers.data();

            cmd->BeginDebugRegion("ImageGroupBarrier: " + names);
            vkCmdPipelineBarrier2(cmd->Handle(), &depInfo);
            cmd->EndDebugRegion();
        }
    }

    void Image::CreateRTV(bool useStencil)
    {
        PE_ERROR_IF(!(m_imageInfo.usage & ImageUsage::ColorAttachmentBit ||
                      m_imageInfo.usage & ImageUsage::DepthStencilAttachmentBit),
                    "Image was not created with ColorAttachmentBit or DepthStencilAttachmentBit");

        m_rtv = CreateImageView(ImageViewType::Type2D, 0, useStencil);
    }

    void Image::CreateSRV(ImageViewType type, int mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_imageInfo.usage & ImageUsage::SampledBit), "Image was not created with SampledBit");

        ImageViewHandle view = CreateImageView(type, mip, useStencil);
        if (mip == -1)
            m_srv = view;
        else
            m_srvs[mip] = view;
    }

    void Image::CreateUAV(ImageViewType type, uint32_t mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_imageInfo.usage & ImageUsage::StorageBit), "Image was not created with StorageBit");

        m_uavs[mip] = CreateImageView(type, static_cast<int>(mip), useStencil);
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;

        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    ImageViewHandle Image::CreateImageView(ImageViewType type, int mip, bool useStencil)
    {
        VkImageViewCreateInfo viewInfoVK{};
        viewInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfoVK.image = m_handle;
        viewInfoVK.viewType = Translate<VkImageViewType>(type);
        viewInfoVK.format = Translate<VkFormat>(m_imageInfo.format);
        if (IsDepthFormat(m_imageInfo.format))
        {
            if (useStencil && HasStencil(m_imageInfo.format))
                viewInfoVK.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
            else
                viewInfoVK.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else if (IsStencilFormat(m_imageInfo.format))
        {
            viewInfoVK.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        else
        {
            viewInfoVK.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        viewInfoVK.subresourceRange.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfoVK.subresourceRange.levelCount = mip == -1 ? m_imageInfo.mipLevels : 1;
        viewInfoVK.subresourceRange.baseArrayLayer = 0;
        viewInfoVK.subresourceRange.layerCount = m_imageInfo.arrayLayers;
        viewInfoVK.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        VkImageView vkView;
        PE_CHECK(vkCreateImageView(RHII.GetDevice(), &viewInfoVK, nullptr, &vkView));
        ImageViewHandle view = vkView;
        Debug::SetObjectName(view, m_imageInfo.name);

        return view;
    }

    void Image::CopyDataToImageStaged(CommandBuffer *cmd,
                                      void *data,
                                      size_t size,
                                      uint32_t baseArrayLayer,
                                      uint32_t layerCount,
                                      uint32_t mipLevel)
    {
        PE_ERROR_IF(!cmd, "Image::CopyDataToImageStaged(): no command buffer specified.");

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
        region.imageSubresource.aspectMask = GetAspectMaskVK(m_imageInfo.format);
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : m_imageInfo.arrayLayers;
        region.imageOffset = VkOffset3D{0, 0, 0};
        region.imageExtent = VkExtent3D{m_imageInfo.width, m_imageInfo.height, m_imageInfo.depth};

        ImageBarrierInfo barrier{};
        barrier.image = this;
        barrier.stageFlags = PipelineStage::TransferBit;
        barrier.accessMask = Access::TransferWriteBit;
        barrier.layout = ImageLayout::TransferDst;
        barrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        barrier.arrayLayers = region.imageSubresource.layerCount;
        barrier.baseMipLevel = region.imageSubresource.mipLevel;
        barrier.mipLevels = m_imageInfo.mipLevels; // we need to transition all mip levels here, so they are all in the same state
        Barrier(cmd, barrier);

        vkCmdCopyBufferToImage(cmd->Handle(),
                               staging->Handle(),
                               m_handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer *buf = staging;
                                    Buffer::Destroy(buf); });
    }

    // TODO: add multiple regions for all mip levels
    void Image::CopyImage(CommandBuffer *cmd, Image *src)
    {
        PE_ERROR_IF(m_imageInfo.width != src->m_imageInfo.width && m_imageInfo.height != src->m_imageInfo.height,
                    "Image sizes are different");

        cmd->BeginDebugRegion("CopyImage");

        VkImageCopy region{};
        // Source
        region.srcSubresource.aspectMask = GetAspectMaskVK(m_imageInfo.format);
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = m_imageInfo.arrayLayers;
        region.srcOffset = VkOffset3D{0, 0, 0};
        // Destination
        region.dstSubresource.aspectMask = GetAspectMaskVK(m_imageInfo.format);
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = src->m_imageInfo.arrayLayers;
        region.dstOffset = VkOffset3D{0, 0, 0};

        region.extent = VkExtent3D{src->m_imageInfo.width,
                                   src->m_imageInfo.height,
                                   src->m_imageInfo.depth};

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = this;
        barriers[0].stageFlags = PipelineStage::TransferBit;
        barriers[0].accessMask = Access::TransferWriteBit;
        barriers[0].layout = ImageLayout::TransferDst;
        barriers[1].image = src;
        barriers[1].stageFlags = PipelineStage::TransferBit;
        barriers[1].accessMask = Access::TransferReadBit;
        barriers[1].layout = ImageLayout::TransferSrc;
        Barriers(cmd, barriers);

        // std::string regionName = src->imageInfo.name + " layerCount=" + std::to_string(region.srcSubresource.layerCount) +
        //                          ", baseLayer=" + std::to_string(region.srcSubresource.baseArrayLayer) +
        //                          ", mipLevel=" + std::to_string(region.srcSubresource.mipLevel) +
        //                          " -> " + imageInfo.name + " layerCount=" + std::to_string(region.dstSubresource.layerCount) +
        //                          ", baseLayer=" + std::to_string(region.dstSubresource.baseArrayLayer) +
        //                          ", mipLevel=" + std::to_string(region.dstSubresource.mipLevel);
        cmd->BeginDebugRegion(src->m_imageInfo.name + " -> " + m_imageInfo.name);
        vkCmdCopyImage(
            cmd->Handle(),
            src->m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void Image::GenerateMipMaps(CommandBuffer *cmd)
    {
        if (m_mipmapsGenerated || m_imageInfo.mipLevels < 2)
            return;

        Downsampler::Dispatch(cmd, this);
        m_mipmapsGenerated = true;
    }

    // TODO: add multiple regions for all mip levels
    void Image::BlitImage(CommandBuffer *cmd, Image *src, ImageBlit *region, Filter filter)
    {
        PE_ERROR_IF(!cmd, "BlitImage(): Command buffer is null.");

        cmd->BeginDebugRegion("BlitImage");

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

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = this;
        barriers[0].stageFlags = PipelineStage::TransferBit;
        barriers[0].accessMask = Access::TransferWriteBit;
        barriers[0].layout = ImageLayout::TransferDst;
        barriers[0].baseArrayLayer = region->srcSubresource.baseArrayLayer;
        barriers[0].arrayLayers = region->srcSubresource.layerCount;
        barriers[0].baseMipLevel = region->srcSubresource.mipLevel;
        barriers[0].mipLevels = 1;

        barriers[1].image = src;
        barriers[1].stageFlags = PipelineStage::TransferBit;
        barriers[1].accessMask = Access::TransferReadBit;
        barriers[1].layout = ImageLayout::TransferSrc;
        barriers[1].baseArrayLayer = region->dstSubresource.baseArrayLayer;
        barriers[1].arrayLayers = region->dstSubresource.layerCount;
        barriers[1].baseMipLevel = region->dstSubresource.mipLevel;
        barriers[1].mipLevels = 1;

        Barriers(cmd, barriers);

        // std::string regionName = src->imageInfo.name + " layerCount=" + std::to_string(region->srcSubresource.layerCount) +
        //                          ", baseLayer=" + std::to_string(region->srcSubresource.baseArrayLayer) +
        //                          ", mipLevel=" + std::to_string(region->srcSubresource.mipLevel) +
        //                          " -> " + imageInfo.name + " layerCount=" + std::to_string(region->dstSubresource.layerCount) +
        //                          ", baseLayer=" + std::to_string(region->dstSubresource.baseArrayLayer) +
        //                          ", mipLevel=" + std::to_string(region->dstSubresource.mipLevel);
        cmd->BeginDebugRegion(src->m_imageInfo.name + " -> " + m_imageInfo.name);
        vkCmdBlitImage(cmd->Handle(),
                       src->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &regionVK,
                       Translate<VkFilter>(filter));
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }
}
#endif
