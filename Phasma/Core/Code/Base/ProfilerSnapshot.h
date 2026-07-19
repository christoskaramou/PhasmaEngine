#pragma once

namespace pe
{
    struct ProfilerFrameSample
    {
        float frameMs = 0.f;
        float cpuTotalMs = 0.f;
        float cpuUpdateMs = 0.f;
        float cpuDrawMs = 0.f;
        float gpuTotalMs = 0.f;
    };

    // In-memory profiler frame for live stream or disk dump. Gather only — no UI.
    struct ProfilerSnapshot
    {
        float fps = 0.f;
        float frameMs = 0.f;
        float cpuTotalMs = 0.f;
        float cpuUpdateMs = 0.f;
        float cpuDrawMs = 0.f;
        float cpuScopeTotalMs = 0.f;
        float gpuTotalMs = 0.f;
        uint32_t renderDocCaptureCount = 0;
        bool renderDocAvailable = false;

        uint64_t ramTotalMb = 0;
        uint64_t ramUsedMb = 0;
        uint64_t ramProcessMb = 0;
        uint64_t gpuVramAppMb = 0;
        uint64_t gpuVramOtherMb = 0;
        uint64_t gpuVramBudgetMb = 0;
        uint64_t gpuHostAppMb = 0;
        uint64_t gpuHostOtherMb = 0;
        uint64_t gpuHostBudgetMb = 0;

        std::vector<Profiler::Entry> cpuEntries;
        std::vector<Profiler::Counter> counters;
        std::vector<GpuTimerSample> gpuSamples;
        std::vector<ProfilerFrameSample> frameHistory;

        // Pulls current Core/RHI metrics. gpuSamples are caller-owned (e.g. drained AfterCommandWait).
        static ProfilerSnapshot Gather(std::vector<GpuTimerSample> gpuSamples = {});

        // Compact single-line JSON (safe for length-prefixed stream frames).
        std::string ToJson() const;
    };
} // namespace pe
