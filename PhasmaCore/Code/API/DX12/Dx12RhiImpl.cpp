#include "API/DX12/Dx12RhiImpl.h"

#include "API/Buffer.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12PipelineImpl.h"
#include "API/DX12/Dx12SwapchainImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/Pipeline.h"
#include "API/Shader.h"
#include "API/Swapchain.h"
#include "Base/FileWatcher.h"

#include <D3D12MemAlloc.h>

namespace pe
{
    using Microsoft::WRL::ComPtr;

    static bool EnvFlagOn(const char *name)
    {
        const char *v = std::getenv(name);
        return v && v[0] == '1';
    }

    static std::string WideToUtf8(const wchar_t *w)
    {
        if (!w)
            return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1)
            return {};
        std::string out(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
        return out;
    }

    bool Dx12RhiImpl::Init(SDL_Window * /*window*/)
    {
        // Debug interfaces must be acquired before D3D12CreateDevice.
#if !defined(PE_RELEASE)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                debug->EnableDebugLayer();
        }
#endif
        if (EnvFlagOn("PE_DX12_DEBUG"))
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                debug->EnableDebugLayer();
        }
        if (EnvFlagOn("PE_DX12_GBV"))
        {
            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug1))))
                debug1->SetEnableGPUBasedValidation(TRUE);
        }
#if defined(PE_DEBUG) || defined(PE_RELWITHDEBINFO)
        {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
            {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }
#endif

        UINT factoryFlags = 0;
#if !defined(PE_RELEASE)
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory))))
        {
            PE_ERROR("Dx12RhiImpl::Init: CreateDXGIFactory2 failed");
            return false;
        }

        for (UINT i = 0;
             m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                   IID_PPV_ARGS(&m_adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC3 desc{};
            m_adapter->GetDesc3(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
            {
                m_adapter.Reset();
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_1,
                                            IID_PPV_ARGS(&m_device))))
            {
                m_adapterName = WideToUtf8(desc.Description);
                break;
            }
            m_adapter.Reset();
        }

        if (!m_device)
        {
            PE_ERROR("Dx12RhiImpl::Init: no DX12-capable adapter found");
            return false;
        }

        PE_INFO("DX12 device created on '%s'", m_adapterName.c_str());

        D3D12MA::ALLOCATOR_DESC allocatorDesc{};
        allocatorDesc.pDevice = m_device.Get();
        allocatorDesc.pAdapter = m_adapter.Get();
        HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_d3d12Allocator);
        if (FAILED(hr))
        {
            PE_ERROR("Dx12RhiImpl::Init: D3D12MA::CreateAllocator failed");
            return false;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS opts{};
        m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts));
        if (opts.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3)
        {
            PE_ERROR("Dx12RhiImpl::Init: GPU lacks Tier-3 resource binding (full bindless)");
            return false;
        }
        if (opts.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
        {
            PE_ERROR("Dx12RhiImpl::Init: GPU lacks Tier-2 heap (placed-resource compatibility)");
            return false;
        }

        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
        m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
        if (sm.HighestShaderModel < D3D_SHADER_MODEL_6_6)
        {
            PE_ERROR("Dx12RhiImpl::Init: GPU/driver lacks Shader Model 6.6");
            return false;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
        m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5));
        m_caps.rayTracing = false; // forced off in Phase 1 regardless of device support

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
        m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7));
        m_caps.meshShaders = (opts7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);

        m_caps.dynamicRendering = true;
        m_caps.indirectCount = true;
        m_caps.maxPushConstantsBytes = 128;
        m_caps.maxBindlessTextures = 1'000'000;

        m_cbvSrvUavHeap =
            std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1'000'000, true);
        m_samplerHeap = std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2'048, true);
        m_cbvSrvUavStagingHeap =
            std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65'536, false);
        m_rtvStagingHeap = std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1'024, false);
        m_dsvStagingHeap = std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1'024, false);
        m_sharedRootSignature = std::make_unique<Dx12RootSignature>(m_device.Get());

#if !defined(PE_RELEASE)
        if (EnvFlagOn("PE_DX12_BREAK"))
        {
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            }
        }
