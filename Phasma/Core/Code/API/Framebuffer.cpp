#include "API/Framebuffer_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanFramebufferImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12FramebufferImpl.h"
#endif

namespace pe
{
    Framebuffer::Impl *CreateFramebufferImpl(Framebuffer *owner,
                                             uint32_t width,
                                             uint32_t height,
                                             const std::vector<ImageView *> &views,
                                             RenderPass *renderPass,
                                             const std::string &name)
    {
        const PeGraphicsApi api = RHII.GetApi();
        switch (api)
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanFramebufferImpl(owner, width, height, views, renderPass, name);
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            return new Dx12FramebufferImpl(owner, width, height, views, renderPass, name);
#else
            PE_ERROR("CreateFramebufferImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        default:
            PE_ERROR("CreateFramebufferImpl: unsupported graphics api %u", static_cast<uint32_t>(api));
            return nullptr;
        }
    }

    Framebuffer *Framebuffer::Create(uint32_t width,
                                     uint32_t height,
                                     const std::vector<ImageView *> &views,
                                     RenderPass *renderPass,
                                     const std::string &name)
    {
        Framebuffer *fb = new Framebuffer(width, height, views, renderPass, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(Framebuffer), reinterpret_cast<void *>(fb));
#endif
        return fb;
    }

    void Framebuffer::Destroy(Framebuffer *&fb)
    {
        if (fb)
        {
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(Framebuffer), reinterpret_cast<void *>(fb));
#endif
            delete fb;
            fb = nullptr;
        }
    }

    std::vector<Framebuffer *> Framebuffer::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(Framebuffer));
        std::vector<Framebuffer *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<Framebuffer *>(p));
        return out;
#else
        return {};
#endif
    }

    Framebuffer::Framebuffer(uint32_t width,
                             uint32_t height,
                             const std::vector<ImageView *> &views,
                             RenderPass *renderPass,
                             const std::string &name)
        : m_size{width, height},
          m_name{name}
    {
        m_impl = CreateFramebufferImpl(this, width, height, views, renderPass, name);
        PE_ERROR_IF(!m_impl, "Framebuffer: backend returned null Impl from CreateFramebufferImpl");
    }

    Framebuffer::~Framebuffer()
    {
        delete m_impl;
        m_impl = nullptr;
    }
} // namespace pe
