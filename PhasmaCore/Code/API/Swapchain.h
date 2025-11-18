#pragma once

namespace pe
{
    class Surface;
    class Semaphore;
    class Image;

    class Swapchain : public PeHandle<Swapchain, vk::SwapchainKHR>
    {
    public:
        Swapchain(Surface *surface, const std::string &name);
        ~Swapchain();

        uint32_t AquireNextImage(Semaphore *semaphore);
        Image *GetImage(uint32_t index) { return m_images[index]; }
        uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }

    private:
        std::vector<Image *> m_images;
        Rect2Du m_extent;
    };
}
