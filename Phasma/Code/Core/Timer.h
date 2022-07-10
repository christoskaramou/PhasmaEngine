/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

        double GetDelta();

        double CountTotal();

        double cpuStamp;
        double gpuStamp;
        double shadowStamp[SHADOWMAP_CASCADES];
        double computeStamp;
        double geometryStamp;
        double ssaoStamp;
        double ssrStamp;
        double compositionStamp;
        double fxaaStamp;
        double bloomStamp;
        double ssgiStamp;
        double dofStamp;
        double motionBlurStamp;
        double guiStamp;
        double fsrStamp;

    private:
        std::chrono::duration<double> m_delta{};

    public:
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

        std::chrono::high_resolution_clock::time_point m_total;
    };

    class CommandBuffer;

    class GpuTimer : public IHandle<GpuTimer, QueryPoolHandle>
    {
    public:
        GpuTimer();

        ~GpuTimer();

        void Start(CommandBuffer *cmd);

        float End();

        inline static GpuTimer *gpu = nullptr;
        inline static GpuTimer *shadows[SHADOWMAP_CASCADES]{};
        inline static GpuTimer *compute = nullptr;
        inline static GpuTimer *geometry = nullptr;
        inline static GpuTimer *ssao = nullptr;
        inline static GpuTimer *ssr = nullptr;
        inline static GpuTimer *composition = nullptr;
        inline static GpuTimer *fxaa = nullptr;
        inline static GpuTimer *bloom = nullptr;
        inline static GpuTimer *ssgi = nullptr;
        inline static GpuTimer *motionBlur = nullptr;
        inline static GpuTimer *gui = nullptr;
        inline static GpuTimer *dof = nullptr;
        inline static GpuTimer *fsr = nullptr;

    private:
        float
        GetTime();

        void Reset();

        uint64_t m_queries[2]{};
        float m_timestampPeriod;
        CommandBuffer *m_cmd;
    };
}
