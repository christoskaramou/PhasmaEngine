#include "SampleApp.h"

#include "RunOptions.h"
#include "SampleUtils.h"

#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"

#include <cstring>

namespace
{
    template <typename T>
    struct RequestResult
    {
        T handle = nullptr;
        std::string message;
    };

    RequestResult<WGPUAdapter> RequestAdapter(WGPUInstance instance)
    {
        RequestResult<WGPUAdapter> result{};

        WGPURequestAdapterCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPURequestAdapterStatus status,
                               WGPUAdapter requestedAdapter,
                               WGPUStringView message,
                               void *userdata1,
                               void *)
        {
            auto *request = static_cast<RequestResult<WGPUAdapter> *>(userdata1);
            request->handle =
                status == WGPURequestAdapterStatus_Success ? requestedAdapter : nullptr;
            if (message.data)
                request->message.assign(message.data, message.length);
        };

        wgpuInstanceRequestAdapter(instance, nullptr, callback);
        return result;
    }

    RequestResult<WGPUDevice> RequestDevice(WGPUAdapter adapter,
                                            const std::vector<WGPUFeatureName> &requiredFeatures)
    {
        RequestResult<WGPUDevice> result{};

        WGPUUncapturedErrorCallbackInfo errorCallback{};
        errorCallback.callback = pwgpu::test::DefaultUncapturedErrorCallback;

        WGPUDeviceLostCallbackInfo deviceLostCallback{};
        deviceLostCallback.callback = [](WGPUDevice const *,
                                         WGPUDeviceLostReason reason,
                                         WGPUStringView message,
                                         void *,
                                         void *)
        {
            fprintf(stderr,
                    "[Sample] Device lost (%d): %.*s\n",
                    static_cast<int>(reason),
                    message.data ? static_cast<int>(message.length) : 0,
                    message.data ? message.data : "");
        };

        WGPUDeviceDescriptor deviceDesc{};
        deviceDesc.uncapturedErrorCallbackInfo = errorCallback;
        deviceDesc.deviceLostCallbackInfo = deviceLostCallback;
        deviceDesc.requiredFeatureCount = requiredFeatures.size();
        deviceDesc.requiredFeatures =
            requiredFeatures.empty() ? nullptr : requiredFeatures.data();

        WGPURequestDeviceCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPURequestDeviceStatus status,
                               WGPUDevice requestedDevice,
                               WGPUStringView message,
                               void *userdata1,
                               void *)
        {
            auto *request = static_cast<RequestResult<WGPUDevice> *>(userdata1);
            request->handle =
                status == WGPURequestDeviceStatus_Success ? requestedDevice : nullptr;
            if (message.data)
                request->message.assign(message.data, message.length);
        };

        wgpuAdapterRequestDevice(adapter, &deviceDesc, callback);
        return result;
    }

    bool HasValidSize(uint32_t width, uint32_t height)
    {
        return width > 0 && height > 0;
    }

    bool SupportsPresentMode(const WGPUSurfaceCapabilities &capabilities, WGPUPresentMode mode)
    {
        for (size_t i = 0; i < capabilities.presentModeCount; i++)
        {
            if (capabilities.presentModes[i] == mode)
                return true;
        }

        return false;
    }

    const char *ApiDisplayName(PeGraphicsApi api)
    {
        switch (api)
        {
        case PE_GRAPHICS_API_DX12:
            return "DX12";
        case PE_GRAPHICS_API_VULKAN:
            return "Vulkan";
        default:
            return "Unknown";
        }
    }
} // namespace

namespace pwgpu::test
{
    SampleApp::SampleApp(SampleBase &module, SampleAppDesc desc)
        : m_module(module), m_desc(std::move(desc))
    {
    }

    int SampleApp::Run(int argc, char *argv[])
    {
        if (!InitCommon(argc, argv))
            return 1;

        if (!m_module.Init(m_ctx))
        {
            m_module.Shutdown(m_ctx);
            ShutdownCommon();
            return 1;
        }

        m_module.Resize(m_ctx, m_ctx.width, m_ctx.height);

        bool running = true;
        while (running)
        {
            PumpEvents(running);
            if (!running)
                break;

            wgpuInstanceProcessEvents(m_ctx.instance);
            const bool fpsRefreshed = UpdateTiming();
            if (fpsRefreshed)
                UpdateTitle();
            m_module.Update(m_ctx);

            int programmaticWidth = 0;
            int programmaticHeight = 0;
            SDL_GetWindowSize(m_ctx.window, &programmaticWidth, &programmaticHeight);
            if (programmaticWidth > 0 && programmaticHeight > 0 &&
                (static_cast<uint32_t>(programmaticWidth) != m_ctx.width ||
                 static_cast<uint32_t>(programmaticHeight) != m_ctx.height))
            {
                const uint32_t newWidth = static_cast<uint32_t>(programmaticWidth);
                const uint32_t newHeight = static_cast<uint32_t>(programmaticHeight);
                ReconfigureSurface(newWidth, newHeight);
                m_module.Resize(m_ctx, newWidth, newHeight);
            }

            running = m_module.Render(m_ctx);
            m_frameCount++;
            if (m_exitAfterFrames > 0 && m_frameCount >= m_exitAfterFrames)
                running = false;
            if (m_exitAfterSeconds > 0.0 && m_ctx.timing.elapsedSeconds >= m_exitAfterSeconds)
                running = false;
        }

        m_module.Shutdown(m_ctx);
        ShutdownCommon();
        return 0;
    }

