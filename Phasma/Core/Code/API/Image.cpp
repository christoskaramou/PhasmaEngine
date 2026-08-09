#include "API/Image_Internal.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Downsampler/Downsampler.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanImageImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace pe
{
    Image::Impl *CreateImageImpl(Image *owner, const ImageDesc &desc)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            return new Dx12ImageImpl(owner, desc);
#endif
        return new VulkanImageImpl(owner, desc);
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
        Image_Barrier_Vulkan(cmd, info);
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
        Image_Barriers_Vulkan(cmd, infos);
    }

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
            ::PeFormat format = PE_FORMAT_UNDEFINED;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            uint32_t blockBytes = 0;
            size_t dataOffset = 0;
            size_t dataSize = 0;
        };

        bool MapDxgiToPeFormat(uint32_t dxgiFormat, ::PeFormat &format, uint32_t &blockBytes)
        {
            switch (dxgiFormat)
            {
            case 71:
                format = PE_FORMAT_BC1_RGBA_UNORM;
                blockBytes = 8;
                return true;
            case 72:
                format = PE_FORMAT_BC1_RGBA_SRGB;
                blockBytes = 8;
                return true;
            case 74:
                format = PE_FORMAT_BC2_UNORM;
                blockBytes = 16;
                return true;
            case 75:
                format = PE_FORMAT_BC2_SRGB;
                blockBytes = 16;
                return true;
            case 77:
                format = PE_FORMAT_BC3_UNORM;
                blockBytes = 16;
                return true;
            case 78:
                format = PE_FORMAT_BC3_SRGB;
                blockBytes = 16;
                return true;
            case 80:
                format = PE_FORMAT_BC4_UNORM;
                blockBytes = 8;
                return true;
            case 81:
                format = PE_FORMAT_BC4_SNORM;
                blockBytes = 8;
                return true;
            case 83:
                format = PE_FORMAT_BC5_UNORM;
                blockBytes = 16;
                return true;
            case 84:
                format = PE_FORMAT_BC5_SNORM;
                blockBytes = 16;
                return true;
            case 95:
                format = PE_FORMAT_BC6H_UFLOAT;
                blockBytes = 16;
                return true;
            case 96:
                format = PE_FORMAT_BC6H_SFLOAT;
                blockBytes = 16;
                return true;
            case 98:
                format = PE_FORMAT_BC7_UNORM;
                blockBytes = 16;
                return true;
            case 99:
                format = PE_FORMAT_BC7_SRGB;
                blockBytes = 16;
                return true;
            default:
                return false;
            }
        }

        bool MapLegacyDdsToPeFormat(uint32_t fourCC, ::PeFormat &format, uint32_t &blockBytes)
        {
            switch (fourCC)
            {
            case MakeFourCC('D', 'X', 'T', '1'):
                format = PE_FORMAT_BC1_RGBA_UNORM;
                blockBytes = 8;
                return true;
            case MakeFourCC('D', 'X', 'T', '3'):
                format = PE_FORMAT_BC2_UNORM;
                blockBytes = 16;
                return true;
            case MakeFourCC('D', 'X', 'T', '5'):
                format = PE_FORMAT_BC3_UNORM;
                blockBytes = 16;
                return true;
            case MakeFourCC('A', 'T', 'I', '1'):
            case MakeFourCC('B', 'C', '4', 'U'):
                format = PE_FORMAT_BC4_UNORM;
                blockBytes = 8;
                return true;
            case MakeFourCC('B', 'C', '4', 'S'):
                format = PE_FORMAT_BC4_SNORM;
                blockBytes = 8;
                return true;
            case MakeFourCC('A', 'T', 'I', '2'):
            case MakeFourCC('B', 'C', '5', 'U'):
                format = PE_FORMAT_BC5_UNORM;
                blockBytes = 16;
                return true;
            case MakeFourCC('B', 'C', '5', 'S'):
                format = PE_FORMAT_BC5_SNORM;
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

                if (!MapDxgiToPeFormat(dxgiFormat, outInfo.format, outInfo.blockBytes))
                {
                    reason = "unsupported DDS DXGI format " + std::to_string(dxgiFormat);
                    return false;
                }

                dataOffset += kDx10HeaderSize;
            }
            else
            {
                if (!MapLegacyDdsToPeFormat(fourCC, outInfo.format, outInfo.blockBytes))
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

            ImageDesc desc{};
            desc.format = dds.format;
            desc.width = dds.width;
            desc.height = dds.height;
            desc.mipLevels = dds.mipLevels;
            desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
            desc.initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
            desc.name = path;

            Image *image = Image::Create(desc);
            image->SetClearColor(Color::Transparent);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

            SamplerDesc samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
            samplerInfo.maxLod = static_cast<float>(dds.mipLevels);
            samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            image->SetSampler(Sampler::Create(samplerInfo));

            size_t offset = 0;
            for (uint32_t mip = 0; mip < dds.mipLevels; ++mip)
            {
                const uint32_t mipWidth = std::max(1u, dds.width >> mip);
                const uint32_t mipHeight = std::max(1u, dds.height >> mip);
                const size_t blockWidth = (mipWidth + 3u) / 4u;
                const size_t blockHeight = (mipHeight + 3u) / 4u;
                const size_t mipSize = blockWidth * blockHeight * dds.blockBytes;

                cmd->CopyDataToImageStaged(image,
                                           const_cast<uint8_t *>(fileData.data() + dds.dataOffset + offset),
                                           mipSize,
                                           0,
                                           1,
                                           mip);
                offset += mipSize;
            }
            PE_ERROR_IF(offset != dds.dataSize, "DDS upload byte count mismatch for '%s'", path.c_str());

            ImageBarrierInfo toRead{};
            toRead.image = image;
            toRead.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.stageFlags = PE_STAGE_FRAGMENT_SHADER |
                                PE_STAGE_COMPUTE_SHADER |
                                PE_STAGE_RAY_TRACING_SHADER_KHR;
            toRead.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            toRead.baseMipLevel = 0;
            toRead.mipLevels = dds.mipLevels;
            cmd->ImageBarrier(toRead);

            return image;
        }

        Image *CreateImageAndUpload(CommandBuffer *cmd,
                                    const std::string &name,
                                    void *data,
                                    size_t size,
                                    uint32_t width,
                                    uint32_t height,
                                    ::PeFormat format,
                                    uint32_t mipLevels,
                                    PeImageUsageFlags usage,
                                    const SamplerDesc &samplerInfo)
        {
            ImageDesc desc{};
            desc.format = format;
            desc.width = width;
            desc.height = height;
            desc.mipLevels = mipLevels;
            desc.usage = usage;
            desc.initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
            desc.name = name;

            Image *image = Image::Create(desc);
            image->SetClearColor(Color::Transparent);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            image->SetSampler(Sampler::Create(samplerInfo));

            cmd->CopyDataToImageStaged(image, data, size);
            cmd->GenerateMipMaps(image);

            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER |
                                 PE_STAGE_COMPUTE_SHADER |
                                 PE_STAGE_RAY_TRACING_SHADER_KHR;
            barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(barrier);

            return image;
        }
    } // namespace

    Image *Image::Create(const ImageDesc &desc)
    {
        Image *image = new Image(desc);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Image), reinterpret_cast<void *>(image));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Image created (Handle: %p)", reinterpret_cast<void *>(image));
