#pragma once

#include <webgpu/webgpu.h>

namespace pe
{
    // GPU time of one recording's render and compute passes. WebGPU can only stamp the start and end of a pass
    // (timestampWrites), so there is one sample per pass and none for the whole command buffer. The results are
    // mapped after the submit and arrive some frames later as an AfterCommandWait event, the same samples the other
    // backends' GpuTimer gives. Passes are timed while Debug::IsGpuTimingEnabled and the device has timestamp-query.
    // ponytail: the backend's own blit and clear passes are not timed; pass their descriptors through Add if needed.
    class WebGpuPassTimer
    {
    public:
        ~WebGpuPassTimer();

        const WGPUPassTimestampWrites *Add(const std::string &name); // null: this pass is not timed
        void Resolve(WGPUCommandEncoder encoder);                    // after the last pass, before Finish
        void ReadBack();                                             // after the submit
        void Reset();                                                // the recording was dropped

    private:
        struct Results;

        static constexpr uint32_t kMaxPasses = 128;

        WGPUQuerySet m_set = nullptr;
        WGPUBuffer m_resolved = nullptr;
        WGPUPassTimestampWrites m_writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        std::vector<std::string> m_names;
        Results *m_results = nullptr; // a resolved recording that has not been submitted yet
    };
} // namespace pe
