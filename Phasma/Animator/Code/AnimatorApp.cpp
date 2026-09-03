#include "AnimatorApp.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Swapchain.h"
#include "AnimationTimeline.h"
#include "Animation/ClipExchange.h"
#include "Camera/Camera.h"
#include "GUI/Backends/GUIBackend.h"
#include "GUI/GUIState.h"
#include "GUI/Helpers.h"
#include "Project/ProjectSelection.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Primitives.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Scene/SelectionManager.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/Physics2DSystem.h"
#include "Systems/PhysicsSystem.h"
#include "Terrain/TerrainSystem.h"
#include "Voxel/VoxelSystem.h"
#include "Window/WindowEvents.h"
#include "imgui/ImGuizmo.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"

#include <nlohmann/json.hpp>
#if defined(PE_WIN32)
#include <SDL_syswm.h>
#include <commdlg.h>
#include <windows.h>
#endif

namespace pe
{
    AnimatorApp *AnimatorApp::s_instance = nullptr;

    RuntimeSceneRenderer *GetAnimatorRenderer()
    {
        return AnimatorApp::Instance() ? &AnimatorApp::Instance()->Renderer() : nullptr;
    }

    namespace
    {
        constexpr size_t kLogLines = 400;

        const char *LogLevelName(LogType type)
        {
            return type == LogType::Error ? "error" : type == LogType::Warn ? "warn"
                                                                            : "info";
        }

        std::string ActionResult(const nlohmann::json &j)
        {
            return j.dump();
        }
    } // namespace

    namespace
    {
        constexpr struct
        {
            const char *name;
            GUIStyle style;
        } kStyles[] = {{"Classic", GUIStyle::Classic}, {"Dark", GUIStyle::Dark}, {"Light", GUIStyle::Light}, {"Modern", GUIStyle::Modern}, {"Unity", GUIStyle::Unity}, {"Unreal", GUIStyle::Unreal}};

        const char *StyleName(GUIStyle style)
        {
            for (const auto &entry : kStyles)
                if (entry.style == style)
                    return entry.name;
            return kStyles[4].name;
        }

        bool StyleByName(std::string name, GUIStyle &out)
        {
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto &entry : kStyles)
            {
                std::string candidate = entry.name;
                for (char &c : candidate)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (candidate == name)
                {
                    out = entry.style;
                    return true;
                }
            }
            return false;
        }

        // the theme picks its font too, the way the editor pushes one per style every frame
        void ApplyStyle(GUIStyle style)
        {
            GUIState::s_guiStyle = style;
            ui::ApplyTheme(style);
            ImFont *font = GUIState::s_fontClassic;
            switch (style)
            {
            case GUIStyle::Dark:
                font = GUIState::s_fontDark;
                break;
            case GUIStyle::Light:
                font = GUIState::s_fontLight;
                break;
            case GUIStyle::Modern:
                font = GUIState::s_fontModern;
                break;
            case GUIStyle::Unity:
                font = GUIState::s_fontUnity;
                break;
            case GUIStyle::Unreal:
                font = GUIState::s_fontUnreal;
                break;
            case GUIStyle::Classic:
                break;
            }
            ImGui::GetIO().FontDefault = font;
        }

        void SetFontScale(float scale)
        {
            ImGui::GetIO().FontGlobalScale = std::clamp(scale, 0.5f, 2.5f); // the editor's range
        }

        nlohmann::json ViewJson(const OrbitView &v)
        {
            return {{"target", {v.target.x, v.target.y, v.target.z}}, {"distance", v.distance}, {"yaw", v.yaw}, {"pitch", v.pitch}, {"ortho", v.orthographic}, {"ortho_size", v.orthographicSize}};
        }

