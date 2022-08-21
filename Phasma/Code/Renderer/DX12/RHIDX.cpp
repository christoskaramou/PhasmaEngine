// #if PE_DX12
// #include "Renderer/RHI.h"
// #include "Systems/RendererSystem.h"
// #include "Renderer/Command.h"
// #include "Renderer/Semaphore.h"
// #include "Renderer/Descriptor.h"
// #include "Renderer/Image.h"
// #include "Renderer/Swapchain.h"
// #include "Renderer/Swapchain.h"
// #include "Renderer/Queue.h"
// #include "Renderer/Buffer.h"
// #include "Utilities/Downsampler.h"

// #pragma comment(lib, "dxgi.lib")
// #pragma comment(lib, "d3d12.lib")
// #pragma comment(lib, "dxguid.lib")

// using namespace Microsoft::WRL;

// namespace pe
// {

//     constexpr D3D_FEATURE_LEVEL c_featureLevel = D3D_FEATURE_LEVEL_12_1;

//     void RHI::InitDX(SDL_Window *window)
//     {
//         m_window = window;
//         Debug::CreateDebugMessengerDX();
//         CreateInstanceDX();
//         FindGpuDX();
//         CreateSurfaceDX();
//         CreateDeviceDX();
//     }

//     void RHI::DestroyDX()
//     {
//         if (m_instanceDX)
//             m_instanceDX->Release();
//         if (m_deviceDX)
//             m_deviceDX->Release();

//         Debug::DestroyDebugMessengerDX();
//     }

//     void RHI::CreateInstanceDX()
//     {
//         uint32_t instanceFlags = 0;
// #ifdef PE_DEBUG_MODE
//         instanceFlags = DXGI_CREATE_FACTORY_DEBUG;
// #endif

//         IDXGIFactory7 *instance = nullptr;
//         PE_CHECK(CreateDXGIFactory2(instanceFlags, IID_PPV_ARGS(&instance)));
//         m_instanceDX = instance;
//     }

//     void RHI::CreateSurfaceDX()
//     {
//         // m_surface = Surface::Create(m_window);

//         IDXGIOutput6 *dxgiOutput6 = nullptr;

//         for (uint32_t i = 0;; i++)
//         {
//             IDXGIOutput *dxgiOutput = nullptr;
//             HRESULT hr = m_gpuDX->EnumOutputs(i, &dxgiOutput);
//             if (hr == DXGI_ERROR_NOT_FOUND)
//                 break;

//             PE_CHECK(dxgiOutput->QueryInterface(IID_PPV_ARGS(&dxgiOutput6)));
//             ReleaseDX(dxgiOutput);

//             uint32_t numModes;
//             dxgiOutput6->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);

//             if (numModes <= 0)
//             {
//                 ReleaseDX(dxgiOutput6);
//                 continue;
//             }

//             DXGI_OUTPUT_DESC1 outputDesc;
//             dxgiOutput6->GetDesc1(&outputDesc);
//             if (outputDesc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
//             {
//                 ReleaseDX(dxgiOutput6);
//                 continue;
//             }

//             std::vector<DXGI_MODE_DESC1> desc(numModes);
//             dxgiOutput6->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, desc.data());
//         }
//     }

//     void RHI::FindGpuDX()
//     {
//         DXGI_GPU_PREFERENCE preference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
//         for (UINT i = 0;; ++i)
//         {
//             IDXGIAdapter1 *pAdapter = nullptr;
//             HRESULT hr = m_instanceDX->EnumAdapterByGpuPreference(i, preference, IID_PPV_ARGS(&pAdapter));
//             if (hr == DXGI_ERROR_NOT_FOUND)
//                 break;

//             if (SUCCEEDED(D3D12CreateDevice(pAdapter, c_featureLevel, _uuidof(ID3D12Device), nullptr)))
//             {
//                 IDXGIAdapter4 *pAdapter4 = nullptr;
//                 PE_CHECK(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter4)));

//                 DXGI_ADAPTER_DESC3 desc;
//                 PE_CHECK(pAdapter4->GetDesc3(&desc));
//                 if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
//                 {
//                     pAdapter->Release();
//                     continue;
//                 }

//                 m_gpuDX = pAdapter4;
//                 return;
//             }
//             pAdapter->Release();
//         }

//         PE_ERROR_IF(!m_gpuDX, "No GPU found");
//     }

//     void RHI::CreateDeviceDX()
//     {
//         ID3D12Device8 *device = nullptr;
//         PE_CHECK(D3D12CreateDevice(m_gpuDX, c_featureLevel, IID_PPV_ARGS(&device)));
//         m_deviceDX = device;

//         PE_ERROR_IF(!m_deviceDX, "Failed to create device");
//     }
// }
// #endif
