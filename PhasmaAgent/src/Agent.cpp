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

    bool Agent::Send(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        return m_impl->Send(user_message, attachments);
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

    void Agent::CancelAfterCurrentRound()
    {
        m_impl->CancelAfterCurrentRound();
    }

    void Agent::ClearHistory()
    {
        m_impl->ClearHistory();
    }

    std::vector<HistoryEntry> Agent::GetHistory() const
    {
        return m_impl->GetHistory();
    }

    void Agent::LoadHistory(const std::vector<HistoryEntry> &entries)
    {
        m_impl->LoadHistory(entries);
    }

    void Agent::InjectSystemMessage(const std::string &content)
    {
        m_impl->InjectSystemMessage(content);
    }

    bool Agent::ForceCompact(size_t keepRecent)
    {
        return m_impl->ForceCompact(keepRecent);
    }

    void Agent::SetModel(const std::string &model)
    {
        m_impl->SetModel(model);
    }

    void Agent::SetRepoMap(const std::string &repoMap)
    {
        m_impl->SetRepoMap(repoMap);
    }

    TokenUsage Agent::GetUsage() const
    {
        return m_impl->GetUsage();
    }

    Provider Agent::GetProvider() const
    {
        return m_impl->GetProvider();
    }

} // namespace pagent
