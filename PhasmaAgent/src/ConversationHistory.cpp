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

    // Compact an assistant message from the old window: strip tool_calls, keep text + summary
    static NeutralMessage CompactAssistant(const NeutralMessage &msg)
    {
        NeutralMessage compacted;
        compacted.role = NeutralMessage::Role::Assistant;

        // Build a short summary of tool usage
        std::string summary;
        if (!msg.tool_calls.empty())
        {
            summary = "[used: ";
            for (size_t i = 0; i < msg.tool_calls.size(); ++i)
            {
                if (i > 0)
                    summary += ", ";
                summary += msg.tool_calls[i].name;
            }
            summary += "]";
        }

        // Keep text content (truncated) + tool summary
        if (!msg.content.empty())
        {
            constexpr size_t maxTextLen = 200;
            if (msg.content.size() > maxTextLen)
                compacted.content = msg.content.substr(0, maxTextLen) + "...";
            else
                compacted.content = msg.content;
            if (!summary.empty())
                compacted.content += " " + summary;
        }
        else
        {
            compacted.content = summary;
        }

        // No tool_calls — they are stripped
        return compacted;
    }

    std::vector<NeutralMessage> ConversationHistory::GetMessages(int maxMessages) const
    {
        std::shared_lock lock(m_mutex);

        // Separate system message
        std::vector<NeutralMessage> out;
        size_t start = 0;
        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
        {
            out.push_back(m_entries.front().message);
            start = 1;
        }

        size_t total = m_entries.size() - start;

        // If history fits within limit, return as-is
        if (maxMessages <= 0 || static_cast<int>(total) <= maxMessages)
        {
            for (size_t i = start; i < m_entries.size(); ++i)
                out.push_back(m_entries[i].message);
            return out;
        }

        // Split into old (compacted) and recent (intact) windows.
        // Recent window: last recentCount messages, always kept intact.
        const size_t recentCount = std::min(total, static_cast<size_t>(maxMessages / 2));
        size_t recentStart = m_entries.size() - recentCount;

        // Walk recentStart backward to keep tool-call chains intact.
        // The OpenAI/Gemini API requires tool result messages to always follow
        // the assistant message that initiated the tool calls. Splitting a chain
        // between old (compacted) and recent (intact) windows drops both sides.
        while (recentStart > start)
        {
            if (m_entries[recentStart].message.role == NeutralMessage::Role::Tool)
            {
                --recentStart;
                continue;
            }
            if (m_entries[recentStart].message.role == NeutralMessage::Role::Assistant &&
                !m_entries[recentStart].message.tool_calls.empty())
            {
                --recentStart;
                continue;
            }
            break;
        }

        // Old window: everything between start and recentStart, compacted
        for (size_t i = start; i < recentStart; ++i)
        {
            const auto &msg = m_entries[i].message;

            if (msg.role == NeutralMessage::Role::Tool)
                continue; // Drop old tool results entirely

            if (msg.role == NeutralMessage::Role::Assistant)
            {
                auto compacted = CompactAssistant(msg);
                if (!compacted.content.empty())
                    out.push_back(std::move(compacted));
                continue;
            }

            // User messages: keep as-is
            out.push_back(msg);
        }

        // Recent window: intact (tool-call chains are guaranteed complete)
        for (size_t i = recentStart; i < m_entries.size(); ++i)
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

    size_t ConversationHistory::EntryCount() const
    {
        std::shared_lock lock(m_mutex);
        return m_entries.size();
    }

    std::string ConversationHistory::BuildOldMessagesText(size_t keepRecent) const
    {
        std::shared_lock lock(m_mutex);

        size_t start = 0;
        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
            start = 1; // skip system message

        if (m_entries.size() - start <= keepRecent)
            return {}; // nothing old to summarize

        size_t oldEnd = m_entries.size() - keepRecent;
        std::string text;
        for (size_t i = start; i < oldEnd; ++i)
        {
            const auto &msg = m_entries[i].message;
            const char *role = "user";
            if (msg.role == NeutralMessage::Role::Assistant)
                role = "assistant";
            else if (msg.role == NeutralMessage::Role::Tool)
                role = "tool";

            text += role;
            text += ": ";
            if (msg.content.size() > 300)
                text += msg.content.substr(0, 300) + "...";
            else
                text += msg.content;
            text += "\n";

            // Include tool call names
            for (const auto &tc : msg.tool_calls)
                text += "  [called " + tc.name + "]\n";
        }
        return text;
    }

    void ConversationHistory::ReplaceOldWithSummary(const std::string &summary, size_t keepRecent)
    {
        std::unique_lock lock(m_mutex);

        size_t start = 0;
        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
            start = 1;

        if (m_entries.size() - start <= keepRecent)
            return;

        size_t oldEnd = m_entries.size() - keepRecent;

        // Build the summary message
        NeutralMessage summaryMsg;
        summaryMsg.role = NeutralMessage::Role::User;
        summaryMsg.content = "[Conversation summary: " + summary + "]";

        // Erase old entries and insert summary
        m_entries.erase(m_entries.begin() + start, m_entries.begin() + oldEnd);
        m_entries.insert(m_entries.begin() + start, {std::move(summaryMsg), NowMs()});
    }
} // namespace pagent
