#pragma once

#include <webgpu/webgpu.h>

struct WGPUInstanceImpl;
struct WGPUQueueImpl;

namespace pwgpu
{

    // The timeline on which an async operation completes.
    //
    // CPU-timeline operations: requestAdapter, requestDevice, createPipelineAsync,
    //   getCompilationInfo, popErrorScope.
    // Queue-timeline operations: mapAsync, onSubmittedWorkDone.
    //
    // WaitAny with timeoutNS > 0 must not mix CPU and Queue timelines,
    // and must not mix Queue-timeline futures from different queues.
    enum class FutureTimeline
    {
        CPU,
        Queue
    };

    // A tracked future: holds the callback + metadata needed to fire it at the right time.
    // The callback is type-erased into a std::function<void()> that captures all arguments
    // (status, result handle, userdata, etc.) at registration time.
    struct TrackedFuture
    {
        uint64_t id = 0;
        WGPUCallbackMode mode = WGPUCallbackMode_WaitAnyOnly;
        std::function<void()> callback;
        bool completed = false; // true once the async operation has finished

        FutureTimeline timeline = FutureTimeline::CPU;
        WGPUQueueImpl *queue = nullptr; // non-null only for Queue-timeline futures

        // --- Deferred completion for Queue-timeline futures ---
        // The GPU submission serial this future is waiting on.
        // Completed when queue->GetSemaphore()->GetValue() >= waitSerial.
        // The semaphore lives on WGPUQueueImpl (one per queue), not duplicated here.
        uint64_t waitSerial = 0;

        // --- Deferred completion for CPU-timeline futures ---
        // Optional std::future for async CPU work. Completed when ready.
        std::shared_future<void> cpuFuture;
    };

    // Central registry for all in-flight WGPUFuture objects on an instance.
    //
    // Every async WebGPU operation (requestAdapter, mapAsync, etc.) registers a
    // TrackedFuture here.  The callback fires according to its WGPUCallbackMode:
    //
    //   WaitAnyOnly        — only inside wgpuInstanceWaitAny when this future's ID is queried
    //   AllowProcessEvents — same + inside wgpuInstanceProcessEvents
    //   AllowSpontaneous   — same + may fire immediately at registration if already complete
    class FutureRegistry
    {
    public:
        // Allocate a future ID without tracking. For null-callback / no-instance paths.
        WGPUFuture NextId() { return WGPUFuture{m_nextId.fetch_add(1, std::memory_order_relaxed)}; }

        // Register a CPU-timeline future whose async work is already done.
        WGPUFuture TrackEvent(WGPUCallbackMode mode, std::function<void()> callback);

        // Register a CPU-timeline future with a std::future for deferred completion.
        WGPUFuture TrackEvent(WGPUCallbackMode mode, std::function<void()> callback,
                              std::shared_future<void> cpuFuture);

        // Register a Queue-timeline future that completes when GPU work is done.
        // waitSerial: the queue submission serial to wait for.
        WGPUFuture TrackEvent(WGPUCallbackMode mode, std::function<void()> callback,
                              WGPUQueueImpl *queue, uint64_t waitSerial);

        // Fire all completed futures whose mode >= AllowProcessEvents.
        // Polls GPU semaphores / CPU futures to update completion status first.
        // Callbacks fire in future-ID order (spec requirement).
        void ProcessEvents();

        // Wait for at least one of the listed futures to complete.
        // Fires the callback for each completed future whose mode >= WaitAnyOnly.
        // Sets FutureWaitInfo::completed = true for each that fired.
        //
        // timeoutNS == 0: non-blocking poll (no mixed-source restriction).
        // timeoutNS > 0:  timed wait — must not mix CPU and Queue timelines,
        //                 and must not mix Queue futures from different queues.
        WGPUWaitStatus WaitAny(size_t count, WGPUFutureWaitInfo *infos, uint64_t timeoutNS);

        // Check if a specific future has been completed and its callback already fired.
        bool IsCompleted(uint64_t futureId) const;

    private:
        WGPUFuture TrackEventImpl(WGPUCallbackMode mode, std::function<void()> callback,
                                  FutureTimeline timeline, WGPUQueueImpl *queue,
                                  uint64_t waitSerial, std::shared_future<void> cpuFuture);

        // Poll all incomplete futures and mark them completed if their
        // queue semaphore / cpu-future has resolved. Must be called under m_mutex.
        void PollCompletions();

        mutable std::mutex m_mutex;
        // Ordered map so ProcessEvents iterates in ID order (spec: creation order).
        std::map<uint64_t, TrackedFuture> m_futures;
        std::atomic<uint64_t> m_nextId{1};
    };

} // namespace pwgpu
