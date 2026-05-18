#include "GUI.h"
#include "Agent/EditorMcp.h"
#include "Agent/EditorToolCatalog.h"
#include "Agent/EditorToolRuntime.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "GUI/Backends/GUIBackend.h"
#include "GUIState.h"
#include "Helpers.h"
#include "Particles/ParticleManager.h"
#include "Runtime/RuntimePlaySession.h"
#include "Runtime/RuntimeStartup.h"
#include "Script/ScriptSystem.h"
#include "Scene/SelectionManager.h"
#include "Scene/SceneHost.h"
#include "IconsFontAwesome.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "Widgets/AssetInfo.h"
#include "Widgets/CameraWidget.h"
#include "Widgets/Console.h"
#include "Widgets/FileBrowser.h"
#include "Widgets/FileSelector.h"
#include "Widgets/GlobalWidget.h"
#include "Widgets/Hierarchy.h"
#include "Widgets/LightWidget.h"
#include "Widgets/Loading.h"
#include "Widgets/MeshWidget.h"
#include "Widgets/ProfilerWidget.h"
#include "Widgets/Models.h"
#include "Widgets/Particles.h"
#include "Widgets/Properties.h"
#include "Widgets/SceneView.h"
#include "Widgets/ScriptEditor.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/AnimationTimeline.h"
#include "PhasmaMCP/Codebase/CodebaseIndexer.h"
#include "Widgets/TransformWidget.h"
#ifdef PE_PHYSICS
#include "Widgets/PhysicsWidget.h"
#endif
#ifdef PE_AUDIO
#include "Widgets/AudioWidget.h"
#endif
#include "UndoRedo.h"
#include <nlohmann/json.hpp>
#include "imgui/imgui_internal.h"

namespace pe
{
    namespace
    {
        ImGuiContext *s_hotReloadCtx = nullptr;

        bool IsScriptTestFailureLine(const std::string &line)
        {
            return line.find("[FAIL]") != std::string::npos ||
                   line.find("FAIL:") != std::string::npos ||
                   line.find("[ERROR]") != std::string::npos ||
                   line.rfind("error:", 0) == 0;
        }

        bool IsScriptTestWarningLine(const std::string &line)
        {
            return line.find("[WARN]") != std::string::npos;
        }

        std::string StripScriptTestSeverityPrefix(const std::string &line)
        {
            if (line.rfind("[ERROR] ", 0) == 0)
                return line.substr(8);
            if (line.rfind("[WARN] ", 0) == 0)
                return line.substr(7);
            return line;
        }

        std::string SlugifyEditorToken(const std::string &value)
        {
            std::string out;
            out.reserve(value.size());
            bool separator = false;
            for (unsigned char c : value)
            {
                if (std::isalnum(c) || c == '_')
                {
                    out.push_back(static_cast<char>(std::tolower(c)));
                    separator = false;
                }
                else if (!out.empty() && !separator)
                {
                    out.push_back('_');
                    separator = true;
                }
            }
            while (!out.empty() && out.back() == '_')
                out.pop_back();
            return out;
        }

        std::string NormalizeEditorActionId(const std::string &value)
        {
            std::string out;
            out.reserve(value.size());
            bool separator = false;
            for (unsigned char c : value)
            {
                if (std::isalnum(c) || c == '_')
                {
                    out.push_back(static_cast<char>(std::tolower(c)));
                    separator = false;
                }
                else if (!out.empty() && !separator)
                {
                    out.push_back('.');
                    separator = true;
                }
            }
            while (!out.empty() && out.back() == '.')
                out.pop_back();
            return out;
        }

        std::string NormalizeWindowSelector(const std::string &value)
        {
            std::string id = NormalizeEditorActionId(value);
            if (id.rfind("window.", 0) == 0)
                id = id.substr(7);
            std::replace(id.begin(), id.end(), '.', '_');
            return id;
        }

        nlohmann::json ParseEditorActionArgs(const std::string &argsJson)
        {
            if (argsJson.empty())
                return nlohmann::json::object();

            nlohmann::json args = nlohmann::json::parse(argsJson, nullptr, false);
            return args.is_object() ? args : nlohmann::json::object();
        }

        bool ResolveRequestedBool(bool current, const nlohmann::json &args, bool &value, std::string &error)
        {
            if (args.contains("open"))
            {
                if (!args["open"].is_boolean())
                {
                    error = "open must be a boolean";
                    return false;
                }
                value = args["open"].get<bool>();
                return true;
            }

            std::string state = args.value("state", "toggle");
            std::transform(state.begin(), state.end(), state.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (state == "toggle")
                value = !current;
            else if (state == "open" || state == "show" || state == "shown" || state == "on" || state == "true")
                value = true;
            else if (state == "closed" || state == "close" || state == "hide" || state == "hidden" || state == "off" || state == "false")
                value = false;
            else
            {
                error = "state must be toggle, open, or closed";
                return false;
            }
            return true;
        }

        const char *StyleId(GUIStyle style)
        {
            switch (style)
            {
            case GUIStyle::Classic:
                return "classic";
            case GUIStyle::Dark:
                return "dark";
            case GUIStyle::Light:
                return "light";
            case GUIStyle::Modern:
                return "modern";
            case GUIStyle::Unity:
                return "unity";
            case GUIStyle::Unreal:
                return "unreal";
            default:
                return "unknown";
            }
        }

        const char *StyleLabel(GUIStyle style)
        {
            switch (style)
            {
            case GUIStyle::Classic:
                return "Classic";
            case GUIStyle::Dark:
                return "Dark";
            case GUIStyle::Light:
                return "Light";
            case GUIStyle::Modern:
                return "Modern";
            case GUIStyle::Unity:
                return "Unity";
            case GUIStyle::Unreal:
                return "Unreal";
            default:
                return "Unknown";
            }
        }

        bool ApplyEditorStyle(const std::string &styleId, std::string &error)
        {
            std::string style = NormalizeEditorActionId(styleId);
            if (style.rfind("layout.style.", 0) == 0)
                style = style.substr(13);

            if (style == "classic")
            {
                GUIState::s_guiStyle = GUIStyle::Classic;
                ui::ApplyClassicTheme();
            }
            else if (style == "dark")
            {
                GUIState::s_guiStyle = GUIStyle::Dark;
                ui::ApplyDarkTheme();
            }
            else if (style == "light")
            {
                GUIState::s_guiStyle = GUIStyle::Light;
                ui::ApplyLightTheme();
            }
            else if (style == "modern")
            {
                GUIState::s_guiStyle = GUIStyle::Modern;
                ui::ApplyModernTheme();
            }
            else if (style == "unity")
            {
                GUIState::s_guiStyle = GUIStyle::Unity;
                ui::ApplyUnityTheme();
            }
            else if (style == "unreal")
            {
                GUIState::s_guiStyle = GUIStyle::Unreal;
                ui::ApplyUnrealTheme();
            }
            else
            {
                error = "unknown style: " + styleId;
                return false;
            }
            return true;
        }

        float FontScaleForPreset(const std::string &preset, bool &ok)
        {
            std::string value = NormalizeEditorActionId(preset);
            if (value.rfind("layout.font.", 0) == 0)
                value = value.substr(12);

            ok = true;
            if (value == "small")
                return 0.85f;
            if (value == "medium")
                return 1.0f;
            if (value == "large")
                return 1.25f;
            if (value == "extra.large" || value == "extra_large" || value == "extra")
                return 1.5f;

            ok = false;
            return 1.0f;
        }

        void WriteAgentConfigFile(const std::string &path, const nlohmann::json &j)
        {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());

            nlohmann::ordered_json ordered = nlohmann::ordered_json::object();
            if (j.is_object())
            {
                if (j.contains("mcp"))
                    ordered["mcp"] = j["mcp"];
                if (j.contains("indexing"))
                    ordered["indexing"] = j["indexing"];
                for (auto it = j.begin(); it != j.end(); ++it)
                {
                    if (it.key() == "mcp" || it.key() == "indexing")
                        continue;
                    ordered[it.key()] = it.value();
                }
            }
            else
            {
                ordered = j;
            }

            std::ofstream out(path);
            if (out)
                out << ordered.dump(2) << "\n";
        }

        void ApplyDefaultCodebaseIndexingConfig(const std::filesystem::path &repoRoot,
                                                pmcp::CodebaseIndexingConfig &config)
        {
            auto makePath = [&](const std::string &relative)
            { return (repoRoot / relative).string(); };

            auto addIfExists = [&](std::vector<std::string> &values, const std::string &path)
            {
                if (std::filesystem::exists(path))
                    values.push_back(path);
            };

            addIfExists(config.directories, makePath("PhasmaMCP"));
            addIfExists(config.directories, makePath("PhasmaCore"));
            addIfExists(config.directories, makePath("PhasmaEditor"));
            addIfExists(config.directories, makePath("PhasmaRuntime"));
            addIfExists(config.directories, makePath("PhasmaWebGPU"));

            addIfExists(config.skip_directories, makePath("PhasmaMCP/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaCore/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaEditor/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaRuntime/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaWebGPU/WgslBridge/target"));
            addIfExists(config.skip_directories, makePath("PhasmaEditor/Assets/Agent"));
            config.skip_files.clear();
            config.skip_regex.clear();
            config.skip_extensions = {
                ".png",
                ".jpg",
                ".jpeg",
                ".bmp",
                ".tga",
                ".hdr",
                ".exr",
                ".ktx",
                ".ktx2",
                ".dds",
                ".obj",
                ".fbx",
                ".gltf",
                ".glb",
                ".stl",
                ".ply",
                ".dae",
                ".ttf",
                ".otf",
                ".woff",
                ".woff2",
                ".wav",
                ".mp3",
                ".ogg",
                ".flac",
                ".zip",
                ".tar",
                ".gz",
                ".7z",
                ".rar",
                ".exe",
                ".dll",
                ".so",
                ".dylib",
                ".lib",
                ".a",
                ".o",
                ".pdb",
                ".spv",
                ".dxo",
                ".cso",
            };
        }

        nlohmann::json BuildDefaultAgentConfig(const std::filesystem::path &repoRoot)
        {
            pmcp::CodebaseIndexingConfig config;
            ApplyDefaultCodebaseIndexingConfig(repoRoot, config);

            return nlohmann::json{
                {"mcp", false},
                {"indexing",
                 {
                     {"directories", config.directories},
                     {"include_files", config.include_files},
                     {"skip_directories", config.skip_directories},
                     {"skip_files", config.skip_files},
                     {"skip_extensions", config.skip_extensions},
                     {"skip_regex", config.skip_regex},
                 }},
            };
        }

