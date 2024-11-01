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
        StringHash(const std::string &string) { Combine(string); }
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
}
