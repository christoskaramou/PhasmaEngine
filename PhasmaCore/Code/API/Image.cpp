#include "API/Image.h"
#include "API/RHI.h"
#include "API/Command.h"
#include "API/Buffer.h"
#include "API/Downsampler/Downsampler.h"
#include "tinygltf/stb_image.h"

namespace pe
{
    bool ImageTilingSupport(vk::Format format, vk::ImageTiling tiling)
    {
        vk::FormatProperties fProps = RHII.GetGpu().getFormatProperties(format);

        if (tiling == vk::ImageTiling::eOptimal)
        {
            return !!fProps.optimalTilingFeatures;
        }
        else if (tiling == vk::ImageTiling::eLinear)
        {
            return !!fProps.linearTilingFeatures;
        }

        return false;
    }

    vk::SamplerCreateInfo Sampler::CreateInfoInit()
    {
        // pNext                   = {};
        // flags                   = {};
        // magFilter               = VULKAN_HPP_NAMESPACE::Filter::eNearest;
        // minFilter               = VULKAN_HPP_NAMESPACE::Filter::eNearest;
        // mipmapMode              = VULKAN_HPP_NAMESPACE::SamplerMipmapMode::eNearest;
        // addressModeU            = VULKAN_HPP_NAMESPACE::SamplerAddressMode::eRepeat;
        // addressModeV            = VULKAN_HPP_NAMESPACE::SamplerAddressMode::eRepeat;
        // addressModeW            = VULKAN_HPP_NAMESPACE::SamplerAddressMode::eRepeat;
        // mipLodBias              = {};
        // anisotropyEnable        = {};
        // maxAnisotropy           = {};
        // compareEnable           = {};
        // compareOp               = VULKAN_HPP_NAMESPACE::CompareOp::eNever;
        // minLod                  = {};
        // maxLod                  = {};
        // borderColor             = VULKAN_HPP_NAMESPACE::BorderColor::eFloatTransparentBlack;
        // unnormalizedCoordinates = {};

        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = 16.0f;
        info.compareOp = vk::CompareOp::eLess;
        info.minLod = -1000.f;
        info.maxLod = 1000.f;
        info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
        return info;
    }

    Sampler::Sampler(const vk::SamplerCreateInfo &info, const std::string &name)
        : m_info{info}, m_name{name}
    {
        m_apiHandle = RHII.GetDevice().createSampler(info);
        Debug::SetObjectName(m_apiHandle, name);
    }

