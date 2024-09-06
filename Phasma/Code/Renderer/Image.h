#pragma once

#include "Renderer/Pipeline.h"

namespace pe
{
    class Context;
    class CommandBuffer;
    class Buffer;
    class Image;
    struct ImageBlit;

    struct ImageBarrierInfo : public BarrierInfo
    {
        ImageBarrierInfo() { type = BarrierType::Image; }
        Image *image = nullptr;
        ImageLayout layout = ImageLayout::Undefined;
        PipelineStageFlags stageFlags = PipelineStage::None;
        AccessFlags accessMask = Access::None;
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayers = 0;
        uint32_t baseMipLevel = 0;
        uint32_t mipLevels = 0;
        uint32_t queueFamilyId = VK_QUEUE_FAMILY_IGNORED;

        bool operator!=(const ImageBarrierInfo &other) const
        {
            return !(image == other.image &&
                     layout == other.layout &&
                     stageFlags == other.stageFlags &&
                     accessMask == other.accessMask &&
                     baseArrayLayer == other.baseArrayLayer &&
                     arrayLayers == other.arrayLayers &&
                     baseMipLevel == other.baseMipLevel &&
                     mipLevels == other.mipLevels &&
                     queueFamilyId == other.queueFamilyId);
        }
    };

    class SamplerCreateInfo : public Hashable
    {
    public:
        SamplerCreateInfo();
        void UpdateHash() override;

        Filter magFilter;
        Filter minFilter;
        SamplerMipmapMode mipmapMode;
        SamplerAddressMode addressModeU;
        SamplerAddressMode addressModeV;
        SamplerAddressMode addressModeW;
        float mipLodBias;
        uint32_t anisotropyEnable;
        float maxAnisotropy;
        uint32_t compareEnable;
        CompareOp compareOp;
        float minLod;
        float maxLod;
        BorderColor borderColor;
        uint32_t unnormalizedCoordinates;
        std::string name;
    };

    class ImageCreateInfo
    {
    public:
        ImageCreateInfo();

        uint32_t width;
        uint32_t height;
        uint32_t depth;
        ImageTiling tiling;
        ImageUsageFlags usage;
        Format format;
        ImageLayout initialLayout;
        ImageCreateFlags imageFlags;
        ImageType imageType;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        SampleCount samples;
        SharingMode sharingMode;
        uint32_t queueFamilyIndexCount;
        const uint32_t *pQueueFamilyIndices;
        vec4 clearColor;
        std::string name;
    };

    class Sampler : public IHandle<Sampler, SamplerHandle>
    {
    public:
        Sampler(const SamplerCreateInfo &info);

        ~Sampler();

        SamplerCreateInfo info;
    };

    class Image : public IHandle<Image, ImageHandle>
    {
    public:
        Image() {}

        Image(const ImageCreateInfo &info);

        ~Image();

        uint32_t GetWidth() { return m_imageInfo.width; }
        uint32_t GetHeight() { return m_imageInfo.height; }
        float GetWidth_f() { return static_cast<float>(m_imageInfo.width); }
        float GetHeight_f() { return static_cast<float>(m_imageInfo.height); }
        Sampler *GetSampler() { return m_sampler; }
        const std::string &GetName() { return m_imageInfo.name; }
        Format GetFormat() { return m_imageInfo.format; }
        Format *GetFormatPtr() { return &m_imageInfo.format; }
        uint32_t GetMipLevels() { return m_imageInfo.mipLevels; }
        uint32_t GetArrayLayers() { return m_imageInfo.arrayLayers; }
        SampleCount GetSamples() { return m_imageInfo.samples; }
        const vec4 &GetClearColor() { return m_imageInfo.clearColor; }
        const PipelineColorBlendAttachmentState &GetBlendAttachment() { return m_blendAttachment; }

        const ImageBarrierInfo &GetCurrentInfo(uint32_t layer = 0, uint32_t mip = 0) { return m_infos[layer][mip]; }

        void SetCurrentInfo(const ImageBarrierInfo &info, uint32_t layer = 0, uint32_t mip = 0) { m_infos[layer][mip] = info; }

        void SetCurrentInfoAll(const ImageBarrierInfo &info)
        {
            for (uint32_t i = 0; i < m_infos.size(); i++)
            {
                for (uint32_t j = 0; j < m_infos[i].size(); j++)
                {
                    m_infos[i][j] = info;
                }
            }
        }

        void CreateRTV(bool useStencil = false);
        void CreateSRV(ImageViewType type, int mip = -1, bool useStencil = false);
        void CreateUAV(ImageViewType type, uint32_t mip, bool useStencil = false);

        bool HasRTV() { return !!m_rtv; }
        bool HasSRV(int mip = -1) { return mip == -1 ? !!m_srv : !!m_srvs[mip]; }
        bool HasUAV(uint32_t mip) { return !!m_uavs[mip]; }

        ImageViewHandle GetRTV() { return m_rtv; }
        ImageViewHandle GetSRV(int mip = -1) { return mip == -1 ? m_srv : m_srvs[mip]; }
        ImageViewHandle GetUAV(uint32_t mip) { return m_uavs[mip]; }

        void SetRTV(ImageViewHandle view) { m_rtv = view; }

        bool HasGeneratedMips() { return m_mipmapsGenerated; }

        static uint32_t CalculateMips(uint32_t width, uint32_t height);

        static Image *LoadRGBA8(CommandBuffer *cmd, const std::string &path);

        static Image *LoadRGBA8Cubemap(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSize);

    private:
        friend class CommandBuffer;
        friend class Swapchain;
        friend class Renderer;
        friend class RendererSystem;

        ImageViewHandle CreateImageView(ImageViewType type, int mip = -1, bool useStencil = false);

        void Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info);

        static void Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos);

        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size = 0,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
                                   uint32_t mipLevel = 0);

        void CopyImage(CommandBuffer *cmd, Image *src);

        void GenerateMipMaps(CommandBuffer *cmd);

        void BlitImage(CommandBuffer *cmd, Image *src, ImageBlit *region, Filter filter);

        float width_f{};
        float height_f{};
        Sampler *m_sampler;
        AllocationHandle m_allocation;
        ImageCreateInfo m_imageInfo;
        PipelineColorBlendAttachmentState m_blendAttachment;
        ImageViewHandle m_rtv; // Render target view
        ImageViewHandle m_srv; // Shader resource view
        std::vector<ImageViewHandle> m_srvs; // Shader resource views for multiple mip levels
        std::vector<ImageViewHandle> m_uavs; // Unordered access views
        std::vector<std::vector<ImageBarrierInfo>> m_infos{}; // Tracking image barrier info for each layer and mip level
        bool m_mipmapsGenerated = false;
    };
}