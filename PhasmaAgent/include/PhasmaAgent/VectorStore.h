#pragma once

#include <string>
#include <vector>
#include <shared_mutex>

namespace pagent
{
    struct VectorEntry
    {
        std::string id;
        std::string content;  // Original text that was embedded
        std::string metadata; // JSON string with source info
        std::vector<float> embedding;
    };

    class VectorStore
    {
    public:
        struct SearchResult
        {
            const VectorEntry *entry = nullptr;
            float score = 0.0f; // cosine similarity
        };

        void Add(VectorEntry entry);
        void Remove(const std::string &id);
        std::vector<SearchResult> Search(const std::vector<float> &query, int top_k = 5, float min_score = 0.3f) const;

        void SaveToFile(const std::string &path) const;
        void LoadFromFile(const std::string &path);

        size_t Size() const;
        void Clear();

    private:
        static float CosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);

        std::vector<VectorEntry> m_entries;
        mutable std::shared_mutex m_mutex;
    };
} // namespace pagent
