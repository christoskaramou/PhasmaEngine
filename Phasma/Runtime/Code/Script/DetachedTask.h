#pragma once
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace pe
{
    // Background work whose owner never waits for it (unlike std::async's future): the worker is
    // detached and shares only the result slot, so destroying or reassigning the task returns at once.
    template <class T>
    class DetachedTask
    {
    public:
        template <class F>
        void Start(F &&work)
        {
            auto state = std::make_shared<State>();
            std::thread([state, work = std::forward<F>(work)]() mutable
                        {
                std::optional<T> value;
                std::exception_ptr error;
                try
                {
                    value.emplace(work());
                }
                catch (...)
                {
                    error = std::current_exception();
                }
                std::lock_guard lock(state->mutex);
                state->value = std::move(value);
                state->error = error;
                state->done = true; })
                .detach();
            m_state = std::move(state);
        }

        // Started and not yet collected (std::future naming, so it drops in for one).
        bool valid() const { return m_state != nullptr; }

        bool Ready() const
        {
            if (!m_state)
                return false;
            std::lock_guard lock(m_state->mutex);
            return m_state->done;
        }

        // Requires Ready(); rethrows the worker's exception.
        T Get()
        {
            const auto state = std::move(m_state);
            std::lock_guard lock(state->mutex);
            if (state->error)
                std::rethrow_exception(state->error);
            return std::move(*state->value);
        }

    private:
        struct State
        {
            std::mutex mutex;
            std::optional<T> value;
            std::exception_ptr error;
            bool done = false;
        };
        std::shared_ptr<State> m_state;
    };
} // namespace pe