    Sampler::~Sampler()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroySampler(m_apiHandle);
    }

    Image *Image::LoadRGBA8(CommandBuffer *cmd, const std::string &path)
    {
        int texWidth, texHeight, texChannels;
        unsigned char *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        PE_ERROR_IF(!pixels, "No pixel data loaded");

        vk::ImageCreateInfo info = CreateInfoInit();
        info.format = vk::Format::eR8G8B8A8Unorm;
        info.extent = vk::Extent3D{static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
        info.mipLevels = Image::CalculateMips(texWidth, texHeight);
        info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
        info.initialLayout = vk::ImageLayout::ePreinitialized;
        Image *image = Image::Create(info, path);
        image->m_clearColor = Color::Transparent;
        image->CreateSRV(vk::ImageViewType::e2D);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(info.mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        image->m_sampler = Sampler::Create(samplerInfo);

        cmd->CopyDataToImageStaged(image, pixels, texWidth * texHeight * STBI_rgb_alpha);
        cmd->GenerateMipMaps(image);

        ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBA8Cubemap(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSize)
    {
        vk::ImageCreateInfo info = CreateInfoInit();
        info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
        info.format = vk::Format::eR8G8B8A8Unorm;
        info.extent = vk::Extent3D{static_cast<uint32_t>(imageSize), static_cast<uint32_t>(imageSize), 1};
        info.arrayLayers = 6;
        info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        Image *cubeMap = Image::Create(info, "cube_map");

        cubeMap->CreateSRV(vk::ImageViewType::eCube);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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

    vk::ImageCreateInfo Image::CreateInfoInit()
    {
        // pNext                 = {};
        // flags                 = {};
        // imageType             = VULKAN_HPP_NAMESPACE::ImageType::e1D;
        // format                = VULKAN_HPP_NAMESPACE::Format::eUndefined;
        // extent                = {};
        // mipLevels             = {};
        // arrayLayers           = {};
        // samples               = VULKAN_HPP_NAMESPACE::SampleCountFlagBits::e1;
        // tiling                = VULKAN_HPP_NAMESPACE::ImageTiling::eOptimal;
        // usage                 = {};
        // sharingMode           = VULKAN_HPP_NAMESPACE::SharingMode::eExclusive;
        // queueFamilyIndexCount = {};
        // pQueueFamilyIndices   = {};
        // initialLayout         = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined;

        vk::ImageCreateInfo info{};
        info.imageType = vk::ImageType::e2D;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        return info;
    }

    Image::Image(const vk::ImageCreateInfo &info, const std::string &name)
        : m_createInfo(info),
          m_sampler(nullptr),
          m_rtv(nullptr),
          m_srv(nullptr),
          m_name(name),
          m_srvs(m_createInfo.mipLevels, nullptr),
          m_uavs(m_createInfo.mipLevels, nullptr)
    {
        PE_ERROR_IF(!ImageTilingSupport(info.format, info.tiling), "Image::Image(const ImageCreateInfo &info): Image tiling support error.");
        PE_ERROR_IF(!m_createInfo.mipLevels, "Image::Image(const ImageCreateInfo &info): Mip levels cannot be zero.");
        PE_ERROR_IF(!m_createInfo.arrayLayers, "Image::Image(const ImageCreateInfo &info): Array layers cannot be zero.");

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

        m_createInfo.usage |= vk::ImageUsageFlagBits::eTransferSrc;

        VkImageCreateInfo ci = static_cast<VkImageCreateInfo>(m_createInfo);
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage imageVK = VK_NULL_HANDLE;
        VmaAllocationInfo allocInfo{};
        VkResult vr = vmaCreateImage(RHII.GetAllocator(), &ci, &aci, &imageVK, &m_allocation, &allocInfo);
        PE_CHECK(vr);
        m_apiHandle = imageVK;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Image::~Image()
    {
        if (m_rtv)
        {
            RHII.GetDevice().destroyImageView(m_rtv);
            m_rtv = vk::ImageView{};
        }
        if (m_srv)
        {
            RHII.GetDevice().destroyImageView(m_srv);
            m_srv = vk::ImageView{};
        }
        for (auto &view : m_srvs)
        {
            if (view)
            {
                RHII.GetDevice().destroyImageView(view);
                view = vk::ImageView{};
            }
        }

        for (auto &view : m_uavs)
        {
            if (view)
            {
                RHII.GetDevice().destroyImageView(view);
                view = vk::ImageView{};
            }
        }

        if (m_apiHandle)
        {
            vmaDestroyImage(RHII.GetAllocator(), m_apiHandle, m_allocation);
            m_apiHandle = vk::Image{};
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

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = oldInfo.stageFlags;
        barrier.dstStageMask = info.stageFlags;
        barrier.srcAccessMask = oldInfo.accessMask;
        barrier.dstAccessMask = info.accessMask;
        barrier.oldLayout = oldInfo.layout;
        barrier.newLayout = info.layout;
        barrier.srcQueueFamilyIndex = oldInfo.queueFamilyId;
        barrier.dstQueueFamilyIndex = info.queueFamilyId;
        barrier.image = image.m_apiHandle;
        barrier.subresourceRange.aspectMask = GetAspectMask(image.m_createInfo.format);
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
                image.m_trackInfos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
    }

    void Image::Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos)
    {
        if (infos.empty())
            return;

        std::vector<vk::ImageMemoryBarrier2> barriers;
        barriers.reserve(infos.size());

        for (auto &info : infos)
        {
            PE_ERROR_IF(!info.image, "Image::Barriers: no image specified.");

            Image *image = info.image;
            const vk::ImageCreateInfo &imageInfo = image->m_createInfo;

            // Get the correct mip levels and array layers if they are not specified (zero)
            uint32_t mipLevels = info.mipLevels ? info.mipLevels : imageInfo.mipLevels;
            uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : imageInfo.arrayLayers;

            const ImageTrackInfo &oldInfo = image->m_trackInfos[info.baseArrayLayer][info.baseMipLevel];

            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = oldInfo.stageFlags;
            barrier.dstStageMask = info.stageFlags;
            barrier.srcAccessMask = oldInfo.accessMask;
            barrier.dstAccessMask = info.accessMask;
            barrier.oldLayout = oldInfo.layout;
            barrier.newLayout = info.layout;
            barrier.srcQueueFamilyIndex = cmd->GetFamilyId();
            barrier.dstQueueFamilyIndex = cmd->GetFamilyId();
            barrier.image = image->m_apiHandle;
            barrier.subresourceRange.aspectMask = GetAspectMask(imageInfo.format);
            barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
            barrier.subresourceRange.layerCount = arrayLayers;
            barriers.push_back(barrier);

            for (uint32_t i = 0; i < arrayLayers; i++)
                for (uint32_t j = 0; j < mipLevels; j++)
                    image->m_trackInfos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
        }

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        depInfo.pImageMemoryBarriers = barriers.data();

        cmd->BeginDebugRegion("ImageGroupBarrier");
        cmd->ApiHandle().pipelineBarrier2(depInfo);
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
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eColorAttachment ||
                      m_createInfo.usage & vk::ImageUsageFlagBits::eDepthStencilAttachment),
                    "Image was not created with ColorAttachmentBit or DepthStencilAttachmentBit for RTV usage");

        m_rtv = CreateImageView(vk::ImageViewType::e2D, 0, useStencil);
    }

    void Image::CreateSRV(vk::ImageViewType type, int mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eSampled), "Image was not created with SampledBit for SRV usage");
        vk::ImageView view = CreateImageView(type, mip, useStencil);
        if (mip == -1)
            m_srv = view;
        else
            m_srvs[mip] = view;
    }

    void Image::CreateUAV(vk::ImageViewType type, uint32_t mip, bool useStencil)
    {
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eStorage), "Image was not created with StorageBit for UAV usage");
        m_uavs[mip] = CreateImageView(type, static_cast<int>(mip), useStencil);
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;

        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    vk::ImageView Image::CreateImageView(vk::ImageViewType type, int mip, bool useStencil)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_apiHandle;
        viewInfo.viewType = type;
        viewInfo.format = m_createInfo.format;
        viewInfo.subresourceRange.aspectMask = GetAspectMask(m_createInfo.format);
        viewInfo.subresourceRange.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfo.subresourceRange.levelCount = mip == -1 ? m_createInfo.mipLevels : 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_createInfo.arrayLayers;
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        vk::ImageView view = RHII.GetDevice().createImageView(viewInfo);
        Debug::SetObjectName(view, m_name);

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
            vk::BufferUsageFlagBits2::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "staging_buffer");

        // Copy data to staging buffer
        BufferRange range{};
        range.data = data;
        range.size = size;
        staging->Copy(1, &range, false);

        ImageBarrierInfo barrier{};
        barrier.image = this;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.layout = vk::ImageLayout::eTransferDstOptimal;
        barrier.baseArrayLayer = baseArrayLayer;
        barrier.arrayLayers = layerCount ? layerCount : m_createInfo.arrayLayers;
        barrier.baseMipLevel = mipLevel;
        barrier.mipLevels = m_createInfo.mipLevels; // we need to transition all mip levels here, so they are all in the same state
        Barrier(cmd, barrier);

        vk::BufferImageCopy2 region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = GetAspectMask(m_createInfo.format);
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : m_createInfo.arrayLayers;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{m_createInfo.extent.width, m_createInfo.extent.height, m_createInfo.extent.depth};

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = staging->ApiHandle();
        copyInfo.dstImage = m_apiHandle;
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyBufferToImage2(copyInfo);
        cmd->AddAfterWaitCallback([staging]()
                                  { Buffer *buf = staging;
                                    Buffer::Destroy(buf); });
    }

    // TODO: add multiple regions for all mip levels
    void Image::CopyImage(CommandBuffer *cmd, Image *src)
    {
        PE_ERROR_IF(m_createInfo.extent.width != src->m_createInfo.extent.width && m_createInfo.extent.height != src->m_createInfo.extent.height,
                    "Image sizes are different");

        cmd->BeginDebugRegion("CopyImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = src;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[0].accessMask = vk::AccessFlagBits2::eTransferRead;
        barriers[0].layout = vk::ImageLayout::eTransferSrcOptimal;
        barriers[1].image = this;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[1].accessMask = vk::AccessFlagBits2::eTransferWrite;
        barriers[1].layout = vk::ImageLayout::eTransferDstOptimal;
        Image::Barriers(cmd, barriers);

        vk::ImageCopy2 region{};
        region.srcSubresource.aspectMask = GetAspectMask(m_createInfo.format);
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcSubresource.mipLevel = 0;
        region.srcOffset = vk::Offset3D{0, 0, 0};
        region.dstSubresource.aspectMask = GetAspectMask(m_createInfo.format);
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstSubresource.mipLevel = 0;
        region.dstOffset = vk::Offset3D{0, 0, 0};
        region.extent = m_createInfo.extent;

        vk::CopyImageInfo2 copyInfo{};
        copyInfo.srcImage = src->m_apiHandle;
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstImage = m_apiHandle;
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->BeginDebugRegion(src->m_name + " -> " + m_name);
        cmd->ApiHandle().copyImage2(copyInfo);
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
    void Image::BlitImage(CommandBuffer *cmd, Image *src, const vk::ImageBlit &region, vk::Filter filter)
    {
        PE_ERROR_IF(!cmd, "BlitImage(): Command buffer is null.");

        cmd->BeginDebugRegion("BlitImage");

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = this;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[0].accessMask = vk::AccessFlagBits2::eTransferWrite;
        barriers[0].layout = vk::ImageLayout::eTransferDstOptimal;
        barriers[0].baseArrayLayer = region.srcSubresource.baseArrayLayer;
        barriers[0].arrayLayers = region.srcSubresource.layerCount;
        barriers[0].baseMipLevel = region.srcSubresource.mipLevel;
        barriers[0].mipLevels = 1;

        barriers[1].image = src;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[1].accessMask = vk::AccessFlagBits2::eTransferRead;
        barriers[1].layout = vk::ImageLayout::eTransferSrcOptimal;
        barriers[1].baseArrayLayer = region.dstSubresource.baseArrayLayer;
        barriers[1].arrayLayers = region.dstSubresource.layerCount;
        barriers[1].baseMipLevel = region.dstSubresource.mipLevel;
        barriers[1].mipLevels = 1;

        Image::Barriers(cmd, barriers);

        cmd->BeginDebugRegion(src->m_name + " -> " + m_name);
        cmd->ApiHandle().blitImage(src->ApiHandle(), vk::ImageLayout::eTransferSrcOptimal,
                                   ApiHandle(), vk::ImageLayout::eTransferDstOptimal,
                                   1, &region,
                                   filter);
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }
}
