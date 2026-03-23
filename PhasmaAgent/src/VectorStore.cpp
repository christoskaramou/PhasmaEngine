#include "PhasmaAgent/VectorStore.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <future>
#include <unordered_map>

namespace pagent
{
    using json = nlohmann::json;

    // Parse "file" and "last_modified" from metadata JSON once and cache them on the entry.
    void VectorStore::ParseMetadataCache(VectorEntry &e)
    {
        if (e.metadata.empty())
            return;
        try
        {
            auto meta = json::parse(e.metadata);
            e.file = meta.value("file", "");
            e.last_modified = meta.value("last_modified", "");
        }
        catch (...)
        {
        }
    }

    void VectorStore::RebuildIdIndex()
    {
        m_idIndex.clear();
        m_idIndex.reserve(m_entries.size());
        for (size_t i = 0; i < m_entries.size(); ++i)
            m_idIndex[m_entries[i].id] = i;
    }

    // Swap-and-pop removal — O(1), keeps m_idIndex consistent.
    void VectorStore::RemoveAt(size_t idx)
    {
        const size_t last = m_entries.size() - 1;
        m_idIndex.erase(m_entries[idx].id);
        if (idx != last)
        {
            m_entries[idx] = std::move(m_entries[last]);
            m_idIndex[m_entries[idx].id] = idx;
        }
        m_entries.pop_back();
    }

    void VectorStore::Add(VectorEntry entry)
    {
        ParseMetadataCache(entry);
        std::unique_lock lock(m_mutex);
        auto it = m_idIndex.find(entry.id);
        if (it != m_idIndex.end())
        {
            m_entries[it->second] = std::move(entry);
            return;
        }
        m_idIndex[entry.id] = m_entries.size();
        m_entries.push_back(std::move(entry));
    }

    void VectorStore::Remove(const std::string &id)
    {
        std::unique_lock lock(m_mutex);
        auto it = m_idIndex.find(id);
        if (it == m_idIndex.end())
            return;
        RemoveAt(it->second);
    }

