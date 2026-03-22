#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
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
        void RemoveByMetadataType(const std::string &type);
        void RemoveByFile(const std::string &file);

        // Remove and return all entries matching a file. Used for content-hash reuse during re-indexing.
        std::vector<VectorEntry> ExtractByFile(const std::string &file);

        bool HasFileWithTimestamp(const std::string &file, const std::string &timestamp) const;
        std::unordered_map<std::string, std::string> BuildFileTimestampMap() const;
        std::vector<SearchResult> Search(const std::vector<float> &query, int top_k = 5, float min_score = 0.3f) const;

        // Search with multiple embedding queries in parallel. Each entry's score is the
        // maximum across all queries. Returns top_k unique results sorted by merged score.
        // Takes a single shared lock for the whole operation so returned pointers are stable.
        std::vector<SearchResult> SearchMulti(const std::vector<std::vector<float>> &queries, int top_k = 5, float min_score = 0.3f) const;

        void SaveToFile(const std::string &path) const;
        void SaveToBinary(const std::string &path) const;
        void LoadFromFile(const std::string &path);
        void LoadFromFile(const std::string &path, int expectedDims);
        void LoadFromBinary(const std::string &path);
        void LoadFromBinary(const std::string &path, int expectedDims);

        size_t Size() const;
        void Clear();

        // Iterate all entries (thread-safe, holds shared lock for the duration)
        void ForEachEntry(const std::function<void(const VectorEntry &)> &fn) const;

    private:
        static float CosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);

        std::vector<VectorEntry> m_entries;
        mutable std::shared_mutex m_mutex;
    };
} // namespace pagent
