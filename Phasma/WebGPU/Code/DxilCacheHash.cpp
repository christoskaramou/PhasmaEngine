#include "DxilCacheHash.h"

#include <cstring>
#include <iomanip>

namespace pwgpu
{
    DxilCacheSha256::DxilCacheSha256()
    {
        m_state = {
            0x6a09e667u,
            0xbb67ae85u,
            0x3c6ef372u,
            0xa54ff53au,
            0x510e527fu,
            0x9b05688cu,
            0x1f83d9abu,
            0x5be0cd19u,
        };
    }

    void DxilCacheSha256::Update(const void *data, size_t size)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        while (size > 0)
        {
            const size_t copySize = std::min(size, m_buffer.size() - m_bufferSize);
            std::memcpy(m_buffer.data() + m_bufferSize, bytes, copySize);
            m_bufferSize += copySize;
            m_totalBytes += copySize;
            bytes += copySize;
            size -= copySize;

            if (m_bufferSize == m_buffer.size())
            {
                Transform(m_buffer.data());
                m_bufferSize = 0;
            }
        }
    }

    void DxilCacheSha256::UpdateUint32(uint32_t value)
    {
        const uint8_t bytes[4] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8) & 0xffu),
            static_cast<uint8_t>((value >> 16) & 0xffu),
            static_cast<uint8_t>((value >> 24) & 0xffu),
        };
        Update(bytes, sizeof(bytes));
    }

    void DxilCacheSha256::UpdateUint64(uint64_t value)
    {
        uint8_t bytes[8]{};
        for (uint32_t i = 0; i < 8; ++i)
            bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xffu);
        Update(bytes, sizeof(bytes));
    }

    std::array<uint8_t, 32> DxilCacheSha256::Final()
    {
        const uint64_t totalBits = m_totalBytes * 8u;
        const uint8_t one = 0x80u;
        Update(&one, 1);

        const uint8_t zero = 0;
        while (m_bufferSize != 56)
            Update(&zero, 1);

        uint8_t lengthBytes[8]{};
        for (uint32_t i = 0; i < 8; ++i)
            lengthBytes[7 - i] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xffu);
        Update(lengthBytes, sizeof(lengthBytes));

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            digest[i * 4 + 0] = static_cast<uint8_t>((m_state[i] >> 24) & 0xffu);
            digest[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 16) & 0xffu);
            digest[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 8) & 0xffu);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i] & 0xffu);
        }
        return digest;
    }

    uint32_t DxilCacheSha256::RotateRight(uint32_t value, uint32_t bits)
    {
        return (value >> bits) | (value << (32u - bits));
    }

    uint32_t DxilCacheSha256::Choose(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (~x & z);
    }

    uint32_t DxilCacheSha256::Majority(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    uint32_t DxilCacheSha256::BigSigma0(uint32_t x)
    {
        return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
    }

    uint32_t DxilCacheSha256::BigSigma1(uint32_t x)
    {
        return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
    }

    uint32_t DxilCacheSha256::SmallSigma0(uint32_t x)
    {
        return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
    }

    uint32_t DxilCacheSha256::SmallSigma1(uint32_t x)
    {
        return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
    }

    void DxilCacheSha256::Transform(const uint8_t *block)
    {
        static constexpr std::array<uint32_t, 64> k = {
            0x428a2f98u,
            0x71374491u,
            0xb5c0fbcfu,
            0xe9b5dba5u,
            0x3956c25bu,
            0x59f111f1u,
            0x923f82a4u,
            0xab1c5ed5u,
            0xd807aa98u,
            0x12835b01u,
            0x243185beu,
            0x550c7dc3u,
            0x72be5d74u,
            0x80deb1feu,
            0x9bdc06a7u,
            0xc19bf174u,
            0xe49b69c1u,
            0xefbe4786u,
            0x0fc19dc6u,
            0x240ca1ccu,
            0x2de92c6fu,
            0x4a7484aau,
            0x5cb0a9dcu,
            0x76f988dau,
            0x983e5152u,
            0xa831c66du,
            0xb00327c8u,
            0xbf597fc7u,
            0xc6e00bf3u,
            0xd5a79147u,
            0x06ca6351u,
            0x14292967u,
            0x27b70a85u,
            0x2e1b2138u,
            0x4d2c6dfcu,
            0x53380d13u,
            0x650a7354u,
            0x766a0abbu,
            0x81c2c92eu,
            0x92722c85u,
            0xa2bfe8a1u,
            0xa81a664bu,
            0xc24b8b70u,
            0xc76c51a3u,
            0xd192e819u,
            0xd6990624u,
            0xf40e3585u,
            0x106aa070u,
            0x19a4c116u,
            0x1e376c08u,
            0x2748774cu,
            0x34b0bcb5u,
            0x391c0cb3u,
            0x4ed8aa4au,
            0x5b9cca4fu,
            0x682e6ff3u,
            0x748f82eeu,
            0x78a5636fu,
            0x84c87814u,
            0x8cc70208u,
            0x90befffau,
            0xa4506cebu,
            0xbef9a3f7u,
            0xc67178f2u,
        };

        uint32_t w[64]{};
        for (uint32_t i = 0; i < 16; ++i)
        {
            const uint32_t j = i * 4;
            w[i] = (static_cast<uint32_t>(block[j]) << 24) |
                   (static_cast<uint32_t>(block[j + 1]) << 16) |
                   (static_cast<uint32_t>(block[j + 2]) << 8) |
                   static_cast<uint32_t>(block[j + 3]);
        }
        for (uint32_t i = 16; i < 64; ++i)
            w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (uint32_t i = 0; i < 64; ++i)
        {
            const uint32_t t1 = h + BigSigma1(e) + Choose(e, f, g) + k[i] + w[i];
            const uint32_t t2 = BigSigma0(a) + Majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    void HashDxilCacheString(DxilCacheSha256 &hash, std::string_view value)
    {
        hash.UpdateUint64(value.size());
        if (!value.empty())
            hash.Update(value.data(), value.size());
    }

    void HashDxilCacheWideString(DxilCacheSha256 &hash, std::wstring_view value)
    {
        hash.UpdateUint64(value.size());
        for (wchar_t ch : value)
            hash.UpdateUint32(static_cast<uint32_t>(ch));
    }

    std::string DxilCacheHexString(const std::array<uint8_t, 32> &digest)
    {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (uint8_t byte : digest)
            out << std::setw(2) << static_cast<unsigned>(byte);
        return out.str();
    }

    std::string DxilCacheSha256Hex(std::string_view data)
    {
        DxilCacheSha256 hash;
        if (!data.empty())
            hash.Update(data.data(), data.size());
        return DxilCacheHexString(hash.Final());
    }
} // namespace pwgpu
