#include "API/Swapchain_Internal.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanSwapchainImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12SwapchainImpl.h"
#endif

namespace pe
{
    Swapchain::Impl *CreateSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12SwapchainImpl(owner, desc);
#else
            PE_ERROR("CreateSwapchainImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        }
        return new VulkanSwapchainImpl(owner, desc);
    }

    Swapchain *Swapchain::Create(const SwapchainDesc &desc)
    {
        Swapchain *sc = new Swapchain(desc);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Swapchain), reinterpret_cast<void *>(sc));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object Swapchain created (Handle: %p)", reinterpret_cast<void *>(sc));
#endif
#endif
        return sc;
    }

    void Swapchain::Destroy(Swapchain *&sc)
    {
        if (sc)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object Swapchain destroyed (Handle: %p)", reinterpret_cast<void *>(sc));
#endif
            delete sc;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Swapchain), reinterpret_cast<void *>(sc));
#endif
            sc = nullptr;
        }
    }

    std::vector<Swapchain *> Swapchain::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Swapchain));
        std::vector<Swapchain *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Swapchain *>(p));
        return out;
#else
        return {};
#endif
    }

    Swapchain::Swapchain(const SwapchainDesc &desc)
        : m_presentMode{desc.presentMode},
          m_width{desc.width},
          m_height{desc.height},
          m_name{desc.name}
    {
        m_impl = CreateSwapchainImpl(this, desc);
    }

    Swapchain::~Swapchain()
    {
        for (Image *image : m_images)
        {
            if (image)
                Image::Destroy(image);
        }
        m_images.clear();
        delete m_impl;
        m_impl = nullptr;
    }

    uint32_t Swapchain::AquireNextImage(Semaphore *semaphore)
    {
        return m_impl->AquireNextImage(semaphore);
    }
} // namespace pe
