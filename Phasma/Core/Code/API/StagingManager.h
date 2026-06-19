#pragma once

namespace pe
{
    class Buffer;

    constexpr uint8_t kDeleteDelay = 5;
    constexpr uint8_t kOversizeCapMultiplier = 2;

    struct StagingAllocation
    {
        Buffer *buffer = nullptr;
        void *data = nullptr;
        size_t size = 0;
        size_t used = 0;
        size_t delaySerial = 0;
    };

    class StagingManager
    {
    public:
        ~StagingManager();

        StagingAllocation Allocate(size_t size);
        void RemoveUnused();
        void SetUnused(const StagingAllocation &allocation);
        const std::deque<StagingAllocation> &GetAllocations() { return m_allocations; }

    private:
        std::deque<StagingAllocation> m_allocations{};
        std::mutex m_mutex{};
    };
} // namespace pe
