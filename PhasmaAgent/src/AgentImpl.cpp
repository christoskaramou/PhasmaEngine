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

    void Agent::Impl::Poll()
    {
        std::vector<AgentEvent> events;
        m_eventQueue.Swap(events);

        Agent::EventCallback cb;
        {
            std::lock_guard lock(m_callbackMutex);
            cb = m_eventCallback;
        }

        if (cb)
        {
            for (const auto &ev : events)
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

    void Agent::Impl::ClearHistory()
    {
        m_history.Clear();
    }

    std::vector<HistoryEntry> Agent::Impl::GetHistory() const
    {
        return m_history.GetSnapshot();
    }

    void Agent::Impl::InjectSystemMessage(const std::string &content)
    {
        m_history.InjectSystem(content);
    }
} // namespace pagent
