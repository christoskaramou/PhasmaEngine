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

    // Strip image_base64 from all take_screenshot tool results except the most recent one.
    // Screenshots are only needed once — keeping them in history wastes tokens rapidly.
    static void StripOldScreenshotImages(std::vector<NeutralMessage> &messages)
    {
        bool keptRecent = false;
        for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i)
        {
            auto &msg = messages[i];
            if (msg.role != NeutralMessage::Role::Tool || msg.tool_name != "take_screenshot")
                continue;
            if (!keptRecent)
            {
                keptRecent = true; // most recent screenshot: keep image intact
                continue;
            }
            if (msg.tool_result_json.find("\"image_base64\"") != std::string::npos)
                msg.tool_result_json = R"({"note":"[screenshot image removed from history to save tokens]"})";
        }
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

        // No tool_calls - they are stripped
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
            StripOldScreenshotImages(out);
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

        StripOldScreenshotImages(out);
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

    void ConversationHistory::LoadHistory(const std::vector<HistoryEntry> &entries)
    {
        std::unique_lock lock(m_mutex);
        m_entries = entries;
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

    // Compute the boundary between "old" (to be summarized) and "recent" (to keep intact),
    // walking backward to avoid splitting tool-call chains. Returns oldEnd such that
    // entries [start, oldEnd) are the old window and [oldEnd, end) is the recent window.
    // Returns start if there is nothing to summarize (whole history is one chain or too small).
    static size_t ComputeOldEnd(const std::vector<HistoryEntry> &entries, size_t start, size_t keepRecent)
    {
        if (entries.size() - start <= keepRecent)
            return start; // nothing to summarize

        size_t oldEnd = entries.size() - keepRecent;

        // Walk backward to avoid splitting a tool-call chain at the boundary
        while (oldEnd > start)
        {
            const auto role = entries[oldEnd].message.role;
            if (role == NeutralMessage::Role::Tool)
            {
                --oldEnd;
                continue;
            }
            if (role == NeutralMessage::Role::Assistant && !entries[oldEnd].message.tool_calls.empty())
            {
                --oldEnd;
                continue;
            }
            break;
        }
        return oldEnd;
    }

    std::string ConversationHistory::BuildOldMessagesText(size_t keepRecent) const
    {
        std::shared_lock lock(m_mutex);

        size_t start = 0;
        if (!m_entries.empty() && m_entries.front().message.role == NeutralMessage::Role::System)
            start = 1; // skip system message

        size_t oldEnd = ComputeOldEnd(m_entries, start, keepRecent);
        if (oldEnd <= start)
            return {}; // nothing old to summarize
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

            // Prefer tool_result_json for Tool messages, fallback to content
            const std::string &body = (msg.role == NeutralMessage::Role::Tool && !msg.tool_result_json.empty())
                                          ? msg.tool_result_json
                                          : msg.content;
            constexpr size_t kMaxBody = 400;
            if (body.size() > kMaxBody)
                text += body.substr(0, kMaxBody) + "...";
            else
                text += body;
            text += "\n";

            // Include tool call names for assistant messages
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

        size_t oldEnd = ComputeOldEnd(m_entries, start, keepRecent);
        if (oldEnd <= start)
            return; // Nothing to replace (entire history is one tool-call chain)

        // Erase the old window
        m_entries.erase(m_entries.begin() + start, m_entries.begin() + oldEnd);

        // Inject the summary. Avoid creating consecutive user messages:
        // If the first remaining entry is already a User message, prepend the summary
        // to it so no extra message is inserted. Otherwise insert a new User message.
        const std::string summaryText = "[Summary of prior context: " + summary + "]";
        if (m_entries[start].message.role == NeutralMessage::Role::User)
        {
            m_entries[start].message.content = summaryText + "\n\n" + m_entries[start].message.content;
        }
        else
        {
            NeutralMessage summaryMsg;
            summaryMsg.role = NeutralMessage::Role::User;
            summaryMsg.content = summaryText;
            m_entries.insert(m_entries.begin() + start, {std::move(summaryMsg), NowMs()});
        }
    }
} // namespace pagent
