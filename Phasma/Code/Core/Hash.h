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
    class Hash
    {
    public:
        Hash() : m_hash(0) {}

        Hash(size_t hash) : m_hash(hash) {}

        Hash(const Hash& other) : m_hash(other.m_hash) {}

        Hash(const void *data, size_t size) : m_hash(0) { Combine(data, size); }

        void Combine(const void *data, size_t size)
        {
            const size_t *array = reinterpret_cast<const size_t *>(data);
            size_t arraySize = size / sizeof(size_t);
            size_t bytesToFit = size % sizeof(size_t);

            for (size_t i = 0; i < arraySize; i++)
                m_hash ^= std::hash<size_t>()(array[i]) + 0x9e3779b9 + (m_hash << 6) + (m_hash >> 2);

            if (bytesToFit)
            {
                // Shift the remaining bytes of the array to a size_t (8 bytes) variable to avoid scrap
                // bits from the array raw memory access. It push the bytes to the right and it will 
                // replace the unused left bits with zeros
                size_t lastBytes = array[arraySize] >> ((sizeof(size_t) - bytesToFit) * 8);
                m_hash ^= std::hash<size_t>()(lastBytes) + 0x9e3779b9 + (m_hash << 6) + (m_hash >> 2);
            }
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

        StringHash(const std::string &string) : Hash(string.data(), string.size()) {}

        StringHash(const char *string) : Hash(string, strlen(string)) {}

        void Combine(const std::string &string) { Hash::Combine(string.data(), string.size()); }

        void Combine(const char *string) { Hash::Combine(string, strlen(string)); }
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