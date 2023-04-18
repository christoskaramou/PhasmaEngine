#pragma once

namespace pe
{
    template <Launch launch>
    class SyncQueue
    {
    public:
        using Func = std::function<void()>;

        inline static void Request(Func &&func, Func &&signal = nullptr)
        {
            if (func == nullptr)
                PE_ERROR("Cannot add null function to queue.");

            std::lock_guard<std::mutex> guard(s_requestMutex);

            if constexpr (launch == Launch::Async)
            {
                s_tasks.push_back(e_ThreadPool.Enqueue(std::forward<Func>(func)));
                s_signals.push_back(std::forward<Func>(signal));
            }
            else if constexpr (launch == Launch::AsyncDeferred)
            {
                s_funcs.push_back(std::forward<Func>(func));
                s_signals.push_back(std::forward<Func>(signal));
            }
            else if constexpr (launch == Launch::AsyncNoWait)
            {
                s_tasks.push_back(e_ThreadPool.Enqueue(std::forward<Func>(func)));
                s_signals.push_back(std::forward<Func>(signal));
            }
            else if constexpr (launch == Launch::Sync)
            {
                func();
                if (signal)
                    signal();
            }
            else if constexpr (launch == Launch::SyncDeferred)
            {
                s_funcs.push_back(std::forward<Func>(func));
                s_signals.push_back(std::forward<Func>(signal));
            }
        }

        inline static void ExecuteRequests()
        {
            if constexpr (launch == Launch::All)
            {
                SyncQueue<Launch::AsyncNoWait>::ExecuteRequests();
                SyncQueue<Launch::Async>::ExecuteRequests();
                SyncQueue<Launch::SyncDeferred>::ExecuteRequests();
                SyncQueue<Launch::AsyncDeferred>::ExecuteRequests();
            }
            else if constexpr (launch == Launch::Async)
            {
                Async();
            }
            else if constexpr (launch == Launch::AsyncDeferred)
            {
                AsyncDeferred();
            }
            else if constexpr (launch == Launch::AsyncNoWait)
            {
                AsyncNoWait();
            }
            else if constexpr (launch == Launch::Sync)
            {
            }
            else if constexpr (launch == Launch::SyncDeferred)
            {
                SyncDeferred();
            }
        }

    private:
        inline static void Async()
        {
            for (int i = static_cast<int>(s_tasks.size()) - 1; i >= 0; i--)
            {
                s_tasks[i].get();

                if (s_signals[i])
                    s_signals[i]();
            }

            s_tasks.clear();
            s_signals.clear();
        }

        inline static void AsyncDeferred()
        {
            std::vector<Task<void>> tasks(s_funcs.size());
            for (int i = 0; i < static_cast<int>(s_funcs.size()); i++)
                tasks[i] = e_ThreadPool.Enqueue(s_funcs[i]);

            for (int i = static_cast<int>(tasks.size()) - 1; i >= 0; i--)
            {
                tasks[i].get();

                if (s_signals[i])
                    s_signals[i]();
            }

            s_funcs.clear();
            s_signals.clear();
        }

        inline static void AsyncNoWait()
        {
            int i = 0;
            for (auto it = s_tasks.begin(); it != s_tasks.end();)
            {
                auto status = it->wait_for(std::chrono::seconds(0));
                if (status != std::future_status::timeout)
                {
                    it->get();
                    it = s_tasks.erase(it);

                    if (s_signals[i])
                        s_signals[i]();

                    s_signals.erase(s_signals.begin() + i);
                }
                else
                {
                    ++it;
                    ++i;
                }
            }
        }

        inline static void SyncDeferred()
        {
            for (int i = static_cast<int>(s_funcs.size()) - 1; i >= 0; i--)
            {
                s_funcs[i]();

                if (s_signals[i])
                    s_signals[i]();
            }

            s_funcs.clear();
            s_signals.clear();
        }

    private:
        inline static std::deque<Task<void>> s_tasks;
        inline static std::deque<Func> s_funcs;
        inline static std::deque<Func> s_signals;
        inline static std::mutex s_requestMutex;
    };
}