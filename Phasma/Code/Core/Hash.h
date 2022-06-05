/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    template <typename T, typename = std::void_t<>>
    struct is_hashable : std::false_type
    {
    };

    template <typename T>
    struct is_hashable<T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>> : std::true_type
    {
    };

    template <typename T>
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

    private:
        size_t m_hash;
    };

    class StringHash : public Hash
    {
    public:
        StringHash() : Hash() {}

        StringHash(const std::string &string) { Combine(string); }
    };

    class HashableBase
    {
    public:
        HashableBase() : m_hash{} {}

        Hash GetHash() const { return m_hash; }

        void SetHash(Hash hash) { m_hash = hash; }

    protected:
        Hash m_hash;
    };

    template <class... Params>
    class Hashable : public HashableBase
    {
    public:
        // Implement in each class that inherits from Hashable
        virtual void CreateHash(Params &&...params);
    };
}