        bool MergeJsonDefaults(nlohmann::json &target, const nlohmann::json &defaults)
        {
            bool changed = false;

            if (!defaults.is_object())
                return changed;

            if (!target.is_object())
            {
                target = defaults;
                return true;
            }

            for (auto it = defaults.begin(); it != defaults.end(); ++it)
            {
                const std::string key = it.key();
                const auto &defaultValue = it.value();

                if (!target.contains(key))
                {
                    target[key] = defaultValue;
                    changed = true;
                    continue;
                }

                if (defaultValue.is_object() && target[key].is_object())
                {
                    if (MergeJsonDefaults(target[key], defaultValue))
                        changed = true;
                }
            }

            return changed;
        }

        bool AddJsonStringArrayItem(nlohmann::json &object, const char *key, const std::string &value)
        {
            if (value.empty())
                return false;

            if (!object.is_object())
                object = nlohmann::json::object();

            auto &array = object[key];
            if (!array.is_array())
                array = nlohmann::json::array();

            for (const auto &item : array)
                if (item.is_string() && item.get<std::string>() == value)
                    return false;

            array.push_back(value);
            return true;
        }

        bool AddDefaultJsonStringArrayItems(nlohmann::json &object, const nlohmann::json &defaults, const char *key)
        {
            if (!defaults.is_object() || !defaults.contains(key) || !defaults[key].is_array())
                return false;

            bool changed = false;
            for (const auto &item : defaults[key])
                if (item.is_string())
                    changed |= AddJsonStringArrayItem(object, key, item.get<std::string>());
            return changed;
        }
    } // namespace

    GUI::GUI()
        : m_render(true),
          m_attachment(std::make_unique<Attachment>()),
          m_show_demo_window(false),
          m_dockspaceId(0),
          m_dockspaceInitialized(false),
          m_requestDockReset(false)
    {
    }

    GUI::~GUI()
    {
        EventSystem::UnregisterCallback(EventType::AfterCommandWait, m_afterCommandWaitToken);
        *m_codebaseAlive = false;
        CancelCodebaseIndexing();
        if (m_indexThread.joinable())
            m_indexThread.join();

        if (GUIState::s_viewportTextureId)
            ReleaseImageTexture(GUIState::s_viewportTextureId);

        m_menuWindowWidgets.clear();
        m_widgets.clear();
        m_editorMcp.reset();
        m_editorToolRuntime.reset();

        Image::Destroy(GUIState::s_sceneViewImage);
        if (m_initialized)
        {
            GUIBackend::Shutdown();
            if (m_ownsImGuiContext)
                ImGui::DestroyContext();
        }
    }

    void *GUI::RegisterImageTexture(Image *image)
    {
        return GUIBackend::RegisterImageTexture(image);
    }

    void GUI::ReleaseImageTexture(void *&textureID)
    {
        GUIBackend::ReleaseImageTexture(textureID);
    }

    std::string GUI::TakeUISnapshot() const
    {
        nlohmann::json ui;
        for (const auto &w : m_menuWindowWidgets)
            ui[w->GetName()] = *w->GetOpen();
        return ui.dump();
    }

    void GUI::RestoreUISnapshot(const std::string &jsonStr)
    {
        nlohmann::json ui = nlohmann::json::parse(jsonStr, nullptr, false);
        if (ui.is_discarded())
        {
            PE_WARN("[HotReload] Failed to parse UI snapshot — skipping widget restore");
            return;
        }
        for (auto &w : m_widgets)
        {
            auto it = ui.find(w->GetName());
            if (it != ui.end() && it->is_boolean())
                *w->GetOpen() = it->get<bool>();
        }
    }

    void GUI::SetHotReloadContext(ImGuiContext *ctx)
    {
        s_hotReloadCtx = ctx;
    }

    bool GUI::IsMcpServerRunning() const
    {
        return m_editorMcp && m_editorMcp->IsRunning();
    }

    void GUI::SetMcpServerEnabled(bool enabled)
    {
        const bool running = IsMcpServerRunning();
        if (enabled == running || !m_editorMcp)
            return;

        if (enabled)
        {
            m_editorMcp->Start();
            return;
        }

        CancelCodebaseIndexing();
        m_editorMcp->Stop();
    }

    std::string GUI::QueryEditorActions()
    {
        nlohmann::json result;
        result["windows"] = nlohmann::json::array();
        result["actions"] = nlohmann::json::array();
        result["state"] = {
            {"mcp_running", IsMcpServerRunning()},
            {"play_mode", GUIState::s_playMode},
            {"paused", GUIState::s_isPaused},
            {"viewport_floating", GUIState::s_sceneViewFloating},
            {"style", StyleId(GUIState::s_guiStyle)},
            {"font_scale", ImGui::GetIO().FontGlobalScale},
            {"render_enabled", m_render},
        };

        auto addAction = [&](const std::string &id, const std::string &label, const std::string &category,
                             const std::string &kind, bool enabled, bool checked, bool hasChecked)
        {
            nlohmann::json action = {
                {"id", id},
                {"label", label},
                {"category", category},
                {"kind", kind},
                {"enabled", enabled},
            };
            if (hasChecked)
                action["checked"] = checked;
            result["actions"].push_back(std::move(action));
        };

        auto addWindow = [&](const std::string &name, bool open, bool floating)
        {
            const std::string id = "window." + SlugifyEditorToken(name);
            nlohmann::json window = {
                {"id", id},
                {"name", name},
                {"open", open},
            };
            if (name == "Viewport")
                window["floating"] = floating;
            result["windows"].push_back(std::move(window));
            addAction(id, name, "Window", "toggle", true, open, true);
        };

        for (auto &widget : m_menuWindowWidgets)
            addWindow(widget->GetName(), *widget->GetOpen(), false);

        if (auto *sceneView = GetWidget<SceneView>())
            addWindow(sceneView->GetName(), *sceneView->GetOpen(), GUIState::s_sceneViewFloating);

        addWindow("Dear ImGui Demo", m_show_demo_window, false);

        auto &undoRedo = UndoRedo::Instance();
        auto &globalSettings = Settings::Get<GlobalSettings>();
        addAction("file.load_model", "Load ModelAsset", "File", "command", !GUIState::s_modelLoading.load(), false, false);
        addAction("file.new_scene", "New Scene", "File", "command", true, false, false);
        addAction("file.load_scene", "Load Scene", "File", "command", true, false, false);
        addAction("file.save_scene", "Save Scene", "File", "command", true, false, false);
        addAction("file.save_scene_as", "Save Scene As", "File", "command", true, false, false);
        addAction("file.reload_module", "Reload Module", "File", "command", true, false, false);
        addAction("file.exit", "Exit", "File", "command", true, false, false);

        addAction("edit.undo", "Undo", "Edit", "command", undoRedo.CanUndo(), false, false);
        addAction("edit.redo", "Redo", "Edit", "command", undoRedo.CanRedo(), false, false);

        addAction("connection.mcp.toggle", "MCP Server", "Connection", "toggle", true, IsMcpServerRunning(), true);
        addAction("connection.mcp.start", "Start MCP Server", "Connection", "command", !IsMcpServerRunning(), false, false);
        addAction("connection.mcp.stop", "Stop MCP Server", "Connection", "command", IsMcpServerRunning(), false, false);
        addAction("connection.index_codebase", "Index Codebase", "Connection", "command", !m_isIndexing.load(), false, false);
        addAction("connection.rebuild_codebase_index", "Rebuild Codebase Index", "Connection", "command", !m_isIndexing.load(), false, false);

        addAction("viewport.floating", "Viewport Floating", "Window", "toggle", true, GUIState::s_sceneViewFloating, true);
        addAction("viewport.redock", "Redock Viewport", "Window", "command", GUIState::s_sceneViewFloating, false, false);

        addAction("gizmo.transform", "Transform Gizmo", "Gizmos", "toggle", true, GUIState::s_useTransformGizmo, true);
        addAction("gizmo.lights", "Light Gizmos", "Gizmos", "toggle", true, GUIState::s_useLightGizmos, true);
        addAction("gizmo.cameras", "Camera Gizmos", "Gizmos", "toggle", true, GUIState::s_useCameraGizmos, true);
        addAction("gizmo.grid", "Grid", "Gizmos", "toggle", true, globalSettings.draw_grid, true);

        for (GUIStyle style : {GUIStyle::Classic, GUIStyle::Dark, GUIStyle::Light, GUIStyle::Modern, GUIStyle::Unity, GUIStyle::Unreal})
        {
            const std::string id = std::string("layout.style.") + StyleId(style);
            addAction(id, StyleLabel(style), "Layout", "choice", true, GUIState::s_guiStyle == style, true);
        }
        addAction("layout.font.small", "Small Font", "Layout", "choice", true, ImGui::GetIO().FontGlobalScale < 0.95f, true);
        addAction("layout.font.medium", "Medium Font", "Layout", "choice", true,
                  ImGui::GetIO().FontGlobalScale >= 0.95f && ImGui::GetIO().FontGlobalScale < 1.15f, true);
        addAction("layout.font.large", "Large Font", "Layout", "choice", true,
                  ImGui::GetIO().FontGlobalScale >= 1.15f && ImGui::GetIO().FontGlobalScale < 1.35f, true);
        addAction("layout.font.extra_large", "Extra Large Font", "Layout", "choice", true, ImGui::GetIO().FontGlobalScale >= 1.35f, true);
        addAction("layout.reset", "Reset to Default Layout", "Layout", "command", true, false, false);

        addAction("play.start", "Play", "Toolbar", "command", !GUIState::s_playMode, false, false);
        addAction("play.stop", "Stop", "Toolbar", "command", GUIState::s_playMode, false, false);
        addAction("play.pause", "Pause", "Toolbar", "toggle", GUIState::s_playMode, GUIState::s_isPaused, true);

        addAction("status.console_errors", "Show Console Errors", "Status", "command", GetWidget<Console>() != nullptr, false, false);
        addAction("status.console_warnings", "Show Console Warnings", "Status", "command", GetWidget<Console>() != nullptr, false, false);
        addAction("help.imgui_demo", "Dear ImGui Demo", "Help", "toggle", true, m_show_demo_window, true);

        auto *scriptSystem = GetGlobalSystem<ScriptSystem>();
        const bool hasScriptTests = scriptSystem && !scriptSystem->GetTestScriptPaths().empty();
        addAction("help.run_script_tests_all", "Run All Script Tests", "Help", "command", hasScriptTests, false, false);
        addAction("editor.render.toggle", "Toggle Editor Rendering", "Editor", "toggle", true, m_render, true);

        return result.dump();
    }

