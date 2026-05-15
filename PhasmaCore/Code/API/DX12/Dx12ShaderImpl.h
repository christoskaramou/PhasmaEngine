#pragma once

#include "API/Shader_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <d3d12shader.h>
#include <wrl/client.h>

namespace pe
{
    struct Dx12ShaderImpl final : public Shader::Impl
    {
        Dx12ShaderImpl(Shader *owner, const ShaderDesc &desc);
        Dx12ShaderImpl(Shader *owner, std::vector<uint8_t> dxil);
        ~Dx12ShaderImpl() override = default;

        static Dx12ShaderImpl *From(Shader *shader) { return static_cast<Dx12ShaderImpl *>(shader->m_impl); }
        static const Dx12ShaderImpl *From(const Shader *shader) { return static_cast<const Dx12ShaderImpl *>(shader->m_impl); }
        static Dx12ShaderImpl *TryFrom(Shader *shader) { return shader && shader->m_impl ? From(shader) : nullptr; }
        static const Dx12ShaderImpl *TryFrom(const Shader *shader) { return shader && shader->m_impl ? From(shader) : nullptr; }

        D3D12_SHADER_BYTECODE GetBytecode() const;

        Shader *m_owner{};
        std::vector<uint8_t> m_dxil{};
    };

    inline const uint8_t *GetDx12ShaderDxil(const Shader *shader)
    {
        const Dx12ShaderImpl *impl = Dx12ShaderImpl::TryFrom(shader);
        return impl && !impl->m_dxil.empty() ? impl->m_dxil.data() : nullptr;
    }

    inline size_t GetDx12ShaderDxilSizeBytes(const Shader *shader)
    {
        const Dx12ShaderImpl *impl = Dx12ShaderImpl::TryFrom(shader);
        return impl ? impl->m_dxil.size() : 0;
    }

    inline D3D12_SHADER_BYTECODE GetDx12ShaderBytecode(const Shader *shader)
    {
        const Dx12ShaderImpl *impl = Dx12ShaderImpl::TryFrom(shader);
        return impl ? impl->GetBytecode() : D3D12_SHADER_BYTECODE{};
    }

    bool ReflectStructuredBufferMembersFromDx12(Shader *shader,
                                                const std::string &bufferName,
                                                std::vector<StructMemberInfo> &outMembers,
                                                uint32_t &outTotalByteSize);

    void PopulateReflectionFromDxil(Reflection &refl, Shader *shader);
} // namespace pe

#endif // PE_WIN32
