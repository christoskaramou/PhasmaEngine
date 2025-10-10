#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Context;
    class CommandBuffer;
    class Buffer;
    class Image;

    struct ImageBarrierInfo
    {
        Image *image = nullptr;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags2 stageFlags = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2 accessMask = vk::AccessFlagBits2::eNone;
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
    using ImageTrackInfo = ImageBarrierInfo;

    class Sampler : public PeHandle<Sampler, vk::Sampler>
    {
    public:
        Sampler(const vk::SamplerCreateInfo &info, const std::string &name = "");
        ~Sampler();

        const vk::SamplerCreateInfo &GetInfo() { return m_info; }
        static vk::SamplerCreateInfo CreateInfoInit();

    private:
        vk::SamplerCreateInfo m_info;
        std::string m_name;
    };

    class Image : public PeHandle<Image, vk::Image>
    {
    public:
        Image() {}
        Image(const vk::ImageCreateInfo &info, const std::string &name = "");
        ~Image();

        uint32_t GetWidth() { return m_createInfo.extent.width; }
        uint32_t GetHeight() { return m_createInfo.extent.height; }
        float GetWidth_f() { return static_cast<float>(m_createInfo.extent.width); }
        float GetHeight_f() { return static_cast<float>(m_createInfo.extent.height); }
        Sampler *GetSampler() { return m_sampler; }
        void SetSampler(Sampler *sampler) { m_sampler = sampler; }
        const std::string &GetName() { return m_name; }
        vk::Format GetFormat() { return m_createInfo.format; }
        uint32_t GetMipLevels() { return m_createInfo.mipLevels; }
        uint32_t GetArrayLayers() { return m_createInfo.arrayLayers; }
        vk::SampleCountFlagBits GetSamples() { return m_createInfo.samples; }
        const ImageTrackInfo &GetCurrentInfo(uint32_t layer = 0, uint32_t mip = 0) { return m_trackInfos[layer][mip]; }
        void SetCurrentInfo(const ImageTrackInfo &info, uint32_t layer = 0, uint32_t mip = 0) { m_trackInfos[layer][mip] = info; }
        void SetCurrentInfoAll(const ImageTrackInfo &info);
        void CreateRTV(bool useStencil = false);
        void CreateSRV(vk::ImageViewType type, int mip = -1, bool useStencil = false);
        void CreateUAV(vk::ImageViewType type, uint32_t mip, bool useStencil = false);
        bool HasRTV() { return !!m_rtv; }
        bool HasSRV(int mip = -1) { return mip == -1 ? !!m_srv : !!m_srvs[mip]; }
        bool HasUAV(uint32_t mip) { return !!m_uavs[mip]; }
        vk::ImageView GetRTV() { return m_rtv; }
        vk::ImageView GetSRV(int mip = -1) { return mip == -1 ? m_srv : m_srvs[mip]; }
        vk::ImageView GetUAV(uint32_t mip) { return m_uavs[mip]; }
        void SetRTV(vk::ImageView view) { m_rtv = view; }
        bool HasGeneratedMips() { return m_mipmapsGenerated; }
        vec4 GetClearColor() { return m_clearColor; }
        void SetClearColor(const vec4 &color) { m_clearColor = color; }

        static uint32_t CalculateMips(uint32_t width, uint32_t height);
        static Image *LoadRGBA8(CommandBuffer *cmd, const std::string &path);
        static Image *LoadRGBA8Cubemap(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSize);
        static vk::ImageCreateInfo CreateInfoInit();

    private:
        friend class CommandBuffer;
        friend class Swapchain;

        static void Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info);
        static void Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos);

        vk::ImageView CreateImageView(vk::ImageViewType type, int mip = -1, bool useStencil = false);
        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size = 0,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
                                   uint32_t mipLevel = 0);
        void CopyImage(CommandBuffer *cmd, Image *src);
        void GenerateMipMaps(CommandBuffer *cmd);
        void BlitImage(CommandBuffer *cmd, Image *src, const vk::ImageBlit &region, vk::Filter filter);

        Sampler *m_sampler;
        VmaAllocation m_allocation;
        vk::ImageCreateInfo m_createInfo;
        vk::ImageView m_rtv;                                     // Render target view
        vk::ImageView m_srv;                                     // Shader resource view
        std::vector<vk::ImageView> m_srvs;                       // Shader resource views for multiple mip levels
        std::vector<vk::ImageView> m_uavs;                       // Unordered access views
        std::vector<std::vector<ImageTrackInfo>> m_trackInfos{}; // Tracking image barrier info for each layer and mip level
        bool m_mipmapsGenerated = false;
        vec4 m_clearColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        std::string m_name;
    };
}
