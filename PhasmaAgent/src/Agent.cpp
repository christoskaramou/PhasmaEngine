#include "PhasmaAgent/Agent.h"
#include "AgentImpl.h"

namespace pagent
{
    Agent::Agent(AgentConfig config)
        : m_impl(std::make_unique<Impl>(std::move(config)))
    {
    }

    Agent::~Agent() = default;

    Agent::Agent(Agent &&) noexcept = default;
    Agent &Agent::operator=(Agent &&) noexcept = default;

    void Agent::RegisterTool(ToolDefinition tool)
    {
        m_impl->RegisterTool(std::move(tool));
    }

    void Agent::UnregisterTool(const std::string &name)
    {
        m_impl->UnregisterTool(name);
    }

    void Agent::ClearTools()
    {
        m_impl->ClearTools();
    }

    bool Agent::Send(const std::string &user_message)
    {
        return m_impl->Send(user_message);
    }

    void Agent::Poll()
    {
        m_impl->Poll();
    }

    void Agent::SetEventCallback(EventCallback callback)
    {
        m_impl->SetEventCallback(std::move(callback));
    }

    bool Agent::IsBusy() const
    {
        return m_impl->IsBusy();
    }

    void Agent::CancelPending()
    {
        m_impl->CancelPending();
    }

    void Agent::ClearHistory()
    {
        m_impl->ClearHistory();
    }

    std::vector<HistoryEntry> Agent::GetHistory() const
    {
        return m_impl->GetHistory();
    }

    void Agent::InjectSystemMessage(const std::string &content)
    {
        m_impl->InjectSystemMessage(content);
    }
} // namespace pagent
