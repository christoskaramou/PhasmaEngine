#include "API/ImageView_Internal.h"
#include "API/RHI.h"
#if defined(PE_VULKAN)
#include "API/Vulkan/VulkanImageViewImpl.h"
#endif
#if defined(PE_WIN32)
#include "API/DX12/Dx12ImageViewImpl.h"
#endif
#if defined(PE_WEB)
#include "API/WebGPU/WebGpuImageImpl.h"
#endif

namespace pe
{
    ImageView::Impl *CreateImageViewImpl(ImageView *owner, const ImageViewDesc &desc)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            return new Dx12ImageViewImpl(owner, desc, Dx12ImageViewKind::Srv);
#endif
#if defined(PE_WEB)
        if (RHII.GetApi() == PE_GRAPHICS_API_WEBGPU)
            return new WebGpuImageViewImpl(owner, desc);
#endif
#if defined(PE_VULKAN)
        return new VulkanImageViewImpl(owner, desc);
#else
        PE_ERROR("%s: unsupported graphics api %u", __func__, static_cast<uint32_t>(RHII.GetApi()));
        return nullptr;
#endif
    }

    ImageView *ImageView::Create(Image *parent, const ImageViewDesc &desc, const std::string &name)
    {
        ImageView *view = new ImageView(parent, desc, name);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(ImageView), reinterpret_cast<void *>(view));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object ImageView created (Handle: %p)", reinterpret_cast<void *>(view));
#endif
#endif
        return view;
    }

    void ImageView::Destroy(ImageView *&view)
    {
        if (view)
        {
#if defined(PE_TRACK_RESOURCES) && !defined(PE_TRACK_RESOURCES_NOSPAM)
            PE_INFO("Object ImageView destroyed (Handle: %p)", reinterpret_cast<void *>(view));
#endif
            delete view;
#if defined(PE_TRACK_RESOURCES)
            PeTracker::Untrack(typeid(ImageView), reinterpret_cast<void *>(view));
#endif
            view = nullptr;
        }
    }

    std::vector<ImageView *> ImageView::GetHandles()
    {
#if defined(PE_TRACK_RESOURCES)
        auto ptrs = PeTracker::GetHandles(typeid(ImageView));
        std::vector<ImageView *> out;
        out.reserve(ptrs.size());
        for (void *p : ptrs)
            out.push_back(static_cast<ImageView *>(p));
        return out;
#else
        return {};
#endif
    }

    ImageView::ImageView(Image *parent, const ImageViewDesc &desc, const std::string &name)
        : m_parent{parent}, m_desc{desc}, m_name{name}
    {
        m_impl = CreateImageViewImpl(this, desc);
    }

    ImageView::ImageView(Image *parent, const ImageViewDesc &desc, const std::string &name, bool deferImpl)
        : m_parent{parent}, m_desc{desc}, m_name{name}
    {
        if (!deferImpl)
            m_impl = CreateImageViewImpl(this, desc);
    }

    ImageView::~ImageView()
    {
        delete m_impl;
    }
} // namespace pe
