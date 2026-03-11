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
        std::vector<NeutralMessage> GetMessages() const;
        std::vector<HistoryEntry> GetSnapshot() const;
        void Clear();
        void InjectSystem(const std::string &content);

    private:
        std::vector<HistoryEntry> m_entries;
        mutable std::shared_mutex m_mutex;
    };
} // namespace pagent
