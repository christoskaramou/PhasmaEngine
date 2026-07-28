#include "API/DX12/Dx12RhiImpl.h"

#include <D3D12MemAlloc.h>

namespace pe
{
    using Microsoft::WRL::ComPtr;

    static std::string EnvFlagValue(const char *name)
    {
#if defined(PE_WIN32)
        char *value = nullptr;
        size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, name) != 0 || !value)
            return {};

        std::string result(value);
        std::free(value);
        return result;
#else
        const char *v = std::getenv(name);
        return v ? std::string(v) : std::string{};
#endif
    }

    static bool EnvFlagOn(const char *name)
    {
        const std::string value = EnvFlagValue(name);
        return !value.empty() && value[0] == '1';
    }

    static bool EnvFlagOff(const char *name)
    {
        const std::string value = EnvFlagValue(name);
        return !value.empty() && value[0] == '0';
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

    static GpuAdapterType Dx12AdapterType(const DXGI_ADAPTER_DESC3 &desc)
    {
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
            return GpuAdapterType::Cpu;
        return desc.DedicatedVideoMemory > 0 ? GpuAdapterType::DiscreteGpu : GpuAdapterType::IntegratedGpu;
    }

    static DXGI_GPU_PREFERENCE Dx12GpuPreferenceOrder(GpuAdapterPreference preference)
    {
        switch (preference)
        {
        case GpuAdapterPreference::IntegratedGpu:
            return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
        case GpuAdapterPreference::DiscreteGpu:
            return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
        default:
            return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
        }
    }

    static bool MatchesDx12AdapterPreference(GpuAdapterPreference preference, GpuAdapterType type)
    {
        switch (preference)
        {
        case GpuAdapterPreference::Auto:
            return true;
        case GpuAdapterPreference::IntegratedGpu:
            return type == GpuAdapterType::IntegratedGpu;
        case GpuAdapterPreference::DiscreteGpu:
            return type == GpuAdapterType::DiscreteGpu;
        case GpuAdapterPreference::Cpu:
            return type == GpuAdapterType::Cpu;
        default:
            return true;
        }
    }

    static const char *D3D12MessageSeverityName(D3D12_MESSAGE_SEVERITY severity)
    {
        switch (severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            return "corruption";
        case D3D12_MESSAGE_SEVERITY_ERROR:
            return "error";
        case D3D12_MESSAGE_SEVERITY_WARNING:
            return "warning";
        case D3D12_MESSAGE_SEVERITY_INFO:
            return "info";
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
            return "message";
        default:
            return "unknown";
        }
    }

    static bool ShouldLogD3D12Message(D3D12_MESSAGE_CATEGORY category,
                                      D3D12_MESSAGE_SEVERITY severity,
                                      D3D12_MESSAGE_ID id,
                                      uint32_t &repeatCount)
    {
        static std::mutex s_messageCountsMutex;
        static std::unordered_map<uint64_t, uint32_t> s_messageCounts;

        const uint64_t key = (static_cast<uint64_t>(severity) << 48) |
                             (static_cast<uint64_t>(category) << 32) |
                             static_cast<uint32_t>(id);

        std::lock_guard<std::mutex> lock(s_messageCountsMutex);
        repeatCount = ++s_messageCounts[key];
        return repeatCount <= 4;
    }

    static void CALLBACK Dx12InfoQueueMessageCallback(D3D12_MESSAGE_CATEGORY category,
                                                      D3D12_MESSAGE_SEVERITY severity,
                                                      D3D12_MESSAGE_ID id,
                                                      LPCSTR description,
                                                      void * /*context*/)
    {
        if (severity != D3D12_MESSAGE_SEVERITY_CORRUPTION &&
            severity != D3D12_MESSAGE_SEVERITY_ERROR &&
            severity != D3D12_MESSAGE_SEVERITY_WARNING)
            return;

        uint32_t repeatCount = 0;
        if (!ShouldLogD3D12Message(category, severity, id, repeatCount))
            return;

        PE_WARN("DX12 debug layer %s [category=%u id=%d]: %s%s",
                D3D12MessageSeverityName(severity),
                static_cast<uint32_t>(category),
                static_cast<int32_t>(id),
                description ? description : "",
                repeatCount == 4 ? " (further repeats suppressed)" : "");
    }

    static const char *DxgiDeviceRemovedReasonName(HRESULT reason)
    {
        switch (reason)
        {
        case DXGI_ERROR_DEVICE_HUNG:
            return "DEVICE_HUNG (a submitted workload did not complete - GPU timeout/TDR)";
        case DXGI_ERROR_DEVICE_REMOVED:
            return "DEVICE_REMOVED (adapter lost)";
        case DXGI_ERROR_DEVICE_RESET:
            return "DEVICE_RESET";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            return "DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL:
            return "INVALID_CALL (bad API usage - run with PE_DX12_DEBUG=1)";
        default:
            return "unknown reason";
        }
    }

    static const char *Dx12BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op)
    {
        switch (op)
        {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
            return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
            return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
            return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
            return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
            return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
            return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
            return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
            return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
            return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
            return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:
            return "ResolveSubresource";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
            return "ClearRenderTargetView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
            return "ClearUnorderedAccessView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
            return "ClearDepthStencilView";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
            return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_PRESENT:
            return "Present";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:
            return "BeginSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:
            return "EndSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE:
            return "WriteBufferImmediate";
        case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE:
            return "BuildRaytracingAS";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE:
            return "CopyRaytracingAS";
        case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO:
            return "EmitRaytracingASPostbuildInfo";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS:
            return "DispatchRays";
        case D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1:
            return "SetPipelineState1";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH:
            return "DispatchMesh";
        default:
            return "op";
        }
    }

    static const char *DredAllocationTypeName(D3D12_DRED_ALLOCATION_TYPE type)
    {
        switch (type)
        {
        case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:
            return "CommandQueue";
        case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:
            return "CommandAllocator";
        case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:
            return "PipelineState";
        case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:
            return "CommandList";
        case D3D12_DRED_ALLOCATION_TYPE_FENCE:
            return "Fence";
        case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:
            return "DescriptorHeap";
        case D3D12_DRED_ALLOCATION_TYPE_HEAP:
            return "Heap";
        case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:
            return "QueryHeap";
        case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:
            return "CommandSignature";
        case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY:
            return "PipelineLibrary";
        case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:
            return "Resource";
        case D3D12_DRED_ALLOCATION_TYPE_PASS:
            return "Pass";
        default:
            return "other";
        }
    }

    void Dx12ReportDeviceRemoved()
    {
        ID3D12Device *device = GetDx12Device();
        if (!device)
            return;

        const HRESULT reason = device->GetDeviceRemovedReason();
        if (reason == S_OK)
            return;

        PE_WARN("[DX12] Device removed: 0x%08X %s",
                static_cast<unsigned>(reason),
                DxgiDeviceRemovedReasonName(reason));

        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred))))
        {
            PE_WARN("[DX12] No DRED data available (enable with PE_DX12_DRED=1)");
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)))
        {
            for (const D3D12_AUTO_BREADCRUMB_NODE *node = breadcrumbs.pHeadAutoBreadcrumbNode; node;
                 node = node->pNext)
            {
                const UINT count = node->BreadcrumbCount;
                const UINT completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                if (count == 0 || completed >= count)
                    continue;

                std::string cmdList = WideToUtf8(node->pCommandListDebugNameW);
                if (cmdList.empty() && node->pCommandListDebugNameA)
                    cmdList = node->pCommandListDebugNameA;
                std::string queue = WideToUtf8(node->pCommandQueueDebugNameW);
                if (queue.empty() && node->pCommandQueueDebugNameA)
                    queue = node->pCommandQueueDebugNameA;
                PE_WARN("[DX12] DRED breadcrumbs: cmdlist='%s' queue='%s' completed %u of %u ops",
                        cmdList.c_str(), queue.c_str(), completed, count);

                const UINT begin = completed > 8 ? completed - 8 : 0;
                const UINT end = std::min(count, completed + 4);
                for (UINT i = begin; i < end && node->pCommandHistory; ++i)
                {
                    const D3D12_AUTO_BREADCRUMB_OP op = node->pCommandHistory[i];
                    PE_WARN("[DX12]   [%u] %s(%u)%s", i, Dx12BreadcrumbOpName(op),
                            static_cast<unsigned>(op), i == completed ? "  <-- did not complete" : "");
                }
            }
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)) && pageFault.PageFaultVA != 0)
        {
            PE_WARN("[DX12] DRED page fault VA=0x%016llX",
                    static_cast<unsigned long long>(pageFault.PageFaultVA));
            int logged = 0;
            for (const D3D12_DRED_ALLOCATION_NODE *node = pageFault.pHeadExistingAllocationNode;
                 node && logged < 16; node = node->pNext, ++logged)
            {
                std::string name = WideToUtf8(node->ObjectNameW);
                if (name.empty() && node->ObjectNameA)
                    name = node->ObjectNameA;
                PE_WARN("[DX12]   existing allocation near VA: '%s' (%s)", name.c_str(),
                        DredAllocationTypeName(node->AllocationType));
            }
            logged = 0;
            for (const D3D12_DRED_ALLOCATION_NODE *node = pageFault.pHeadRecentFreedAllocationNode;
                 node && logged < 16; node = node->pNext, ++logged)
            {
                std::string name = WideToUtf8(node->ObjectNameW);
                if (name.empty() && node->ObjectNameA)
                    name = node->ObjectNameA;
                PE_WARN("[DX12]   recently freed near VA: '%s' (%s)", name.c_str(),
                        DredAllocationTypeName(node->AllocationType));
            }
        }
    }

    bool Dx12RhiImpl::Init(SDL_Window * /*window*/)
    {
        const bool dx12DebugLayerRequested = EnvFlagOn("PE_DX12_DEBUG");
        const bool dx12DebugLayerSuppressed = EnvFlagOff("PE_DX12_DEBUG");

        // Debug interfaces must be acquired before D3D12CreateDevice.
        const bool enableDx12DebugLayer =
#if !defined(PE_RELEASE)
            !dx12DebugLayerSuppressed || dx12DebugLayerRequested;
#else
            dx12DebugLayerRequested;
#endif

        bool dx12DebugLayerEnabled = false;
        if (enableDx12DebugLayer)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                dx12DebugLayerEnabled = true;
            }
        }
        if (EnvFlagOn("PE_DX12_GBV"))
        {
            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug1))))
                debug1->SetEnableGPUBasedValidation(TRUE);
        }

        const bool dx12DredRequested = EnvFlagOn("PE_DX12_DRED");
        const bool dx12DredSuppressed = EnvFlagOff("PE_DX12_DRED");
        const bool enableDx12Dred =
