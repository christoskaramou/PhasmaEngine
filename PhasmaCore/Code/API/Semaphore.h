#pragma once

namespace pe
{
    class Semaphore : public PeHandle<Semaphore, SemaphoreApiHandle>
    {
    public:
        Semaphore(bool timeline, const std::string &name);
        ~Semaphore();

        bool IsTimeline() { return m_timeline; }
        void Wait(uint64_t value);
        void Signal(uint64_t value);
        uint64_t GetValue();
        void SetStageFlags(PipelineStageFlags flags) { m_stageFlags = flags; }
        void AddStageFlags(PipelineStageFlags flags) { m_stageFlags |= flags; }
        PipelineStageFlags GetStageFlags() { return m_stageFlags; }

    private:
        const bool m_timeline;
        PipelineStageFlags m_stageFlags;
    };
}
