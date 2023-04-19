#pragma once

namespace pe
{
    class EventSystem
    {
    public:
        using Func = Delegate<std::any>::Func_type;
        
        static void Init();

        static void Destroy();

        // DISPATCHING EVENTS ----------------------------------
        static void RegisterEvent(EventID event);

        static void UnregisterEvent(EventID type);

        static void RegisterCallback(EventID type, Func &&func);
        
        static void DispatchEvent(EventID type, std::any &&data);

        static void ClearEvents();
        // -----------------------------------------------------

        // POLLING EVENTS --------------------------------------
        // Push event have to be polled manually
        static void PushEvent(EventID type);

        // Poll a pushed event
        static bool PollEvent(EventID type);

        static void ClearPushedEvents();
        // -----------------------------------------------------

    private:
        inline static std::unordered_map<size_t, Delegate<std::any>> m_events{};
        inline static std::unordered_set<size_t> m_pushEvents{};
    };
}
