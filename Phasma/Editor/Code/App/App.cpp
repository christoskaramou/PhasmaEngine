#include "Code/App/App.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Camera/Camera.h"
#include "GUI/Backends/GUIBackend.h"
#include "GUI/GUIState.h"
#include "GUI/UndoRedo.h"
#include "Project/ProjectSelection.h"
#include "Runtime/RuntimeStartup.h"
#include "Runtime/RuntimePlaySession.h"
#include "Scene/SelectionManager.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Scene/ModelAsset.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"
#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "UI/RuntimeUi.h"
#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#endif
#ifdef PE_PHYSICS2D
#include "Systems/Physics2DSystem.h"
#endif
#ifdef PE_AUDIO
#include "Systems/AudioSystem.h"
#endif
#include "Systems/AnimationSystem.h"
#include "Terrain/TerrainSystem.h"
#include "Voxel/VoxelSystem.h"
#include "Window/Window.h"
#include "imgui/ImGuizmo.h"
#ifdef NDEBUG
#include "Window/SplashScreen.h"
#endif

#include <nlohmann/json.hpp>

#include <vector>

namespace pe
{
    namespace
    {
        bool UsesDx12StartupOrchestration()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12;
        }

        bool UsesWslDozenVulkan()
        {
            return RHII.UsesDozenVulkan();
        }

        bool NeedsWslDozenFifoPacing()
        {
            if (!UsesWslDozenVulkan() || !RHII.GetSwapchain())
                return false;

            const PePresentMode mode = RHII.GetSwapchain()->GetPresentMode();
            return mode == PE_PRESENT_MODE_FIFO || mode == PE_PRESENT_MODE_FIFO_RELAXED;
        }

        double GetWindowRefreshHz(Window *window)
        {
            constexpr double fallbackHz = 60.0;
            if (!window)
                return fallbackHz;

            const int display = SDL_GetWindowDisplayIndex(window->ApiHandle());
            SDL_DisplayMode mode{};
            if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0)
                return static_cast<double>(mode.refresh_rate);

