#include "Base/Timer.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanGpuTimerImpl.h"
#include "Base/Timer_Internal.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12GpuTimerImpl.h"
#endif

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

    FrameTimer &FrameTimer::Instance()
    {
        static FrameTimer frame_timer;
        return frame_timer;
    }

    FrameTimer::FrameTimer()
        : Timer(),
          m_updatesStamp{0.0},
          m_cpuTotalStamp{0.0},
          m_delta{}
    {
        m_lastTime = std::chrono::high_resolution_clock::now();
    }

    void FrameTimer::CountDeltaTime()
    {
        m_delta = std::chrono::high_resolution_clock::now() - m_start;
    }

    void FrameTimer::Tick()
    {
        auto now = std::chrono::high_resolution_clock::now();
        m_delta = now - m_lastTime;
        m_lastTime = now;
        m_start = now;
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
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            auto vk = std::make_unique<VulkanGpuTimerImpl>(name);
            m_apiHandle = reinterpret_cast<void *>(static_cast<VkQueryPool>(vk->ApiHandle()));
            m_impl = std::move(vk);
            break;
        }
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
        {
            auto dx = std::make_unique<Dx12GpuTimerImpl>(name);
            m_apiHandle = dx.get();
            m_impl = std::move(dx);
            break;
        }
#endif
        default:
            PE_ERROR("GpuTimer: unsupported graphics API");
        }
    }

    GpuTimer::~GpuTimer() = default;

    void GpuTimer::Start(CommandBuffer *cmd)
    {
        m_impl->Start(cmd);
    }
    void GpuTimer::End()
    {
        m_impl->End();
    }
    float GpuTimer::GetTime()
    {
        return m_impl->GetTime();
    }
    double GpuTimer::GetStartTimeMs() const
    {
        return m_impl->GetStartTimeMs();
    }
    CommandBuffer *GpuTimer::GetCommandBuffer() const
    {
        return m_impl->GetCommandBuffer();
    }
    void GpuTimer::ResetState()
    {
        m_impl->ResetState();
    }
} // namespace pe
