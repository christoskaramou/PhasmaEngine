#include "API/Image.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Downsampler/Downsampler.h"
#include "API/Helpers.h"
#include "API/RHI.h"
#include "API/StagingManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t MakeFourCC(const char a, const char b, const char c, const char d)
        {
            return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
        }

        uint32_t ReadU32(const uint8_t *data, size_t offset)
        {
            uint32_t value = 0;
            std::memcpy(&value, data + offset, sizeof(value));
            return value;
        }

        struct DdsInfo
        {
            vk::Format format = vk::Format::eUndefined;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            uint32_t blockBytes = 0;
            size_t dataOffset = 0;
            size_t dataSize = 0;
        };

        bool MapDxgiToVkFormat(uint32_t dxgiFormat, vk::Format &format, uint32_t &blockBytes)
        {
            switch (dxgiFormat)
            {
            case 71: // DXGI_FORMAT_BC1_UNORM
                format = vk::Format::eBc1RgbaUnormBlock;
                blockBytes = 8;
                return true;
            case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
                format = vk::Format::eBc1RgbaSrgbBlock;
                blockBytes = 8;
                return true;
            case 74: // DXGI_FORMAT_BC2_UNORM
                format = vk::Format::eBc2UnormBlock;
                blockBytes = 16;
                return true;
            case 75: // DXGI_FORMAT_BC2_UNORM_SRGB
                format = vk::Format::eBc2SrgbBlock;
                blockBytes = 16;
                return true;
            case 77: // DXGI_FORMAT_BC3_UNORM
                format = vk::Format::eBc3UnormBlock;
                blockBytes = 16;
                return true;
            case 78: // DXGI_FORMAT_BC3_UNORM_SRGB
                format = vk::Format::eBc3SrgbBlock;
                blockBytes = 16;
                return true;
            case 80: // DXGI_FORMAT_BC4_UNORM
                format = vk::Format::eBc4UnormBlock;
                blockBytes = 8;
                return true;
            case 81: // DXGI_FORMAT_BC4_SNORM
                format = vk::Format::eBc4SnormBlock;
                blockBytes = 8;
                return true;
            case 83: // DXGI_FORMAT_BC5_UNORM
                format = vk::Format::eBc5UnormBlock;
                blockBytes = 16;
                return true;
            case 84: // DXGI_FORMAT_BC5_SNORM
                format = vk::Format::eBc5SnormBlock;
                blockBytes = 16;
                return true;
            case 95: // DXGI_FORMAT_BC6H_UF16
                format = vk::Format::eBc6HUfloatBlock;
                blockBytes = 16;
                return true;
            case 96: // DXGI_FORMAT_BC6H_SF16
                format = vk::Format::eBc6HSfloatBlock;
                blockBytes = 16;
                return true;
            case 98: // DXGI_FORMAT_BC7_UNORM
                format = vk::Format::eBc7UnormBlock;
                blockBytes = 16;
                return true;
            case 99: // DXGI_FORMAT_BC7_UNORM_SRGB
                format = vk::Format::eBc7SrgbBlock;
                blockBytes = 16;
                return true;
            default:
                return false;
            }
        }

        bool MapLegacyDdsToVkFormat(uint32_t fourCC, vk::Format &format, uint32_t &blockBytes)
        {
            switch (fourCC)
            {
            case MakeFourCC('D', 'X', 'T', '1'):
                format = vk::Format::eBc1RgbaUnormBlock;
                blockBytes = 8;
                return true;
            case MakeFourCC('D', 'X', 'T', '3'):
                format = vk::Format::eBc2UnormBlock;
                blockBytes = 16;
                return true;
            case MakeFourCC('D', 'X', 'T', '5'):
                format = vk::Format::eBc3UnormBlock;
                blockBytes = 16;
                return true;
            case MakeFourCC('A', 'T', 'I', '1'):
            case MakeFourCC('B', 'C', '4', 'U'):
                format = vk::Format::eBc4UnormBlock;
                blockBytes = 8;
                return true;
            case MakeFourCC('B', 'C', '4', 'S'):
                format = vk::Format::eBc4SnormBlock;
                blockBytes = 8;
                return true;
            case MakeFourCC('A', 'T', 'I', '2'):
            case MakeFourCC('B', 'C', '5', 'U'):
                format = vk::Format::eBc5UnormBlock;
                blockBytes = 16;
                return true;
            case MakeFourCC('B', 'C', '5', 'S'):
                format = vk::Format::eBc5SnormBlock;
                blockBytes = 16;
                return true;
            default:
                return false;
            }
        }

        bool ParseDdsInfo(const std::vector<uint8_t> &fileData, DdsInfo &outInfo, std::string &reason)
        {
            constexpr size_t kMagicSize = 4;
            constexpr size_t kHeaderSize = 124;
            constexpr size_t kDx10HeaderSize = 20;

            if (fileData.size() < kMagicSize + kHeaderSize)
            {
                reason = "file too small";
                return false;
            }

            if (std::memcmp(fileData.data(), "DDS ", kMagicSize) != 0)
            {
                reason = "invalid DDS magic";
                return false;
            }

            const uint32_t headerSize = ReadU32(fileData.data(), 4);
            if (headerSize != kHeaderSize)
            {
                reason = "invalid DDS header size";
                return false;
            }

            outInfo.height = ReadU32(fileData.data(), 12);
            outInfo.width = ReadU32(fileData.data(), 16);
            outInfo.mipLevels = ReadU32(fileData.data(), 28);
            if (outInfo.width == 0 || outInfo.height == 0)
            {
                reason = "invalid texture dimensions";
                return false;
            }
            if (outInfo.mipLevels == 0)
                outInfo.mipLevels = 1;

            const uint32_t fourCC = ReadU32(fileData.data(), 84);
            const uint32_t dx10FourCC = MakeFourCC('D', 'X', '1', '0');

            size_t dataOffset = kMagicSize + kHeaderSize;
            if (fourCC == dx10FourCC)
            {
                if (fileData.size() < dataOffset + kDx10HeaderSize)
                {
                    reason = "incomplete DDS DX10 header";
                    return false;
                }

                const uint32_t dxgiFormat = ReadU32(fileData.data(), dataOffset + 0);
                const uint32_t resourceDimension = ReadU32(fileData.data(), dataOffset + 4);
                const uint32_t arraySize = ReadU32(fileData.data(), dataOffset + 12);

                if (resourceDimension != 3 || arraySize != 1)
                {
                    reason = "unsupported DDS resource type (only 2D non-array textures are supported)";
                    return false;
                }

                if (!MapDxgiToVkFormat(dxgiFormat, outInfo.format, outInfo.blockBytes))
                {
                    reason = "unsupported DDS DXGI format " + std::to_string(dxgiFormat);
                    return false;
                }

                dataOffset += kDx10HeaderSize;
            }
            else
            {
                if (!MapLegacyDdsToVkFormat(fourCC, outInfo.format, outInfo.blockBytes))
                {
                    reason = "unsupported DDS FourCC";
                    return false;
                }
            }

            size_t requiredBytes = 0;
            for (uint32_t mip = 0; mip < outInfo.mipLevels; ++mip)
            {
                const uint32_t mipWidth = std::max(1u, outInfo.width >> mip);
                const uint32_t mipHeight = std::max(1u, outInfo.height >> mip);
                const size_t blockWidth = (mipWidth + 3u) / 4u;
                const size_t blockHeight = (mipHeight + 3u) / 4u;
                const size_t mipSize = blockWidth * blockHeight * outInfo.blockBytes;
                requiredBytes += mipSize;
            }

            if (dataOffset + requiredBytes > fileData.size())
            {
                reason = "DDS data payload is incomplete";
                return false;
            }

            outInfo.dataOffset = dataOffset;
            outInfo.dataSize = requiredBytes;
            return true;
        }

        Image *LoadDdsCompressed(CommandBuffer *cmd, const std::string &path, const std::vector<uint8_t> &fileData)
        {
            DdsInfo dds{};
            std::string reason;
            if (!ParseDdsInfo(fileData, dds, reason))
            {
                PE_WARN("[Image] Failed to parse DDS '%s': %s", path.c_str(), reason.c_str());
                return nullptr;
            }

            vk::ImageCreateInfo info = Image::CreateInfoInit();
            info.format = dds.format;
            info.extent = vk::Extent3D{dds.width, dds.height, 1};
            info.mipLevels = dds.mipLevels;
            info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
            info.initialLayout = vk::ImageLayout::eUndefined;

            Image *image = Image::Create(info, path);
            image->SetClearColor(Color::Transparent);
            image->CreateSRV(vk::ImageViewType::e2D);

            vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
            samplerInfo.maxLod = static_cast<float>(dds.mipLevels);
            samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
            image->SetSampler(Sampler::Create(samplerInfo));

            StagingAllocation alloc = RHII.GetStagingManager()->Allocate(dds.dataSize);
            std::memcpy(alloc.data, fileData.data() + dds.dataOffset, dds.dataSize);
            alloc.buffer->Flush(dds.dataSize, 0);

            ImageBarrierInfo toTransfer{};
            toTransfer.image = image;
            toTransfer.layout = vk::ImageLayout::eTransferDstOptimal;
            toTransfer.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
            toTransfer.accessMask = vk::AccessFlagBits2::eTransferWrite;
            toTransfer.baseMipLevel = 0;
            toTransfer.mipLevels = dds.mipLevels;
            cmd->ImageBarrier(toTransfer);

            std::vector<vk::BufferImageCopy2> regions;
            regions.reserve(dds.mipLevels);

            size_t offset = 0;
            for (uint32_t mip = 0; mip < dds.mipLevels; ++mip)
            {
                const uint32_t mipWidth = std::max(1u, dds.width >> mip);
                const uint32_t mipHeight = std::max(1u, dds.height >> mip);
                const size_t blockWidth = (mipWidth + 3u) / 4u;
                const size_t blockHeight = (mipHeight + 3u) / 4u;
                const size_t mipSize = blockWidth * blockHeight * dds.blockBytes;

                vk::BufferImageCopy2 region{};
                region.bufferOffset = static_cast<vk::DeviceSize>(offset);
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = mip;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D{0, 0, 0};
                region.imageExtent = vk::Extent3D{mipWidth, mipHeight, 1};
                regions.push_back(region);

                offset += mipSize;
            }

            vk::CopyBufferToImageInfo2 copyInfo{};
            copyInfo.srcBuffer = alloc.buffer->ApiHandle();
            copyInfo.dstImage = image->ApiHandle();
            copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
            copyInfo.regionCount = static_cast<uint32_t>(regions.size());
            copyInfo.pRegions = regions.data();
            cmd->ApiHandle().copyBufferToImage2(copyInfo);

            cmd->AddAfterWaitCallback([alloc = std::move(alloc)]() mutable
                                      { RHII.GetStagingManager()->SetUnused(alloc); });

            ImageBarrierInfo toRead{};
            toRead.image = image;
            toRead.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toRead.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader |
                                vk::PipelineStageFlagBits2::eComputeShader |
                                vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
            toRead.accessMask = vk::AccessFlagBits2::eShaderRead;
            toRead.baseMipLevel = 0;
            toRead.mipLevels = dds.mipLevels;
            cmd->ImageBarrier(toRead);

            return image;
        }
    } // namespace

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

    static Image *CreateImageAndUpload(CommandBuffer *cmd, const std::string &name, void *data, size_t size, uint32_t width, uint32_t height, vk::Format format, uint32_t mipLevels, vk::ImageUsageFlags usage, const vk::SamplerCreateInfo &samplerInfo)
    {
        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.format = format;
        info.extent = vk::Extent3D{width, height, 1};
        info.mipLevels = mipLevels;
        info.usage = usage;
        info.initialLayout = vk::ImageLayout::eUndefined;

        Image *image = Image::Create(info, name);
        image->SetClearColor(Color::Transparent);
        image->CreateSRV(vk::ImageViewType::e2D);
        image->SetSampler(Sampler::Create(samplerInfo));

        cmd->CopyDataToImageStaged(image, data, size);
        cmd->GenerateMipMaps(image);

        ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);

        return image;
    }

    Image *Image::LoadRGBA(CommandBuffer *cmd, const std::string &path, vk::Format format, bool isFloat)
    {
        // Read file to memory first to support unicode paths on Windows
        // stbi_load uses fopen which doesn't handle unicode on Windows
        std::vector<uint8_t> fileData;
        {
            FileSystem file(path, std::ios::in | std::ios::binary);
            if (!file.IsOpen())
            {
                PE_WARN("[Image] Failed to open image file: %s", path.c_str());
                return nullptr;
            }
            fileData = file.ReadAllBytes();
        }

        if (fileData.size() >= 4 && std::memcmp(fileData.data(), "DDS ", 4) == 0)
        {
            if (isFloat)
            {
                PE_WARN("[Image] DDS float decode is not supported for '%s'", path.c_str());
                return nullptr;
            }
            return LoadDdsCompressed(cmd, path, fileData);
        }

        int texWidth, texHeight, texChannels;
        void *pixels = nullptr;
        if (isFloat)
            pixels = stbi_loadf_from_memory(fileData.data(), static_cast<int>(fileData.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        else
            pixels = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels)
        {
            const char *reason = stbi_failure_reason();
            PE_WARN("[Image] Decode failed for '%s': %s", path.c_str(), reason ? reason : "unknown");
            return nullptr;
        }

        uint32_t mipLevels = Image::CalculateMips(texWidth, texHeight);
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;

        Image *image = CreateImageAndUpload(cmd, path, pixels, texWidth * texHeight * (isFloat ? 16 : 4), texWidth, texHeight, format, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBAFromMemory(CommandBuffer *cmd, void *data, int size, vk::Format format, bool isFloat)
    {
        int texWidth, texHeight, texChannels;
        void *pixels = nullptr;
        if (isFloat)
            pixels = stbi_loadf_from_memory(static_cast<stbi_uc *>(data), size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        else
            pixels = stbi_load_from_memory(static_cast<stbi_uc *>(data), size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels)
        {
            const char *reason = stbi_failure_reason();
            PE_WARN("[Image] Embedded image decode failed: %s", reason ? reason : "unknown");
            return nullptr;
        }

        uint32_t mipLevels = Image::CalculateMips(texWidth, texHeight);
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

        static int embeddedCount = 0;
        std::string name = "Embedded_Texture_" + std::to_string(embeddedCount++);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;

        Image *image = CreateImageAndUpload(cmd, name, pixels, texWidth * texHeight * (isFloat ? 16 : 4), texWidth, texHeight, format, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBA8FromMemory(CommandBuffer *cmd, void *data, int size)
    {
        return LoadRGBAFromMemory(cmd, data, size, vk::Format::eR8G8B8A8Unorm, false);
    }

    Image *Image::LoadRawFromMemory(CommandBuffer *cmd, void *data, uint32_t width, uint32_t height, vk::Format format, const std::string &name)
    {
        uint32_t mipLevels = Image::CalculateMips(width, height);
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

        static int embeddedCount = 0;
        std::string n = name.empty() ? "Embedded_Raw_" + std::to_string(embeddedCount++) : name;

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;

        // Assuming 4 bytes per pixel for raw load
        return CreateImageAndUpload(cmd, n, data, width * height * 4, width, height, format, mipLevels, usage, samplerInfo);
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
            PE_ERROR("[Image] Unsupported format for LoadRaw");
            break;
        }

        const size_t expectedSize = params.width * params.height * bytesPerPixel;
        PE_ERROR_IF(bytes.size() != expectedSize, ("Raw image size mismatch. Expected " + std::to_string(expectedSize) + " bytes, got " + std::to_string(bytes.size())).c_str());

        uint32_t mipLevels = params.generateMips ? Image::CalculateMips(params.width, params.height) : 1;
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        if (params.generateMips)
            usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;

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
        samplerInfo.maxLod = params.generateMips ? static_cast<float>(mipLevels) : 0.0f;

        return CreateImageAndUpload(cmd, path, bytes.data(), bytes.size(), params.width, params.height, params.format, mipLevels, usage, samplerInfo);
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

        {
            vk::FormatProperties fProps = RHII.GetGpu().getFormatProperties(info.format);
            if (fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferSrc)
                m_createInfo.usage |= vk::ImageUsageFlagBits::eTransferSrc;
        }

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
        Unload();
    }

    void Image::Unload()
    {
        ImageView::Destroy(m_rtv);
        ImageView::Destroy(m_srv);

        for (auto &view : m_srvs)
            ImageView::Destroy(view);
        m_srvs.clear();

        for (auto &view : m_uavs)
            ImageView::Destroy(view);
        m_uavs.clear();

        Sampler::Destroy(m_sampler);

        if (m_apiHandle)
        {
            vmaDestroyImage(RHII.GetAllocator(), m_apiHandle, m_allocation);
            m_apiHandle = VK_NULL_HANDLE;
        }
    }

    void Image::Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info)
    {
        PE_ERROR_IF(!info.image, "Image::Barrier: no image specified.");
        Image &image = *info.image;

        uint32_t mipLevels = info.mipLevels ? info.mipLevels : image.m_createInfo.mipLevels;
        uint32_t arrayLayers = info.arrayLayers ? info.arrayLayers : image.m_createInfo.arrayLayers;
        const ImageTrackInfo &oldInfo = image.m_trackInfos[info.baseArrayLayer][info.baseMipLevel];

        bool requestRead = VulkanHelpers::IsReadOnlyAccess(info.accessMask);
        bool previousRead = VulkanHelpers::IsReadOnlyAccess(oldInfo.accessMask);
        bool sameState = oldInfo.layout == info.layout &&
                         oldInfo.stageFlags == info.stageFlags &&
                         oldInfo.accessMask == info.accessMask &&
                         oldInfo.queueFamilyId == info.queueFamilyId;
        if (requestRead && previousRead && sameState)
            return;

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = oldInfo.stageFlags;
        barrier.dstStageMask = info.stageFlags;
        barrier.srcAccessMask = oldInfo.accessMask;
        barrier.dstAccessMask = info.accessMask;
        barrier.oldLayout = oldInfo.layout;
        barrier.newLayout = info.layout;
        if (barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone)
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcQueueFamilyIndex = oldInfo.queueFamilyId;
        barrier.dstQueueFamilyIndex = info.queueFamilyId;
        barrier.image = image.m_apiHandle;
        barrier.subresourceRange.aspectMask = image.m_aspectMaskOverride
                                                  ? image.m_aspectMaskOverride
                                                  : VulkanHelpers::GetAspectMask(image.m_createInfo.format);
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

            bool requestRead = VulkanHelpers::IsReadOnlyAccess(info.accessMask);
            bool previousRead = VulkanHelpers::IsReadOnlyAccess(oldInfo.accessMask);
            bool sameState = oldInfo.layout == info.layout &&
                             oldInfo.stageFlags == info.stageFlags &&
                             oldInfo.accessMask == info.accessMask &&
                             oldInfo.queueFamilyId == info.queueFamilyId;
            if (requestRead && previousRead && sameState)
                continue;

            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = oldInfo.stageFlags;
            barrier.dstStageMask = info.stageFlags;
            barrier.srcAccessMask = oldInfo.accessMask;
            barrier.dstAccessMask = info.accessMask;
            barrier.oldLayout = oldInfo.layout;
            barrier.newLayout = info.layout;
            if (barrier.srcStageMask == vk::PipelineStageFlagBits2::eNone)
                barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->m_apiHandle;
            barrier.subresourceRange.aspectMask = image->m_aspectMaskOverride
                                                      ? image->m_aspectMaskOverride
                                                      : VulkanHelpers::GetAspectMask(imageInfo.format);
            barrier.subresourceRange.baseMipLevel = info.baseMipLevel;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = info.baseArrayLayer;
            barrier.subresourceRange.layerCount = arrayLayers;
            barriers.push_back(barrier);

            for (uint32_t i = 0; i < arrayLayers; i++)
                for (uint32_t j = 0; j < mipLevels; j++)
                    image->m_trackInfos[info.baseArrayLayer + i][info.baseMipLevel + j] = info;
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

    void Image::CopyToBuffer(CommandBuffer *cmd, Buffer *dst)
    {
        cmd->BeginDebugRegion("CopyImageToBuffer");

        ImageBarrierInfo barrier{};
        barrier.image = this;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.layout = vk::ImageLayout::eTransferSrcOptimal;
        Image::Barrier(cmd, barrier);

        BufferBarrierInfo bufBarrier{};
        bufBarrier.buffer = dst;
        bufBarrier.stageMask = vk::PipelineStageFlagBits2::eTransfer;
        bufBarrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        cmd->BufferBarrier(bufBarrier);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VulkanHelpers::GetAspectMask(m_createInfo.format);
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = m_createInfo.extent;

        vk::CopyImageToBufferInfo2 copyInfo{};
        copyInfo.srcImage = m_apiHandle;
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstBuffer = dst->ApiHandle();
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyImageToBuffer2(copyInfo);
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