            return fallbackHz;
        }

        int GetMainWindowDisplayIndex()
        {
            SDL_Window *window = RHII.GetWindow();
            if (!window)
                return 0;

            const int display = SDL_GetWindowDisplayIndex(window);
            return display >= 0 ? display : 0;
        }

        void PaceWslDozenFifo(Window *window)
        {
            using Clock = std::chrono::steady_clock;
            static bool active = false;
            static Clock::time_point nextFrame{};

            if (!NeedsWslDozenFifoPacing())
            {
                active = false;
                return;
            }

            const auto frameDuration = std::chrono::duration<double>(1.0 / GetWindowRefreshHz(window));
            const auto now = Clock::now();
            if (!active)
            {
                active = true;
                nextFrame = now + std::chrono::duration_cast<Clock::duration>(frameDuration);
                return;
            }

            if (now < nextFrame)
                std::this_thread::sleep_until(nextFrame);

            const auto afterSleep = Clock::now();
            nextFrame += std::chrono::duration_cast<Clock::duration>(frameDuration);
            if (nextFrame < afterSleep)
                nextFrame = afterSleep + std::chrono::duration_cast<Clock::duration>(frameDuration);
        }

        std::string NormalizeDirectoryPath(const std::filesystem::path &path)
        {
            std::string value = path.lexically_normal().generic_string();
            if (!value.empty() && value.back() != '/')
                value.push_back('/');
            return value;
        }

        std::string ResolveEditorAssetsRoot()
        {
            Path::Init();

            const std::vector<std::filesystem::path> candidates = {
                std::filesystem::path(Path::RuntimeAssets),
                std::filesystem::path(Path::Executable) / "Assets",
                std::filesystem::path(Path::Root) / "Assets",
                std::filesystem::path(Path::Root) / "Phasma" / "Runtime" / "RuntimeAssets",
                std::filesystem::path(Path::Root) / "Editor" / "Assets",
                std::filesystem::path(Path::Assets),
            };

            for (const std::filesystem::path &candidate : candidates)
            {
                if (std::filesystem::exists(candidate / "Scripts" / "global" / "editor_shortcuts.lua"))
                    return NormalizeDirectoryPath(candidate);
            }

            for (const std::filesystem::path &candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                    return NormalizeDirectoryPath(candidate);
            }

            return Path::Assets;
        }

        bool ConfigureRuntimeUiFrame(RuntimeUiSystem &runtimeUi, RendererSystem &rendererSystem)
        {
            Image *displayRT = rendererSystem.GetDisplayRT();
            if (!displayRT)
                return false;

            runtimeUi.SetFrameSurfaceSize(displayRT->GetWidth(), displayRT->GetHeight());

            const ImGuiIO &io = ImGui::GetIO();
            if (!rendererSystem.GetGUI().Render())
            {
                if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
                    return false;

                runtimeUi.SetFrameInputRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
                return true;
            }

            if (!GUIState::s_sceneViewImageRectValid ||
                GUIState::s_sceneViewImageWidth <= 0.0f ||
                GUIState::s_sceneViewImageHeight <= 0.0f)
            {
                runtimeUi.DisableFrameInput();
                return true;
            }

            runtimeUi.SetFrameInputRect(GUIState::s_sceneViewImageMinX,
                                        GUIState::s_sceneViewImageMinY,
                                        GUIState::s_sceneViewImageWidth,
                                        GUIState::s_sceneViewImageHeight);
            return true;
        }

        float EditorCameraAspect()
        {
            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            Image *displayRT = renderer ? renderer->GetDisplayRT() : nullptr;
            float fallbackAspect = 16.0f / 9.0f;
            if (displayRT && displayRT->GetHeight() > 0)
                fallbackAspect = displayRT->GetWidth_f() / displayRT->GetHeight_f();

            if (!renderer || !renderer->GetGUI().Render())
                return fallbackAspect;

            return GUIState::GetSceneViewAspectRatio(fallbackAspect);
        }

        Scene *GetEditorActiveScene()
        {
            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            return renderer ? &renderer->GetScene() : nullptr;
        }

        void WaitForSceneMutation()
        {
            if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
                renderer->WaitAllFramesCommands();
        }

        void EditorClearSceneSelection()
        {
            SelectionManager::Instance().ClearSelection();
        }

        bool EditorIsSceneNodeSelected(const NodeId *node)
        {
            return SelectionManager::Instance().GetSelectedNode() == node;
        }

        bool EditorScriptPlayMode()
        {
            return GUIState::s_playMode;
        }

        void EditorSetScriptPlayMode(bool enabled)
        {
            const bool wasPlay = GUIState::s_playMode;
            GUIState::s_playMode = enabled;
            if (!enabled)
            {
                GUIState::s_isPaused = false;
                SetRuntimePlaySessionPaused(false);
            }
            if (wasPlay != enabled)
            {
                if (auto *ss = GetGlobalSystem<ScriptSystem>())
                    ss->OnPlayModeChanged(enabled);

                if (wasPlay && !enabled)
                {
                    if (RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi())
                        runtimeUi->ClearAllScreens();
                    // Play-scoped script voxel worlds (voxel.create) die on ANY play-exit, mirroring
                    // StopRuntimePlaySession — engine.set_play_mode(false) skips that teardown and
                    // would otherwise strand the world; a Voxel World node rebuilds via reconcile.
                    if (auto *voxels = GetGlobalSystem<voxel::VoxelSystem>())
                        voxels->Destroy();
                    if (auto *terr = GetGlobalSystem<terrain::TerrainSystem>())
                        terr->Destroy();
                }
            }
        }

        bool EditorScriptPaused()
        {
            return GUIState::s_isPaused;
        }

        void EditorSetScriptPaused(bool paused)
        {
            GUIState::s_isPaused = paused;
            SetRuntimePlaySessionPaused(paused);
        }

        bool EditorScriptViewportFocused()
        {
            if (GUIState::s_playMode)
                return true;
            return GUIState::s_sceneViewFocused;
        }

        void EditorSetScriptModelLoading(bool loading)
        {
            GUIState::s_modelLoading = loading;
        }
    } // namespace

    App::App() : m_frameTimer(FrameTimer::Instance())
    {
        Path::Init();
        const std::string editorAssetsRoot = ResolveEditorAssetsRoot();
        const ProjectSelection projectSelection = ResolveProjectSelection();
        ApplyProjectSelectionAssetsRoot(projectSelection);
        if (!projectSelection.warning.empty())
            PE_WARN("[Runtime] %s", projectSelection.warning.c_str());
        PE_INFO("[Runtime] Active project root: %s (%s)",
                projectSelection.project.root.generic_string().c_str(),
                ProjectSelectionSourceName(projectSelection.source));
        PE_INFO("[Runtime] Active assets root: %s", Path::Assets.c_str());

        auto shaderCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(EventType::CompileShaders, fileEvent);
        };
        auto watchShaders = [&](const std::string &root)
        {
            const std::string dir = root + "Shaders";
            if (!std::filesystem::exists(dir))
                return;
            for (auto &file : std::filesystem::recursive_directory_iterator(dir))
                FileWatcher::Add(file.path().string(), shaderCallback);
        };
        watchShaders(Path::RuntimeAssets); // engine shaders
        if (Path::Assets != Path::RuntimeAssets)
            watchShaders(Path::Assets); // project shaders (post-carve)

        auto scriptCallback = [](size_t fileEvent)
        {
            EventSystem::PushEvent(fileEvent);
            EventSystem::PushEvent(EventType::CompileScripts);
        };
        auto registerScriptWatchers = [&](const std::string &assetsRoot)
        {
            const std::filesystem::path scriptsDir = std::filesystem::path(assetsRoot) / "Scripts";
            if (!std::filesystem::exists(scriptsDir))
                return;

            for (auto &file : std::filesystem::recursive_directory_iterator(scriptsDir))
            {
                if (file.path().extension() == ".lua" && !ScriptSystem::IsTestScriptPath(file.path().string()))
                    FileWatcher::Add(file.path().string(), scriptCallback);
            }
        };
        registerScriptWatchers(editorAssetsRoot);
        registerScriptWatchers(Path::Assets);

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
        const bool usesDx12StartupOrchestration = UsesDx12StartupOrchestration();
        const bool usesWslDozenVulkan = UsesWslDozenVulkan();
