#pragma once

namespace pe
{
    class EventSystem
    {
    public:
        using Func = Delegate<std::any>::FunctionType;
        
        static void Init();
        static void Destroy();

        // DISPATCHING EVENTS ----------------------------------
        static void RegisterEvent(size_t event);
        static void UnregisterEvent(size_t event);
        static void RegisterCallback(size_t event, Func &&func);
        static void DispatchEvent(size_t event, std::any &&data);
        static void ClearEvents();
        // -----------------------------------------------------

        // POLLING EVENTS --------------------------------------
        // Push event have to be polled manually
        static void PushEvent(size_t event);
        // Poll a pushed event
        static bool PollEvent(size_t event);
        static void ClearPushedEvents();
        // -----------------------------------------------------

    private:
        inline static std::unordered_map<size_t, Delegate<std::any>> m_events{};
        inline static std::unordered_set<size_t> m_pushEvents{};
    };
}
