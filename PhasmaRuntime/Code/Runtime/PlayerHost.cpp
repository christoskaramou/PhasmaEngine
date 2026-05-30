#include "Runtime/PlayerHost.h"
#include "API/GraphicsApiSelection.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Project/ProjectSelection.h"
#include "Camera/Camera.h"
#include "Render/RuntimeSceneRenderer.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Runtime/RuntimeHost.h"
#include "Runtime/RuntimePlaySession.h"
#include "Runtime/RuntimeStartup.h"
#include "Script/Bindings/Input/InputState.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/PhysicsSystem.h"
#include "UI/RuntimeUi.h"
#include "Window/WindowEvents.h"

namespace pe
{
    namespace
    {
        void LogProjectSelection(const ProjectSelection &selection,
                                 const RuntimeStartupSceneSelection &startupScene)
        {
            if (!selection.warning.empty())
                PE_WARN("[Runtime] %s", selection.warning.c_str());
            if (!startupScene.warning.empty())
                PE_WARN("[Runtime] %s", startupScene.warning.c_str());

            PE_INFO("[Runtime] Active project root: %s (%s)",
                    selection.project.root.generic_string().c_str(),
                    ProjectSelectionSourceName(selection.source));

            if (selection.loadedManifest)
                PE_INFO("[Runtime] Project manifest: %s", selection.project.manifestPath.generic_string().c_str());

            if (startupScene.IsExplicitEmpty())
                PE_INFO("[Runtime] Startup scene: none (runtime settings)");
            else if (!startupScene.scenePath.empty() && std::filesystem::exists(startupScene.scenePath))
                PE_INFO("[Runtime] Startup scene: %s (%s)",
                        startupScene.scenePath.generic_string().c_str(),
                        RuntimeStartupSceneSourceName(startupScene.source));
            else if (!startupScene.scenePath.empty())
                PE_WARN("[Runtime] Startup scene not found: %s", startupScene.scenePath.generic_string().c_str());
            else
                PE_INFO("[Runtime] Startup scene: none");
        }

