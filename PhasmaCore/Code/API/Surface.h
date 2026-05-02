#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Context;

    class Surface : public PeHandle<Surface, vk::SurfaceKHR>
    {
    public:
        Surface(SDL_Window *window);
        ~Surface();

        void SetPresentMode(PePresentMode preferredMode);
        const Rect2Du &GetActualExtent() const { return m_actualExtent; }
        void SetActualExtent(const Rect2Du &extent) { m_actualExtent = extent; }
        vk::Format GetFormat() const { return m_format; }
        vk::ColorSpaceKHR GetColorSpace() const { return m_colorSpace; }
        PePresentMode GetPresentMode() const { return m_presentMode; }
        std::vector<PePresentMode> GetSupportedPresentModes() const;

    private:
        Rect2Du m_actualExtent;
        vk::Format m_format;
        vk::ColorSpaceKHR m_colorSpace;
        PePresentMode m_presentMode;
    };
} // namespace pe
