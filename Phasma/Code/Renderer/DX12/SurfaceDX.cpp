// #if PE_DX12
// #include "Renderer/Surface.h"
// #include "Systems/RendererSystem.h"
// #include "Renderer/RHI.h"

// namespace pe
// {
//     Surface::Surface(SDL_Window *window)
//     {
//         IDXGIOutput6 *dxgiOutput6 = nullptr;

//         for (uint32_t i = 0;; i++)
//         {
//             IDXGIOutput *dxgiOutput = nullptr;
//             if ((RHII.GetGpu()->EnumOutputs(i, &dxgiOutput)) != DXGI_ERROR_NOT_FOUND)
//             {
//                 PE_CHECK(dxgiOutput->QueryInterface(IID_PPV_ARGS(&dxgiOutput6)));
//                 ReleaseDX(dxgiOutput);

//                 uint32_t numModes;
//                 dxgiOutput6->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);

//                 if (numModes <= 0)
//                 {
//                     ReleaseDX(dxgiOutput6);
//                     continue;
//                 }

//                 DXGI_OUTPUT_DESC1 outputDesc;
//                 dxgiOutput6->GetDesc1(&outputDesc);
//                 if (outputDesc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
//                 {
//                     ReleaseDX(dxgiOutput6);
//                     continue;
//                 }

//                 std::vector<DXGI_MODE_DESC1> desc(numModes);
//                 dxgiOutput6->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, desc.data());

//             }
//             else
//             {
//                 break;
//             }
//         }

//         int w, h;
//         SDL_Vulkan_GeaatDrawableSize(window, &w, &h);

//         actualExtent = Rect2D{0, 0, w, h};
//     }

//     Surface::~Surface()
//     {
//     }

//     void Surface::CheckTransfer()
//     {
//     }

//     void Surface::FindFormat()
//     {
//         uint32_t numModes;
//         m_handle->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);

//         std::vector<DXGI_MODE_DESC1> desc(numModes);
//         m_handle->GetDisplayModeList1(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, desc.data());

//         format = Translate<Format>(formats[0].Format);
//     }

//     void Surface::FindPresentationMode()
//     {
//         uint32_t presentModesCount;
//         vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_handle, &presentModesCount, nullptr);

//         std::vector<VkPresentModeKHR> presentModes(presentModesCount);
//         vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_handle, &presentModesCount, presentModes.data());

//         for (const auto &i : presentModes)
//             if (i == VK_PRESENT_MODE_MAILBOX_KHR)
//             {
//                 presentMode = Translate<PresentMode>(i);
//                 return;
//             }

//         for (const auto &i : presentModes)
//             if (i == VK_PRESENT_MODE_IMMEDIATE_KHR)
//             {
//                 presentMode = Translate<PresentMode>(i);
//                 return;
//             }

//         presentMode = Translate<PresentMode>(VK_PRESENT_MODE_FIFO_KHR);
//     }

//     void Surface::FindProperties()
//     {
//         CheckTransfer();
//         FindFormat();
//         FindPresentationMode();
//     }
// }
// #endif
