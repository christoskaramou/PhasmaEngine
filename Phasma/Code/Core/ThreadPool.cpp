#include "Core/ThreadPool.h"

namespace pe
{
    // externs
    ThreadPool e_ThreadPool(std::thread::hardware_concurrency()); // general ThreadPool
    ThreadPool e_FW_ThreadPool(1); // FileWatcher ThreadPool
    ThreadPool e_GUI_ThreadPool(std::thread::hardware_concurrency()); // GUI ThreadPool

    // the constructor just launches some amount of workers
    inline ThreadPool::ThreadPool(size_t threads) : m_stop(false)
    {
        m_workers.reserve(threads);

        for (size_t i = 0; i < threads; ++i)
        {
            m_workers.emplace_back(
                [this]
                {
                    for (;;)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->m_queue_mutex);

                            this->m_condition.wait(lock, [this]
                                                   { return this->m_stop || !this->m_tasks.empty(); });

                            if (this->m_stop && this->m_tasks.empty())
                                return;

                            task = std::move(this->m_tasks.front());
                            this->m_tasks.pop();
                        }
                        task();
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