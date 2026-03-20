#include "AgentWidget.h"
#include "GUI/GUI.h"
#include "PhasmaAgent/AgentUtils.h"
#include "PhasmaAgent/GoogleEmbedding.h"
#include "PhasmaAgent/OpenAIEmbedding.h"
#include "PhasmaAgent/OllamaEmbedding.h"
#include "PhasmaAgent/VectorStore.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/IncludeGraph.h"
#include "PhasmaAgent/CodebaseIndexer.h"

using namespace pagent;

namespace pe
{

    static std::string SanitizeModelName(const std::string &model)
    {
        std::string name = model;
        for (auto &c : name)
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        return name;
    }

    std::shared_ptr<pagent::IEmbeddingProvider> AgentWidget::CreateEmbeddingProvider()
    {
        if (!m_embeddingEnabled || m_embeddingModels.empty())
            return nullptr;

        const std::string &model = m_embeddingModels[m_selectedEmbeddingModel];
        std::string geminiKey = GetEnvOrEmpty("PAGENT_GEMINI_API_KEY");
        std::string openaiKey = GetEnvOrEmpty("PAGENT_OPENAI_API_KEY");

        switch (m_selectedEmbeddingProvider)
        {
        case 0: // Google
            if (!geminiKey.empty())
                return std::make_shared<pagent::GoogleEmbedding>(geminiKey, model, 3072);
            break;
        case 1: // OpenAI
        {
            if (openaiKey.empty())
                break;
            int dims = 1536;
            if (model.find("large") != std::string::npos)
                dims = 3072;
            return std::make_shared<pagent::OpenAIEmbedding>(openaiKey, model, dims);
        }
        case 2: // Ollama
            return std::make_shared<pagent::OllamaEmbedding>(model);
        case 3: // Voyage
        {
            std::string voyageKey = GetEnvOrEmpty("PAGENT_VOYAGE_API_KEY");
            if (voyageKey.empty())
                break;
            return std::make_shared<pagent::OpenAIEmbedding>(voyageKey, model, 1024, "api.voyageai.com", "/v1/embeddings");
        }
        default:
            break;
        }
        return nullptr;
    }

