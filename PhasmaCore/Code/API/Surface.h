#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Context;

    class Surface : public NoCopy
    {
    public:
        struct Impl;

        static Surface *Create(SDL_Window *window);
        static void Destroy(Surface *&s);
        static std::vector<Surface *> GetHandles();

        Surface(SDL_Window *window);
        ~Surface();

        void SetPresentMode(PePresentMode preferredMode);
        const Rect2Du &GetActualExtent() const { return m_actualExtent; }
        void SetActualExtent(const Rect2Du &extent) { m_actualExtent = extent; }
        ::PeFormat GetFormat() const { return m_format; }
        ::PeColorSpace GetColorSpace() const { return m_colorSpace; }
        PePresentMode GetPresentMode() const { return m_presentMode; }
        std::vector<PePresentMode> GetSupportedPresentModes() const;

    private:
        friend struct VulkanSurfaceImpl;
#if defined(PE_WIN32)
        friend class Dx12SwapchainImpl;
        friend struct Dx12SurfaceImpl;
#endif

        Impl *m_impl{};
        Rect2Du m_actualExtent{};
        ::PeFormat m_format{PE_FORMAT_UNDEFINED};
        ::PeColorSpace m_colorSpace{PE_COLOR_SPACE_SRGB_NONLINEAR};
        PePresentMode m_presentMode{PE_PRESENT_MODE_FIFO};
    };
} // namespace pe
