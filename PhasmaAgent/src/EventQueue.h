#pragma once

#include "PhasmaAgent/Agent.h"
#include <queue>
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
        std::queue<AgentEvent> m_queue;
        std::mutex m_mutex;
    };
} // namespace pagent
