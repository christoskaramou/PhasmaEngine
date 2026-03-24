#include "PhasmaAgent/CodebaseContext.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/IncludeGraph.h"

namespace pagent
{
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
        if (!m_codebaseBM25)
            m_codebaseBM25 = std::make_shared<BM25Index>();
    }

    CodebaseIndexStatus CodebaseContext::GetStatus() const
    {
        CodebaseIndexStatus status;
        status.indexing = m_indexing.load();
        status.ready = m_codebaseBM25 && m_codebaseBM25->Size() > 0;
        return status;
    }

    void CodebaseContext::SetIndexing(bool indexing)
    {
        m_indexing.store(indexing);
    }
} // namespace pagent
