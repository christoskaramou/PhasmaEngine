#pragma once

#include "SampleContext.h"

#include <SDL.h>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    struct SampleFrame
    {
        WGPUCommandEncoder encoder = nullptr;
        WGPUSurfaceTexture surfaceTexture{};
        WGPUTextureView surfaceView = nullptr;
        uint64_t frameIndex = 0;
        bool present = true;
        bool running = true;
    };

    class SampleBase
    {
    public:
        virtual ~SampleBase() = default;

        virtual bool Init(SampleContext &ctx);
        virtual void Resize(SampleContext &ctx, uint32_t width, uint32_t height);
        virtual void HandleEvent(SampleContext &ctx, const SDL_Event &event, bool &running);
        virtual void Update(SampleContext &ctx);
        virtual bool Render(SampleContext &ctx);
        virtual void Shutdown(SampleContext &ctx);

    protected:
        virtual bool Execute(SampleContext &ctx, SampleFrame &frame) = 0;
        virtual bool AfterSubmit(SampleContext &ctx, SampleFrame &frame);

        bool AcquireFrame(SampleContext &ctx, SampleFrame &frame);
        void SubmitAndPresent(SampleContext &ctx,
                              SampleFrame &frame,
                              WGPUCommandBuffer commandBuffer,
                              bool present = true);
        void ReleaseFrame(SampleFrame &frame);

        WGPURenderPassEncoder BeginRenderPass(const SampleFrame &frame,
                                              const char *label,
                                              WGPUColor clearColor) const;
        WGPURenderPassEncoder BeginRenderPass(const SampleFrame &frame,
                                              const char *label,
                                              WGPUColor clearColor,
                                              WGPUTextureView depthView,
                                              float depthClearValue = 1.0f) const;

    private:
        uint64_t m_frameIndex = 0;
    };
} // namespace pwgpu::test
