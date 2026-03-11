#include "EventQueue.h"

namespace pagent
{
    void EventQueue::Push(AgentEvent event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(event));
    }

    void EventQueue::Swap(std::vector<AgentEvent> &out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.reserve(out.size() + m_queue.size());
        while (!m_queue.empty())
        {
            out.push_back(std::move(m_queue.front()));
            m_queue.pop();
        }
    }

    void EventQueue::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty())
            m_queue.pop();
    }
} // namespace pagent