#ifdef NDEBUG
        if (!isHotReload && !usesDx12StartupOrchestration && !usesWslDozenVulkan)
            m_splashScreen = SplashScreen::Create(SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS, GetMainWindowDisplayIndex());
#endif

        // Adopt the SDL window that was created by the launcher.
        m_window = Window::Adopt(RHII.GetWindow());
        SceneSettings &settings = Settings::Get<SceneSettings>();
        RuntimeStartupSceneResolveOptions startupSceneOptions{};
        startupSceneOptions.allowEditorRestore = !isHotReload;
        startupSceneOptions.allowProjectFallback = !isHotReload;
        const RuntimeStartupSceneSelection startupScene =
            ResolveRuntimeStartupScene(projectSelection, startupSceneOptions);
        if (!startupScene.warning.empty())
            PE_WARN("[Runtime] %s", startupScene.warning.c_str());
        const RuntimeStartupSceneSettings startupSceneSettings = ReadRuntimeStartupSceneSettings(startupScene);
        // Scene settings win when authored; editor_config / PE_PRESENT_MODE only fill the gap.
        if (startupSceneSettings.presentMode)
            settings.preferred_present_mode = *startupSceneSettings.presentMode;
        else if (const std::optional<PePresentMode> forcedPresentMode = ReadStartupPresentModeOverride())
            settings.preferred_present_mode = *forcedPresentMode;
        if (startupSceneSettings.renderScale)
            settings.render_scale = *startupSceneSettings.renderScale;
        RHII.GetSurface()->SetPresentMode(settings.preferred_present_mode);
        RHII.InitSwapchain();

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();

        cmd->Begin();
        CreateGlobalSystem<RendererSystem>();
        SetActiveSceneGetter(
            []() -> Scene *
            {
                return GetEditorActiveScene();
            });
        SceneHostCallbacks sceneHost{};
        sceneHost.beforeMutation = WaitForSceneMutation;
        SetSceneHostCallbacks(sceneHost);
        SceneRuntimeHooks sceneHooks = CreateDefaultSceneRuntimeHooks();
        sceneHooks.clearSelection = EditorClearSceneSelection;
        sceneHooks.isNodeSelected = EditorIsSceneNodeSelected;
        SetSceneRuntimeHooks(sceneHooks);
        CameraRuntimeCallbacks cameraHooks = CreateDefaultCameraRuntimeCallbacks();
        cameraHooks.getAspect = EditorCameraAspect;
        SetCameraRuntimeCallbacks(cameraHooks);
        ScriptRuntimeHooks scriptHooks{};
        scriptHooks.isPlayMode = EditorScriptPlayMode;
        scriptHooks.setPlayMode = EditorSetScriptPlayMode;
        scriptHooks.isPaused = EditorScriptPaused;
        scriptHooks.setPaused = EditorSetScriptPaused;
        scriptHooks.isViewportFocused = EditorScriptViewportFocused;
        scriptHooks.setModelLoading = EditorSetScriptModelLoading;
        scriptHooks.loadEditorOnlyGlobalScripts = true;
        scriptHooks.isEditorHost = true;
        scriptHooks.editorAssetsRoot = editorAssetsRoot;
        SetScriptRuntimeHooks(scriptHooks);
        GetGlobalSystem<RendererSystem>()->Init(cmd);
        m_runtimeUi = std::make_unique<RuntimeUiSystem>();
        if (m_runtimeUi->Init(CreateImGuiRuntimeUiBackend(), GetGlobalSystem<RendererSystem>()->GetDisplayRT()))
        {
            SetActiveRuntimeUi(m_runtimeUi.get());
            PE_INFO("[RuntimeUI] Running with backend: %s", m_runtimeUi->GetBackendName().c_str());
        }
        else
        {
            m_runtimeUi.reset();
        }
