#if PE_DX12
#include "Renderer/Debug.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "RenderDoc/renderdoc_app.h"

namespace pe
{
#if PE_DEBUG_MODE
    void Debug::CreateDebugMessengerDX()
    {
        ID3D12Debug3 *debug;
        PE_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
        debug->EnableDebugLayer();
        // debug->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);
        // debug->SetEnableGPUBasedValidation(true);
        debug->SetEnableSynchronizedCommandQueueValidation(true);
        debug->Release();

        IDXGIInfoQueue *infoQueue = nullptr;
        PE_CHECK(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&infoQueue)));
        infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, true);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO, true);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE, true);
        infoQueue->Release();
    }

    void Debug::DestroyDebugMessengerDX()
    {
        IDXGIInfoQueue *infoQueue = nullptr;
        PE_CHECK(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&infoQueue)));
        infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, false);
        infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, false);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, false);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO, false);
        // infoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE, false);
        infoQueue->Release();

        IDXGIDebug1 *dxgiDebug;
        PE_CHECK(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)));
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        dxgiDebug->Release();
    }

    void Debug::SetObjectNameDX(uintptr_t object, ObjectType type, const std::string &name)
    {
        ID3D12Object *obj = reinterpret_cast<ID3D12Object *>(object);
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<uint32_t>(name.size()), name.c_str());
    }
#endif
};
#endif
