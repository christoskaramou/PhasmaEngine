#pragma once

#ifdef PE_TRACY
#include <tracy/Tracy.hpp>
#endif

#include <cstdint>
#include <vector>

namespace pe
{
    class Profiler
    {
    public:
        struct Entry
        {
            const char *name;
            float timeMs;
            uint32_t depth;
        };

        struct Counter
        {
            const char *name;
            uint64_t value;
        };

        static void BeginFrame();
        static void EndFrame();

        static void BeginScope(const char *name);
        static void EndScope();
        static void AddCounter(const char *name, uint64_t value = 1);

        static const std::vector<Entry> &GetEntries();
        static const std::vector<Counter> &GetCounters();
        static float GetFrameTimeMs();

    private:
        struct ActiveScope
        {
            std::chrono::high_resolution_clock::time_point start;
            uint32_t entryIndex;
            uint32_t writeIndex;
            uint64_t generation;
        };

        static std::vector<Entry> s_entries[2];
        static std::vector<Counter> s_counters[2];
        static float s_frameTimes[2];
        static std::chrono::high_resolution_clock::time_point s_frameStart;
        static thread_local std::vector<ActiveScope> s_scopeStack;
        static thread_local uint32_t s_currentDepth;
        static uint32_t s_writeIndex;
        static uint64_t s_frameGeneration;
    };

    struct CpuProfileScope
    {
        CpuProfileScope(const char *name) { Profiler::BeginScope(name); }
        ~CpuProfileScope() { Profiler::EndScope(); }
    };
} // namespace pe

#define PE_CONCAT_IMPL(a, b) a##b
#define PE_CONCAT(a, b) PE_CONCAT_IMPL(a, b)
#define PE_PROFILE_COUNTER(name, value) pe::Profiler::AddCounter(name, value)

#ifdef PE_TRACY
#define PE_PROFILE_SCOPE(name) \
    ZoneScopedN(name);         \
    pe::CpuProfileScope PE_CONCAT(_peProfile_, __LINE__)(name)
#define PE_FRAME_MARK FrameMark
#else
#define PE_PROFILE_SCOPE(name) pe::CpuProfileScope PE_CONCAT(_peProfile_, __LINE__)(name)
#define PE_FRAME_MARK ((void)0)
#endif
