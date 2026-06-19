#include "FutureRegistry.h"
#include "Device.h"
#include "API/Semaphore.h"

namespace pwgpu
{

    WGPUFuture FutureRegistry::TrackEvent(WGPUCallbackMode mode, std::function<void()> callback)
    {
        return TrackEventImpl(mode, std::move(callback), FutureTimeline::CPU, nullptr, 0, {});
    }

    WGPUFuture FutureRegistry::TrackEvent(WGPUCallbackMode mode, std::function<void()> callback,
                                          std::shared_future<void> cpuFuture)
    {
        return TrackEventImpl(mode, std::move(callback), FutureTimeline::CPU, nullptr, 0,
                              std::move(cpuFuture));
    }

    WGPUFuture FutureRegistry::TrackEvent(WGPUCallbackMode mode, std::function<void()> callback,
                                          WGPUQueueImpl *queue, uint64_t waitSerial)
    {
        return TrackEventImpl(mode, std::move(callback), FutureTimeline::Queue, queue, waitSerial,
                              {});
    }

    WGPUFuture FutureRegistry::TrackEventImpl(WGPUCallbackMode mode, std::function<void()> callback,
                                              FutureTimeline timeline, WGPUQueueImpl *queue,
                                              uint64_t waitSerial,
                                              std::shared_future<void> cpuFuture)
    {
        const uint64_t id = m_nextId.fetch_add(1, std::memory_order_relaxed);
        const WGPUFuture future{id};

        if (!callback)
            return future;

        if (static_cast<int>(mode) == 0)
            mode = WGPUCallbackMode_AllowSpontaneous;

        bool alreadyComplete = true;
        if (timeline == FutureTimeline::Queue && queue)
        {
            pe::Semaphore *sem = queue->GetSemaphore();
            if (sem && waitSerial > 0)
                alreadyComplete = sem->GetValue() >= waitSerial;
        }
        else if (cpuFuture.valid())
        {
            alreadyComplete =
                cpuFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        if (mode == WGPUCallbackMode_AllowSpontaneous && alreadyComplete)
        {
            callback();
            return future;
        }

        TrackedFuture tf;
        tf.id = id;
        tf.mode = mode;
        tf.callback = std::move(callback);
        tf.completed = alreadyComplete;
        tf.timeline = timeline;
        tf.queue = queue;
        tf.waitSerial = waitSerial;
        tf.cpuFuture = std::move(cpuFuture);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_futures.emplace(id, std::move(tf));
        }

        return future;
    }

