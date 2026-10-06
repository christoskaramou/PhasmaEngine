#include "Base/Timer_Internal.h"

#if defined(PE_WIN32)
#include <windows.h>
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
        m_frameStart = std::chrono::steady_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        m_delta = now - m_lastTime;
        m_lastTime = now;
        m_start = now;
    }

    void FrameTimer::EndFrame(uint32_t targetFps)
    {
        using Clock = std::chrono::steady_clock;
        if (SDL_GetKeyboardFocus() || targetFps == 0)
        {
            m_nextFrame = {};
            return;
        }

        const auto frameDuration = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / targetFps));
        if (m_nextFrame == Clock::time_point{} || frameDuration != m_frameDuration)
        {
            m_frameDuration = frameDuration;
            m_nextFrame = m_frameStart + frameDuration;
        }

        if (Clock::now() < m_nextFrame)
        {
#if defined(PE_WIN32)
            static std::unique_ptr<void, decltype(&CloseHandle)> timer(
                CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE), &CloseHandle);
            LARGE_INTEGER dueTime{};
            dueTime.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<std::chrono::duration<LONGLONG, std::ratio<1, 10000000>>>(m_nextFrame - Clock::now()).count());
            if (!timer || !SetWaitableTimer(timer.get(), &dueTime, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(timer.get(), INFINITE) != WAIT_OBJECT_0)
#endif
                std::this_thread::sleep_until(m_nextFrame);
        }

        // Carry the deadline forward so late wake-ups do not accumulate.
        m_nextFrame += frameDuration;
        const auto afterWait = Clock::now();
        if (m_nextFrame <= afterWait)
            m_nextFrame = afterWait + frameDuration;
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
        m_impl = CreateGpuTimerImpl(name);
        PE_ERROR_IF(!m_impl, "GpuTimer: backend returned null Impl from CreateGpuTimerImpl");
        m_apiHandle = m_impl.get();
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
