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

        double delta;
        double time;
        std::vector<double> timestamps{};

    private:
        std::chrono::duration<double> m_duration{};

    public:
        static FrameTimer &Instance()
        {
            static FrameTimer frame_timer;
            return frame_timer;
        }

        FrameTimer(FrameTimer const &) = delete; // copy constructor
        FrameTimer(FrameTimer &&)

            noexcept = delete;                              // move constructor
        FrameTimer &operator=(FrameTimer const &) = delete; // copy assignment
        FrameTimer &operator=(FrameTimer &&) = delete;      // move assignment
    private:
        ~FrameTimer() = default; // destructor
        FrameTimer();            // default constructor
    };

    class CommandBuffer;

    class GPUTimer
    {
    public:
        GPUTimer();

        void Start(CommandBuffer *cmd);

        float End();

        void Destroy();

    private:
        float GetTime();

        void Reset();

        QueryPoolHandle queryPool;
        uint64_t queries[2]{};
        float timestampPeriod;
        CommandBuffer *_cmd;
    };
}
