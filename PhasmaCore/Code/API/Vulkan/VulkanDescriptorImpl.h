#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Descriptor_Internal.h"

namespace pe
{
    struct VulkanDescriptorPoolImpl final : public DescriptorPool::Impl
    {
        VulkanDescriptorPoolImpl(DescriptorPool *owner, const DescriptorPoolDesc &desc);
        ~VulkanDescriptorPoolImpl() override;

        static VulkanDescriptorPoolImpl *From(DescriptorPool *pool) { return static_cast<VulkanDescriptorPoolImpl *>(pool->m_impl); }
        static const VulkanDescriptorPoolImpl *From(const DescriptorPool *pool) { return static_cast<const VulkanDescriptorPoolImpl *>(pool->m_impl); }
        static VulkanDescriptorPoolImpl *TryFrom(DescriptorPool *pool) { return pool && pool->m_impl ? From(pool) : nullptr; }
        static const VulkanDescriptorPoolImpl *TryFrom(const DescriptorPool *pool) { return pool && pool->m_impl ? From(pool) : nullptr; }

        DescriptorPool *m_owner{};
        vk::DescriptorPool m_pool{};
    };

    struct VulkanDescriptorLayoutImpl final : public DescriptorLayout::Impl
    {
        VulkanDescriptorLayoutImpl(DescriptorLayout *owner);
        ~VulkanDescriptorLayoutImpl() override;

        static VulkanDescriptorLayoutImpl *From(DescriptorLayout *layout) { return static_cast<VulkanDescriptorLayoutImpl *>(layout->m_impl); }
        static const VulkanDescriptorLayoutImpl *From(const DescriptorLayout *layout) { return static_cast<const VulkanDescriptorLayoutImpl *>(layout->m_impl); }
        static VulkanDescriptorLayoutImpl *TryFrom(DescriptorLayout *layout) { return layout && layout->m_impl ? From(layout) : nullptr; }
        static const VulkanDescriptorLayoutImpl *TryFrom(const DescriptorLayout *layout) { return layout && layout->m_impl ? From(layout) : nullptr; }

        DescriptorLayout *m_owner{};
        vk::DescriptorSetLayout m_layout{};
    };

    struct VulkanDescriptorImpl final : public Descriptor::Impl
    {
        explicit VulkanDescriptorImpl(Descriptor *owner);
        ~VulkanDescriptorImpl() override = default;

        void Update(const std::vector<DescriptorBindingInfo> &bindingInfos,
                    const std::vector<DescriptorUpdateInfo> &updateInfos) override;

        static VulkanDescriptorImpl *From(Descriptor *descriptor) { return static_cast<VulkanDescriptorImpl *>(descriptor->m_impl); }
        static const VulkanDescriptorImpl *From(const Descriptor *descriptor) { return static_cast<const VulkanDescriptorImpl *>(descriptor->m_impl); }
        static VulkanDescriptorImpl *TryFrom(Descriptor *descriptor) { return descriptor && descriptor->m_impl ? From(descriptor) : nullptr; }
        static const VulkanDescriptorImpl *TryFrom(const Descriptor *descriptor) { return descriptor && descriptor->m_impl ? From(descriptor) : nullptr; }

        Descriptor *m_owner{};
        vk::DescriptorSet m_set{};
    };

    inline vk::DescriptorPool GetVulkanDescriptorPool(DescriptorPool *pool)
    {
        VulkanDescriptorPoolImpl *impl = VulkanDescriptorPoolImpl::TryFrom(pool);
        return impl ? impl->m_pool : vk::DescriptorPool{};
    }

    inline vk::DescriptorPool GetVulkanDescriptorPool(const DescriptorPool *pool)
    {
        const VulkanDescriptorPoolImpl *impl = VulkanDescriptorPoolImpl::TryFrom(pool);
        return impl ? impl->m_pool : vk::DescriptorPool{};
    }

    inline vk::DescriptorSetLayout GetVulkanDescriptorLayout(DescriptorLayout *layout)
    {
        VulkanDescriptorLayoutImpl *impl = VulkanDescriptorLayoutImpl::TryFrom(layout);
        return impl ? impl->m_layout : vk::DescriptorSetLayout{};
    }

    inline vk::DescriptorSetLayout GetVulkanDescriptorLayout(const DescriptorLayout *layout)
    {
        const VulkanDescriptorLayoutImpl *impl = VulkanDescriptorLayoutImpl::TryFrom(layout);
        return impl ? impl->m_layout : vk::DescriptorSetLayout{};
    }

    inline vk::DescriptorSet GetVulkanDescriptorSet(Descriptor *descriptor)
    {
        VulkanDescriptorImpl *impl = VulkanDescriptorImpl::TryFrom(descriptor);
        return impl ? impl->m_set : vk::DescriptorSet{};
    }

    inline vk::DescriptorSet GetVulkanDescriptorSet(const Descriptor *descriptor)
    {
        const VulkanDescriptorImpl *impl = VulkanDescriptorImpl::TryFrom(descriptor);
        return impl ? impl->m_set : vk::DescriptorSet{};
    }

    vk::DescriptorType ToVkDescriptorType(PeBindingType type);
    PeBindingType FromVkDescriptorType(vk::DescriptorType type);

    vk::ShaderStageFlags ToVkShaderStageFlags(PeShaderStageFlags stages);
    PeShaderStageFlags FromVkShaderStageFlags(vk::ShaderStageFlags stages);
} // namespace pe
