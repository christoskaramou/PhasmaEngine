#pragma once

namespace pe
{
    class Context;

    class Surface : public PeHandle<Surface, vk::SurfaceKHR>
    {
    public:
        Surface(SDL_Window *window);
        ~Surface();

        void CheckTransfer();
        void FindFormat();
        void SetPresentMode(vk::PresentModeKHR preferredMode);
        void FindProperties();
        const Rect2Du &GetActualExtent() const { return m_actualExtent; }
        void SetActualExtent(const Rect2Du &extent) { m_actualExtent = extent; }
        vk::Format GetFormat() const { return m_format; }
        vk::ColorSpaceKHR GetColorSpace() const { return m_colorSpace; }
        vk::PresentModeKHR GetPresentMode() const { return m_presentMode; }
        const std::vector<vk::PresentModeKHR> &GetSupportedPresentModes() const { return m_supportedPresentModes; }

private:
        bool SupportsPresentMode(vk::PresentModeKHR mode);

        Rect2Du m_actualExtent;
        vk::Format m_format;
        vk::ColorSpaceKHR m_colorSpace;
        vk::PresentModeKHR m_presentMode;
        std::vector<vk::PresentModeKHR> m_supportedPresentModes;
    };
}
