#include "Base/Profiler.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace pe
{
    std::vector<Profiler::Entry> Profiler::s_entries[2];
    std::vector<Profiler::Counter> Profiler::s_counters[2];
    float Profiler::s_frameTimes[2] = {};
    std::chrono::high_resolution_clock::time_point Profiler::s_frameStart;
    thread_local std::vector<Profiler::ActiveScope> Profiler::s_scopeStack;
    thread_local uint32_t Profiler::s_currentDepth = 0;
    uint32_t Profiler::s_writeIndex = 0;
    uint64_t Profiler::s_frameGeneration = 0;

    namespace
    {
        // BeginScope/EndScope are lockless: each scope atomically claims a
        // pre-allocated slot in s_entries[writeIndex]. Mutex is only taken on
        // the rare paths (frame boundaries, widget reads, counter mutation).
        constexpr uint32_t kMaxEntriesPerFrame = 16384;

        std::atomic<uint32_t> g_entryCount[2] = {{0}, {0}};

        std::mutex &ProfilerMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        // Pointer-identity keyed counter accumulator. PE_PROFILE_COUNTER always
        // passes a string literal so the const char* address is stable for the
        // process lifetime. Lookup is O(1) instead of strcmp-over-vector.
        std::unordered_map<const char *, uint64_t> &CounterMap(uint32_t writeIndex)
        {
            static std::unordered_map<const char *, uint64_t> maps[2];
            return maps[writeIndex];
        }

        void EnsureCapacity(std::vector<Profiler::Entry> &entries)
        {
            if (entries.size() < kMaxEntriesPerFrame)
                entries.resize(kMaxEntriesPerFrame);
        }
    } // namespace

    void Profiler::BeginFrame()
    {
        std::lock_guard<std::mutex> lock(ProfilerMutex());
        s_frameStart = std::chrono::high_resolution_clock::now();
        EnsureCapacity(s_entries[s_writeIndex]);
        g_entryCount[s_writeIndex].store(0, std::memory_order_relaxed);
        s_counters[s_writeIndex].clear();
        CounterMap(s_writeIndex).clear();
        s_frameGeneration++;
        s_currentDepth = 0;
        s_scopeStack.clear();
    }

    void Profiler::EndFrame()
    {
        while (!s_scopeStack.empty())
            EndScope();

        auto now = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> lock(ProfilerMutex());
        s_frameTimes[s_writeIndex] = std::chrono::duration<float, std::milli>(now - s_frameStart).count();

        s_writeIndex ^= 1;
    }

    void Profiler::BeginScope(const char *name)
    {
        const uint32_t writeIndex = s_writeIndex;
        const uint32_t index = g_entryCount[writeIndex].fetch_add(1, std::memory_order_relaxed);
        // Scopes fired before the first BeginFrame safely no-op the write.
        if (index < s_entries[writeIndex].size())
        {
            Entry &entry = s_entries[writeIndex][index];
            entry.name = name;
            entry.depth = s_currentDepth;
            entry.timeMs = 0.0f;
        }

        ActiveScope scope{};
        scope.start = std::chrono::high_resolution_clock::now();
        scope.entryIndex = index;
        scope.writeIndex = writeIndex;
        scope.generation = s_frameGeneration;
        s_scopeStack.push_back(scope);

        s_currentDepth++;
    }

    void Profiler::EndScope()
    {
        if (s_scopeStack.empty())
            return;

        auto end = std::chrono::high_resolution_clock::now();
        const ActiveScope scope = s_scopeStack.back();

        if (scope.entryIndex < s_entries[scope.writeIndex].size())
        {
            s_entries[scope.writeIndex][scope.entryIndex].timeMs =
                std::chrono::duration<float, std::milli>(end - scope.start).count();
        }

        s_scopeStack.pop_back();
        s_currentDepth--;
    }

    void Profiler::AddCounter(const char *name, uint64_t value)
    {
        std::lock_guard<std::mutex> lock(ProfilerMutex());
        CounterMap(s_writeIndex)[name] += value;
    }

    const std::vector<Profiler::Entry> &Profiler::GetEntries()
    {
        std::lock_guard<std::mutex> lock(ProfilerMutex());
        const uint32_t readIndex = s_writeIndex ^ 1;
        auto &entries = s_entries[readIndex];
        uint32_t count = g_entryCount[readIndex].load(std::memory_order_relaxed);
        if (count > kMaxEntriesPerFrame)
            count = kMaxEntriesPerFrame;
        if (entries.size() > count)
            entries.resize(count);
        return entries;
    }

    const std::vector<Profiler::Counter> &Profiler::GetCounters()
    {
        std::lock_guard<std::mutex> lock(ProfilerMutex());
        const uint32_t readIndex = s_writeIndex ^ 1;
        auto &counters = s_counters[readIndex];
        auto &map = CounterMap(readIndex);
        if (counters.size() != map.size())
        {
            counters.clear();
            counters.reserve(map.size());
            for (auto &kv : map)
                counters.push_back({kv.first, kv.second});
        }
        else
        {
            // Same set of names this frame — just refresh values in place.
            for (Counter &c : counters)
            {
                auto it = map.find(c.name);
                if (it != map.end())
                    c.value = it->second;
            }
        }
        return counters;
    }

    float Profiler::GetFrameTimeMs()
    {
        return s_frameTimes[s_writeIndex ^ 1];
    }
} // namespace pe
