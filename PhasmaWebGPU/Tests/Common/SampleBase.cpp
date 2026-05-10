#include "SampleBase.h"

#include "SampleUtils.h"

namespace pwgpu::test
{
    bool SampleBase::Init(SampleContext &)
    {
        return true;
    }

    void SampleBase::Resize(SampleContext &, uint32_t, uint32_t) {}

    void SampleBase::HandleEvent(SampleContext &, const SDL_Event &, bool &) {}

    void SampleBase::Update(SampleContext &) {}

    bool SampleBase::Render(SampleContext &ctx)
    {
        SampleFrame frame{};
        frame.frameIndex = m_frameIndex++;
        frame.present = true;
        frame.running = true;

        WGPUCommandEncoderDescriptor encoderDesc{};
        encoderDesc.label = {"sample_frame_encoder", WGPU_STRLEN};
        frame.encoder = wgpuDeviceCreateCommandEncoder(ctx.device, &encoderDesc);
        if (!frame.encoder)
        {
            fprintf(stderr, "[Sample] Failed to create command encoder\n");
            return false;
        }

        if (!AcquireFrame(ctx, frame))
        {
            wgpuCommandEncoderRelease(frame.encoder);
            return true;
        }

        frame.running = Execute(ctx, frame);

        WGPUCommandBufferDescriptor commandBufferDesc{};
        commandBufferDesc.label = {"sample_frame_commands", WGPU_STRLEN};
        WGPUCommandBuffer commandBuffer =
            wgpuCommandEncoderFinish(frame.encoder, &commandBufferDesc);
        SubmitAndPresent(ctx, frame, commandBuffer);
        ReleaseFrame(frame);
        wgpuCommandEncoderRelease(frame.encoder);
        return frame.running;
    }

    void SampleBase::Shutdown(SampleContext &) {}

    bool SampleBase::AfterSubmit(SampleContext &, SampleFrame &)
    {
        return true;
    }

    bool SampleBase::AcquireFrame(SampleContext &ctx, SampleFrame &frame)
    {
        wgpuSurfaceGetCurrentTexture(ctx.surface, &frame.surfaceTexture);
        if (!frame.surfaceTexture.texture ||
            !IsSurfaceTextureStatusOk(frame.surfaceTexture.status))
        {
            if (frame.surfaceTexture.texture)
                wgpuTextureRelease(frame.surfaceTexture.texture);
            if (frame.surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost ||
                frame.surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Error)
            {
                fprintf(stderr,
                        "[Sample] Surface texture unusable: %s\n",
                        SurfaceTextureStatusName(frame.surfaceTexture.status));
            }
            return false;
        }

        frame.surfaceView = CreateTextureView(frame.surfaceTexture.texture, ctx.surfaceFormat);
        if (!frame.surfaceView)
        {
            wgpuTextureRelease(frame.surfaceTexture.texture);
            frame.surfaceTexture.texture = nullptr;
            return false;
        }

        return true;
    }

    void SampleBase::SubmitAndPresent(SampleContext &ctx,
                                      SampleFrame &frame,
                                      WGPUCommandBuffer commandBuffer,
                                      bool present)
    {
        if (!commandBuffer)
            return;

        wgpuQueueSubmit(ctx.queue, 1, &commandBuffer);
        const bool afterOk = AfterSubmit(ctx, frame);
        frame.running = frame.running && afterOk;
        if (present && frame.present)
            wgpuSurfacePresent(ctx.surface);
        wgpuCommandBufferRelease(commandBuffer);
    }

    void SampleBase::ReleaseFrame(SampleFrame &frame)
    {
        if (frame.surfaceView)
        {
            wgpuTextureViewRelease(frame.surfaceView);
            frame.surfaceView = nullptr;
        }

        if (frame.surfaceTexture.texture)
        {
            wgpuTextureRelease(frame.surfaceTexture.texture);
            frame.surfaceTexture.texture = nullptr;
        }
    }

    WGPURenderPassEncoder SampleBase::BeginRenderPass(const SampleFrame &frame,
                                                      const char *label,
                                                      WGPUColor clearColor) const
    {
        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = frame.surfaceView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor;

        WGPURenderPassDescriptor renderPassDesc{};
        renderPassDesc.label = {label, WGPU_STRLEN};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;
        return wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
    }

    WGPURenderPassEncoder SampleBase::BeginRenderPass(const SampleFrame &frame,
                                                      const char *label,
                                                      WGPUColor clearColor,
                                                      WGPUTextureView depthView,
                                                      float depthClearValue) const
    {
        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = frame.surfaceView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor;

        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = depthView;
        depthAttachment.depthClearValue = depthClearValue;
        depthAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store;
        depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

        WGPURenderPassDescriptor renderPassDesc{};
        renderPassDesc.label = {label, WGPU_STRLEN};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;
        renderPassDesc.depthStencilAttachment = &depthAttachment;
        return wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
    }
} // namespace pwgpu::test
