#pragma once

namespace pe
{
    class CommandBuffer;

    class GpuTimer::Impl
    {
    public:
        virtual ~Impl() = default;
        virtual void Start(CommandBuffer *cmd) = 0;
        virtual void End() = 0;
        virtual float GetTime() = 0;
        virtual double GetStartTimeMs() const = 0;
        virtual CommandBuffer *GetCommandBuffer() const = 0;
        virtual void ResetState() = 0;
    };

    std::unique_ptr<GpuTimer::Impl> CreateGpuTimerImpl(const std::string &name);
} // namespace pe
