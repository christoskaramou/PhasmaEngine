#ifdef PE_VULKAN
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Command.h"
#include "API/Buffer.h"
#include "API/Downsampler/Downsampler.h"
#include "tinygltf/stb_image.h"

namespace pe
{
    SamplerCreateInfo::SamplerCreateInfo()
        : magFilter{Filter::Linear},
          minFilter{Filter::Linear},
          mipmapMode{SamplerMipmapMode::Linear},
          addressModeU{SamplerAddressMode::Repeat},
          addressModeV{SamplerAddressMode::Repeat},
          addressModeW{SamplerAddressMode::Repeat},
          mipLodBias{0.f},
          anisotropyEnable{1},
          maxAnisotropy{16.0f},
          compareEnable{0},
          compareOp{CompareOp::Less},
          minLod{-1000.f},
          maxLod{1000.f},
          borderColor{BorderColor::FloatOpaqueBlack},
          unnormalizedCoordinates{0},
          name{""}
    {
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
        : width{0},
          height{0},
          depth{1},
          tiling{ImageTiling::Optimal},
          usage{ImageUsage::None},
          format{Format::Undefined},
          initialLayout{ImageLayout::Undefined},
          imageFlags{ImageCreate::None},
          imageType{ImageType::Type2D},
          mipLevels{1},
          arrayLayers{1},
          samples{SampleCount::Count1},
          sharingMode{SharingMode::Exclusive},
          queueFamilyIndexCount{0},
          pQueueFamilyIndices{nullptr},
          clearColor{Color::Transparent}
    {
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
        : m_info{info}
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        VkSamplerCreateInfo infoVK{};
        infoVK.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        infoVK.pNext = nullptr;
        infoVK.flags = 0;
        infoVK.magFilter = Translate<VkFilter>(m_info.magFilter);
        infoVK.minFilter = Translate<VkFilter>(m_info.minFilter);
        infoVK.mipmapMode = Translate<VkSamplerMipmapMode>(m_info.mipmapMode);
        infoVK.addressModeU = Translate<VkSamplerAddressMode>(m_info.addressModeU);
        infoVK.addressModeV = Translate<VkSamplerAddressMode>(m_info.addressModeV);
        infoVK.addressModeW = Translate<VkSamplerAddressMode>(m_info.addressModeW);
        infoVK.mipLodBias = log2(gSettings.render_scale) - 1.0f;
        infoVK.anisotropyEnable = m_info.anisotropyEnable;
        infoVK.maxAnisotropy = m_info.maxAnisotropy;
        infoVK.compareEnable = m_info.compareEnable;
        infoVK.compareOp = Translate<VkCompareOp>(m_info.compareOp);
        infoVK.minLod = m_info.minLod;
        infoVK.maxLod = m_info.maxLod;
        infoVK.borderColor = Translate<VkBorderColor>(m_info.borderColor);
        infoVK.unnormalizedCoordinates = m_info.unnormalizedCoordinates;

        VkSampler vkSampler;
        PE_CHECK(vkCreateSampler(RHII.GetDevice(), &infoVK, nullptr, &vkSampler));
        m_apiHandle = vkSampler;

        Debug::SetObjectName(m_apiHandle, m_info.name);
    }

    Sampler::~Sampler()
    {
        if (m_apiHandle)
            vkDestroySampler(RHII.GetDevice(), m_apiHandle, nullptr);
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

        for (uint32_t i = 0; i < cubeMap->m_createInfo.arrayLayers; ++i)
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

        m_createInfo = info;
        m_sampler = nullptr;

        m_rtv = {};
        m_srvs.resize(m_createInfo.mipLevels);
        for (uint32_t i = 0; i < m_createInfo.mipLevels; i++)
            m_srvs[i] = {};
        m_uavs.resize(m_createInfo.mipLevels);
        for (uint32_t i = 0; i < m_createInfo.mipLevels; i++)
            m_uavs[i] = {};

        m_trackInfos.resize(m_createInfo.arrayLayers);
        for (uint32_t i = 0; i < m_createInfo.arrayLayers; i++)
        {
            m_trackInfos[i] = std::vector<ImageTrackInfo>(m_createInfo.mipLevels);
            for (uint32_t j = 0; j < m_createInfo.mipLevels; j++)
            {
                ImageTrackInfo info{};
                info.image = this;
                info.layout = m_createInfo.initialLayout;
                m_trackInfos[i][j] = info;
            }
        }

        VkImageCreateInfo imageInfoVK{};
        imageInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfoVK.flags = Translate<VkImageCreateFlags>(m_createInfo.imageFlags);
        imageInfoVK.imageType = Translate<VkImageType>(m_createInfo.imageType);
        imageInfoVK.format = Translate<VkFormat>(m_createInfo.format);
        imageInfoVK.extent = VkExtent3D{m_createInfo.width, m_createInfo.height, m_createInfo.depth};
        imageInfoVK.mipLevels = m_createInfo.mipLevels;
        imageInfoVK.arrayLayers = m_createInfo.arrayLayers;
        imageInfoVK.samples = Translate<VkSampleCountFlagBits>(m_createInfo.samples);
        imageInfoVK.tiling = Translate<VkImageTiling>(m_createInfo.tiling);
        imageInfoVK.usage = Translate<VkImageUsageFlags>(m_createInfo.usage | ImageUsage::TransferSrcBit); // All images can be copied
        imageInfoVK.sharingMode = Translate<VkSharingMode>(m_createInfo.sharingMode);
        imageInfoVK.initialLayout = Translate<VkImageLayout>(m_createInfo.initialLayout);

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK;
        VmaAllocation allocationVK;
        VmaAllocationInfo allocationInfo;
        PE_CHECK(vmaCreateImage(RHII.GetAllocator(), &imageInfoVK, &allocationCreateInfo, &imageVK, &allocationVK, &allocationInfo));
        m_apiHandle = imageVK;
        m_allocation = allocationVK;

        Debug::SetObjectName(m_apiHandle, m_createInfo.name);
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

        if (m_apiHandle)
        {
            vmaDestroyImage(RHII.GetAllocator(), m_apiHandle, m_allocation);
            m_apiHandle = {};
        }

        if (m_sampler)
        {
            Sampler::Destroy(m_sampler);
            m_sampler = nullptr;
        }
    }

    void Image::Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info)
    {
        PE_ERROR_IF(!info.image, "Image::Barrier: no image specified.");
        Image &image = *info.image;

        uint32_t mipLevels = info.mipLevels ? info.mipLevels : image.m_createInfo.mipLevels;
        uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : image.m_createInfo.arrayLayers;
        const ImageTrackInfo &oldInfo = image.m_trackInfos[info.baseArrayLayer][info.baseMipLevel];

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
        barrier.image = image.m_apiHandle;
        barrier.subresourceRange.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(image.m_createInfo.format));
        barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
        barrier.subresourceRange.layerCount = arrayLayers;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd->BeginDebugRegion("ImageBarrier");
        vkCmdPipelineBarrier2(cmd->ApiHandle(), &depInfo);
        cmd->EndDebugRegion();

