#include "EventQueue.h"

namespace pagent
{
    void EventQueue::Push(AgentEvent event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(std::move(event));
    }

    void EventQueue::Swap(std::vector<AgentEvent> &out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.insert(out.end(),
                   std::make_move_iterator(m_events.begin()),
                   std::make_move_iterator(m_events.end()));
        m_events.clear();
    }

    void EventQueue::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.clear();
    }
} // namespace pagent
