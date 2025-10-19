#pragma once

namespace pe
{
    class DescriptorPool : public PeHandle<DescriptorPool, vk::DescriptorPool>
    {
    public:
        DescriptorPool(const std::vector<vk::DescriptorPoolSize> &sizes, const std::string &name);
        ~DescriptorPool();
    };

    class Image;
    class Buffer;

    struct DescriptorBindingInfo
    {
        uint32_t binding = (uint32_t)-1;
        uint32_t count = 1;
        vk::ImageLayout imageLayout = vk::ImageLayout::eUndefined;
        vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler;
        bool bindless = false;
        std::string name{};
    };

    struct DescriptorUpdateInfo
    {
        uint32_t binding = (uint32_t)-1;
        std::vector<Buffer *> buffers{};
        std::vector<uint64_t> offsets{};
        std::vector<uint64_t> ranges{}; // range of the buffers in bytes to use
        std::vector<vk::ImageView> views{};
        std::vector<vk::Sampler> samplers{}; // if type == DescriptorType::CombinedImageSampler, these are the samplers for each view
    };

    class DescriptorLayout : public PeHandle<DescriptorLayout, vk::DescriptorSetLayout>
    {
    public:
        DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                         vk::ShaderStageFlags stage,
                         const std::string &name,
                         bool pushDescriptor = false);
        ~DescriptorLayout();

        uint32_t GetVariableCount() { return m_variableCount; }
        bool IsPushDescriptor() { return m_pushDescriptor; }

        inline static std::unordered_map<size_t, DescriptorLayout *> s_descriptorLayouts{};

        inline static size_t CalculateHash(const std::vector<DescriptorBindingInfo> &bindingInfos, vk::ShaderStageFlags stage, bool pushDescriptor = false)
        {
            Hash hash;
            hash.Combine(static_cast<uint32_t>(stage));
            hash.Combine(pushDescriptor);
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

        inline static DescriptorLayout *GetOrCreate(const std::vector<DescriptorBindingInfo> &bindingInfos, vk::ShaderStageFlags stage, bool pushDescriptor = false)
        {
            static size_t count = 0;
            size_t hash = DescriptorLayout::CalculateHash(bindingInfos, stage, pushDescriptor);
            auto it = DescriptorLayout::s_descriptorLayouts.find(hash);
            if (it == DescriptorLayout::s_descriptorLayouts.end())
            {
                DescriptorLayout *layout = DescriptorLayout::Create(bindingInfos, stage, "Descriptor_layout_" + std::to_string(count), pushDescriptor);
                DescriptorLayout::s_descriptorLayouts[hash] = layout;
                return layout;
            }
            else
            {
                return it->second;
            }
        }

        inline static void ClearCache()
        {
            for (auto &[hash, layout] : s_descriptorLayouts)
                DescriptorLayout::Destroy(layout);
            s_descriptorLayouts.clear();
        }

    private:
        uint32_t m_variableCount;
        bool m_pushDescriptor;
        bool m_allowUpdateAfterBind;
    };

    class Descriptor : public PeHandle<Descriptor, vk::DescriptorSet>
    {
    public:
        Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                   vk::ShaderStageFlags stage,
                   bool pushDescriptor,
                   const std::string &name);
        ~Descriptor();

        void SetImageViews(uint32_t binding,
                           const std::vector<vk::ImageView> &views,
                           const std::vector<vk::Sampler> &samplers);
        void SetImageView(uint32_t binding, vk::ImageView view, vk::Sampler sampler);
        void SetBuffers(uint32_t binding,
                        const std::vector<Buffer *> &buffers,
                        const std::vector<uint64_t> &offsets = {},
                        const std::vector<uint64_t> &ranges = {});
        void SetBuffer(uint32_t binding, Buffer *buffer, uint64_t offset = 0, uint64_t range = 0);
        void SetSamplers(uint32_t binding, const std::vector<vk::Sampler> &samplers);
        void SetSampler(uint32_t binding, vk::Sampler sampler);
        void Update();
        DescriptorPool *GetPool() const { return m_pool; }
        DescriptorLayout *GetLayout() const { return m_layout; }
        vk::ShaderStageFlags GetStage() const { return m_stage; }

    private:
        DescriptorPool *m_pool;
        DescriptorLayout *m_layout;
        std::vector<DescriptorBindingInfo> m_bindingInfos{};
        std::vector<DescriptorUpdateInfo> m_updateInfos{};
        bool m_pushDescriptor;
        std::vector<uint32_t> m_dynamicOffsets{};
        vk::ShaderStageFlags m_stage;
    };

}
