#pragma once

namespace pe
{
    struct DescriptorPoolSize
    {
        uint32_t descriptorCount;
        DescriptorType type;
    };

    class DescriptorPool : public IHandle<DescriptorPool, DescriptorPoolHandle>
    {
    public:
        DescriptorPool(uint32_t count, DescriptorPoolSize *sizes, const std::string &name);

        ~DescriptorPool();
    };

    class Image;
    class Buffer;

    struct DescriptorBindingInfo
    {
        uint32_t binding = (uint32_t)-1;
        uint32_t count = 1;
        ImageLayout imageLayout = ImageLayout::Undefined;
        DescriptorType type = DescriptorType::CombinedImageSampler;
        bool bindless = false;
    };

    struct DescriptorUpdateInfo
    {
        uint32_t binding = (uint32_t)-1;
        std::vector<Buffer *> buffers{};
        std::vector<Image *> images{};
        SamplerHandle sampler{};
    };

    class DescriptorLayout : public IHandle<DescriptorLayout, DescriptorSetLayoutHandle>
    {
    public:
        DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                         ShaderStage stage,
                         const std::string &name);

        ~DescriptorLayout();

        uint32_t GetVariableCount() { return m_variableCount; }

    private:
        uint32_t m_variableCount;
    };

    class Descriptor : public IHandle<Descriptor, DescriptorSetHandle>
    {
    public:
        Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                   ShaderStage stage,
                   const std::string &name);

        ~Descriptor();

        void SetImages(uint32_t binding, const std::vector<Image *> &images);
        void SetImage(uint32_t binding, Image *image);

        void SetBuffers(uint32_t binding, const std::vector<Buffer *> &buffers);
        void SetBuffer(uint32_t binding, Buffer *buffer);

        void SetSampler(uint32_t binding, SamplerHandle sampler);

        void UpdateDescriptor();

        DescriptorPool *GetPool() const { return m_pool; }

        DescriptorLayout *GetLayout() const { return m_layout; }

        void GetFrameDynamicOffsets(uint32_t *count, uint32_t **offsets);

        static std::vector<uint32_t> GetAllFrameDynamicOffsets(uint32_t count, Descriptor **descriptors);

    private:
        DescriptorPool *m_pool;
        DescriptorLayout *m_layout;
        std::vector<uint32_t> m_frameDynamicOffsets;
        std::map<uint32_t, DescriptorBindingInfo> m_bindingInfoMap;
        std::map<uint32_t, DescriptorUpdateInfo> m_updateInfoMap;
    };

}
