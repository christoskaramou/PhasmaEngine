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
                s_futures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
                s_signals.push_back(std::forward<Func>(signal));
            }
            else if constexpr (launch == Launch::AsyncDeferred)
            {
                s_funcs.push_back(std::forward<Func>(func));
                s_signals.push_back(std::forward<Func>(signal));
            }
            else if constexpr (launch == Launch::AsyncNoWait)
            {
                s_futures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
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
            for (int i = static_cast<int>(s_futures.size()) - 1; i >= 0; i--)
            {
                s_futures[i].get();

                if (s_signals[i])
                    s_signals[i]();
            }

            s_futures.clear();
            s_signals.clear();
        }

        inline static void AsyncDeferred()
        {
            std::vector<std::future<void>> futures(s_funcs.size());
            for (int i = 0; i < static_cast<int>(s_funcs.size()); i++)
                futures[i] = std::async(std::launch::async, s_funcs[i]);

            for (int i = static_cast<int>(futures.size()) - 1; i >= 0; i--)
            {
                futures[i].get();

                if (s_signals[i])
                    s_signals[i]();
            }

            s_funcs.clear();
            s_signals.clear();
        }

        inline static void AsyncNoWait()
        {
            int i = 0;
            for (auto it = s_futures.begin(); it != s_futures.end();)
            {
                auto status = it->wait_for(std::chrono::seconds(0));
                if (status != std::future_status::timeout)
                {
                    it->get();
                    it = s_futures.erase(it);

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
        inline static std::deque<std::future<void>> s_futures;
        inline static std::deque<Func> s_funcs;
        inline static std::deque<Func> s_signals;
        inline static std::mutex s_requestMutex;
    };
}