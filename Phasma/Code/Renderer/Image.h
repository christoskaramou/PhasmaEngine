#pragma once

#include "Renderer/Pipeline.h"

namespace pe
{
    class Context;
    class CommandBuffer;
    class Buffer;
    class Image;
    struct ImageBlit;
    struct BarrierInfo;

    class SamplerCreateInfo
    {
    public:
        SamplerCreateInfo();

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
        MemoryPropertyFlags properties;
        Format format;
        ImageCreateFlags imageFlags;
        ImageType imageType;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        SampleCount samples;
        SharingMode sharingMode;
        uint32_t queueFamilyIndexCount;
        const uint32_t *pQueueFamilyIndices;
        ImageLayout initialLayout;
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

        ImageLayout GetCurrentLayout(uint32_t layer = 0, uint32_t mip = 0) { return m_layouts[layer][mip]; }

        void SetCurrentLayout(ImageLayout layout, uint32_t layer = 0, uint32_t mip = 0) { m_layouts[layer][mip] = layout; }

        void CreateRTV();
        void CreateSRV(ImageViewType type, int mip = -1);
        void CreateUAV(ImageViewType type, uint32_t mip);

        bool HasRTV() { return !!m_rtv; }
        bool HasSRV(int mip = -1) { return mip == -1 ? !!m_srv : !!m_srvs[mip]; }
        bool HasUAV(uint32_t mip) { return !!m_uavs[mip]; }

        ImageViewHandle GetRTV() { return m_rtv; }
        ImageViewHandle GetSRV(int mip = -1) { return mip == -1 ? m_srv : m_srvs[mip]; }
        ImageViewHandle GetUAV(uint32_t mip) { return m_uavs[mip]; }

        void SetRTV(ImageViewHandle view) { m_rtv = view; }

        bool HasGeneratedMips() { return m_mipmapsGenerated; }

        static uint32_t CalculateMips(uint32_t width, uint32_t height);

    private:
        friend class CommandBuffer;

        ImageViewHandle CreateImageView(ImageViewType type, int mip = -1);

        // For arrayLayers and/or mipLevels bigger than size 1, all of their layouts must be the same
        // TODO: Manage transitions for different layouts in arrayLayers and/or mipLevels
        void Barrier(CommandBuffer *cmd,
                     ImageLayout newLayout,
                     uint32_t baseArrayLayer = 0,
                     uint32_t arrayLayers = 0,
                     uint32_t baseMipLevel = 0,
                     uint32_t mipLevels = 0);

        static void GroupBarrier(CommandBuffer *cmd, const std::vector<BarrierInfo> &barrierInfos);

        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size = 0,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
                                   uint32_t mipLevel = 0);

        void CopyImage(CommandBuffer *cmd, Image *src);

        void GenerateMipMaps(CommandBuffer *cmd);

        void BlitImage(CommandBuffer *cmd, Image *src, ImageBlit *region, Filter filter);

        void TransitionImageLayout(CommandBuffer *cmd,
                                   ImageLayout oldLayout,
                                   ImageLayout newLayout,
                                   PipelineStageFlags oldStageMask,
                                   PipelineStageFlags newStageMask,
                                   AccessFlags srcMask,
                                   AccessFlags dstMask,
                                   uint32_t baseArrayLayer,
                                   uint32_t arrayLayers,
                                   uint32_t baseMipLevel,
                                   uint32_t mipLevels);

    public:
        Sampler *sampler;
        AllocationHandle allocation;
        ImageCreateInfo imageInfo;

        float width_f{};
        float height_f{};
        PipelineColorBlendAttachmentState blendAttachment;

        inline static std::unordered_map<size_t, Image *> uniqueImages{};

    private:
        friend class Swapchain;
        ImageViewHandle m_rtv;
        ImageViewHandle m_srv;
        std::vector<ImageViewHandle> m_srvs;
        std::vector<ImageViewHandle> m_uavs;
        std::vector<std::vector<ImageLayout>> m_layouts{};
        bool m_mipmapsGenerated = false;
    };
}