    bool SampleApp::InitCommon(int argc, char *argv[])
    {
        pe::Log::Init();
        setvbuf(stdout, nullptr, _IONBF, 0);

        m_ctx.exeDir = GetExecutableDir(argc > 0 ? argv[0] : nullptr);
        m_ctx.shaderDir = GetShaderDir(m_ctx.exeDir, m_desc.sampleName);

        const RunOptions runOptions = ParseRunOptions(argc, argv);
        if (!runOptions.Succeeded())
        {
            fprintf(stderr, "%s\n", runOptions.error.c_str());
            return false;
        }

        const pe::GraphicsApiSelection apiSelection = pe::ResolveGraphicsApi(argc, argv, nullptr, false);
        if (!apiSelection.Succeeded())
        {
            fprintf(stderr, "%s\n", apiSelection.error.c_str());
            return false;
        }
        PeGraphicsApi api = apiSelection.api;
        m_ctx.apiName = ApiDisplayName(api);
        m_exitAfterFrames = runOptions.exitAfterFrames;
        m_exitAfterSeconds = runOptions.exitAfterSeconds;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }

        std::string displayError;
        if (!ValidateDisplayIndex(runOptions.displayIndex, displayError))
        {
            fprintf(stderr, "%s\n", displayError.c_str());
            ShutdownCommon();
            return false;
        }

        uint32_t windowFlags = m_desc.windowFlags;
        if (api == PE_GRAPHICS_API_DX12)
            windowFlags &= ~SDL_WINDOW_VULKAN;

        m_ctx.window = SDL_CreateWindow(m_desc.windowTitle,
                                        SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                        SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                        static_cast<int>(m_desc.initialWidth),
                                        static_cast<int>(m_desc.initialHeight),
                                        windowFlags);
        if (!m_ctx.window)
        {
            fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
            ShutdownCommon();
            return false;
        }

        pe::EventSystem::Init();
        pe::RHII.Init(m_ctx.window, api);
        m_ctx.gpuName = pe::RHII.GetGpuName();

        m_ctx.instance = wgpuCreateInstance(nullptr);
        if (!m_ctx.instance)
        {
            fprintf(stderr, "Failed to create WebGPU instance\n");
            ShutdownCommon();
            return false;
        }

        const RequestResult<WGPUAdapter> adapterRequest = RequestAdapter(m_ctx.instance);
        m_ctx.adapter = adapterRequest.handle;
        if (!m_ctx.adapter)
        {
            fprintf(stderr,
                    "No adapter%s%s\n",
                    adapterRequest.message.empty() ? "" : ": ",
                    adapterRequest.message.c_str());
            ShutdownCommon();
            return false;
        }

        const RequestResult<WGPUDevice> deviceRequest =
            RequestDevice(m_ctx.adapter, m_desc.requiredFeatures);
        m_ctx.device = deviceRequest.handle;
        if (!m_ctx.device)
        {
            fprintf(stderr,
                    "No device%s%s\n",
                    deviceRequest.message.empty() ? "" : ": ",
                    deviceRequest.message.c_str());
            ShutdownCommon();
            return false;
        }

        m_ctx.queue = wgpuDeviceGetQueue(m_ctx.device);
        if (!m_ctx.queue)
        {
            fprintf(stderr, "Failed to get device queue\n");
            ShutdownCommon();
            return false;
        }

        m_ctx.surface = wgpuInstanceCreateSurface(m_ctx.instance, nullptr);
        if (!m_ctx.surface)
        {
            fprintf(stderr, "No surface\n");
            ShutdownCommon();
            return false;
        }

        WGPUSurfaceCapabilities capabilities{};
        wgpuSurfaceGetCapabilities(m_ctx.surface, m_ctx.adapter, &capabilities);
        m_ctx.surfaceFormat = ChooseSurfaceFormat(capabilities);
        if (m_desc.forcePresentMode &&
            SupportsPresentMode(capabilities, m_desc.preferredPresentMode))
        {
            m_ctx.presentMode = m_desc.preferredPresentMode;
        }
        else
        {
            m_ctx.presentMode = ChoosePresentMode(capabilities);
        }
        m_ctx.presentModeName = PresentModeName(m_ctx.presentMode);
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(m_ctx.window, &windowWidth, &windowHeight);
        m_ctx.width = windowWidth > 0 ? static_cast<uint32_t>(windowWidth) : m_desc.initialWidth;
        m_ctx.height =
            windowHeight > 0 ? static_cast<uint32_t>(windowHeight) : m_desc.initialHeight;

        ReconfigureSurface(m_ctx.width, m_ctx.height);

