#pragma once

#include <deque>
#include <string>
#include <string_view>

namespace pe
{
    // A bounded measurement window for one capture session, scene and settings context, plus the
    // whole session's timeline, which settings changes and stream gaps do not reset.
    class ProfilerAdvice
    {
    public:
        bool AddSnapshot(std::string_view json);
        // full adds every 10 s timeline row and every spike event: the saved session file.
        std::string Analyze(int targetFps, bool full = false) const;
        bool HasSession() const { return !m_timeline.empty(); }

    private:
        struct Sample
        {
            double timestampMs = 0;
            double frameMs = 0;
            double gpuMs = 0;
            double lightingMs = 0;
            double shadowMs = 0;
            double vramMb = 0;
            double vramBudgetMb = 0;
        };
        // One frame's CPU scopes or GPU passes, summed by name.
        struct ScopeCost
        {
            float ms = 0;       // inclusive
            float selfMs = 0;   // minus direct children
            uint32_t depth = 0; // shallowest occurrence
        };
        using Costs = std::unordered_map<std::string, ScopeCost>;
        struct FrameTimes
        {
            float frameMs = 0;
            float cpuMs = 0;
            float gpuMs = 0;
        };
        // A frame and its local baseline: the median of the 64 frames up to its packet.
        struct StreamFrame
        {
            FrameTimes times;
            FrameTimes base;
        };
        // Wait: the CPU blocked on the GPU, the driver or presentation; a symptom, not work to remove.
        enum Side : uint8_t
        {
            Gpu,
            Cpu,
            Wait,
            Other
        };
        // Where one spike's extra time went, decided once, against the steady medians of its moment.
        struct SpikeSource
        {
            std::string name = "unattributed";
            Side side = Other;
            double excessMs = 0;
            double share = 0;
        };
        struct WorstFrame
        {
            FrameTimes times;
            FrameTimes base;
            double timestampMs = 0;
            SpikeSource source;
        };
        // ponytail: 0.25 ms bins up to 64 ms, the last one open; finer bins or an exact reservoir if
        // the timeline ever needs exact percentiles.
        static constexpr int kHistBins = 257;
        static constexpr double kHistBinMs = 0.25;
        struct Total
        {
            double ms = 0;
            double sq = 0;       // sum of its per-frame time squared
            unsigned frames = 0; // frames it ran in
        };
        // 10 s of the session.
        struct Bucket
        {
            uint32_t hist[kHistBins] = {};
            unsigned frames = 0;
            float maxMs = 0;
            double frameMsSum = 0;
            unsigned spikes[4] = {}; // hitches by Side
            std::unordered_map<std::string, unsigned> sources;
            bool contextChanged = false;
            // Every frame's scope and pass time from the stream's totals, summed by name.
            unsigned costFrames = 0;
            unsigned gpuCostFrames = 0;
            std::unordered_map<std::string, Total> cpuCost;
            std::unordered_map<std::string, Total> gpuCost;
        };
        // One hitch: consecutive spike frames, timed and sided by its slowest frame.
        struct SpikeEvent
        {
            double atMs = 0; // since the session start
            float frameMs = 0;
            float baseMs = 0;
            Side side = Other;
            std::string source; // empty: no packet's worst frame fell in it, so no breakdown
            float share = 0;
            unsigned frames = 1;
            float durationMs = 0;
        };
        // The hitch the last frame belonged to.
        struct Hitch
        {
            size_t row = 0;
            size_t event = SIZE_MAX;
            float peakMs = 0;
            Side side = Other;
            bool sourced = false;
        };

        static bool IsSpike(const FrameTimes &times, const FrameTimes &base);
        static bool IsWait(std::string_view scope);
        static std::vector<std::pair<const std::string *, const Total *>> Intermittent(const std::unordered_map<std::string, Total> &costs,
                                                                                       unsigned frames);
        static Side SideOf(const FrameTimes &times, double gpuMs, const FrameTimes &base);
        SpikeSource Attribute(const FrameTimes &times, const FrameTimes &base, const Costs &cpu, const Costs &gpu) const;
        void Reset();
        void ResetSession();

        std::deque<Sample> m_samples;
        std::deque<Costs> m_steadyCpu; // aligned with m_samples
        std::deque<Costs> m_steadyGpu;
        std::deque<StreamFrame> m_frames; // every streamed frame of the window
        std::deque<FrameTimes> m_recent;  // the last 64 frames, for the local baseline
        std::deque<WorstFrame> m_worst;   // attributed spike frames of the window
        std::vector<Bucket> m_timeline;   // the whole session
        std::vector<SpikeEvent> m_events;
        std::optional<Hitch> m_hitch;
        double m_sessionStartMs = 0;
        bool m_contextChanged = false;
        std::string m_context;
        std::string m_session;
        std::string m_status = "Collect at least eight snapshots over one second with unchanged settings.";
    };

    int RunProfilerAdviceCli(int argc, char *argv[]);
} // namespace pe
