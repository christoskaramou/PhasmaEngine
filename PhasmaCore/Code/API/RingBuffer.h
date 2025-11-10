#pragma once

namespace pe
{
    class Buffer;

    struct Allocation
    {
        [[nodiscard]] bool Valid() const { return data != nullptr; }

        void *data = nullptr;     // mapped CPU ptr
        size_t size = 0;          // aligned size in bytes
        size_t offset = 0;        // byte offset within the chunk's buffer
        Buffer *buffer = nullptr; // GPU/upload buffer handle
        void *chunk = nullptr;    // opaque owner pointer (internal Chunk*)
    };

    class RingBuffer
    {
    public:
        RingBuffer() = default;
        ~RingBuffer();

        // Thread-safe
        [[nodiscard]] Allocation Allocate(size_t size);
        void Free(const Allocation &range);
        void Reset();

    private:
        struct Block
        {
            size_t offset; // start byte offset in the chunk
            size_t size;   // length in bytes
            bool free;     // true if available
        };

        struct Chunk
        {
            ~Chunk();

            Buffer *buffer = nullptr;  // persistently mapped buffer
            size_t capacity = 0;       // total bytes in this chunk
            size_t tail = 0;           // highest end of any used block
            size_t usedBytes = 0;      // sum of all allocated block sizes
            std::vector<Block> blocks; // sorted blocks

            void *PtrAt(size_t offset) const;
        };

        // ---- constants / math helpers ----
        static constexpr size_t kAlignment = 16;
        static constexpr size_t kMinCapacity = 64 * 1024;
        static constexpr float kGrowthFactor = 2.0f;

        static inline size_t AlignUp(size_t v) { return (v + (kAlignment - 1)) & ~(kAlignment - 1); }

        static std::vector<Block>::iterator FindBestFitBlock(std::vector<Block> &blocks, size_t sizeAligned);
        static size_t TryAppendAtTail(const Chunk *chunk, size_t sizeAligned);
        static Allocation MakeAllocation(Chunk *chunk, size_t sizeAligned, size_t offset);
        static Allocation CarveFromBlock(Chunk *chunk, std::vector<Block>::iterator it, size_t sizeAligned);
        static Allocation AppendNewBlock(Chunk *chunk, size_t appendOffset, size_t sizeAligned);
        static void MergeFreeBlocks(Chunk *chunk);

#if defined(_DEBUG)
        static void DebugValidateChunk(const Chunk *c);
#else
        static inline void DebugValidateChunk(const Chunk *) {}
#endif

        Chunk *CreateChunk(size_t minCapacity);
        Allocation AllocateFromChunk(Chunk *chunk, size_t sizeAligned);
        void FreeInChunk(Chunk *chunk, const Allocation &range);
        void DestroyUnused(Chunk *chunk);
        bool OwnsChunk(const Chunk *chunk) const;

        std::vector<std::unique_ptr<Chunk>> m_chunks;
        std::mutex m_mutex;
    };
}
