#include "API/Surface_Internal.h"
#include "API/RHI.h"
#if defined(PE_VULKAN)
#include "API/Vulkan/VulkanSurfaceImpl.h"
#endif
#if defined(PE_WIN32)
#include "API/DX12/Dx12SurfaceImpl.h"
#endif
#if defined(PE_WEB)
#include "API/WebGPU/WebGpuSwapchainImpl.h"
#endif

namespace pe
{
    Surface::Impl *CreateSurfaceImpl(Surface *owner, SDL_Window *window)
    {
        const PeGraphicsApi api = RHII.GetApi();
        switch (api)
        {
#if defined(PE_VULKAN)
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanSurfaceImpl(owner, window);
#endif
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            return new Dx12SurfaceImpl(owner, window);
#else
            PE_ERROR("CreateSurfaceImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
#if defined(PE_WEB)
        case PE_GRAPHICS_API_WEBGPU:
            return new WebGpuSurfaceImpl(owner, window);
#endif
        default:
            PE_ERROR("CreateSurfaceImpl: unsupported graphics api %u", static_cast<uint32_t>(api));
            return nullptr;
        }
    }

    Surface *Surface::Create(SDL_Window *window)
    {
        Surface *s = new Surface(window);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Surface), reinterpret_cast<void *>(s));
#endif
        return s;
    }

    void Surface::Destroy(Surface *&s)
    {
        if (s)
        {
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Surface), reinterpret_cast<void *>(s));
#endif
            delete s;
            s = nullptr;
        }
    }

    std::vector<Surface *> Surface::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Surface));
        std::vector<Surface *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Surface *>(p));
        return out;
#else
        return {};
#endif
    }

    Surface::Surface(SDL_Window *window)
    {
        m_impl = CreateSurfaceImpl(this, window);
        PE_ERROR_IF(!m_impl, "Surface: backend returned null Impl from CreateSurfaceImpl");
        SetPresentMode(PE_PRESENT_MODE_FIFO);
    }

    Surface::~Surface()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    std::vector<PePresentMode> Surface::GetSupportedPresentModes() const
    {
        return m_impl ? m_impl->GetSupportedPresentModes() : std::vector<PePresentMode>{};
    }

    bool Surface::IsPresentable() const
    {
        return m_impl ? m_impl->IsPresentable() : false;
    }

    void Surface::SetPresentMode(PePresentMode preferredMode)
    {
        const auto presentModes = GetSupportedPresentModes();
        for (const auto &presentMode : presentModes)
        {
            if (presentMode == preferredMode)
            {
                m_presentMode = presentMode;
                return;
            }
        }

        m_presentMode = PE_PRESENT_MODE_FIFO;
    }
} // namespace pe
