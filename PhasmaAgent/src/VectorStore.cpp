#include "PhasmaAgent/VectorStore.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>

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
            f << arr.dump();
    }

    void VectorStore::LoadFromFile(const std::string &path)
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
        }
        catch (...)
        {
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
} // namespace pagent