        for (uint32_t i = 0; i < arrayLayers; i++)
            for (uint32_t j = 0; j < mipLevels; j++)
                image.m_trackInfos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
    }

    void Image::Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos)
    {
        if (infos.empty())
            return;

        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.reserve(infos.size());

        for (auto &info : infos)
        {
            PE_ERROR_IF(!info.image, "Image::Barriers: no image specified.");

            Image *image = info.image;
            const ImageCreateInfo &imageInfo = image->m_createInfo;

            // Get the correct mip levels and array layers if they are not specified (zero)
            uint32_t mipLevels = info.mipLevels ? info.mipLevels : imageInfo.mipLevels;
            uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : imageInfo.arrayLayers;

            const ImageTrackInfo &oldInfo = image->m_trackInfos[info.baseArrayLayer][info.baseMipLevel];

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
            barrier.image = image->m_apiHandle;
            barrier.subresourceRange.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(imageInfo.format));
            barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
            barrier.subresourceRange.layerCount = arrayLayers;
            barriers.push_back(barrier);

            for (uint32_t i = 0; i < arrayLayers; i++)
                for (uint32_t j = 0; j < mipLevels; j++)
                    image->m_trackInfos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
        }

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        depInfo.pImageMemoryBarriers = barriers.data();

        cmd->BeginDebugRegion("ImageGroupBarrier");
        vkCmdPipelineBarrier2(cmd->ApiHandle(), &depInfo);
        cmd->EndDebugRegion();
    }

    void Image::SetCurrentInfoAll(const ImageTrackInfo &info)
    {
        for (uint32_t i = 0; i < m_trackInfos.size(); i++)
        {
            for (uint32_t j = 0; j < m_trackInfos[i].size(); j++)
            {
                m_trackInfos[i][j] = info;
            }
        }
    }

    void Image::CreateRTV(bool useStencil)
    {
        PE_ERROR_IF(!(m_createInfo.usage & ImageUsage::ColorAttachmentBit ||
                      m_createInfo.usage & ImageUsage::DepthStencilAttachmentBit),
                    "Image was not created with ColorAttachmentBit or DepthStencilAttachmentBit for RTV usage");

        m_rtv = CreateImageView(ImageViewType::Type2D, 0, useStencil);
    }

    void Image::CreateSRV(ImageViewType type, int mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_createInfo.usage & ImageUsage::SampledBit), "Image was not created with SampledBit for SRV usage");

        ImageViewApiHandle view = CreateImageView(type, mip, useStencil);
        if (mip == -1)
            m_srv = view;
        else
            m_srvs[mip] = view;
    }

    void Image::CreateUAV(ImageViewType type, uint32_t mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_createInfo.usage & ImageUsage::StorageBit), "Image was not created with StorageBit for UAV usage");

        m_uavs[mip] = CreateImageView(type, static_cast<int>(mip), useStencil);
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;

        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    ImageViewApiHandle Image::CreateImageView(ImageViewType type, int mip, bool useStencil)
    {
        VkImageViewCreateInfo viewInfoVK{};
        viewInfoVK.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfoVK.image = m_apiHandle;
        viewInfoVK.viewType = Translate<VkImageViewType>(type);
        viewInfoVK.format = Translate<VkFormat>(m_createInfo.format);
        viewInfoVK.subresourceRange.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(m_createInfo.format));

        viewInfoVK.subresourceRange.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfoVK.subresourceRange.levelCount = mip == -1 ? m_createInfo.mipLevels : 1;
        viewInfoVK.subresourceRange.baseArrayLayer = 0;
        viewInfoVK.subresourceRange.layerCount = m_createInfo.arrayLayers;
        viewInfoVK.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfoVK.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        VkImageView vkView;
        PE_CHECK(vkCreateImageView(RHII.GetDevice(), &viewInfoVK, nullptr, &vkView));
        ImageViewApiHandle view = vkView;
        Debug::SetObjectName(view, m_createInfo.name);

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
        BufferRange range{};
        range.data = data;
        range.size = size;
        staging->Copy(1, &range, false);

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(m_createInfo.format));
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : m_createInfo.arrayLayers;
        region.imageOffset = VkOffset3D{0, 0, 0};
        region.imageExtent = VkExtent3D{m_createInfo.width, m_createInfo.height, m_createInfo.depth};

        ImageBarrierInfo barrier{};
        barrier.image = this;
        barrier.stageFlags = PipelineStage::TransferBit;
        barrier.accessMask = Access::TransferWriteBit;
        barrier.layout = ImageLayout::TransferDst;
        barrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        barrier.arrayLayers = region.imageSubresource.layerCount;
        barrier.baseMipLevel = region.imageSubresource.mipLevel;
        barrier.mipLevels = m_createInfo.mipLevels; // we need to transition all mip levels here, so they are all in the same state
        Barrier(cmd, barrier);

        vkCmdCopyBufferToImage(cmd->ApiHandle(),
                               staging->ApiHandle(),
                               m_apiHandle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer *buf = staging;
                                    Buffer::Destroy(buf); });
    }

    // TODO: add multiple regions for all mip levels
    void Image::CopyImage(CommandBuffer *cmd, Image *src)
    {
        PE_ERROR_IF(m_createInfo.width != src->m_createInfo.width && m_createInfo.height != src->m_createInfo.height,
                    "Image sizes are different");

        cmd->BeginDebugRegion("CopyImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = src;
        barriers[0].stageFlags = PipelineStage::TransferBit;
        barriers[0].accessMask = Access::TransferReadBit;
        barriers[0].layout = ImageLayout::TransferSrc;
        barriers[1].image = this;
        barriers[1].stageFlags = PipelineStage::TransferBit;
        barriers[1].accessMask = Access::TransferWriteBit;
        barriers[1].layout = ImageLayout::TransferDst;
        Image::Barriers(cmd, barriers);

        VkImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
        region.srcSubresource.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(m_createInfo.format));
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcSubresource.mipLevel = 0;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource.aspectMask = Translate<VkImageAspectFlags>(GetAspectMask(m_createInfo.format));
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstSubresource.mipLevel = 0;
        region.dstOffset = {0, 0, 0};
        region.extent = {m_createInfo.width, m_createInfo.height, m_createInfo.depth};

        VkCopyImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        copyInfo.srcImage = src->m_apiHandle;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstImage = m_apiHandle;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->BeginDebugRegion(src->m_createInfo.name + " -> " + m_createInfo.name);
        vkCmdCopyImage2(cmd->ApiHandle(), &copyInfo);
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void Image::GenerateMipMaps(CommandBuffer *cmd)
    {
        if (m_mipmapsGenerated || m_createInfo.mipLevels < 2)
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

        Image::Barriers(cmd, barriers);

        cmd->BeginDebugRegion(src->m_createInfo.name + " -> " + m_createInfo.name);
        vkCmdBlitImage(cmd->ApiHandle(),
                       src->ApiHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       ApiHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &regionVK,
                       Translate<VkFilter>(filter));
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }
}
#endif
