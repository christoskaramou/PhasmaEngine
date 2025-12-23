#include "Base/Timer.h"
#include "API/Command.h"
#include "API/RHI.h"

namespace pe
{
    Timer::Timer()
        : m_start{},
          m_system_delay{0}
    {
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

    FrameTimer::FrameTimer()
        : Timer(),
          m_updatesStamp{0.0},
          m_cpuTotalStamp{0.0},
          m_delta{}
    {
    }

    void FrameTimer::CountDeltaTime()
    {
        m_delta = std::chrono::high_resolution_clock::now() - m_start;
    }

    double FrameTimer::GetDelta() const
    {
        return m_delta.count();
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
        : m_queries{},
          m_cmd{nullptr},
          m_resultsReady{false},
          m_inUse{false}
    {
        VkPhysicalDeviceProperties gpuProps;
        vkGetPhysicalDeviceProperties(RHII.GetGpu(), &gpuProps);
        PE_ERROR_IF(!gpuProps.limits.timestampComputeAndGraphics, "Timestamps not supported");

        m_timestampPeriod = gpuProps.limits.timestampPeriod;

        VkQueryPoolCreateInfo qpci{};
        qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 2;

        VkQueryPool pool;
        PE_CHECK(vkCreateQueryPool(RHII.GetDevice(), &qpci, nullptr, &pool));
        m_apiHandle = pool;

        Debug::SetObjectName(m_apiHandle, name);
    }

    GpuTimer::~GpuTimer()
    {
        if (m_apiHandle)
            vkDestroyQueryPool(RHII.GetDevice(), m_apiHandle, nullptr);
    }

    void GpuTimer::Start(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::Start() called before End()");

        m_cmd = cmd;
        vkCmdResetQueryPool(m_cmd->ApiHandle(), m_apiHandle, 0, 2);
        vkCmdWriteTimestamp(m_cmd->ApiHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_apiHandle, 0);
        m_resultsReady = false;
        m_inUse = true;
    }

    void GpuTimer::End()
    {
        PE_ERROR_IF(!m_inUse, "GpuTimer::End() called before Start()");

        vkCmdWriteTimestamp(m_cmd->ApiHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_apiHandle, 1);
        m_resultsReady = false;
        m_inUse = false;
    }

    float GpuTimer::GetTime()
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::GetTime() called before End()");

        if (!m_resultsReady)
        {
            VkResult res = vkGetQueryPoolResults(RHII.GetDevice(),
                                                 m_apiHandle,
                                                 0,
                                                 2,
                                                 2 * sizeof(uint64_t),
                                                 &m_queries,
                                                 sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);

            if (res != VK_SUCCESS)
                return 0.f;

            m_cmd = nullptr;
            m_resultsReady = true;
        }

        return static_cast<float>(m_queries[1] - m_queries[0]) * m_timestampPeriod * 1e-6f; // ms
    }
} // namespace pe
