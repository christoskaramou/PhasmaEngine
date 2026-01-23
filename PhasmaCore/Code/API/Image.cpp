#include "API/Image.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Downsampler/Downsampler.h"
#include "API/RHI.h"
#include "API/StagingManager.h"
#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

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

    ImageView::ImageView(Image *parent, const vk::ImageViewCreateInfo &info, const std::string &name)
        : m_parent{parent}, m_info{info}, m_name{name}
    {
        m_apiHandle = RHII.GetDevice().createImageView(info);
        Debug::SetObjectName(m_apiHandle, name);
    }

    ImageView::~ImageView()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroyImageView(m_apiHandle);
    }

    vk::ImageViewCreateInfo ImageView::CreateInfoInit()
    {
        // pNext                 = {};
        // flags                 = {};
        // image                 = {};
        // viewType              = VULKAN_HPP_NAMESPACE::ImageViewType::e1D;
        // format                = VULKAN_HPP_NAMESPACE::Format::eUndefined;
        // components            = {};
        // subresourceRange      = {};

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        return viewInfo;
    }

    Image *Image::LoadRGBA(CommandBuffer *cmd, const std::string &path, vk::Format format, bool isFloat)
    {
        int texWidth, texHeight, texChannels;
        void *pixels = nullptr;
        if (isFloat)
            pixels = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        else
            pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        PE_ERROR_IF(!pixels, "No pixel data loaded");

        vk::ImageCreateInfo info = CreateInfoInit();
        info.format = format;
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

        cmd->CopyDataToImageStaged(image, pixels, texWidth * texHeight * (isFloat ? 16 : 4));
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

    Image *Image::LoadRGBA8(CommandBuffer *cmd, const std::string &path)
    {
        return LoadRGBA(cmd, path, vk::Format::eR8G8B8A8Unorm, false);
    }

    Image *Image::LoadRGBA32F(CommandBuffer *cmd, const std::string &path)
    {
        return LoadRGBA(cmd, path, vk::Format::eR32G32B32A32Sfloat, true);
    }

    Image *Image::LoadRaw(CommandBuffer *cmd, const std::string &path, const LoadRawParams &params)
    {
        std::vector<uint8_t> bytes;
        {
            FileSystem f(path, std::ios::in | std::ios::binary);
            PE_ERROR_IF(!f.IsOpen(), ("Failed to open raw image file: " + path).c_str());
            bytes = f.ReadAllBytes();
        }

        uint32_t bytesPerPixel = 0;
        switch (params.format)
        {
        case vk::Format::eR16G16Sfloat:
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
            bytesPerPixel = 4;
            break;
        case vk::Format::eR16G16B16A16Sfloat:
            bytesPerPixel = 8;
            break;
        case vk::Format::eR32G32B32A32Sfloat:
            bytesPerPixel = 16;
            break;

        default:
            PE_ERROR("Unsupported format for LoadRaw");
            break;
        }

        const size_t expectedSize = params.width * params.height * bytesPerPixel;
        PE_ERROR_IF(bytes.size() != expectedSize, ("Raw image size mismatch. Expected " + std::to_string(expectedSize) + " bytes, got " + std::to_string(bytes.size())).c_str());

        vk::ImageCreateInfo info = CreateInfoInit();
        info.format = params.format;
        info.extent = vk::Extent3D{params.width, params.height, 1};
        info.mipLevels = params.generateMips ? Image::CalculateMips(params.width, params.height) : 1;
        info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        if (params.generateMips)
            info.usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
        info.initialLayout = vk::ImageLayout::eUndefined;

        Image *image = Image::Create(info, path);
        image->m_clearColor = Color::Transparent;
        image->CreateSRV(vk::ImageViewType::e2D);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = params.generateMips ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
        if (params.clampToEdge)
        {
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        }
        samplerInfo.mipLodBias = params.mipLodBias;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = params.generateMips ? static_cast<float>(info.mipLevels) : 0.0f;
        image->m_sampler = Sampler::Create(samplerInfo);

        cmd->CopyDataToImageStaged(image, bytes.data(), static_cast<uint32_t>(bytes.size()));

        if (params.generateMips)
            cmd->GenerateMipMaps(image);

        ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader |
                             vk::PipelineStageFlagBits2::eComputeShader |
                             vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);

        return image;
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

        vmaSetAllocationName(RHII.GetAllocator(), m_allocation, m_name.c_str());
        Debug::SetObjectName(m_apiHandle, name);
    }

    Image::~Image()
    {
        if (m_rtv)
            ImageView::Destroy(m_rtv);

        if (m_srv)
            ImageView::Destroy(m_srv);

        for (auto &view : m_srvs)
        {
            if (view)
                ImageView::Destroy(view);
        }
        m_srvs.clear();

        for (auto &view : m_uavs)
        {
            if (view)
                ImageView::Destroy(view);
        }
        m_uavs.clear();

        if (m_sampler)
            Sampler::Destroy(m_sampler);

        if (m_apiHandle)
            vmaDestroyImage(RHII.GetAllocator(), m_apiHandle, m_allocation);
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
        barrier.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(image.m_createInfo.format);
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
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->m_apiHandle;
            barrier.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(imageInfo.format);
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

    void Image::CreateRTV()
    {
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eColorAttachment ||
                      m_createInfo.usage & vk::ImageUsageFlagBits::eDepthStencilAttachment),
                    "Image was not created with ColorAttachmentBit or DepthStencilAttachmentBit for RTV usage");

        if (m_rtv)
        {
            PE_INFO("Image::CreateRTV: RTV already exists, recreating.");
            ImageView::Destroy(m_rtv);
        }

        vk::ImageViewCreateInfo viewInfo = ImageView::CreateInfoInit();
        viewInfo.image = m_apiHandle;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_createInfo.format;
        viewInfo.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_createInfo.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_createInfo.arrayLayers;
        m_rtv = ImageView::Create(this, viewInfo, m_name + "_RTV");
    }

    void Image::CreateSRV(vk::ImageViewType type, int mip)
    {
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eSampled), "Image was not created with SampledBit for SRV usage");

        vk::ImageViewCreateInfo viewInfo = ImageView::CreateInfoInit();
        viewInfo.image = m_apiHandle;
        viewInfo.viewType = type;
        viewInfo.format = m_createInfo.format;
        viewInfo.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        viewInfo.subresourceRange.baseMipLevel = mip == -1 ? 0 : mip;
        viewInfo.subresourceRange.levelCount = mip == -1 ? m_createInfo.mipLevels : 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_createInfo.arrayLayers;
        ImageView *view = ImageView::Create(this, viewInfo, m_name + "_SRV");

        if (mip == -1)
        {
            if (m_srv)
            {
                PE_INFO("Image::CreateSRV: SRV already exists, recreating.");
                ImageView::Destroy(m_srv);
            }
            m_srv = view;
        }
        else
        {
            if (m_srvs[mip])
            {
                PE_INFO("Image::CreateSRV: SRV for mip {} already exists, recreating.", mip);
                ImageView::Destroy(m_srvs[mip]);
            }
            m_srvs[mip] = view;
        }
    }

    void Image::CreateUAV(vk::ImageViewType type, uint32_t mip)
    {
        PE_ERROR_IF(!(m_createInfo.usage & vk::ImageUsageFlagBits::eStorage), "Image was not created with StorageBit for UAV usage");
        if (m_uavs[mip])
        {
            PE_INFO("Image::CreateUAV: UAV for mip {} already exists, recreating.", mip);
            ImageView::Destroy(m_uavs[mip]);
        }

        vk::ImageViewCreateInfo viewInfo = ImageView::CreateInfoInit();
        viewInfo.image = m_apiHandle;
        viewInfo.viewType = type;
        viewInfo.format = m_createInfo.format;
        viewInfo.subresourceRange.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        viewInfo.subresourceRange.baseMipLevel = mip;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_createInfo.arrayLayers;
        m_uavs[mip] = ImageView::Create(this, viewInfo, m_name + "_UAV");
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;

        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    void Image::CopyDataToImageStaged(CommandBuffer *cmd,
                                      void *data,
                                      size_t size,
                                      uint32_t baseArrayLayer,
                                      uint32_t layerCount,
                                      uint32_t mipLevel)
    {
        PE_ERROR_IF(!cmd, "Image::CopyDataToImageStaged(): no command buffer specified.");

        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(size);
        std::memcpy(alloc.data, data, size);
        alloc.buffer->Flush(size, 0);

        ImageBarrierInfo barrier{};
        barrier.image = this;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.layout = vk::ImageLayout::eTransferDstOptimal;
        barrier.baseArrayLayer = baseArrayLayer;
        barrier.arrayLayers = layerCount ? layerCount : m_createInfo.arrayLayers;
        barrier.baseMipLevel = mipLevel;
        barrier.mipLevels = m_createInfo.mipLevels;
        Barrier(cmd, barrier);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = baseArrayLayer;
        region.imageSubresource.layerCount = layerCount ? layerCount : m_createInfo.arrayLayers;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{m_createInfo.extent.width, m_createInfo.extent.height, m_createInfo.extent.depth};

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = alloc.buffer->ApiHandle();
        copyInfo.dstImage = m_apiHandle;
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyBufferToImage2(copyInfo);

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                  { RHII.GetStagingManager()->SetUnused(alloc); });
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
        region.srcSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcSubresource.mipLevel = 0;
        region.srcOffset = vk::Offset3D{0, 0, 0};
        region.dstSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
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
    void Image::Blit(CommandBuffer *cmd, Image *src, const vk::ImageBlit &region, vk::Filter filter)
    {
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
                                   m_apiHandle, vk::ImageLayout::eTransferDstOptimal,
                                   1, &region,
                                   filter);
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }
} // namespace pe
