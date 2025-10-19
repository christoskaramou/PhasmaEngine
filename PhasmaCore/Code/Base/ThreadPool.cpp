#include "Base/ThreadPool.h"

namespace pe
{
    static size_t ClampThreads(size_t n)
    {
        return std::max<size_t>(1, n);
    }

    ThreadPool::ThreadPool(size_t threads)
    {
        const size_t count = ClampThreads(threads);
        m_workers.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            m_workers.emplace_back([this]
                                   {
            for (;;) {
                std::optional<std::function<void()>> task;
                {
                    std::unique_lock<std::mutex> lock(m_queue_mutex);
                    m_condition.wait(lock, [this] {
                        return m_stop || !m_tasks.empty();
                    });
                    if (m_stop && m_tasks.empty())
                        return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop_front();
                }
                (*task)();
            } });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (std::thread &t : m_workers)
        {
            if (t.joinable())
                t.join();
        }
    }

    static const size_t N = ClampThreads(std::thread::hardware_concurrency());
    ThreadPool ThreadPool::General(N);
    ThreadPool ThreadPool::Update(N);
    ThreadPool ThreadPool::Render(N);
    ThreadPool ThreadPool::FW(1);
    ThreadPool ThreadPool::GUI(N);
    std::thread::id ThreadPool::MainThreadID = std::this_thread::get_id();
} // namespace pe
