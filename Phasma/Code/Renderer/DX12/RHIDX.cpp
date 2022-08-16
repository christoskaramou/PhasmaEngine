#if PE_DX12
#include "Renderer/RHI.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")

using namespace Microsoft::WRL;

namespace pe
{
    void RHI::InitDX()
    {
        Debug::CreateDebugMessengerDX();
        CreateInstanceDX();
        FindGpuDX();
        CreateDeviceDX();
    }

    void RHI::DestroyDX()
    {
        if (m_instanceDX)
            m_instanceDX->Release();
        if (m_deviceDX)
            m_deviceDX->Release();

        Debug::DestroyDebugMessengerDX();
    }

    void RHI::CreateInstanceDX()
    {
        uint32_t instanceFlags = 0;
#ifdef PE_DEBUG_MODE
        instanceFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

        IDXGIFactory7 *instance = nullptr;
        PE_CHECK(CreateDXGIFactory2(instanceFlags, IID_PPV_ARGS(&instance)));
        m_instanceDX = instance;
    }

    void RHI::FindGpuDX()
    {
        IDXGIAdapter4 *gpu = nullptr;
        uint32_t i = 0;
        while (m_instanceDX->EnumAdapterByGpuPreference(
                   i++, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&gpu)) != DXGI_ERROR_NOT_FOUND)
        {
            HRESULT hr = D3D12CreateDevice(gpu, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                m_gpuDX = gpu;
                break;
            }

            ReleaseDX(gpu);
        }
    }

    void RHI::CreateDeviceDX()
    {
        ID3D12Device8 *device = nullptr;
        PE_CHECK(D3D12CreateDevice(m_gpuDX, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
        m_deviceDX = device;
    }
}
#endif
