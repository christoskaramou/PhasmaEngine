#include "API/DX12/Dx12SwapchainImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/RHI.h"
#include "API/Surface.h"

#include "SDL2/SDL_syswm.h"

namespace pe
{
    using Microsoft::WRL::ComPtr;

    static HWND HwndFromSdlWindow(SDL_Window *window)
    {
        if (!window)
            return nullptr;
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info))
            return nullptr;
        return info.info.win.window;
    }

    Dx12SwapchainImpl::Dx12SwapchainImpl(Swapchain *owner, const SwapchainDesc &desc)
        : m_owner{owner}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi, "Dx12SwapchainImpl requires DX12 RHI to be initialized");

        IDXGIFactory6 *factory = rhi->GetFactory();
        ID3D12Device *device = rhi->GetDevice();
        ID3D12CommandQueue *queue = rhi->GetGraphicsQueue();
        PE_ERROR_IF(!factory || !device || !queue, "Dx12SwapchainImpl: missing factory/device/queue");

        HWND hwnd = HwndFromSdlWindow(desc.window);
        PE_ERROR_IF(!hwnd, "Dx12SwapchainImpl: SDL window has no HWND");

        m_allowTearing = rhi->SupportsAllowTearing();
        const bool useImmediate = m_owner->m_presentMode == PE_PRESENT_MODE_IMMEDIATE && m_allowTearing;
        m_owner->m_presentMode = useImmediate ? PE_PRESENT_MODE_IMMEDIATE : PE_PRESENT_MODE_FIFO;

        const uint32_t bbCount = desc.backbufferCount < 2 ? 2 : desc.backbufferCount;
        m_format = desc.surface ? pe_dx12::Format(desc.surface->GetFormat()) : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (m_format == DXGI_FORMAT_UNKNOWN)
        {
            PE_WARN("Dx12SwapchainImpl: unsupported surface format %u; falling back to R8G8B8A8_UNORM",
                    desc.surface ? static_cast<uint32_t>(desc.surface->GetFormat())
                                 : static_cast<uint32_t>(PE_FORMAT_UNDEFINED));
            m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        DXGI_SWAP_CHAIN_DESC1 sc{};
        sc.Width = desc.width;
        sc.Height = desc.height;
        sc.Format = m_format;
        sc.Stereo = FALSE;
        sc.SampleDesc.Count = 1;
        sc.SampleDesc.Quality = 0;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount = bbCount;
        sc.Scaling = DXGI_SCALING_STRETCH;
        sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> sc1;
        HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sc, nullptr, nullptr, &sc1);
        PE_ERROR_IF(FAILED(hr), "Dx12SwapchainImpl: CreateSwapChainForHwnd failed");

        // Disable Alt+Enter fullscreen toggle; the engine drives present mode explicitly.
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        hr = sc1.As(&m_swapchain);
        PE_ERROR_IF(FAILED(hr), "Dx12SwapchainImpl: IDXGISwapChain3 not available");

        DXGI_SWAP_CHAIN_DESC1 actualDesc{};
        if (SUCCEEDED(m_swapchain->GetDesc1(&actualDesc)) && actualDesc.Format != DXGI_FORMAT_UNKNOWN)
            m_format = actualDesc.Format;
        const ::PeFormat imageFormat = pe_dx12::FromFormat(m_format);
        PE_ERROR_IF(imageFormat == PE_FORMAT_UNDEFINED,
                    "Dx12SwapchainImpl: unsupported swapchain format %u",
                    static_cast<uint32_t>(m_format));
        if (desc.surface)
            desc.surface->m_format = imageFormat;

        m_currentBackbuffer = m_swapchain->GetCurrentBackBufferIndex();

        m_backbuffers.resize(bbCount);
        m_rtvHandles.resize(bbCount);

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = bbCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;
        hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
        PE_ERROR_IF(FAILED(hr), "Dx12SwapchainImpl: CreateDescriptorHeap(RTV) failed");

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const uint32_t rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (uint32_t i = 0; i < bbCount; ++i)
        {
            hr = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i]));
            PE_ERROR_IF(FAILED(hr), "Dx12SwapchainImpl: GetBuffer failed");
            m_rtvHandles[i] = rtv;
            device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, rtv);
            rtv.ptr += rtvStride;
        }

        uint32_t width = desc.width;
        uint32_t height = desc.height;
        if (!m_backbuffers.empty() && m_backbuffers[0])
        {
            const D3D12_RESOURCE_DESC backbufferDesc = m_backbuffers[0]->GetDesc();
            width = static_cast<uint32_t>(backbufferDesc.Width);
            height = static_cast<uint32_t>(backbufferDesc.Height);
        }
        PE_ERROR_IF(width == 0 || height == 0, "Dx12SwapchainImpl: invalid backbuffer extent %ux%u", width, height);

        m_owner->m_width = width;
        m_owner->m_height = height;
        m_owner->m_images.resize(bbCount);
        for (uint32_t i = 0; i < bbCount; ++i)
        {
            Image *img = new Image();
            img->m_impl = CreateDx12SwapchainImageImpl(img, m_backbuffers[i].Get(), m_format);
            img->m_width = width;
            img->m_height = height;
            img->m_depth = 1;
            img->m_mipLevels = 1;
            img->m_arrayLayers = 1;
            img->m_format = imageFormat;
            img->m_usage = PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_TRANSFER_DST;
            img->m_samples = PE_SAMPLE_COUNT_1;
            img->m_imageType = PE_IMAGE_TYPE_2D;
            img->m_name = "Swapchain_image_" + std::to_string(i);
            img->m_trackInfos.resize(1);

            ImageTrackInfo info{};
            info.image = img;
            info.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
            info.stageFlags = PE_STAGE_ALL_COMMANDS;
            info.accessMask = PE_ACCESS_NONE;
            img->m_trackInfos[0].resize(1, info);
            m_owner->m_images[i] = img;

            ImageViewDesc viewDesc{};
            viewDesc.viewType = PE_IMAGE_VIEW_TYPE_2D;
            viewDesc.format = img->GetFormat();
            viewDesc.aspectMask = PE_IMAGE_ASPECT_COLOR;
            viewDesc.baseMipLevel = 0;
            viewDesc.levelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.layerCount = 1;
            img->SetRTV(Dx12ImageViewImpl::Create(img,
                                                  viewDesc,
                                                  Dx12ImageViewKind::Rtv,
                                                  "Swapchain_image_view" + std::to_string(i)));
        }
    }

    Dx12SwapchainImpl::~Dx12SwapchainImpl()
    {
        for (Image *image : m_owner->m_images)
            Image::Destroy(image);
        m_owner->m_images.clear();
        m_backbuffers.clear();
        m_rtvHandles.clear();
        m_rtvHeap.Reset();
        m_swapchain.Reset();
    }

    uint32_t Dx12SwapchainImpl::AquireNextImage(Semaphore * /*semaphore*/)
    {
        m_currentBackbuffer = m_swapchain->GetCurrentBackBufferIndex();
        return m_currentBackbuffer;
    }

    void Dx12SwapchainImpl::Present()
    {
        const bool immediate = m_owner->GetPresentMode() == PE_PRESENT_MODE_IMMEDIATE && m_allowTearing;
        const UINT syncInterval = immediate ? 0u : 1u;
        const UINT flags = immediate ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        const HRESULT hr = m_swapchain->Present(syncInterval, flags);
        PE_ERROR_IF(FAILED(hr), "Dx12SwapchainImpl: Present failed (0x%08X)", static_cast<unsigned>(hr));
        m_currentBackbuffer = m_swapchain->GetCurrentBackBufferIndex();
    }
} // namespace pe
