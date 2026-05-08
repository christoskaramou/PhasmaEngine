#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Shader_Internal.h"

namespace pe
{
    struct VulkanShaderImpl final : public Shader::Impl
    {
        VulkanShaderImpl(Shader *owner, const ShaderDesc &desc);
        VulkanShaderImpl(Shader *owner, const uint32_t *spirv, size_t sizeWords);
        ~VulkanShaderImpl() override;

        static VulkanShaderImpl *From(Shader *shader) { return static_cast<VulkanShaderImpl *>(shader->m_impl); }
        static const VulkanShaderImpl *From(const Shader *shader) { return static_cast<const VulkanShaderImpl *>(shader->m_impl); }
        static VulkanShaderImpl *TryFrom(Shader *shader) { return shader && shader->m_impl ? From(shader) : nullptr; }
        static const VulkanShaderImpl *TryFrom(const Shader *shader) { return shader && shader->m_impl ? From(shader) : nullptr; }

        vk::ShaderModule GetOrCreateModule();
        void DestroyModule();

        Shader *m_owner{};
        std::vector<uint32_t> m_spirv{};
        vk::ShaderModule m_module{};
    };

    inline const uint32_t *GetVulkanShaderSpirv(const Shader *shader)
    {
        const VulkanShaderImpl *impl = VulkanShaderImpl::TryFrom(shader);
        return impl ? impl->m_spirv.data() : nullptr;
    }

    inline size_t GetVulkanShaderSpirvSizeWords(const Shader *shader)
    {
        const VulkanShaderImpl *impl = VulkanShaderImpl::TryFrom(shader);
        return impl ? impl->m_spirv.size() : 0;
    }

    inline size_t GetVulkanShaderSpirvSizeBytes(const Shader *shader)
    {
        return GetVulkanShaderSpirvSizeWords(shader) * sizeof(uint32_t);
    }

    inline vk::ShaderModule GetOrCreateVulkanShaderModule(Shader *shader)
    {
        VulkanShaderImpl *impl = VulkanShaderImpl::TryFrom(shader);
        return impl ? impl->GetOrCreateModule() : vk::ShaderModule{};
    }

    vk::ShaderStageFlagBits ToVkSingleShaderStage(PeShaderStageFlags stage);
} // namespace pe
