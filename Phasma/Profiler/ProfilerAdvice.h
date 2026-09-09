#pragma once

#include <deque>
#include <string>
#include <string_view>

namespace pe
{
    // A bounded measurement window for one capture session, scene and settings context.
    class ProfilerAdvice
    {
    public:
        bool AddSnapshot(std::string_view json);
        std::string Analyze(int targetFps) const;

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
        std::deque<Sample> m_samples;
        std::string m_context;
        std::string m_session;
        std::string m_status = "Collect at least eight snapshots over one second with unchanged settings.";
    };

    int RunProfilerAdviceCli(int argc, char *argv[]);
} // namespace pe