#if defined(PE_DEBUG) || defined(PE_RELWITHDEBINFO)
            !dx12DredSuppressed || dx12DredRequested;
#else
            dx12DredRequested;
#endif
        if (enableDx12Dred)
        {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
            {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }

        UINT factoryFlags = 0;
        if (dx12DebugLayerEnabled)
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
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

        std::string preferenceWarning;
        const GpuAdapterPreference adapterPreference = ResolveGpuAdapterPreference(&preferenceWarning);
        if (!preferenceWarning.empty())
            PE_WARN("[DX12] %s", preferenceWarning.c_str());

        auto tryCreateWarpDevice = [&]() -> bool
        {
            m_adapter.Reset();
            if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&m_adapter))))
                return false;

            DXGI_ADAPTER_DESC3 desc{};
            m_adapter->GetDesc3(&desc);
            if (SUCCEEDED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                            IID_PPV_ARGS(&m_device))))
            {
                m_adapterName = WideToUtf8(desc.Description);
                return true;
            }

            m_adapter.Reset();
            return false;
        };

        auto tryCreateDevice = [&](GpuAdapterPreference preference, bool requireMatch) -> bool
        {
            if (preference == GpuAdapterPreference::Cpu)
                return tryCreateWarpDevice();

            const DXGI_GPU_PREFERENCE order = Dx12GpuPreferenceOrder(preference);
            for (UINT i = 0;
                 m_factory->EnumAdapterByGpuPreference(i, order, IID_PPV_ARGS(&m_adapter)) != DXGI_ERROR_NOT_FOUND;
                 ++i)
            {
                DXGI_ADAPTER_DESC3 desc{};
                m_adapter->GetDesc3(&desc);
                const GpuAdapterType adapterType = Dx12AdapterType(desc);
                if (adapterType == GpuAdapterType::Cpu)
                {
                    m_adapter.Reset();
                    continue;
                }
                if (requireMatch && !MatchesDx12AdapterPreference(preference, adapterType))
                {
                    m_adapter.Reset();
                    continue;
                }
                if (SUCCEEDED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                                IID_PPV_ARGS(&m_device))))
                {
                    m_adapterName = WideToUtf8(desc.Description);
                    return true;
                }
                m_adapter.Reset();
            }
            return false;
        };

        if (adapterPreference == GpuAdapterPreference::Auto)
        {
            tryCreateDevice(adapterPreference, false);
        }
        else if (tryCreateDevice(adapterPreference, true))
        {
            PE_INFO("[DX12] GPU adapter preference '%s' matched: %s",
                    GpuAdapterPreferenceConfigName(adapterPreference),
                    m_adapterName.c_str());
        }
        else
        {
            PE_WARN("[DX12] GPU adapter preference '%s' unavailable; using automatic selection",
                    GpuAdapterPreferenceConfigName(adapterPreference));
            tryCreateDevice(GpuAdapterPreference::Auto, false);
        }

        if (!m_device)
        {
            PE_ERROR("Dx12RhiImpl::Init: no DX12-capable adapter found");
            return false;
        }

        PE_INFO("DX12 device created on '%s'", m_adapterName.c_str());

        HRESULT hr = S_OK;
        if (dx12DebugLayerEnabled)
        {
            ComPtr<ID3D12InfoQueue1> infoQueue;
            if (SUCCEEDED(m_device.As(&infoQueue)))
            {
                hr = infoQueue->RegisterMessageCallback(Dx12InfoQueueMessageCallback,
                                                        D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                        nullptr,
                                                        &m_infoQueueCallbackCookie);
                if (SUCCEEDED(hr))
                {
                    m_infoQueueCallback = infoQueue;
                    PE_INFO("DX12 info-queue callback registered");
                }
                else
                {
                    PE_WARN("Dx12RhiImpl::Init: RegisterMessageCallback failed (0x%08X)",
                            static_cast<unsigned>(hr));
                }
            }
            else
            {
                PE_WARN("Dx12RhiImpl::Init: ID3D12InfoQueue1 unavailable; debug messages require PIX or manual queue draining");
            }
        }

        D3D12MA::ALLOCATOR_DESC allocatorDesc{};
        allocatorDesc.pDevice = m_device.Get();
        allocatorDesc.pAdapter = m_adapter.Get();
        hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_d3d12Allocator);
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
        const HRESULT shaderModelHr =
            m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
        if (FAILED(shaderModelHr) || sm.HighestShaderModel < D3D_SHADER_MODEL_6_6)
        {
            PE_ERROR("Dx12RhiImpl::Init: GPU/driver lacks Shader Model 6.6");
            return false;
        }

        m_caps.bufferDeviceAddress = true;
        PE_INFO("[DX12] Feature level minimum=12_0, shader model minimum=6.6, start-instance input=per-instance vertex");

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
        m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5));
        m_rayTracingTier = opts5.RaytracingTier;
        m_caps.rayTracing = SupportsDxr();
        if (SupportsDxr())
        {
            PE_INFO("[DX12] DXR tier %u available; ray-tracing cap enabled",
                    static_cast<uint32_t>(m_rayTracingTier));
        }

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
        if (m_infoQueueCallback && m_infoQueueCallbackCookie)
        {
            m_infoQueueCallback->UnregisterMessageCallback(m_infoQueueCallbackCookie);
            m_infoQueueCallbackCookie = 0;
        }
        m_infoQueueCallback.Reset();
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
