#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Context;
    class CommandBuffer;
    class Buffer;
    class Image;

    struct ImageDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        ::PeFormat format = PE_FORMAT_UNDEFINED;
        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE;
        PeSampleCount samples = PE_SAMPLE_COUNT_1;
        PeImageLayout initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
        PeImageType imageType = PE_IMAGE_TYPE_2D;
        bool cubeCompatible = false;
        bool array2DCompatible = false; // 3D image accessible as 2D-array view
        bool mutableFormat = false;     // view created with format that differs from desc.format
        bool tilingLinear = false;
        std::string name;
    };

    // Vulkan-flavored barrier descriptor — retype to PeBarrierSync /
    // PeBarrierAccess / PeImageLayout is deferred to Phase 0 step 7
    // (Command.h barrier retype), mirroring BufferBarrierInfo.
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

    // Sampler and ImageView remain PeHandle classes in Phase 0 step 3. Sampler
    // pImpl is Phase 0 step 4. ImageView migration tracks Sampler.
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

    class ImageView : public PeHandle<ImageView, vk::ImageView>
    {
    public:
        ImageView() {}
        ImageView(Image *parent, const vk::ImageViewCreateInfo &info, const std::string &name = "");
        ~ImageView();

        Image *GetParent() { return m_parent; }
        const vk::ImageViewCreateInfo &GetInfo() { return m_info; }

        static vk::ImageViewCreateInfo CreateInfoInit();

    private:
        Image *m_parent{};
        vk::ImageViewCreateInfo m_info;
        std::string m_name;
    };

    class Image : public Resource
    {
    public:
        struct Impl; // forward — defined in Image_Internal.h, body engine-private

        Image();
        ~Image() override;

        static Image *Create(const ImageDesc &desc);
        static void Destroy(Image *&image);
        static std::vector<Image *> GetHandles();

        virtual void Load() override {}
        virtual void Unload() override;

        uint32_t GetWidth() const { return m_width; }
        uint32_t GetHeight() const { return m_height; }
        float GetWidth_f() const { return static_cast<float>(m_width); }
        float GetHeight_f() const { return static_cast<float>(m_height); }
        Sampler *GetSampler() { return m_sampler; }
        void SetSampler(Sampler *sampler) { m_sampler = sampler; }
        const std::string &GetName() const { return m_name; }
        ::PeFormat GetFormat() const { return m_format; }
        uint32_t GetMipLevels() const { return m_mipLevels; }
        uint32_t GetArrayLayers() const { return m_arrayLayers; }
        PeSampleCount GetSamples() const { return m_samples; }
        PeImageUsageFlags GetUsage() const { return m_usage; }
        const ImageTrackInfo &GetCurrentInfo(uint32_t layer = 0, uint32_t mip = 0) { return m_trackInfos[layer][mip]; }
        void SetCurrentInfo(const ImageTrackInfo &info, uint32_t layer = 0, uint32_t mip = 0) { m_trackInfos[layer][mip] = info; }
        void SetCurrentInfoAll(const ImageTrackInfo &info);

        void CreateRTV();
        void CreateSRV(PeImageViewType type, int mip = -1);
        void CreateUAV(PeImageViewType type, uint32_t mip);
        bool HasRTV() { return !!m_rtv; }
        bool HasSRV(int mip = -1) { return mip == -1 ? !!m_srv : !!m_srvs[mip]; }
        bool HasUAV(uint32_t mip) { return !!m_uavs[mip]; }
        ImageView *GetRTV() { return m_rtv; }
        ImageView *GetSRV(int mip = -1) { return mip == -1 ? m_srv : m_srvs[mip]; }
        ImageView *GetUAV(uint32_t mip) { return m_uavs[mip]; }
        void SetRTV(ImageView *view) { m_rtv = view; }
        bool HasGeneratedMips() { return m_mipmapsGenerated; }
        vec4 GetClearColor() { return m_clearColor; }
        void SetClearColor(const vec4 &color) { m_clearColor = color; }

        // Vulkan-flavored aspect override — deferred to Phase 0 step 7 alongside
        // ImageBarrierInfo retype. Sole consumer is PhasmaWebGPU stencil-aspect
        // view forwarding in Texture.cpp.
        void SetAspectMaskOverride(vk::ImageAspectFlags m) { m_aspectMaskOverride = m; }
        vk::ImageAspectFlags GetAspectMaskOverride() const { return m_aspectMaskOverride; }

        static uint32_t CalculateMips(uint32_t width, uint32_t height);

        // Loaders accept ::PeFormat. Decoders still produce vk-flavored data
        // internally; the format param flows into ImageDesc.format.
        static Image *LoadRGBA(CommandBuffer *cmd, const std::string &path, ::PeFormat format, bool isFloat = false);
        static Image *LoadRGBA8(CommandBuffer *cmd, const std::string &path);
        static Image *LoadRGBA32F(CommandBuffer *cmd, const std::string &path);
        static Image *LoadRGBAFromMemory(CommandBuffer *cmd, void *data, int size, ::PeFormat format, bool isFloat = false);
        static Image *LoadRGBA8FromMemory(CommandBuffer *cmd, void *data, int size);
        static Image *LoadRawFromMemory(CommandBuffer *cmd, void *data, uint32_t width, uint32_t height, ::PeFormat format, const std::string &name = "");

        struct LoadRawParams
        {
            uint32_t width;
            uint32_t height;
            ::PeFormat format;
            bool generateMips = false;
            bool clampToEdge = false;
            float mipLodBias = 0.0f;
        };
        static Image *LoadRaw(CommandBuffer *cmd, const std::string &path, const LoadRawParams &params);

    private:
        friend class CommandBuffer;
        friend class Swapchain;
        friend struct VulkanImageImpl;

        Image(const ImageDesc &desc);
        static void Barrier(CommandBuffer *cmd, const ImageBarrierInfo &info);
        static void Barriers(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos);

        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size = 0,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
                                   uint32_t mipLevel = 0);
        void CopyImage(CommandBuffer *cmd, Image *src);
        void CopyToBuffer(CommandBuffer *cmd, Buffer *dst);
        void GenerateMipMaps(CommandBuffer *cmd);
        void Blit(CommandBuffer *cmd, Image *src, const vk::ImageBlit &region, vk::Filter filter);

        Impl *m_impl{};
        // Mirrored cheap data so getters skip the Impl hop.
        uint32_t m_width{};
        uint32_t m_height{};
        uint32_t m_depth{1};
        uint32_t m_mipLevels{1};
        uint32_t m_arrayLayers{1};
        ::PeFormat m_format{PE_FORMAT_UNDEFINED};
        PeImageUsageFlags m_usage{PE_IMAGE_USAGE_NONE};
        PeSampleCount m_samples{PE_SAMPLE_COUNT_1};
        PeImageType m_imageType{PE_IMAGE_TYPE_2D};

        Sampler *m_sampler{};
        ImageView *m_rtv{};
        ImageView *m_srv{};
        std::vector<ImageView *> m_srvs{};
        std::vector<ImageView *> m_uavs{};
        std::vector<std::vector<ImageTrackInfo>> m_trackInfos{};
        bool m_mipmapsGenerated = false;
        vec4 m_clearColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vk::ImageAspectFlags m_aspectMaskOverride{};
        std::string m_name;
    };
} // namespace pe
