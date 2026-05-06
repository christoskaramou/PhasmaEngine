#pragma once

#include "Base/Timer_Internal.h"

#include <wrl/client.h>

struct ID3D12QueryHeap;

namespace pe
{
    class Buffer;

    class Dx12GpuTimerImpl final : public GpuTimer::Impl
    {
    public:
        Dx12GpuTimerImpl(const std::string &name);
        ~Dx12GpuTimerImpl() override;

        void Start(CommandBuffer *cmd) override;
        void End() override;
        float GetTime() override;
        double GetStartTimeMs() const override;
        CommandBuffer *GetCommandBuffer() const override { return m_cmd; }
        void ResetState() override
        {
            m_inUse = false;
            m_resultsReady = false;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
        Buffer *m_readback = nullptr;
        uint64_t m_queries[2]{};
        double m_msPerTick = 0.0;
        CommandBuffer *m_cmd = nullptr;
        bool m_resultsReady = false;
        bool m_inUse = false;
    };
} // namespace pe
