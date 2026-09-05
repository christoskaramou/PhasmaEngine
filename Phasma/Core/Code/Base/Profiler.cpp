
#include <cstring>

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

        // PE_PROFILE_COUNTER fires under every draw/dispatch/barrier on every recording thread, so
        // the hot path is lock-free: a thread_local name->slot cache plus a relaxed fetch_add.
        // PE_PROFILE_COUNTER always passes a string literal, so the const char* address is the key.
        constexpr uint32_t kMaxCounters = 256;

        struct CounterSlot
        {
            const char *name = nullptr;
            std::atomic<uint64_t> value[2]{};
        };

        CounterSlot g_counterSlots[kMaxCounters];
        std::atomic<uint32_t> g_counterSlotCount{0};

        CounterSlot *FindOrRegisterCounter(const char *name)
        {
            std::lock_guard<std::mutex> lock(ProfilerMutex());
            const uint32_t count = g_counterSlotCount.load(std::memory_order_relaxed);
            for (uint32_t i = 0; i < count; ++i)
                if (g_counterSlots[i].name == name)
                    return &g_counterSlots[i];
            if (count >= kMaxCounters)
            {
                PE_ERROR("Profiler: more than %u distinct counters", kMaxCounters);
                return nullptr;
            }
            g_counterSlots[count].name = name;
            g_counterSlotCount.store(count + 1, std::memory_order_release);
            return &g_counterSlots[count];
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
        const uint32_t counterCount = g_counterSlotCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < counterCount; ++i)
            g_counterSlots[i].value[s_writeIndex].store(0, std::memory_order_relaxed);
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
        const auto start = std::chrono::high_resolution_clock::now();
        const uint32_t writeIndex = s_writeIndex;
        const uint32_t index = g_entryCount[writeIndex].fetch_add(1, std::memory_order_relaxed);
        // Scopes fired before the first BeginFrame safely no-op the write.
        if (index < s_entries[writeIndex].size())
        {
            Entry &entry = s_entries[writeIndex][index];
            entry.name = name;
            entry.depth = s_currentDepth;
            entry.timeMs = 0.0f;
            entry.startOffsetMs = std::chrono::duration<float, std::milli>(start - s_frameStart).count();
        }

        ActiveScope scope{};
        scope.start = start;
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
        thread_local std::unordered_map<const char *, CounterSlot *> cache;
        auto it = cache.find(name);
        if (it == cache.end())
            it = cache.emplace(name, FindOrRegisterCounter(name)).first;
        if (it->second)
            it->second->value[s_writeIndex].fetch_add(value, std::memory_order_relaxed);
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
        counters.clear();
        const uint32_t count = g_counterSlotCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint64_t value = g_counterSlots[i].value[readIndex].load(std::memory_order_relaxed);
            if (value) // only names hit this frame, as before
                counters.push_back({g_counterSlots[i].name, value});
        }
        return counters;
    }

    float Profiler::GetFrameTimeMs()
    {
        return s_frameTimes[s_writeIndex ^ 1];
    }
} // namespace pe
