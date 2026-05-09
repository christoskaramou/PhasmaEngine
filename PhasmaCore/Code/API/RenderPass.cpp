#include "API/RenderPass_Internal.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanRenderPassImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12RenderPassImpl.h"
#endif

namespace pe
{
    RenderPass::Impl *CreateRenderPassImpl(RenderPass *owner, uint32_t count, Attachment *attachments, const std::string &name)
    {
        const PeGraphicsApi api = RHII.GetApi();
        switch (api)
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanRenderPassImpl(owner, count, attachments, name);
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            return new Dx12RenderPassImpl(owner, count, attachments, name);
#else
            PE_ERROR("CreateRenderPassImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        default:
            PE_ERROR("CreateRenderPassImpl: unsupported graphics api %u", static_cast<uint32_t>(api));
            return nullptr;
        }
    }

    RenderPass *RenderPass::Create(uint32_t count, Attachment *attachments, const std::string &name)
    {
        RenderPass *rp = new RenderPass(count, attachments, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(RenderPass), reinterpret_cast<void *>(rp));
#endif
        return rp;
    }

    void RenderPass::Destroy(RenderPass *&rp)
    {
        if (rp)
        {
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(RenderPass), reinterpret_cast<void *>(rp));
#endif
            delete rp;
            rp = nullptr;
        }
    }

    std::vector<RenderPass *> RenderPass::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(RenderPass));
        std::vector<RenderPass *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<RenderPass *>(p));
        return out;
#else
        return {};
#endif
    }

    RenderPass::RenderPass(uint32_t count, Attachment *attachments, const std::string &name)
        : m_name{name}
    {
        m_impl = CreateRenderPassImpl(this, count, attachments, name);
        PE_ERROR_IF(!m_impl, "RenderPass: backend returned null Impl from CreateRenderPassImpl");
    }

    RenderPass::~RenderPass()
    {
        delete m_impl;
        m_impl = nullptr;
    }
} // namespace pe
