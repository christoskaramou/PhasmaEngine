#pragma once

namespace pe
{
    template <class T>
    using Task = std::future<T>;
    using TaskStatus = std::future_status;

    class ThreadPool
    {
    public:
        ThreadPool(size_t threads);
        ~ThreadPool();

        template <class F, class... Args>
        auto Enqueue(F &&f, Args &&...args) -> Task<typename std::invoke_result<F, Args...>::type>;

    private:
        // need to keep track of threads so we can join them
        std::vector<std::thread> m_workers;
        // the task queue
        std::deque<std::function<void()>> m_tasks;
        // synchronization
        std::mutex m_queue_mutex;
        std::condition_variable m_condition;
        std::atomic<bool> m_stop{false};
    };

    // add new work item to the pool
    template <class F, class... Args>
    auto ThreadPool::Enqueue(F &&f, Args &&...args) -> Task<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto pckg_task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        Task<return_type> task = pckg_task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (m_stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            m_tasks.emplace_back([pckg_task_param = std::move(pckg_task)]() mutable
                                 { (*pckg_task_param)(); });
        }
        m_condition.notify_one();
        return task;
    }
}
