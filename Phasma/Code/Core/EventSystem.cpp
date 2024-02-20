#include "Core/EventSystem.h"

namespace pe
{
    // PREDEFINED EVENTS ---------------------------------------
    EventID EventQuit           = EventID("PE_Quit");
    EventID EventCustom         = EventID("PE_Custom");
    EventID EventSetWindowTitle = EventID("PE_SetWindowTitle");
    EventID EventCompileShaders = EventID("PE_CompileShaders");
    EventID EventResize         = EventID("PE_Resize");
    EventID EventFileWrite      = EventID("PE_FileWrite");
    // ----------------------------------------------------------

    void EventSystem::Init()
    {
        // Register predefined events
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
        // Use emplace for efficient in-place construction
        // inserted is a bool that's true if insertion took place
        auto [it, inserted] = m_events.emplace(event, Delegate<std::any>());
        PE_ERROR_IF(!inserted, "Event is already registered!");
    }

    void EventSystem::RegisterCallback(EventID event, Func &&func)
    {
        auto it = m_events.find(event);

        // Check if the event is registered
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");

        // Attach the callback
        it->second += std::forward<Func>(func);
    }

    void EventSystem::DispatchEvent(EventID event, std::any &&data)
    {
        auto it = m_events.find(event);

        // Check if the event is registered
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");

        // Invoke the delegate associated with this event
        it->second.Invoke(std::forward<std::any>(data));
    }

    void EventSystem::UnregisterEvent(EventID event)
    {
        auto it = m_events.find(event);

        // Check if the event is not registered
        PE_ERROR_IF(it == m_events.end(), "Event not registered!");

        // Remove the event
        m_events.erase(it);
    }

    bool EventSystem::PollEvent(EventID event)
    {
        auto it = m_pushEvents.find(event);

        // Return false if not found
        if (it == m_pushEvents.end())
            return false;

        // Erase the event using the iterator, avoiding another lookup
        m_pushEvents.erase(it);
        return true;
    }

    void EventSystem::PushEvent(EventID event)
    {
        // Use emplace for efficient in-place construction
        m_pushEvents.emplace(event);
    }

    void EventSystem::ClearPushedEvents()
    {
        m_pushEvents.clear();
    }

    void EventSystem::ClearEvents()
    {
        m_events.clear();
    }
}
