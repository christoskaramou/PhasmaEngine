#pragma once

namespace pe
{
    class ThreadPool
    {
    public:
        explicit ThreadPool(size_t threads);
        ~ThreadPool();

        template <class F, class... Args>
        auto Enqueue(F &&fn, Args &&...args) -> std::shared_future<std::invoke_result_t<F, Args...>>;

        static ThreadPool General;
        static ThreadPool Update;
        static ThreadPool Render;
        static ThreadPool FW;
        static ThreadPool GUI;
        // Main thread id
        static std::thread::id MainThreadID;

    private:
        std::vector<std::thread> m_workers;
        std::deque<std::function<void()>> m_tasks;

        std::mutex m_queue_mutex;
        std::condition_variable m_condition;
        bool m_stop{false};
    };

    template <class F, class... Args>
    auto ThreadPool::Enqueue(F &&fn, Args &&...args) -> std::shared_future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(fn),
             t = std::make_tuple(std::forward<Args>(args)...)]() mutable -> return_type
            {
                return std::apply(std::move(f), std::move(t));
            });

        std::shared_future<return_type> future = task_ptr->get_future().share();
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            if (m_stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            m_tasks.emplace_back([task_ptr = std::move(task_ptr)]() mutable
                                 { (*task_ptr)(); });
        }
        m_condition.notify_one();
        return future;
    }
} // namespace pe