        m_freq = SDL_GetPerformanceFrequency();
        m_prevTicks = SDL_GetPerformanceCounter();
        m_fpsBase = m_prevTicks;
        m_fpsCount = 0;
        m_frameCount = 0;
        UpdateTitle();
        return true;
    }

    void SampleApp::PumpEvents(bool &running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            m_module.HandleEvent(m_ctx, event, running);

            if (event.type == SDL_QUIT)
            {
                running = false;
                continue;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                uint32_t width = event.window.data1 > 0
                                     ? static_cast<uint32_t>(event.window.data1)
                                     : 0;
                uint32_t height = event.window.data2 > 0
                                      ? static_cast<uint32_t>(event.window.data2)
                                      : 0;

                if (HasValidSize(width, height) &&
                    (width != m_ctx.width || height != m_ctx.height))
                {
                    ReconfigureSurface(width, height);
                    m_module.Resize(m_ctx, width, height);
                }
            }
        }
    }

    void SampleApp::ReconfigureSurface(uint32_t width, uint32_t height)
    {
        if (!m_ctx.surface || !HasValidSize(width, height))
            return;

        m_ctx.width = width;
        m_ctx.height = height;

        WGPUSurfaceConfiguration config = MakeSurfaceConfig(m_ctx.device,
                                                            m_ctx.surfaceFormat,
                                                            m_desc.surfaceUsage,
                                                            width,
                                                            height,
                                                            m_ctx.presentMode,
                                                            m_desc.alphaMode,
                                                            m_desc.surfaceViewFormats.data(),
                                                            m_desc.surfaceViewFormats.size());
        wgpuSurfaceConfigure(m_ctx.surface, &config);
    }

    bool SampleApp::UpdateTiming()
    {
        Uint64 now = SDL_GetPerformanceCounter();
        if (m_freq == 0)
            m_freq = SDL_GetPerformanceFrequency();

        if (m_prevTicks != 0 && m_freq != 0)
        {
            m_ctx.timing.deltaSeconds =
                static_cast<double>(now - m_prevTicks) / static_cast<double>(m_freq);
            m_ctx.timing.elapsedSeconds += m_ctx.timing.deltaSeconds;
        }

        m_prevTicks = now;
        m_fpsCount++;

        if (m_fpsBase != 0 && m_freq != 0 && now - m_fpsBase >= m_freq)
        {
            m_ctx.timing.fps = static_cast<double>(m_fpsCount) *
                               static_cast<double>(m_freq) /
                               static_cast<double>(now - m_fpsBase);
            m_fpsBase = now;
            m_fpsCount = 0;
            return true;
        }

        return false;
    }

    void SampleApp::UpdateTitle() const
    {
        if (!m_ctx.window)
            return;

        char title[512];
        if (m_desc.showFpsInTitle)
        {
            double frameTimeMs = m_ctx.timing.fps > 0.0 ? 1000.0 / m_ctx.timing.fps : 0.0;
            snprintf(title,
                     sizeof(title),
                     "%s %ux%u - Device: %s - API: %s - Present: %s - FPS: %.0f - %.2f ms",
                     m_desc.windowTitle,
                     m_ctx.width,
                     m_ctx.height,
                     m_ctx.gpuName.c_str(),
                     m_ctx.apiName.c_str(),
                     m_ctx.presentModeName.c_str(),
                     m_ctx.timing.fps,
                     frameTimeMs);
        }
        else
        {
            snprintf(title,
                     sizeof(title),
                     "%s %ux%u - Device: %s - API: %s - Present: %s",
                     m_desc.windowTitle,
                     m_ctx.width,
                     m_ctx.height,
                     m_ctx.gpuName.c_str(),
                     m_ctx.apiName.c_str(),
                     m_ctx.presentModeName.c_str());
        }

        SDL_SetWindowTitle(m_ctx.window, title);
    }

    void SampleApp::ShutdownCommon()
    {
        if (m_ctx.device)
            wgpuDeviceDestroy(m_ctx.device);

        if (m_ctx.surface)
        {
            wgpuSurfaceUnconfigure(m_ctx.surface);
            wgpuSurfaceRelease(m_ctx.surface);
            m_ctx.surface = nullptr;
        }

        if (m_ctx.queue)
        {
            wgpuQueueRelease(m_ctx.queue);
            m_ctx.queue = nullptr;
        }

        if (m_ctx.device)
        {
            wgpuDeviceRelease(m_ctx.device);
            m_ctx.device = nullptr;
        }

        if (m_ctx.adapter)
        {
            wgpuAdapterRelease(m_ctx.adapter);
            m_ctx.adapter = nullptr;
        }

        if (m_ctx.instance)
        {
            wgpuInstanceRelease(m_ctx.instance);
            m_ctx.instance = nullptr;
        }

        pe::RHII.Destroy();

        if (m_ctx.window)
        {
            SDL_DestroyWindow(m_ctx.window);
            m_ctx.window = nullptr;
        }

        SDL_Quit();
    }
} // namespace pwgpu::test
