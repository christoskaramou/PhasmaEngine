// P1 frame-loop spikes for the WebGPU RHI backend (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md,
// decision 4). Browser only. Main spike: an rAF callback loop (emscripten_set_main_loop_arg, fps 0) with JSPI only for
// the startup waits. Secondary spike (-DSPIKE_JSPI_AWAIT): while (Frame()) awaiting requestAnimationFrame through JSPI.
// Build (from this folder):
//   em++ FrameLoopSpike.cpp -std=c++20 -O2 --use-port=emdawnwebgpu -sJSPI -fwasm-exceptions -sALLOW_MEMORY_GROWTH
//        [-DSPIKE_JSPI_AWAIT] -o <out>/FrameLoopSpike.html
#include <webgpu/webgpu.h>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr uint32_t kFrames = 600;
    constexpr uint32_t kHeavyFirst = 400; // frames [kHeavyFirst, kHeavyLast) draw the expensive fragment shader
    constexpr uint32_t kHeavyLast = 500;
    constexpr uint32_t kThrowFrame = 300;
    constexpr uint64_t kWaitTimeoutNs = 10'000'000'000ull;

    // Fullscreen triangle; the heavy variant loops per pixel to load the GPU.
    constexpr const char *kShader = R"(
@group(0) @binding(0) var<uniform> work : u32;
@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    var v = pos.x * 0.001;
    for (var k = 0u; k < work; k++) { v = fract(sin(v * 12.9898 + f32(k)) * 43758.5453); }
    return vec4f(v, 0.2, 0.4, 1.0);
}
)";

    struct Teardown
    {
        ~Teardown() { std::printf("[spike] teardown destructor ran\n"); }
    };

    struct Spike
    {
        WGPUInstance instance = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUSurface surface = nullptr;
        WGPURenderPipeline pipeline = nullptr;
        WGPUBindGroup lightGroup = nullptr;
        WGPUBindGroup heavyGroup = nullptr;
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        uint32_t frame = 0;
        uint32_t drawn = 0;
        uint32_t skipped = 0;
        uint32_t caught = 0;
        uint32_t errors = 0;
        double last = 0.0;
        std::vector<double> normalIntervals;
        std::vector<double> heavyIntervals;
        Teardown teardown;
    };

    void Wait(Spike &s, WGPUFuture future, const char *what)
    {
        WGPUFutureWaitInfo info{};
        info.future = future;
        const WGPUWaitStatus status = wgpuInstanceWaitAny(s.instance, 1, &info, kWaitTimeoutNs);
        std::printf("[spike] %s wait: status %d, completed %d\n", what, static_cast<int>(status), info.completed ? 1 : 0);
    }

    void RequestAdapterAndDevice(Spike &s)
    {
        WGPURequestAdapterCallbackInfo adapterCb{};
        adapterCb.mode = WGPUCallbackMode_WaitAnyOnly;
        adapterCb.userdata1 = &s;
        adapterCb.callback = [](WGPURequestAdapterStatus, WGPUAdapter adapter, WGPUStringView, void *user, void *)
        {
            static_cast<Spike *>(user)->adapter = adapter;
        };
        Wait(s, wgpuInstanceRequestAdapter(s.instance, nullptr, adapterCb), "adapter");

        WGPUDeviceDescriptor deviceDesc{};
        deviceDesc.uncapturedErrorCallbackInfo.userdata1 = &s;
        deviceDesc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const *, WGPUErrorType type, WGPUStringView text, void *user, void *)
        {
            static_cast<Spike *>(user)->errors++;
            std::printf("[spike] WebGPU error %d: %.*s\n", static_cast<int>(type), static_cast<int>(text.length), text.data);
        };

        WGPURequestDeviceCallbackInfo deviceCb{};
        deviceCb.mode = WGPUCallbackMode_WaitAnyOnly;
        deviceCb.userdata1 = &s;
        deviceCb.callback = [](WGPURequestDeviceStatus, WGPUDevice device, WGPUStringView, void *user, void *)
        {
            static_cast<Spike *>(user)->device = device;
        };
        Wait(s, wgpuAdapterRequestDevice(s.adapter, &deviceDesc, deviceCb), "device");
    }

    WGPUBindGroup MakeWorkGroup(Spike &s, WGPUBindGroupLayout layout, uint32_t work)
    {
        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.size = 16;
        bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = wgpuDeviceCreateBuffer(s.device, &bufferDesc);
        wgpuQueueWriteBuffer(s.queue, buffer, 0, &work, sizeof(work));

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = 16;

        WGPUBindGroupDescriptor groupDesc{};
        groupDesc.layout = layout;
        groupDesc.entryCount = 1;
        groupDesc.entries = &entry;
        return wgpuDeviceCreateBindGroup(s.device, &groupDesc);
    }

    void CreatePipeline(Spike &s)
    {
        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = {kShader, WGPU_STRLEN};
        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgsl.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device, &shaderDesc);

        WGPUColorTargetState target{};
        target.format = s.format;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = {"fs", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.vertex.module = module;
        pipelineDesc.vertex.entryPoint = {"vs", WGPU_STRLEN};
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragment;
        s.pipeline = wgpuDeviceCreateRenderPipeline(s.device, &pipelineDesc);

        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(s.pipeline, 0);
        s.lightGroup = MakeWorkGroup(s, layout, 1);
        s.heavyGroup = MakeWorkGroup(s, layout, 400000);
    }

    void ConfigureSurface(Spike &s)
    {
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
        canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvas.selector = {"#canvas", WGPU_STRLEN};
        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &canvas.chain;
        s.surface = wgpuInstanceCreateSurface(s.instance, &surfaceDesc);

        WGPUSurfaceCapabilities caps{};
        wgpuSurfaceGetCapabilities(s.surface, s.adapter, &caps);
        s.format = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        WGPUSurfaceConfiguration config{};
        config.device = s.device;
        config.format = s.format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = 1280;
        config.height = 720;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(s.surface, &config);
    }

    void Draw(Spike &s, bool heavy)
    {
        WGPUSurfaceTexture surfaceTexture{};
        wgpuSurfaceGetCurrentTexture(s.surface, &surfaceTexture);
        WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

        WGPURenderPassColorAttachment color{};
        color.view = view;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {(s.frame % 60) / 60.0, 0.1, 0.2, 1.0};

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, nullptr);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, s.pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, heavy ? s.heavyGroup : s.lightGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(s.queue, 1, &commands); // no wgpuSurfacePresent: the browser presents when control returns

        wgpuCommandBufferRelease(commands);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surfaceTexture.texture);
    }

    void Summary(const char *name, std::vector<double> values)
    {
        if (values.empty())
            return;

        std::sort(values.begin(), values.end());
        double sum = 0.0;
        for (double v : values)
            sum += v;

        const double mean = sum / values.size();
        double var = 0.0;
        for (double v : values)
            var += (v - mean) * (v - mean);

        std::printf("[spike] %s intervals: n=%zu mean=%.3f ms sd=%.3f ms p50=%.3f p99=%.3f max=%.3f\n",
                    name, values.size(), mean, std::sqrt(var / values.size()), values[values.size() / 2],
                    values[values.size() * 99 / 100], values.back());
    }

    // One frame; false when the run is over. Every 7th frame takes the early-return path a not-ready window takes.
    bool Frame(Spike &s)
    {
        const double now = emscripten_get_now();
        if (s.frame > 0)
        {
            const bool heavy = s.frame > kHeavyFirst && s.frame <= kHeavyLast;
            (heavy ? s.heavyIntervals : s.normalIntervals).push_back(now - s.last);
        }

        s.last = now;
        s.frame++;
        if (s.frame > kFrames)
            return false;

        try
        {
            if (s.frame == kThrowFrame)
                throw std::runtime_error("spike frame exception");
        }
        catch (const std::exception &e)
        {
            s.caught++;
            std::printf("[spike] caught at frame %u: %s\n", s.frame, e.what());
        }

        if (s.frame % 7 == 0)
        {
            s.skipped++;
            return true;
        }

        Draw(s, s.frame >= kHeavyFirst && s.frame < kHeavyLast);
        s.drawn++;
        return true;
    }

    void Finish(Spike *s)
    {
        Summary("normal", s->normalIntervals);
        Summary("heavy", s->heavyIntervals);
        std::printf("[spike] %u frames: drawn %u, early returns %u, exceptions caught %u, %u WebGPU errors\n",
                    s->frame - 1, s->drawn, s->skipped, s->caught, s->errors);
        delete s;
        std::printf("[spike] done\n");
    }

    Spike *Start()
    {
        WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_TimedWaitAny};
        WGPUInstanceDescriptor instanceDesc{};
        instanceDesc.requiredFeatureCount = 1;
        instanceDesc.requiredFeatures = features;

        auto *s = new Spike();
        const double t0 = emscripten_get_now();
        s->instance = wgpuCreateInstance(&instanceDesc);
        RequestAdapterAndDevice(*s);
        if (!s->device)
        {
            std::printf("[spike] no device\n");
            return nullptr;
        }

        s->queue = wgpuDeviceGetQueue(s->device);
        ConfigureSurface(*s);
        CreatePipeline(*s);
        std::printf("[spike] startup (instance, adapter, device, surface, pipeline): %.1f ms\n", emscripten_get_now() - t0);
        return s;
    }
} // namespace

#if defined(SPIKE_JSPI_AWAIT)
// The body is JavaScript; clang-format would split its => into "= >".
// clang-format off
EM_ASYNC_JS(void, AwaitAnimationFrame, (), { await new Promise((resolve) => requestAnimationFrame(resolve)); });
// clang-format on

int main()
{
    std::printf("[spike] mode: JSPI await of requestAnimationFrame\n");
    Spike *s = Start();
    if (!s)
        return 1;

    while (Frame(*s))
        AwaitAnimationFrame();

    Finish(s);
    return 0;
}
#else
int main()
{
    std::printf("[spike] mode: rAF callback loop (emscripten_set_main_loop_arg, fps 0)\n");
    Spike *s = Start();
    if (!s)
        return 1;

    emscripten_set_main_loop_arg(
        [](void *arg)
        {
            auto *spike = static_cast<Spike *>(arg);
            if (Frame(*spike))
                return;

            emscripten_cancel_main_loop();
            Finish(spike);
        },
        s, 0, false);
    return 0; // main returns; the callback owns the heap state
}
#endif
