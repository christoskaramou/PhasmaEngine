#include "Code/App/App.h"
#include "API/Command.h"
#include "API/Debug.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Base/Log.h"
#include "GUI/UndoRedo.h"
#include "Scene/ModelAsset.h"
#include "Script/ScriptSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#endif
#ifdef PE_AUDIO
#include "Systems/AudioSystem.h"
#endif
#include "Systems/AnimationSystem.h"
#include "Window/Window.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"
#if defined(PE_WIN32)
#include "GUI/Backends/imgui_impl_dx12.h"
#endif
#ifdef NDEBUG
#include "Window/SplashScreen.h"
#endif

#include <nlohmann/json.hpp>

namespace pe
{
    App::App() : m_frameTimer(FrameTimer::Instance())
    {
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
                if (file.path().extension() == ".lua" && !ScriptSystem::IsTestScriptPath(file.path().string()))
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

        // On hot-reload reload_state.json already exists — skip splash screen.
        const bool isHotReload = std::filesystem::exists(Path::Executable + "reload_state.json");
#ifdef NDEBUG
        if (!isHotReload && RHII.GetApi() != PE_GRAPHICS_API_DX12)
            m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
#endif

        // Adopt the SDL window that was created by the launcher.
        m_window = Window::Adopt(RHII.GetWindow());

        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

        cmd->Begin();
        CreateGlobalSystem<RendererSystem>()->Init(cmd);
        if (!isDx12)
            CreateGlobalSystem<PostProcessSystem>()->Init(cmd);
#ifdef PE_PHYSICS
        CreateGlobalSystem<PhysicsSystem>()->Init(nullptr);
#endif
#ifdef PE_AUDIO
        CreateGlobalSystem<AudioSystem>()->Init(nullptr);
#endif
        CreateGlobalSystem<AnimationSystem>()->Init(nullptr);

        // ScriptSystem is initialized last because it can call other systems in Init()
        CreateGlobalSystem<ScriptSystem>()->Init(nullptr);

        ModelAsset::GetDefaultResources(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        queue->WaitIdle();

        if (!isDx12)
        {
            // Render frames so everything is initialized before destroying the splash screen.
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
                Frame();
        }

        std::string snapPath = Path::Executable + "reload_state.json";
        std::string hotReloadSnapshot;
        const bool hasHotReloadSnapshot = std::filesystem::exists(snapPath);
        if (hasHotReloadSnapshot)
        {
            std::ifstream f(snapPath);
            hotReloadSnapshot.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        const bool shouldRestoreHotReloadSnapshot = hasHotReloadSnapshot && !hotReloadSnapshot.empty();

        // Call Lua init() after initial frames. For hot-reload we defer this until after
        // startup scene loading and snapshot restore so init runs against the current scene.
        if (!shouldRestoreHotReloadSnapshot)
            if (auto *ss = GetGlobalSystem<ScriptSystem>())
                ss->CallInit();

        // Restore scene before showing the window so the user never sees the
        // default/auto-loaded scene. One extra frame is rendered with the restored
        // state so the swapchain image is correct when Show() makes it visible.
        if (shouldRestoreHotReloadSnapshot)
        {
            if (auto *rs = GetGlobalSystem<RendererSystem>())
            {
                PE_INFO("[HotReload] Restoring snapshot");
                nlohmann::json root = nlohmann::json::parse(hotReloadSnapshot, nullptr, false);
                if (!root.is_discarded() && root.contains("scene") && root["scene"].is_string())
                {
                    rs->GetScene().RestoreSnapshot(root["scene"].get<std::string>());
                    if (root.contains("scene_path") && root["scene_path"].is_string())
                        rs->GetScene().SetScenePath(root["scene_path"].get<std::string>());
                    if (root.contains("ui") && root["ui"].is_string())
                        rs->GetGUI().RestoreUISnapshot(root["ui"].get<std::string>());
                }
                else
                {
                    // Old format: raw scene JSON without wrapper
                    rs->GetScene().RestoreSnapshot(hotReloadSnapshot);
                }
                // No Frame() here — calling it would flush the event queue and could
                // trigger script/scene events (e.g. ball_pit loading) before Run() starts.
            }
        }

        m_window->Show();
        m_window->Maximize();
        GetGlobalSystem<RendererSystem>()->GetGUI().ApplyStartupLayout(!shouldRestoreHotReloadSnapshot);

        if (hasHotReloadSnapshot)
        {
            std::filesystem::remove(snapPath);

            if (shouldRestoreHotReloadSnapshot)
            {
                if (auto *ss = GetGlobalSystem<ScriptSystem>())
                    ss->CallInit(ScriptSystem::InitScope::ActiveNodeScriptsOnly);
            }
        }

        // Reset undo/redo after all initialization (scripts, auto-loads, hot-reload restore, etc.)
        // so the final post-startup state is the clean baseline.
        UndoRedo::Instance().Clear();

#ifdef NDEBUG
        SplashScreen::Destroy(m_splashScreen);
#endif
    }

    App::~App()
    {
        std::string flagPath = Path::Executable + "reload.flag";
        const bool isReload = std::filesystem::exists(flagPath);

        if (isReload)
        {
            if (auto *rs = GetGlobalSystem<RendererSystem>())
            {
                nlohmann::json root;
                root["scene"] = rs->GetScene().TakeSnapshot();
                const auto &scenePath = rs->GetScene().GetScenePath();
                root["scene_path"] = scenePath.empty() ? "" : scenePath.generic_string();
                root["ui"] = rs->GetGUI().TakeUISnapshot();
                std::ofstream(Path::Executable + "reload_state.json") << root.dump();
            }
            std::filesystem::remove(flagPath);

            RHII.WaitDeviceIdle();
        }

        PE_INFO("Application exiting");
        FileWatcher::StopAndJoin();
        ThreadPool::GUI.WaitIdle();
        ThreadPool::General.WaitIdle();
        FileWatcher::Clear();
        DestroyGlobalSystems();
        ModelAsset::DestroyDefaults();
        Context::Remove();
        Log::ClearCallbacks();

        // Flush deletion queues and pipeline caches while this DLL is still loaded.
        // main.cpp calls RHII::Destroy() after FreeLibrary — lambdas and cached
        // handles must not outlive this module's code.
        RHII.WaitDeviceIdle();
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            RHII.FlushDeletionQueue(i);
        CommandBuffer::ClearCache();

        Window::Destroy(m_window);
        EventSystem::Destroy();
    }

    void App::RenderReloadFrame()
    {
        auto *rendererSystem = GetGlobalSystem<RendererSystem>();
        if (!rendererSystem)
            return;

        // The last normal Frame() submitted GPU work that may still be in-flight.
        // Drain everything before touching the same semaphores/images again.
        rendererSystem->WaitAllFramesCommands();

        RHII.NextFrame();
        rendererSystem->WaitPreviousFrameCommands();

        ImGui_ImplSDL2_NewFrame();
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
            ImGui_ImplVulkan_NewFrame();
#if defined(PE_WIN32)
        else if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            ImGui_ImplDX12_NewFrame();
#endif
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        // Centered "Reloading..." overlay drawn on top of the frozen scene.
        const ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##reload_overlay", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoNav);
        ImGui::Text("Reloading...");
        ImGui::End();

        ImGui::Render();
        rendererSystem->Draw();
        rendererSystem->WaitAllFramesCommands();
    }

    void App::ReleaseImGuiContext()
    {
        if (auto *rs = GetGlobalSystem<RendererSystem>())
            rs->GetGUI().ReleaseImGuiOwnership();
    }

    bool App::Frame()
    {
        Profiler::BeginFrame();

        RHII.NextFrame();

        m_frameTimer.Tick();

        const bool isVulkan = RHII.GetApi() == PE_GRAPHICS_API_VULKAN;
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        const bool hasImGuiRenderer = isVulkan || isDx12;

        auto rendererSystem = GetGlobalSystem<RendererSystem>();
        {
            PE_PROFILE_SCOPE("Wait Frame Commands");
            rendererSystem->WaitPreviousFrameCommands();
        }

        // Start ImGui frame
        if (hasImGuiRenderer)
        {
            PE_PROFILE_SCOPE("ImGui New Frame");
            ImGui_ImplSDL2_NewFrame();
            if (isVulkan)
                ImGui_ImplVulkan_NewFrame();
#if defined(PE_WIN32)
            else
                ImGui_ImplDX12_NewFrame();
#endif
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

            if (HasGlobalSystem<ScriptSystem>())
                if (auto *ss = GetGlobalSystem<ScriptSystem>())
                    ss->Update();
        }

        // Get ImGui render data ready
        if (hasImGuiRenderer)
            ImGui::Render();
        m_frameTimer.CountUpdatesStamp();

        if (!m_window->isMinimized())
        {
            {
                PE_PROFILE_SCOPE("Draw");
                rendererSystem->Draw();
            }
            if (isVulkan)
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
