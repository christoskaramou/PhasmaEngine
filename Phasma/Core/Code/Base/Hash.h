#pragma once

namespace pe
{
    template <class T, class = std::void_t<>>
    struct is_hashable : std::false_type
    {
    };

    template <class T>
    struct is_hashable<T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>> : std::true_type
    {
    };

    template <class T>
    constexpr bool is_hashable_v = is_hashable<T>::value;

    // Deterministic, STL/compiler-independent 64-bit FNV-1a. std::hash<std::string> is
    // implementation-defined (MSVC and libc++ disagree), so anything persisted to disk and
    // re-derived on another toolchain (e.g. the Android shader cache, baked on desktop/MSVC
    // and read on-device/NDK-libc++) must hash strings through this instead of std::hash.
    // Mirrors the FNV constants used by the type-ID hasher in Base.h.
    constexpr uint64_t Fnv1a64(std::string_view sv) noexcept
    {
        uint64_t hash = 14695981039346656037ULL; // FNV offset basis
        for (unsigned char c : sv)
        {
            hash ^= c;
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }

    class Hash
    {
    public:
        Hash() : m_hash(0) {}
        Hash(const size_t &hash) : m_hash(hash) {}
        Hash(const Hash &hash) : m_hash(hash.m_hash) {}

        void Combine(const size_t &hash)
        {
            m_hash ^= hash + 0x9e3779b9 + (m_hash << 6) + (m_hash >> 2);
        }

        void Combine(const Hash &hash)
        {
            m_hash ^= hash.m_hash + 0x9e3779b9 + (m_hash << 6) + (m_hash >> 2);
        }

        template <class T>
        void Combine(const T &value)
        {
            static_assert(is_hashable_v<T>, "hash type must be hashable");
            static_assert(!std::is_pointer_v<T>, "hash type must not be a pointer");

            Combine(std::hash<T>()(value));
        }

        // Portable string combine: hashes the bytes with FNV-1a so the resulting key is
        // identical across compilers/STLs. Use this (not the std::hash-based templated
        // Combine) for any string folded into a key that is persisted and re-derived
        // elsewhere — e.g. shader-cache source text, entry points, and shader defines.
        void CombineString(std::string_view value)
        {
            Combine(static_cast<size_t>(Fnv1a64(value)));
        }

        // Portable integer combine. The templated Combine<T> routes integers through std::hash<T>,
        // which is NOT portable: MSVC hashes the bytes (FNV-1a) while libc++ returns the value
        // (identity). For any integer folded into a persisted, cross-toolchain key (shader-cache
        // API/target/stage), feed the raw value straight into the portable mixer instead.
        void CombineValue(uint64_t value)
        {
            Combine(static_cast<size_t>(value));
        }

        bool operator==(const Hash &other) { return m_hash == other.m_hash; }
        operator size_t() { return m_hash; }
        operator size_t() const { return m_hash; }

    private:
        size_t m_hash;
    };

    class StringHash : public Hash
    {
    public:
        StringHash() : Hash() {}
        StringHash(std::string_view string) { CombineString(string); }
    };

    class Hashable
    {
    public:
        Hashable() : m_hash{} {}
        Hash GetHash() const { return m_hash; }

        virtual void UpdateHash() = 0;

    protected:
        Hash m_hash;
    };

    struct PairHash_um // pair as key for unordered_map
    {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2> &pair) const
        {
            Hash hash;
            hash.Combine(pair.first);
            hash.Combine(pair.second);
            return hash;
        }
    };
} // namespace pe
