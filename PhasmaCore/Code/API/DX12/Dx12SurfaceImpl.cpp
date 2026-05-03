#include "API/DX12/Dx12SurfaceImpl.h"

#if defined(PE_WIN32)

#include "API/Surface.h"

namespace pe
{
    namespace
    {
        HWND HwndFromSdlWindow(SDL_Window *window)
        {
            if (!window)
                return nullptr;
            SDL_SysWMinfo info{};
            SDL_VERSION(&info.version);
            if (!SDL_GetWindowWMInfo(window, &info))
                return nullptr;
            return info.info.win.window;
        }
    } // namespace

    Dx12SurfaceImpl::Dx12SurfaceImpl(Surface *owner, SDL_Window *window)
        : m_owner{owner}
    {
        m_hwnd = HwndFromSdlWindow(window);
        PE_ERROR_IF(!m_hwnd, "Dx12SurfaceImpl: SDL window has no HWND");

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        m_owner->m_actualExtent = Rect2Du{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }

    std::vector<PePresentMode> Dx12SurfaceImpl::GetSupportedPresentModes() const
    {
        // DXGI tearing capability is probed at swapchain-creation time inside Dx12SwapchainImpl.
        // FIFO maps to vsync; IMMEDIATE maps to DXGI_PRESENT_ALLOW_TEARING when supported.
        return {PE_PRESENT_MODE_FIFO, PE_PRESENT_MODE_IMMEDIATE};
    }
} // namespace pe

#endif // PE_WIN32
