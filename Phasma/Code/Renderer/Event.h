#pragma once

namespace pe
{
    class CommandBuffer;

    struct EventSetInfoImage
    {
        Image *image;
        ImageLayout oldLayout;
        ImageLayout newLayout;
        PipelineStageFlags srcStage;
        AccessFlags srcAccess;
        PipelineStageFlags dstStage;
        AccessFlags dstAccess;
    };

    class Event : public IHandle<Event, EventHandle>
    {
    public:
        Event(const std::string &name);

        ~Event();

        void Set(CommandBuffer *cmd, Image *image,
                 ImageLayout scrLayout, ImageLayout dstLayout,
                 PipelineStageFlags srcStage, PipelineStageFlags dstStage,
                 AccessFlags srcAccess, AccessFlags dstAccess);

        void Wait();

        void Reset(PipelineStageFlags resetStage);

        bool IsSet() const { return m_set; }

    private:
        CommandBuffer *m_cmd;
        EventSetInfoImage m_infoImage{};
        bool m_set;
    };
}