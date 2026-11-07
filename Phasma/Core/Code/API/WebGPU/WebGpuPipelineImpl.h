#pragma once

#include "API/Pipeline_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // A render or compute pipeline (spec decisions 5 and 8). Its layout holds the pass's engine sets at groups 0-2,
    // with the empty layout where the pass has no set, and the push-constant slot at group 3. Depth bias and depth
    // test and write come from PassInfo, as on DX12. Ray tracing does not exist on the web.
    struct WebGpuPipelineImpl final : public Pipeline::Impl
    {
        WebGpuPipelineImpl(Pipeline *owner, PassInfo &info);
        ~WebGpuPipelineImpl() override;

        static WebGpuPipelineImpl *From(Pipeline *pipeline) { return static_cast<WebGpuPipelineImpl *>(pipeline->m_impl); }

        WGPUPipelineLayout m_layout = nullptr;
        WGPURenderPipeline m_render = nullptr;
        WGPUComputePipeline m_compute = nullptr;
        uint32_t m_emptyGroups = 0; // bit i: group i has the empty layout, so draws bind the empty group there

    private:
        void RecordPushRange(PassInfo &info);
        void CreateLayout(PassInfo &info);
        void CreateCompute(PassInfo &info);
        void CreateRender(PassInfo &info);
    };
} // namespace pe
