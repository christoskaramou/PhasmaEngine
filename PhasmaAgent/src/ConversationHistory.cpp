#include "ConversationHistory.h"
#include <chrono>

namespace pagent
{
    static uint64_t NowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void ConversationHistory::Append(NeutralMessage msg)
    {
        std::unique_lock lock(m_mutex);
        m_entries.push_back({std::move(msg), NowMs()});
    }

    std::vector<NeutralMessage> ConversationHistory::GetMessages() const
    {
        std::shared_lock lock(m_mutex);
        std::vector<NeutralMessage> out;
        out.reserve(m_entries.size());
        for (const auto &e : m_entries)
            out.push_back(e.message);
        return out;
    }

    std::vector<HistoryEntry> ConversationHistory::GetSnapshot() const
    {
        std::shared_lock lock(m_mutex);
        return m_entries;
    }

    void ConversationHistory::Clear()
    {
        std::unique_lock lock(m_mutex);
        m_entries.clear();
    }

    void ConversationHistory::InjectSystem(const std::string &content)
    {
        std::unique_lock lock(m_mutex);
        NeutralMessage msg;
        msg.role = NeutralMessage::Role::System;
        msg.content = content;

        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
            m_entries.front() = {std::move(msg), NowMs()};
        else
            m_entries.insert(m_entries.begin(), {std::move(msg), NowMs()});
    }
} // namespace pagent