    void FutureRegistry::PollCompletions()
    {
        std::unordered_map<WGPUQueueImpl *, uint64_t> queueSerials;

        for (auto &[id, tf] : m_futures)
        {
            if (tf.completed)
                continue;

            if (tf.timeline == FutureTimeline::Queue && tf.queue)
            {
                auto it = queueSerials.find(tf.queue);
                if (it == queueSerials.end())
                {
                    pe::Semaphore *sem = tf.queue->GetSemaphore();
                    uint64_t val = sem ? sem->GetValue() : 0;
                    it = queueSerials.emplace(tf.queue, val).first;
                }
                if (it->second >= tf.waitSerial)
                    tf.completed = true;
            }
            else if (tf.cpuFuture.valid())
            {
                if (tf.cpuFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    tf.completed = true;
            }
        }
    }

    void FutureRegistry::ProcessEvents()
    {
        std::vector<std::function<void()>> toFire;
        std::vector<WGPUQueueImpl *> queuesToRecycle;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            PollCompletions();

            for (auto it = m_futures.begin(); it != m_futures.end();)
            {
                auto &tf = it->second;
                if (tf.completed && tf.mode != WGPUCallbackMode_WaitAnyOnly)
                {
                    if (tf.queue)
                        queuesToRecycle.push_back(tf.queue);
                    toFire.push_back(std::move(tf.callback));
                    it = m_futures.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        std::sort(queuesToRecycle.begin(), queuesToRecycle.end());
        queuesToRecycle.erase(std::unique(queuesToRecycle.begin(), queuesToRecycle.end()),
                              queuesToRecycle.end());
        for (auto *q : queuesToRecycle)
            q->RecyclePendingSubmits();

        for (auto &cb : toFire)
            cb();
    }

    WGPUWaitStatus FutureRegistry::WaitAny(size_t count, WGPUFutureWaitInfo *infos, uint64_t timeoutNS)
    {
        if (count == 0 && infos == nullptr)
            return timeoutNS == 0 ? WGPUWaitStatus_Success : WGPUWaitStatus_TimedOut;

        if (!infos)
            return WGPUWaitStatus_Error;

        if (timeoutNS > 0)
        {
            bool hasCPU = false;
            bool hasQueue = false;
            WGPUQueueImpl *firstQueue = nullptr;
            bool mixedQueues = false;
            uint64_t maxWaitSerial = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (size_t i = 0; i < count; ++i)
                {
                    auto it = m_futures.find(infos[i].future.id);
                    if (it == m_futures.end())
                        continue;
                    const auto &tf = it->second;
                    if (tf.timeline == FutureTimeline::CPU)
                        hasCPU = true;
                    else
                    {
                        hasQueue = true;
                        if (!firstQueue)
                            firstQueue = tf.queue;
                        else if (tf.queue != firstQueue)
                            mixedQueues = true;
                        if (tf.waitSerial > maxWaitSerial)
                            maxWaitSerial = tf.waitSerial;
                    }
                }
            }

            if ((hasCPU && hasQueue) || mixedQueues)
                return WGPUWaitStatus_Error;

            if (hasQueue && firstQueue)
            {
                pe::Semaphore *sem = firstQueue->GetSemaphore();
                if (sem && maxWaitSerial > 0)
                    sem->WaitTimeout(maxWaitSerial, timeoutNS);
            }
            else if (hasCPU)
            {
                std::shared_future<void> cpuWait;
                {
                    std::lock_guard<std::mutex> lock2(m_mutex);
                    for (size_t i = 0; i < count; ++i)
                    {
                        auto it = m_futures.find(infos[i].future.id);
                        if (it != m_futures.end() && !it->second.completed &&
                            it->second.cpuFuture.valid())
                        {
                            cpuWait = it->second.cpuFuture;
                            break;
                        }
                    }
                }
                if (cpuWait.valid())
                    cpuWait.wait_for(std::chrono::nanoseconds(timeoutNS));
            }
        }

        for (size_t i = 0; i < count; ++i)
            infos[i].completed = WGPU_FALSE;

        bool anyFired = false;

        struct PendingFire
        {
            uint64_t id;
            std::function<void()> callback;
        };
        std::vector<PendingFire> toFire;
        std::vector<WGPUQueueImpl *> queuesToRecycle;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            PollCompletions();

            for (size_t i = 0; i < count; ++i)
            {
                auto it = m_futures.find(infos[i].future.id);
                if (it == m_futures.end())
                {
                    infos[i].completed = WGPU_TRUE;
                    anyFired = true;
                    continue;
                }

                auto &tf = it->second;
                if (tf.completed)
                {
                    infos[i].completed = WGPU_TRUE;
                    if (tf.queue)
                        queuesToRecycle.push_back(tf.queue);
                    toFire.push_back({tf.id, std::move(tf.callback)});
                    m_futures.erase(it);
                    anyFired = true;
                }
            }
        }

        std::sort(queuesToRecycle.begin(), queuesToRecycle.end());
        queuesToRecycle.erase(std::unique(queuesToRecycle.begin(), queuesToRecycle.end()),
                              queuesToRecycle.end());
        for (auto *q : queuesToRecycle)
            q->RecyclePendingSubmits();

        std::sort(toFire.begin(), toFire.end(),
                  [](const PendingFire &a, const PendingFire &b)
                  { return a.id < b.id; });

        for (auto &pf : toFire)
            pf.callback();

        if (anyFired)
            return WGPUWaitStatus_Success;

        return WGPUWaitStatus_TimedOut;
    }

    bool FutureRegistry::IsCompleted(uint64_t futureId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_futures.find(futureId);
        if (it == m_futures.end())
            return true;
        return it->second.completed;
    }

} // namespace pwgpu