        bool ViewFromJson(const nlohmann::json &j, OrbitView &v)
        {
            if (!j.is_object() || !j.contains("target") || !j["target"].is_array() || j["target"].size() != 3)
                return false;
            if (!std::all_of(j["target"].begin(), j["target"].end(), [](const nlohmann::json &value)
                             { return value.is_number(); }) ||
                (j.contains("distance") && !j["distance"].is_number()) ||
                (j.contains("yaw") && !j["yaw"].is_number()) ||
                (j.contains("pitch") && !j["pitch"].is_number()) ||
                (j.contains("ortho") && !j["ortho"].is_boolean()) ||
                (j.contains("ortho_size") && !j["ortho_size"].is_number()))
                return false;
            v.target = vec3(j["target"][0].get<float>(), j["target"][1].get<float>(), j["target"][2].get<float>());
            v.distance = j.value("distance", 2.f);
            v.yaw = j.value("yaw", 0.f);
            v.pitch = j.value("pitch", 0.f);
            v.orthographic = j.value("ortho", false);
            v.orthographicSize = j.value("ortho_size", 1.f);
            return std::isfinite(v.target.x) && std::isfinite(v.target.y) && std::isfinite(v.target.z) &&
                   std::isfinite(v.distance) && v.distance > 0.f && std::isfinite(v.yaw) && std::isfinite(v.pitch) &&
                   std::isfinite(v.orthographicSize) && v.orthographicSize > 0.f;
        }
    } // namespace

    AnimatorApp::AnimatorApp(int argc, char *argv[]) : m_renderer(m_scene)
    {
        s_instance = this;
        Path::Init();
        if (Path::EditorAssets.empty())
        {
            const std::string beside = Path::Executable + "EditorAssets/";
            if (std::filesystem::exists(beside))
                Path::EditorAssets = beside; // the editor's fonts, shipped beside both programs
        }
        // The active project's Assets (rig presets, models) like the editor and the player.
        const ProjectSelection project = ResolveProjectSelection();
        ApplyProjectSelectionAssetsRoot(project);
        PE_INFO("[Animator] Active assets root: %s", Path::Assets.c_str());

        Log::Attach(
            [this](const std::string &message, LogType type)
            {
                std::lock_guard lock(m_logMutex);
                m_log.push_back({type, message});
                if (m_log.size() > kLogLines)
                    m_log.pop_front();
            });

        SetActiveSceneGetter(ActiveScene);
        SetSceneRuntimeHooks(CreateDefaultSceneRuntimeHooks());
        SceneHostCallbacks sceneHost{};
        sceneHost.beforeMutation = WaitSceneMutation;
        SetSceneHostCallbacks(sceneHost);
        CameraRuntimeCallbacks cameraHooks = CreateDefaultCameraRuntimeCallbacks();
        cameraHooks.getAspect = ViewportAspect;
        SetCameraRuntimeCallbacks(cameraHooks);

        // The runtime's scene hooks reach every system the player has, so the animator creates the same set.
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
        CreateGlobalSystem<voxel::VoxelSystem>()->Init(nullptr);
        CreateGlobalSystem<terrain::TerrainSystem>()->Init(nullptr);
        // ponytail: the ImGui overlay is drawn into the display target at window resolution, so that target must be
        // window-sized; a scaled one clips the UI and the viewport image at the scale. Full-res is right for one
        // character anyway. Drawing the overlay after the upscale blit would lift this.
        Settings::Get<SceneSettings>().render_scale = 1.f;
        m_renderer.SetRuntimeSettingsForced(false);
        m_renderer.SetOverlay([this](CommandBuffer *cmd, Image *displayRT)
                              { DrawOverlay(cmd, displayRT); });
        m_renderer.Init(nullptr);
        ResetScene();

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        GUIBackend::ConfigureIO();
        ImGui::StyleColorsClassic();
        m_attachment.image = m_renderer.GetDisplayRT();
        m_attachment.loadOp = PE_LOAD_OP_LOAD;
        GUIBackend::Init(&m_attachment);
        // the editor's fonts per theme: Inter (Unity, Dark), Roboto (Unreal), Open Sans (Light, Modern)
        GUIState::s_fontClassic = io.Fonts->AddFontDefault();
        auto loadFont = [&](const char *file, float size)
        {
            const std::string path = Path::EditorAssets + "Fonts/" + file;
            return std::filesystem::exists(path) ? io.Fonts->AddFontFromFileTTF(path.c_str(), size)
                                                 : GUIState::s_fontClassic;
        };
        GUIState::s_fontUnity = GUIState::s_fontDark = loadFont("Inter-Regular.ttf", 15.f);
        GUIState::s_fontUnreal = loadFont("Roboto-Regular.ttf", 15.f);
        GUIState::s_fontLight = GUIState::s_fontModern = loadFont("OpenSans-Regular.ttf", 17.f);
        GUIBackend::CreateFontsTexture();
        ApplyStyle(GUIStyle::Unity);

        m_timeline = std::make_unique<AnimationTimeline>();
        m_timeline->Init(this);
        LoadConfig(argc, argv);
    }

    AnimatorApp::~AnimatorApp()
    {
        m_renderer.WaitAllFramesCommands();
        RHII.WaitDeviceIdle();
        SaveConfig();
        m_timeline.reset(); // releases its reference-frame texture through the backend below
        if (GUIState::s_viewportTextureId)
            GUIBackend::ReleaseImageTexture(GUIState::s_viewportTextureId);
        Image::Destroy(GUIState::s_sceneViewImage);
        GUIBackend::Shutdown();
        ImGui::DestroyContext();
        m_renderer.Destroy();
        ThreadPool::GUI.WaitIdle();
        ThreadPool::General.WaitIdle();
        DestroyGlobalSystems();
        ModelAsset::DestroyDefaults();
        Context::Remove();
        SetCameraRuntimeCallbacks({});
        SetSceneHostCallbacks({});
        SetSceneRuntimeHooks({});
        SetActiveSceneGetter(nullptr);
        s_instance = nullptr;
    }

    Scene *AnimatorApp::ActiveScene()
    {
        return s_instance ? &s_instance->m_scene : nullptr;
    }

    void AnimatorApp::WaitSceneMutation()
    {
        if (s_instance)
            s_instance->m_renderer.WaitAllFramesCommands();
    }

    float AnimatorApp::ViewportAspect()
    {
        // the camera projects at the Timeline's viewport region, so the whole render fills it and pick rays land
        const float region = AnimationTimeline::RegionAspect();
        if (region > 0.f)
            return region;
        return RHII.GetHeight() > 0 ? static_cast<float>(RHII.GetWidth()) / static_cast<float>(RHII.GetHeight())
                                    : 16.f / 9.f;
    }

    // -------------------------------------------------------------------------
    // scene + model
    // -------------------------------------------------------------------------
    void AnimatorApp::ResetScene()
    {
        m_renderer.WaitAllFramesCommands();
        SelectionManager::Instance().ClearSelection();
        if (!m_modelPath.empty())
            m_grid = Settings::Get<SceneSettings>().draw_grid;
        m_scene.NewScene();
        if (m_scene.GetCameras().empty())
        {
            Camera *camera = m_scene.AddCamera();
            m_scene.SetActiveCamera(camera);
            camera->SetPosition(vec3(0.f, 1.f, -3.f));
            camera->SetEuler(vec3(0.f));
            camera->Update();
        }
        m_scene.CreateDirectionalLight();
        Settings::Get<SceneSettings>().draw_grid = m_grid;
        Settings::Get<SceneSettings>().render_scale = 1.f;
        EnsureGround();
        m_scene.MarkDirty();
        m_modelPath.clear();
        m_modelRoots.clear();
    }

    bool AnimatorApp::OpenModel(const std::filesystem::path &pemesh, std::string *error)
    {
        std::error_code ec;
        const std::filesystem::path path = std::filesystem::absolute(pemesh, ec);
        if (!std::filesystem::exists(path, ec) || !ModelAssetCooked::IsCookedPath(path))
        {
            if (error)
                *error = "not a .pemesh file: " + pemesh.generic_string();
            return false;
        }
        ModelAsset *model = ModelAsset::Load(path);
        if (!model)
        {
            if (error)
                *error = "failed to load " + path.generic_string();
            return false;
        }
        OrbitView previousView;
        if (!m_modelPath.empty() && m_timeline->GetOrbitView(previousView))
            m_views[m_modelPath.generic_string()] = previousView;
        m_timeline->DropTarget();
        ResetScene(); // one character at a time: the previous model goes with its scene
        const SceneNodeHandle handle = m_scene.AddModelDeferred(model);
        m_scene.UpdateGeometryBuffers();
        m_scene.MarkDirty();
        m_modelRoots = m_scene.GetModelRootNodes(model);
        if (m_modelRoots.empty() && handle.nodeId)
            m_modelRoots.push_back(handle.nodeId);
        // Stand it on the floor: the lowest point of the meshes goes to the ground plane, so the feet the contact
        // tools plant are the feet on the visible floor. Rig space is node-local, so nothing the tools measure moves.
        float lowest = std::numeric_limits<float>::max();
        for (int i = 0; i < model->GetNodeCount(); i++)
            if (model->GetNodeMesh(i) >= 0)
                lowest = std::min(lowest, model->GetNodeWorldBoundingBox(i).min.y);
        if (std::isfinite(lowest) && lowest < std::numeric_limits<float>::max() && std::abs(lowest) > 1e-4f)
            for (NodeId *root : m_modelRoots)
                m_scene.SetLocalMatrix(root, glm::translate(mat4(1.f), vec3(0.f, -lowest, 0.f)) * m_scene.GetLocalMatrix(root));
        if (!m_modelRoots.empty())
            SelectionManager::Instance().Select(m_modelRoots.front()); // the Timeline follows the selection
        m_modelPath = path;
        RememberRecent(path);
        if (auto it = m_views.find(path.generic_string()); it != m_views.end())
            m_timeline->SetOrbitView(it->second); // where this character was last looked at
        SaveConfig();
        PE_INFO("[Animator] Opened %s", path.generic_string().c_str());
        return true;
    }

    // -------------------------------------------------------------------------
    // config: the last model and the viewport share, beside the executable
    // -------------------------------------------------------------------------
    std::filesystem::path AnimatorApp::ConfigPath() const
    {
        return std::filesystem::path(Path::Executable) / "animator_config.json";
    }

    void AnimatorApp::LoadConfig(int argc, char *argv[])
    {
        std::filesystem::path open;
        for (int i = 1; i + 1 < argc; i++)
            if (std::string_view(argv[i]) == "--open")
                open = argv[i + 1];
        std::ifstream in(ConfigPath());
        if (in)
        {
            const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object())
            {
                if (open.empty() && j.contains("last_model") && j["last_model"].is_string())
                    open = j["last_model"].get<std::string>();
                if (j.contains("viewport_share") && j["viewport_share"].is_number())
                    m_timeline->SetViewportShare(j["viewport_share"].get<float>());
                GUIStyle style;
                if (j.contains("style") && j["style"].is_string() && StyleByName(j["style"].get<std::string>(), style))
                    ApplyStyle(style);
                if (j.contains("font_scale") && j["font_scale"].is_number())
                    SetFontScale(j["font_scale"].get<float>());
                if (j.contains("recent") && j["recent"].is_array())
                    for (const auto &entry : j["recent"])
                        if (entry.is_string() && m_recent.size() < 8)
                            m_recent.emplace_back(entry.get<std::string>());
                if (j.contains("bookmarks") && j["bookmarks"].is_array())
                    for (const auto &entry : j["bookmarks"])
                    {
                        Bookmark bookmark;
                        if (entry.is_object() && entry.contains("name") && entry["name"].is_string() &&
                            ViewFromJson(entry, bookmark.view))
                        {
                            bookmark.name = entry["name"].get<std::string>();
                            m_bookmarks.push_back(bookmark);
                        }
                    }
                if (j.contains("views") && j["views"].is_object())
                    for (const auto &[key, value] : j["views"].items())
                    {
                        OrbitView view;
                        if (ViewFromJson(value, view))
                            m_views[key] = view;
                    }
                m_grid = j.contains("grid") && j["grid"].is_boolean() ? j["grid"].get<bool>() : true;
                Settings::Get<SceneSettings>().draw_grid = m_grid;
                GUIState::s_useOrientationGizmo =
                    j.contains("gizmo") && j["gizmo"].is_boolean() ? j["gizmo"].get<bool>() : true;
                SetGroundVisible(j.contains("ground") && j["ground"].is_boolean() ? j["ground"].get<bool>() : true);
                m_timeline->SetShowBoneNames(j.contains("bone_names") && j["bone_names"].is_boolean()
                                                 ? j["bone_names"].get<bool>()
                                                 : false);
            }
        }
        std::string error;
        if (!open.empty() && !OpenModel(open, &error))
            PE_WARN("[Animator] %s", error.c_str());
    }

    void AnimatorApp::SaveConfig()
    {
        OrbitView view;
        if (!m_modelPath.empty() && m_timeline && m_timeline->GetOrbitView(view))
            m_views[m_modelPath.generic_string()] = view;
        nlohmann::json j;
        j["last_model"] = m_modelPath.generic_string();
        j["viewport_share"] = m_timeline ? m_timeline->ViewportShare() : 0.6f;
        j["style"] = StyleName(GUIState::s_guiStyle);
        j["font_scale"] = ImGui::GetIO().FontGlobalScale;
        j["recent"] = nlohmann::json::array();
        for (const std::filesystem::path &path : m_recent)
            j["recent"].push_back(path.generic_string());
        j["bookmarks"] = nlohmann::json::array();
        for (const Bookmark &bookmark : m_bookmarks)
        {
            nlohmann::json entry = ViewJson(bookmark.view);
            entry["name"] = bookmark.name;
            j["bookmarks"].push_back(entry);
        }
        j["views"] = nlohmann::json::object();
        for (const auto &[key, value] : m_views)
            if (m_views.size() <= 32 || std::find(m_recent.begin(), m_recent.end(), std::filesystem::path(key)) != m_recent.end())
                j["views"][key] = ViewJson(value);
        j["grid"] = Settings::Get<SceneSettings>().draw_grid;
        j["gizmo"] = GUIState::s_useOrientationGizmo;
        j["ground"] = m_ground;
        j["bone_names"] = m_timeline && m_timeline->ShowBoneNames();
        std::ofstream out(ConfigPath());
        if (out)
            out << j.dump(2) << '\n';
    }

    void AnimatorApp::RememberRecent(const std::filesystem::path &path)
    {
        m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), path), m_recent.end());
        m_recent.insert(m_recent.begin(), path);
        if (m_recent.size() > 8)
            m_recent.resize(8);
    }

    // -------------------------------------------------------------------------
    // unsaved changes, saving, the title
    // -------------------------------------------------------------------------
    void AnimatorApp::RequestOpen(const std::filesystem::path &pemesh)
    {
        if (m_timeline && m_timeline->IsDirty())
        {
            m_pendingOpen = pemesh;
            m_pendingQuit = false;
            m_promptPending = true;
            return;
        }
        std::string error;
        if (!OpenModel(pemesh, &error))
            PE_WARN("[Animator] %s", error.c_str());
    }

    void AnimatorApp::RequestQuit()
    {
        if (m_timeline && m_timeline->IsDirty())
        {
            m_pendingOpen.clear();
            m_pendingQuit = true;
            m_promptPending = true;
            return;
        }
        m_quit = true;
    }

    bool AnimatorApp::SaveAll(std::string *error)
    {
        if (!m_timeline)
            return false;
        if (m_timeline->Rig().WeightDirty() && !m_timeline->HasTarget())
        {
            if (error)
                *error = "the edited skin weights no longer have a model target";
            return false;
        }
        if (m_timeline->HasTarget() && !m_timeline->SaveClips())
        {
            if (error)
                *error = "the clips could not be written to " + m_modelPath.generic_string();
            return false;
        }
        if (m_timeline->Rig().DocumentDirty() && !m_timeline->Rig().SaveDocument(error))
            return false;
        return true;
    }

    void AnimatorApp::UpdateTitle()
    {
        std::string title = "PhasmaAnimator";
        if (!m_modelPath.empty())
            title += " - " + m_modelPath.filename().string();
        if (m_timeline && m_timeline->IsDirty())
            title += " *";
        if (title != m_title)
        {
            m_title = title;
            if (SDL_Window *window = RHII.GetWindow())
                SDL_SetWindowTitle(window, m_title.c_str());
        }
    }

    // -------------------------------------------------------------------------
    // the ground plane: a flat receiver under the origin, so the character throws a shadow and the feet read
    // -------------------------------------------------------------------------
    void AnimatorApp::EnsureGround()
    {
        m_groundNode = nullptr;
        ModelAsset *plane = Primitives::CreatePlane(40.f, 40.f);
        if (!plane)
            return;
        const SceneNodeHandle handle = m_scene.AddModelDeferred(plane); // the route the script primitives take
        m_groundNode = handle.nodeId;
        if (!m_groundNode)
            return;
        // a hair under the grid lines, so the two never fight for the same depth
        m_scene.SetLocalMatrix(m_groundNode, glm::translate(mat4(1.f), vec3(0.f, -0.005f, 0.f)));
        m_scene.SetNodeRenderVisible(m_groundNode, m_ground);
        m_scene.UpdateGeometryBuffers();
    }

    void AnimatorApp::SetGroundVisible(bool visible)
    {
        m_ground = visible;
        if (m_groundNode && m_scene.IsNodeAlive(m_groundNode))
        {
            m_scene.SetNodeRenderVisible(m_groundNode, visible);
            m_scene.MarkDirty();
        }
    }

    // -------------------------------------------------------------------------
    // frame
    // -------------------------------------------------------------------------
    bool AnimatorApp::WindowRenderable() const
    {
        SDL_Window *window = RHII.GetWindow();
        return window && !IsWindowMinimized(window) && GetWindowDrawableExtent(window).IsValid();
    }

    bool AnimatorApp::ProcessEvents()
    {
        SDL_Window *window = RHII.GetWindow();
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                RequestQuit(); // the close button asks about unsaved changes like File > Exit
            if (event.type == SDL_DROPFILE && event.drop.file)
            {
                const std::string dropped = event.drop.file;
                SDL_free(event.drop.file);
                RequestOpen(dropped);
            }
            if (IsRuntimeWindowEventFor(event, window) &&
                (IsRuntimeWindowDisplayChangedEvent(event) || IsRuntimeWindowResizeEvent(event)))
                m_resizePending = true;
        }
        EventSystem::QueuedEvent queued;
        while (EventSystem::PollEvent(queued))
        {
            const EventType *type = std::get_if<EventType>(&queued.key);
            if (!type)
                continue;
            if (*type == EventType::Quit)
                RequestQuit();
            if (*type == EventType::Resize)
                m_resizePending = true;
            else if (*type == EventType::CompileShaders)
            {
                std::optional<size_t> hash;
                if (queued.payload.has_value() && queued.payload.type() == typeid(size_t))
                    hash = std::any_cast<size_t>(queued.payload);
                m_renderer.PollShaders(hash);
                CommandBuffer::ClearCache();
            }
        }
        return true;
    }

    void AnimatorApp::ApplyPendingResize()
    {
        SDL_Window *window = RHII.GetWindow();
        const WindowDrawableExtent extent = GetWindowDrawableExtent(window);
        const bool changed = extent.IsValid() && (extent.width != static_cast<int>(RHII.GetWidth()) ||
                                                  extent.height != static_cast<int>(RHII.GetHeight()));
        if ((!m_resizePending && !changed) || !WindowRenderable() || !extent.IsValid())
            return;
        m_renderer.Resize(static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height));
        m_resizePending = false;
    }

    bool AnimatorApp::Frame()
    {
        RHII.NextFrame();
        FrameTimer::Instance().Tick();
        Profiler::BeginFrame();
        struct ProfilerFrame
        {
            ~ProfilerFrame() { Profiler::EndFrame(); }
        } profilerFrame;

        m_renderer.WaitPreviousFrameCommands();
        bool presentationReady = true;
        if (WindowRenderable())
        {
            try
            {
                if (Swapchain *swapchain = RHII.GetSwapchain())
                    presentationReady = swapchain->WaitForNextFrame();
            }
            catch (const SwapchainOutOfDateError &)
            {
                m_resizePending = true;
                presentationReady = false;
            }
        }
        if (!presentationReady)
        {
            const bool keepRunning = ProcessEvents();
            ApplyPendingResize();
            SDL_Delay(1);
            return keepRunning && !m_quit;
        }
        if (!ProcessEvents())
            return false;
        ApplyPendingResize();
        if (!WindowRenderable())
        {
            SDL_Delay(16);
            return !m_quit;
        }
        PollCommandFile();
        if (m_quit)
            return false;
        EnsureSceneTexture();

        GUIBackend::NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        UpdateGlobalSystems();
        DrawShell();
        UpdateTitle();
        ImGui::Render();

        m_renderer.Update();
        m_renderer.Draw();
        FrameTimer::Instance().CountCpuTotalStamp();
        return !m_quit;
    }

    // -------------------------------------------------------------------------
    // shell: the menu bar and the Timeline under it
    // -------------------------------------------------------------------------
    void AnimatorApp::OpenDialog()
    {
        std::string picked;
        if (PickFile("Open .pemesh", "Cooked model (*.pemesh)\0*.pemesh\0All files\0*.*\0", picked))
        {
            RequestOpen(picked);
            return;
        }
#if !defined(PE_WIN32)
        m_openPopupPending = true; // no native dialog: a path field
#endif
    }

    void AnimatorApp::ImportModelDialog()
    {
        std::string source;
        if (!PickFile("Import source model",
                      "Models (*.gltf;*.glb;*.fbx;*.dae;*.obj)\0*.gltf;*.glb;*.fbx;*.dae;*.obj\0All files\0*.*\0",
                      source))
            return;
        std::string output;
        if (!PickSaveFile("Save cooked model", "Cooked model (*.pemesh)\0*.pemesh\0", "pemesh", output))
            return;
        std::string error;
        if (!ClipExchange::CookModel(source, output, error))
        {
            m_notice = error;
            m_showNotice = true;
            return;
        }
        RequestOpen(output);
    }

    void AnimatorApp::RunClipAction(const char *action, const std::filesystem::path &path)
    {
        const nlohmann::json response =
            nlohmann::json::parse(HandleAction(action, nlohmann::json({{"path", path.generic_string()}}).dump()),
                                  nullptr,
                                  false);
        if (!response.is_object())
            m_notice = "Clip exchange returned an invalid response.";
        else if (response.value("ok", false))
            m_notice = response.value("summary", std::string("Done."));
        else
            m_notice = response.value("error", std::string("Clip exchange failed."));
        m_showNotice = true;
    }

    void AnimatorApp::ImportClipDialog()
    {
        std::string picked;
        if (PickFile("Import clip",
                     "Motion clips (*.bvh;*.gltf;*.glb;*.fbx;*.dae;*.pemesh)\0*.bvh;*.gltf;*.glb;*.fbx;*.dae;*.pemesh\0All files\0*.*\0",
                     picked))
        {
            RunClipAction("timeline.import", picked);
            return;
        }
#if !defined(PE_WIN32)
        m_clipPopupAction = "timeline.import";
        m_clipPopupPending = true;
#endif
    }

    void AnimatorApp::ExportClipDialog()
    {
        std::string picked;
        if (PickSaveFile("Export active clip", "glTF 2.0 (*.gltf)\0*.gltf\0", "gltf", picked))
        {
            RunClipAction("timeline.export", picked);
            return;
        }
#if !defined(PE_WIN32)
        m_clipPopupAction = "timeline.export";
        m_clipPopupPending = true;
#endif
    }

    void AnimatorApp::DrawShell()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open .pemesh...", "Ctrl+O"))
                    OpenDialog();
                ui::ItemTooltip("Open a cooked model. Dropping a .pemesh on the window opens it too.");
                if (ImGui::MenuItem("Import Model..."))
                    ImportModelDialog();
                ui::ItemTooltip("Cook a glTF, GLB, FBX, DAE or OBJ into a .pemesh and open it. Flat meshes enter 2D Plane mode automatically.");
                if (ImGui::BeginMenu("Recent", !m_recent.empty()))
                {
                    std::filesystem::path pick;
                    for (const std::filesystem::path &path : m_recent)
                        if (ImGui::MenuItem(path.filename().string().c_str()))
                            pick = path;
                    ImGui::EndMenu();
                    if (!pick.empty())
                        RequestOpen(pick);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Clip...", nullptr, false, m_timeline->HasTarget()))
                    ImportClipDialog();
                ui::ItemTooltip("Retarget clips from BVH, glTF or another supported motion file onto this rig by bone name.");
                if (ImGui::MenuItem("Export Active Clip...", nullptr, false, m_timeline->HasTarget()))
                    ExportClipDialog();
                ui::ItemTooltip("Write the active action and skeleton as glTF 2.0. Extracted root travel is baked into the export.");
                ImGui::Separator();
                if (ImGui::MenuItem("Save", "Ctrl+S", false, m_timeline->HasTarget()))
                {
                    std::string error;
                    if (!SaveAll(&error))
                    {
                        m_notice = error;
                        m_showNotice = true;
                    }
                }
                ui::ItemTooltip("Write clips to the .pemesh and rig edits to its rig document.");
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                    RequestQuit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                const bool target = m_timeline->HasTarget();
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, target))
                    HandleAction("timeline.undo", "{}");
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, target))
                    HandleAction("timeline.undo", R"({"redo":true})");
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Pose", "Ctrl+C in viewport", false, target))
                    HandleAction("timeline.pose_copy", "{}");
                ui::ItemTooltip("Copy the selected bones' pose at the current frame (every bone when none is selected).");
                if (ImGui::MenuItem("Paste Pose", "Ctrl+V in viewport", false, target))
                    HandleAction("timeline.pose_paste", "{}");
                ui::ItemTooltip("Key the copied pose at the current frame, in this or any other action of the rig.");
                if (ImGui::MenuItem("Paste Pose Flipped", "Ctrl+Shift+V in viewport", false, target))
                    HandleAction("timeline.pose_paste", R"({"flipped":true})");
                ui::ItemTooltip("Key the copied pose mirrored across X: each bone lands on its .L / .R twin.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::BeginMenu("Style"))
                {
                    for (const auto &entry : kStyles)
                        if (ImGui::MenuItem(entry.name, nullptr, GUIState::s_guiStyle == entry.style))
                            ApplyStyle(entry.style);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Font Size"))
                {
                    const float scale = io.FontGlobalScale;
                    if (ImGui::MenuItem("Small", nullptr, scale < 0.95f))
                        SetFontScale(0.85f);
                    if (ImGui::MenuItem("Medium", nullptr, scale >= 0.95f && scale < 1.15f))
                        SetFontScale(1.f);
                    if (ImGui::MenuItem("Large", nullptr, scale >= 1.15f && scale < 1.35f))
                        SetFontScale(1.25f);
                    if (ImGui::MenuItem("Extra Large", nullptr, scale >= 1.35f))
                        SetFontScale(1.5f);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                    m_timeline->SetViewportShare(0.6f);
                ui::ItemTooltip("The viewport back to 60% of the window.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                Camera *camera = m_scene.GetCameras().empty() ? nullptr : m_scene.GetActiveCamera();
                const bool target = m_timeline->HasTarget();
                if (ImGui::MenuItem("Frame Character", "F", false, target))
                    m_timeline->RequestFrameView();
                ui::ItemTooltip("Fit the character in the viewport.");
                if (ImGui::MenuItem("Reset Camera", nullptr, false, target))
                    m_timeline->ResetView();
                ui::ItemTooltip("The front view from slightly above, framed.");
                ImGui::Separator();
                static const struct
                {
                    const char *name;
                    vec3 look; // the direction the camera looks along (Blender's views: Right = camera at +X)
                } kViews[] = {{"Front", vec3(0.f, 0.f, -1.f)}, {"Back", vec3(0.f, 0.f, 1.f)}, {"Right", vec3(-1.f, 0.f, 0.f)}, {"Left", vec3(1.f, 0.f, 0.f)}, {"Top", vec3(0.f, -1.f, 0.f)}, {"Bottom", vec3(0.f, 1.f, 0.f)}};
                for (const auto &view : kViews)
                    if (ImGui::MenuItem(view.name, nullptr, false, target))
                        m_timeline->RequestView(view.look, false);
                ImGui::Separator();
                const bool ortho = camera && camera->IsOrthographic();
                if (ImGui::MenuItem("Orthographic", nullptr, ortho, camera != nullptr))
                    m_timeline->SetOrthographic(!ortho);
                m_grid = Settings::Get<SceneSettings>().draw_grid;
                if (ImGui::MenuItem("Grid", nullptr, &m_grid))
                    Settings::Get<SceneSettings>().draw_grid = m_grid;
                if (ImGui::MenuItem("Ground", nullptr, m_ground))
                    SetGroundVisible(!m_ground);
                ui::ItemTooltip("A flat plane under the origin: the character's shadow lands on it.");
                ImGui::MenuItem("Axis Gizmo", nullptr, &GUIState::s_useOrientationGizmo);
                ui::ItemTooltip("The axis arrows in the viewport corner; click one to look along it.");
                if (ImGui::MenuItem("Bone Names", nullptr, m_timeline->ShowBoneNames()))
                    m_timeline->SetShowBoneNames(!m_timeline->ShowBoneNames());
                if (ImGui::MenuItem("Turntable", nullptr, m_timeline->Turntable()))
                    m_timeline->SetTurntable(!m_timeline->Turntable());
                ui::ItemTooltip("Slowly orbit the character; a right or middle drag pauses it.");
                if (ImGui::MenuItem("Maximize Viewport", "Ctrl+Space"))
                    m_timeline->ToggleMaximize();
                ImGui::Separator();
                if (ImGui::BeginMenu("Bookmarks"))
                {
                    OrbitView current;
                    const bool haveView = m_timeline->GetOrbitView(current);
                    if (ImGui::MenuItem("Add Bookmark...", nullptr, false, haveView))
                        m_bookmarkPromptPending = true;
                    if (!m_bookmarks.empty())
                        ImGui::Separator();
                    int remove = -1;
                    for (int i = 0; i < static_cast<int>(m_bookmarks.size()); i++)
                    {
                        ImGui::PushID(i);
                        if (ImGui::MenuItem(m_bookmarks[i].name.c_str()))
                            m_timeline->SetOrbitView(m_bookmarks[i].view);
                        ui::ItemTooltip("Right-click removes it.");
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            remove = i;
                        ImGui::PopID();
                    }
                    if (remove >= 0)
                        m_bookmarks.erase(m_bookmarks.begin() + remove);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("Hotkeys", nullptr, &m_showHotkeys);
                ImGui::EndMenu();
            }
            const std::string label = m_modelPath.empty()
                                          ? std::string("No model: File > Open .pemesh, or drop one on the window")
                                          : m_modelPath.filename().string();
            const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            // SetCursorPosX, not SameLine(x): SameLine adds the menu bar's group offset and pushed the label off the edge
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - labelWidth - 16.f));
            ImGui::TextDisabled("%s", label.c_str());
            ImGui::EndMainMenuBar();
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false) && !ImGui::IsAnyItemActive())
            OpenDialog();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsAnyItemActive())
            m_timeline->ToggleMaximize();
        DrawPrompts();

        if (m_openPopupPending)
        {
            ImGui::OpenPopup("Open .pemesh");
            m_openPopupPending = false;
        }
        if (ImGui::BeginPopupModal("Open .pemesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.f);
            ImGui::InputText("##animator_open_path", m_openBuffer, sizeof(m_openBuffer));
            ImGui::Dummy({0.f, ImGui::GetFontSize() * 0.5f});
            ui::DialogButtonRow({"Open", "Cancel"});
            if (ImGui::Button("Open", ui::DialogButtonSize("Open")))
            {
                const std::string path = m_openBuffer;
                ImGui::CloseCurrentPopup();
                RequestOpen(path);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ui::DialogButtonSize("Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_clipPopupPending)
        {
            ImGui::OpenPopup("Clip path");
            m_clipPopupPending = false;
            m_clipPathBuffer[0] = '\0';
        }
        if (ImGui::BeginPopupModal("Clip path", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(m_clipPopupAction == "timeline.import" ? "Source motion file" : "Output .gltf");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.f);
            const bool entered = ImGui::InputText("##animator_clip_path",
                                                  m_clipPathBuffer,
                                                  sizeof(m_clipPathBuffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            const char *confirm = m_clipPopupAction == "timeline.import" ? "Import" : "Export";
            ImGui::Dummy({0.f, ImGui::GetFontSize() * 0.5f});
            ui::DialogButtonRow({confirm, "Cancel"});
            if ((ImGui::Button(confirm, ui::DialogButtonSize(confirm)) || entered) && m_clipPathBuffer[0])
            {
                const std::string action = m_clipPopupAction;
                const std::string path = m_clipPathBuffer;
                ImGui::CloseCurrentPopup();
                RunClipAction(action.c_str(), path);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ui::DialogButtonSize("Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        m_timeline->Update(viewport->WorkPos, viewport->WorkSize);
    }

    void AnimatorApp::DrawPrompts()
    {
        if (m_showNotice)
        {
            ImGui::SetNextWindowSize({ImGui::GetFontSize() * 32.f, 0.f}, ImGuiCond_Appearing);
            if (ImGui::Begin("PhasmaAnimator message", &m_showNotice, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("%s", m_notice.c_str());
                ImGui::Dummy({0.f, ImGui::GetFontSize() * 0.5f});
                ui::DialogButtonRow({"OK"});
                if (ImGui::Button("OK", ui::DialogButtonSize("OK")))
                    m_showNotice = false;
            }
            ImGui::End();
        }

        if (m_promptPending)
        {
            ImGui::OpenPopup("Unsaved changes");
            m_promptPending = false;
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static std::string s_error;
            ImGui::Text("%s has unsaved clip or rig changes.", m_modelPath.filename().string().c_str());
            if (!s_error.empty())
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "%s", s_error.c_str());
            bool proceed = false;
            ImGui::Dummy({0.f, ImGui::GetFontSize() * 0.5f});
            ui::DialogButtonRow({"Save", "Discard", "Cancel"});
            if (ImGui::Button("Save", ui::DialogButtonSize("Save")))
            {
                s_error.clear();
                proceed = SaveAll(&s_error);
            }
            ui::ItemTooltip("Write the clips and the rig document, then continue.");
            ImGui::SameLine();
            if (ImGui::Button("Discard", ui::DialogButtonSize("Discard")))
                proceed = true;
            ui::ItemTooltip("Continue and lose every unsaved clip and rig change.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ui::DialogButtonSize("Cancel")))
            {
                s_error.clear();
                m_pendingOpen.clear();
                m_pendingQuit = false;
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Stay on this model and keep the changes.");
            if (proceed)
            {
                s_error.clear();
                ImGui::CloseCurrentPopup();
                if (m_pendingQuit)
                    m_quit = true;
                else if (!m_pendingOpen.empty())
                {
                    std::string error;
                    if (!OpenModel(m_pendingOpen, &error))
                        PE_WARN("[Animator] %s", error.c_str());
                }
                m_pendingOpen.clear();
                m_pendingQuit = false;
            }
            ImGui::EndPopup();
        }

        if (m_bookmarkPromptPending)
        {
            ImGui::OpenPopup("Add Bookmark");
            m_bookmarkPromptPending = false;
            snprintf(m_bookmarkName, sizeof(m_bookmarkName), "View %d", static_cast<int>(m_bookmarks.size()) + 1);
        }
        if (ImGui::BeginPopupModal("Add Bookmark", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
            const bool entered = ImGui::InputText("Name", m_bookmarkName, sizeof(m_bookmarkName),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            OrbitView view;
            ImGui::Dummy({0.f, ImGui::GetFontSize() * 0.5f});
            ui::DialogButtonRow({"Add", "Cancel"});
            if ((ImGui::Button("Add", ui::DialogButtonSize("Add")) || entered) && m_bookmarkName[0] &&
                m_timeline->GetOrbitView(view))
            {
                m_bookmarks.push_back({m_bookmarkName, view});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ui::DialogButtonSize("Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_showHotkeys)
        {
            // One row per shortcut, grouped by where you are when you press it. The chord column is a fixed
            // width measured over the whole sheet, so the sections stay aligned with one table each.
            static const struct
            {
                const char *section; // nullptr continues the section above
                const char *chord;
                const char *action;
            } kSheet[] = {
                {"Files", "Ctrl+O", "Open a cooked .pemesh model"},
                {nullptr, "Ctrl+S", "Save the clips and the rig document"},
                {nullptr, "Drop a file", "Dropping a .pemesh on the window opens it"},

                {"Viewport", "RMB drag", "Orbit the camera"},
                {nullptr, "MMB drag", "Pan the camera"},
                {nullptr, "Wheel", "Zoom in and out"},
                {nullptr, "F", "Frame the character"},
                {nullptr, "Ctrl+Space", "Maximize the viewport"},
                {nullptr, "Click an axis", "Look along that axis of the corner gizmo"},

                {"Posing", "Drag a bone tail", "Pose the chain: Rotate, Move or Both"},
                {nullptr, "Ctrl+C", "Copy the pose (with the viewport hovered)"},
                {nullptr, "Ctrl+V", "Paste the pose"},
                {nullptr, "Ctrl+Shift+V", "Paste the pose flipped onto the .L / .R twin"},
                {nullptr, "Mirror X", "Solve the counterpart chain in the same undo step"},
                {nullptr, "Auto Key", "Key every viewport and pose-bar edit"},

                {"Playback", "Space", "Play or pause"},
                {nullptr, "Shift+Ctrl+Space", "Play in reverse"},
                {nullptr, "Left / Right", "Jump to the previous or next key"},

                {"Dope Sheet: keys", "G", "Move the selected keys"},
                {nullptr, "S", "Scale them around the playhead"},
                {nullptr, "X / Delete", "Delete them"},
                {nullptr, "Shift+D", "Duplicate them"},
                {nullptr, "I", "Insert a key at the playhead"},
                {nullptr, "A / Alt+A", "Select all or none"},
                {nullptr, "Ctrl+C / Ctrl+V", "Copy and paste keys, into any action of the rig"},
                {nullptr, "Enter / Esc", "Confirm or cancel the edit in flight"},

                {"Dope Sheet: view", "Home", "Frame every key"},
                {nullptr, "Numpad .", "Frame the selected keys"},
                {nullptr, "Numpad + / -", "Zoom in and out"},
                {nullptr, "Wheel", "Zoom"},
                {nullptr, "Ctrl / Shift + wheel", "Scroll horizontally or vertically"},
                {nullptr, "MMB drag", "Pan"},
                {nullptr, "Ctrl+MMB drag", "Zoom"},
                {nullptr, "Esc", "Clear the marked interval"},

                {"Markers", "M", "Mark the playhead"},
                {nullptr, "Right-click the ruler", "Add, rename, jump to or delete a marker"},

                {"Undo", "Ctrl+Z", "Undo: the keys in Animate, the rig document in Rig"},
                {nullptr, "Ctrl+Y / Ctrl+Shift+Z", "Redo"},
            };

            const float fontSize = ImGui::GetFontSize();
            const ImGuiStyle &style = ImGui::GetStyle();
            // Both columns are sized from the text, so nothing is silently clipped at the default size
            // and the sheet reads the same at every font scale and theme.
            float chordWidth = 0.f, actionWidth = 0.f;
            for (const auto &row : kSheet)
            {
                chordWidth = std::max(chordWidth, ImGui::CalcTextSize(row.chord).x);
                actionWidth = std::max(actionWidth, ImGui::CalcTextSize(row.action).x);
            }
            const float sheetWidth = chordWidth + actionWidth + style.ItemSpacing.x * 2.f +
                                     style.CellPadding.x * 4.f + style.WindowPadding.x * 2.f +
                                     style.ScrollbarSize;
            ImGui::SetNextWindowSize({sheetWidth, fontSize * 40.f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints({fontSize * 20.f, fontSize * 8.f}, {FLT_MAX, FLT_MAX});
            if (ImGui::Begin("Hotkeys", &m_showHotkeys))
            {
                const ImVec4 chordColour = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark); // the theme's accent

                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {style.CellPadding.x, style.ItemSpacing.y * 0.75f});
                for (size_t i = 0; i < IM_ARRAYSIZE(kSheet);)
                {
                    const char *section = kSheet[i].section;
                    ImGui::SeparatorText(section);
                    if (ImGui::BeginTable(section, 2,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings |
                                              ImGuiTableFlags_PadOuterX))
                    {
                        ImGui::TableSetupColumn("chord", ImGuiTableColumnFlags_WidthFixed, chordWidth);
                        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
                        do
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            // right-aligned, so the chords and the actions read as two clean columns
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + chordWidth -
                                                 ImGui::CalcTextSize(kSheet[i].chord).x);
                            ImGui::TextColored(chordColour, "%s", kSheet[i].chord);
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(kSheet[i].action);
                            ++i;
                        }
                        while (i < IM_ARRAYSIZE(kSheet) && !kSheet[i].section);
                        ImGui::EndTable();
                    }
                    else
                        for (++i; i < IM_ARRAYSIZE(kSheet) && !kSheet[i].section; ++i)
                            ; // the table is clipped away: step over its rows or the outer loop never moves
                }
                ImGui::PopStyleVar();
            }
            ImGui::End();
        }
    }

    // -------------------------------------------------------------------------
    // the scene image behind the Timeline's viewport, and the ImGui pass over the scene
    // -------------------------------------------------------------------------
    void *AnimatorApp::RegisterImageTexture(Image *image)
    {
        return GUIBackend::RegisterImageTexture(image);
    }

    void AnimatorApp::ReleaseImageTexture(void *&texture)
    {
        GUIBackend::ReleaseImageTexture(texture);
    }

    bool AnimatorApp::EnsureSceneTexture()
    {
        Image *displayRT = m_renderer.GetDisplayRT();
        if (!displayRT)
            return false;
        Image *&image = GUIState::s_sceneViewImage;
        if (!image || image->GetWidth() != displayRT->GetWidth() || image->GetHeight() != displayRT->GetHeight())
        {
            m_renderer.WaitAllFramesCommands();
            if (GUIState::s_viewportTextureId)
                GUIBackend::ReleaseImageTexture(GUIState::s_viewportTextureId);
            Image::Destroy(image);
            image = m_renderer.CreateFSSampledImage(false);
        }
        if (!image || !image->HasSRV())
            return false;
        if (!GUIState::s_viewportTextureId)
            GUIState::s_viewportTextureId = GUIBackend::RegisterImageTexture(image);
        return GUIState::s_viewportTextureId != nullptr;
    }

    void AnimatorApp::DrawOverlay(CommandBuffer *cmd, Image *displayRT)
    {
        m_attachment.image = displayRT;
        const ImDrawData *drawData = ImGui::GetDrawData();
        if (!drawData || drawData->TotalVtxCount <= 0)
            return;
        Image *sceneImage = GUIState::s_sceneViewImage;
        if (sceneImage && sceneImage->GetWidth() == displayRT->GetWidth() &&
            sceneImage->GetHeight() == displayRT->GetHeight())
        {
            // the scene as rendered this frame, sampled by the viewport image drawn in the pass below
            cmd->CopyImage(displayRT, sceneImage);
            ImageBarrierInfo barrier{};
            barrier.image = sceneImage;
            barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(barrier);
        }
        cmd->BeginPass(1, &m_attachment, "Animator GUI", true);
        GUIBackend::RenderDrawData(cmd);
        cmd->EndPass();
    }

    // -------------------------------------------------------------------------
    // file picker
    // -------------------------------------------------------------------------
    bool AnimatorApp::PickFile(const char *title, const char *filter, std::string &outPath)
    {
#if defined(PE_WIN32)
        char fileName[MAX_PATH] = {};
        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(RHII.GetWindow(), &info))
            dialog.hwndOwner = info.info.win.window;
        dialog.lpstrTitle = title;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(sizeof(fileName));
        const std::string initialDir = m_modelPath.empty() ? Path::Assets : m_modelPath.parent_path().string();
        dialog.lpstrInitialDir = initialDir.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&dialog))
            return false;
        outPath = fileName;
        return true;
#else
        (void)title;
        (void)filter;
        (void)outPath;
        return false;
#endif
    }

    bool AnimatorApp::PickSaveFile(const char *title, const char *filter, const char *extension, std::string &outPath)
    {
#if defined(PE_WIN32)
        char fileName[MAX_PATH] = {};
        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(RHII.GetWindow(), &info))
            dialog.hwndOwner = info.info.win.window;
        dialog.lpstrTitle = title;
        dialog.lpstrFilter = filter;
        dialog.lpstrDefExt = extension;
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(sizeof(fileName));
        const std::string initialDir = m_modelPath.empty() ? Path::Assets : m_modelPath.parent_path().string();
        dialog.lpstrInitialDir = initialDir.c_str();
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameA(&dialog))
            return false;
        outPath = fileName;
        return true;
#else
        (void)title;
        (void)filter;
        (void)extension;
        (void)outPath;
        return false;
#endif
    }

    // -------------------------------------------------------------------------
    // actions: the command file (tests) and the menu share them
    // -------------------------------------------------------------------------
    std::string AnimatorApp::HandleAction(const std::string &action, const std::string &argsJson)
    {
        nlohmann::json args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (!args.is_object())
            args = nlohmann::json::object();
        auto ok = [&](nlohmann::json extra = nlohmann::json::object())
        {
            extra["ok"] = true;
            extra["action"] = action;
            return extra.dump();
        };
        auto fail = [&](const std::string &message)
        {
            return ActionResult({{"error", message}, {"action", action}});
        };
        try
        {
            if (action == "animator.open")
            {
                std::string error;
                if (args.value("save", false) && !SaveAll(&error))
                    return fail(error);
                if (!OpenModel(args.value("path", ""), &error))
                    return fail(error);
                nlohmann::json roots = nlohmann::json::array();
                for (NodeId *root : m_modelRoots)
                    if (root && m_scene.IsNodeAlive(root))
                        roots.push_back({{"id", "node:0:" + std::to_string(root->index)},
                                         {"name", m_scene.GetNodeName(root)}});
                return ok({{"path", m_modelPath.generic_string()}, {"model_roots", roots}});
            }
            if (action == "animator.import_model")
            {
                const std::filesystem::path source = args.value("path", args.value("source", std::string()));
                if (source.empty())
                    return fail("path is required");
                std::filesystem::path output = args.value("output", std::string());
                if (output.empty())
                {
                    output = source;
                    output.replace_extension(".pemesh");
                }
                std::string error;
                if (!ClipExchange::CookModel(source, output, error) || !OpenModel(output, &error))
                    return fail(error);
                return ok({{"source", source.generic_string()}, {"path", output.generic_string()}});
            }
            if (action == "animator.new" || action == "file.new_scene")
            {
                m_timeline->DropTarget();
                ResetScene();
                return ok();
            }
            if (action == "animator.screenshot")
            {
                const std::string path = args.value("path", (std::filesystem::path(Path::Executable) / "animator.png").generic_string());
                m_renderer.RequestScreenshot(path);
                return ok({{"path", path}});
            }
            if (action == "animator.log")
            {
                const int count = std::max(1, args.value("count", 20));
                const std::string level = args.value("level", "");
                nlohmann::json entries = nlohmann::json::array();
                std::lock_guard lock(m_logMutex);
                for (auto it = m_log.rbegin(); it != m_log.rend() && static_cast<int>(entries.size()) < count; ++it)
                    if (level.empty() || level == LogLevelName(it->type))
                        entries.push_back({{"level", LogLevelName(it->type)}, {"message", it->message}});
                return ok({{"entries", entries}, {"total", m_log.size()}});
            }
            if (action == "animator.state")
            {
                const Camera *camera = m_scene.GetCameras().empty() ? nullptr : m_scene.GetActiveCamera();
                nlohmann::json state = {{"model", m_modelPath.generic_string()},
                                        {"has_target", m_timeline->HasTarget()},
                                        {"viewport", m_timeline->ViewportShare()},
                                        {"style", StyleName(GUIState::s_guiStyle)},
                                        {"font_scale", ImGui::GetIO().FontGlobalScale},
                                        {"dirty", m_timeline->IsDirty()},
                                        {"ground", m_ground},
                                        {"turntable", m_timeline->Turntable()},
                                        {"bone_names", m_timeline->ShowBoneNames()},
                                        {"recent", m_recent.size()},
                                        {"bookmarks", m_bookmarks.size()}};
                OrbitView view;
                if (m_timeline->GetOrbitView(view))
                    state["view"] = ViewJson(view);
                if (camera)
                {
                    const vec3 p = camera->GetPosition();
                    state["camera"] = {p.x, p.y, p.z};
                }
                return ok(state);
            }
            if (action == "layout.style")
            {
                GUIStyle style;
                if (!StyleByName(args.value("name", ""), style))
                    return fail("unknown style: " + args.value("name", ""));
                ApplyStyle(style);
                return ok({{"style", StyleName(style)}});
            }
            if (action == "layout.font_scale")
            {
                SetFontScale(args.value("scale", 1.f));
                return ok({{"font_scale", ImGui::GetIO().FontGlobalScale}});
            }
            if (action == "animator.exit")
            {
                std::string error;
                if (args.value("save", false) && !SaveAll(&error))
                    return fail(error);
                m_quit = true; // the command file never prompts: probes end here right after posing
                return ok();
            }
            if (action == "animator.save")
            {
                std::string error;
                if (!SaveAll(&error))
                    return fail(error);
                return ok();
            }
            if (action == "animator.ground")
            {
                if (args.contains("visible"))
                    SetGroundVisible(args.value("visible", true));
                return ok({{"visible", m_ground}});
            }
            if (action == "animator.hotkeys")
            {
                m_showHotkeys = args.value("show", !m_showHotkeys);
                return ok({{"show", m_showHotkeys}});
            }
            if (action == "animator.bookmark")
            {
                const std::string name = args.value("name", "");
                if (name.empty())
                    return fail("bookmark needs a name");
                if (args.value("apply", false))
                {
                    for (const Bookmark &bookmark : m_bookmarks)
                        if (bookmark.name == name)
                        {
                            m_timeline->SetOrbitView(bookmark.view);
                            return ok({{"applied", name}});
                        }
                    return fail("unknown bookmark: " + name);
                }
                OrbitView view;
                if (!m_timeline->GetOrbitView(view))
                    return fail("the orbit does not own the camera yet");
                m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                                                 [&](const Bookmark &b)
                                                 { return b.name == name; }),
                                  m_bookmarks.end());
                m_bookmarks.push_back({name, view});
                return ok({{"added", name}, {"count", m_bookmarks.size()}});
            }
            // Pose aliases select the Animate panel.
            if (action == "rig.reference_load" || action == "rig.reference_clear" || action == "rig.pin" ||
                action == "rig.grab" || action == "rig.lock")
            {
                m_timeline->SetRigMode(false);
                return m_timeline->HandleAction("timeline." + action.substr(4), argsJson);
            }
            if (action.rfind("rig.", 0) == 0)
            {
                m_timeline->SetRigMode(true);
                return m_timeline->Rig().HandleAction(action, argsJson);
            }
            if (action.rfind("timeline.", 0) == 0)
            {
                AnimationTimeline *timeline = m_timeline.get();
                timeline->SetRigMode(false);
                if (action == "timeline.mode")
                {
                    const std::string mode = args.value("mode", "dope");
                    timeline->SetGraphMode(mode == "graph" || mode == "graph_editor" || mode == "curves");
                    return ok();
                }
                if (action == "timeline.frame")
                {
                    timeline->RequestFrame(args.value("frame", 0.0f));
                    return ok();
                }
                if (action == "timeline.bone")
                {
                    timeline->RequestBone(args.value("bone", ""));
                    return ok();
                }
                if (action == "timeline.save")
                {
                    timeline->RequestSave();
                    return ok();
                }
                if (action == "timeline.play")
                {
                    timeline->RequestPlay(args.value("play", true), args.value("reverse", false));
                    return ok();
                }
                if (action == "timeline.undo")
                    return timeline->StepViewportUndo(m_scene, args.value("redo", false)) ? ok()
                                                                                          : fail("nothing to undo");
                if (action == "timeline.clip")
                {
                    AnimationTimeline::PendingClip clip;
                    clip.name = args.value("name", "");
                    clip.end = args.value("end", -1.f);
                    clip.fps = args.value("fps", -1.f);
                    timeline->RequestClip(clip);
                    return ok();
                }
                if (action == "timeline.pose")
                {
                    AnimationTimeline::PendingPose pose;
                    pose.bone = args.value("bone", "");
                    if (pose.bone.empty())
                        return fail("bone is required");
                    pose.frame = args.value("frame", -1.f);
                    auto readVec = [&](const char *key, vec3 &out, int bit)
                    {
                        if (!args.contains(key) || !args[key].is_array() || args[key].size() != 3 ||
                            !std::all_of(args[key].begin(), args[key].end(),
                                         [](const nlohmann::json &c)
                                         { return c.is_number(); }))
                            return;
                        const vec3 v(args[key][0].get<float>(), args[key][1].get<float>(), args[key][2].get<float>());
                        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                            return;
                        out = v;
                        pose.mask |= bit;
                    };
                    readVec("loc", pose.loc, 1);
                    readVec("rot", pose.rot, 2);
                    readVec("scale", pose.scl, 4);
                    if (pose.mask == 0)
                        return fail("give loc[3], rot[3] or scale[3] (three finite numbers each)");
                    timeline->RequestPose(pose);
                    return ok();
                }
                if (action == "timeline.rest")
                {
                    timeline->RequestRestPose();
                    return ok();
                }
                return timeline->HandleAction(action, argsJson);
            }
            return fail("unknown action: " + action);
        }
        catch (const std::exception &e)
        {
            return fail(std::string("invalid action arguments: ") + e.what());
        }
    }

    void AnimatorApp::PollCommandFile()
    {
        // ponytail: a file beside the executable instead of a server; tests write animator_command.json (one
        // {action,args} or a list) and read animator_result.json back
        const std::filesystem::path command = std::filesystem::path(Path::Executable) / "animator_command.json";
        const std::filesystem::path result = std::filesystem::path(Path::Executable) / "animator_result.json";
        std::error_code ec;
        if (!std::filesystem::exists(command, ec))
            return;
        std::string text;
        {
            std::ifstream in(command, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        nlohmann::json requests = nlohmann::json::parse(text, nullptr, false);
        // a file still being written (or held by a scanner) reads empty or short: leave it for the next frame
        static int s_unreadableFrames = 0;
        if (requests.is_discarded() && ++s_unreadableFrames < 120)
            return;
        s_unreadableFrames = 0;
        std::filesystem::remove(command, ec);
        if (requests.is_object())
            requests = nlohmann::json::array({requests});
        nlohmann::json results = nlohmann::json::array();
        if (requests.is_array())
        {
            for (const nlohmann::json &request : requests)
            {
                const std::string action = request.is_object() ? request.value("action", "") : "";
                const std::string args = request.is_object() && request.contains("args") ? request["args"].dump() : "{}";
                nlohmann::json parsed = nlohmann::json::parse(HandleAction(action, args), nullptr, false);
                results.push_back(parsed.is_discarded() ? nlohmann::json{{"error", "unparseable result"}} : parsed);
            }
        }
        else
            results.push_back({{"error", "animator_command.json is not JSON"}});
        const std::filesystem::path temp = result.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary);
            out << results.dump();
        }
        std::filesystem::rename(temp, result, ec);
    }
} // namespace pe
