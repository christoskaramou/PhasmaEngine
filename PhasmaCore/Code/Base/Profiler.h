#pragma once

#ifdef PE_TRACY
#include <tracy/Tracy.hpp>
#endif

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

        static void BeginFrame();
        static void EndFrame();

        static void BeginScope(const char *name);
        static void EndScope();

        static const std::vector<Entry> &GetEntries();
        static float GetFrameTimeMs();

    private:
        struct ActiveScope
        {
            std::chrono::high_resolution_clock::time_point start;
            uint32_t entryIndex;
        };

        static std::vector<Entry> s_entries[2];
        static float s_frameTimes[2];
        static std::chrono::high_resolution_clock::time_point s_frameStart;
        static std::vector<ActiveScope> s_scopeStack;
        static uint32_t s_currentDepth;
        static uint32_t s_writeIndex;
    };

    struct CpuProfileScope
    {
        CpuProfileScope(const char *name) { Profiler::BeginScope(name); }
        ~CpuProfileScope() { Profiler::EndScope(); }
    };
} // namespace pe

#define PE_CONCAT_IMPL(a, b) a##b
#define PE_CONCAT(a, b) PE_CONCAT_IMPL(a, b)

#ifdef PE_TRACY
#define PE_PROFILE_SCOPE(name)                                            \
    ZoneScopedN(name);                                                    \
    pe::CpuProfileScope PE_CONCAT(_peProfile_, __LINE__)(name)
#define PE_FRAME_MARK FrameMark
#else
#define PE_PROFILE_SCOPE(name) pe::CpuProfileScope PE_CONCAT(_peProfile_, __LINE__)(name)
#define PE_FRAME_MARK ((void)0)
#endif
