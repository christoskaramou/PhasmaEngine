#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class AccelerationStructure;
    class Buffer;
    class ImageView;
    class Sampler;

    struct DescriptorPoolSize
    {
        PeBindingType type = PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uint32_t descriptorCount = 0;
    };

    struct DescriptorPoolDesc
    {
        std::vector<DescriptorPoolSize> sizes{};
        uint32_t maxSets = 1;
        bool updateAfterBind = true;
    };

    class DescriptorPool : public NoCopy
    {
    public:
        struct Impl;

        static DescriptorPool *Create(const DescriptorPoolDesc &desc, const std::string &name);
        static DescriptorPool *Create(const std::vector<DescriptorPoolSize> &sizes,
                                      const std::string &name,
                                      uint32_t maxSets = 1,
                                      bool updateAfterBind = true);
        static void Destroy(DescriptorPool *&pool);
        static std::vector<DescriptorPool *> GetHandles();

        const DescriptorPoolDesc &GetDesc() const { return m_desc; }
        const std::string &GetName() const { return m_name; }

    private:
        friend struct VulkanDescriptorPoolImpl;
        friend struct Dx12DescriptorPoolImpl;

        DescriptorPool(const DescriptorPoolDesc &desc, const std::string &name);
        ~DescriptorPool();

        Impl *m_impl{};
        DescriptorPoolDesc m_desc{};
        std::string m_name{};
    };

    struct DescriptorBindingInfo
    {
        uint32_t binding = (uint32_t)-1;
        uint32_t count = 1;
        uint32_t dxRegister = (uint32_t)-1;
        uint32_t dxSpace = 0;
        PeImageLayout imageLayout = PE_IMAGE_LAYOUT_UNDEFINED;
        PeBindingType type = PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bool bindless = false;
        std::string name{};
    };

    struct DescriptorUpdateInfo
    {
        uint32_t binding = (uint32_t)-1;
        std::vector<Buffer *> buffers{};
        std::vector<uint64_t> offsets{};
        std::vector<uint64_t> ranges{}; // range of the buffers in bytes to use
        std::vector<ImageView *> views{};
        std::vector<Sampler *> samplers{}; // if type == CombinedImageSampler, these are the samplers for each view
        std::vector<AccelerationStructure *> accelerationStructures{};
    };

    class DescriptorLayout : public NoCopy
    {
    public:
        struct Impl;

        static DescriptorLayout *Create(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                        PeShaderStageFlags stage,
                                        const std::string &name,
                                        bool pushDescriptor = false);
        static void Destroy(DescriptorLayout *&layout);
        static std::vector<DescriptorLayout *> GetHandles();

        uint32_t GetVariableCount() const { return m_variableCount; }
        bool IsPushDescriptor() const { return m_pushDescriptor; }
        PeShaderStageFlags GetStage() const { return m_stage; }
        const std::vector<DescriptorBindingInfo> &GetBindingInfos() const { return m_bindingInfos; }
        const std::string &GetName() const { return m_name; }

        inline static size_t CalculateHash(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                           PeShaderStageFlags stage,
                                           bool pushDescriptor = false)
        {
            Hash hash;
            hash.Combine(stage);
            hash.Combine(pushDescriptor);
            for (size_t i = 0; i < bindingInfos.size(); i++)
            {
                const DescriptorBindingInfo &info = bindingInfos[i];
                hash.Combine(info.binding);
                hash.Combine(info.count);
                hash.Combine(info.dxRegister);
                hash.Combine(info.dxSpace);
                hash.Combine(static_cast<uint32_t>(info.imageLayout));
                hash.Combine(static_cast<uint32_t>(info.type));
                hash.Combine(info.bindless);
            }

            return hash;
        }

        inline static DescriptorLayout *GetOrCreate(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                                    PeShaderStageFlags stage,
                                                    bool pushDescriptor = false)
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
        friend struct VulkanDescriptorLayoutImpl;
        friend struct Dx12DescriptorLayoutImpl;

        DescriptorLayout(const std::vector<DescriptorBindingInfo> &bindingInfos,
                         PeShaderStageFlags stage,
                         const std::string &name,
                         bool pushDescriptor);
        ~DescriptorLayout();

        inline static std::unordered_map<size_t, DescriptorLayout *> s_descriptorLayouts{};

        Impl *m_impl{};
        std::vector<DescriptorBindingInfo> m_bindingInfos{};
        PeShaderStageFlags m_stage{};
        std::string m_name{};
        uint32_t m_variableCount{};
        bool m_pushDescriptor{};
        bool m_allowUpdateAfterBind{};
    };

    class DescriptorLayoutBuilder
    {
    public:
        DescriptorLayoutBuilder &AddBinding(uint32_t binding,
                                            PeBindingType type,
                                            uint32_t count = 1,
                                            PeImageLayout imageLayout = PE_IMAGE_LAYOUT_UNDEFINED,
                                            bool bindless = false,
                                            const std::string &name = "");
        DescriptorLayoutBuilder &SetStage(PeShaderStageFlags stage);
        DescriptorLayoutBuilder &SetPushDescriptor(bool pushDescriptor);
        DescriptorLayoutBuilder &SetName(const std::string &name);

        DescriptorLayout *Build() const;
        const std::vector<DescriptorBindingInfo> &GetBindings() const { return m_bindingInfos; }

    private:
        std::vector<DescriptorBindingInfo> m_bindingInfos{};
        PeShaderStageFlags m_stage = PE_SHADER_STAGE_ALL;
        bool m_pushDescriptor = false;
        std::string m_name = "Descriptor_layout";
    };

    class Descriptor : public NoCopy
    {
    public:
        struct Impl;

        static Descriptor *Create(const std::vector<DescriptorBindingInfo> &bindingInfos,
                                  PeShaderStageFlags stage,
                                  bool pushDescriptor,
                                  const std::string &name);
        static void Destroy(Descriptor *&descriptor);
        static std::vector<Descriptor *> GetHandles();

        void SetImageViews(uint32_t binding,
                           const std::vector<ImageView *> &views,
                           const std::vector<Sampler *> &samplers = {});
        void SetImageView(uint32_t binding, ImageView *view, Sampler *sampler = nullptr);
        void SetBuffers(uint32_t binding,
                        const std::vector<Buffer *> &buffers,
                        const std::vector<uint64_t> &offsets = {},
                        const std::vector<uint64_t> &ranges = {});
        void SetBuffer(uint32_t binding, Buffer *buffer, uint64_t offset = 0, uint64_t range = 0);
        void SetSamplers(uint32_t binding, const std::vector<Sampler *> &samplers);
        void SetSampler(uint32_t binding, Sampler *sampler);
        void SetAccelerationStructure(uint32_t binding, AccelerationStructure *tlas);
        void Update();
        DescriptorPool *GetPool() const { return m_pool; }
        DescriptorLayout *GetLayout() const { return m_layout; }
        PeShaderStageFlags GetStage() const { return m_stage; }
        const std::vector<DescriptorUpdateInfo> &GetBoundResources() const { return m_boundResources; }
        const std::vector<DescriptorBindingInfo> &GetBindingInfos() const { return m_bindingInfos; }

    private:
        friend struct VulkanDescriptorImpl;
        friend struct Dx12DescriptorImpl;

        Descriptor(const std::vector<DescriptorBindingInfo> &bindingInfos,
                   PeShaderStageFlags stage,
                   bool pushDescriptor,
                   const std::string &name);
        ~Descriptor();

        Impl *m_impl{};
        DescriptorPool *m_pool{};
        DescriptorLayout *m_layout{};
        std::vector<DescriptorBindingInfo> m_bindingInfos{};
        std::vector<DescriptorUpdateInfo> m_updateInfos{}; // for partial updates
        std::vector<DescriptorUpdateInfo> m_boundResources{};
        bool m_pushDescriptor{};
        std::vector<uint32_t> m_dynamicOffsets{};
        PeShaderStageFlags m_stage{};
        std::string m_name{};
    };

} // namespace pe
