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

    void VectorStore::Add(VectorEntry entry)
    {
        std::unique_lock lock(m_mutex);
        // Replace if same id exists
        for (auto &e : m_entries)
        {
            if (e.id == entry.id)
            {
                e = std::move(entry);
                return;
            }
        }
        m_entries.push_back(std::move(entry));
    }

    void VectorStore::Remove(const std::string &id)
    {
        std::unique_lock lock(m_mutex);
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                           [&id](const VectorEntry &e)
                           { return e.id == id; }),
            m_entries.end());
    }

    void VectorStore::RemoveByMetadataType(const std::string &type)
    {
        std::unique_lock lock(m_mutex);
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                           [&type](const VectorEntry &e)
                           {
                               try
                               {
                                   auto meta = json::parse(e.metadata);
                                   return meta.value("type", "") == type;
                               }
                               catch (...)
                               {
                                   return false;
                               }
                           }),
            m_entries.end());
    }

    void VectorStore::RemoveByFile(const std::string &file)
    {
        std::unique_lock lock(m_mutex);
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                           [&file](const VectorEntry &e)
                           {
                               try
                               {
                                   auto meta = json::parse(e.metadata);
                                   return meta.value("file", "") == file;
                               }
                               catch (...)
                               {
                                   return false;
                               }
                           }),
            m_entries.end());
    }

    std::vector<VectorEntry> VectorStore::ExtractByFile(const std::string &file)
    {
        std::unique_lock lock(m_mutex);
        std::vector<VectorEntry> extracted;
        auto it = std::partition(m_entries.begin(), m_entries.end(),
                                 [&file](const VectorEntry &e)
                                 {
                                     try
                                     {
                                         auto meta = nlohmann::json::parse(e.metadata);
                                         return meta.value("file", "") != file;
                                     }
                                     catch (...)
                                     {
                                         return true;
                                     }
                                 });
        for (auto moveIt = it; moveIt != m_entries.end(); ++moveIt)
            extracted.push_back(std::move(*moveIt));
        m_entries.erase(it, m_entries.end());
        return extracted;
    }

    bool VectorStore::HasFileWithTimestamp(const std::string &file, const std::string &timestamp) const
    {
        std::shared_lock lock(m_mutex);
        for (const auto &e : m_entries)
        {
            try
            {
                auto meta = json::parse(e.metadata);
                if (meta.value("file", "") == file && meta.value("last_modified", "") == timestamp)
                    return true;
            }
            catch (...)
            {
            }
        }
        return false;
    }

    std::unordered_map<std::string, std::string> VectorStore::BuildFileTimestampMap() const
    {
        std::unordered_map<std::string, std::string> timestamps;

        std::shared_lock lock(m_mutex);
        timestamps.reserve(m_entries.size());

        for (const auto &e : m_entries)
        {
            try
            {
                auto meta = json::parse(e.metadata);
                const std::string file = meta.value("file", "");
                const std::string timestamp = meta.value("last_modified", "");
                if (!file.empty() && !timestamp.empty())
                    timestamps[file] = timestamp;
            }
            catch (...)
            {
            }
        }

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

            std::unique_lock lock(m_mutex);
            m_entries.clear();
            m_entries.reserve(arr.size());
            for (const auto &obj : arr)
            {
                VectorEntry e;
                e.id = obj.value("id", "");
                e.content = obj.value("content", "");
                e.metadata = obj.value("metadata", "");
                if (obj.contains("embedding") && obj["embedding"].is_array())
                    e.embedding = obj["embedding"].get<std::vector<float>>();
                m_entries.push_back(std::move(e));
            }

            // Dimension mismatch: clear incompatible vectors
            if (expectedDims > 0 && !m_entries.empty() &&
                static_cast<int>(m_entries[0].embedding.size()) != expectedDims)
            {
                m_entries.clear();
            }
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

        std::unique_lock lock(m_mutex);
        m_entries.clear();
        m_entries.reserve(count);

        for (uint32_t i = 0; i < count && f.good(); ++i)
        {
            VectorEntry e;
            e.id = readStr(f);
            e.content = readStr(f);
            e.metadata = readStr(f);
            e.embedding.resize(dims);
            f.read(reinterpret_cast<char *>(e.embedding.data()), dims * sizeof(float));
            m_entries.push_back(std::move(e));
        }
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
