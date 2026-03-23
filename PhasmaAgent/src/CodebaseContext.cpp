#include "PhasmaAgent/CodebaseContext.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/CodebaseIndexer.h"
#include "PhasmaAgent/IncludeGraph.h"
#include "PhasmaAgent/VectorStore.h"

#include <thread>

namespace pagent
{
    namespace
    {
        IndexerConfig BuildIndexerConfig(const CodebaseIndexingConfig &config)
        {
            IndexerConfig indexerConfig;
            indexerConfig.directories = config.directories;
            indexerConfig.include_files = config.include_files;
            indexerConfig.skip_directories = config.skip_directories;
            indexerConfig.skip_files = config.skip_files;
            indexerConfig.skip_extensions = config.skip_extensions;
            indexerConfig.skip_regex = config.skip_regex;
            return indexerConfig;
        }
    } // namespace

    IEmbeddingProvider *CodebaseContext::GetEmbeddingProvider() const
    {
        return m_embeddingProvider.get();
    }

    std::shared_ptr<IEmbeddingProvider> CodebaseContext::GetEmbeddingProviderShared() const
    {
        return m_embeddingProvider;
    }

    void CodebaseContext::SetEmbeddingProvider(std::shared_ptr<IEmbeddingProvider> provider)
    {
        m_embeddingProvider = std::move(provider);
    }

    std::shared_ptr<VectorStore> CodebaseContext::GetCodebaseStoreShared() const
    {
        return m_codebaseStore;
    }

    std::shared_ptr<BM25Index> CodebaseContext::GetCodebaseBM25Shared() const
    {
        return m_codebaseBM25;
    }

    std::shared_ptr<IncludeGraph> CodebaseContext::GetIncludeGraphShared() const
    {
        return m_includeGraph;
    }

    void CodebaseContext::SetIncludeGraph(std::shared_ptr<IncludeGraph> graph)
    {
        m_includeGraph = std::move(graph);
    }

    CodebaseIndexingConfig &CodebaseContext::MutableIndexingConfig()
    {
        return m_indexingConfig;
    }

    const CodebaseIndexingConfig &CodebaseContext::GetIndexingConfig() const
    {
        return m_indexingConfig;
    }

    void CodebaseContext::EnsureStores()
    {
        if (!m_codebaseStore)
            m_codebaseStore = std::make_shared<VectorStore>();
        if (!m_codebaseBM25)
            m_codebaseBM25 = std::make_shared<BM25Index>();
    }

    void CodebaseContext::SaveStoreToBinary(const std::string &storePath) const
    {
        if (m_codebaseStore && !storePath.empty())
            m_codebaseStore->SaveToBinary(storePath);
    }

    void CodebaseContext::LoadStoreAsync(const std::string &storePath,
                                         std::shared_ptr<std::atomic<bool>> alive,
                                         std::function<void()> onLoaded)
    {
        EnsureStores();
        m_loading.store(true);

        auto store = m_codebaseStore;
        auto bm25 = m_codebaseBM25;
        std::thread([store, bm25, storePath, alive, this, onLoaded = std::move(onLoaded)]()
                    {
            if (!storePath.empty())
                store->LoadFromBinary(storePath);

            std::vector<std::pair<std::string, std::string>> documents;
            documents.reserve(store->Size());
            store->ForEachEntry([&documents](const VectorEntry &entry)
            {
                documents.emplace_back(entry.id, entry.content);
            });
            bm25->Rebuild(std::move(documents));

            if (!*alive)
                return;

            m_loading.store(false);
            if (onLoaded)
                onLoaded(); })
            .detach();
    }

    void CodebaseContext::CheckStatusAsync(std::shared_ptr<std::atomic<bool>> alive)
    {
        if (m_statusChecking.load() || !m_codebaseStore)
            return;

        m_statusChecking.store(true);
        m_statusReady.store(false);

        auto store = m_codebaseStore;
        auto embeddingProvider = m_embeddingProvider;
        auto config = m_indexingConfig;
        std::thread([store, embeddingProvider, config, alive, this]()
                    {
            CodebaseIndexer indexer(embeddingProvider.get(), store.get(), nullptr);
            auto status = indexer.CheckStatus(BuildIndexerConfig(config));

            if (!*alive)
                return;

            {
                std::lock_guard lock(m_statusMutex);
                m_statusFiles = std::move(status.outdatedFiles);
            }
            m_statusTotal.store(status.totalFiles);
            m_statusOutdated.store(status.needsIndexing);
            m_statusReady.store(true);
            m_statusChecking.store(false); })
            .detach();
    }

    void CodebaseContext::MarkStatusDirty()
    {
        m_statusReady.store(false);
    }

    CodebaseIndexStatus CodebaseContext::GetStatus() const
    {
        CodebaseIndexStatus status;
        status.loading = m_loading.load();
        status.checking = m_statusChecking.load();
        status.ready = m_statusReady.load();
        status.outdated = m_statusOutdated.load();
        status.total = m_statusTotal.load();
        {
            std::lock_guard lock(m_statusMutex);
            status.outdatedFiles = m_statusFiles;
        }
        return status;
    }

    std::string CodebaseContext::SanitizeModelNameForPath(const std::string &modelName)
    {
        std::string sanitized = modelName;
        for (auto &c : sanitized)
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        return sanitized;
    }

    std::string CodebaseContext::BuildStorePath(const std::string &assetsPath,
                                                const std::string &embeddingModelName)
    {
        if (embeddingModelName.empty() || embeddingModelName.find("fetching") != std::string::npos)
            return {};
        return assetsPath + "Agent/codebase_" + SanitizeModelNameForPath(embeddingModelName) + ".bin";
    }
} // namespace pagent
