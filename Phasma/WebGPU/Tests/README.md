# PhasmaWebGPU Sample Foundation

New visual samples should prefer the shared sample shell in `Tests/Common/`.

## Recommended shape

1. Derive from `pwgpu::test::SampleBase`.
2. Override `Init()` for pipeline/resource creation.
3. Override `Resize()` for resize-dependent textures/views.
4. Override `HandleEvent()` if the sample needs SDL input/events.
5. Override `Update()` for CPU-side per-frame state.
6. Override `Execute()` to record per-frame commands.
7. Override `AfterSubmit()` if the sample needs post-submit readback/capture work.
8. Launch through `pwgpu::test::SampleApp`.

## Minimal example

```cpp
class MySample final : public pwgpu::test::SampleBase
{
public:
    bool Init(pwgpu::test::SampleContext &ctx) override
    {
        return true;
    }

    bool Execute(pwgpu::test::SampleContext &ctx,
                 pwgpu::test::SampleFrame &frame) override
    {
        WGPURenderPassEncoder pass =
            BeginRenderPass(frame, "my_pass", {0.1, 0.1, 0.1, 1.0});
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return true;
    }
};

int main(int argc, char *argv[])
{
    MySample sample;
    pwgpu::test::SampleApp app(sample, MakeMySampleDesc());
    return app.Run(argc, argv);
}
```

## Notes

- `SampleApp` owns SDL, RHI, WebGPU instance/device/surface setup, resize events, timing, FPS title updates, and `--exit-after-*` CLI options.
- `SampleApp` also forwards SDL events into `SampleBase::HandleEvent()` before its built-in quit/resize handling.
- `SampleBase` owns the default frame lifecycle: command encoder creation, swapchain acquire/release, submit, and present.
- Samples that need custom frame orchestration can override `Render()` or reuse `AcquireFrame()`, `SubmitAndPresent()`, and `ReleaseFrame()` directly.
- `SampleBase::AfterSubmit()` is the shared place for readback/capture work that must happen after the frame has been submitted but before the surface texture is released.
- Use `pwgpu::test::CreateBuffer`, `UploadRGBA8Texture`, `CreateTextureView`, and `MakeRuntimeShaderModule` before adding local helpers.
