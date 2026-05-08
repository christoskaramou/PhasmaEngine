#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class CommandBuffer;

    struct EventSetInfoImage
    {
        Image *image;
        PeImageLayout oldLayout;
        PeImageLayout newLayout;
        PeBarrierSync srcStage;
        PeBarrierAccess srcAccess;
        PeBarrierSync dstStage;
        PeBarrierAccess dstAccess;
    };

    class Event : public PeHandle<Event, PeBackendHandle>
    {
    public:
        struct Impl;

        Event(const std::string &name);
        ~Event();

        void Set(CommandBuffer *cmd, Image *image,
                 PeImageLayout srcLayout, PeImageLayout dstLayout,
                 PeBarrierSync srcStage, PeBarrierSync dstStage,
                 PeBarrierAccess srcAccess, PeBarrierAccess dstAccess);
        void Wait();
        void Reset(PeBarrierSync resetStage);
        bool IsSet() const { return m_set; }

    private:
        friend struct VulkanEventImpl;

        Impl *m_impl{};
        CommandBuffer *m_cmd = nullptr;
        EventSetInfoImage m_infoImage{};
        bool m_set = false;
    };
} // namespace pe
