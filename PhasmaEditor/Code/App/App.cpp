#include "Code/App/App.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Model.h"
#include "Systems/LightSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Window/Window.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"
#ifdef NDEBUG
#include "Window/SplashScreen.h"
#endif
#if defined(PE_SCRIPTS)
#include "Script/ScriptManager.h"
#endif

namespace pe
{
    App::App() : m_frameTimer(FrameTimer::Instance())
    {
        if (!SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
            {
                PE_ERROR("SDL_GetError: %s", SDL_GetError());
                return;
            }
        }

        auto shaderCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileShaders);
        };
        if (std::filesystem::exists(Path::Assets + "Shaders"))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Shaders"))
                FileWatcher::Add(file.path().string(), shaderCallback);
        }

#if defined(PE_SCRIPTS)
        auto scriptCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileScripts);
        };
        if (std::filesystem::exists(Path::Assets + "Scripts"))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Scripts"))
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
        CreateGlobalSystem<LightSystem>()->Init(cmd);
        CreateGlobalSystem<RendererSystem>()->Init(cmd);
        CreateGlobalSystem<PostProcessSystem>()->Init(cmd);
        Model::GetDefaultResources(cmd);
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
        PE_INFO("Application exiting");
        FileWatcher::Stop();
        FileWatcher::Clear();
        DestroyGlobalSystems();
        Model::DestroyDefaults();
        Context::Remove();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(m_window);
        EventSystem::Destroy();
#if defined(PE_SCRIPTS)
        ScriptManager::Destroy();
#endif
    }

    bool App::Frame()
    {
        RHII.NextFrame();

        m_frameTimer.Tick();

        auto rendererSystem = GetGlobalSystem<RendererSystem>();
        rendererSystem->WaitPreviousFrameCommands();

        // Start ImGui frame
        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        if (!m_window->ProcessEvents())
            return false;

        if (!m_window->isMinimized())
            UpdateGlobalSystems();

#if defined(PE_SCRIPTS)
        ScriptManager::Update();
#endif

        // Get ImGui render data ready
        ImGui::Render();
        m_frameTimer.CountUpdatesStamp();

        if (!m_window->isMinimized())
        {
            DrawGlobalSystems();
#if defined(PE_SCRIPTS)
            ScriptManager::Draw();
#endif
            // Render platform windows (floating ImGui windows) after main rendering
            rendererSystem->DrawPlatformWindows();
        }

        m_frameTimer.CountCpuTotalStamp();

        return true;
    }

    void App::Run()
    {
        while (Frame())
        {
        }
    }
} // namespace pe
