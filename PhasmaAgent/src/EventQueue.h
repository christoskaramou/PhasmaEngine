#pragma once

#include "PhasmaAgent/Agent.h"
#include <mutex>
#include <vector>

namespace pagent
{
    class EventQueue
    {
    public:
        void Push(AgentEvent event);
        void Swap(std::vector<AgentEvent> &out);
        void Clear();

    private:
        std::vector<AgentEvent> m_events;
        std::mutex m_mutex;
    };
} // namespace pagent
