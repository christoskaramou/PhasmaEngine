#pragma once

#include "API/Pipeline_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    struct Dx12PipelineImpl final : public Pipeline::Impl
    {
        Dx12PipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info);
        ~Dx12PipelineImpl() override = default;

        static Dx12PipelineImpl *From(Pipeline *pipeline) { return static_cast<Dx12PipelineImpl *>(pipeline->m_impl); }
        static const Dx12PipelineImpl *From(const Pipeline *pipeline) { return static_cast<const Dx12PipelineImpl *>(pipeline->m_impl); }

        ID3D12PipelineState *Get() const { return m_pso.Get(); }
        uint32_t GetVertexBindingStride(uint32_t binding) const
        {
            return binding < m_vertexBindingStrides.size() ? m_vertexBindingStrides[binding] : 0u;
        }

        Pipeline *m_owner{};
        PassInfo &m_info;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
        std::vector<uint32_t> m_vertexBindingStrides{};
    };

    inline ID3D12PipelineState *GetDx12Pipeline(Pipeline *pipeline)
    {
        return pipeline ? Dx12PipelineImpl::From(pipeline)->Get() : nullptr;
    }
} // namespace pe

#endif // PE_WIN32