#endif

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Priority = 0;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        if (FAILED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_graphicsQueue))))
        {
            PE_ERROR("Dx12RhiImpl::Init: CreateCommandQueue failed");
            return false;
        }

        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence))))
        {
            PE_ERROR("Dx12RhiImpl::Init: CreateFence failed");
            return false;
        }

        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
        {
            PE_ERROR("Dx12RhiImpl::Init: CreateEvent for fence failed");
            return false;
        }

        for (auto &alloc : m_clearAllocators)
        {
            if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&alloc))))
            {
                PE_ERROR("Dx12RhiImpl::Init: CreateCommandAllocator(clear) failed");
                return false;
            }
        }

        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               m_clearAllocators[0].Get(), nullptr,
                                               IID_PPV_ARGS(&m_clearCmdList))))
        {
            PE_ERROR("Dx12RhiImpl::Init: CreateCommandList(clear) failed");
            return false;
        }
        PE_CHECK(m_clearCmdList->Close());

        return true;
    }

    void Dx12RhiImpl::EnsureClearTriangleBuffer()
    {
        if (m_clearTriangleBuffer)
            return;

        std::array<vec4, 3> vertices = {
            vec4{-0.5f, -0.5f, 0.0f, 1.0f},
            vec4{0.0f, 0.5f, 0.0f, 1.0f},
            vec4{0.5f, -0.5f, 0.0f, 1.0f},
        };

        BufferDesc desc{};
        desc.size = vertices.size() * sizeof(vec4);
        desc.usage = PE_BUFFER_USAGE_VERTEX_BUFFER;
        desc.memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU;
        desc.name = "DX12_clear_triangle_vb";
        m_clearTriangleBuffer = Buffer::Create(desc);

        BufferRange range{};
        range.data = vertices.data();
        range.size = desc.size;
        range.offset = 0;
        m_clearTriangleBuffer->Copy(1, &range, false);
    }

    void Dx12RhiImpl::EnsureClearTrianglePipeline()
    {
        if (m_clearTrianglePipeline)
            return;

        const std::string shaderPath = Path::Assets + "Shaders/DX12/ClearTriangle.hlsl";
        if (!FileWatcher::Get(shaderPath))
            FileWatcher::Add(shaderPath, [](size_t) {});

        ShaderDesc vsDesc{};
        vsDesc.sourcePath = shaderPath;
        vsDesc.entryPoint = "mainVS";
        vsDesc.stage = PE_SHADER_STAGE_VERTEX;
        vsDesc.debugName = "DX12_clear_triangle_vs";

        ShaderDesc psDesc{};
        psDesc.sourcePath = shaderPath;
        psDesc.entryPoint = "mainPS";
        psDesc.stage = PE_SHADER_STAGE_FRAGMENT;
        psDesc.debugName = "DX12_clear_triangle_ps";

        m_clearTrianglePassInfo = new PassInfo();
        m_clearTrianglePassInfo->name = "DX12_clear_triangle_pipeline";
        m_clearTrianglePassInfo->pVertShader = Shader::Create(vsDesc);
        m_clearTrianglePassInfo->pFragShader = Shader::Create(psDesc);
        m_clearTrianglePassInfo->topology = PE_TOPOLOGY_TRIANGLE_LIST;
        m_clearTrianglePassInfo->polygonMode = PE_POLYGON_MODE_FILL;
        m_clearTrianglePassInfo->cullMode = PE_CULL_MODE_NONE;
        m_clearTrianglePassInfo->blendEnable = false;
        m_clearTrianglePassInfo->colorBlendAttachments = {BlendState::Default};
        m_clearTrianglePassInfo->colorFormats = {PE_FORMAT_R8G8B8A8_UNORM};
        m_clearTrianglePassInfo->depthFormat = PE_FORMAT_UNDEFINED;
        m_clearTrianglePassInfo->depthTestEnable = false;
        m_clearTrianglePassInfo->depthWriteEnable = false;
        m_clearTrianglePassInfo->Update();

        m_clearTrianglePipeline = Pipeline::Create(nullptr, *m_clearTrianglePassInfo);
    }

    void Dx12RhiImpl::DrawClearScreen(Swapchain *sc)
    {
        PE_ERROR_IF(!sc, "Dx12RhiImpl::DrawClearScreen requires a swapchain");
        auto *scImpl = static_cast<Dx12SwapchainImpl *>(sc->m_impl);
        const uint32_t bb = sc->AquireNextImage(nullptr);
        EnsureClearTriangleBuffer();
        EnsureClearTrianglePipeline();

        if (m_frameFence->GetCompletedValue() < m_frameFenceValues[m_clearFrameIndex])
        {
            PE_CHECK(m_frameFence->SetEventOnCompletion(m_frameFenceValues[m_clearFrameIndex], m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }

        PE_CHECK(m_clearAllocators[m_clearFrameIndex]->Reset());
        PE_CHECK(m_clearCmdList->Reset(m_clearAllocators[m_clearFrameIndex].Get(), nullptr));

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = m_clearTriangleBuffer->GetDeviceAddress();
        vbv.SizeInBytes = static_cast<UINT>(m_clearTriangleBuffer->Size());
        vbv.StrideInBytes = sizeof(vec4);
        m_clearCmdList->SetPipelineState(GetDx12Pipeline(m_clearTrianglePipeline));
        m_clearCmdList->SetGraphicsRootSignature(m_sharedRootSignature->Get());
        ID3D12DescriptorHeap *heaps[] = {m_cbvSrvUavHeap->Get(), m_samplerHeap->Get()};
        m_clearCmdList->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        m_clearCmdList->SetGraphicsRootDescriptorTable(3, m_cbvSrvUavHeap->GetGpuHandle(0));
        m_clearCmdList->SetGraphicsRootDescriptorTable(4, m_cbvSrvUavHeap->GetGpuHandle(0));
        m_clearCmdList->SetGraphicsRootDescriptorTable(5, m_samplerHeap->GetGpuHandle(0));
        m_clearCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_clearCmdList->IASetVertexBuffers(0, 1, &vbv);

        D3D12_RESOURCE_BARRIER toRt{};
        toRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRt.Transition.pResource = scImpl->GetBackbuffer(bb);
        toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_clearCmdList->ResourceBarrier(1, &toRt);

        m_clearCmdList->ClearRenderTargetView(scImpl->GetRtv(bb), m_clearColor, 0, nullptr);
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(sc->GetWidth());
        viewport.Height = static_cast<float>(sc->GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{};
        scissor.right = static_cast<LONG>(sc->GetWidth());
        scissor.bottom = static_cast<LONG>(sc->GetHeight());
        m_clearCmdList->RSSetViewports(1, &viewport);
        m_clearCmdList->RSSetScissorRects(1, &scissor);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = scImpl->GetRtv(bb);
        m_clearCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        m_clearCmdList->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER toPresent = toRt;
        std::swap(toPresent.Transition.StateBefore, toPresent.Transition.StateAfter);
        m_clearCmdList->ResourceBarrier(1, &toPresent);

        PE_CHECK(m_clearCmdList->Close());
        ID3D12CommandList *lists[] = {m_clearCmdList.Get()};
        m_graphicsQueue->ExecuteCommandLists(1, lists);

        sc->Present();

        const uint64_t signalValue = ++m_fenceValue;
        PE_CHECK(m_graphicsQueue->Signal(m_frameFence.Get(), signalValue));
        m_frameFenceValues[m_clearFrameIndex] = signalValue;
        m_clearFrameIndex = (m_clearFrameIndex + 1) % static_cast<uint32_t>(m_clearAllocators.size());
    }

    void Dx12RhiImpl::WaitDeviceIdle()
    {
        if (!m_graphicsQueue || !m_frameFence)
            return;

        const uint64_t target = ++m_fenceValue;
        m_graphicsQueue->Signal(m_frameFence.Get(), target);
        if (m_frameFence->GetCompletedValue() < target)
        {
            m_frameFence->SetEventOnCompletion(target, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    void Dx12RhiImpl::Shutdown()
    {
        WaitDeviceIdle();
        Pipeline::Destroy(m_clearTrianglePipeline);
        delete m_clearTrianglePassInfo;
        m_clearTrianglePassInfo = nullptr;
        Buffer::Destroy(m_clearTriangleBuffer);
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        if (m_d3d12Allocator)
        {
            m_d3d12Allocator->Release();
            m_d3d12Allocator = nullptr;
        }
    }

    void Dx12RhiImpl::NextFrame() {}
} // namespace pe
