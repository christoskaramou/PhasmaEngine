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
        uint32_t binding = 0;
        Buffer *pBuffer = nullptr;
        Image *pImage = nullptr;
        ImageLayout imageLayout = ImageLayout::Undefined;
        DescriptorType type = DescriptorType::CombinedImageSampler;
        SamplerHandle sampler = {};
    };

    struct DescriptorInfo
    {
        uint32_t count = 0;
        DescriptorBindingInfo *bindingInfos = nullptr;
        ShaderStage stage = ShaderStage::VertexBit;
    };

    class DescriptorLayout : public IHandle<DescriptorLayout, DescriptorSetLayoutHandle>
    {
    public:
        DescriptorLayout(DescriptorInfo *info, const std::string &name);

        ~DescriptorLayout();

        uint32_t GetVariableCount() { return m_variableCount; }

    private:
        uint32_t m_variableCount;
    };

    class Descriptor : public IHandle<Descriptor, DescriptorSetHandle>
    {
    public:
        Descriptor(DescriptorInfo *info, const std::string &name);

        ~Descriptor();

        void UpdateDescriptor(DescriptorInfo *info);

        DescriptorPool *GetPool() const { return m_pool; }

        DescriptorLayout *GetLayout() const { return m_layout; }

        std::vector<DescriptorBindingInfo> &GetBindingInfos() { return m_bindingInfos; }

        void GetFrameDynamicOffsets(uint32_t *count, uint32_t **offsets);

        static std::vector<uint32_t> GetAllFrameDynamicOffsets(uint32_t count, Descriptor **descriptors);

    private:
        DescriptorPool *m_pool;
        DescriptorLayout *m_layout;
        std::vector<DescriptorBindingInfo> m_bindingInfos;
        std::vector<uint32_t> m_frameDynamicOffsets;
    };

}
