#pragma once

namespace pwgpu
{
    class DxilCacheSha256
    {
    public:
        DxilCacheSha256();

        void Update(const void *data, size_t size);
        void UpdateUint32(uint32_t value);
        void UpdateUint64(uint64_t value);
        std::array<uint8_t, 32> Final();

    private:
        static uint32_t RotateRight(uint32_t value, uint32_t bits);
        static uint32_t Choose(uint32_t x, uint32_t y, uint32_t z);
        static uint32_t Majority(uint32_t x, uint32_t y, uint32_t z);
        static uint32_t BigSigma0(uint32_t x);
        static uint32_t BigSigma1(uint32_t x);
        static uint32_t SmallSigma0(uint32_t x);
        static uint32_t SmallSigma1(uint32_t x);

        void Transform(const uint8_t *block);

        std::array<uint32_t, 8> m_state{};
        std::array<uint8_t, 64> m_buffer{};
        size_t m_bufferSize = 0;
        uint64_t m_totalBytes = 0;
    };

    void HashDxilCacheString(DxilCacheSha256 &hash, std::string_view value);
    void HashDxilCacheWideString(DxilCacheSha256 &hash, std::wstring_view value);
    std::string DxilCacheHexString(const std::array<uint8_t, 32> &digest);
    std::string DxilCacheSha256Hex(std::string_view data);
} // namespace pwgpu
