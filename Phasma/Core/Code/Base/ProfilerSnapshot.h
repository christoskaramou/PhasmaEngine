#pragma once

namespace pe
{
    class Image;
    struct ProfilerFrameSample
    {
        float frameMs = 0.f;
        float cpuTotalMs = 0.f;
        float cpuUpdateMs = 0.f;
        float cpuDrawMs = 0.f;
        float gpuTotalMs = 0.f;
    };

    // One scope's or pass's inclusive time summed over every frame between two publishes, and the
    // number of those frames it ran in: small costs that add up show here, not in any single frame.
    struct ProfilerTotal
    {
        std::string name;
        double ms = 0;
        double sq = 0; // sum of each frame's time squared: how much it varies frame to frame
        uint32_t frames = 0;
    };

    // In-memory profiler frame for live stream or disk dump. Gather only — no UI.
    struct ProfilerSnapshot
    {
        std::string metadataJson = "{}";
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
        // Slowest frame since the previous publish, with its own breakdown, so a spike between
        // sampled snapshots keeps its attribution. Serialized only when worstFrame.frameMs > 0.
        ProfilerFrameSample worstFrame;
        std::vector<Profiler::Entry> worstCpuEntries;
        std::vector<GpuTimerSample> worstGpuSamples;
        // Serialized only when totalFrames > 0.
        uint32_t totalFrames = 0;
        uint32_t totalGpuFrames = 0;
        std::vector<ProfilerTotal> cpuTotals;
        std::vector<ProfilerTotal> gpuTotals;

        // Pulls current Core/RHI metrics. gpuSamples are caller-owned (e.g. drained AfterCommandWait).
        static ProfilerSnapshot Gather(std::vector<GpuTimerSample> gpuSamples = {},
                                       const std::filesystem::path &scenePath = {}, Image *viewport = nullptr);

        static std::string CaptureMetadata(const std::filesystem::path &scenePath, Image *viewport = nullptr);

        // Compact single-line JSON (safe for length-prefixed stream frames).
        std::string ToJson() const;
    };
} // namespace pe
