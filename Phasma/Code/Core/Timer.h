#pragma once

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

namespace pe
{
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
        void Tick();

        double GetDelta() const;

        double CountTotal() const;

        void CountUpdatesStamp();
        void CountCpuTotalStamp();

        double GetUpdatesStamp() const { return m_updatesStamp; }
        double GetCpuTotal() const { return m_cpuTotalStamp; }

        static FrameTimer &Instance()
        {
            static FrameTimer frame_timer;
            return frame_timer;
        }

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
        std::chrono::high_resolution_clock::time_point m_total;
    };

    class CommandBuffer;

    class GpuTimer : public PeHandle<GpuTimer, QueryPoolApiHandle>
    {
    public:
        static GpuTimer *GetFree();

        static void Return(GpuTimer *gpuTimer);

        static void DestroyAll();

        GpuTimer(const std::string &name);

        ~GpuTimer();

        void Start(CommandBuffer *cmd);

        void End();

        float GetTime();

    private:
        uint64_t m_queries[2]{};
        float m_timestampPeriod;
        CommandBuffer *m_cmd;
        bool m_resultsReady;
        bool m_inUse;

        inline static std::stack<GpuTimer *> s_gpuTimers{};
    };
}
