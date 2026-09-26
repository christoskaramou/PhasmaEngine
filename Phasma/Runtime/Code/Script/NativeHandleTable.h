#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace pe
{
    // Opaque 64-bit handles for engine objects handed to game code. Handle 0 is never issued and ids
    // are never reused. Valid(ref) tells whether the referenced object is still alive; dead entries are
    // pruned when marked stale (e.g. after a subtree delete) and whenever the table doubles.
    template <class Key, class Ref, class Valid>
    class NativeHandleTable
    {
    public:
        static constexpr size_t MinPruneSize = 64;

        explicit NativeHandleTable(Valid valid = {}) : m_valid(std::move(valid)) {}

        // Same live object, same handle.
        uint64_t Issue(Key key, const Ref &ref)
        {
            auto it = m_keys.find(key);
            if (it != m_keys.end())
            {
                const auto entry = m_entries.find(it->second);
                if (entry != m_entries.end() && m_valid(entry->second.second))
                    return it->second;
                if (entry != m_entries.end())
                    m_entries.erase(entry);
                m_keys.erase(it);
            }
            Maintain();
            const uint64_t handle = m_next++;
            m_entries.emplace(handle, std::make_pair(key, ref));
            m_keys.emplace(key, handle);
            return handle;
        }

        const Ref *Resolve(uint64_t handle) const
        {
            const auto it = m_entries.find(handle);
            return it != m_entries.end() && m_valid(it->second.second) ? &it->second.second : nullptr;
        }

        // Objects died outside Issue/Resolve; prune on the next Maintain.
        void MarkStale() { m_stale = true; }

        void Maintain()
        {
            if (m_stale || m_entries.size() >= (std::max)(MinPruneSize, 2 * m_prunedSize))
                Prune();
        }

        void Prune()
        {
            for (auto it = m_entries.begin(); it != m_entries.end();)
            {
                if (m_valid(it->second.second))
                {
                    ++it;
                    continue;
                }
                const auto key = m_keys.find(it->second.first);
                if (key != m_keys.end() && key->second == it->first)
                    m_keys.erase(key);
                it = m_entries.erase(it);
            }
            m_prunedSize = m_entries.size();
            m_stale = false;
        }

        // Keeps the counter so handles from before the clear stay invalid.
        void Clear()
        {
            m_entries.clear();
            m_keys.clear();
            m_prunedSize = 0;
            m_stale = false;
        }

        size_t Size() const { return m_entries.size(); }

    private:
        Valid m_valid;
        std::unordered_map<uint64_t, std::pair<Key, Ref>> m_entries;
        std::unordered_map<Key, uint64_t> m_keys;
        uint64_t m_next = 1;
        size_t m_prunedSize = 0;
        bool m_stale = false;
    };
} // namespace pe
