#include "Code/App/App.h"
#include "Window/Window.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/PostProcessSystem.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_internal.h"
#include "Script/ScriptManager.h"
#ifdef NDEBUG
#include "Window/SplashScreen.h"
#endif

namespace pe
{
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

        auto shaderCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileShaders);
        };
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Executable + "Assets/Shaders"))
            FileWatcher::Add(file.path().string(), shaderCallback);

#if defined(PE_SCRIPTS)
        auto scriptCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileScripts);
        };

        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Executable + "Assets/Scripts"))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            if (filePath.find("Assets/Scripts/Build") != std::string::npos)
                continue;

            FileWatcher::Add(file.path().string(), scriptCallback);
        }
        ScriptManager::Init();
#endif
        FileWatcher::Start();
        EventSystem::Init();

#ifdef NDEBUG
        m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
#endif

        uint32_t flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;

        SDL_DisplayMode dm;
        SDL_GetDesktopDisplayMode(0, &dm);
        m_window = Window::Create(100, 100, dm.w - 100, dm.h - 100, flags);

        RHII.Init(m_window->ApiHandle());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

        cmd->Begin();
        CreateGlobalSystem<CameraSystem>()->Init(cmd);
        CreateGlobalSystem<LightSystem>()->Init(cmd);
        CreateGlobalSystem<RendererSystem>()->Init(cmd);
        CreateGlobalSystem<PostProcessSystem>()->Init(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        queue->WaitIdle();

        // Render some frames so everything is initialized before destroying the splash screen
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
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
        Context::Remove();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(m_window);
        EventSystem::Destroy();
#if defined(PE_SCRIPTS)
        ScriptManager::Shutdown();
#endif
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
            UpdateGlobalSystems();

        // Get ImGui render data ready
        ImGui::Render();
        m_frameTimer.CountUpdatesStamp();

        if (!m_window->isMinimized())
        {
            DrawGlobalSystems();
            
            // Render platform windows (floating ImGui windows) after main rendering
            GetGlobalSystem<RendererSystem>()->DrawPlatformWindows();
        }
        
        m_frameTimer.CountCpuTotalStamp();
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
