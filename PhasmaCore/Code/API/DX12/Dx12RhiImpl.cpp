#include "API/DX12/Dx12RhiImpl.h"

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
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                     &allowTearing, sizeof(allowTearing))))
        {
            m_allowTearing = allowTearing != FALSE;
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
        m_samplerStagingHeap =
            std::make_unique<Dx12DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2'048, false);
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

        return true;
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

    GpuMemorySnapshot Dx12RhiImpl::GetGpuMemorySnapshot()
    {
        GpuMemorySnapshot snap{};
        if (!m_adapter || !m_d3d12Allocator)
            return snap;

        ComPtr<IDXGIAdapter3> adapter3;
        if (FAILED(m_adapter.As(&adapter3)))
            return snap;

        DXGI_ADAPTER_DESC3 desc{};
        m_adapter->GetDesc3(&desc);

        DXGI_QUERY_VIDEO_MEMORY_INFO local{};
        DXGI_QUERY_VIDEO_MEMORY_INFO nonlocal{};
        // Phase-1 DX12 is single-node only: every device object and queue uses
        // NodeMask 0, so node 0 is the active DXGI memory node for now.
        constexpr UINT memoryNode = 0;
        const HRESULT localHr = adapter3->QueryVideoMemoryInfo(memoryNode, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        const HRESULT nonlocalHr = adapter3->QueryVideoMemoryInfo(memoryNode, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
        if (FAILED(localHr))
            PE_WARN("Dx12RhiImpl::GetGpuMemorySnapshot: LOCAL QueryVideoMemoryInfo failed (0x%08X)", static_cast<unsigned>(localHr));
        if (FAILED(nonlocalHr))
            PE_WARN("Dx12RhiImpl::GetGpuMemorySnapshot: NON_LOCAL QueryVideoMemoryInfo failed (0x%08X)", static_cast<unsigned>(nonlocalHr));

        D3D12MA::TotalStatistics stats{};
        m_d3d12Allocator->CalculateStatistics(&stats);

        const bool uma = m_d3d12Allocator->IsUMA();

        // LOCAL maps to vram. On UMA, LOCAL covers the full unified pool, so size
        // becomes dedicated + shared and the host segment stays empty.
        snap.vram.size = uma ? (desc.DedicatedVideoMemory + desc.SharedSystemMemory) : desc.DedicatedVideoMemory;
        snap.vram.budget = local.Budget;
        snap.vram.used = local.CurrentUsage;
        snap.vram.app = stats.MemorySegmentGroup[0].Stats.BlockBytes;
        snap.vram.heaps = 1;
        snap.vram.other = (snap.vram.used > snap.vram.app) ? (snap.vram.used - snap.vram.app) : 0;

        if (!uma)
        {
            snap.host.size = desc.SharedSystemMemory;
            snap.host.budget = nonlocal.Budget;
            snap.host.used = nonlocal.CurrentUsage;
            snap.host.app = stats.MemorySegmentGroup[1].Stats.BlockBytes;
            snap.host.heaps = 1;
            snap.host.other = (snap.host.used > snap.host.app) ? (snap.host.used - snap.host.app) : 0;
        }

        return snap;
    }
} // namespace pe
