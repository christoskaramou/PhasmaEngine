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
#include "Script/ScriptManager.h"

namespace pe
{
    // Main thread id
    std::thread::id e_MainThreadID = std::this_thread::get_id();

    App::App() : m_frameTimer(FrameTimer::Instance())
    {
        if (!SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
            {
                std::cout << SDL_GetError();
                return;
            }
        }

        FileWatcher::Start();
        EventSystem::Init();
        // ScriptManager::Init();

#ifdef NDEBUG
        m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
#endif

        uint32_t flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef PE_VULKAN
        flags |= SDL_WINDOW_VULKAN;
#endif
        SDL_DisplayMode dm;
        SDL_GetDesktopDisplayMode(0, &dm);
        m_window = Window::Create(100, 100, dm.w - 100, dm.h - 100, flags);

        RHII.Init(m_window->ApiHandle());

        Queue *queue = Queue::Get(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetFree(queue);

        cmd->Begin();
        CreateGlobalSystem<CameraSystem>()->Init(cmd);
        CreateGlobalSystem<LightSystem>()->Init(cmd);
        CreateGlobalSystem<RendererSystem>()->Init(cmd);
        CreateGlobalSystem<PostProcessSystem>()->Init(cmd);
        cmd->End();

        queue->Submit(1, &cmd);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();

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
        DestroyGlobalSystems();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(m_window);
        EventSystem::Destroy();
        // ScriptManager::Shutdown();
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
            UpdateGlobalSystems(m_frameTimer.GetDelta());

        // Get ImGui render data ready
        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        m_frameTimer.CountUpdatesStamp();

        if (!m_window->isMinimized())
            DrawGlobalSystems();
        m_frameTimer.CountCpuTotalStamp();

        if (!Settings::Get<GlobalSettings>().unlock_fps)
            m_frameTimer.ThreadSleep(1.0 / Settings::Get<GlobalSettings>().target_fps - m_frameTimer.Count());
        m_frameTimer.CountDeltaTime();

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
