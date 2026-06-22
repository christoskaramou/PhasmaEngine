#pragma once

// Coalescing free-list byte suballocator over [0, capacity). First-fit for Phase 1
// (policy swappable later behind this interface). Alloc returns kInvalid on OOM.
// FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class FreeListAllocator
    {
    public:
        static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

        explicit FreeListAllocator(uint32_t capacityBytes);
        uint32_t Alloc(uint32_t bytes); // returns byte offset, or kInvalid on OOM
        void Free(uint32_t offset, uint32_t bytes);
        uint32_t Used() const;

    private:
        struct Span
        {
            uint32_t offset;
            uint32_t size;
        };
        std::vector<Span> m_free; // sorted by offset, coalesced on Free
        uint32_t m_capacity;
        uint32_t m_used = 0;
    };
} // namespace pe::voxel
