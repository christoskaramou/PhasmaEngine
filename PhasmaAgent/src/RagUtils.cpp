#include "PhasmaAgent/RagUtils.h"
#include "PhasmaAgent/Agent.h"
#include "PhasmaAgent/AgentUtils.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/IncludeGraph.h"
#include "PhasmaAgent/VectorStore.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace pagent
{
    namespace
    {
        struct FusedEntry
        {
            std::string id;
            std::string content;
            float score = 0.0f;
        };

        static std::vector<FusedEntry> BuildFusedResults(const RagQueryContext &context,
                                                         const std::string &queryText,
                                                         const int retrieveK)
        {
            const bool hasVectorStore = context.codebaseStore && context.codebaseStore->Size() > 0 && context.embeddingProvider;
            const bool hasBM25 = context.codebaseBM25 && context.codebaseBM25->Size() > 0;
            if ((!hasVectorStore && !hasBM25) || queryText.empty())
                return {};

            constexpr float rrfK = 60.0f;
            std::unordered_map<std::string, FusedEntry> fused;

            if (hasVectorStore)
            {
                auto queryVec = context.embeddingProvider->Embed(queryText);
                if (!queryVec.empty())
                {
                    auto vecResults = context.codebaseStore->Search(queryVec, retrieveK, 0.1f);
                    for (int rank = 0; rank < static_cast<int>(vecResults.size()); ++rank)
                    {
                        const auto &result = vecResults[rank];
                        auto &entry = fused[result.entry->id];
                        entry.id = result.entry->id;
                        entry.content = result.entry->content;
                        entry.score += 1.0f / (rrfK + rank + 1);
                    }
                }
            }

            if (hasBM25)
            {
                auto bm25Results = context.codebaseBM25->Search(queryText, retrieveK);
                for (int rank = 0; rank < static_cast<int>(bm25Results.size()); ++rank)
                {
                    const auto &result = bm25Results[rank];
                    auto &entry = fused[result.id];
                    entry.id = result.id;
                    if (entry.content.empty())
                        entry.content = result.content;
                    entry.score += 1.0f / (rrfK + rank + 1);
                }
            }

            std::vector<FusedEntry> sorted;
            sorted.reserve(fused.size());
            for (auto &[id, entry] : fused)
                sorted.push_back(std::move(entry));

            std::sort(sorted.begin(), sorted.end(),
                      [](const FusedEntry &a, const FusedEntry &b)
                      { return a.score > b.score; });
            return sorted;
        }

        static std::string ExtractFilePathFromContent(const std::string &content)
        {
            if (content.compare(0, 9, "// File: ") != 0)
                return {};
            auto end = content.find_first_of(" (\n", 9);
            if (end == std::string::npos)
                return {};
            return content.substr(9, end - 9);
        }
    } // namespace

    std::string BuildRagContext(const RagQueryContext &context,
                                const std::string &queryText,
                                const int ragTopK,
                                const int ragMaxContextChars)
    {
        const auto sorted = BuildFusedResults(context, queryText, std::max(ragTopK * 5, 30));
        if (sorted.empty())
            return {};

        std::vector<std::string> selectedEntries;
        std::unordered_set<std::string> selectedIds;
        std::unordered_set<std::string> topFiles;

        for (int i = 0; i < static_cast<int>(sorted.size()) && static_cast<int>(selectedEntries.size()) < ragTopK; ++i)
        {
            selectedEntries.push_back("- " + SanitizeUTF8(sorted[i].content) + "\n");
            selectedIds.insert(sorted[i].id);
            const std::string file = ExtractFilePathFromContent(sorted[i].content);
            if (!file.empty())
                topFiles.insert(file);
        }

        if (context.includeGraph && context.includeGraph->Size() > 0 && context.codebaseStore && context.embeddingProvider && !topFiles.empty())
        {
            const int neighborSlots = std::max(1, ragTopK / 3);
            std::unordered_set<std::string> neighborFiles;
            for (const auto &file : topFiles)
            {
                for (const auto &neighbor : context.includeGraph->GetNeighbors(file))
                    if (!topFiles.count(neighbor))
                        neighborFiles.insert(neighbor);
            }

            int added = 0;
            for (int i = 0; i < static_cast<int>(sorted.size()) && added < neighborSlots; ++i)
            {
                if (selectedIds.count(sorted[i].id))
                    continue;
                const std::string file = ExtractFilePathFromContent(sorted[i].content);
                if (file.empty() || !neighborFiles.count(file))
                    continue;
                selectedEntries.push_back("- " + SanitizeUTF8(sorted[i].content) + "\n");
                selectedIds.insert(sorted[i].id);
                ++added;
            }
        }

        std::string ragContext = "\n[Relevant codebase context:\n";
        int totalChars = 0;
        int injected = 0;
        for (const auto &entry : selectedEntries)
        {
            if (totalChars + static_cast<int>(entry.size()) > ragMaxContextChars)
                break;
            ragContext += entry;
            totalChars += static_cast<int>(entry.size());
            ++injected;
        }
        ragContext += "]\n";
        return injected > 0 ? ragContext : std::string{};
    }

    std::vector<std::string> BuildRagFilePaths(const RagQueryContext &context,
                                               const std::string &queryText,
                                               const int topK)
    {
        const auto sorted = BuildFusedResults(context, queryText, std::max(topK * 5, 30));
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for (int i = 0; i < static_cast<int>(sorted.size()) && static_cast<int>(paths.size()) < topK; ++i)
        {
            const std::string path = ExtractFilePathFromContent(sorted[i].content);
            if (path.empty() || !seen.insert(path).second)
                continue;
            paths.push_back(path);
        }
        return paths;
    }
} // namespace pagent
