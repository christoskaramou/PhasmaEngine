#include "AgentImpl.h"

namespace pagent
{
    Agent::Impl::Impl(AgentConfig config)
        : m_config(std::move(config))
    {
        if (m_config.custom_backend)
        {
            m_backend = m_config.custom_backend;
            m_config.custom_backend.reset();
        }
        else if (m_config.provider == Provider::Anthropic)
        {
            m_backend = std::make_unique<AnthropicBackend>();
        }
        else if (m_config.provider == Provider::Google)
        {
            m_backend = std::make_unique<GoogleBackend>();
        }
        else if (m_config.provider == Provider::GoogleVertex)
        {
            auto gb = std::make_unique<GoogleBackend>();
            gb->ConfigureVertex(m_config.vertex_project_id, m_config.vertex_location);
            // Set base_url so RequestWorker hits the right Vertex AI regional host
            if (m_config.base_url.empty() && !m_config.vertex_location.empty())
                m_config.base_url = "https://" + m_config.vertex_location + "-aiplatform.googleapis.com";
            m_backend = std::move(gb);
        }
        else
        {
            m_backend = std::make_unique<OpenAIBackend>();
        }

        m_worker = std::make_unique<RequestWorker>(
            m_config,
            m_backend.get(),
            m_history,
            m_toolRegistry,
            m_eventQueue);

        if (m_config.provider == Provider::Ollama)
            m_ollamaProcess.Start(m_config.model);
    }

    void Agent::Impl::RegisterTool(ToolDefinition tool)
    {
        m_toolRegistry.Register(std::move(tool));
    }

    void Agent::Impl::UnregisterTool(const std::string &name)
    {
        m_toolRegistry.Unregister(name);
    }

    void Agent::Impl::ClearTools()
    {
        m_toolRegistry.Clear();
    }

    bool Agent::Impl::Send(const std::string &user_message)
    {
        return m_worker->Submit(user_message);
    }

    bool Agent::Impl::Send(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        return m_worker->Submit(user_message, attachments);
    }

    void Agent::Impl::Poll()
    {
        std::vector<AgentEvent> events;
        m_eventQueue.Swap(events);

        Agent::EventCallback cb;
        {
            std::lock_guard lock(m_callbackMutex);
            cb = m_eventCallback;
        }

        for (const auto &ev : events)
        {
            if (ev.type == AgentEventType::Usage)
            {
                m_usage.totalInput += ev.input_tokens;
                m_usage.totalOutput += ev.output_tokens;
                m_usage.totalCacheRead += ev.cache_read_tokens;
                m_usage.totalCacheWrite += ev.cache_creation_tokens;
            }
            if (cb)
                cb(ev);
        }
    }

    void Agent::Impl::SetEventCallback(Agent::EventCallback cb)
    {
        std::lock_guard lock(m_callbackMutex);
        m_eventCallback = std::move(cb);
    }

    bool Agent::Impl::IsBusy() const
    {
        return m_worker->IsBusy();
    }

    void Agent::Impl::CancelPending()
    {
        m_worker->Cancel();
    }

    void Agent::Impl::CancelAfterCurrentRound()
    {
        m_worker->CancelAfterCurrentRound();
    }

    void Agent::Impl::ClearHistory()
    {
        m_history.Clear();
    }

    std::vector<HistoryEntry> Agent::Impl::GetHistory() const
    {
        return m_history.GetSnapshot();
    }

    void Agent::Impl::LoadHistory(const std::vector<HistoryEntry> &entries)
    {
        m_history.LoadHistory(entries);
    }

    void Agent::Impl::InjectSystemMessage(const std::string &content)
    {
        m_history.InjectSystem(content);
    }

    TokenUsage Agent::Impl::GetUsage() const
    {
        return m_usage;
    }

    void Agent::Impl::SetModel(const std::string &model)
    {
        m_config.model = model;
        if (m_config.provider == Provider::Ollama)
            m_ollamaProcess.Start(model);
    }

    void Agent::Impl::SetRepoMap(const std::string &repoMap)
    {
        m_config.repo_map = repoMap;
    }

    bool Agent::Impl::ForceCompact(size_t keepRecent)
    {
        return m_worker->ForceCompact(keepRecent);
    }

} // namespace pagent
