#pragma once

namespace pe
{    
    class Surface;
    class Semaphore;
    class Image;

    class Swapchain : public IHandle<Swapchain, SwapchainHandle>
    {
    public:
        Swapchain(Surface *surface, const std::string &name);

        ~Swapchain();

        uint32_t Aquire(Semaphore *semaphore);

        std::vector<Image *> images;
    };
}