    std::string GUI::SetEditorWindowOpen(const std::string &windowName, const std::string &argsJson)
    {
        const nlohmann::json args = ParseEditorActionArgs(argsJson);
        const std::string target = NormalizeWindowSelector(windowName);

        auto setOpen = [&](const std::string &name, bool &open, bool focusWhenOpened) -> std::string
        {
            bool newOpen = open;
            std::string error;
            if (!ResolveRequestedBool(open, args, newOpen, error))
                return nlohmann::json{{"error", error}}.dump();

            open = newOpen;
            if (open && focusWhenOpened)
                ImGui::SetWindowFocus(name.c_str());

            return nlohmann::json{
                {"ok", true},
                {"id", "window." + SlugifyEditorToken(name)},
                {"name", name},
                {"open", open},
            }
                .dump();
        };

        for (auto &widget : m_menuWindowWidgets)
        {
            if (SlugifyEditorToken(widget->GetName()) == target)
                return setOpen(widget->GetName(), *widget->GetOpen(), true);
        }

        if (auto *sceneView = GetWidget<SceneView>())
        {
            if (SlugifyEditorToken(sceneView->GetName()) == target || target == "scene_view")
                return setOpen(sceneView->GetName(), *sceneView->GetOpen(), true);
        }

        if (target == "dear_imgui_demo" || target == "imgui_demo" || target == "demo")
            return setOpen("Dear ImGui Demo", m_show_demo_window, false);

        nlohmann::json known = nlohmann::json::array();
        for (auto &widget : m_menuWindowWidgets)
            known.push_back(widget->GetName());
        known.push_back("Viewport");
        known.push_back("Dear ImGui Demo");
        return nlohmann::json{{"error", "unknown editor window: " + windowName}, {"known_windows", known}}.dump();
    }

    std::string GUI::InvokeEditorAction(const std::string &actionId, const std::string &argsJson)
    {
        const std::string action = NormalizeEditorActionId(actionId);
        const nlohmann::json args = ParseEditorActionArgs(argsJson);

        auto ok = [&](nlohmann::json extra = nlohmann::json::object())
        {
            extra["ok"] = true;
            extra["action"] = action;
            return extra.dump();
        };

        if (action.rfind("window.", 0) == 0)
            return SetEditorWindowOpen(action, argsJson);

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();

        if (action == "file.load_model")
        {
            if (GUIState::s_modelLoading.load())
                return R"({"error":"model load already in progress"})";

            std::string path = args.value("path", "");
            if (!path.empty())
            {
                std::filesystem::path modelPath(path);
                if (modelPath.is_relative())
                {
                    std::filesystem::path fromAssets = std::filesystem::path(Path::Assets) / modelPath;
                    std::filesystem::path fromObjects = std::filesystem::path(Path::Assets) / "Objects" / modelPath;
                    modelPath = std::filesystem::exists(fromAssets) ? fromAssets : fromObjects;
                }
                if (!std::filesystem::exists(modelPath))
                    return nlohmann::json{{"error", "model file not found: " + modelPath.string()}}.dump();

                GUIState::s_modelLoading = true;
                const std::string loadPath = modelPath.string();
                ThreadPool::GUI.Enqueue([loadPath]()
                                        {
                    try
                    {
                        if (ModelAsset *m = ModelAsset::Load(loadPath))
                            EventSystem::PushEvent(EventType::ModelLoaded, m);
                    }
                    catch (const std::exception &e)
                    {
                        PE_WARN("[Scene] Failed to load model: %s", e.what());
                    }
                    GUIState::s_modelLoading = false; });
                return ok({{"status", "loading"}, {"path", loadPath}});
            }

            auto *fs = GetWidget<FileSelector>();
            if (!fs)
                return R"({"error":"FileSelector not available"})";

            std::vector<std::string> exts;
            for (const char *ext : FileBrowser::s_modelExtensionsVec)
                exts.push_back(ext);
            fs->OpenSelection([](const std::string &selectedPath)
                              {
                GUIState::s_modelLoading = true;
                ThreadPool::GUI.Enqueue([selectedPath]()
                {
                    try
                    {
                        if (ModelAsset *m = ModelAsset::Load(selectedPath))
                            EventSystem::PushEvent(EventType::ModelLoaded, m);
                    }
                    catch (const std::exception &e)
                    {
                        PE_WARN("[Scene] Failed to load model: %s", e.what());
                    }
                    GUIState::s_modelLoading = false;
                });
                return true; },
                              exts);
            return ok({{"status", "dialog_opened"}});
        }

        if (action == "file.new_scene")
        {
            if (renderer && args.value("discard_unsaved", false))
            {
                pe::NewScene();
                SaveEditorConfig();
                UndoRedo::Instance().Clear();
                return ok();
            }
            NewScene();
            return ok();
        }

        if (action == "file.load_scene")
        {
            std::string path = args.value("path", "");
            if (path.empty())
            {
                if (renderer && renderer->GetScene().IsDirty())
                    m_showSaveBeforeLoad = true;
                else
                    OpenLoadSceneDialog();
                return ok({{"status", "dialog_opened"}});
            }

            if (!renderer)
                return R"({"error":"renderer not available"})";
            if (renderer->GetScene().IsDirty() && !args.value("discard_unsaved", false))
                return R"({"error":"scene has unsaved changes; pass discard_unsaved=true to load anyway"})";

            std::filesystem::path scenePath(path);
            if (scenePath.is_relative())
            {
                std::filesystem::path fromAssets = std::filesystem::path(Path::Assets) / scenePath;
                std::filesystem::path fromScenes = std::filesystem::path(Path::Assets) / "Scenes" / scenePath;
                scenePath = std::filesystem::exists(fromAssets) ? fromAssets : fromScenes;
            }
            if (!std::filesystem::exists(scenePath))
                return nlohmann::json{{"error", "scene file not found: " + scenePath.string()}}.dump();

            pe::LoadScene(scenePath);
            SaveEditorConfig();
            UndoRedo::Instance().Clear();
            return ok({{"path", scenePath.string()}});
        }

        if (action == "file.save_scene")
        {
            if (!renderer)
                return R"({"error":"renderer not available"})";

            std::string path = args.value("path", "");
            if (!path.empty())
            {
                std::filesystem::path savePath(path);
                if (savePath.is_relative())
                    savePath = std::filesystem::path(Path::Assets) / "Scenes" / savePath;
                if (savePath.extension() != ".pescene")
                    savePath += ".pescene";
                if (std::filesystem::exists(savePath) && !args.value("overwrite", false))
                    return nlohmann::json{{"error", "file exists; pass overwrite=true to replace: " + savePath.string()}}.dump();

                pe::SaveScene(savePath);
                return ok({{"path", savePath.string()}});
            }

            Scene &scene = renderer->GetScene();
            if (!scene.GetScenePath().empty())
            {
                pe::SaveScene(scene.GetScenePath());
                return ok({{"path", scene.GetScenePath().string()}});
            }

            ShowSaveSceneMenuItem_Action();
            return ok({{"status", "dialog_opened"}});
        }

        if (action == "file.save_scene_as")
        {
            ShowSaveSceneMenuItem_Action();
            return ok({{"status", "dialog_opened"}});
        }

        if (action == "file.reload_module")
        {
            EventSystem::PushEvent(EventType::ReloadModule);
            return ok();
        }

        if (action == "file.exit")
        {
            if (args.value("discard_unsaved", false))
            {
                SaveEditorConfig();
                EventSystem::PushEvent(EventType::Quit);
            }
            else
            {
                TriggerExitConfirmation();
            }
            return ok();
        }

        if (action == "edit.undo" || action == "edit.redo")
        {
            if (!renderer)
                return R"({"error":"renderer not available"})";

            auto &undoRedo = UndoRedo::Instance();
            int steps = std::max(1, args.value("steps", 1));
            if (action == "edit.undo")
            {
                if (!undoRedo.CanUndo())
                    return R"({"error":"nothing to undo"})";
                undoRedo.UndoTo(renderer->GetScene(), static_cast<size_t>(steps));
            }
            else
            {
                if (!undoRedo.CanRedo())
                    return R"({"error":"nothing to redo"})";
                undoRedo.RedoTo(renderer->GetScene(), static_cast<size_t>(steps));
            }
            return ok({{"steps", steps}});
        }

        if (action == "connection.mcp.toggle" || action == "connection.mcp.start" || action == "connection.mcp.stop")
        {
            bool enabled = !IsMcpServerRunning();
            if (action == "connection.mcp.start")
                enabled = true;
            else if (action == "connection.mcp.stop")
                enabled = false;
            SetMcpServerEnabled(enabled);
            return ok({{"mcp_running", IsMcpServerRunning()}});
        }

        if (action == "connection.index_codebase" || action == "connection.rebuild_codebase_index")
        {
            StartCodebaseIndexing(action == "connection.rebuild_codebase_index" || args.value("full_rebuild", false));
            return ok({{"status", "indexing"}});
        }

        if (action == "viewport.floating")
        {
            bool value = GUIState::s_sceneViewFloating;
            std::string error;
            if (!ResolveRequestedBool(GUIState::s_sceneViewFloating, args, value, error))
                return nlohmann::json{{"error", error}}.dump();
            GUIState::s_sceneViewFloating = value;
            if (!GUIState::s_sceneViewFloating)
                GUIState::s_sceneViewRedockQueued = true;
            return ok({{"floating", GUIState::s_sceneViewFloating}});
        }

        if (action == "viewport.redock")
        {
            GUIState::s_sceneViewFloating = false;
            GUIState::s_sceneViewRedockQueued = true;
            return ok({{"floating", false}});
        }

        auto toggleBool = [&](bool &target) -> std::string
        {
            bool value = target;
            std::string error;
            if (!ResolveRequestedBool(target, args, value, error))
                return nlohmann::json{{"error", error}}.dump();
            target = value;
            return ok({{"value", target}});
        };

        if (action == "gizmo.transform")
            return toggleBool(GUIState::s_useTransformGizmo);
        if (action == "gizmo.lights")
            return toggleBool(GUIState::s_useLightGizmos);
        if (action == "gizmo.cameras")
            return toggleBool(GUIState::s_useCameraGizmos);
        if (action == "gizmo.grid")
            return toggleBool(Settings::Get<GlobalSettings>().draw_grid);

        if (action.rfind("layout.style.", 0) == 0 || action == "layout.style")
        {
            std::string error;
            const std::string style = action == "layout.style" ? args.value("style", "") : action;
            if (!ApplyEditorStyle(style, error))
                return nlohmann::json{{"error", error}}.dump();
            return ok({{"style", StyleId(GUIState::s_guiStyle)}});
        }

        if (action.rfind("layout.font.", 0) == 0 || action == "layout.font")
        {
            if (args.contains("scale") && args["scale"].is_number())
            {
                ImGui::GetIO().FontGlobalScale = args["scale"].get<float>();
                return ok({{"font_scale", ImGui::GetIO().FontGlobalScale}});
            }

            bool presetOk = false;
            const std::string preset = action == "layout.font" ? args.value("preset", "") : action;
            float scale = FontScaleForPreset(preset, presetOk);
            if (!presetOk)
                return nlohmann::json{{"error", "unknown font preset: " + preset}}.dump();

            ImGui::GetIO().FontGlobalScale = scale;
            return ok({{"font_scale", scale}});
        }

        if (action == "layout.reset")
        {
            m_requestDockReset = true;
            return ok();
        }

        if (action == "play.start")
        {
            if (!GUIState::s_playMode)
                Play();
            return ok({{"play_mode", GUIState::s_playMode}, {"paused", GUIState::s_isPaused}});
        }

        if (action == "play.stop")
        {
            if (GUIState::s_playMode)
                Stop();
            return ok({{"play_mode", GUIState::s_playMode}, {"paused", GUIState::s_isPaused}});
        }

        if (action == "play.pause")
        {
            if (!GUIState::s_playMode)
                return R"({"error":"not in play mode"})";

            bool value = GUIState::s_isPaused;
            std::string error;
            if (!ResolveRequestedBool(GUIState::s_isPaused, args, value, error))
                return nlohmann::json{{"error", error}}.dump();
            GUIState::s_isPaused = value;
            SetRuntimePlaySessionPaused(GUIState::s_isPaused);
            return ok({{"paused", GUIState::s_isPaused}});
        }

        if (action == "status.console_errors" || action == "status.console_warnings")
        {
            auto *console = GetWidget<Console>();
            if (!console)
                return R"({"error":"Console not available"})";
            console->FocusWithFilter(action == "status.console_errors" ? "[ERROR]" : "[WARN]");
            return ok();
        }

        if (action == "help.imgui_demo")
            return toggleBool(m_show_demo_window);

        if (action == "help.run_script_tests_all")
        {
            auto *scriptSystem = GetGlobalSystem<ScriptSystem>();
            if (!scriptSystem)
                return R"({"error":"ScriptSystem not available"})";
            const auto tests = scriptSystem->GetTestScriptPaths();
            if (tests.empty())
                return R"({"error":"no script tests available"})";
            RunScriptTests(tests, "all script tests");
            return ok({{"count", static_cast<int>(tests.size())}});
        }

        if (action == "editor.render.toggle")
        {
            bool value = m_render;
            std::string error;
            if (!ResolveRequestedBool(m_render, args, value, error))
                return nlohmann::json{{"error", error}}.dump();
            m_render = value;
            return ok({{"render_enabled", m_render}});
        }

        return nlohmann::json{{"error", "unknown editor action: " + actionId}}.dump();
    }

