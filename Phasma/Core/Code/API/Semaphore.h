#pragma once

namespace pe
{
    class Semaphore : public PeHandle<Semaphore, PeBackendHandle>
    {
    public:
        struct Impl;

        Semaphore(bool timeline, const std::string &name);
        ~Semaphore();

        bool IsTimeline() { return m_timeline; }
        void Wait(uint64_t value);
        bool WaitTimeout(uint64_t value, uint64_t timeoutNS);
        void Signal(uint64_t value);
        uint64_t GetValue();
        void SetStageFlags(PeBarrierSync flags) { m_stageFlags = flags; }
        void AddStageFlags(PeBarrierSync flags) { m_stageFlags |= flags; }
        PeBarrierSync GetStageFlags() const { return m_stageFlags; }

    private:
        friend struct VulkanSemaphoreImpl;
#if defined(PE_WIN32)
        friend struct Dx12SemaphoreImpl;
#endif

        Impl *m_impl{};
        const bool m_timeline;
        PeBarrierSync m_stageFlags;
    };
} // namespace pe