#ifdef PE_PHYSICS
        CreateGlobalSystem<PhysicsSystem>()->Init(nullptr);
#endif
#ifdef PE_PHYSICS2D
        CreateGlobalSystem<Physics2DSystem>()->Init(nullptr);
#endif
#ifdef PE_AUDIO
        CreateGlobalSystem<AudioSystem>()->Init(nullptr);
#endif
        CreateGlobalSystem<AnimationSystem>()->Init(nullptr);
        CreateGlobalSystem<voxel::VoxelSystem>()->Init(nullptr);
        CreateGlobalSystem<terrain::TerrainSystem>()->Init(nullptr);

        // ScriptSystem is initialized last because it can call other systems in Init()
        CreateGlobalSystem<ScriptSystem>()->Init(nullptr);

        ModelAsset::GetDefaultResources(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        queue->WaitIdle();

        if (!usesDx12StartupOrchestration)
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

        // Restore hot-reload state after the warm-up frames but before normal Run().
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

        GetGlobalSystem<RendererSystem>()->GetGUI().ApplyStartupLayout(!shouldRestoreHotReloadSnapshot, startupScene);

        // Call Lua init() after startup scene loading so script-built editor
        // scenes are not cleared by the saved startup scene.
        if (!shouldRestoreHotReloadSnapshot)
            if (auto *ss = GetGlobalSystem<ScriptSystem>())
                ss->CallInit();

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
        SetCameraRuntimeCallbacks({});
        SetScriptRuntimeHooks({});
        SetActiveSceneGetter(nullptr);
        SetSceneHostCallbacks({});
        SetSceneRuntimeHooks({});
        // Lua destroy() hooks still call runtime_ui.*; run them while the system is live.
        if (auto *scripts = GetGlobalSystem<ScriptSystem>())
            scripts->Destroy();
        SetActiveRuntimeUi(nullptr);
        if (m_runtimeUi)
        {
            m_runtimeUi->Shutdown();
            m_runtimeUi.reset();
        }
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
        if (!GUIBackend::IsSupported())
            return;

        // The last normal Frame() submitted GPU work that may still be in-flight.
        // Drain everything before touching the same semaphores/images again.
        rendererSystem->WaitAllFramesCommands();

        RHII.NextFrame();
        rendererSystem->WaitPreviousFrameCommands();

        GUIBackend::NewFrame();
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

        const bool hasImGuiRenderer = GUIBackend::IsSupported();

        auto rendererSystem = GetGlobalSystem<RendererSystem>();
        {
            PE_PROFILE_SCOPE("Wait Frame Commands");
            rendererSystem->WaitPreviousFrameCommands();
        }

        // Start ImGui frame
        if (hasImGuiRenderer)
        {
            PE_PROFILE_SCOPE("ImGui New Frame");
            GUIBackend::NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
        }
        RuntimeUiSystem *runtimeUi =
            (hasImGuiRenderer && m_runtimeUi && m_runtimeUi->IsInitialized()) ? m_runtimeUi.get() : nullptr;
        const bool runtimeUiFrameOpen = runtimeUi && ConfigureRuntimeUiFrame(*runtimeUi, *rendererSystem);
        if (runtimeUiFrameOpen)
            runtimeUi->BeginFrame();

        {
            PE_PROFILE_SCOPE("Process Events");
            if (!m_window->ProcessEvents())
            {
                if (runtimeUiFrameOpen)
                    runtimeUi->EndFrame();
                return false;
            }
        }

        if (!m_window->isMinimized())
        {
            PE_PROFILE_SCOPE("Update Systems");
            UpdateGlobalSystems();
        }

        if (runtimeUiFrameOpen)
            runtimeUi->SyncSceneWidgets(rendererSystem->GetScene());

        if (runtimeUiFrameOpen)
            runtimeUi->EndFrame();

        if (!m_window->isMinimized())
        {
            PE_PROFILE_SCOPE("Late Script Mutation Catch-Up");
            rendererSystem->LateCatchUpForScriptMutations();
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
            if (GUIBackend::SupportsPlatformWindows())
            {
                PE_PROFILE_SCOPE("ImGui Draw Platform Windows");
                rendererSystem->DrawPlatformWindows();
            }
        }

        m_frameTimer.CountCpuTotalStamp();

        PaceWslDozenFifo(m_window);

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
