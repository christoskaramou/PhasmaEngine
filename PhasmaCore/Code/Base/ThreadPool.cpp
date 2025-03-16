#include "Base/ThreadPool.h"

namespace pe
{
    // externs
    ThreadPool e_ThreadPool(std::thread::hardware_concurrency());        // general ThreadPool
    ThreadPool e_Update_ThreadPool(std::thread::hardware_concurrency()); // Update ThreadPool
    ThreadPool e_Render_ThreadPool(std::thread::hardware_concurrency()); // Render ThreadPool
    ThreadPool e_FW_ThreadPool(1);                                       // FileWatcher ThreadPool
    ThreadPool e_GUI_ThreadPool(std::thread::hardware_concurrency());    // GUI ThreadPool

    // the constructor just launches some amount of workers
    inline ThreadPool::ThreadPool(size_t threads) : m_stop{false}
    {
        m_workers.reserve(threads);

        for (size_t i = 0; i < threads; ++i)
        {
            m_workers.emplace_back(
                [this]
                {
                    for (;;)
                    {
                        std::optional<std::function<void()>> task;
                        {
                            std::unique_lock<std::mutex> lock(m_queue_mutex);
                            m_condition.wait(lock, [this]
                                             { return m_stop || !m_tasks.empty(); });

                            if (m_stop && m_tasks.empty())
                                return;

                            task = std::move(m_tasks.front());
                            m_tasks.pop_front();
                        }
                        (*task)();
                    }
                });
        }
    }

    // the destructor joins all threads
    inline ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (std::thread &worker : m_workers)
            worker.join();
    }
}
