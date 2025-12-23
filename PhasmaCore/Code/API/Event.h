#pragma once

namespace pe
{
    class CommandBuffer;

    struct EventSetInfoImage
    {
        Image *image;
        vk::ImageLayout oldLayout;
        vk::ImageLayout newLayout;
        vk::PipelineStageFlags2 srcStage;
        vk::AccessFlags2 srcAccess;
        vk::PipelineStageFlags2 dstStage;
        vk::AccessFlags2 dstAccess;
    };

    class Event : public PeHandle<Event, vk::Event>
    {
    public:
        Event(const std::string &name);
        ~Event();

        void Set(CommandBuffer *cmd, Image *image,
                 vk::ImageLayout srcLayout, vk::ImageLayout dstLayout,
                 vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                 vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess);
        void Wait();
        void Reset(vk::PipelineStageFlags2 resetStage);
        bool IsSet() const { return m_set; }

    private:
        CommandBuffer *m_cmd;
        EventSetInfoImage m_infoImage{};
        bool m_set;
    };
} // namespace pe
