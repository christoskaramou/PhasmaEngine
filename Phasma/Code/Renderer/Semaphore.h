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

        void SetWaitStageFlags(PipelineStageFlags flags) { m_waitStageFlags = flags; }

        void AddWaitStageFlags(PipelineStageFlags flags) { m_waitStageFlags |= flags; }

        PipelineStageFlags GetWaitStageFlags() { return m_waitStageFlags; }

    private:
        const bool m_timeline;
        PipelineStageFlags m_waitStageFlags;
    };
}