    static std::atomic_bool s_modelLoading = false;

    void GUI::QueueMainThreadAction(std::function<void()> fn)
    {
        std::lock_guard lock(m_mainThreadActionMutex);
        m_pendingMainThreadActions.push_back(std::move(fn));
    }

    void GUI::ShowLoadModelMenuItem()
    {
        if (ImGui::MenuItem("Load ModelAsset...", "Choose ModelAsset"))
        {
            if (GUIState::s_modelLoading)
                return;

            auto *fs = GetWidget<FileSelector>();
            if (fs)
            {
                std::vector<std::string> exts;
                for (const char *ext : FileBrowser::s_modelExtensionsVec)
                    exts.push_back(ext);

                fs->OpenSelection([](const std::string &path)
                                  {
                    auto loadAsync = [path]()
                    {
                        GUIState::s_modelLoading = true;
                        try
                        {
                            if (ModelAsset *m = ModelAsset::Load(path))
                                EventSystem::PushEvent(EventType::ModelLoaded, m);
                        }
                        catch (const std::exception &e)
                        {
                            PE_WARN("[Scene] Failed to load model: %s", e.what());
                        }
                        GUIState::s_modelLoading = false;
                    };
                    ThreadPool::GUI.Enqueue(loadAsync); return true; }, exts);
            }
        }
    }

    void GUI::ShowLoadSceneMenuItem()
    {
        if (ImGui::MenuItem("Load Scene...", "Load a scene from JSON"))
        {
            RendererSystem *rs = GetGlobalSystem<RendererSystem>();
            if (rs && rs->GetScene().IsDirty())
            {
                m_showSaveBeforeLoad = true;
            }
            else
            {
                OpenLoadSceneDialog();
            }
        }
    }

    void GUI::OpenLoadSceneDialog()
    {
        auto *fs = GetWidget<FileSelector>();
        if (!fs)
            return;

        auto onSceneSelected = [this](const std::string &path)
        {
            UndoRedo::Instance().Clear();
            ThreadPool::GUI.Enqueue([this, path]()
                                    {
                    auto preload = std::make_shared<ScenePreloadHandle>(pe::PreloadScene(path));
                    QueueMainThreadAction([this, preload]()
                                          {
                        if (preload->IsValid())
                        {
                            pe::LoadSceneApply(std::move(*preload));
                            SaveEditorConfig();
                        } }); });
            return true;
        };

        std::vector<std::string> exts = {};
        fs->OpenSelection(onSceneSelected, exts);
    }

    void GUI::DrawSaveBeforeLoadPopup()
    {
        if (m_showSaveBeforeLoad)
        {
            ImGui::OpenPopup("Save Scene?");
            m_showSaveBeforeLoad = false;
        }

        if (ImGui::BeginPopupModal("Save Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Current scene has unsaved changes.\nDo you want to save before loading?");
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Save", ImVec2(80, 0)))
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                {
                    Scene &scene = rs->GetScene();
                    if (!scene.GetScenePath().empty())
                        pe::SaveScene(scene.GetScenePath());
                    else
                        ShowSaveSceneMenuItem_Action();
                }
                ImGui::CloseCurrentPopup();
                OpenLoadSceneDialog();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(80, 0)))
            {
                ImGui::CloseCurrentPopup();
                OpenLoadSceneDialog();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void GUI::NewScene()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        if (rs->GetScene().IsDirty())
            m_showSaveBeforeNew = true;
        else
        {
            pe::NewScene();
            SaveEditorConfig();
            UndoRedo::Instance().Clear();
        }
    }

