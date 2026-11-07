#pragma once

#include "API/Surface_Internal.h"
#include "API/Swapchain_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // The page's <canvas id="canvas"> (the SDL Emscripten canvas). The browser presents it when the frame callback
    // returns (spec decision 4), so FIFO is the only present mode.
    struct WebGpuSurfaceImpl final : public Surface::Impl
    {
        WebGpuSurfaceImpl(Surface *owner, SDL_Window *window);
        ~WebGpuSurfaceImpl() override;

        static WebGpuSurfaceImpl *From(Surface *surface) { return static_cast<WebGpuSurfaceImpl *>(surface->m_impl); }

        std::vector<PePresentMode> GetSupportedPresentModes() const override { return {PE_PRESENT_MODE_FIFO}; }
        bool IsPresentable() const override;

        Surface *m_owner = nullptr;
        SDL_Window *m_window = nullptr;
        WGPUSurface m_surface = nullptr;
        WGPUTextureFormat m_format = WGPUTextureFormat_Undefined;
    };

    // Reports two engine images so per-frame rings keep working (decision 5). Each acquire hands the canvas's current
    // texture to the next image in turn and rebuilds that image's render-target view.
    struct WebGpuSwapchainImpl final : public Swapchain::Impl
    {
        static constexpr uint32_t kImageCount = 2;

        WebGpuSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc);
        ~WebGpuSwapchainImpl() override;

        uint32_t AquireNextImage(Semaphore *semaphore) override;
        bool WaitForNextFrame() override; // the rAF callback is the frame wait; only checks for a canvas resize
        void Present() override {}        // the browser presents when the frame callback returns

        Swapchain *m_owner = nullptr;
        Surface *m_surface = nullptr;
        uint32_t m_next = 0;
    };
} // namespace pe
