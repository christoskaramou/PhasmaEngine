#include "ConversationHistory.h"
#include <chrono>
#include <mutex>
#include <shared_mutex>

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

    std::vector<NeutralMessage> ConversationHistory::GetMessages(int maxMessages) const
    {
        std::shared_lock lock(m_mutex);

        if (maxMessages <= 0 || static_cast<int>(m_entries.size()) <= maxMessages)
        {
            std::vector<NeutralMessage> out;
            out.reserve(m_entries.size());
            for (const auto &e : m_entries)
                out.push_back(e.message);
            return out;
        }

        // Always keep the system message (first entry) if present, then take last N
        std::vector<NeutralMessage> out;
        size_t start = 0;
        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
        {
            out.push_back(m_entries.front().message);
            start = 1;
            --maxMessages; // reserve one slot for system
        }

        int available = static_cast<int>(m_entries.size()) - static_cast<int>(start);
        size_t trimStart = start + std::max(0, available - maxMessages);
        for (size_t i = trimStart; i < m_entries.size(); ++i)
            out.push_back(m_entries[i].message);

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
