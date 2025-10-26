#pragma once

namespace pe
{
    class Buffer;

    struct Allocation
    {
    public:
        [[nodiscard]] bool Valid() const { return data != nullptr; }

        void *data = nullptr;     // mapped data
        size_t size = 0;          // aligned size
        size_t offset = 0;        // byte offset in buffer
        Buffer *buffer = nullptr; // exposed buffer

    private:
        friend class RingBuffer;
        void *chunk = nullptr; // internal reference
    };

    class RingBuffer
    {
    public:
        RingBuffer() = default;
        ~RingBuffer();

        [[nodiscard]] Allocation Allocate(size_t size);
        void Free(const Allocation &range);
        void Reset();

    private:
        struct Block
        {
            size_t offset; // start in bytes
            size_t size;   // length in bytes
            bool free;     // true if available
        };

        struct Chunk
        {
            Buffer *buffer = nullptr;  // mapped buffer
            size_t capacity = 0;       // total bytes
            size_t tail = 0;           // highest used end
            size_t usedBytes = 0;      // sum of live suballoc sizes
            std::vector<Block> blocks; // sorted by offset

            void *PtrAt(size_t offset) const;
            ~Chunk();
        };

        static constexpr size_t kAlignment = 16;
        static constexpr size_t kMinCapacity = 64 * 1024; // initial chunk 64KB
        static constexpr float kGrowthFactor = 2.0f;      // new chunk grow x2

        inline static size_t AlignUp(size_t v) { return (v + (kAlignment - 1)) & ~(kAlignment - 1); }

        Chunk *CreateChunk(size_t minCapacity);
        inline int FindIndex(Chunk *chunk, size_t sizeAligned) const;
        inline size_t TryAppend(Chunk *chunk, size_t sizeAligned) const;
        inline Allocation AllocateFromChunk(Chunk *chunk, size_t sizeAligned);
        inline void FreeInChunk(Chunk *chunk, const Allocation &range);
        inline void MergeFreeBlocks(Chunk *chunk);
        inline void DestroyUnused(Chunk *chunk);
        inline bool OwnsChunk(const Chunk *chunk) const;
        inline Allocation MakeAllocation(Chunk *chunk, size_t sizeAligned, size_t offset) const;

        std::vector<std::unique_ptr<Chunk>> m_chunks;
        std::mutex m_mutex;
    };
}
