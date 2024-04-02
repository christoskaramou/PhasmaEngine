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
        m_updatesStamp = 0.0;
        m_cpuTotalStamp = 0.0;
    }

    void FrameTimer::Tick()
    {
        m_delta = std::chrono::high_resolution_clock::now() - m_start;
    }

    double FrameTimer::GetDelta() const
    {
        return m_delta.count();
    }

    double FrameTimer::CountTotal() const
    {
        const std::chrono::duration<double> t_duration = std::chrono::high_resolution_clock::now() - m_total;
        return t_duration.count();
    }

    void FrameTimer::CountUpdatesStamp()
    {
        m_updatesStamp = Count();
    }

    void FrameTimer::CountCpuTotalStamp()
    {
        m_cpuTotalStamp = Count();
    }

    GpuTimer::GpuTimer(const std::string &name)
    {
        m_inUse = false;
        m_cmd = nullptr;
        m_resultsReady = false;

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

        Debug::SetObjectName(m_handle, name);
    }

    GpuTimer::~GpuTimer()
    {
        if (m_handle)
            vkDestroyQueryPool(RHII.GetDevice(), m_handle, nullptr);
    }

    void GpuTimer::Start(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::Start() called before End()");

        m_cmd = cmd;
        vkCmdResetQueryPool(m_cmd->Handle(), m_handle, 0, 2);
        vkCmdWriteTimestamp(m_cmd->Handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_handle, 0);
        m_resultsReady = false;
        m_inUse = true;
    }

    void GpuTimer::End()
    {
        PE_ERROR_IF(!m_inUse, "GpuTimer::End() called before Start()");

        vkCmdWriteTimestamp(m_cmd->Handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_handle, 1);
        m_resultsReady = false;
        m_inUse = false;
    }

    float GpuTimer::GetTime()
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::GetTime() called before End()");

        if (m_resultsReady)
            return static_cast<float>(m_queries[1] - m_queries[0]) * m_timestampPeriod * 1e-6f; // ms

        VkResult res = vkGetQueryPoolResults(RHII.GetDevice(),
                                             m_handle,
                                             0,
                                             2,
                                             2 * sizeof(uint64_t),
                                             &m_queries,
                                             sizeof(uint64_t),
                                             VK_QUERY_RESULT_64_BIT);

        m_cmd = nullptr;
        m_resultsReady = true;

        if (res != VK_SUCCESS)
            return 0.f;

        return static_cast<float>(m_queries[1] - m_queries[0]) * m_timestampPeriod * 1e-6f; // ms
    }

    GpuTimer *GpuTimer::GetFree()
    {
        if (s_gpuTimers.empty())
        {
            for (int i = 0; i < 10; ++i)
                s_gpuTimers.push(GpuTimer::Create("gpu timer_" + std::to_string(s_gpuTimers.size())));
        }

        GpuTimer *gpuTimer = s_gpuTimers.top();
        s_gpuTimers.pop();

        return gpuTimer;
    }

    void GpuTimer::Return(GpuTimer *gpuTimer)
    {
        if (gpuTimer && gpuTimer->Handle())
            s_gpuTimers.push(gpuTimer);
    }

    void GpuTimer::DestroyAll()
    {
        while (!s_gpuTimers.empty())
        {
            GpuTimer::Destroy(s_gpuTimers.top());
            s_gpuTimers.pop();
        }
    }
}
