#include "Base/EventSystem.h"

namespace pe
{
    // -------- Lifecycle --------
    void EventSystem::Init() noexcept
    {
        std::scoped_lock lock(s_mutex);
        s_events.clear();
        s_queues.clear();

        s_events.try_emplace(EventType::Quit);
        s_events.try_emplace(EventType::Custom);
        s_events.try_emplace(EventType::SetWindowTitle);
        s_events.try_emplace(EventType::CompileShaders);
        s_events.try_emplace(EventType::CompileScripts);
        s_events.try_emplace(EventType::Resize);
        s_events.try_emplace(EventType::FileWrite);
        s_events.try_emplace(EventType::PresentMode);
        s_events.try_emplace(EventType::AfterCommandWait);
        s_events.try_emplace(EventType::DynamicRendering);
        s_events.try_emplace(EventType::ModelLoaded);
        s_events.try_emplace(EventType::ModelRemoved);
    }

    void EventSystem::Destroy() noexcept
    {
        std::scoped_lock lock(s_mutex);
        s_events.clear();
        s_queues.clear();
    }

    // -------- Register/Dispatch (callback bus) --------
    void EventSystem::RegisterEvent(EventKey key)
    {
        std::scoped_lock lock(s_mutex);
        auto [it, inserted] = s_events.try_emplace(key); // default-constructed delegate
        PE_ERROR_IF(!inserted, "Event already registered!");
    }

    void EventSystem::UnregisterEvent(EventKey key)
    {
        std::scoped_lock lock(s_mutex);
        auto it = s_events.find(key);
        PE_ERROR_IF(it == s_events.end(), "Event not registered!");
        if (it != s_events.end())
            s_events.erase(it);
    }

    void EventSystem::RegisterCallback(EventKey key, Func &&func)
    {
        std::scoped_lock lock(s_mutex);
        auto it = s_events.find(key);
        PE_ERROR_IF(it == s_events.end(), "Event not registered!");
        if (it != s_events.end())
            it->second += std::move(func);
    }

    void EventSystem::DispatchEvent(EventKey key, const std::any &data)
    {
        // Copy delegate under lock, then invoke outside to avoid deadlocks/re-entrancy
        Delegate<const std::any &> delegateCopy;
        {
            std::scoped_lock lock(s_mutex);
            auto it = s_events.find(key);
            PE_ERROR_IF(it == s_events.end(), "Event not registered!");
            if (it == s_events.end())
                return;
            delegateCopy = it->second;
        }
        delegateCopy.Invoke(data);
    }

    void EventSystem::ClearEvents() noexcept
    {
        std::scoped_lock lock(s_mutex);
        s_events.clear();
    }

    // -------- SDL-like queue --------
    void EventSystem::PushEvent(EventKey key, std::any payload)
    {
        std::scoped_lock lock(s_mutex);
        s_queues.push_back(QueuedEvent{std::move(key), std::move(payload)});
    }

    bool EventSystem::PollEvent(QueuedEvent &out)
    {
        std::scoped_lock lock(s_mutex);
        if (s_queues.empty())
            return false;
        out = std::move(s_queues.front());
        s_queues.pop_front();
        return true;
    }

    bool EventSystem::PeekEvent(QueuedEvent &out)
    {
        std::scoped_lock lock(s_mutex);
        if (s_queues.empty())
            return false;
        out = s_queues.front(); // copy; do not consume
        return true;
    }

    bool EventSystem::PeekEvent(EventKey key, QueuedEvent &out)
    {
        std::scoped_lock lock(s_mutex);
        for (const auto &e : s_queues)
        {
            if (e.key == key)
            {
                out = e;
                return true;
            }
        }
        return false;
    }

    void EventSystem::ClearPushedEvents() noexcept
    {
        std::scoped_lock lock(s_mutex);
        s_queues.clear();
    }

    // -------- Runtime (size_t) helpers --------
    bool EventSystem::PeekEvent(size_t id, QueuedEvent &out)
    {
        std::scoped_lock lock(s_mutex);
        for (const auto &e : s_queues)
        {
            if (e.key == EventKey{id})
            {
                out = e;
                return true;
            }
        }
        return false;
    }

    bool EventSystem::PeekAndPop(size_t id, QueuedEvent &out)
    {
        std::scoped_lock lock(s_mutex);
        for (auto it = s_queues.begin(); it != s_queues.end(); ++it)
        {
            if (it->key == EventKey{id})
            {
                out = *it;          // copy out (payload copied)
                s_queues.erase(it); // targeted consume
                return true;
            }
        }
        return false;
    }

    size_t EventSystem::CountPending(size_t id)
    {
        std::scoped_lock lock(s_mutex);
        size_t c = 0;
        for (const auto &e : s_queues)
            if (e.key == EventKey{id})
                ++c;
        return c;
    }
} // namespace pe
