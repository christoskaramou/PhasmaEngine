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
        std::vector<ImageViewHandle> views{};
        std::vector<SamplerHandle> samplers{};
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

        inline static std::unordered_map<size_t, DescriptorLayout *> s_descriptorLayouts{};

        inline static Hash CalculateHash(const std::vector<DescriptorBindingInfo> &bindingInfos, ShaderStage stage)
        {
            Hash hash;
            hash.Combine(static_cast<uint32_t>(stage));
            for (size_t i = 0; i < bindingInfos.size(); i++)
            {
                const DescriptorBindingInfo &info = bindingInfos[i];
                hash.Combine(info.binding);
                hash.Combine(info.count);
                hash.Combine(static_cast<uint32_t>(info.imageLayout));
                hash.Combine(static_cast<uint32_t>(info.type));
                hash.Combine(info.bindless);
            }

            return hash;
        }

        inline static DescriptorLayout *GetOrCreate(const std::vector<DescriptorBindingInfo> &bindingInfos, ShaderStage stage)
        {
            static size_t count = 0; 
            Hash hash = DescriptorLayout::CalculateHash(bindingInfos, stage);
            auto it = DescriptorLayout::s_descriptorLayouts.find(hash);
            if (it == DescriptorLayout::s_descriptorLayouts.end())
            {
                DescriptorLayout *layout = DescriptorLayout::Create(bindingInfos, stage, "Descriptor_layout_" + std::to_string(count));
                DescriptorLayout::s_descriptorLayouts[hash] = layout;
                return layout;
            }
            else
            {
                return it->second;
            }
        }

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

        void SetImages(uint32_t binding,
                       const std::vector<ImageViewHandle> &views,
                       const std::vector<SamplerHandle> &samplers);
        void SetImage(uint32_t binding, ImageViewHandle view, SamplerHandle sampler);

        void SetBuffers(uint32_t binding, const std::vector<Buffer *> &buffers);
        void SetBuffer(uint32_t binding, Buffer *buffer);

        void SetSampler(uint32_t binding, SamplerHandle sampler);

        void Update();

        DescriptorPool *GetPool() const { return m_pool; }

        DescriptorLayout *GetLayout() const { return m_layout; }

        void GetFrameDynamicOffsets(uint32_t *count, uint32_t **offsets);

        static std::vector<uint32_t> GetAllFrameDynamicOffsets(uint32_t count, Descriptor **descriptors);

    private:
        DescriptorPool *m_pool;
        DescriptorLayout *m_layout;
        std::vector<uint32_t> m_frameDynamicOffsets;
        std::unordered_map<uint32_t, DescriptorBindingInfo> m_bindingInfoMap;
        std::unordered_map<uint32_t, DescriptorUpdateInfo> m_updateInfoMap;
    };

}
