#pragma once

#include "Base/Timer_Internal.h"

namespace pe
{
    class VulkanGpuTimerImpl final : public GpuTimer::Impl
    {
    public:
        VulkanGpuTimerImpl(const std::string &name);
        ~VulkanGpuTimerImpl() override;

        void Start(CommandBuffer *cmd) override;
        void End() override;
        float GetTime() override;
        double GetStartTimeMs() const override;
        CommandBuffer *GetCommandBuffer() const override { return m_cmd; }
        void ResetState() override
        {
            m_inUse = false;
            m_resultsReady = false;
        }

        vk::QueryPool ApiHandle() const { return m_pool; }

    private:
        vk::QueryPool m_pool{};
        uint64_t m_queries[2]{};
        float m_timestampPeriod = 1.0f;
        CommandBuffer *m_cmd = nullptr;
        bool m_resultsReady = false;
        bool m_inUse = false;
    };
} // namespace pe
