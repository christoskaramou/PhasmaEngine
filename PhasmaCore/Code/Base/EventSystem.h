#pragma once
#include "Base/Delegate.h"

namespace pe
{
    enum class EventType : size_t
    {
        Quit,
        Custom,
        SetWindowTitle,
        CompileShaders,
        CompileScripts,
        Resize,
        FileWrite,
        PresentMode,
        AfterCommandWait,
    };

    // One key type for both core + dynamic events
    using EventKey = std::variant<EventType, size_t>;

    struct EventKeyHash
    {
        size_t operator()(const EventKey &k) const noexcept
        {
            return std::visit([](auto v)
                              {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, EventType>) {
                    using U = std::underlying_type_t<EventType>;
                    return std::hash<U>{}(static_cast<U>(v)) ^ 0x9e3779b97f4a7c15ull;
                } else {
                    return std::hash<size_t>{}(v);
                } }, k);
        }
    };

    class EventSystem
    {
    public:
        using Func = Delegate<const std::any &>::FunctionType;

        struct QueuedEvent
        {
            EventKey key{};
            std::any payload{};
        };

        // ---- Lifecycle ----
        static void Init() noexcept;
        static void Destroy() noexcept;

        // ---- Register/Dispatch (callback bus) ----
        static void RegisterEvent(EventKey key);
        static void UnregisterEvent(EventKey key);
        static void RegisterCallback(EventKey key, Func &&func);
        static void DispatchEvent(EventKey key, const std::any &data);
        static void ClearEvents() noexcept;

        // Convenience overloads
        static inline void RegisterEvent(EventType type) { RegisterEvent(EventKey{type}); }
        static inline void RegisterEvent(size_t id) { RegisterEvent(EventKey{id}); }
        static inline void UnregisterEvent(EventType type) { UnregisterEvent(EventKey{type}); }
        static inline void UnregisterEvent(size_t id) { UnregisterEvent(EventKey{id}); }
        static inline void RegisterCallback(EventType type, Func &&f) { RegisterCallback(EventKey{type}, std::move(f)); }
        static inline void RegisterCallback(size_t id, Func &&f) { RegisterCallback(EventKey{id}, std::move(f)); }
        static inline void DispatchEvent(EventType type, const std::any &d) { DispatchEvent(EventKey{type}, d); }
        static inline void DispatchEvent(size_t id, const std::any &d) { DispatchEvent(EventKey{id}, d); }

        // ---- SDL-like queue ----
        static void PushEvent(EventKey key, std::any payload = {});
        static inline void PushEvent(EventType type, std::any payload = {}) { PushEvent(EventKey{type}, std::move(payload)); }
        static inline void PushEvent(size_t id, std::any payload = {}) { PushEvent(EventKey{id}, std::move(payload)); }

        [[nodiscard]] static bool PollEvent(QueuedEvent &out);
        [[nodiscard]] static bool PeekEvent(QueuedEvent &out);               // peek front (no consume)
        [[nodiscard]] static bool PeekEvent(EventKey key, QueuedEvent &out); // peek first match (O(n), no consume)
        static void ClearPushedEvents() noexcept;

        // ---- Runtime (size_t) helpers ----
        [[nodiscard]] static bool PeekEvent(size_t id, QueuedEvent &out);  // no consume
        [[nodiscard]] static bool PeekAndPop(size_t id, QueuedEvent &out); // targeted consume
        static size_t CountPending(size_t id);                             // O(n)

    private:
        inline static std::mutex s_mutex;
        inline static std::unordered_map<EventKey, Delegate<const std::any &>, EventKeyHash> s_events{};
        inline static std::deque<QueuedEvent> s_queue{};
    };
}
