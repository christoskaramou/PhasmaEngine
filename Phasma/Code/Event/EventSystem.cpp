#include "EventSystem.h"

namespace pe
{
    void EventSystem::DispatchEvent(EventType type, const std::any& data)
    {
        if (m_events.find(type) != m_events.end())
            m_events[type].Invoke(data);
    }
    
    void EventSystem::RegisterEvent(EventType type)
    {
        if (m_events.find(type) == m_events.end())
            m_events[type] = Delegate<std::any>();
    }
    
    void EventSystem::UnregisterEvent(EventType type)
    {
        if (m_events.find(type) != m_events.end())
            m_events.erase(type);
    }
    
    void EventSystem::RegisterEventAction(EventType type, Func&& func)
    {
        RegisterEvent(type);
        m_events[type] += std::forward<Func>(func);
    }
    
    void EventSystem::UnregisterEventAction(EventType type, Func&& func)
    {
        if (m_events.find(type) != m_events.end())
            m_events[type] -= std::forward<Func>(func);
    }
    
    void EventSystem::PushEvent(EventType type)
    {
        m_pushedEventTypes.insert(type);
    }
    
    bool EventSystem::PollEvent(EventType type)
    {
        if (m_pushedEventTypes.find(type) != m_pushedEventTypes.end())
        {
            m_pushedEventTypes.erase(type);
            return true;
        }
        
        return false;
    }
    
    void EventSystem::ClearPushedEvents()
    {
        m_pushedEventTypes.clear();
    }
    
    void EventSystem::ClearEvents()
    {
        m_events.clear();
        ClearPushedEvents();
    }
}
