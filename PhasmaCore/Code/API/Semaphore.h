#pragma once

namespace pe
{
    class Semaphore : public PeHandle<Semaphore, vk::Semaphore>
    {
    public:
        Semaphore(bool timeline, const std::string &name);
        ~Semaphore();

        bool IsTimeline() { return m_timeline; }
        void Wait(uint64_t value);
        void Signal(uint64_t value);
        uint64_t GetValue();
        void SetStageFlags(vk::PipelineStageFlags2 flags) { m_stageFlags = flags; }
        void AddStageFlags(vk::PipelineStageFlags2 flags) { m_stageFlags |= flags; }
        vk::PipelineStageFlags2 GetStageFlags() { return m_stageFlags; }

    private:
        const bool m_timeline;
        vk::PipelineStageFlags2 m_stageFlags;
        uint64_t m_lastCompleted{0};
    };
} // namespace pe
