#include "Base/Profiler.h"

namespace pe
{
    std::vector<Profiler::Entry> Profiler::s_entries[2];
    float Profiler::s_frameTimes[2] = {};
    std::chrono::high_resolution_clock::time_point Profiler::s_frameStart;
    std::vector<Profiler::ActiveScope> Profiler::s_scopeStack;
    uint32_t Profiler::s_currentDepth = 0;
    uint32_t Profiler::s_writeIndex = 0;

    void Profiler::BeginFrame()
    {
        s_frameStart = std::chrono::high_resolution_clock::now();
        s_entries[s_writeIndex].clear();
        s_currentDepth = 0;
        s_scopeStack.clear();
    }

    void Profiler::EndFrame()
    {
        while (!s_scopeStack.empty())
            EndScope();

        auto now = std::chrono::high_resolution_clock::now();
        s_frameTimes[s_writeIndex] = std::chrono::duration<float, std::milli>(now - s_frameStart).count();

        s_writeIndex ^= 1;
    }

    void Profiler::BeginScope(const char *name)
    {
        auto &entries = s_entries[s_writeIndex];

        // Reserve entry slot now (pre-order) — time filled on EndScope
        Entry entry{};
        entry.name = name;
        entry.depth = s_currentDepth;
        entry.timeMs = 0.0f;
        uint32_t index = static_cast<uint32_t>(entries.size());
        entries.push_back(entry);

        ActiveScope scope{};
        scope.start = std::chrono::high_resolution_clock::now();
        scope.entryIndex = index;
        s_scopeStack.push_back(scope);

        s_currentDepth++;
    }

    void Profiler::EndScope()
    {
        if (s_scopeStack.empty())
            return;

        auto end = std::chrono::high_resolution_clock::now();
        const ActiveScope &scope = s_scopeStack.back();

        s_entries[s_writeIndex][scope.entryIndex].timeMs =
            std::chrono::duration<float, std::milli>(end - scope.start).count();

        s_scopeStack.pop_back();
        s_currentDepth--;
    }

    const std::vector<Profiler::Entry> &Profiler::GetEntries()
    {
        return s_entries[s_writeIndex ^ 1];
    }

    float Profiler::GetFrameTimeMs()
    {
        return s_frameTimes[s_writeIndex ^ 1];
    }
} // namespace pe
