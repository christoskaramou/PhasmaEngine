#pragma once

#include "API/Shader_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // One line of a module's binding sidecar (WebGPU RHI spec decision 7): a WGSL binding and its layout facts.
    struct WebGpuSidecarEntry
    {
        uint32_t group = 0;
        uint32_t binding = 0; // WebGPU binding: 2b, or 2b + 1 for a combined image sampler's sampler (decision 5)
        WebGpuBindingFacts facts{};
    };

    // Every module comes from the desktop bake: ShaderCache/_spv/<key> for engine reflection, _wgsl/<key> for the
    // device, and _wgsl/<key>.bindings for the layouts. The browser has no shader compiler to fall back to.
    struct WebGpuShaderImpl final : public Shader::Impl
    {
        explicit WebGpuShaderImpl(Shader *owner);
        ~WebGpuShaderImpl() override;

        static WebGpuShaderImpl *From(Shader *shader) { return static_cast<WebGpuShaderImpl *>(shader->m_impl); }
        static const WebGpuShaderImpl *TryFrom(const Shader *shader)
        {
            return shader && shader->m_impl ? static_cast<const WebGpuShaderImpl *>(shader->m_impl) : nullptr;
        }

        WGPUShaderModule GetOrCreateModule();

        Shader *m_owner = nullptr;
        std::vector<uint32_t> m_spirv; // engine reflection only; the device never sees it
        std::string m_wgsl;
        std::vector<WebGpuSidecarEntry> m_bindings;
        WGPUShaderModule m_module = nullptr;
    };

    // Two stages of one pass may see a binding differently: a texture one stage samples (float) and another only
    // loads (unfilterable-float) takes float, and a fact one stage lacks comes from the other. Other differences are
    // a PE_ERROR, since one layout entry cannot serve both modules.
    WebGpuBindingFacts MergeWebGpuBindingFacts(const WebGpuBindingFacts &a,
                                               const WebGpuBindingFacts &b,
                                               const std::string &bindingName);

    // Adds each sidecar line's facts to the engine resource it describes (engine binding = WebGPU binding / 2).
    void ApplyWebGpuBindingSidecar(Reflection &refl, Shader *shader);
} // namespace pe
