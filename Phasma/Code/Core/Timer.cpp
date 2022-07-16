#include "Core/Timer.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"

namespace pe
{
    Timer::Timer()
    {
        m_start = {};
        m_system_delay = 0;
    }

    void Timer::Start()
    {
        m_start = std::chrono::high_resolution_clock::now();
    }

    double Timer::Count()
    {
        const std::chrono::duration<double> t_duration = std::chrono::high_resolution_clock::now() - m_start;
        return t_duration.count();
    }

    void Timer::ThreadSleep(double seconds)
    {
        if (seconds <= 0.0f)
            return;

        const std::chrono::nanoseconds delay{(static_cast<size_t>(NANO(seconds)) - m_system_delay)};

        Timer timer;
        timer.Start();
        std::this_thread::sleep_for(delay);
        m_system_delay = static_cast<size_t>(NANO(timer.Count())) - delay.count();
    }

    FrameTimer::FrameTimer() : Timer()
    {
        m_total = std::chrono::high_resolution_clock::now();
        m_delta = {};
    }

    void FrameTimer::Tick()
    {
        m_delta = std::chrono::high_resolution_clock::now() - m_start;
    }

    double FrameTimer::GetDelta()
    {
        return m_delta.count();
    }

    
    double FrameTimer::CountTotal()
    {
        const std::chrono::duration<double> t_duration = std::chrono::high_resolution_clock::now() - m_total;
        return t_duration.count();
    }

    GpuTimer::GpuTimer(const std::string &name)
    {
        m_cmd = nullptr;

        VkPhysicalDeviceProperties gpuProps;
        vkGetPhysicalDeviceProperties(RHII.GetGpu(), &gpuProps);

        if (!gpuProps.limits.timestampComputeAndGraphics)
            PE_ERROR("Timestamps not supported");

        m_timestampPeriod = gpuProps.limits.timestampPeriod;

        VkQueryPoolCreateInfo qpci{};
        qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 2;

        VkQueryPool pool;
        PE_CHECK(vkCreateQueryPool(RHII.GetDevice(), &qpci, nullptr, &pool));
        m_handle = pool;

        Debug::SetObjectName(m_handle, ObjectType::QueryPool, name);
    }

    GpuTimer::~GpuTimer()
    {
        if (m_handle)
            vkDestroyQueryPool(RHII.GetDevice(), m_handle, nullptr);
    }

    void GpuTimer::Reset()
    {
        vkCmdResetQueryPool(m_cmd->Handle(), m_handle, 0, 2);
    }

    void GpuTimer::Start(CommandBuffer *cmd)
    {
        m_cmd = cmd;
        Reset();
        vkCmdWriteTimestamp(m_cmd->Handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_handle, 0);
    }

    float GpuTimer::End()
    {
        vkCmdWriteTimestamp(m_cmd->Handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_handle, 1);
        m_cmd = nullptr;
        return GetTime();
    }

    float GpuTimer::GetTime()
    {
        VkResult res = vkGetQueryPoolResults(RHII.GetDevice(),
                                             m_handle,
                                             0,
                                             2,
                                             2 * sizeof(uint64_t),
                                             &m_queries,
                                             sizeof(uint64_t),
                                             VK_QUERY_RESULT_64_BIT);
        if (res != VK_SUCCESS)
            return 0.f;

        return static_cast<float>(m_queries[1] - m_queries[0]) * m_timestampPeriod * 1e-6f; // ms
    }
}