#endif
#endif
        return image;
    }

    void Image::Destroy(Image *&image)
    {
        if (image)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Image destroyed (Handle: %p)", reinterpret_cast<void *>(image));
#endif
            delete image;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Image), reinterpret_cast<void *>(image));
#endif
            image = nullptr;
        }
    }

    std::vector<Image *> Image::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Image));
        std::vector<Image *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Image *>(p));
        return out;
#else
        return {};
#endif
    }

    Image::Image(const ImageDesc &desc)
        : m_width{desc.width},
          m_height{desc.height},
          m_depth{desc.depth},
          m_mipLevels{desc.mipLevels},
          m_arrayLayers{desc.arrayLayers},
          m_format{desc.format},
          m_usage{desc.usage},
          m_samples{desc.samples},
          m_imageType{desc.imageType},
          m_clearColor{desc.clearColor},
          m_name{desc.name},
          m_srvs(desc.mipLevels, nullptr),
          m_uavs(desc.mipLevels, nullptr)
    {
        PE_ERROR_IF(!m_mipLevels, "Image: Mip levels cannot be zero.");
        PE_ERROR_IF(!m_arrayLayers, "Image: Array layers cannot be zero.");

        m_trackInfos.resize(m_arrayLayers);
        for (uint32_t i = 0; i < m_arrayLayers; i++)
        {
            m_trackInfos[i] = std::vector<ImageTrackInfo>(m_mipLevels);
            for (uint32_t j = 0; j < m_mipLevels; j++)
            {
                ImageTrackInfo info{};
                info.image = this;
                info.layout = desc.initialLayout;
                m_trackInfos[i][j] = info;
            }
        }

        m_impl = CreateImageImpl(this, desc);
        // Re-read usage from the impl: the Vulkan path may add eTransferSrc
        // when the format supports it. Mirror that on the public usage so
        // CreateRTV/SRV/UAV checks see the effective set.
        // (The public m_usage already matches desc.usage; transfer_src auto-add
        // is purely backend-internal and not asserted on the public surface.)
    }

    Image::Image() = default;

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

        delete m_impl;
        m_impl = nullptr;
    }

    void Image::SetCurrentInfoAll(const ImageTrackInfo &info)
    {
        for (uint32_t i = 0; i < m_trackInfos.size(); i++)
            for (uint32_t j = 0; j < m_trackInfos[i].size(); j++)
                m_trackInfos[i][j] = info;
    }

    void Image::CreateRTV()
    {
        m_impl->CreateRTV();
    }

    void Image::CreateSRV(PeImageViewType type, int mip)
    {
        m_impl->CreateSRV(type, mip);
    }

    void Image::CreateUAV(PeImageViewType type, uint32_t mip)
    {
        m_impl->CreateUAV(type, mip);
    }

    uint32_t Image::CalculateMips(uint32_t width, uint32_t height)
    {
        static const uint32_t maxMips = 12;
        const uint32_t resolution = max(width, height);
        const uint32_t mips = static_cast<uint32_t>(floor(log2(static_cast<double>(resolution)))) + 1;
        return min(mips, maxMips);
    }

    void Image::Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info)
    {
        Image_Barrier_Backend(cmd, info);
    }

    void Image::Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos)
    {
        Image_Barriers_Backend(cmd, infos);
    }

    void Image::CopyDataToImageStaged(CommandBuffer *cmd,
                                      void *data,
                                      size_t size,
                                      uint32_t baseArrayLayer,
                                      uint32_t layerCount,
                                      uint32_t mipLevel)
    {
        m_impl->CopyDataToImageStaged(cmd, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void Image::CopyImage(CommandBuffer *cmd, Image *src)
    {
        m_impl->CopyImage(cmd, src);
    }

    void Image::CopyToBuffer(CommandBuffer *cmd, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        m_impl->CopyToBuffer(cmd, dst, mipLevel, baseArrayLayer, layerCount);
    }

    void Image::GenerateMipMaps(CommandBuffer *cmd)
    {
        if (m_mipmapsGenerated || m_mipLevels < 2)
            return;

        Downsampler::Dispatch(cmd, this);
        m_mipmapsGenerated = true;
    }

    void Image::Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter)
    {
        m_impl->Blit(cmd, src, region, filter);
    }

    Image *Image::LoadRGBA(CommandBuffer *cmd, const std::string &path, ::PeFormat format, bool isFloat)
    {
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
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST |
                                  PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        Image *image = CreateImageAndUpload(cmd, path, pixels, texWidth * texHeight * (isFloat ? 16 : 4),
                                            texWidth, texHeight, format, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBAFromMemory(CommandBuffer *cmd, void *data, int size, ::PeFormat format, bool isFloat)
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
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST |
                                  PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;

        static int embeddedCount = 0;
        std::string name = "Embedded_Texture_" + std::to_string(embeddedCount++);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        Image *image = CreateImageAndUpload(cmd, name, pixels, texWidth * texHeight * (isFloat ? 16 : 4),
                                            texWidth, texHeight, format, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBA8FromMemory(CommandBuffer *cmd, void *data, int size)
    {
        return LoadRGBAFromMemory(cmd, data, size, PE_FORMAT_R8G8B8A8_UNORM, false);
    }

    Image *Image::LoadRawFromMemory(CommandBuffer *cmd, void *data, uint32_t width, uint32_t height, ::PeFormat format, const std::string &name)
    {
        uint32_t mipLevels = Image::CalculateMips(width, height);
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST |
                                  PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;

        static int embeddedCount = 0;
        std::string n = name.empty() ? "Embedded_Raw_" + std::to_string(embeddedCount++) : name;

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        // Assuming 4 bytes per pixel for raw load
        return CreateImageAndUpload(cmd, n, data, width * height * 4, width, height, format, mipLevels, usage, samplerInfo);
    }

    Image *Image::LoadRGBA8(CommandBuffer *cmd, const std::string &path)
    {
        return LoadRGBA(cmd, path, PE_FORMAT_R8G8B8A8_UNORM, false);
    }

    Image *Image::LoadWhitenedRGBA8(CommandBuffer *cmd, const std::string &path, float amount)
    {
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

        int texWidth = 0, texHeight = 0, texChannels = 0;
        stbi_uc *pixels = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                                &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels)
        {
            const char *reason = stbi_failure_reason();
            PE_WARN("[Image] Whitened image decode failed for '%s': %s", path.c_str(), reason ? reason : "unknown");
            return nullptr;
        }

        amount = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f;
        const int count = texWidth * texHeight;
        for (int i = 0; i < count; ++i)
        {
            stbi_uc *p = pixels + i * 4;
            const float luminance = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
            const float lift = amount * luminance;
            for (int channel = 0; channel < 3; ++channel)
                p[channel] = static_cast<stbi_uc>(std::lround(p[channel] + (255.0f - p[channel]) * lift));
        }

        uint32_t mipLevels = Image::CalculateMips(texWidth, texHeight);
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST |
                                  PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        const std::string imageName = path + "##whitened_" + std::to_string(static_cast<int>(std::lround(amount * 1000.0f)));
        Image *image = CreateImageAndUpload(cmd, imageName, pixels,
                                            static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4,
                                            texWidth, texHeight, PE_FORMAT_R8G8B8A8_UNORM, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadColorizeMaskRGBA8(CommandBuffer *cmd, const std::string &path)
    {
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

        int texWidth = 0, texHeight = 0, texChannels = 0;
        stbi_uc *pixels = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                                &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels)
        {
            const char *reason = stbi_failure_reason();
            PE_WARN("[Image] Colorize-mask decode failed for '%s': %s", path.c_str(), reason ? reason : "unknown");
            return nullptr;
        }

        const int count = texWidth * texHeight;
        for (int i = 0; i < count; ++i)
        {
            stbi_uc *p = pixels + i * 4;
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            // Keep source alpha as coverage; RGB is forced white so tint SETS color.
        }

        uint32_t mipLevels = Image::CalculateMips(texWidth, texHeight);
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST |
                                  PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        const std::string maskName = path + "##colorize_mask";
        Image *image = CreateImageAndUpload(cmd, maskName, pixels,
                                            static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4,
                                            texWidth, texHeight, PE_FORMAT_R8G8B8A8_UNORM, mipLevels, usage, samplerInfo);

        cmd->AddAfterWaitCallback([pixels]()
                                  { stbi_image_free(pixels); });

        return image;
    }

    Image *Image::LoadRGBA32F(CommandBuffer *cmd, const std::string &path)
    {
        return LoadRGBA(cmd, path, PE_FORMAT_R32G32B32A32_SFLOAT, true);
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
        case PE_FORMAT_R16G16_SFLOAT:
        case PE_FORMAT_R8G8B8A8_UNORM:
        case PE_FORMAT_R8G8B8A8_SRGB:
            bytesPerPixel = 4;
            break;
        case PE_FORMAT_R16G16B16A16_SFLOAT:
            bytesPerPixel = 8;
            break;
        case PE_FORMAT_R32G32B32A32_SFLOAT:
            bytesPerPixel = 16;
            break;
        default:
            PE_ERROR("[Image] Unsupported format for LoadRaw");
            break;
        }

        const size_t expectedSize = params.width * params.height * bytesPerPixel;
        PE_ERROR_IF(bytes.size() != expectedSize, ("Raw image size mismatch. Expected " + std::to_string(expectedSize) + " bytes, got " + std::to_string(bytes.size())).c_str());

        uint32_t mipLevels = params.generateMips ? Image::CalculateMips(params.width, params.height) : 1;
        PeImageUsageFlags usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        if (params.generateMips)
            usage |= PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_STORAGE;

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.minFilter = PE_FILTER_LINEAR;
        samplerInfo.magFilter = PE_FILTER_LINEAR;
        samplerInfo.mipmapMode = params.generateMips ? PE_SAMPLER_MIPMAP_MODE_LINEAR : PE_SAMPLER_MIPMAP_MODE_NEAREST;
        if (params.clampToEdge)
        {
            samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
        samplerInfo.mipLodBias = params.mipLodBias;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = params.generateMips ? static_cast<float>(mipLevels) : 0.0f;

        return CreateImageAndUpload(cmd, path, bytes.data(), bytes.size(), params.width, params.height, params.format, mipLevels, usage, samplerInfo);
    }
} // namespace pe
