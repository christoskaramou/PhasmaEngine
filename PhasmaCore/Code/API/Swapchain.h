#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Image;
    class Semaphore;
    class Surface;

    struct SwapchainDesc
    {
        SDL_Window *window = nullptr;
        Surface *surface = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        PePresentMode presentMode = PE_PRESENT_MODE_FIFO;
        uint32_t backbufferCount = 2;
        std::string name;
    };

    class Swapchain : public NoCopy
    {
    public:
        struct Impl;

        static Swapchain *Create(const SwapchainDesc &desc);
        static void Destroy(Swapchain *&sc);
        static std::vector<Swapchain *> GetHandles();

        uint32_t AquireNextImage(Semaphore *semaphore);
        Image *GetImage(uint32_t index) { return m_images[index]; }
        uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }
        PePresentMode GetPresentMode() const { return m_presentMode; }
        const std::string &GetName() const { return m_name; }
        uint32_t GetWidth() const { return m_width; }
        uint32_t GetHeight() const { return m_height; }

    private:
        friend struct VulkanSwapchainImpl;
#if defined(PE_WIN32)
        friend class Dx12SwapchainImpl;
#endif

        Swapchain(const SwapchainDesc &desc);
        ~Swapchain();

        Impl *m_impl{};
        std::vector<Image *> m_images;
        PePresentMode m_presentMode{PE_PRESENT_MODE_FIFO};
        uint32_t m_width{};
        uint32_t m_height{};
        std::string m_name;
    };
} // namespace pe
