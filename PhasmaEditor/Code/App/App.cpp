#include "Code/App/App.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/UndoRedo.h"
#include "Scene/ModelAsset.h"
#include "Script/ScriptSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Window/Window.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"
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
                PE_ERROR("[SDL] %s", SDL_GetError());
                return;
            }
        }

        auto shaderCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(EventType::CompileShaders, fileEvent);
        };
        if (std::filesystem::exists(Path::Assets + "Shaders"))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Shaders"))
                FileWatcher::Add(file.path().string(), shaderCallback);
        }

        auto scriptCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileScripts);
        };
        if (std::filesystem::exists(Path::Assets + "Scripts"))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Scripts"))
            {
                if (file.path().extension() == ".lua")
                    FileWatcher::Add(file.path().string(), scriptCallback);
            }
        }

        // Watch for external commands (file-based IPC)
        // Write script to command.lua, then write anything to command.run to trigger execution
        {
            std::string agentDir = Path::Assets + "Agent/";
            if (!std::filesystem::exists(agentDir))
                std::filesystem::create_directories(agentDir);
            std::string triggerFile = agentDir + "command.run";
            if (!std::filesystem::exists(triggerFile))
                std::ofstream(triggerFile).close();
            FileWatcher::Add(triggerFile, [](size_t)
                             { EventSystem::PushEvent(EventType::RunCommand); });
        }

        FileWatcher::Start();
        EventSystem::Init();

#ifdef NDEBUG
        m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
#endif

        uint32_t flags = SDL_WINDOW_HIDDEN |
                         SDL_WINDOW_RESIZABLE |
                         SDL_WINDOW_ALLOW_HIGHDPI |
                         SDL_WINDOW_VULKAN;

        SDL_DisplayMode dm;
        SDL_GetDesktopDisplayMode(0, &dm);
        m_window = Window::Create(100, 100, dm.w - 100, dm.h - 100, flags);

        RHII.Init(m_window->ApiHandle());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

        cmd->Begin();
        CreateGlobalSystem<RendererSystem>()->Init(cmd);
        CreateGlobalSystem<PostProcessSystem>()->Init(cmd);
        CreateGlobalSystem<ScriptSystem>()->Init(nullptr);
        ModelAsset::GetDefaultResources(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        queue->WaitIdle();

        // Render frames so everything is initialized before destroying the splash screen
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            Frame();

        // Call Lua init() after initial frames so all systems are ready
        if (auto *ss = GetGlobalSystem<ScriptSystem>())
            ss->CallInit();

        // Reset undo/redo after all initialization (scripts, auto-loads, etc.)
        // so the post-init state is the clean baseline, not the pre-script state.
        UndoRedo::Instance().Clear();

        m_window->Show();
        m_window->Maximize();
        GetGlobalSystem<RendererSystem>()->GetGUI().ApplyStartupLayout();

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
        ModelAsset::DestroyDefaults();
        Context::Remove();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(m_window);
        EventSystem::Destroy();
    }

    bool App::Frame()
    {
        Profiler::BeginFrame();

        RHII.NextFrame();

        m_frameTimer.Tick();

        auto rendererSystem = GetGlobalSystem<RendererSystem>();
        {
            PE_PROFILE_SCOPE("Wait Frame Commands");
            rendererSystem->WaitPreviousFrameCommands();
        }

        // Start ImGui frame
        {
            PE_PROFILE_SCOPE("ImGui New Frame");
            ImGui_ImplSDL2_NewFrame();
            ImGui_ImplVulkan_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
        }

        {
            PE_PROFILE_SCOPE("Process Events");
            if (!m_window->ProcessEvents())
                return false;
        }

        if (!m_window->isMinimized())
        {
            PE_PROFILE_SCOPE("Update Systems");
            UpdateGlobalSystems();
        }

        // Get ImGui render data ready
        ImGui::Render();
        m_frameTimer.CountUpdatesStamp();

        if (!m_window->isMinimized())
        {
            {
                PE_PROFILE_SCOPE("Draw");
                rendererSystem->Draw();
            }
            {
                PE_PROFILE_SCOPE("ImGui Draw Platform Windows");
                rendererSystem->DrawPlatformWindows();
            }
        }

        m_frameTimer.CountCpuTotalStamp();

        Profiler::EndFrame();
        PE_FRAME_MARK;

        return true;
    }

    void App::Run()
    {
        while (Frame())
        {
        }
    }
} // namespace pe
