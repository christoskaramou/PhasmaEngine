#pragma once

#include "../Core/Base.h"
#include "Delegate.h"
#include <map>
#include <deque>
#include <utility>
#include <any>
#include <unordered_set>

namespace pe
{
    // An example of a class member function register called AClass::DoSomething(T t)
    //      auto lambda = [this](const std::any& x) { DoSomething(std::any_cast<T>(x)); };
    //      EventSystem::Get()->RegisterEventAction(EventType::X, lambda)
    //
    // Later somewhere else
    //      EventSystem::Get()->DispatchEvent(EventType::X, data or std::any(data));
    
    enum class EventType
    {
        Custom,
        SetWindowTitle,
        CompileShaders,
        ScaleRenderTargets
    };
    
    using Func = Delegate<std::any>::Func_type;
    
    class EventSystem : public NoCopy, public NoMove
    {
    public:
        // Immediately dispatch a registered event
        void DispatchEvent(EventType type, const std::any& data);
        
        void RegisterEvent(EventType type);
        
        void UnregisterEvent(EventType type);
        
        void RegisterEventAction(EventType type, Func&& func);
        
        void UnregisterEventAction(EventType type, Func&& func);
        
        void PushEvent(EventType type);
        
        bool PollEvent(EventType type);
        
        void ClearPushedEvents();
        
        void ClearEvents();
    
    private:
        std::unordered_map<EventType, Delegate<std::any>> m_events;
        std::unordered_set<EventType> m_pushedEventTypes;
    
    public:
        static auto Get()
        {
            static auto instance = new EventSystem();
            return instance;
        }
        
        EventSystem(EventSystem const&) = delete;                // copy constructor
        EventSystem(EventSystem&&) noexcept = delete;            // move constructor
        EventSystem& operator=(EventSystem const&) = delete;    // copy assignment
        EventSystem& operator=(EventSystem&&) = delete;            // move assignment
        ~EventSystem() = default;                                // destructor
    private:
        EventSystem() = default;                                // default constructor
    };
}