    void AgentWidget::UpdateEmbeddingModels(bool fetchRemote, std::string preferredModel)
    {
        m_embeddingModels.clear();
        m_selectedEmbeddingModel = 0;

        switch (m_selectedEmbeddingProvider)
        {
        case 0: // Google
            m_embeddingModels = {"gemini-embedding-2-preview", "gemini-embedding-001"};
            break;
        case 1: // OpenAI
            m_embeddingModels = {"text-embedding-3-small", "text-embedding-3-large"};
            break;
        case 3: // Voyage
            m_embeddingModels = {"voyage-code-3", "voyage-3"};
            break;
        case 2: // Ollama - local only by default, Fetch button gets remote too
        {
            m_embeddingModelIsLocal.clear();
            m_isVerifyingEmbeddingModel = false;
            m_isFetchingEmbeddingModels = true;
            auto alive = m_alive;
            bool localOnly = !fetchRemote;
            if (localOnly && !preferredModel.empty())
            {
                m_embeddingModels = {preferredModel};
                m_embeddingModelIsLocal = {true}; // optimistic until the local refresh confirms it
                m_selectedEmbeddingModel = 0;
                m_isVerifyingEmbeddingModel = true;
            }
            std::thread([this, alive, localOnly, preferredModel]()
                        {
                auto modelInfos = pagent::Agent::FetchOllamaEmbeddingModels("", localOnly);
                if (!*alive) return;
                QueueAction([this, alive, modelInfos, preferredModel]()
                {
                    m_isFetchingEmbeddingModels = false;
                    m_isVerifyingEmbeddingModel = false;
                    if (m_selectedEmbeddingProvider != 2) return;
                    m_embeddingModels.clear();
                    m_embeddingModelIsLocal.clear();
                    for (const auto &mi : modelInfos)
                    {
                        m_embeddingModels.push_back(mi.name);
                        m_embeddingModelIsLocal.push_back(mi.local);
                    }

                    // Select preferred model (from config), falling back to qwen3-embedding:0.6b,
                    // then index 0 if neither is installed.
                    static constexpr const char *kDefaultEmbModel = "qwen3-embedding:0.6b";
                    auto findModel = [&](const std::string &name) -> int
                    {
                        for (int k = 0; k < static_cast<int>(m_embeddingModels.size()); ++k)
                            if (m_embeddingModels[k] == name)
                                return k;
                        return -1;
                    };
                    // Case 1: Ollama not running or no embedding models installed
                    if (m_embeddingModels.empty())
                    {
                        if (m_embeddingEnabled)
                        {
                            m_embeddingEnabled = false;
                            SaveConfig();
                            std::lock_guard lock(m_chatMutex);
                            m_chat.push_back({ChatMessage::Role::System,
                                "RAG disabled: Ollama is not running or no embedding models are installed.\n"
                                "Start Ollama and run: ollama pull " + std::string(kDefaultEmbModel)});
                            m_scrollToBottom = 3;
                        }
                        return;
                    }

                    int idx = preferredModel.empty() ? -1 : findModel(preferredModel);
                    if (idx < 0)
                        idx = findModel(kDefaultEmbModel);

                    // Case 2: default model not installed — auto-pull it
                    if (idx < 0 && m_embeddingEnabled && !m_isPullingEmbedding)
                    {
                        // Add qwen3-embedding:0.6b as a remote entry so the pull logic can target it
                        m_embeddingModels.push_back(kDefaultEmbModel);
                        m_embeddingModelIsLocal.push_back(false);
                        idx = static_cast<int>(m_embeddingModels.size()) - 1;
                        m_selectedEmbeddingModel = idx;

                        std::string pullModel = kDefaultEmbModel;
                        auto aliveInner = alive;
                        m_isPullingEmbedding = true;
                        {
                            std::lock_guard lock(m_chatMutex);
                            m_chat.push_back({ChatMessage::Role::System,
                                "RAG: embedding model not found locally. Downloading " + pullModel + "..."});
                            m_scrollToBottom = 3;
                        }
                        m_pullEmbeddingCancel = pagent::Agent::PullModel(pullModel,
                            [this, aliveInner](const std::string &status)
                            {
                                if (!*aliveInner) return;
                                QueueAction([this, aliveInner, status]()
                                {
                                    if (!*aliveInner) return;
                                    std::lock_guard lock(m_chatMutex);
                                    if (!m_chat.empty() && m_chat.back().role == ChatMessage::Role::System)
                                        m_chat.back().text = status;
                                    m_scrollToBottom = 3;
                                });
                            },
                            [this, aliveInner, pullModel](bool success)
                            {
                                if (!*aliveInner) return;
                                QueueAction([this, aliveInner, pullModel, success]()
                                {
                                    m_isPullingEmbedding = false;
                                    m_pullEmbeddingCancel.reset();
                                    std::lock_guard lock(m_chatMutex);
                                    if (success)
                                    {
                                        // Mark it local and re-init
                                        for (int k = 0; k < static_cast<int>(m_embeddingModels.size()); ++k)
                                            if (m_embeddingModels[k] == pullModel)
                                                m_embeddingModelIsLocal[k] = true;
                                        ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
                                        m_chat.push_back({ChatMessage::Role::System, "Embedding model ready."});
                                    }
                                    else
                                    {
                                        m_embeddingEnabled = false;
                                        SaveConfig();
                                        m_chat.push_back({ChatMessage::Role::System,
                                            "Failed to download " + pullModel + ". RAG disabled."});
                                    }
                                    m_scrollToBottom = 3;
                                });
                            });
                        return; // provider will be created after successful pull via ConfigureAgent
                    }

                    m_selectedEmbeddingModel = idx >= 0 ? idx : 0;

                    // Embedding provider wasn't created at ConfigureAgent time (models weren't ready).
                    // Create it now and inject into the running agent without recreating it.
                    if (m_embeddingEnabled && !m_embeddingProvider && !m_embeddingModels.empty())
                    {
                        m_embeddingProvider = CreateEmbeddingProvider();
                        if (m_agent.has_value() && m_embeddingProvider)
                            m_agent->SetEmbeddingProvider(m_embeddingProvider);
                        InitCodebaseStore();
                    }
                    // Manual Fetch case: provider already exists, just re-check status.
                    else if (m_embeddingEnabled && m_embeddingProvider && !m_embeddingModels.empty())
                    {
                        CheckIndexStatus();
                    }
                }); })
                .detach();
            break;
        }
        default:
            break;
        }
    }

