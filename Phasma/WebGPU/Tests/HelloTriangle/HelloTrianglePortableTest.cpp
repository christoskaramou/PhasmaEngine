// Hello Triangle using only webgpu.h, so the same code runs on PhasmaWebGPU (native) and on the browser's WebGPU.
// ponytail: standalone host for one sample; move the web branch into SampleApp when more samples go to the browser.
// Web build (from this folder):
//   em++ HelloTrianglePortableTest.cpp -std=c++20 -O2 --use-port=emdawnwebgpu
//        --embed-file Shaders/triangle.wgsl@/triangle.wgsl -o <out>/index.html
#include <webgpu/webgpu.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#include "../Common/RunOptions.h"
#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include <SDL.h>
#include <SDL_syswm.h>
#endif

namespace
{
    struct App
    {
        WGPUInstance instance = nullptr;
        WGPUSurface surface = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUShaderModule shader = nullptr;
        WGPURenderPipeline pipeline = nullptr;
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        uint32_t width = 1280;
        uint32_t height = 720;
        std::string shaderPath;
        uint64_t frames = 0;
        uint32_t errors = 0;
        bool failed = false;
    };

    void Fail(App &app, const char *what, WGPUStringView message)
    {
        fprintf(stderr,
                "[HelloTrianglePortable] %s: %.*s\n",
                what,
                message.data ? static_cast<int>(message.length) : 0,
                message.data ? message.data : "");
        app.failed = true;
    }

    bool ConfigureSurface(App &app)
    {
        WGPUSurfaceCapabilities caps{};
        wgpuSurfaceGetCapabilities(app.surface, app.adapter, &caps);
        if (caps.formatCount == 0)
        {
            wgpuSurfaceCapabilitiesFreeMembers(caps);
            return false;
        }

        app.format = caps.formats[0]; // the implementation's preferred format comes first
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        WGPUSurfaceConfiguration config{};
        config.device = app.device;
        config.format = app.format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = app.width;
        config.height = app.height;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.presentMode = WGPUPresentMode_Fifo; // the only mode every implementation must support
        wgpuSurfaceConfigure(app.surface, &config);
        return true;
    }

    bool CreatePipeline(App &app)
    {
        std::ifstream file(app.shaderPath, std::ios::binary);
        if (!file)
            return false;

        std::stringstream source;
        source << file.rdbuf();
        const std::string code = source.str();

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = {code.data(), code.size()};

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgsl.chain;
        shaderDesc.label = {"triangle_wgsl", WGPU_STRLEN};
        app.shader = wgpuDeviceCreateShaderModule(app.device, &shaderDesc);

        WGPUColorTargetState colorTarget{};
        colorTarget.format = app.format;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = app.shader;
        fragment.entryPoint = {"fs_main", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = {"tri_pipe", WGPU_STRLEN};
        pipelineDesc.vertex.module = app.shader;
        pipelineDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
        pipelineDesc.primitive.cullMode = WGPUCullMode_None;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragment;
        app.pipeline = wgpuDeviceCreateRenderPipeline(app.device, &pipelineDesc);
        return app.pipeline != nullptr;
    }

    void OnDevice(WGPURequestDeviceStatus status,
                  WGPUDevice device,
                  WGPUStringView message,
                  void *userdata1,
                  void *)
    {
        App &app = *static_cast<App *>(userdata1);
        if (status != WGPURequestDeviceStatus_Success)
        {
            Fail(app, "No device", message);
            return;
        }

        app.device = device;
        app.queue = wgpuDeviceGetQueue(device);
        if (!ConfigureSurface(app))
        {
            Fail(app, "Surface has no formats", {});
            return;
        }

        if (!CreatePipeline(app))
            Fail(app, "Pipeline creation failed", {app.shaderPath.c_str(), WGPU_STRLEN});
    }

    void OnAdapter(WGPURequestAdapterStatus status,
                   WGPUAdapter adapter,
                   WGPUStringView message,
                   void *userdata1,
                   void *)
    {
        App &app = *static_cast<App *>(userdata1);
        if (status != WGPURequestAdapterStatus_Success)
        {
            Fail(app, "No adapter", message);
            return;
        }

        app.adapter = adapter;

        WGPUDeviceDescriptor deviceDesc{};
        deviceDesc.uncapturedErrorCallbackInfo.userdata1 = &app;
        deviceDesc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const *, WGPUErrorType type, WGPUStringView text, void *userdata, void *)
        {
            static_cast<App *>(userdata)->errors++;
            fprintf(stderr,
                    "[HelloTrianglePortable] WebGPU error %d: %.*s\n",
                    static_cast<int>(type),
                    text.data ? static_cast<int>(text.length) : 0,
                    text.data ? text.data : "");
        };
        deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        deviceDesc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const *, WGPUDeviceLostReason reason, WGPUStringView text, void *, void *)
        {
            fprintf(stderr,
                    "[HelloTrianglePortable] Device lost (%d): %.*s\n",
                    static_cast<int>(reason),
                    text.data ? static_cast<int>(text.length) : 0,
                    text.data ? text.data : "");
        };

        WGPURequestDeviceCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnDevice;
        callback.userdata1 = &app;
        wgpuAdapterRequestDevice(adapter, &deviceDesc, callback);
    }

    // Requests stay asynchronous on purpose: browsers deliver the callbacks only after control returns to them.
    bool Start(App &app, WGPUChainedStruct *surfaceSource)
    {
        app.instance = wgpuCreateInstance(nullptr);
        if (!app.instance)
            return false;

        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = surfaceSource;
        app.surface = wgpuInstanceCreateSurface(app.instance, &surfaceDesc);
        if (!app.surface)
            return false;

        WGPURequestAdapterOptions options{};
        options.compatibleSurface = app.surface;

        WGPURequestAdapterCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnAdapter;
        callback.userdata1 = &app;
        wgpuInstanceRequestAdapter(app.instance, &options, callback);
        return true;
    }

    void Frame(App &app)
    {
        wgpuInstanceProcessEvents(app.instance);
        if (!app.pipeline)
            return;

        WGPUSurfaceTexture surfaceTexture{};
        wgpuSurfaceGetCurrentTexture(app.surface, &surfaceTexture);
        const bool acquired = surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                              surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
        if (!acquired)
        {
            if (surfaceTexture.texture)
                wgpuTextureRelease(surfaceTexture.texture);
            return;
        }

        WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.device, nullptr);

        WGPURenderPassColorAttachment color{};
        color.view = view;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, app.pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(app.queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);

#if !defined(__EMSCRIPTEN__)
        wgpuSurfacePresent(app.surface); // the browser presents when the frame callback returns
#endif
        wgpuTextureRelease(surfaceTexture.texture);

        app.frames++;
        if (app.frames == 120)
            printf("[HelloTrianglePortable] 120 frames, %u WebGPU errors\n", app.errors);
    }
} // namespace

#if defined(__EMSCRIPTEN__)
int main()
{
    static App app; // outlives main(): the browser keeps calling Frame after main returns
    app.shaderPath = "/triangle.wgsl";
    emscripten_set_canvas_element_size("#canvas", static_cast<int>(app.width), static_cast<int>(app.height));

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector = {"#canvas", WGPU_STRLEN};
    if (!Start(app, &canvas.chain))
        return 1;

    emscripten_set_main_loop_arg(
        [](void *arg)
        {
            Frame(*static_cast<App *>(arg));
        },
        &app,
        0,
        false);
    return 0;
}
#else
namespace
{
    // PhasmaWebGPU draws through the engine RHI and ignores the surface source, so the RHI must own the window first.
    SDL_Window *CreateRhiWindow(const App &app, int displayIndex, PeGraphicsApi api)
    {
        uint32_t windowFlags = SDL_WINDOW_SHOWN;
        if (api == PE_GRAPHICS_API_VULKAN)
            windowFlags |= SDL_WINDOW_VULKAN;

        SDL_Window *window = SDL_CreateWindow("Phasma WebGPU Hello Triangle Portable",
                                              SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                              SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                              static_cast<int>(app.width),
                                              static_cast<int>(app.height),
                                              windowFlags);
        if (!window)
            return nullptr;

        pe::EventSystem::Init();
        pe::RHII.Init(window, api);
        return window;
    }

    bool StartOnWindow(App &app, SDL_Window *window)
    {
        WGPUChainedStruct *surfaceSource = nullptr;
#if defined(_WIN32)
        SDL_SysWMinfo wmInfo{};
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(window, &wmInfo);

        WGPUSurfaceSourceWindowsHWND hwndSource{};
        hwndSource.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        hwndSource.hinstance = wmInfo.info.win.hinstance;
        hwndSource.hwnd = wmInfo.info.win.window;
        surfaceSource = &hwndSource.chain;
#endif
        return Start(app, surfaceSource);
    }

    void RunFrames(App &app, double exitAfterSeconds)
    {
        const uint64_t startTicks = SDL_GetTicks64();
        bool running = true;
        while (running && !app.failed)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT)
                    running = false;
            }

            Frame(app);

            const double elapsedSeconds = static_cast<double>(SDL_GetTicks64() - startTicks) / 1000.0;
            if (exitAfterSeconds > 0.0 && elapsedSeconds >= exitAfterSeconds)
                running = false;
        }
    }

    void Release(App &app)
    {
        if (app.pipeline)
            wgpuRenderPipelineRelease(app.pipeline);

        if (app.shader)
            wgpuShaderModuleRelease(app.shader);

        if (app.surface)
        {
            wgpuSurfaceUnconfigure(app.surface);
            wgpuSurfaceRelease(app.surface);
        }

        if (app.queue)
            wgpuQueueRelease(app.queue);

        if (app.device)
            wgpuDeviceRelease(app.device);

        if (app.adapter)
            wgpuAdapterRelease(app.adapter);

        if (app.instance)
            wgpuInstanceRelease(app.instance);
    }
} // namespace

int main(int argc, char *argv[])
{
    const pwgpu::test::RunOptions runOptions = pwgpu::test::ParseRunOptions(argc, argv);
    const pe::GraphicsApiSelection apiSelection = pe::ResolveGraphicsApi(argc, argv, nullptr, false);
    if (!runOptions.Succeeded() || !apiSelection.Succeeded())
    {
        fprintf(stderr, "%s%s\n", runOptions.error.c_str(), apiSelection.error.c_str());
        return 1;
    }

    pe::Log::Init();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        return 1;

    App app;
    SDL_Window *window = CreateRhiWindow(app, runOptions.displayIndex, apiSelection.api);
    if (!window)
        return 1;

    char *basePath = SDL_GetBasePath();
    app.shaderPath = std::string(basePath ? basePath : "") + "Shaders/HelloTrianglePortableTest/triangle.wgsl";
    SDL_free(basePath);

    if (StartOnWindow(app, window))
        RunFrames(app, runOptions.exitAfterSeconds);

    printf("[HelloTrianglePortable] exit: %llu frames, %u WebGPU errors%s\n",
           static_cast<unsigned long long>(app.frames),
           app.errors,
           app.failed ? ", FAILED" : "");
    const bool passed = !app.failed && app.errors == 0 && app.frames > 0;

    Release(app);
    pe::RHII.Destroy();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return passed ? 0 : 1;
}
#endif
