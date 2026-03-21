#pragma once

#include <memory>
#include <string>
#include <vector>

namespace pagent
{
    class BM25Index;
    class IncludeGraph;
    class IEmbeddingProvider;
    class VectorStore;

    struct RagQueryContext
    {
        std::shared_ptr<IEmbeddingProvider> embeddingProvider;
        std::shared_ptr<VectorStore> codebaseStore;
        std::shared_ptr<BM25Index> codebaseBM25;
        std::shared_ptr<IncludeGraph> includeGraph;
    };

    std::string BuildRagContext(const RagQueryContext &context,
                                const std::string &queryText,
                                int ragTopK = 5,
                                int ragMaxContextChars = 10000);

    std::vector<std::string> BuildRagFilePaths(const RagQueryContext &context,
                                               const std::string &queryText,
                                               int topK = 5);
} // namespace pagent
