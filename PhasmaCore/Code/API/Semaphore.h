#pragma once

namespace pe
{
    class Semaphore : public PeHandle<Semaphore, vk::Semaphore>
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
        void SetStageFlags(vk::PipelineStageFlags2 flags) { m_stageFlags = flags; }
        void AddStageFlags(vk::PipelineStageFlags2 flags) { m_stageFlags |= flags; }
        vk::PipelineStageFlags2 GetStageFlags() { return m_stageFlags; }

    private:
        friend struct VulkanSemaphoreImpl;
#if defined(PE_WIN32)
        friend struct Dx12SemaphoreImpl;
#endif

        Impl *m_impl{};
        const bool m_timeline;
        vk::PipelineStageFlags2 m_stageFlags;
    };
} // namespace pe