        bool ShouldQuitFromEvent(const SDL_Event &event, bool uiCaptured)
        {
            return event.type == SDL_QUIT ||
                   (!uiCaptured && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
        }

        Scene *s_playerScene = nullptr;
        bool s_playerPlayMode = true;
        bool s_playerPaused = false;

        class ScopedFileWatcherState : public NoCopy, public NoMove
        {
        public:
            explicit ScopedFileWatcherState(bool enabled)
                : m_previousEnabled(FileWatcher::IsEnabled())
            {
                FileWatcher::SetEnabled(enabled);
            }

            ~ScopedFileWatcherState()
            {
                FileWatcher::SetEnabled(m_previousEnabled);
            }

        private:
            bool m_previousEnabled = true;
        };

        Scene *GetPlayerActiveScene()
        {
            return s_playerScene;
        }

        bool PlayerScriptPlayMode()
        {
            return s_playerPlayMode;
        }

        void PlayerSetScriptPlayMode(bool enabled)
        {
            s_playerPlayMode = enabled;
            if (!enabled)
                EventSystem::PushEvent(EventType::RequestExit);
        }

        bool PlayerScriptPaused()
        {
            return s_playerPaused;
        }

        void PlayerSetScriptPaused(bool paused)
        {
            s_playerPaused = paused;
            SetRuntimePlaySessionPaused(paused);
        }

        void RegisterPlayerFileWatchers()
        {
            if (!FileWatcher::IsEnabled())
                return;

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
        }

        void WaitForPlayerSceneMutation()
        {
            RHII.WaitDeviceIdle();
        }

        class PlayerSceneRegistration : public NoCopy, public NoMove
        {
        public:
            PlayerSceneRegistration(Scene &scene)
            {
                s_playerScene = &scene;
                s_playerPlayMode = true;
                s_playerPaused = false;
                SetActiveSceneGetter(GetPlayerActiveScene);
                SetSceneRuntimeHooks(CreateDefaultSceneRuntimeHooks());
                SceneHostCallbacks sceneHost{};
                sceneHost.beforeMutation = WaitForPlayerSceneMutation;
                SetSceneHostCallbacks(sceneHost);
                SetCameraRuntimeCallbacks(CreateDefaultCameraRuntimeCallbacks());
                ScriptRuntimeHooks scriptHooks{};
                scriptHooks.isPlayMode = PlayerScriptPlayMode;
                scriptHooks.setPlayMode = PlayerSetScriptPlayMode;
                scriptHooks.isPaused = PlayerScriptPaused;
                scriptHooks.setPaused = PlayerSetScriptPaused;
                SetScriptRuntimeHooks(scriptHooks);
            }

            ~PlayerSceneRegistration()
            {
                SetScriptRuntimeHooks({});
                SetCameraRuntimeCallbacks({});
                SetSceneHostCallbacks({});
                SetSceneRuntimeHooks({});
                SetActiveSceneGetter(nullptr);
                s_playerPaused = false;
                s_playerPlayMode = true;
                s_playerScene = nullptr;
            }
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

        class PlayerRuntimeCleanup : public NoCopy, public NoMove
        {
        public:
            ~PlayerRuntimeCleanup()
            {
                Cleanup();
            }

            void MarkGlobalSystemsCreated()
            {
                m_globalSystemsCreated = true;
            }

            void Cleanup()
            {
                if (m_cleaned)
                    return;

                StopRuntimePlaySession();
                FileWatcher::StopAndJoin();
                ThreadPool::GUI.WaitIdle();
                ThreadPool::General.WaitIdle();
                FileWatcher::Clear();

                if (m_globalSystemsCreated)
                    DestroyGlobalSystems();

                ModelAsset::DestroyDefaults();
                Context::Remove();
                m_cleaned = true;
            }

        private:
            bool m_globalSystemsCreated = false;
            bool m_cleaned = false;
        };

        class PlayerFramePump : public NoCopy, public NoMove
        {
        public:
            PlayerFramePump(SDL_Window *window, RuntimeSceneRenderer &renderer, RuntimeUiSystem *runtimeUi)
                : m_window(window), m_renderer(renderer), m_runtimeUi(runtimeUi)
            {
            }

            ~PlayerFramePump()
            {
                m_renderer.WaitAllFramesCommands();
            }

            bool Frame()
            {
                RHII.NextFrame();
                FrameTimer::Instance().Tick();
                m_renderer.WaitPreviousFrameCommands();

                if (m_runtimeUi)
                {
                    if (Image *displayRT = m_renderer.GetDisplayRT())
                        m_runtimeUi->SetFrameSurfaceSize(displayRT->GetWidth(), displayRT->GetHeight());
                    m_runtimeUi->BeginFrame();
                }

                if (!ProcessEvents())
                {
                    if (m_runtimeUi)
                        m_runtimeUi->EndFrame();
                    return false;
                }

                if (m_resizePending)
                    ResizeSwapchain();

                if (IsWindowMinimized(m_window))
                {
                    if (m_runtimeUi)
                        m_runtimeUi->EndFrame();
                    SDL_Delay(16);
                    return true;
                }

                UpdateGlobalSystems();
                const bool keepRunning = ProcessRuntimeEvents();
                if (m_runtimeUi)
                    m_runtimeUi->EndFrame();

                if (!keepRunning)
                    return false;

                m_renderer.Update();
                m_renderer.Draw();
                FrameTimer::Instance().CountUpdatesStamp();
                FrameTimer::Instance().CountCpuTotalStamp();
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
                InputState::BeginFrame();
                while (SDL_PollEvent(&event))
                {
                    const bool uiCaptured = m_runtimeUi && m_runtimeUi->ProcessEvent(event);

                    if (ShouldQuitFromEvent(event, uiCaptured))
                        return false;

                    if (!uiCaptured && event.type == SDL_MOUSEMOTION)
                        InputState::AddMouseMotion(event.motion.xrel, event.motion.yrel);

                    if (!uiCaptured && event.type == SDL_MOUSEWHEEL)
                        InputState::AddMouseWheel(event.wheel.x, event.wheel.y);

                    if (IsRuntimeWindowResizeEvent(event))
                        m_resizePending = true;
                }

                if (m_runtimeUi)
                {
                    InputState::SetMouseCapturedByUi(m_runtimeUi->WantsMouseCapture());
                    InputState::SetKeyboardCapturedByUi(m_runtimeUi->WantsKeyboardCapture());
                }

                return true;
            }

            bool ProcessRuntimeEvents()
            {
                EventSystem::QueuedEvent event;
                while (EventSystem::PollEvent(event))
                {
                    const EventType *type = std::get_if<EventType>(&event.key);
                    if (!type)
                        continue;

                    switch (*type)
                    {
                    case EventType::Quit:
                    case EventType::RequestExit:
                        return false;
                    case EventType::CompileScripts:
                        if (HasGlobalSystem<ScriptSystem>())
                            if (auto *ss = GetGlobalSystem<ScriptSystem>())
                                ss->Reload();
                        break;
                    case EventType::Resize:
                        m_resizePending = true;
                        break;
                    case EventType::CompileShaders:
                    {
                        std::optional<size_t> hash = std::nullopt;
                        if (event.payload.has_value() && event.payload.type() == typeid(size_t))
                            hash = std::any_cast<size_t>(event.payload);
                        m_renderer.PollShaders(hash);
                        CommandBuffer::ClearCache();
                        break;
                    }
                    case EventType::Screenshot:
                    {
                        std::string path;
                        if (event.payload.has_value() && event.payload.type() == typeid(std::string))
                            path = std::any_cast<std::string>(event.payload);
                        m_renderer.RequestScreenshot(std::move(path));
                        break;
                    }
                    default:
                        break;
                    }
                }

                return true;
            }

            void ResizeSwapchain()
            {
                const WindowDrawableExtent extent = GetWindowDrawableExtent(m_window);
                if (!extent.IsValid())
                    return;

                m_renderer.Resize(static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height));
                m_resizePending = false;
            }

            SDL_Window *m_window = nullptr;
            RuntimeSceneRenderer &m_renderer;
            RuntimeUiSystem *m_runtimeUi = nullptr;
            bool m_resizePending = false;
        };
    } // namespace

