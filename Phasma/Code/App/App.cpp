#include "Code/App/App.h"
#include "Window/Window.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Window/SplashScreen.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    // Main thread id
    std::thread::id e_MainThreadID = std::this_thread::get_id();

    App::App() : m_frameTimer(FrameTimer::Instance())
    {
        // freopen("log.txt", "w", stdout);
        // freopen("errors.txt", "w", stderr);

        if (!SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
            {
                std::cout << SDL_GetError();
                return;
            }
        }

        EventSystem::Init();

#ifdef NDEBUG
        m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
#endif

        uint32_t flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if PE_VULKAN
        flags |= SDL_WINDOW_VULKAN;
#endif
        SDL_DisplayMode dm;
        SDL_GetDesktopDisplayMode(0, &dm);
        m_window = Window::Create(SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dm.w - 100, dm.h - 100, flags);

        RHII.Init(m_window->Handle());

        Queue *queue = Queue::Get(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetFree(queue->GetFamilyId());

        cmd->Begin();
        CONTEXT->CreateSystem<CameraSystem>()->Init(cmd);
        CONTEXT->CreateSystem<LightSystem>()->Init(cmd);
        CONTEXT->CreateSystem<RendererSystem>()->Init(cmd);
        CONTEXT->CreateSystem<PostProcessSystem>()->Init(cmd);
        cmd->End();

        queue->Submit(1, &cmd, 0, nullptr, nullptr, 0, nullptr, nullptr);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();

        FileWatcher::Start(0.25);
        m_context = CONTEXT;

        // Render some frames so everything is initialized before destroying the splash screen
        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
            Frame();

        m_window->Show();

#ifdef NDEBUG
        SplashScreen::Destroy(m_splashScreen);
#endif
    }

    App::~App()
    {
        FileWatcher::Stop();
        FileWatcher::Clear();
        m_context->DestroySystems();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(m_window);
        EventSystem::Destroy();
    }

    bool App::Frame()
    {
        m_frameTimer.Start();

        // Start ImGui frame
        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        if (!m_window->ProcessEvents(m_frameTimer.GetDelta()))
            return false;

        if (!m_window->isMinimized())
        {
            m_context->UpdateSystems(m_frameTimer.GetDelta());
            m_frameTimer.CountUpdatesStamp();
        }

        // Get ImGui render data ready
        ImGui::Render();
        ImGui::UpdatePlatformWindows();

        if (!m_window->isMinimized())
        {
            m_context->DrawSystems();
            m_frameTimer.CountCpuTotalStamp();
        }

        m_frameTimer.ThreadSleep(1.0 / static_cast<double>(GUI::fps) - m_frameTimer.Count());
        m_frameTimer.Tick();

        RHII.NextFrame();

        return true;
    }

    void App::Run()
    {
        while (Frame())
        {
        }
    }
}