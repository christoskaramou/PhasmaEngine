#include "Runtime/PlayerHost.h"
#include "API/GraphicsApiSelection.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "Project/ProjectSelection.h"
#include "Camera/Camera.h"
#include "Render/RuntimeSceneRenderer.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/ModelAsset.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Runtime/RuntimeHost.h"
#include "Runtime/RuntimePlaySession.h"
#include "Runtime/RenderDocCaptureShortcut.h"
#include "Runtime/RuntimeStartup.h"
#include "Script/Bindings/Input/InputState.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/Physics2DSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Voxel/VoxelSystem.h"
#include "UI/RuntimeUi.h"
#include "Window/WindowEvents.h"

#if defined(PE_ANDROID)
#include <SDL_system.h>
#include <jni.h>
#endif

namespace pe
{
    namespace
    {
        struct RuntimeUiSafeArea
        {
            float minX = 0.0f;
            float minY = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            bool valid = false;
        };

#if defined(PE_ANDROID)
        int QueryPhasmaPlayerSafeInset(JNIEnv *env, jclass activityClass, const char *methodName)
        {
            jmethodID method = env->GetStaticMethodID(activityClass, methodName, "()I");
            if (!method || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return 0;
            }

            const jint value = env->CallStaticIntMethod(activityClass, method);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                return 0;
            }
            return static_cast<int>(value);
        }

        RuntimeUiSafeArea QueryAndroidRuntimeUiSafeArea(uint32_t surfaceWidth, uint32_t surfaceHeight)
        {
            RuntimeUiSafeArea area{};
            if (surfaceWidth == 0 || surfaceHeight == 0)
                return area;

            JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
            if (!env)
                return area;

            jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
            if (!activity || env->ExceptionCheck())
            {
                env->ExceptionClear();
                return area;
            }

            jclass activityClass = env->GetObjectClass(activity);
            if (!activityClass || env->ExceptionCheck())
            {
                env->ExceptionClear();
                env->DeleteLocalRef(activity);
                return area;
            }

            const int left = QueryPhasmaPlayerSafeInset(env, activityClass, "getSafeAreaInsetLeft");
            const int top = QueryPhasmaPlayerSafeInset(env, activityClass, "getSafeAreaInsetTop");
            const int right = QueryPhasmaPlayerSafeInset(env, activityClass, "getSafeAreaInsetRight");
            const int bottom = QueryPhasmaPlayerSafeInset(env, activityClass, "getSafeAreaInsetBottom");
            env->DeleteLocalRef(activityClass);
            env->DeleteLocalRef(activity);

            const float clampedLeft = static_cast<float>(std::clamp(left, 0, static_cast<int>(surfaceWidth)));
            const float clampedTop = static_cast<float>(std::clamp(top, 0, static_cast<int>(surfaceHeight)));
            const float clampedRight = static_cast<float>(std::clamp(right, 0, static_cast<int>(surfaceWidth)));
            const float clampedBottom = static_cast<float>(std::clamp(bottom, 0, static_cast<int>(surfaceHeight)));

            area.minX = clampedLeft;
            area.minY = clampedTop;
            area.width = std::max(0.0f, static_cast<float>(surfaceWidth) - clampedLeft - clampedRight);
            area.height = std::max(0.0f, static_cast<float>(surfaceHeight) - clampedTop - clampedBottom);
            area.valid = area.width > 0.0f && area.height > 0.0f;
            static int s_loggedLeft = -1;
            static int s_loggedTop = -1;
            static int s_loggedRight = -1;
            static int s_loggedBottom = -1;
            if (area.valid &&
                (left != s_loggedLeft || top != s_loggedTop || right != s_loggedRight || bottom != s_loggedBottom))
            {
                PE_INFO("[RuntimeUI] Android safe area insets: left=%d top=%d right=%d bottom=%d", left, top, right, bottom);
                s_loggedLeft = left;
                s_loggedTop = top;
                s_loggedRight = right;
                s_loggedBottom = bottom;
            }
            return area;
        }
#else
        RuntimeUiSafeArea QueryAndroidRuntimeUiSafeArea(uint32_t, uint32_t)
        {
            return {};
        }
#endif

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