    void VectorStore::RemoveByMetadataType(const std::string &type)
    {
        std::unique_lock lock(m_mutex);
        // Collect indices in reverse so RemoveAt doesn't shift unvisited entries
        std::vector<size_t> toRemove;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            try
            {
                auto meta = json::parse(m_entries[i].metadata);
                if (meta.value("type", "") == type)
                    toRemove.push_back(i);
            }
            catch (...)
            {
            }
        }
        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
            RemoveAt(*it);
    }

    void VectorStore::RemoveByFile(const std::string &file)
    {
        std::unique_lock lock(m_mutex);
        std::vector<size_t> toRemove;
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].file == file)
                toRemove.push_back(i);
        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
            RemoveAt(*it);
    }

    std::vector<VectorEntry> VectorStore::ExtractByFile(const std::string &file)
    {
        std::unique_lock lock(m_mutex);
        std::vector<VectorEntry> extracted;
        // Collect matching indices in reverse so swap-and-pop is safe
        std::vector<size_t> toRemove;
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].file == file)
                toRemove.push_back(i);
        extracted.reserve(toRemove.size());
        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        {
            extracted.push_back(std::move(m_entries[*it]));
            RemoveAt(*it);
        }
        return extracted;
    }

    bool VectorStore::HasFileWithTimestamp(const std::string &file, const std::string &timestamp) const
    {
        std::shared_lock lock(m_mutex);
        for (const auto &e : m_entries)
            if (e.file == file && e.last_modified == timestamp)
                return true;
        return false;
    }

    std::unordered_map<std::string, std::string> VectorStore::BuildFileTimestampMap() const
    {
        std::shared_lock lock(m_mutex);
        std::unordered_map<std::string, std::string> timestamps;
        timestamps.reserve(m_entries.size());
        for (const auto &e : m_entries)
            if (!e.file.empty() && !e.last_modified.empty())
                timestamps[e.file] = e.last_modified;
        return timestamps;
    }

    std::vector<VectorStore::SearchResult> VectorStore::Search(
        const std::vector<float> &query, int top_k, float min_score) const
    {
        std::shared_lock lock(m_mutex);
        std::vector<SearchResult> results;
        results.reserve(m_entries.size());

        for (const auto &entry : m_entries)
        {
            float score = CosineSimilarity(query, entry.embedding);
            if (score >= min_score)
                results.push_back({&entry, score});
        }

        // Sort by score descending
        std::sort(results.begin(), results.end(),
                  [](const SearchResult &a, const SearchResult &b)
                  { return a.score > b.score; });

        if (static_cast<int>(results.size()) > top_k)
            results.resize(top_k);

        return results;
    }

    void VectorStore::SaveToFile(const std::string &path) const
    {
        std::shared_lock lock(m_mutex);
        json arr = json::array();
        for (const auto &e : m_entries)
        {
            json obj;
            obj["id"] = e.id;
            obj["content"] = e.content;
            obj["metadata"] = e.metadata;
            obj["embedding"] = e.embedding;
            arr.push_back(std::move(obj));
        }

        std::ofstream f(path);
        if (f.is_open())
            f << arr.dump(-1, ' ', false, json::error_handler_t::replace);
    }

    void VectorStore::LoadFromFile(const std::string &path)
    {
        LoadFromFile(path, 0);
    }

    void VectorStore::LoadFromFile(const std::string &path, int expectedDims)
    {
        std::ifstream f(path);
        if (!f.is_open())
            return;

        std::string jsonBody((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        if (jsonBody.empty())
            return;

        try
        {
            auto arr = json::parse(jsonBody);
            if (!arr.is_array())
                return;

            // Build locally — all parsing and metadata caching outside the lock
            std::vector<VectorEntry> entries;
            entries.reserve(arr.size());
            for (const auto &obj : arr)
            {
                VectorEntry e;
                e.id = obj.value("id", "");
                e.content = obj.value("content", "");
                e.metadata = obj.value("metadata", "");
                if (obj.contains("embedding") && obj["embedding"].is_array())
                    e.embedding = obj["embedding"].get<std::vector<float>>();
                ParseMetadataCache(e);
                entries.push_back(std::move(e));
            }

            // Dimension mismatch: discard
            if (expectedDims > 0 && !entries.empty() &&
                static_cast<int>(entries[0].embedding.size()) != expectedDims)
                return;

            std::unordered_map<std::string, size_t> idIndex;
            idIndex.reserve(entries.size());
            for (size_t i = 0; i < entries.size(); ++i)
                idIndex[entries[i].id] = i;

            std::unique_lock lock(m_mutex);
            m_entries = std::move(entries);
            m_idIndex = std::move(idIndex);
        }
        catch (...)
        {
        }
    }

    // Binary format: [magic 4B][version 4B][entryCount 4B][dims 4B]
    // Per entry: [idLen 4B][id][contentLen 4B][content][metaLen 4B][metadata][embedding dims*4B]
    static constexpr uint32_t BINARY_MAGIC = 0x56535442; // "VSTB"
    static constexpr uint32_t BINARY_VERSION = 1;

    static void writeU32(std::ofstream &f, uint32_t v)
    {
        f.write(reinterpret_cast<const char *>(&v), 4);
    }
    static void writeStr(std::ofstream &f, const std::string &s)
    {
        writeU32(f, static_cast<uint32_t>(s.size()));
        f.write(s.data(), s.size());
    }
    static uint32_t readU32(std::ifstream &f)
    {
        uint32_t v = 0;
        f.read(reinterpret_cast<char *>(&v), 4);
        return v;
    }
    static std::string readStr(std::ifstream &f)
    {
        uint32_t len = readU32(f);
        if (len > 10'000'000) // sanity limit
            return {};
        std::string s(len, '\0');
        f.read(s.data(), len);
        return s;
    }

    void VectorStore::SaveToBinary(const std::string &path) const
    {
        std::shared_lock lock(m_mutex);
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open())
            return;

        uint32_t dims = m_entries.empty() ? 0 : static_cast<uint32_t>(m_entries[0].embedding.size());
        writeU32(f, BINARY_MAGIC);
        writeU32(f, BINARY_VERSION);
        writeU32(f, static_cast<uint32_t>(m_entries.size()));
        writeU32(f, dims);

        for (const auto &e : m_entries)
        {
            writeStr(f, e.id);
            writeStr(f, e.content);
            writeStr(f, e.metadata);
            f.write(reinterpret_cast<const char *>(e.embedding.data()), e.embedding.size() * sizeof(float));
        }
    }

    void VectorStore::LoadFromBinary(const std::string &path)
    {
        LoadFromBinary(path, 0);
    }

    void VectorStore::LoadFromBinary(const std::string &path, int expectedDims)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            return;

        uint32_t magic = readU32(f);
        if (magic != BINARY_MAGIC)
            return;
        uint32_t version = readU32(f);
        if (version != BINARY_VERSION)
            return;
        uint32_t count = readU32(f);
        uint32_t dims = readU32(f);

        if (expectedDims > 0 && dims != static_cast<uint32_t>(expectedDims))
            return;

        // Build locally — all I/O and metadata parsing outside the lock
        std::vector<VectorEntry> entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count && f.good(); ++i)
        {
            VectorEntry e;
            e.id = readStr(f);
            e.content = readStr(f);
            e.metadata = readStr(f);
            e.embedding.resize(dims);
            f.read(reinterpret_cast<char *>(e.embedding.data()), dims * sizeof(float));
            ParseMetadataCache(e);
            entries.push_back(std::move(e));
        }

        std::unordered_map<std::string, size_t> idIndex;
        idIndex.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
            idIndex[entries[i].id] = i;

        std::unique_lock lock(m_mutex);
        m_entries = std::move(entries);
        m_idIndex = std::move(idIndex);
    }

    size_t VectorStore::Size() const
    {
        std::shared_lock lock(m_mutex);
        return m_entries.size();
    }

    void VectorStore::Clear()
    {
        std::unique_lock lock(m_mutex);
        m_entries.clear();
        m_idIndex.clear();
    }

    void VectorStore::ForEachEntry(const std::function<void(const VectorEntry &)> &fn) const
    {
        std::shared_lock lock(m_mutex);
        for (const auto &e : m_entries)
            fn(e);
    }

    float VectorStore::CosineSimilarity(const std::vector<float> &a, const std::vector<float> &b)
    {
        if (a.size() != b.size() || a.empty())
            return 0.0f;

        float dot = 0.0f, normA = 0.0f, normB = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            dot += a[i] * b[i];
            normA += a[i] * a[i];
            normB += b[i] * b[i];
        }

        float denom = std::sqrt(normA) * std::sqrt(normB);
        return denom > 0.0f ? dot / denom : 0.0f;
    }

    std::vector<VectorStore::SearchResult> VectorStore::SearchMulti(
        const std::vector<std::vector<float>> &queries, int top_k, float min_score) const
    {
        if (queries.empty())
            return {};

        // Single query: skip thread overhead
        if (queries.size() == 1)
            return Search(queries[0], top_k, min_score);

        // Hold one shared lock for the entire operation so m_entries is stable
        // and returned pointers remain valid until the caller is done with them.
        std::shared_lock lock(m_mutex);
        if (m_entries.empty())
            return {};

        // Each async task scores all entries against one query embedding.
        // They read m_entries without locking (parent holds the shared lock).
        // Returns (entry_index, score) pairs that pass min_score.
        std::vector<std::future<std::vector<std::pair<size_t, float>>>> futures;
        futures.reserve(queries.size());
        for (const auto &q : queries)
        {
            futures.push_back(std::async(std::launch::async,
                                         [this, q_copy = q, min_score]() -> std::vector<std::pair<size_t, float>>
                                         {
                                             std::vector<std::pair<size_t, float>> hits;
                                             for (size_t i = 0; i < m_entries.size(); ++i)
                                             {
                                                 float score = CosineSimilarity(q_copy, m_entries[i].embedding);
                                                 if (score >= min_score)
                                                     hits.emplace_back(i, score);
                                             }
                                             return hits;
                                         }));
        }

        // Merge: for each entry index keep the highest score across all queries.
        std::unordered_map<size_t, float> maxScores;
        for (auto &f : futures)
        {
            for (auto &[idx, score] : f.get())
            {
                auto [it, inserted] = maxScores.emplace(idx, score);
                if (!inserted && score > it->second)
                    it->second = score;
            }
        }

        std::vector<SearchResult> out;
        out.reserve(maxScores.size());
        for (auto &[idx, score] : maxScores)
            out.push_back({&m_entries[idx], score});

        int keep = std::min(static_cast<int>(out.size()), top_k);
        std::partial_sort(out.begin(), out.begin() + keep, out.end(),
                          [](const SearchResult &a, const SearchResult &b)
                          { return a.score > b.score; });
        out.resize(keep);

        return out;
    }
} // namespace pagent
