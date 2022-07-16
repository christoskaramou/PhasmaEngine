#include "Core/EventSystem.h"

namespace pe
{
    // PREDEFINED EVENTS ---------------------------------------
    EventID EventQuit = EventID("PE_Quit");
    EventID EventCustom = EventID("PE_Custom");
    EventID EventSetWindowTitle = EventID("PE_SetWindowTitle");
    EventID EventCompileShaders = EventID("PE_CompileShaders");
    EventID EventResize = EventID("PE_Resize");
    EventID EventFileWrite = EventID("PE_FileWrite");
    // ----------------------------------------------------------

    void EventSystem::Init()
    {
        RegisterEvent(EventQuit);
        RegisterEvent(EventCustom);
        RegisterEvent(EventSetWindowTitle);
        RegisterEvent(EventCompileShaders);
        RegisterEvent(EventResize);
        RegisterEvent(EventFileWrite);
    }

    void EventSystem::Destroy()
    {
        ClearEvents();
    }

    void EventSystem::RegisterEvent(EventID event)
    {
        PE_ERROR_IF(m_events.find(event) != m_events.end(), "Event is already registered!");
        m_events[event] = Delegate<std::any>();
    }

    void EventSystem::UnregisterEvent(EventID event)
    {
        PE_ERROR_IF(m_events.find(event) == m_events.end(), "Event not registered!");
        m_events.erase(event);
    }

    void EventSystem::RegisterCallback(EventID event, Func &&func)
    {
        auto it = m_events.find(event);
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");
        it->second += std::forward<Func>(func);
    }

    void EventSystem::UnregisterEventCallback(EventID event, Func &&func)
    {
        auto it = m_events.find(event);
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");
        it->second -= std::forward<Func>(func);
    }

    void EventSystem::DispatchEvent(EventID event, std::any &&data)
    {
        auto it = m_events.find(event);
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");
        it->second.Invoke(std::forward<std::any>(data));
    }

    void EventSystem::ClearEvents()
    {
        m_events.clear();
    }

    void EventSystem::PushEvent(EventID event)
    {
        if (m_pushEvents.find(event) == m_pushEvents.end())
            m_pushEvents.insert(event);
    }

    bool EventSystem::PollEvent(EventID event)
    {
        if (m_pushEvents.find(event) != m_pushEvents.end())
        {
            m_pushEvents.erase(event);
            return true;
        }

        return false;
    }

    void EventSystem::ClearPushedEvents()
    {
        m_pushEvents.clear();
    }
}