        // The runtime-UI HUD is authored at desktop pixel sizes. On high-density surfaces (phones,
        // hi-DPI monitors) those sizes are physically tiny -- the Android HUD rendered as an invisible
        // speck. Scale the UI by real display density, falling back to surface-height scaling when the
        // driver reports no DPI. Clamped so standard ~96 dpi desktops stay at 1.0 (no editor/desktop
        // regression) and pathological DPI reports can't balloon the UI.
        float ComputeRuntimeUiScale(SDL_Window *window, uint32_t surfaceHeight)
        {
            float scale = 0.0f;

            if (window)
            {
                const int display = SDL_GetWindowDisplayIndex(window);
                float ddpi = 0.0f;
                float hdpi = 0.0f;
                float vdpi = 0.0f;
                if (display >= 0 && SDL_GetDisplayDPI(display, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 0.0f)
                    scale = ddpi / 96.0f;
            }

            if (scale <= 0.0f && surfaceHeight > 0)
                scale = static_cast<float>(surfaceHeight) / 1080.0f;

            if (scale <= 0.0f)
                scale = 1.0f;

            return std::clamp(scale, 1.0f, 4.0f);
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

                if (!ProcessEvents())
                    return false;

                if (m_resizePending || WindowDrawableExtentChanged())
                    ResizeSwapchain();

                if (IsWindowMinimized(m_window))
                {
                    SDL_Delay(16);
                    return true;
                }

                if (m_runtimeUi)
                {
                    if (Image *displayRT = m_renderer.GetDisplayRT())
                    {
                        m_runtimeUi->SetFrameSurfaceSize(displayRT->GetWidth(), displayRT->GetHeight());
                        m_runtimeUi->SetFrameUiScale(ComputeRuntimeUiScale(m_window, displayRT->GetHeight()));
                        const RuntimeUiSafeArea safeArea =
                            QueryAndroidRuntimeUiSafeArea(displayRT->GetWidth(), displayRT->GetHeight());
                        if (safeArea.valid)
                            m_runtimeUi->SetFrameSafeArea(safeArea.minX, safeArea.minY, safeArea.width, safeArea.height);
                    }
                    m_runtimeUi->BeginFrame();
                }

                UpdateGlobalSystems();
                const bool keepRunning = ProcessRuntimeEvents();
                if (m_runtimeUi)
                    m_runtimeUi->SyncSceneWidgets(m_renderer.GetScene());
                if (m_runtimeUi)
                    m_runtimeUi->EndFrame();

                if (!keepRunning)
                    return false;

                m_renderer.Update();
                m_renderer.Draw();
                FrameTimer::Instance().CountUpdatesStamp();
                FrameTimer::Instance().CountCpuTotalStamp();
                LogFrameRate();
                return true;
            }

            void Run()
            {
                while (Frame())
                {
                }
            }

        private:
            bool WindowDrawableExtentChanged() const
            {
                Image *displayRT = m_renderer.GetDisplayRT();
                if (!displayRT)
                    return false;

                const WindowDrawableExtent extent = GetWindowDrawableExtent(m_window);
                return extent.IsValid() &&
                       (extent.width != static_cast<int>(displayRT->GetWidth()) ||
                        extent.height != static_cast<int>(displayRT->GetHeight()));
            }

            // Emit one averaged FPS line per second. The on-screen runtime-UI HUD is unreadable on the
            // packaged player's surface (and device screencaps come back black), so this is the portable
            // way to measure frame rate from PhasmaEngine.log.
            void LogFrameRate()
            {
                m_fpsAccumSeconds += FrameTimer::Instance().GetDelta();
                ++m_fpsAccumFrames;
                if (m_fpsAccumSeconds < 1.0)
                    return;

                const double avgFps = m_fpsAccumFrames / m_fpsAccumSeconds;
                const double avgMs = (m_fpsAccumSeconds * 1000.0) / m_fpsAccumFrames;
                PE_INFO("[Runtime] FPS: %.1f (%.2f ms/frame avg over %u frames)",
                        avgFps,
                        avgMs,
                        m_fpsAccumFrames);
                m_fpsAccumSeconds = 0.0;
                m_fpsAccumFrames = 0;
            }

            bool ProcessEvents()
            {
                SDL_Event event{};
                InputState::BeginFrame();
                while (SDL_PollEvent(&event))
                {
                    if (IsRenderDocCaptureShortcut(event))
                        TriggerRenderDocCaptureShortcut();

                    const bool uiCaptured = m_runtimeUi && m_runtimeUi->ProcessEvent(event);

                    if (ShouldQuitFromEvent(event, uiCaptured))
                        return false;

                    if (!uiCaptured && event.type == SDL_MOUSEMOTION)
                        InputState::AddMouseMotion(event.motion.xrel, event.motion.yrel);

                    if (!uiCaptured && event.type == SDL_MOUSEWHEEL)
                        InputState::AddMouseWheel(event.wheel.x, event.wheel.y);

                    // Touch (Android). SDL_TouchFingerEvent coords/deltas are normalized [0,1].
                    if (!uiCaptured && event.type == SDL_FINGERDOWN)
                        InputState::OnFingerDown(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
                    else if (!uiCaptured && event.type == SDL_FINGERUP)
                        InputState::OnFingerUp(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
                    else if (!uiCaptured && event.type == SDL_FINGERMOTION)
                        InputState::OnFingerMotion(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y,
                                                   event.tfinger.dx, event.tfinger.dy);

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
                    case EventType::PresentMode:
                    {
                        const PePresentMode previous = RHII.GetSurface()->GetPresentMode();
                        RHII.GetSurface()->SetPresentMode(Settings::Get<SceneSettings>().preferred_present_mode);
                        if (RHII.GetSurface()->GetPresentMode() != previous)
                            m_resizePending = true;
                        break;
                    }
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
            double m_fpsAccumSeconds = 0.0;
            uint32_t m_fpsAccumFrames = 0;
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
            RuntimeRhiSession rhi(window.Get(), api, false);

            const std::optional<PePresentMode> forcedPresentMode = ReadStartupPresentModeOverride();
            PePresentMode effectiveStartupMode = PE_PRESENT_MODE_FIFO;
            {
                SceneSettings &settings = Settings::Get<SceneSettings>();
                const RuntimeStartupSceneSettings startupSceneSettings =
                    ReadRuntimeStartupSceneSettings(startupScene);
                const PePresentMode startupMode = forcedPresentMode.value_or(
                    startupSceneSettings.presentMode.value_or(PE_PRESENT_MODE_FIFO));
                settings.preferred_present_mode = startupMode;
                RHII.GetSurface()->SetPresentMode(startupMode);
                effectiveStartupMode = RHII.GetSurface()->GetPresentMode();
                settings.preferred_present_mode = effectiveStartupMode;
            }
            if (RHII.GetSwapchain())
                RHII.ChangePresentMode(effectiveStartupMode);
            else
                RHII.InitSwapchain();

            {
                Scene scene;
                PlayerSceneRegistration sceneRegistration(scene);
                PlayerRuntimeCleanup runtimeCleanup;

                runtimeCleanup.MarkGlobalSystemsCreated();
                CreateGlobalSystem<AnimationSystem>()->Init(nullptr);
#ifdef PE_PHYSICS
                CreateGlobalSystem<PhysicsSystem>()->Init(nullptr);
#endif
#ifdef PE_PHYSICS2D
                CreateGlobalSystem<Physics2DSystem>()->Init(nullptr);
#endif
#ifdef PE_AUDIO
                CreateGlobalSystem<AudioSystem>()->Init(nullptr);
#endif
                // Voxel subsystem: created idle; stays a no-op until a world is created via API
                // (voxel.create / VoxelSystem::CreateWorld). Permanent subsystem, no feature guard.
                CreateGlobalSystem<voxel::VoxelSystem>()->Init(nullptr);

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

                // LoadScene() copies the scene's saved present_mode into SceneSettings and queues a
                // PresentMode event. An explicit env/config preference outranks the scene, so re-assert it
                // before the frame pump applies the queued event.
                if (forcedPresentMode)
                    Settings::Get<SceneSettings>().preferred_present_mode = effectiveStartupMode;

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
