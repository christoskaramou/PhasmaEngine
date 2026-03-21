#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pagent
{
    class BM25Index;
    class CodebaseIndexer;
    class IncludeGraph;
    class IEmbeddingProvider;
    class VectorStore;

    struct CodebaseIndexingConfig
    {
        std::vector<std::string> directories;
        std::vector<std::string> include_files;
        std::vector<std::string> skip_directories;
        std::vector<std::string> skip_files;
        std::vector<std::string> skip_extensions;
        std::vector<std::string> skip_regex;
    };

    struct CodebaseIndexStatus
    {
        bool loading = false;
        bool checking = false;
        bool ready = false;
        int outdated = 0;
        int total = 0;
        std::vector<std::string> outdatedFiles;
    };

    class CodebaseContext
    {
    public:
        IEmbeddingProvider *GetEmbeddingProvider() const;
        std::shared_ptr<IEmbeddingProvider> GetEmbeddingProviderShared() const;
        void SetEmbeddingProvider(std::shared_ptr<IEmbeddingProvider> provider);

        std::shared_ptr<VectorStore> GetCodebaseStoreShared() const;
        std::shared_ptr<BM25Index> GetCodebaseBM25Shared() const;
        std::shared_ptr<IncludeGraph> GetIncludeGraphShared() const;
        void SetIncludeGraph(std::shared_ptr<IncludeGraph> graph);

        CodebaseIndexingConfig &MutableIndexingConfig();
        const CodebaseIndexingConfig &GetIndexingConfig() const;

        void EnsureStores();
        void SaveStoreToBinary(const std::string &storePath) const;
        void LoadStoreAsync(const std::string &storePath,
                            std::shared_ptr<std::atomic<bool>> alive,
                            std::function<void()> onLoaded = {});
        void CheckStatusAsync(std::shared_ptr<std::atomic<bool>> alive);

        void MarkStatusDirty();
        CodebaseIndexStatus GetStatus() const;

        static std::string SanitizeModelNameForPath(const std::string &modelName);
        static std::string BuildStorePath(const std::string &assetsPath,
                                          const std::string &embeddingModelName);

    private:
        std::shared_ptr<IEmbeddingProvider> m_embeddingProvider;
        std::shared_ptr<VectorStore> m_codebaseStore;
        std::shared_ptr<BM25Index> m_codebaseBM25;
        std::shared_ptr<IncludeGraph> m_includeGraph;
        CodebaseIndexingConfig m_indexingConfig;

        std::atomic<bool> m_loading{false};
        std::atomic<bool> m_statusChecking{false};
        std::atomic<bool> m_statusReady{false};
        std::atomic<int> m_statusOutdated{0};
        std::atomic<int> m_statusTotal{0};
        mutable std::mutex m_statusMutex;
        std::vector<std::string> m_statusFiles;
    };
} // namespace pagent
