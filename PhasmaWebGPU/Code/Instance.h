#pragma once

#include <webgpu/webgpu.h>

namespace pe
{
    class RHI;
}

namespace pwgpu
{

    struct PendingFuture
    {
        uint64_t id = 0;
        std::function<void()> callback;
    };

} // namespace pwgpu

struct WGPUInstanceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    std::deque<pwgpu::PendingFuture> pendingFutures;
    std::mutex futuresMutex;

    pe::RHI *rhi = nullptr;
};
