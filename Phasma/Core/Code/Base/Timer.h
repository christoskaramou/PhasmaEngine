#pragma once

namespace pe
{
    constexpr double MILLI(double seconds)
    {
        return seconds * 1000.0;
    }

    constexpr double MICRO(double seconds)
    {
        return seconds * 1000000.0;
    }

    constexpr double NANO(double seconds)
    {
        return seconds * 1000000000.0;
    }

    class Timer
    {
    public:
        Timer();

        void Start();
        double Count();
        void ThreadSleep(double seconds);

    protected:
        std::chrono::high_resolution_clock::time_point m_start;
        size_t m_system_delay;
    };

    class FrameTimer : public Timer
    {
    public:
        double CountTotal() const;
        void CountUpdatesStamp();
        void CountCpuTotalStamp();
        void CountDeltaTime();
        void Tick();
        double GetUpdatesStamp() const { return m_updatesStamp; }
        double GetCpuTotal() const { return m_cpuTotalStamp; }
        double GetDelta() const;

        static FrameTimer &Instance();

        FrameTimer(FrameTimer const &) = delete;            // copy constructor
        FrameTimer(FrameTimer &&) noexcept = delete;        // move constructor
        FrameTimer &operator=(FrameTimer const &) = delete; // copy assignment
        FrameTimer &operator=(FrameTimer &&) = delete;      // move assignment

    private:
        FrameTimer();            // default constructor
        ~FrameTimer() = default; // destructor

        double m_updatesStamp;
        double m_cpuTotalStamp;
        std::chrono::duration<double> m_delta{};
        std::chrono::high_resolution_clock::time_point m_lastTime;
    };

    class GpuTimer;
    struct GpuTimerInfo
    {
        GpuTimer *timer;
        std::string name;
        size_t depth;
    };

    struct GpuTimerSample
    {
        std::string name;
        size_t depth = 0;
        float timeMs = 0.0f;
        float startOffsetMs = 0.0f; // offset from frame start in ms (for timeline display)
    };

    class CommandBuffer;
    class GpuTimer : public PeHandle<GpuTimer, void *>
    {
    public:
        class Impl;

        GpuTimer(const std::string &name);
        ~GpuTimer();

        void Start(CommandBuffer *cmd);
        void End();
        float GetTime();
        double GetStartTimeMs() const; // absolute start timestamp in ms (valid after GetTime())
        CommandBuffer *GetCommandBuffer() const;
        // Discard in-flight Start() if the parent command buffer was reset.
        void ResetState();

    private:
        std::unique_ptr<Impl> m_impl;

        inline static std::stack<GpuTimer *> s_gpuTimers{};
    };
} // namespace pe