    void GUI::DrawSaveBeforeNewPopup()
    {
        if (m_showSaveBeforeNew)
        {
            ImGui::OpenPopup("Save Before New?");
            m_showSaveBeforeNew = false;
        }

        if (ImGui::BeginPopupModal("Save Before New?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Current scene has unsaved changes.\nDo you want to save before creating a new scene?");
            ImGui::Dummy(ImVec2(0, 10));

            if (ImGui::Button("Save", ImVec2(80, 0)))
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                {
                    Scene &scene = rs->GetScene();
                    if (!scene.GetScenePath().empty())
                        pe::SaveScene(scene.GetScenePath());
                    else
                        ShowSaveSceneMenuItem_Action();
                    pe::NewScene();
                    SaveEditorConfig();
                    UndoRedo::Instance().Clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(80, 0)))
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                {
                    pe::NewScene();
                    SaveEditorConfig();
                }
                UndoRedo::Instance().Clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void GUI::ShowSaveSceneMenuItem()
    {
        if (ImGui::MenuItem("Save Scene...", "Ctrl+S"))
        {
            RendererSystem *rs = GetGlobalSystem<RendererSystem>();
            if (rs && !rs->GetScene().GetScenePath().empty())
                pe::SaveScene(rs->GetScene().GetScenePath());
            else
                ShowSaveSceneMenuItem_Action();
        }
    }

    void GUI::ShowSaveSceneMenuItem_Action()
    {
        auto *fs = GetWidget<FileSelector>();
        if (fs)
        {
            // Suggest the current scene name, or "untitled" if none
            std::string suggestedName = "untitled";
            if (auto *rs = GetGlobalSystem<RendererSystem>())
            {
                const auto &scenePath = rs->GetScene().GetScenePath();
                if (!scenePath.empty())
                    suggestedName = scenePath.stem().string();
            }
            suggestedName += ".pescene";

            std::vector<std::string> exts = {".pescene"};
            fs->OpenSelection([this](const std::string &path)
                              {
                std::filesystem::path savePath(path);
                if (savePath.extension() != ".pescene")
                    savePath += ".pescene";

                if (std::filesystem::exists(savePath))
                {
                    m_pendingSavePath = savePath;
                    m_showOverwriteConfirmation = true;
                    return true; // Close file selector, show overwrite prompt
                }

                auto saveAsync = [savePath, exitAfter = m_exitAfterSave]()
                {
                    pe::SaveScene(savePath);
                    if (exitAfter)
                    {
                        EventSystem::PushEvent(EventType::Quit);
                    }
                };
                m_exitAfterSave = false;
                ThreadPool::GUI.Enqueue(saveAsync);
                return true; }, exts, Path::Assets + "Scenes/",
                              [this]()
                              { m_exitAfterSave = false; }, suggestedName, "Save");
        }
    }

    void GUI::DrawOverwriteConfirmationPopup()
    {
        if (!m_showOverwriteConfirmation)
            return;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();

        bool open = true;
        if (ImGui::Begin("Overwrite File?", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
        {
            std::string filename = m_pendingSavePath.filename().string();
            ImGui::Text("'%s' already exists.\nDo you want to overwrite it?", filename.c_str());
            ImGui::Separator();

            if (ImGui::Button("Overwrite", ImVec2(120, 0)))
            {
                auto savePath = m_pendingSavePath;
                auto saveAsync = [savePath]()
                {
                    pe::SaveScene(savePath);
                };
                ThreadPool::GUI.Enqueue(saveAsync);
                m_pendingSavePath.clear();
                m_showOverwriteConfirmation = false;
                if (auto *fs = GetWidget<FileSelector>())
                    fs->CancelSelection();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_pendingSavePath.clear();
                m_showOverwriteConfirmation = false;
            }
        }
        ImGui::End();

        if (!open)
        {
            m_pendingSavePath.clear();
            m_showOverwriteConfirmation = false;
        }
    }

    void GUI::ShowExitMenuItem()
    {
        if (ImGui::MenuItem("Exit", "Exit"))
            m_showExitConfirmation = true;
    }

    void GUI::RunScriptTests(const std::vector<std::string> &paths, const std::string &label)
    {
        if (paths.empty())
        {
            PE_WARN("[LuaTests] No script tests available for '%s'", label.c_str());
            return;
        }

        auto *scriptSystem = GetGlobalSystem<ScriptSystem>();
        if (!scriptSystem)
        {
            Log::Error("[LuaTests] ScriptSystem is unavailable");
            return;
        }

        const std::string result = scriptSystem->RunScriptTests(paths);
        size_t lineStart = 0;
        while (lineStart <= result.size())
        {
            size_t lineEnd = result.find('\n', lineStart);
            std::string line = result.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
            if (!line.empty())
            {
                if (IsScriptTestFailureLine(line))
                    Log::Error("[LuaTests] " + StripScriptTestSeverityPrefix(line));
                else if (IsScriptTestWarningLine(line))
                    PE_WARN("[LuaTests] %s", StripScriptTestSeverityPrefix(line).c_str());
                else
                    PE_INFO("[LuaTests] %s", line.c_str());
            }

            if (lineEnd == std::string::npos)
                break;
            lineStart = lineEnd + 1;
        }

        if (auto *console = GetWidget<Console>())
            console->FocusWithFilter("[LuaTests]");
    }

    void GUI::ShowRunScriptTestsMenu()
    {
        if (!ImGui::BeginMenu("Run Script Tests"))
            return;

        auto *scriptSystem = GetGlobalSystem<ScriptSystem>();
        if (!scriptSystem)
        {
            ImGui::MenuItem("Script System Unavailable", nullptr, false, false);
            ImGui::EndMenu();
            return;
        }

        const std::vector<std::string> tests = scriptSystem->GetTestScriptPaths();
        if (ImGui::MenuItem("Run All", nullptr, false, !tests.empty()))
            RunScriptTests(tests, "all script tests");

        ImGui::Separator();

        if (tests.empty())
        {
            ImGui::MenuItem("No Test Scripts Found", nullptr, false, false);
            ImGui::EndMenu();
            return;
        }

        for (const auto &testPath : tests)
        {
            const std::string label = std::filesystem::path(testPath).stem().string();
            if (ImGui::MenuItem(label.c_str()))
                RunScriptTests({testPath}, label);
        }

        ImGui::EndMenu();
    }

    void GUI::SaveEditorConfig()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        const auto &scenePath = rs->GetScene().GetScenePath();
        const std::string startupScene = scenePath.empty() ? "" : scenePath.generic_string();

        // Keep editor restore and runtime/launcher startup selection in sync.
        std::string error;
        if (!WriteEditorStartupScene({}, startupScene, &error) && !error.empty())
            PE_WARN("[Runtime] Could not write editor startup scene: %s", error.c_str());

        error.clear();
        if (!WriteRuntimeStartupScene({}, startupScene, &error) && !error.empty())
            PE_WARN("[Runtime] Could not write startup scene setting: %s", error.c_str());
    }

    void GUI::LoadAgentConfig()
    {
        const std::string configPath = Path::Assets + "Agent/agent_config.json";
        const auto repoRoot = GetEditorRepoRootFromAssets();
        const nlohmann::json defaultConfig = BuildDefaultAgentConfig(repoRoot);

        m_mcpStartEnabled = false;

        std::ifstream f(configPath);
        if (!f)
        {
            WriteAgentConfigFile(configPath, defaultConfig);
            PE_INFO("[MCP] Created default agent_config.json");
            ApplyDefaultCodebaseIndexingConfig(repoRoot, m_codebase.MutableIndexingConfig());
            return;
        }

        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(f);
        }
        catch (...)
        {
            PE_WARN("[Agent] Failed to parse agent_config.json — leaving file unchanged and using defaults for this run");
            ApplyDefaultCodebaseIndexingConfig(repoRoot, m_codebase.MutableIndexingConfig());
            return;
        }

        bool changed = MergeJsonDefaults(j, defaultConfig);
        if (j.contains("indexing") && j["indexing"].is_object() &&
            defaultConfig.contains("indexing") && defaultConfig["indexing"].is_object())
        {
            changed |= AddDefaultJsonStringArrayItems(j["indexing"], defaultConfig["indexing"], "directories");
            changed |= AddDefaultJsonStringArrayItems(j["indexing"], defaultConfig["indexing"], "include_files");
            changed |= AddDefaultJsonStringArrayItems(j["indexing"], defaultConfig["indexing"], "skip_directories");
        }

        // Migration: scrub the obsolete `completion` block written by builds prior to the
        // PhasmaAgent → PhasmaMCP refactor. The in-engine AICompletionService was retired
        // (see PhasmaEditor/Code/GUI/AI/ removal), so any provider api_key kept living in
        // plaintext for no runtime use. Remove it on first load.
        if (j.contains("completion"))
        {
            PE_WARN("[MCP] Removing obsolete `completion` block from agent_config.json "
                    "(in-engine completion was retired; any stored api_key is being scrubbed).");
            j.erase("completion");
            changed = true;
        }

        if (changed)
        {
            WriteAgentConfigFile(configPath, j);
            PE_INFO("[MCP] Updated agent_config.json");
        }

        m_mcpStartEnabled = j.value("mcp", false);

        // Indexing config — always start from defaults so a partial JSON doesn't leave skip lists empty
        auto &config = m_codebase.MutableIndexingConfig();
        ApplyDefaultCodebaseIndexingConfig(repoRoot, config);
        if (j.contains("indexing") && j["indexing"].is_object())
        {
            const auto &idx = j["indexing"];

            const auto loadStrings = [&](const char *key, std::vector<std::string> &vec)
            {
                if (idx.contains(key) && idx[key].is_array())
                {
                    vec.clear();
                    for (const auto &item : idx[key])
                        if (item.is_string())
                            vec.push_back(item.get<std::string>());
                }
            };

            loadStrings("directories", config.directories);
            loadStrings("include_files", config.include_files);
            loadStrings("skip_directories", config.skip_directories);
            loadStrings("skip_files", config.skip_files);
            loadStrings("skip_extensions", config.skip_extensions);
            loadStrings("skip_regex", config.skip_regex);
        }

        PE_INFO("[MCP] Startup: %s", m_mcpStartEnabled ? "enabled" : "disabled");
    }

    void GUI::LoadEditorConfig(const RuntimeStartupSceneSelection &startupScene)
    {
        if (!startupScene.warning.empty())
            PE_WARN("[Runtime] %s", startupScene.warning.c_str());
        if (startupScene.IsExplicitEmpty())
            return;

        if (startupScene.scenePath.empty())
            return;

        if (!std::filesystem::exists(startupScene.scenePath))
        {
            Log::Warn("Startup scene not found: " + startupScene.scenePath.generic_string());
            if (startupScene.source == RuntimeStartupSceneSource::EditorConfig)
            {
                std::string error;
                if (!WriteEditorStartupScene({}, "", &error) && !error.empty())
                    PE_WARN("[Runtime] Could not clear editor startup scene: %s", error.c_str());
            }
            return;
        }

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            pe::LoadScene(startupScene.scenePath);
            SaveEditorConfig();
            UndoRedo::Instance().Clear();
        }
    }