    // Returns sanitized embedding model name, or empty if not valid for file paths
    std::string AgentWidget::GetEmbeddingModelFileBase() const
    {
        std::string modelName = (m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size()))
                                    ? m_embeddingModels[m_selectedEmbeddingModel]
                                    : "";
        if (modelName.empty() || modelName.find("fetching") != std::string::npos)
            return {};
        return SanitizeModelName(modelName);
    }

    std::string AgentWidget::GetCodebaseStorePath() const
    {
        auto base = GetEmbeddingModelFileBase();
        return base.empty() ? std::string{} : Path::Assets + "Agent/codebase_" + base + ".bin";
    }

    void AgentWidget::CheckIndexStatus()
    {
        if (m_indexStatusChecking.load() || !m_codebaseStore || !m_embeddingProvider)
            return;

        m_indexStatusChecking.store(true);
        m_indexStatusReady.store(false);

        auto store = m_codebaseStore;
        auto embedding = m_embeddingProvider;
        auto dirs = m_indexDirectories;
        auto includeFiles = m_includeFiles;
        auto skipDirs = m_skipDirectories;
        auto skipFiles = m_skipFiles;
        auto skipExts = m_skipExtensions;
        auto skipRegex = m_skipRegex;

        auto aliveRef = m_alive;
        std::thread([this, aliveRef, store, embedding, dirs, includeFiles, skipDirs, skipFiles, skipExts, skipRegex]()
                    {
            pagent::IndexerConfig config;
            config.directories = dirs;
            config.include_files = includeFiles;
            config.skip_directories = skipDirs;
            config.skip_files = skipFiles;
            if (!skipExts.empty())
                config.skip_extensions = skipExts;
            config.skip_regex = skipRegex;

            pagent::CodebaseIndexer indexer(embedding.get(), store.get(), nullptr);
            auto status = indexer.CheckStatus(config);

            if (!*aliveRef)
                return;
            m_indexStatusTotal = status.totalFiles;
            m_indexStatusOutdated = status.needsIndexing;
            m_indexStatusFiles = std::move(status.outdatedFiles);
            m_indexStatusReady.store(true);
            m_indexStatusChecking.store(false); })
            .detach();
    }

    void AgentWidget::InitCodebaseStore()
    {
        if (m_codebaseStore || !m_embeddingProvider)
            return;

        m_codebaseStore = std::make_shared<pagent::VectorStore>();
        m_codebaseBM25 = std::make_shared<pagent::BM25Index>();

        if (m_agent.has_value())
        {
            m_agent->SetCodebaseStore(m_codebaseStore.get());
            m_agent->SetCodebaseBM25(m_codebaseBM25.get());
        }

        auto csp = GetCodebaseStorePath();
        if (csp.empty())
        {
            CheckIndexStatus();
            return;
        }

        m_codebaseLoading.store(true);
        auto store = m_codebaseStore;
        auto bm25 = m_codebaseBM25;
        auto alive = m_alive;
        std::thread([store, bm25, csp, this, alive]()
                    {
            store->LoadFromBinary(csp); // no dim check — filename encodes model name
            store->ForEachEntry([&bm25](const pagent::VectorEntry &e) { bm25->Add(e.id, e.content); });
            if (!*alive) return;
            m_codebaseLoading.store(false);
            PE_INFO("Codebase loaded: %zu vector entries, %zu BM25 docs", store->Size(), bm25->Size());

            // Auto-index on first run (store is empty and indexing dirs are configured)
            if (store->Size() == 0 && !m_indexDirectories.empty() && m_gui)
            {
                QueueAction([this, alive]()
                {
                    if (!*alive) return;
                    if (!m_gui || m_gui->IsIndexing())
                        return;
                    m_gui->StartCodebaseIndexing();
                    m_indexStatusReady.store(false);
                    std::lock_guard lock(m_chatMutex);
                    m_chat.push_back({ChatMessage::Role::System, "RAG: no index found, starting auto-index..."});
                    m_scrollToBottom = 3;
                });
                return; // CheckIndexStatus runs automatically via m_wasIndexing when indexing finishes
            }

            if (!*alive) return;
            CheckIndexStatus(); })
            .detach();
    }

    std::string AgentWidget::BuildRagContext(const std::string &queryText)
    {
        bool hasVectorStore = m_codebaseStore && m_codebaseStore->Size() > 0 && m_embeddingProvider;
        bool hasBM25 = m_codebaseBM25 && m_codebaseBM25->Size() > 0;

        if ((!hasVectorStore && !hasBM25) || queryText.empty())
            return {};

        const int ragTopK = 5;
        const int ragMaxContextChars = 10000;
        const int retrieveK = std::max(ragTopK * 5, 30);
        const float rrfK = 60.0f;

        struct FusedEntry
        {
            std::string content;
            float score = 0.0f;
        };
        std::unordered_map<std::string, FusedEntry> fused;

        if (hasVectorStore)
        {
            auto queryVec = m_embeddingProvider->Embed(queryText);
            if (!queryVec.empty())
            {
                auto vecResults = m_codebaseStore->Search(queryVec, retrieveK, 0.1f);
                for (int rank = 0; rank < static_cast<int>(vecResults.size()); ++rank)
                {
                    const auto &r = vecResults[rank];
                    auto &entry = fused[r.entry->id];
                    entry.content = r.entry->content;
                    entry.score += 1.0f / (rrfK + rank + 1);
                }
            }
        }

        if (hasBM25)
        {
            auto bm25Results = m_codebaseBM25->Search(queryText, retrieveK);
            for (int rank = 0; rank < static_cast<int>(bm25Results.size()); ++rank)
            {
                const auto &r = bm25Results[rank];
                auto &entry = fused[r.id];
                if (entry.content.empty())
                    entry.content = r.content;
                entry.score += 1.0f / (rrfK + rank + 1);
            }
        }

        if (fused.empty())
            return {};

        std::vector<std::pair<std::string, FusedEntry>> sorted(fused.begin(), fused.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b)
                  { return a.second.score > b.second.score; });

        // Collect primary entries
        std::vector<std::string> selectedEntries;
        std::unordered_set<std::string> selectedIds;
        std::unordered_set<std::string> topFiles;

        for (int i = 0; i < static_cast<int>(sorted.size()) && static_cast<int>(selectedEntries.size()) < ragTopK; ++i)
        {
            const std::string &content = sorted[i].second.content;
            selectedEntries.push_back("- " + pagent::SanitizeUTF8(content) + "\n");
            selectedIds.insert(sorted[i].first);
            // Extract file path for include-graph expansion
            if (content.compare(0, 9, "// File: ") == 0)
            {
                auto end = content.find_first_of(" (\n", 9);
                if (end != std::string::npos)
                    topFiles.insert(content.substr(9, end - 9));
            }
        }

        // Include-graph expansion: add neighbor chunks from related files
        if (m_includeGraph && m_includeGraph->Size() > 0 && hasVectorStore && !topFiles.empty())
        {
            int neighborSlots = std::max(1, ragTopK / 3);
            std::unordered_set<std::string> neighborFiles;
            for (const auto &f : topFiles)
                for (const auto &n : m_includeGraph->GetNeighbors(f))
                    if (!topFiles.count(n))
                        neighborFiles.insert(n);

            int added = 0;
            for (int i = 0; i < static_cast<int>(sorted.size()) && added < neighborSlots; ++i)
            {
                if (selectedIds.count(sorted[i].first))
                    continue;
                const std::string &content = sorted[i].second.content;
                if (content.compare(0, 9, "// File: ") != 0)
                    continue;
                auto end = content.find_first_of(" (\n", 9);
                if (end == std::string::npos)
                    continue;
                if (!neighborFiles.count(content.substr(9, end - 9)))
                    continue;
                selectedEntries.push_back("- " + pagent::SanitizeUTF8(content) + "\n");
                selectedIds.insert(sorted[i].first);
                ++added;
            }
        }

        // Build final context string with char limit
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

    std::vector<std::string> AgentWidget::BuildRagFilePaths(const std::string &queryText)
    {
        bool hasVectorStore = m_codebaseStore && m_codebaseStore->Size() > 0 && m_embeddingProvider;
        bool hasBM25 = m_codebaseBM25 && m_codebaseBM25->Size() > 0;

        if ((!hasVectorStore && !hasBM25) || queryText.empty())
            return {};

        const int topK = 5;
        const int retrieveK = std::max(topK * 5, 30);
        const float rrfK = 60.0f;

        struct FusedEntry
        {
            std::string content;
            float score = 0.0f;
        };
        std::unordered_map<std::string, FusedEntry> fused;

        if (hasVectorStore)
        {
            auto queryVec = m_embeddingProvider->Embed(queryText);
            if (!queryVec.empty())
                for (int rank = 0; auto &r : m_codebaseStore->Search(queryVec, retrieveK, 0.1f))
                {
                    auto &e = fused[r.entry->id];
                    e.content = r.entry->content;
                    e.score += 1.0f / (rrfK + rank++ + 1);
                }
        }
        if (hasBM25)
            for (int rank = 0; auto &r : m_codebaseBM25->Search(queryText, retrieveK))
            {
                auto &e = fused[r.id];
                if (e.content.empty())
                    e.content = r.content;
                e.score += 1.0f / (rrfK + rank++ + 1);
            }

        std::vector<std::pair<std::string, FusedEntry>> sorted(fused.begin(), fused.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b)
                  { return a.second.score > b.second.score; });

        // Extract unique file paths from top results (parse "// File: path" prefix)
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for (int i = 0; i < static_cast<int>(sorted.size()) && static_cast<int>(paths.size()) < topK; ++i)
        {
            const auto &content = sorted[i].second.content;
            if (content.compare(0, 9, "// File: ") != 0)
                continue;
            auto end = content.find_first_of(" (\n", 9);
            if (end == std::string::npos)
                continue;
            std::string path = content.substr(9, end - 9);
            if (seen.insert(path).second)
                paths.push_back(std::move(path));
        }
        return paths;
    }

} // namespace pe
