#pragma once

#include "PhasmaAgent/Agent.h"
#include <vector>
#include <shared_mutex>

namespace pagent
{
    class ConversationHistory
    {
    public:
        void Append(NeutralMessage msg);
        std::vector<NeutralMessage> GetMessages(int maxMessages = 0) const;
        std::vector<HistoryEntry> GetSnapshot() const;
        void Clear();
        void InjectSystem(const std::string &content);
        // Replay a saved history snapshot (used for session restore)
        void LoadHistory(const std::vector<HistoryEntry> &entries);
        size_t EntryCount() const;
        // Replace all entries before the last keepRecent with a single summary message
        void ReplaceOldWithSummary(const std::string &summary, size_t keepRecent);
        // Build a text block from old messages for summarization
        std::string BuildOldMessagesText(size_t keepRecent) const;

    private:
        std::vector<HistoryEntry> m_entries;
        mutable std::shared_mutex m_mutex;
    };
} // namespace pagent
