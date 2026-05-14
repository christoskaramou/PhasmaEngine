#pragma once

#include "API/Pipeline_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    class Buffer;

    struct Dx12PipelineImpl final : public Pipeline::Impl
    {
        Dx12PipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info);
        ~Dx12PipelineImpl() override;

        static Dx12PipelineImpl *From(Pipeline *pipeline) { return static_cast<Dx12PipelineImpl *>(pipeline->m_impl); }
        static const Dx12PipelineImpl *From(const Pipeline *pipeline) { return static_cast<const Dx12PipelineImpl *>(pipeline->m_impl); }

        ID3D12PipelineState *Get() const { return m_pso.Get(); }
        ID3D12StateObject *GetRtStateObject() const { return m_rtStateObject.Get(); }
        D3D12_DISPATCH_RAYS_DESC GetDispatchRaysDesc(uint32_t width, uint32_t height, uint32_t depth) const;
        bool IsRayTracing() const { return m_rtStateObject != nullptr; }
        uint32_t GetVertexBindingStride(uint32_t binding) const
        {
            return binding < m_vertexBindingStrides.size() ? m_vertexBindingStrides[binding] : 0u;
        }

        Pipeline *m_owner{};
        PassInfo &m_info;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
        Microsoft::WRL::ComPtr<ID3D12StateObject> m_rtStateObject;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_rtProperties;
        Buffer *m_sbtBuffer = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE m_rayGenRegion{};
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE m_missRegion{};
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE m_hitRegion{};
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE m_callableRegion{};
        std::vector<uint32_t> m_vertexBindingStrides{};

    private:
        void CreateRayTracingPipeline();
        void CreateRayTracingSBT(const std::wstring &rayGenExport,
                                 const std::vector<std::wstring> &missExports,
                                 const std::vector<std::wstring> &hitGroupExports);
    };

    inline ID3D12PipelineState *GetDx12Pipeline(Pipeline *pipeline)
    {
        return pipeline ? Dx12PipelineImpl::From(pipeline)->Get() : nullptr;
    }

    inline ID3D12StateObject *GetDx12RayTracingStateObject(Pipeline *pipeline)
    {
        return pipeline ? Dx12PipelineImpl::From(pipeline)->GetRtStateObject() : nullptr;
    }
} // namespace pe

#endif // PE_WIN32
