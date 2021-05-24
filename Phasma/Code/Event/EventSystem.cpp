/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
