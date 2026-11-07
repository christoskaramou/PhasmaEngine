// WebGPU RHI backend, P2: surface and swapchain (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md,
// decisions 4 and 5) on the page's canvas. An rAF callback loop acquires and clears a swapchain image each frame,
// resizes the window halfway and recreates the swapchain as the engine's resize path does, then prints
// "[sc] done: <width>x<height>, N frames, M out-of-date, K WebGPU errors" after reading the canvas centre pixel.
// Build: the wasm-player preset, target RhiSwapchain (Emscripten only).
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "Base/EventSystem.h"

#include <emscripten/emscripten.h>

#include <cstdio>

using namespace pe;

namespace
{
    constexpr uint32_t kFrames = 120;
    constexpr uint32_t kResizeFrame = 60;

    struct State
    {
        SDL_Window *window = nullptr;
        uint32_t frame = 0;
        uint32_t outOfDate = 0;
        uint32_t imageIndexMask = 0;
    };

    void RecreateSwapchain(State &s)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(s.window, &width, &height);

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);
        Surface *surface = RHII.GetSurface();
        surface->SetActualExtent({0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        RHII.CreateSwapchain(surface);
    }

    void Frame(State &s)
    {
        if (s.frame == kResizeFrame)
            SDL_SetWindowSize(s.window, 800, 450);

        try
        {
            RHII.GetSwapchain()->WaitForNextFrame();
        }
        catch (const SwapchainOutOfDateError &)
        {
            s.outOfDate++;
            RecreateSwapchain(s);
        }

        Swapchain *swapchain = RHII.GetSwapchain();
        const uint32_t index = swapchain->AquireNextImage(nullptr);
        s.imageIndexMask |= 1u << index;

        // Blue until the resize, red after, so the page can tell the last frame reached the canvas.
        Image *image = swapchain->GetImage(index);
        image->SetClearColor(s.frame < kResizeFrame ? vec4(0.0f, 0.0f, 1.0f, 1.0f) : vec4(1.0f, 0.0f, 0.0f, 1.0f));

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        cmd->ClearColors({image});
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
        swapchain->Present();
    }

    void Finish(State *s)
    {
        // Read the canvas inside the frame that drew it: once a frame is shown, WebGPU replaces the canvas's drawing
        // buffer with a cleared one, so a later read sees transparent black.
        // clang-format off
        EM_ASM({
            const c = Module.canvas;
            const copy = document.createElement("canvas");
            copy.width = c.width;
            copy.height = c.height;
            const g = copy.getContext("2d");
            g.drawImage(c, 0, 0);
            const p = g.getImageData(c.width >> 1, c.height >> 1, 1, 1).data;
            out(`[probe] canvas ${c.width}x${c.height} centre ${p[0]},${p[1]},${p[2]},${p[3]}`);
        });
        // clang-format on

        Swapchain *swapchain = RHII.GetSwapchain();
        std::printf("[sc] done: %ux%u, %u frames, %u out-of-date, image mask %u, %u WebGPU errors\n",
                    swapchain->GetWidth(),
                    swapchain->GetHeight(),
                    s->frame,
                    s->outOfDate,
                    s->imageIndexMask,
                    WebGpuRhiImpl::Get()->m_errorCount);
        delete s;
    }
} // namespace

int main()
{
    try
    {
        SDL_Init(SDL_INIT_VIDEO);
        auto *s = new State();
        s->window = SDL_CreateWindow("RhiSwapchain", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 360, 0);

        EventSystem::Init();
        RHII.Init(s->window, PE_GRAPHICS_API_WEBGPU);
        RHII.InitSwapchain();
        std::printf("[sc] swapchain %ux%u, %u images, format %d\n",
                    RHII.GetSwapchain()->GetWidth(),
                    RHII.GetSwapchain()->GetHeight(),
                    RHII.GetSwapchain()->GetImageCount(),
                    static_cast<int>(RHII.GetSwapchainFormat()));

        emscripten_set_main_loop_arg(
            [](void *arg)
            {
                auto *state = static_cast<State *>(arg);
                try
                {
                    Frame(*state);
                }
                catch (const std::exception &e)
                {
                    std::printf("[sc] FAIL exception at frame %u: %s\n[sc] done: aborted\n", state->frame, e.what());
                    emscripten_cancel_main_loop();
                    return;
                }

                if (++state->frame < kFrames)
                    return;

                emscripten_cancel_main_loop();
                Finish(state);
            },
            s,
            0,
            false);
    }
    catch (const std::exception &e)
    {
        std::printf("[sc] FAIL exception: %s\n[sc] done: aborted\n", e.what());
    }

    return 0;
}