    void GUI::TriggerExitConfirmation()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs && rs->GetScene().IsDirty())
            m_showExitConfirmation = true;
        else
        {
            SaveEditorConfig();
            EventSystem::PushEvent(EventType::Quit);
        }
    }

    void GUI::DrawExitPopup()
    {
        if (m_showExitConfirmation)
        {
            ImGui::OpenPopup("Exit##confirm");
            m_showExitConfirmation = false;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Exit##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            RendererSystem *rs = GetGlobalSystem<RendererSystem>();
            ImGui::Text("The scene has unsaved changes.");
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));

            if (ImGui::Button("Save & Exit", ImVec2(110, 0)))
            {
                if (rs)
                {
                    Scene &scene = rs->GetScene();
                    if (!scene.GetScenePath().empty())
                    {
                        pe::SaveScene(scene.GetScenePath());
                        SaveEditorConfig();
                        EventSystem::PushEvent(EventType::Quit);
                    }
                    else
                    {
                        m_exitAfterSave = true;
                        ShowSaveSceneMenuItem_Action();
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard & Exit", ImVec2(110, 0)))
            {
                SaveEditorConfig();
                EventSystem::PushEvent(EventType::Quit);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    static void SetInitialWindowSizeFraction(float widthFraction, float heightFraction = -1.0f, ImGuiCond cond = ImGuiCond_FirstUseEver)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImVec2 size = ImVec2(
            viewport->WorkSize.x * widthFraction,
            viewport->WorkSize.y * (heightFraction > 0.0f ? heightFraction : widthFraction));
        ImGui::SetNextWindowSize(size, cond);
    }

    void GUI::BuildDockspace()
    {
        if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
            return;

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        float toolbarHeight = 35.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + toolbarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - toolbarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        bool open = true;
        ImGui::Begin("EditorDockSpaceHost", &open, windowFlags);
        ImGui::PopStyleVar(3);

        const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
        m_dockspaceId = static_cast<uint32_t>(dockspaceId);
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

        if (m_requestDockReset)
        {
            ResetDockspaceLayout(m_dockspaceId);
            m_requestDockReset = false;
        }

        ImGui::End();
    }

    void GUI::ResetDockspaceLayout(uint32_t dockspaceId)
    {
        if (dockspaceId == 0)
            return;

        ImGuiID dockspace = static_cast<ImGuiID>(dockspaceId);
        ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspace, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

        constexpr float dockRightFrac = 1.0f / 7.0f;
        constexpr float dockLeftFrac = 1.0f / 6.0f;
        constexpr float dockBottomFrac = 1.0f / 4.5f;

        ImGuiID dockMainId = dockspace;

        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, dockRightFrac, nullptr, &dockMainId);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, dockBottomFrac, nullptr, &dockMainId);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, dockLeftFrac, nullptr, &dockMainId);

        // Central node is now dockMainId - dock the Viewport there
        ImGui::DockBuilderDockWindow("Viewport", dockMainId);

        // Left - Models, Hierarchy (Profiler is floating)
        ImGui::DockBuilderDockWindow("Models", dockLeft);
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);

        // Right - Global Properties and Properties
        ImGui::DockBuilderDockWindow("Global", dockRight);
        ImGui::DockBuilderDockWindow("Properties", dockRight);
        ImGui::DockBuilderDockWindow("Camera", dockRight);

        // Bottom - Console, Asset Viewer, File Browser (Tabbed)
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Asset Info", dockBottom);
        ImGui::DockBuilderDockWindow("File Browser", dockBottom);

        ImGui::DockBuilderFinish(dockspace);

        // Make Console the active tab
        ImGui::SetWindowFocus("Console");
    }

    void GUI::StatusBar()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
        float height = ImGui::GetFrameHeight();
        if (!ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags))
        {
            ImGui::End();
            return;
        }
        ImGui::BeginMenuBar();

        auto *console = GetWidget<Console>();
        const int warns = console ? console->GetWarnCount() : 0;
        const int errors = console ? console->GetErrorCount() : 0;

        // Transparent button style — just coloured text that highlights on hover
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));

        // Error button
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "x %d##errbtn", errors);
            ImGui::PushStyleColor(ImGuiCol_Text, errors > 0 ? ImVec4(1.f, 0.35f, 0.35f, 1.f)
                                                            : ImVec4(0.45f, 0.45f, 0.45f, 1.f));
            if (ImGui::SmallButton(buf) && console)
                console->FocusWithFilter("[ERROR]");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to filter console for errors");
        }

        ImGui::SameLine(0, 8);

        // Warning button
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "! %d##warnbtn", warns);
            ImGui::PushStyleColor(ImGuiCol_Text, warns > 0 ? ImVec4(1.f, 0.85f, 0.1f, 1.f)
                                                           : ImVec4(0.45f, 0.45f, 0.45f, 1.f));
            if (ImGui::SmallButton(buf) && console)
                console->FocusWithFilter("[WARN]");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to filter console for warnings");
        }

        // MCP status button — right-aligned
        {
            const bool mcpRunning = IsMcpServerRunning();
            const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float mcpW = ImGui::CalcTextSize("MCP").x + pad;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - mcpW);

            const ImVec4 mcpColor = mcpRunning ? ImVec4(0.20f, 0.75f, 0.20f, 1.f)
                                               : ImVec4(0.45f, 0.45f, 0.45f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, mcpColor);
            if (ImGui::SmallButton("MCP"))
                SetMcpServerEnabled(!mcpRunning);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(mcpRunning ? "MCP server: ready" : "MCP server: off");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::EndMenuBar();
        ImGui::End();
    }

    void GUI::Menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                ShowLoadModelMenuItem();
                if (ImGui::MenuItem("New Scene"))
                    NewScene();
                ShowLoadSceneMenuItem();
                ShowSaveSceneMenuItem();
                if (ImGui::MenuItem("Save Scene As..."))
                    ShowSaveSceneMenuItem_Action();
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Module"))
                    EventSystem::PushEvent(EventType::ReloadModule);
                ImGui::Separator();
                ShowExitMenuItem();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                auto &undoRedo = UndoRedo::Instance();
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoRedo.CanUndo()))
                {
                    RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        undoRedo.Undo(rs->GetScene());
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoRedo.CanRedo()))
                {
                    RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        undoRedo.Redo(rs->GetScene());
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Connection"))
            {
                const bool mcpRunning = IsMcpServerRunning();
                if (ImGui::MenuItem("MCP Server", nullptr, mcpRunning))
                    SetMcpServerEnabled(!mcpRunning);
                if (ImGui::MenuItem("Index Codebase"))
                    StartCodebaseIndexing();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                for (auto &widget : m_menuWindowWidgets)
                    ImGui::MenuItem(widget->GetName().c_str(), nullptr, widget->GetOpen());

                if (ImGui::BeginMenu("Viewport"))
                {
                    if (auto *sv = GetWidget<SceneView>())
                        ImGui::MenuItem("Enabled", nullptr, sv->GetOpen());

                    if (ImGui::MenuItem("Floating", nullptr, &GUIState::s_sceneViewFloating))
                    {
                        if (GUIState::s_sceneViewFloating)
                        {
                            // Reset when becoming floating
                            GUIState::s_sceneViewFloating = true;
                        }
                        else
                        {
                            GUIState::s_sceneViewRedockQueued = true;
                        }
                    }
                    if (ImGui::MenuItem("Redock", nullptr, false, GUIState::s_sceneViewFloating))
                    {
                        GUIState::s_sceneViewFloating = false;
                        GUIState::s_sceneViewRedockQueued = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Gizmos"))
            {
                auto &gSettings = Settings::Get<GlobalSettings>();
                ImGui::MenuItem("Transform", nullptr, &GUIState::s_useTransformGizmo);
                ImGui::MenuItem("Lights", nullptr, &GUIState::s_useLightGizmos);
                ImGui::MenuItem("Cameras", nullptr, &GUIState::s_useCameraGizmos);
                ImGui::MenuItem("Grid", nullptr, &gSettings.draw_grid);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::BeginMenu("Style"))
                {
                    bool isClassic = GUIState::s_guiStyle == GUIStyle::Classic;
                    bool isModern = GUIState::s_guiStyle == GUIStyle::Modern;
                    bool isDark = GUIState::s_guiStyle == GUIStyle::Dark;
                    bool isLight = GUIState::s_guiStyle == GUIStyle::Light;
                    bool isUnity = GUIState::s_guiStyle == GUIStyle::Unity;
                    bool isUnreal = GUIState::s_guiStyle == GUIStyle::Unreal;

                    if (ImGui::MenuItem("Classic", nullptr, isClassic))
                    {
                        GUIState::s_guiStyle = GUIStyle::Classic;
                        ui::ApplyClassicTheme();
                    }
                    if (ImGui::MenuItem("Dark", nullptr, isDark))
                    {
                        GUIState::s_guiStyle = GUIStyle::Dark;
                        ui::ApplyDarkTheme();
                    }
                    if (ImGui::MenuItem("Light", nullptr, isLight))
                    {
                        GUIState::s_guiStyle = GUIStyle::Light;
                        ui::ApplyLightTheme();
                    }
                    if (ImGui::MenuItem("Modern", nullptr, isModern))
                    {
                        GUIState::s_guiStyle = GUIStyle::Modern;
                        ui::ApplyModernTheme();
                    }
                    if (ImGui::MenuItem("Unity", nullptr, isUnity))
                    {
                        GUIState::s_guiStyle = GUIStyle::Unity;
                        ui::ApplyUnityTheme();
                    }
                    if (ImGui::MenuItem("Unreal", nullptr, isUnreal))
                    {
                        ui::ApplyUnrealTheme();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Font Size"))
                {
                    ImGuiIO &io = ImGui::GetIO();
                    float scale = io.FontGlobalScale;

                    if (ImGui::MenuItem("Small", nullptr, scale < 0.95f))
                        io.FontGlobalScale = 0.85f;
                    if (ImGui::MenuItem("Medium", nullptr, scale >= 0.95f && scale < 1.15f))
                        io.FontGlobalScale = 1.0f;
                    if (ImGui::MenuItem("Large", nullptr, scale >= 1.15f && scale < 1.35f))
                        io.FontGlobalScale = 1.25f;
                    if (ImGui::MenuItem("Extra Large", nullptr, scale >= 1.35f))
                        io.FontGlobalScale = 1.5f;
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset to Default Layout", "Ctrl+Shift+L"))
                    m_requestDockReset = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ShowRunScriptTestsMenu();
                ImGui::Separator();
                ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_show_demo_window);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void GUI::StartCodebaseIndexing()
    {
        StartCodebaseIndexing(false);
    }

    void GUI::StartCodebaseIndexing(bool fullRebuild)
    {
        const auto &indexingConfig = m_codebase.GetIndexingConfig();
        const auto &dirs = indexingConfig.directories;
        if (dirs.empty())
        {
            PE_WARN("[Agent] No indexing directories configured");
            return;
        }
        if (m_isIndexing.load())
        {
            PE_WARN("[Agent] Codebase indexing already running");
            return;
        }

        m_codebase.EnsureStores();
        auto codebaseBM25 = m_codebase.GetCodebaseBM25Shared();

        if (m_indexThread.joinable())
            m_indexThread.join();

        m_codebase.SetIndexing(true);
        m_isIndexing.store(true);
        m_indexCancel.store(false);
        m_indexProgress.store(0);
        m_indexTotal.store(0);
        {
            std::lock_guard lock(m_indexMutex);
            m_indexCurrentFile.clear();
        }

        if (fullRebuild)
            codebaseBM25->Clear();

        auto includeFiles = indexingConfig.include_files;
        auto skipDirs = indexingConfig.skip_directories;
        auto skipFiles = indexingConfig.skip_files;
        auto skipExts = indexingConfig.skip_extensions;
        auto skipRegex = indexingConfig.skip_regex;

        m_indexThread = std::thread([this, codebaseBM25, dirs, includeFiles, skipDirs, skipFiles, skipExts, skipRegex]()
                                    {
            pmcp::IndexerConfig config;
            config.directories = dirs;
            config.include_files = includeFiles;
            config.skip_directories = skipDirs;
            config.skip_files = skipFiles;
            if (!skipExts.empty())
                config.skip_extensions = skipExts;
            config.skip_regex = skipRegex;

            PE_INFO("[Agent] Indexing started (%d directories)", static_cast<int>(config.directories.size()));

            auto pIndexer = std::make_unique<pmcp::CodebaseIndexer>(codebaseBM25.get(),
                [this](int done, int total, const std::string &file)
                {
                    m_indexProgress.store(done);
                    m_indexTotal.store(total);
                    {
                        std::lock_guard lock(m_indexMutex);
                        m_indexCurrentFile = file;
                    }
                    if (done % 50 == 0 || done == total)
                        PE_INFO("[Agent] %d/%d  %s", done, total, file.c_str());
                });

            {
                std::lock_guard lock(m_indexMutex);
                m_indexerPtr = pIndexer.get();
            }

            int chunks = pIndexer->Index(config);

            {
                std::lock_guard lock(m_indexMutex);
                m_indexerPtr = nullptr;
            }

            PE_INFO("[Agent] Indexing %s: %d chunks", m_indexCancel.load() ? "cancelled" : "finished", chunks);
            m_codebase.SetIndexing(false);
            m_isIndexing.store(false); });
    }

    void GUI::CancelCodebaseIndexing()
    {
        m_indexCancel.store(true);
        std::lock_guard lock(m_indexMutex);
        if (m_indexerPtr)
            static_cast<pmcp::CodebaseIndexer *>(m_indexerPtr)->Cancel();
    }

    bool GUI::HasCodebaseIndex() const
    {
        auto bm25 = m_codebase.GetCodebaseBM25Shared();
        return bm25 && bm25->Size() > 0;
    }

    size_t GUI::GetCodebaseEntryCount() const
    {
        auto bm25 = m_codebase.GetCodebaseBM25Shared();
        return bm25 ? bm25->Size() : 0;
    }

    void GUI::Init()
    {
        if (!GUIBackend::IsSupported())
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();

        gSettings.model_list.clear();

        auto Deduplicate = [](auto &vec)
        {
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        };

        const std::filesystem::path modelsDir = std::filesystem::path(Path::Assets + "Objects");
        if (std::filesystem::exists(modelsDir))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(modelsDir))
            {
                if (FileBrowser::IsModelFile(file.path()))
                {
                    auto relativePath = std::filesystem::relative(file.path(), modelsDir);
                    auto u8str = relativePath.generic_u8string();
                    gSettings.model_list.push_back(std::string(reinterpret_cast<const char *>(u8str.c_str())));
                }
            }
        }
        Deduplicate(gSettings.model_list);

        m_hasIniFile = std::filesystem::exists("imgui.ini");

        if (s_hotReloadCtx)
        {
            ImGui::SetCurrentContext(s_hotReloadCtx);
            m_ownsImGuiContext = false;
            s_hotReloadCtx = nullptr;
        }

        if (!ImGui::GetCurrentContext())
        {
            ImGui::CreateContext();
            m_ownsImGuiContext = true;
        }

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        GUIBackend::ConfigureIO();

        ImGui::StyleColorsClassic();

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = PE_LOAD_OP_LOAD;
        GUIBackend::Init(m_attachment.get());

        // Load ALL fonts upfront for dynamic style switching. A hot-reload context
        // already owns its font atlas, so reuse its first font instead of appending
        // duplicate font data on every DLL swap.
        if (m_ownsImGuiContext)
        {
            static const ImWchar icon_ranges[] = {0xf000, 0xf8ff, 0}; // FontAwesome range
            std::string iconFontPath = Path::Assets + "Fonts/fa-solid-900.ttf";
            std::string interFontPath = Path::Assets + "Fonts/Inter-Regular.ttf";
            std::string robotoFontPath = Path::Assets + "Fonts/Roboto-Regular.ttf";
            // std::string sourceSansFontPath = Path::Assets + "Fonts/SourceSans3-Regular.ttf";
            std::string openSansFontPath = Path::Assets + "Fonts/OpenSans-Regular.ttf";
            // std::string latoFontPath = Path::Assets + "Fonts/Lato-Regular.ttf";
            float fontSize = 15.0f;

            // Symbol glyph ranges for fallback font (DejaVu Sans covers these)
            static const ImWchar symbolRanges[] = {
                0x2000,
                0x206F, // General Punctuation (' ' " " … – — etc.)
                0x2190,
                0x21FF, // Arrows
                0x2200,
                0x22FF, // Mathematical Operators
                0x2500,
                0x257F, // Box Drawing
                0x2580,
                0x259F, // Block Elements
                0x25A0,
                0x25FF, // Geometric Shapes
                0x2600,
                0x26FF, // Miscellaneous Symbols
                0x2700,
                0x27BF, // Dingbats
                0,
            };
            std::string fallbackFontPath = Path::Assets + "Fonts/DejaVuSans.ttf";

            // Helper lambda to merge icon font + symbol fallback into the current base font
            auto addMergedFonts = [&]()
            {
                if (std::filesystem::exists(iconFontPath))
                {
                    ImFontConfig config;
                    config.MergeMode = true;
                    config.PixelSnapH = true;
                    config.GlyphMinAdvanceX = fontSize;
                    io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), fontSize, &config, icon_ranges);
                }
                if (std::filesystem::exists(fallbackFontPath))
                {
                    ImFontConfig config;
                    config.MergeMode = true;
                    io.Fonts->AddFontFromFileTTF(fallbackFontPath.c_str(), fontSize, &config, symbolRanges);
                }
            };

            // 1. Classic (Default ImGui)
            GUIState::s_fontClassic = io.Fonts->AddFontDefault();
            addMergedFonts();

            // 2. Unity (Inter)
            if (std::filesystem::exists(interFontPath))
            {
                GUIState::s_fontUnity = io.Fonts->AddFontFromFileTTF(interFontPath.c_str(), fontSize);
                addMergedFonts();
            }
            else
                GUIState::s_fontUnity = GUIState::s_fontClassic;

            // 3. Unreal (Roboto)
            if (std::filesystem::exists(robotoFontPath))
            {
                GUIState::s_fontUnreal = io.Fonts->AddFontFromFileTTF(robotoFontPath.c_str(), fontSize);
                addMergedFonts();
            }
            else
                GUIState::s_fontUnreal = GUIState::s_fontClassic;

            // 4, 5, 6. Modern, Dark, Light (OpenSans)
            if (std::filesystem::exists(openSansFontPath))
            {
                GUIState::s_fontLight = io.Fonts->AddFontFromFileTTF(openSansFontPath.c_str(), fontSize + 2);
                addMergedFonts();
            }
            else
            {
                GUIState::s_fontLight = GUIState::s_fontClassic;
            }
            GUIState::s_fontDark = GUIState::s_fontLight;
            GUIState::s_fontModern = GUIState::s_fontLight;
        }
        else if (io.Fonts && io.Fonts->Fonts.Size > 0)
        {
            ImFont *font = io.Fonts->Fonts[0];
            GUIState::s_fontClassic = font;
            GUIState::s_fontUnity = font;
            GUIState::s_fontUnreal = font;
            GUIState::s_fontLight = font;
            GUIState::s_fontDark = font;
            GUIState::s_fontModern = font;
        }

        GUIBackend::CreateFontsTexture();

        if (GUIState::s_guiStyle == GUIStyle::Classic)
            ui::ApplyClassicTheme();
        else if (GUIState::s_guiStyle == GUIStyle::Dark)
            ui::ApplyDarkTheme();
        else if (GUIState::s_guiStyle == GUIStyle::Light)
            ui::ApplyLightTheme();
        else if (GUIState::s_guiStyle == GUIStyle::Modern)
            ui::ApplyModernTheme();
        else if (GUIState::s_guiStyle == GUIStyle::Unity)
            ui::ApplyUnityTheme();
        else if (GUIState::s_guiStyle == GUIStyle::Unreal)
            ui::ApplyUnrealTheme();
        else
            ui::ApplyUnityTheme();

        auto AddGpuTimerInfo = [this](const std::any &data)
        {
            try
            {
                const auto &commandTimerInfos = std::any_cast<const std::vector<GpuTimerSample> &>(data);
                if (commandTimerInfos.empty())
                    return;

                std::lock_guard<std::mutex> lock(m_timerMutex);
                m_gpuTimerInfos.insert(m_gpuTimerInfos.end(), commandTimerInfos.begin(), commandTimerInfos.end());
            }
            catch (const std::bad_any_cast &ex)
            {
                PE_ERROR(std::string("Bad any cast in GUI::Init()::AddGpuTimerInfo: " + std::string(ex.what())).c_str());
                (void)ex;
            }
        };

        m_afterCommandWaitToken = EventSystem::RegisterCallbackWithToken(EventType::AfterCommandWait, std::move(AddGpuTimerInfo));

        InitAgentServices();

        auto properties = std::make_shared<Properties>();
        auto profiler = std::make_shared<ProfilerWidget>();
        auto models = std::make_shared<Models>();
        auto assetInfo = std::make_shared<AssetInfo>();
        auto sceneView = std::make_shared<SceneView>();
        auto loading = std::make_shared<Loading>();
        auto fileBrowser = std::make_shared<FileBrowser>();
        auto fileSelector = std::make_shared<FileSelector>(); // Separate instance for popups
        auto hierarchy = std::make_shared<Hierarchy>();
        auto particles = std::make_shared<Particles>();
        auto cameraWidget = std::make_shared<CameraWidget>();
        auto console = std::make_shared<Console>();
        auto transformWidget = std::make_shared<TransformWidget>();
        auto meshWidget = std::make_shared<MeshWidget>();
        auto lightWidget = std::make_shared<LightWidget>();
        auto globalWidget = std::make_shared<GlobalWidget>();
        auto scriptEditor = std::make_shared<ScriptEditor>();
        auto shaderEditor = std::make_shared<ShaderEditor>();
        auto animTimeline = std::make_shared<AnimationTimeline>();
#ifdef PE_PHYSICS
        auto physicsWidget = std::make_shared<PhysicsWidget>();
#endif
#ifdef PE_AUDIO
        auto audioWidget = std::make_shared<AudioWidget>();
#endif
        // Console added early to potentially influence tab ordering (Leftmost)
        m_widgets = {
            console,
            properties,
            profiler,
            models,
            assetInfo,
            sceneView,
            loading,
            fileBrowser,
            fileSelector,
            hierarchy,
            particles,
            cameraWidget,
            transformWidget,
            meshWidget,
            lightWidget,
            globalWidget,
            scriptEditor,
            shaderEditor,
            animTimeline,
#ifdef PE_PHYSICS
            physicsWidget,
#endif
#ifdef PE_AUDIO
            audioWidget,
#endif
        };

        // Initialize Core Logging and attach Console
        Log::Attach([console](const std::string &msg, LogType type)
                    { console->AddLog(type, "%s", msg.c_str()); });

        // Populate Menu Vectors
        m_menuWindowWidgets = {console,
                               profiler,
                               properties,
                               models,
                               assetInfo,
                               fileBrowser,
                               hierarchy,
                               particles,
                               globalWidget,
                               scriptEditor,
                               shaderEditor,
                               animTimeline};
        for (auto &widget : m_widgets)
            widget->Init(this);

        RHII.GetMainQueue()->WaitIdle();
        m_initialized = true;
    }

    void GUI::InitAgentServices()
    {
        if (m_editorToolRuntime && m_editorMcp)
            return;

        m_editorToolRuntime = std::make_unique<EditorToolRuntime>(
            [this](std::function<void()> fn)
            {
                QueueMainThreadAction(std::move(fn));
            },
            RHII.GetWindow());
        m_editorMcp = std::make_unique<EditorMcp>(m_editorToolRuntime.get(), this);
        LoadAgentConfig();
        if (m_mcpStartEnabled)
            m_editorMcp->Start();
    }

    void GUI::ApplyStartupLayout(bool restoreLastScene, const RuntimeStartupSceneSelection &startupScene)
    {
        SDL_PumpEvents();

        if (m_hasIniFile)
            ImGui::LoadIniSettingsFromDisk("imgui.ini");
        else
            m_requestDockReset = true;

        if (restoreLastScene)
        {
            // Normal startup restores the last open scene. Hot-reload startup already
            // restored a live snapshot and must not overwrite it from editor_config.json.
            LoadEditorConfig(startupScene);
        }
    }

    void GUI::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_initialized)
            return;

        if (!m_render || ImGui::GetDrawData()->TotalVtxCount <= 0)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();
        m_attachment->image = displayRT;

        const bool canCopySceneView =
            GUIState::s_sceneViewImage && displayRT &&
            GUIState::s_sceneViewImage->GetWidth() == displayRT->GetWidth() &&
            GUIState::s_sceneViewImage->GetHeight() == displayRT->GetHeight();

        if (canCopySceneView)
        {
            cmd->CopyImage(displayRT, GUIState::s_sceneViewImage);

            ImageBarrierInfo sceneViewBarrier{};
            sceneViewBarrier.image = GUIState::s_sceneViewImage;
            sceneViewBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneViewBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            sceneViewBarrier.accessMask = PE_ACCESS_SHADER_READ;
            cmd->ImageBarrier(sceneViewBarrier);
        }

        cmd->BeginPass(1, m_attachment.get(), "GUI", true);
        GUIBackend::RenderDrawData(cmd);
        cmd->EndPass();
    }

    void GUI::DrawPlatformWindows()
    {
        if (!m_initialized)
            return;
        if (!GUIBackend::SupportsPlatformWindows())
            return;

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    void GUI::PumpMainThreadActions()
    {
        std::vector<std::function<void()>> mainThreadActions;
        {
            std::lock_guard lock(m_mainThreadActionMutex);
            mainThreadActions.swap(m_pendingMainThreadActions);
        }
        for (auto &fn : mainThreadActions)
            fn();
    }

    void GUI::Update()
    {
        // Drain the MCP/tool action queue before the render-state guard so that
        // queued tool calls (screenshot, Lua exec, mouse input) are never starved
        // when the editor window is minimised or rendering is paused.
        PumpMainThreadActions();

        if (!m_initialized)
            return;

        GUIState::s_sceneViewImageRectValid = false;

        if (!m_render)
            return;

        // Push the font for the current style
        ImFont *currentFont = GUIState::s_fontClassic;

        switch (GUIState::s_guiStyle)
        {
        case GUIStyle::Dark:
            currentFont = GUIState::s_fontDark;
            break;
        case GUIStyle::Light:
            currentFont = GUIState::s_fontLight;
            break;
        case GUIStyle::Modern:
            currentFont = GUIState::s_fontModern;
            break;
        case GUIStyle::Unity:
            currentFont = GUIState::s_fontUnity;
            break;
        case GUIStyle::Unreal:
            currentFont = GUIState::s_fontUnreal;
            break;
        case GUIStyle::Classic:
        default:
            currentFont = GUIState::s_fontClassic;
            break;
        }

        if (currentFont)
            ImGui::PushFont(currentFont);

        // Undo/Redo keyboard shortcuts - only when no text input is focused
        RendererSystem *undoRedoRS = GetGlobalSystem<RendererSystem>();
        auto &undoRedo = UndoRedo::Instance();
        {
            ImGuiIO &io = ImGui::GetIO();
            if (!io.WantTextInput && io.KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
                {
                    if (undoRedoRS && undoRedo.CanUndo())
                        undoRedo.Undo(undoRedoRS->GetScene());
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    if (undoRedoRS && undoRedo.CanRedo())
                        undoRedo.Redo(undoRedoRS->GetScene());
                }
                if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false))
                    m_requestDockReset = true;

                // Ctrl+S - save scene
                if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                {
                    if (undoRedoRS)
                    {
                        Scene &scene = undoRedoRS->GetScene();
                        if (!scene.GetScenePath().empty())
                        {
                            pe::SaveScene(scene.GetScenePath());
                        }
                        else
                        {
                            // No path yet - open "Save As" dialog
                            ShowSaveSceneMenuItem_Action();
                        }
                    }
                }
            }

            // Delete key - remove selected entity
            if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            {
                auto &selection = SelectionManager::Instance();
                if (selection.HasSelection())
                {
                    // Record undo snapshot before any destructive action
                    if (undoRedoRS)
                        undoRedo.RecordSnapshot(undoRedoRS->GetScene(), "Deleted Node");

                    SelectionType selType = selection.GetSelectionType();
                    if (selType == SelectionType::Node || selType == SelectionType::Mesh)
                    {
                        NodeId *node = selection.GetSelectedNode();
                        if (node)
                        {
                            RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                            if (rs)
                            {
                                Scene &scene = rs->GetScene();
                                uint32_t nodeFlags = scene.GetComponentFlags(node);
                                bool canDelete = true;
                                if (nodeFlags & Component_Camera)
                                {
                                    Camera *cam = scene.GetCameraForNode(node);
                                    canDelete = cam && scene.GetCameras().size() > 1;
                                }
                                if (canDelete)
                                {
                                    selection.ClearSelection();
                                    scene.DeleteNode(node);
                                    EventSystem::PushEvent(EventType::NodeRemoved);
                                }
                            }
                        }
                    }
                    else if (selType == SelectionType::Emitter)
                    {
                        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                        if (rs)
                        {
                            ParticleManager *pm = rs->GetScene().GetParticleManager();
                            if (pm)
                            {
                                int idx = selection.GetSelectedEmitterIndex();
                                auto &emitters = pm->GetEmitters();
                                auto &names = pm->GetEmitterNames();
                                if (idx >= 0 && idx < static_cast<int>(emitters.size()))
                                {
                                    emitters.erase(emitters.begin() + idx);
                                    if (idx < static_cast<int>(names.size()))
                                        names.erase(names.begin() + idx);
                                    pm->UpdateEmitterBuffer();
                                    selection.ClearSelection();
                                }
                            }
                        }
                    }
                }
            }
        }

        {
            PE_PROFILE_SCOPE("Menu & Dockspace");
            Menu();
            StatusBar();
            DrawExitPopup();
            DrawSaveBeforeLoadPopup();
            DrawSaveBeforeNewPopup();
            DrawOverwriteConfirmationPopup();
            Toolbar();
            BuildDockspace();
        }

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        {
            PE_PROFILE_SCOPE("Widgets");
            for (auto &widget : m_widgets)
            {
                if (widget->IsOpen())
                    widget->Update();
            }
        }

        // Undo/Redo auto-capture: detect state changes by comparing idle snapshots.
        // After any activity, keep capturing for a few idle frames to catch
        // async changes (e.g. ModelLoaded events processed after GUI update).
        bool anyActive = ImGui::IsAnyItemActive();
        if (anyActive)
            m_idleFramesAfterEdit = 0;
        if (!anyActive && m_wasAnyItemActive)
            m_needIdleCapture = true; // Activity just ended, start capturing
        if (!anyActive && m_needIdleCapture && undoRedoRS)
        {
            undoRedo.CaptureIdleState(undoRedoRS->GetScene());
            m_idleFramesAfterEdit++;
            if (m_idleFramesAfterEdit >= 3)
            {
                m_needIdleCapture = false;
                m_idleFramesAfterEdit = 0;
            }
        }
        m_wasAnyItemActive = anyActive;

        if (currentFont)
            ImGui::PopFont();
    }

    std::vector<GpuTimerSample> GUI::PopGpuTimerInfos()
    {
        std::lock_guard<std::mutex> lock(m_timerMutex);
        if (m_gpuTimerInfos.empty())
            return {};

        std::vector<GpuTimerSample> timers;
        timers.swap(m_gpuTimerInfos);
        return timers;
    }

    void GUI::Toolbar()
    {
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        float toolbarHeight = 35.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Toolbar", nullptr, windowFlags);
        ImGui::PopStyleVar();

        float buttonSize = 25.0f;
        float centerX = ImGui::GetWindowWidth() * 0.5f;
        float centerY = (toolbarHeight - buttonSize) * 0.5f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;

        // Transparent button backgrounds
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        // Undo/Redo buttons (left side)
        {
            auto &ur = UndoRedo::Instance();
            RendererSystem *undoRS = GetGlobalSystem<RendererSystem>();
            ImGui::SetCursorPos(ImVec2(8.0f, centerY));

            // --- Undo button ---
            bool canUndo = ur.CanUndo();
            if (!canUndo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)) && canUndo)
            {
                if (undoRS)
                    ur.Undo(undoRS->GetScene());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Undo (Ctrl+Z)");
            if (!canUndo)
                ImGui::PopStyleColor();

            // Undo history arrow
            ImGui::SameLine(0, 1);
            if (!canUndo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::ArrowButton("##undoArrow", ImGuiDir_Down) && canUndo)
                ImGui::OpenPopup("##UndoHistory");
            if (!canUndo)
                ImGui::PopStyleColor();

            if (ImGui::BeginPopup("##UndoHistory"))
            {
                const auto &undoStack = ur.GetUndoStack();
                if (ImGui::BeginChild("##UndoScroll", ImVec2(220, 200), false))
                {
                    size_t n = undoStack.size();
                    for (size_t i = 0; i < n; i++)
                    {
                        // index 0 = most recent (back of deque)
                        const auto &entry = undoStack[n - 1 - i];
                        bool isNext = (i == 0);
                        std::string label = std::to_string(i + 1) + ". " + entry.label;
                        if (isNext)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                        if (ImGui::Selectable(label.c_str()))
                        {
                            if (undoRS)
                                ur.UndoTo(undoRS->GetScene(), i + 1);
                            ImGui::CloseCurrentPopup();
                        }
                        if (isNext)
                            ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }

            ImGui::SameLine();

            // --- Redo button ---
            bool canRedo = ur.CanRedo();
            if (!canRedo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button(ICON_FA_ROTATE_RIGHT, ImVec2(buttonSize, buttonSize)) && canRedo)
            {
                if (undoRS)
                    ur.Redo(undoRS->GetScene());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Redo (Ctrl+Y)");
            if (!canRedo)
                ImGui::PopStyleColor();

            // Redo history arrow
            ImGui::SameLine(0, 1);
            if (!canRedo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::ArrowButton("##redoArrow", ImGuiDir_Down) && canRedo)
                ImGui::OpenPopup("##RedoHistory");
            if (!canRedo)
                ImGui::PopStyleColor();

            if (ImGui::BeginPopup("##RedoHistory"))
            {
                const auto &redoStack = ur.GetRedoStack();
                if (ImGui::BeginChild("##RedoScroll", ImVec2(220, 200), false))
                {
                    size_t n = redoStack.size();
                    for (size_t i = 0; i < n; i++)
                    {
                        const auto &entry = redoStack[n - 1 - i];
                        bool isNext = (i == 0);
                        std::string label = std::to_string(i + 1) + ". " + entry.label;
                        if (isNext)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                        if (ImGui::Selectable(label.c_str()))
                        {
                            if (undoRS)
                                ur.RedoTo(undoRS->GetScene(), i + 1);
                            ImGui::CloseCurrentPopup();
                        }
                        if (isNext)
                            ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
        }

        if (GUIState::s_playMode)
        {
            // Center two buttons: Offset = (2*size + spacing) / 2
            float totalWidth = 2.0f * buttonSize + spacing;
            ImGui::SetCursorPos(ImVec2(centerX - totalWidth * 0.5f, centerY));

            // Stop Button (Red icon)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP, ImVec2(buttonSize, buttonSize)))
                Stop();
            ImGui::PopStyleColor();

            ImGui::SameLine();

            // Pause/Play Button (White icon)
            const char *pauseIcon = GUIState::s_isPaused ? ICON_FA_PLAY : ICON_FA_PAUSE;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            if (ImGui::Button(pauseIcon, ImVec2(buttonSize, buttonSize)))
            {
                GUIState::s_isPaused = !GUIState::s_isPaused;
                SetRuntimePlaySessionPaused(GUIState::s_isPaused);
            }
            ImGui::PopStyleColor();
        }
        else
        {
            // Center one button
            ImGui::SetCursorPos(ImVec2(centerX - buttonSize * 0.5f, centerY));

            // Play Button (Green icon)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY, ImVec2(buttonSize, buttonSize)))
                Play();
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();    // Pop FramePadding
        ImGui::PopStyleColor(3); // Pop button colors
        ImGui::End();
    }

    void GUI::Play()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            rs->WaitAllFramesCommands();
            m_playModeSnapshot = rs->GetScene().TakeSnapshot();
            GUIState::s_playMode = true;
            GUIState::s_isPaused = false;
            StartRuntimePlaySession(rs->GetScene());
        }
    }

    void GUI::Stop()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            StopRuntimePlaySession();
            GUIState::s_playMode = false;
            GUIState::s_isPaused = false;
            if (!m_playModeSnapshot.empty())
            {
                rs->WaitAllFramesCommands();
                rs->GetScene().RestoreSnapshot(m_playModeSnapshot);
                m_playModeSnapshot.clear();
            }
            ClearRuntimeAnimationState();
            UndoRedo::Instance().Clear();
        }
    }
} // namespace pe
