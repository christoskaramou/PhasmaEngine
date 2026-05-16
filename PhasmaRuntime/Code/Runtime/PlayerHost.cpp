#include "Runtime/PlayerHost.h"
#include "API/Command.h"
#include "API/GraphicsApiSelection.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Base/EventSystem.h"
#include "Base/Log.h"
#include "Base/Path.h"
#include "Project/ProjectSelection.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace pe
{
    namespace
    {
        bool ParseDisplayIndex(const char *value, int &displayIndex)
        {
            if (!value || *value == '\0')
                return false;

            char *end = nullptr;
            long parsed = std::strtol(value, &end, 10);
            if (*end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max())
                return false;

            displayIndex = static_cast<int>(parsed);
            return true;
        }

        int ParseDisplayIndexArg(int argc, char *argv[])
        {
            int displayIndex = 0;
            for (int i = 1; i < argc; ++i)
            {
                if ((std::strcmp(argv[i], "--display") == 0 || std::strcmp(argv[i], "--screen") == 0) &&
                    i + 1 < argc)
                {
                    if (!ParseDisplayIndex(argv[++i], displayIndex))
                        PE_ERROR("Invalid display index: %s", argv[i]);
                }
            }

            return displayIndex;
        }

        SDL_Window *CreatePlayerWindow(PeGraphicsApi api, int displayIndex)
        {
            const int displayCount = SDL_GetNumVideoDisplays();
            if (displayCount <= 0)
                PE_ERROR("[SDL] no video displays found: %s", SDL_GetError());

            if (displayIndex >= displayCount)
                PE_ERROR("Invalid --display %d; SDL reports %d display(s)", displayIndex, displayCount);

            SDL_Rect displayBounds{};
            if (SDL_GetDisplayBounds(displayIndex, &displayBounds) != 0)
                PE_ERROR("[SDL] SDL_GetDisplayBounds(%d) failed: %s", displayIndex, SDL_GetError());

            int windowWidth = displayBounds.w > 100 ? displayBounds.w - 100 : displayBounds.w;
            int windowHeight = displayBounds.h > 100 ? displayBounds.h - 100 : displayBounds.h;
            uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if !defined(PE_WIN32)
            windowFlags |= SDL_WINDOW_MAXIMIZED;
#endif
            if (api == PE_GRAPHICS_API_VULKAN)
                windowFlags |= SDL_WINDOW_VULKAN;

            SDL_Window *window = SDL_CreateWindow("PhasmaPlayer",
                                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                                  windowWidth,
                                                  windowHeight,
                                                  windowFlags);
            if (!window)
                PE_ERROR("[SDL] %s", SDL_GetError());

            SDL_MaximizeWindow(window);
            SDL_PumpEvents();
            return window;
        }

        void LogProjectSelection(const ProjectSelection &selection)
        {
            if (!selection.warning.empty())
                PE_WARN("[Runtime] %s", selection.warning.c_str());

            PE_INFO("[Runtime] Active project root: %s (%s)",
                    selection.project.root.generic_string().c_str(),
                    ProjectSelectionSourceName(selection.source));

            if (!selection.loadedManifest)
                return;

            PE_INFO("[Runtime] Project manifest: %s", selection.project.manifestPath.generic_string().c_str());
            if (selection.project.startupScene.empty())
                return;

            const std::filesystem::path scene = selection.project.ResolveStartupScene();
            if (std::filesystem::exists(scene))
                PE_INFO("[Runtime] Startup scene: %s", scene.generic_string().c_str());
            else
                PE_WARN("[Runtime] Startup scene not found: %s", scene.generic_string().c_str());
        }

        bool ShouldQuitFromEvent(const SDL_Event &event)
        {
            return event.type == SDL_QUIT ||
                   (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
        }

        bool IsResizeEvent(const SDL_Event &event)
        {
            if (event.type != SDL_WINDOWEVENT)
                return false;

            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_RESTORED:
                return true;
            default:
                return false;
            }
        }

        bool IsWindowMinimized(SDL_Window *window)
        {
            return (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        }

        class SdlVideoSession : public NoCopy, public NoMove
        {
        public:
            SdlVideoSession()
            {
                if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
                    PE_ERROR("[SDL] %s", SDL_GetError());
                m_initialized = true;
            }

            ~SdlVideoSession()
            {
                if (m_initialized)
                    SDL_Quit();
            }

        private:
            bool m_initialized = false;
        };

        class CoreEventSession : public NoCopy, public NoMove
        {
        public:
            CoreEventSession()
            {
                EventSystem::Init();
                m_initialized = true;
            }

            ~CoreEventSession()
            {
                if (m_initialized)
                    EventSystem::Destroy();
            }

        private:
            bool m_initialized = false;
        };

        class PlayerWindow : public NoCopy, public NoMove
        {
        public:
            PlayerWindow(PeGraphicsApi api, int displayIndex)
                : m_window(CreatePlayerWindow(api, displayIndex))
            {
            }

            ~PlayerWindow()
            {
                if (m_window)
                    SDL_DestroyWindow(m_window);
            }

            SDL_Window *Get() const { return m_window; }

        private:
            SDL_Window *m_window = nullptr;
        };

        class RhiSession : public NoCopy, public NoMove
        {
        public:
            RhiSession(SDL_Window *window, PeGraphicsApi api)
            {
                RHII.Init(window, api);
                RHII.InitSwapchain();
                m_initialized = true;
            }

            ~RhiSession()
            {
                if (m_initialized)
                    RHII.Destroy();
            }

        private:
            bool m_initialized = false;
        };

        class PlayerFramePump : public NoCopy, public NoMove
        {
        public:
            explicit PlayerFramePump(SDL_Window *window)
                : m_window(window)
            {
                RecreateFrameSync();
            }

            ~PlayerFramePump()
            {
                RHII.WaitDeviceIdle();
                DestroyFrameSync();
            }

            bool Frame()
            {
                RHII.NextFrame();
                if (!ProcessEvents())
                    return false;

                if (m_resizePending)
                    ResizeSwapchain();

                if (IsWindowMinimized(m_window))
                {
                    SDL_Delay(16);
                    return true;
                }

                DrawClearFrame();
                return true;
            }

            void Run()
            {
                while (Frame())
                {
                }
            }

        private:
            bool ProcessEvents()
            {
                SDL_Event event{};
                while (SDL_PollEvent(&event))
                {
                    if (ShouldQuitFromEvent(event))
                        return false;

                    if (IsResizeEvent(event))
                        m_resizePending = true;
                }

                return true;
            }

            void RecreateFrameSync()
            {
                DestroyFrameSync();

                const uint32_t imageCount = RHII.GetSwapchainImageCount();
                m_acquireSemaphores.reserve(imageCount);
                m_submitSemaphores.reserve(imageCount);

                for (uint32_t i = 0; i < imageCount; ++i)
                {
                    Semaphore *acquireSemaphore = Semaphore::Create(false, "PlayerAcquireSemaphore_" + std::to_string(i));
                    if (RHII.GetApi() != PE_GRAPHICS_API_DX12)
                        acquireSemaphore->SetStageFlags(PE_STAGE_COLOR_ATTACHMENT_OUTPUT);
                    m_acquireSemaphores.push_back(acquireSemaphore);

                    Semaphore *submitSemaphore = Semaphore::Create(false, "PlayerSubmitSemaphore_" + std::to_string(i));
                    if (RHII.GetApi() != PE_GRAPHICS_API_DX12)
                        submitSemaphore->SetStageFlags(PE_STAGE_ALL_COMMANDS);
                    m_submitSemaphores.push_back(submitSemaphore);
                }
            }

            void DestroyFrameSync()
            {
                for (Semaphore *&semaphore : m_acquireSemaphores)
                    Semaphore::Destroy(semaphore);
                m_acquireSemaphores.clear();

                for (Semaphore *&semaphore : m_submitSemaphores)
                    Semaphore::Destroy(semaphore);
                m_submitSemaphores.clear();
            }

            void ResizeSwapchain()
            {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(m_window, &width, &height);
                if (width <= 0 || height <= 0)
                    return;

                RHII.WaitDeviceIdle();
                RHII.GetSurface()->SetActualExtent({0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)});

                Swapchain *swapchain = RHII.GetSwapchain();
                Swapchain::Destroy(swapchain);
                RHII.CreateSwapchain(RHII.GetSurface());

                RecreateFrameSync();
                m_resizePending = false;
            }

            void DrawClearFrame()
            {
                Swapchain *swapchain = RHII.GetSwapchain();
                Queue *queue = RHII.GetMainQueue();
                const uint32_t frame = RHII.GetFrameIndex();
                Semaphore *acquireSemaphore = m_acquireSemaphores[frame];

                uint32_t imageIndex = 0;
                try
                {
                    imageIndex = swapchain->AquireNextImage(acquireSemaphore);
                }
                catch (const SwapchainOutOfDateError &)
                {
                    m_resizePending = true;
                    return;
                }

                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                Image *swapchainImage = swapchain->GetImage(imageIndex);
                swapchainImage->SetClearColor({0.015f, 0.018f, 0.025f, 1.0f});

                Attachment attachment{};
                attachment.image = swapchainImage;
                attachment.loadOp = PE_LOAD_OP_CLEAR;
                attachment.storeOp = PE_STORE_OP_STORE;

                cmd->Begin();
                cmd->BeginPass(1, &attachment, "PlayerClear");
                cmd->EndPass();

                ImageBarrierInfo presentBarrier{};
                presentBarrier.image = swapchainImage;
                presentBarrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
                presentBarrier.stageFlags =
                    (RHII.GetApi() == PE_GRAPHICS_API_DX12 || RHII.UsesDozenVulkan()) ? PE_STAGE_ALL_COMMANDS : PE_STAGE_NONE;
                presentBarrier.accessMask = PE_ACCESS_NONE;
                cmd->ImageBarrier(presentBarrier);
                cmd->End();

                Semaphore *submitSemaphore = m_submitSemaphores[imageIndex];
                queue->Submit(1, &cmd, acquireSemaphore, submitSemaphore);
                queue->Present(swapchain, imageIndex, submitSemaphore);

                cmd->Wait();
                cmd->Return();
            }

            SDL_Window *m_window = nullptr;
            bool m_resizePending = false;
            std::vector<Semaphore *> m_acquireSemaphores;
            std::vector<Semaphore *> m_submitSemaphores;
        };
    } // namespace

    int RunPlayerHost(int argc, char *argv[])
    {
        Path::Init();
        Log::Init();

        try
        {
            const GraphicsApiSelection apiSelection = ResolveGraphicsApi(argc, argv);
            if (!apiSelection.Succeeded())
            {
                PE_ERROR("%s", apiSelection.error.c_str());
                return 1;
            }

            const ProjectSelection projectSelection = ResolveProjectSelection();
            LogProjectSelection(projectSelection);

            const PeGraphicsApi api = apiSelection.api;
            PE_INFO("Selected graphics API: %s (%s)",
                    PeGraphicsApiName(api),
                    GraphicsApiSelectionSourceName(apiSelection.source));

            SdlVideoSession sdl;
            CoreEventSession events;
            PlayerWindow window(api, ParseDisplayIndexArg(argc, argv));
            RhiSession rhi(window.Get(), api);
            PlayerFramePump framePump(window.Get());

            PE_INFO("[Runtime] Player frame pump running (clear/present)");
            framePump.Run();
            return 0;
        }
        catch (const std::exception &e)
        {
            Log::Error(std::string("Unhandled exception in player: ") + e.what());
        }
        catch (...)
        {
            Log::Error("Unhandled non-standard exception in player");
        }

        return 1;
    }
} // namespace pe