    int RunPlayerHost(int argc, char *argv[], PlayerHostDesc desc)
    {
        Path::Init();
        Log::Init();
        ScopedFileWatcherState fileWatcherState(desc.fileWatchersEnabled);

        try
        {
            const GraphicsApiSelection apiSelection = ResolveGraphicsApi(argc, argv);
            if (!apiSelection.Succeeded())
            {
                PE_ERROR("%s", apiSelection.error.c_str());
                return 1;
            }

            const ProjectSelection projectSelection = ResolveProjectSelection();
            ApplyProjectSelectionAssetsRoot(projectSelection);
            RuntimeStartupSceneResolveOptions startupSceneOptions{};
            startupSceneOptions.allowEditorRestore = true;
            const RuntimeStartupSceneSelection startupScene =
                ResolveRuntimeStartupScene(projectSelection, startupSceneOptions);
            LogProjectSelection(projectSelection, startupScene);
            PE_INFO("[Runtime] Active assets root: %s", Path::Assets.c_str());

            const PeGraphicsApi api = apiSelection.api;
            PE_INFO("Selected graphics API: %s (%s)",
                    PeGraphicsApiName(api),
                    GraphicsApiSelectionSourceName(apiSelection.source));

            int displayIndex = 0;
            std::string displayError;
            if (!TryParseRuntimeDisplayIndexArg(argc, argv, displayIndex, &displayError))
                PE_ERROR("%s", displayError.c_str());

            RuntimeSdlSession sdl;
            CoreEventSession events;

            RuntimeWindowDesc windowDesc;
            windowDesc.title = "PhasmaPlayer";
            windowDesc.api = api;
            windowDesc.displayIndex = displayIndex;
#if !defined(PE_WIN32)
            windowDesc.flags |= SDL_WINDOW_MAXIMIZED;
#endif

            RuntimeWindow window(windowDesc);
            RuntimeRhiSession rhi(window.Get(), api, true);

            // Mesh cook mode (desktop tooling): import a source model via Assimp and write a cooked
            // ".pemesh", then exit before the frame loop. The RHI is up so the importer's GPU upload
            // path runs unchanged; the cooked file is the player's portable, Assimp-free mesh asset.
            if (!desc.cookModelPath.empty())
            {
                PE_INFO("[Cook] Importing '%s' -> '%s'", desc.cookModelPath.c_str(), desc.cookOutputPath.c_str());
                ModelAsset *model = ModelAsset::Load(desc.cookModelPath);
                if (!model)
                {
                    PE_ERROR("[Cook] Failed to import source model: %s", desc.cookModelPath.c_str());
                    return 1;
                }
                const bool cooked = ModelAssetCooked::WriteToFile(model, desc.cookOutputPath);
                delete model;
                ModelAsset::DestroyDefaults();
                return cooked ? 0 : 1;
            }
            {
                Scene scene;
                PlayerSceneRegistration sceneRegistration(scene);
                PlayerRuntimeCleanup runtimeCleanup;

                runtimeCleanup.MarkGlobalSystemsCreated();
                CreateGlobalSystem<AnimationSystem>()->Init(nullptr);
#ifdef PE_PHYSICS
                CreateGlobalSystem<PhysicsSystem>()->Init(nullptr);
#endif
#ifdef PE_AUDIO
                CreateGlobalSystem<AudioSystem>()->Init(nullptr);
#endif

                if (!startupScene.IsExplicitEmpty() && !startupScene.scenePath.empty())
                {
                    if (std::filesystem::exists(startupScene.scenePath))
                    {
                        PE_INFO("[Runtime] Loading startup scene in player: %s",
                                startupScene.scenePath.generic_string().c_str());
                        scene.LoadScene(startupScene.scenePath);
                    }
                    else
                    {
                        PE_WARN("[Runtime] Skipping missing startup scene: %s",
                                startupScene.scenePath.generic_string().c_str());
                    }
                }

                RuntimeSceneRenderer renderer(scene);
                renderer.Init(nullptr);

                RuntimeUiSystem runtimeUi;
                RuntimeUiSystem *runtimeUiPtr = nullptr;
                if (desc.runtimeUiBackendFactory)
                {
                    if (runtimeUi.Init(desc.runtimeUiBackendFactory(), renderer.GetDisplayRT()))
                    {
                        SetActiveRuntimeUi(&runtimeUi);
                        renderer.SetRuntimeUi(&runtimeUi);
                        runtimeUiPtr = &runtimeUi;
                        PE_INFO("[RuntimeUI] Running with backend: %s", runtimeUi.GetBackendName().c_str());
                    }
                }

                CreateGlobalSystem<ScriptSystem>()->Init(nullptr);
                RegisterPlayerFileWatchers();
                FileWatcher::Start();
                RuntimePlaySessionStartDesc playStart{};
                playStart.callScriptInit = true;
                StartRuntimePlaySession(scene, playStart);

                {
                    PlayerFramePump framePump(window.Get(), renderer, runtimeUiPtr);
                    PE_INFO("[Runtime] Player frame pump running (startup scene render)");
                    framePump.Run();
                }

                StopRuntimePlaySession();
                FileWatcher::StopAndJoin();
                ThreadPool::GUI.WaitIdle();
                ThreadPool::General.WaitIdle();
                FileWatcher::Clear();
                renderer.SetRuntimeUi(nullptr);
                SetActiveRuntimeUi(nullptr);
                runtimeUi.Shutdown();
                renderer.Destroy();
                runtimeCleanup.Cleanup();
            }
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
