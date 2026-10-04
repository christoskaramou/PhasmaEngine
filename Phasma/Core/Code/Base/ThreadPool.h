#pragma once

namespace pe
{
    class PE_API ThreadPool
    {
    public:
        explicit ThreadPool(size_t threads);
        ~ThreadPool();

        template <class F, class... Args>
        auto Enqueue(F &&fn, Args &&...args) -> std::shared_future<std::invoke_result_t<F, Args...>>;

        // Wait until all tasks are done and no workers are active
        void WaitIdle();

        // fn(begin, end) over [0, count) on the calling thread plus up to maxChunks - 1 pool workers (at least
        // minPerChunk items each). Each takes the next quarter-share as it frees up, so one slow stretch (a run
        // of crossfading rigs) is shared instead of holding the caller. Returns once all is done, rethrowing a failure.
        template <class F>
        void ParallelFor(size_t count, size_t minPerChunk, size_t maxChunks, F &&fn);

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
        std::condition_variable m_idle;
        bool m_stop{false};
        size_t m_activeWorkers{0};
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

    template <class F>
    void ThreadPool::ParallelFor(size_t count, size_t minPerChunk, size_t maxChunks, F &&fn)
    {
        const size_t workers = std::min({maxChunks, size_t(std::max(1u, std::thread::hardware_concurrency())),
                                         std::max(size_t(1), count / std::max(size_t(1), minPerChunk))});
        const size_t grain = std::max(size_t(1), count / (workers * 4));
        std::atomic<size_t> next{0}; // outlives every task: all are waited for below
        auto run = [&]()
        {
            for (size_t begin; (begin = next.fetch_add(grain)) < count;)
                fn(begin, std::min(begin + grain, count));
        };
        std::vector<std::shared_future<void>> tasks;
        tasks.reserve(workers - 1);
        try
        {
            for (size_t worker = 1; worker < workers; ++worker)
                tasks.push_back(Enqueue(run));
            run();
        }
        catch (...)
        {
            for (auto &task : tasks)
                task.wait();
            throw;
        }
        for (auto &task : tasks)
            task.wait();
        for (auto &task : tasks)
            task.get();
    }
} // namespace pe
