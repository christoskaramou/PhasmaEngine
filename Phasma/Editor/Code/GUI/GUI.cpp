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
#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"
#include "Scene/SelectionManager.h"
#include "Scene/SceneHost.h"
#include "UI/RuntimeUi.h"
#include "IconsFontAwesome.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "Widgets/CameraWidget.h"
#include "Widgets/Console.h"
#include "Widgets/FileBrowser.h"
#include "Widgets/FileSelector.h"
#include "Widgets/Hierarchy.h"
#include "Widgets/LightWidget.h"
#include "Widgets/Loading.h"
#include "Widgets/MapPainter.h"
#include "Widgets/TerrainBrush.h"
#include "Widgets/MeshWidget.h"
#include "Widgets/ProfilerWidget.h"
#include "Widgets/Particles.h"
#include "Widgets/Properties.h"
#include "Widgets/PrefabViewer.h"
#include "Widgets/RuntimeUiPalette.h"
#include "Widgets/SceneScripts.h"
#include "Widgets/SceneView.h"
#include "Widgets/ScriptEditor.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/SpriteEditor.h"
#include "Widgets/AnimationTimeline.h"
#include "Widgets/RigEditor.h"
#include "Phasma/MCP/Codebase/CodebaseIndexer.h"
#include "Widgets/TransformWidget.h"
#ifdef PE_PHYSICS
#include "Widgets/PhysicsWidget.h"
#endif
#ifdef PE_PHYSICS2D
#include "Widgets/Physics2DWidget.h"
#endif
#ifdef PE_AUDIO
#include "Widgets/AudioWidget.h"
#endif
#include "UndoRedo.h"
#include <nlohmann/json.hpp>
#include "imgui/imgui_internal.h"
#include <algorithm>

#if defined(PE_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pe
{
    namespace
    {
        // The launcher copies PhasmaEditorModule.dll to a versioned name at start, so a rebuilt DLL only
        // takes effect after a restart: the status bar shows which build runs and flags a newer one.
        struct ModuleBuildInfo
        {
            std::filesystem::file_time_type loaded{};
            std::filesystem::file_time_type onDisk{};
            bool valid = false;
            bool stale = false;
            bool warned = false;
            double nextCheck = 0.0;
        };
        ModuleBuildInfo s_moduleBuild;

        std::filesystem::path EditorModulePath()
        {
#if defined(PE_WIN32)
            const char *name = "PhasmaEditorModule.dll";
#else
            const char *name = "libPhasmaEditorModule.so";
#endif
            return std::filesystem::path(reinterpret_cast<const char8_t *>(Path::Executable.c_str())) / name;
        }

        std::string FormatFileTime(std::filesystem::file_time_type t)
        {
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
            std::tm tm{};
#if defined(PE_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            return buf;
        }

        void RefreshModuleBuild(double now)
        {
            if (now < s_moduleBuild.nextCheck)
                return;
            s_moduleBuild.nextCheck = now + 2.0;
            std::error_code ec;
            const auto stamp = std::filesystem::last_write_time(EditorModulePath(), ec);
            if (ec)
                return;
            if (!s_moduleBuild.valid)
            {
                s_moduleBuild.loaded = stamp;
                s_moduleBuild.valid = true;
                PE_INFO("[Module] running the PhasmaEditorModule build of %s", FormatFileTime(stamp).c_str());
            }
            s_moduleBuild.onDisk = stamp;
            s_moduleBuild.stale = stamp != s_moduleBuild.loaded;
            if (s_moduleBuild.stale && !s_moduleBuild.warned)
            {
                s_moduleBuild.warned = true;
                PE_WARN("[Module] PhasmaEditorModule rebuilt at %s but this editor still runs the %s build - restart it",
                        FormatFileTime(stamp).c_str(), FormatFileTime(s_moduleBuild.loaded).c_str());
            }
        }
    } // namespace

    namespace
    {
        constexpr float kToolbarHeight = 35.0f;
        constexpr const char *kEditorGuiStyleKey = "gui_style";
        constexpr const char *kEditorFontScaleKey = "font_scale";
        constexpr float kMinEditorFontScale = 0.5f;
        constexpr float kMaxEditorFontScale = 2.5f;
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

        bool IsPortableRelativePath(const std::filesystem::path &path)
        {
            if (path.empty() || path.is_absolute())
                return false;

            auto it = path.begin();
            return it == path.end() || *it != "..";
        }

        std::string MakePortableStartupScenePath(const std::filesystem::path &scenePath)
        {
            if (scenePath.empty())
                return {};

            std::error_code ec;
            std::filesystem::path normalizedScene = std::filesystem::weakly_canonical(scenePath, ec);
            if (ec)
                normalizedScene = scenePath.lexically_normal();

            ec.clear();
            std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(Path::Assets, ec);
            if (ec)
                assetsRoot = std::filesystem::path(Path::Assets).lexically_normal();

            ec.clear();
            const std::filesystem::path fromAssets = std::filesystem::relative(normalizedScene, assetsRoot, ec);
            if (!ec && IsPortableRelativePath(fromAssets))
                return (std::filesystem::path("Assets") / fromAssets).generic_string();

            return scenePath.generic_string();
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

        const char *SceneViewAspectId(SceneViewAspectMode mode)
        {
            switch (mode)
            {
            case SceneViewAspectMode::Free:
                return "free";
            case SceneViewAspectMode::Landscape16x9:
                return "16_9";
            case SceneViewAspectMode::Portrait9x16:
                return "9_16";
            case SceneViewAspectMode::Landscape19_5x9:
                return "19_5_9";
            case SceneViewAspectMode::Portrait9x19_5:
                return "9_19_5";
            case SceneViewAspectMode::Square1x1:
                return "1_1";
            default:
                return "unknown";
            }
        }

        const char *SceneViewAspectLabel(SceneViewAspectMode mode)
        {
            switch (mode)
            {
            case SceneViewAspectMode::Free:
                return "Free Aspect";
            case SceneViewAspectMode::Landscape16x9:
                return "16:9";
            case SceneViewAspectMode::Portrait9x16:
                return "9:16";
            case SceneViewAspectMode::Landscape19_5x9:
                return "19.5:9";
            case SceneViewAspectMode::Portrait9x19_5:
                return "9:19.5";
            case SceneViewAspectMode::Square1x1:
                return "1:1";
            default:
                return "Unknown";
            }
        }

        std::optional<SceneViewAspectMode> SceneViewAspectFromId(std::string id)
        {
            std::transform(id.begin(), id.end(), id.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            std::replace(id.begin(), id.end(), ':', '_');
            std::replace(id.begin(), id.end(), '.', '_');
            std::replace(id.begin(), id.end(), 'x', '_');

            if (id == "free" || id == "default")
                return SceneViewAspectMode::Free;
            if (id == "16_9" || id == "landscape_16_9")
                return SceneViewAspectMode::Landscape16x9;
            if (id == "9_16" || id == "portrait_9_16" || id == "phone")
                return SceneViewAspectMode::Portrait9x16;
            if (id == "19_5_9" || id == "landscape_19_5_9")
                return SceneViewAspectMode::Landscape19_5x9;
            if (id == "9_19_5" || id == "portrait_9_19_5" || id == "android")
                return SceneViewAspectMode::Portrait9x19_5;
            if (id == "1_1" || id == "square")
                return SceneViewAspectMode::Square1x1;
            return std::nullopt;
        }

        void SetSceneViewAspectMode(SceneViewAspectMode mode)
        {
            auto &settings = Settings::Get<SceneSettings>();
            if (settings.scene_view_aspect_mode == mode)
                return;

            settings.scene_view_aspect_mode = mode;
            if (RendererSystem *rs = GetGlobalSystem<RendererSystem>())
            {
                rs->ResetTAAHistory();
                rs->GetScene().MarkDirty();
                rs->GetGUI().NotifyChange();
            }
        }

        void PersistEditorLayoutSettings();

        bool ApplyEditorStyle(const std::string &styleId, std::string &error, bool persist = true)
        {
            std::string style = NormalizeEditorActionId(styleId);
            if (style.rfind("layout.style.", 0) == 0)
                style = style.substr(13);

            if (style == "classic")
            {
                GUIState::s_guiStyle = GUIStyle::Classic;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else if (style == "dark")
            {
                GUIState::s_guiStyle = GUIStyle::Dark;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else if (style == "light")
            {
                GUIState::s_guiStyle = GUIStyle::Light;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else if (style == "modern")
            {
                GUIState::s_guiStyle = GUIStyle::Modern;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else if (style == "unity")
            {
                GUIState::s_guiStyle = GUIStyle::Unity;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else if (style == "unreal")
            {
                GUIState::s_guiStyle = GUIStyle::Unreal;
                ui::ApplyTheme(GUIState::s_guiStyle);
            }
            else
            {
                error = "unknown style: " + styleId;
                return false;
            }
            if (persist)
                PersistEditorLayoutSettings();
            return true;
        }

        void SetEditorFontScale(float scale, bool persist = true)
        {
            if (!ImGui::GetCurrentContext())
                return;
            ImGui::GetIO().FontGlobalScale = std::clamp(scale, kMinEditorFontScale, kMaxEditorFontScale);
            if (persist)
                PersistEditorLayoutSettings();
        }

        nlohmann::ordered_json LoadEditorConfigObject(const std::filesystem::path &path)
        {
            std::ifstream in(path);
            if (!in)
                return nlohmann::ordered_json::object();
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(in, nullptr, false);
            return j.is_object() ? j : nlohmann::ordered_json::object();
        }

        void PersistEditorLayoutSettings()
        {
            if (!ImGui::GetCurrentContext())
                return;

            const std::filesystem::path path = RuntimeEditorConfigWritePath({});
            nlohmann::ordered_json j = LoadEditorConfigObject(path);
            j[kEditorGuiStyleKey] = StyleId(GUIState::s_guiStyle);
            j[kEditorFontScaleKey] = ImGui::GetIO().FontGlobalScale;

            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path);
            if (!out)
            {
                PE_WARN("[editor_config] Could not write layout settings: %s", path.generic_string().c_str());
                return;
            }
            out << j.dump(2) << '\n';
        }

        void ApplyPersistedEditorLayoutSettings()
        {
            const nlohmann::ordered_json j = LoadEditorConfigObject(RuntimeEditorConfigPath({}));
            if (j.contains(kEditorGuiStyleKey) && j[kEditorGuiStyleKey].is_string())
            {
                std::string error;
                ApplyEditorStyle(j[kEditorGuiStyleKey].get<std::string>(), error, false);
            }
            if (j.contains(kEditorFontScaleKey) && j[kEditorFontScaleKey].is_number())
                SetEditorFontScale(j[kEditorFontScaleKey].get<float>(), false);
            ui::ApplyTheme(GUIState::s_guiStyle);
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

            addIfExists(config.directories, makePath("Phasma/MCP"));
            addIfExists(config.directories, makePath("Phasma/Core"));
            addIfExists(config.directories, makePath("Phasma/Editor"));
            addIfExists(config.directories, makePath("Phasma/Runtime"));
            addIfExists(config.directories, makePath("Phasma/WebGPU"));

            addIfExists(config.skip_directories, makePath("Phasma/MCP/third_party"));
            addIfExists(config.skip_directories, makePath("Phasma/Core/third_party"));
            addIfExists(config.skip_directories, makePath("Phasma/Runtime/third_party"));
            addIfExists(config.skip_directories, makePath("Phasma/WebGPU/WgslBridge/target"));
            addIfExists(config.skip_directories, makePath("Phasma/Editor/Agent"));
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

        void ApplyEditorDockedWindowClass()
        {
            ImGuiWindowClass windowClass;
            windowClass.TabItemFlagsOverrideSet = ImGuiTabItemFlags_NoCloseWithMiddleMouseButton;
            ImGui::SetNextWindowClass(&windowClass);
        }

        void CloseWidgetOnHovered(const char *windowName, bool &open)
        {
            if (!open || !ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                return;

            ImGuiWindow *window = ImGui::FindWindowByName(windowName);
            if (!window || !window->DockIsActive)
                return;

            const ImGuiItemStatusFlags tabStatusFlags = window->DC.DockTabItemStatusFlags;
            const bool tabHovered = (tabStatusFlags & ImGuiItemStatusFlags_HoveredRect) != 0 &&
                                    (tabStatusFlags & ImGuiItemStatusFlags_HoveredWindow) != 0;
            if (tabHovered)
                open = false;
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

        if (m_initialized)
        {
            SaveWindowState();
            PersistEditorLayoutSettings();

            if (!m_iniFilePath.empty() && ImGui::GetCurrentContext())
                ImGui::SaveIniSettingsToDisk(m_iniFilePath.c_str());
        }

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

    void GUI::QueuePreviewImageTransition(Image *image)
    {
        if (!image)
            return;
        for (Image *queued : m_previewImageTransitions)
            if (queued == image)
                return;
        m_previewImageTransitions.push_back(image);
    }

    void *GUI::GetSceneViewPreviewTextureId() const
    {
        return GUIState::s_viewportTextureId;
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
        auto &globalSettings = Settings::Get<SceneSettings>();
        nlohmann::json result;
        result["windows"] = nlohmann::json::array();
        result["actions"] = nlohmann::json::array();
        result["state"] = {
            {"mcp_running", IsMcpServerRunning()},
            {"play_mode", GUIState::s_playMode},
            {"paused", GUIState::s_isPaused},
            {"viewport_floating", GUIState::s_sceneViewFloating},
            {"viewport_aspect", SceneViewAspectId(globalSettings.scene_view_aspect_mode)},
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
        addAction("file.load_model", "Load ModelAsset", "File", "command", !GUIState::s_modelLoading.load(), false, false);
        addAction("file.new_scene", "New Scene", "File", "command", true, false, false);
        addAction("file.load_scene", "Load Scene", "File", "command", true, false, false);
        addAction("file.save_scene", "Save Scene", "File", "command", true, false, false);
        addAction("file.save_scene_as", "Save Scene As", "File", "command", true, false, false);
        addAction("file.reload_module", "Reload Module", "File", "command", true, false, false);
        addAction("editor.module_info", "Editor Module Build (loaded vs on disk)", "Editor", "query", true, false, false);
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
        for (SceneViewAspectMode mode : {SceneViewAspectMode::Free,
                                         SceneViewAspectMode::Landscape16x9,
                                         SceneViewAspectMode::Portrait9x16,
                                         SceneViewAspectMode::Landscape19_5x9,
                                         SceneViewAspectMode::Portrait9x19_5,
                                         SceneViewAspectMode::Square1x1})
        {
            const std::string id = std::string("viewport.aspect.") + SceneViewAspectId(mode);
            addAction(id, SceneViewAspectLabel(mode), "Window", "choice", true, globalSettings.scene_view_aspect_mode == mode, true);
        }

        addAction("gizmo.transform", "Transform Gizmo", "Gizmos", "toggle", true, GUIState::s_useTransformGizmo, true);
        addAction("gizmo.lights", "Light Gizmos", "Gizmos", "toggle", true, GUIState::s_useLightGizmos, true);
        addAction("gizmo.cameras", "Camera Gizmos", "Gizmos", "toggle", true, GUIState::s_useCameraGizmos, true);
        addAction("gizmo.orientation", "Orientation Gizmo", "Gizmos", "toggle", true, GUIState::s_useOrientationGizmo, true);
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

        const bool hasMapPainter = GetWidget<MapPainter>() != nullptr;
        addAction("voxelpainter.layer",
                  "Voxel Map Painter: Select Layer (args: layer 0=surface 1=strata1 2=strata2 3=features "
                  "4=caves 5=scatter 6=splat - 4/5/6 Terrain node)",
                  "VoxelPainter", "command", hasMapPainter, false, false);
        addAction("voxelpainter.stroke",
                  "Voxel Map Painter: Brush Stroke (args: u, v 0..1; optional radius px, strength, lower, "
                  "brush raise|smooth|flatten|set on gray layers or tree|rock|olive|cypress|block|erase on the "
                  "features layer, value = target for set / block id for block; scatter layer: brush = numeric "
                  "kind id 1..N, 0 erases; splat layer: brush = layer index 1=grass 2=rock 3=sand 4=snow, "
                  "0 erases to auto; scatter/splat strokes apply live)",
                  "VoxelPainter", "command", hasMapPainter, false, false);
        addAction("voxelpainter.save", "Voxel Map Painter: Save PNG + Rebuild World (scatter/splat: persists "
                                       "only, already applied live)",
                  "VoxelPainter", "command", hasMapPainter, false, false);

        const bool hasTimeline = GetWidget<AnimationTimeline>() != nullptr;
        addAction("timeline.mode", "Animation Timeline: Set Mode (args: mode dope|graph)", "Timeline", "command", hasTimeline, false, false);
        addAction("timeline.frame", "Animation Timeline: Set Current Frame (args: frame)", "Timeline", "command", hasTimeline, false, false);
        addAction("timeline.bone", "Animation Timeline: Select Bone Channel (args: bone name)", "Timeline", "command", hasTimeline, false, false);
        addAction("timeline.save", "Animation Timeline: Save Clips Into The Model's .pemesh", "Timeline", "command", hasTimeline, false, false);
        const bool hasRig = GetWidget<RigEditor>() != nullptr;
        addAction("rig.state", "Rig Editor: Dump The Rig Document (bones, shapes, selection)", "Rig", "command", hasRig, false, false);
        addAction("rig.preset", "Rig Editor: Build Bones (args: preset auto|humanoid|existing|clear)", "Rig", "command", hasRig, false, false);
        addAction("rig.add", "Rig Editor: Add Bone (args: name, parent, head[3], tail[3], radius_head, radius_tail, rigid)", "Rig", "command", hasRig, false, false);
        addAction("rig.set", "Rig Editor: Edit Bone (args: bone name|index, name, parent, head, tail, radius_head, radius_tail, rigid)", "Rig", "command", hasRig, false, false);
        addAction("rig.remove", "Rig Editor: Remove Bone (args: bone)", "Rig", "command", hasRig, false, false);
        addAction("rig.select", "Rig Editoimage.pngr: Select Bone (args: bone)", "Rig", "command", hasRig, false, false);
        addAction("rig.save", "Rig Editor: Save <model>.rig.json Beside The .pemesh", "Rig", "command", hasRig, false, false);
        addAction("rig.load", "Rig Editor: Load <model>.rig.json", "Rig", "command", hasRig, false, false);
        addAction("rig.shapes", "Rig Editor: Show Influence Shapes (args: show)", "Rig", "command", hasRig, false, false);
        addAction("rig.snap", "Rig Editor: Snapping (args: enabled, mode joints|surface|volume|increment)", "Rig", "command", hasRig, false, false);
        addAction("rig.mirror", "Rig Editor: X-Mirror .L/.R Edits (args: enabled)", "Rig", "command", hasRig, false, false);
        addAction("rig.heat", "Rig Editor: Weight Heat Map On The Mesh (args: mode off|selected|all)", "Rig", "command", hasRig, false, false);
        addAction("rig.undo", "Rig Editor: Undo", "Rig", "command", hasRig, false, false);
        addAction("rig.redo", "Rig Editor: Redo", "Rig", "command", hasRig, false, false);

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

        // Rig Editor programmatic route (the widget parses and applies the args itself).
        if (action.rfind("rig.", 0) == 0)
        {
            auto *rig = GetWidget<RigEditor>();
            if (!rig)
                return R"({"error":"Rig Editor not available"})";
            *rig->GetOpen() = true;
            return rig->HandleAction(action, argsJson);
        }

        // Animation Timeline programmatic route (mode / frame / bone / save) for agents.
        if (action.rfind("timeline.", 0) == 0)
        {
            auto *timeline = GetWidget<AnimationTimeline>();
            if (!timeline)
                return R"({"error":"Animation Timeline not available"})";
            *timeline->GetOpen() = true;
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
            return R"({"error":"unknown timeline action"})";
        }

        // Map Painter programmatic route: lets agents paint voxel terrain maps without mouse input.
        if (action.rfind("voxelpainter.", 0) == 0)
        {
            auto *painter = GetWidget<MapPainter>();
            if (!painter)
                return R"({"error":"Map Painter not available"})";
            if (action == "voxelpainter.layer")
            {
                painter->SetLayer(args.value("layer", 0));
                return ok();
            }
            if (action == "voxelpainter.stroke")
            {
                int brush = -1; // -1 = widget's current brush
                if (args.contains("brush"))
                {
                    if (args["brush"].is_string())
                    {
                        // Gray-layer brushes and Features-layer stamps share the arg; the painter
                        // interprets the index by the active layer.
                        static const std::map<std::string, int> kBrushNames = {
                            {"raise", 0}, {"smooth", 1}, {"flatten", 2}, {"set", 3}, {"tree", 0}, {"rock", 1}, {"olive", 2}, {"cypress", 3}, {"block", 4}, {"erase", 5}};
                        const auto it = kBrushNames.find(args["brush"]);
                        if (it == kBrushNames.end())
                            return R"({"error":"unknown brush - gray: raise|smooth|flatten|set; features: tree|rock|olive|cypress|block|erase; block uses value as the block id"})";
                        brush = it->second;
                    }
                    else
                    {
                        brush = args.value("brush", -1);
                    }
                }
                if (!painter->Stroke(args.value("u", 0.5f), args.value("v", 0.5f), args.value("radius", 0.0f),
                                     args.value("strength", 0.0f), args.value("lower", false), brush,
                                     args.value("value", -1)))
                    return R"({"error":"no map loaded for this layer - set or create it on the Voxel World node"})";
                return ok();
            }
            if (action == "voxelpainter.save")
            {
                if (!painter->Save())
                    return R"({"error":"nothing to save - no Voxel World node or no map loaded"})";
                return ok();
            }
        }

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

        if (action == "editor.module_info")
        {
            s_moduleBuild.nextCheck = 0.0;
            RefreshModuleBuild(ImGui::GetTime());
            return ok({{"loaded", FormatFileTime(s_moduleBuild.loaded)},
                       {"on_disk", FormatFileTime(s_moduleBuild.onDisk)},
                       {"stale", s_moduleBuild.stale},
                       {"module", EditorModulePath().generic_string()}});
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

        if (action.rfind("viewport.aspect.", 0) == 0 || action == "viewport.aspect")
        {
            const std::string value = action == "viewport.aspect" ? args.value("aspect", "") : action.substr(16);
            if (auto mode = SceneViewAspectFromId(value))
            {
                SetSceneViewAspectMode(*mode);
                return ok({{"aspect", SceneViewAspectId(Settings::Get<SceneSettings>().scene_view_aspect_mode)}});
            }
            return nlohmann::json{{"error", "unknown viewport aspect: " + value}}.dump();
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
        if (action == "gizmo.orientation")
            return toggleBool(GUIState::s_useOrientationGizmo);
        if (action == "gizmo.grid")
            return toggleBool(Settings::Get<SceneSettings>().draw_grid);

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
                SetEditorFontScale(args["scale"].get<float>());
                return ok({{"font_scale", ImGui::GetIO().FontGlobalScale}});
            }

            bool presetOk = false;
            const std::string preset = action == "layout.font" ? args.value("preset", "") : action;
            float scale = FontScaleForPreset(preset, presetOk);
            if (!presetOk)
                return nlohmann::json{{"error", "unknown font preset: " + preset}}.dump();

            SetEditorFontScale(scale);
            return ok({{"font_scale", ImGui::GetIO().FontGlobalScale}});
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
        if (ImGui::MenuItem("Load Cooked Mesh...", "Load a .pemesh"))
        {
            if (GUIState::s_modelLoading)
                return;

            auto *fs = GetWidget<FileSelector>();
            if (fs)
            {
                // Loadable meshes are cooked ".pemesh" only; source models are import-only (File > Import).
                std::vector<std::string> exts = {ModelAssetCooked::kExtension};

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
        ui::ItemTooltip("Load a cooked .pemesh asset into the current scene.");
    }

    void GUI::ShowImportModelMenuItem()
    {
        const bool importOpen = ImGui::BeginMenu("Import");
        ui::ItemTooltip("Cook source model assets into engine-ready .pemesh files.");
        if (!importOpen)
            return;

        // Editor-only: import source models (glTF/FBX/OBJ/...) via Assimp and cook GPU-ready geometry
        // to portable ".pemesh". The player (desktop + Android) loads only ".pemesh".
        const bool busy = GUIState::s_modelLoading;

        // 1) Assets: pick one or more source models; cook each to Assets/Models/<stem>/<stem>.pemesh.
        if (ImGui::MenuItem("Assets (cook models)...", busy ? "busy..." : nullptr, false, !busy))
        {
            if (auto *fs = GetWidget<FileSelector>())
            {
                std::vector<std::string> exts;
                for (const char *ext : FileBrowser::s_modelExtensionsVec)
                    exts.push_back(ext);
                fs->OpenMultiSelection([this](const std::vector<std::string> &paths)
                                       { ImportModelsAsync(paths); }, exts);
            }
        }
        ui::ItemTooltip("Select one or more source model files and cook them under Assets/Models.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        // 2) Folder: mirror a folder under Assets/<name>/, cooking every source model to .pemesh in
        //    place and copying all other data verbatim (source model files are replaced, not copied).
        if (ImGui::MenuItem("Folder (mirror + cook)...", busy ? "busy..." : nullptr, false, !busy))
        {
            if (auto *fs = GetWidget<FileSelector>())
                fs->OpenFolderSelection([this](const std::string &folder)
                                        { ImportFolderAsync(folder); });
        }
        ui::ItemTooltip("Mirror a folder into Assets while cooking any source models it contains.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::EndMenu();
    }

    // UTF-8-safe path display. std::filesystem::path::string()/generic_string() THROW on Windows when a
    // path contains characters with no mapping in the active ANSI code page (e.g. "Unicode❤♻Test");
    // u8string() always succeeds. Use this for any path that goes into a log/format string.
    static std::string PathUtf8(const std::filesystem::path &p)
    {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
    }

    // Incremental import: skip a verbatim copy when the destination already mirrors the source
    // (exists, identical size, and not older). Name + size + mtime identity check.
    static bool CopyTargetUpToDate(const std::filesystem::path &src, const std::filesystem::path &dst)
    {
        std::error_code ec;
        if (!std::filesystem::exists(dst, ec))
            return false;
        const auto srcSize = std::filesystem::file_size(src, ec);
        if (ec)
            return false;
        const auto dstSize = std::filesystem::file_size(dst, ec);
        if (ec || srcSize != dstSize)
            return false;
        const auto srcTime = std::filesystem::last_write_time(src, ec);
        if (ec)
            return false;
        const auto dstTime = std::filesystem::last_write_time(dst, ec);
        if (ec)
            return false;
        return dstTime >= srcTime; // dst at least as new as src -> unchanged
    }

    // Incremental import: skip a cook when the cooked ".pemesh" exists and is at least as new as the
    // source model. The .pemesh is derived, so its size can't be compared to the source — date is the
    // signal (re-edit the source and its newer mtime forces a re-cook).
    static bool CookTargetUpToDate(const std::filesystem::path &src, const std::filesystem::path &outPemesh)
    {
        std::error_code ec;
        if (!std::filesystem::exists(outPemesh, ec))
            return false;
        const auto srcTime = std::filesystem::last_write_time(src, ec);
        if (ec)
            return false;
        const auto outTime = std::filesystem::last_write_time(outPemesh, ec);
        if (ec)
            return false;
        return outTime >= srcTime;
    }

    // Path to the PhasmaCook tool, which sits next to the editor executable.
    static std::filesystem::path PhasmaCookExePath()
    {
        const std::filesystem::path exeDir(reinterpret_cast<const char8_t *>(Path::Executable.c_str()));
#if defined(PE_WIN32)
        return exeDir / "PhasmaCook.exe";
#else
        return exeDir / "PhasmaCook";
#endif
    }

    // Launch PhasmaCook (the only Assimp-linking target) with the given args and wait. Synchronous —
    // runs on the import worker thread. Returns true iff PhasmaCook exits 0. Assimp is intentionally
    // absent from the editor process. The cook window is created hidden, so nothing flashes.
    static bool RunPhasmaCookProcess(const std::vector<std::filesystem::path> &args)
    {
        const std::filesystem::path exe = PhasmaCookExePath();
        std::error_code ec;
        if (!std::filesystem::exists(exe, ec))
        {
            PE_WARN("[Import] PhasmaCook tool not found at: %s", PathUtf8(exe).c_str());
            return false;
        }

#if defined(PE_WIN32)
        // Wide, quoted command line so Unicode paths reach PhasmaCook intact.
        std::wstring cmd = L"\"" + exe.wstring() + L"\"";
        for (const std::filesystem::path &arg : args)
            cmd += L" \"" + arg.wstring() + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.wstring().c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            PE_WARN("[Import] Failed to launch PhasmaCook (error %lu)", static_cast<unsigned long>(GetLastError()));
            return false;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
#else
        std::vector<std::string> argStrings;
        argStrings.push_back(exe.string());
        for (const std::filesystem::path &arg : args)
            argStrings.push_back(arg.string());
        std::vector<char *> argv;
        for (std::string &s : argStrings)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        const pid_t pid = fork();
        if (pid < 0)
        {
            PE_WARN("[Import] fork failed launching PhasmaCook");
            return false;
        }
        if (pid == 0)
        {
            execv(argStrings[0].c_str(), argv.data());
            _exit(127);
        }
        int status = 0;
        int rc;
        do
        {
            rc = waitpid(pid, &status, 0);
        }
        while (rc < 0 && errno == EINTR);
        return rc >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    }

    int GUI::CookModelsToPemesh(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &jobs)
    {
        if (jobs.empty())
            return 0;

        std::error_code ec;

        // Snapshot each output's current state — do NOT delete first: a cook that cannot launch, or a
        // model that fails, must never destroy a last-known-good .pemesh. An output counts as cooked
        // only once it is freshly written (newly created, or its write-time advances past the snapshot).
        std::vector<bool> existedBefore(jobs.size());
        std::vector<std::filesystem::file_time_type> beforeTime(jobs.size());
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            std::filesystem::create_directories(jobs[i].second.parent_path(), ec);
            existedBefore[i] = std::filesystem::exists(jobs[i].second, ec);
            if (existedBefore[i])
                beforeTime[i] = std::filesystem::last_write_time(jobs[i].second, ec);
        }
        auto freshlyCooked = [&](size_t i) -> bool
        {
            std::error_code fec;
            if (!std::filesystem::exists(jobs[i].second, fec))
                return false;
            if (!existedBefore[i])
                return true;
            return std::filesystem::last_write_time(jobs[i].second, fec) != beforeTime[i];
        };

        // Cook the whole set in ONE PhasmaCook process via a UTF-8 manifest (one "<src>\t<out>" per
        // line) — a single hidden device bring-up instead of one window/RHI per model.
        static std::atomic<unsigned> s_manifestCounter{0};
        const std::filesystem::path manifest =
            std::filesystem::temp_directory_path(ec) /
            ("phasma_cook_" + std::to_string(s_manifestCounter.fetch_add(1)) + ".txt");
        {
            std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                PE_WARN("[Import] Could not write cook manifest: %s", PathUtf8(manifest).c_str());
                return 0;
            }
            for (const auto &job : jobs)
                out << PathUtf8(job.first) << '\t' << PathUtf8(job.second) << '\n';
        }

        // Drive the real progress bar (replacing the stale "Uploading to GPU"): a helper thread blocks
        // on PhasmaCook while this worker polls how many output files have appeared.
        auto &loading = Settings::Get<SceneSettings>().loading;
        loading.SetName("Cooking models");
        loading.total = static_cast<uint32_t>(jobs.size());
        loading.current = 0;

        std::atomic<bool> finished{false};
        std::thread cookThread([&]()
                               {
            RunPhasmaCookProcess({std::filesystem::path("--batch"), manifest});
            finished.store(true); });
        while (!finished.load())
        {
            uint32_t done = 0;
            for (size_t i = 0; i < jobs.size(); ++i)
                if (freshlyCooked(i))
                    ++done;
            loading.current = done;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        cookThread.join();

        int cooked = 0;
        for (size_t i = 0; i < jobs.size(); ++i)
            if (freshlyCooked(i))
                ++cooked;
        loading.current = static_cast<uint32_t>(cooked);

        // Surface which models failed and why, from PhasmaCook's "<manifest>.failed" sidecar.
        std::filesystem::path failedPath = manifest;
        failedPath += ".failed";
        if (std::ifstream failedIn{failedPath, std::ios::binary})
        {
            std::string line;
            while (std::getline(failedIn, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;
                const size_t tab = line.find('\t');
                const std::string failedSrc = tab == std::string::npos ? line : line.substr(0, tab);
                const std::string reason = tab == std::string::npos ? std::string() : line.substr(tab + 1);
                PE_WARN("[Import] cook failed: %s (%s)", failedSrc.c_str(), reason.c_str());
            }
        }

        std::filesystem::remove(manifest, ec);
        std::filesystem::remove(failedPath, ec);

        // Leave a neutral, non-empty title (ImGui windows need a non-empty id) so the next operation
        // does not inherit a misleading full "Cooking models" bar.
        loading.total = 0;
        loading.current = 0;

        PE_INFO("[Import] Cooked %d / %zu model(s) via PhasmaCook batch", cooked, jobs.size());
        return cooked;
    }

    bool GUI::CookModelToPemesh(const std::filesystem::path &src, const std::filesystem::path &outPemesh)
    {
        return CookModelsToPemesh({{src, outPemesh}}) == 1;
    }

    void GUI::ImportModelsAsync(std::vector<std::string> paths)
    {
        if (paths.empty())
            return;

        GUIState::s_modelLoading = true;
        ThreadPool::GUI.Enqueue([this, paths = std::move(paths)]()
                                {
            try
            {
                int skipped = 0;
                std::vector<std::pair<std::filesystem::path, std::filesystem::path>> jobs;
                for (const std::string &p : paths)
                {
                    // The selector hands back UTF-8; reconstruct the path from UTF-8 (path(std::string)
                    // would mis-decode it as ANSI on Windows for non-ASCII names) and keep the stem as a
                    // path object so we never call the throwing narrow string() conversion.
                    std::filesystem::path src(reinterpret_cast<const char8_t *>(p.c_str()));
                    std::filesystem::path stem = src.stem();

                    // Cooked assets are build artifacts: write under the build-tree Assets dir
                    // (gitignored), where the file browser also shows them. Never the source tree.
                    std::filesystem::path outDir = std::filesystem::path(Path::Assets) / "Models" / stem;
                    std::filesystem::path outPemesh = outDir / stem;
                    outPemesh += ModelAssetCooked::kExtension;
                    if (CookTargetUpToDate(src, outPemesh))
                    {
                        ++skipped; // already cooked and source unchanged
                        continue;
                    }
                    jobs.emplace_back(std::move(src), std::move(outPemesh));
                }
                const int ok = CookModelsToPemesh(jobs); // one PhasmaCook process for the whole set
                PE_INFO("[Import] Cooked %d, skipped %d up-to-date / %zu model(s)", ok, skipped, paths.size());
            }
            catch (const std::exception &e)
            {
                PE_WARN("[Import] Batch import aborted: %s", e.what());
            }
            catch (...)
            {
                PE_WARN("[Import] Batch import aborted by an unknown error");
            }

            // Always clear the busy flag and refresh, even if the batch threw — otherwise every
            // import/load menu item stays disabled for the rest of the session.
            GUIState::s_modelLoading = false;
            QueueMainThreadAction([this]()
                                  {
                if (auto *fb = GetWidget<FileBrowser>())
                    fb->RefreshCache(); }); });
    }

    void GUI::ImportFolderAsync(std::string srcFolder)
    {
        // The selector hands back UTF-8; decode it as UTF-8 (not ANSI) so a folder path with non-ASCII
        // characters resolves correctly on Windows.
        std::filesystem::path src(reinterpret_cast<const char8_t *>(srcFolder.c_str()));
        std::error_code ec;
        if (!std::filesystem::is_directory(src, ec))
        {
            PE_WARN("[Import] Not a folder: %s", srcFolder.c_str());
            return;
        }

        GUIState::s_modelLoading = true;
        ThreadPool::GUI.Enqueue([this, src]()
                                {
            try
            {
                // Show a sensible progress title from the start so the long scan/copy phase doesn't
                // sit under a stale "Uploading to GPU" bar (CookModelsToPemesh drives it after).
                auto &loading = Settings::Get<SceneSettings>().loading;
                loading.SetName("Importing folder");
                loading.total = 0;
                loading.current = 0;

                // Mirror the source tree under Assets/<folderName>/. Source models are cooked to
                // ".pemesh" in place (the cook brings their textures to the same relative paths);
                // every other file is copied verbatim. The source model files are not copied.
                const std::filesystem::path destRoot = std::filesystem::path(Path::Assets) / src.filename();

                // Skip version-control metadata: when the picked folder is a git checkout, ".git" holds
                // gigabytes of objects that are not asset data.
                auto isVcsPath = [](const std::filesystem::path &rel)
                {
                    for (const auto &part : rel)
                        if (part == ".git")
                            return true;
                    return false;
                };

                std::vector<std::filesystem::path> models;
                std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies; // (src, dst)

                std::error_code itEc;
                for (auto it = std::filesystem::recursive_directory_iterator(
                         src, std::filesystem::directory_options::skip_permission_denied, itEc);
                     it != std::filesystem::recursive_directory_iterator(); it.increment(itEc))
                {
                    if (itEc)
                    {
                        itEc.clear();
                        continue;
                    }
                    std::error_code regEc;
                    if (!it->is_regular_file(regEc) || regEc)
                        continue;

                    const std::filesystem::path &p = it->path();
                    std::error_code relEc;
                    std::filesystem::path rel = std::filesystem::relative(p, src, relEc);
                    if (relEc || rel.empty() || isVcsPath(rel))
                        continue;

                    if (FileBrowser::IsSourceModelFile(p))
                        models.push_back(p);
                    else
                        copies.emplace_back(p, destRoot / rel);
                }

                // 1) Copy all non-model data verbatim (mirroring the tree). Skip files already mirrored
                //    (same size + not older) so re-importing the same folder is incremental.
                loading.SetName("Copying files");
                loading.total = static_cast<uint32_t>(copies.size());
                loading.current = 0;
                int copied = 0, copySkipped = 0;
                for (const auto &io : copies)
                {
                    loading.current = static_cast<uint32_t>(copied + copySkipped);
                    if (CopyTargetUpToDate(io.first, io.second))
                    {
                        ++copySkipped;
                        continue;
                    }
                    std::error_code cec;
                    std::filesystem::create_directories(io.second.parent_path(), cec);
                    std::filesystem::copy_file(io.first, io.second,
                                               std::filesystem::copy_options::overwrite_existing, cec);
                    if (cec)
                        PE_WARN("[Import] Copy failed '%s': %s", PathUtf8(io.first.filename()).c_str(),
                                cec.message().c_str());
                    else
                        ++copied;
                }

                // 2) Cook each source model into its mirrored location. Skip ones whose ".pemesh" is
                //    already up to date with the source, then cook the rest in ONE PhasmaCook process.
                int cookSkipped = 0;
                std::vector<std::pair<std::filesystem::path, std::filesystem::path>> cookJobs;
                for (const std::filesystem::path &m : models)
                {
                    std::error_code relEc;
                    std::filesystem::path rel = std::filesystem::relative(m, src, relEc);
                    if (relEc)
                        continue;
                    std::filesystem::path outPemesh =
                        (destRoot / rel).replace_extension(ModelAssetCooked::kExtension);
                    if (CookTargetUpToDate(m, outPemesh))
                    {
                        ++cookSkipped;
                        continue;
                    }
                    cookJobs.emplace_back(m, std::move(outPemesh));
                }
                const int cooked = CookModelsToPemesh(cookJobs);

                PE_INFO("[Import] Folder '%s' -> %s : cooked %d (skipped %d), copied %d (skipped %d)",
                        PathUtf8(src.filename()).c_str(), PathUtf8(destRoot).c_str(),
                        cooked, cookSkipped, copied, copySkipped);
            }
            catch (const std::exception &e)
            {
                PE_WARN("[Import] Folder import aborted: %s", e.what());
            }
            catch (...)
            {
                PE_WARN("[Import] Folder import aborted by an unknown error");
            }

            // Always clear the busy flag and refresh, even if the import threw.
            GUIState::s_modelLoading = false;
            QueueMainThreadAction([this]()
                                  {
                if (auto *fb = GetWidget<FileBrowser>())
                    fb->RefreshCache(); }); });
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
        ui::ItemTooltip("Load a .pescene file, prompting first if the current scene is dirty.");
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
            ui::ItemTooltip("Save the current scene, then continue loading another scene.");
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(80, 0)))
            {
                ImGui::CloseCurrentPopup();
                OpenLoadSceneDialog();
            }
            ui::ItemTooltip("Discard unsaved changes and continue loading another scene.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Cancel loading and keep the current scene open.");
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
            ui::ItemTooltip("Save the current scene, then create a new scene.");
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
            ui::ItemTooltip("Discard unsaved changes and create a new scene.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                ImGui::CloseCurrentPopup();
            ui::ItemTooltip("Cancel creating a new scene.");

            ImGui::EndPopup();
        }
    }

    void GUI::ShowSaveSceneMenuItem()
    {
        if (ImGui::MenuItem("Save Scene...", "Ctrl+S"))
            RequestSaveScene();
        ui::ItemTooltip("Save the current scene, or choose a path if it has not been saved yet.");
    }

    // Interactive Save entry point (Ctrl+S / menu). Routes a first-time save to the
    // Save-As dialog (which already confirms via its own path picker / overwrite
    // prompt); for an already-saved scene it raises a confirm popup before writing,
    // so a stray keystroke can't silently overwrite the file on disk.
    void GUI::RequestSaveScene()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;
        const auto &scenePath = rs->GetScene().GetScenePath();
        if (scenePath.empty())
        {
            ShowSaveSceneMenuItem_Action();
            return;
        }
        m_confirmSavePath = scenePath;
        m_showConfirmSave = true;
    }

    void GUI::DrawConfirmSavePopup()
    {
        if (m_showConfirmSave)
        {
            ImGui::OpenPopup("Save Scene?##confirm");
            m_showConfirmSave = false;
        }

        if (ImGui::BeginPopupModal("Save Scene?##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Overwrite this scene file on disk?");
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextDisabled("%s", m_confirmSavePath.string().c_str());
            ImGui::Dummy(ImVec2(0, 10));

            // Right-align the Save/Cancel pair within the auto-sized popup.
            const float btnW = 100.0f;
            const float pairW = btnW * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > pairW)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - pairW));

            if (ImGui::Button("Save", ImVec2(btnW, 0)))
            {
                auto savePath = m_confirmSavePath;
                ThreadPool::GUI.Enqueue([savePath]()
                                        { pe::SaveScene(savePath); });
                m_confirmSavePath.clear();
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Overwrite the scene file on disk.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)))
            {
                m_confirmSavePath.clear();
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Cancel — do not write the scene file.");
            ImGui::EndPopup();
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
            ui::ItemTooltip("Overwrite the existing scene file.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_pendingSavePath.clear();
                m_showOverwriteConfirmation = false;
            }
            ui::ItemTooltip("Choose a different save path.");
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
        ui::ItemTooltip("Exit the editor, prompting if the scene has unsaved changes.");
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
        const bool runTestsOpen = ImGui::BeginMenu("Run Script Tests");
        ui::ItemTooltip("Run Lua editor script tests and focus the console output.");
        if (!runTestsOpen)
            return;

        auto *scriptSystem = GetGlobalSystem<ScriptSystem>();
        if (!scriptSystem)
        {
            ImGui::MenuItem("Script System Unavailable", nullptr, false, false);
            ui::ItemTooltip("ScriptSystem is not available in this editor session.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::EndMenu();
            return;
        }

        const std::vector<std::string> tests = scriptSystem->GetTestScriptPaths();
        if (ImGui::MenuItem("Run All", nullptr, false, !tests.empty()))
            RunScriptTests(tests, "all script tests");
        ui::ItemTooltip("Run every discovered Lua script test.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Separator();

        if (tests.empty())
        {
            ImGui::MenuItem("No Test Scripts Found", nullptr, false, false);
            ui::ItemTooltip("No scripts were discovered in the test script folders.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::EndMenu();
            return;
        }

        for (const auto &testPath : tests)
        {
            const std::string label = std::filesystem::path(testPath).stem().string();
            if (ImGui::MenuItem(label.c_str()))
                RunScriptTests({testPath}, label);
            ui::ItemTooltip("Run this Lua script test.");
        }

        ImGui::EndMenu();
    }

    void GUI::SaveEditorConfig()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        const auto &scenePath = rs->GetScene().GetScenePath();
        const std::string startupScene = MakePortableStartupScenePath(scenePath);

        // Keep editor restore and runtime/launcher startup selection in sync.
        std::string error;
        if (!WriteEditorStartupScene({}, startupScene, &error) && !error.empty())
            PE_WARN("[Runtime] Could not write editor startup scene: %s", error.c_str());

        error.clear();
        if (!WriteRuntimeStartupScene({}, startupScene, &error) && !error.empty())
            PE_WARN("[Runtime] Could not write startup scene setting: %s", error.c_str());
    }

    void GUI::SaveWindowState()
    {
        const std::string path = Path::Assets + "editor_windows.json";
        nlohmann::json j;
        for (const auto &w : m_menuWindowWidgets)
            j[w->GetName()] = *w->GetOpen();

        std::ofstream f(path);
        if (f)
            f << j.dump(2);
    }

    void GUI::LoadWindowState()
    {
        const std::string path = Path::Assets + "editor_windows.json";
        std::ifstream f(path);
        if (!f)
            return;

        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_discarded())
            return;

        for (auto &w : m_menuWindowWidgets)
        {
            auto it = j.find(w->GetName());
            if (it != j.end() && it->is_boolean())
                *w->GetOpen() = it->get<bool>();
        }
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
        // (see Phasma/Editor/Code/GUI/AI/ removal), so any provider api_key kept living in
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
            ui::ItemTooltip("Save the current scene, then exit the editor.");
            ImGui::SameLine();
            if (ImGui::Button("Discard & Exit", ImVec2(110, 0)))
            {
                SaveEditorConfig();
                EventSystem::PushEvent(EventType::Quit);
                ImGui::CloseCurrentPopup();
            }
            ui::ItemTooltip("Exit the editor without saving scene changes.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                ImGui::CloseCurrentPopup();
            ui::ItemTooltip("Cancel exit and return to the editor.");

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
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kToolbarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kToolbarHeight));
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
            ImGuiViewport *vp = ImGui::GetMainViewport();
            if (vp->WorkSize.x > 0 && vp->WorkSize.y > 0)
            {
                ResetDockspaceLayout(m_dockspaceId);
                m_requestDockReset = false;
            }
        }

        ImGui::End();
    }

    void GUI::ResetDockspaceLayout(uint32_t dockspaceId)
    {
        if (dockspaceId == 0)
            return;

        ImGuiID dockspace = static_cast<ImGuiID>(dockspaceId);
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImVec2 dockPos(viewport->WorkPos.x, viewport->WorkPos.y + kToolbarHeight);
        ImVec2 dockSize(viewport->WorkSize.x, viewport->WorkSize.y - kToolbarHeight);

        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspace, dockPos);
        ImGui::DockBuilderSetNodeSize(dockspace, dockSize);

        constexpr float dockRightFrac = 1.0f / 7.0f;
        constexpr float dockLeftFrac = 1.0f / 6.0f;
        constexpr float dockBottomFrac = 1.0f / 4.5f;

        ImGuiID dockMainId = dockspace;

        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, dockRightFrac, nullptr, &dockMainId);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, dockBottomFrac, nullptr, &dockMainId);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, dockLeftFrac, nullptr, &dockMainId);

        // Central node is now dockMainId - dock the Viewport there
        ImGui::DockBuilderDockWindow("Viewport", dockMainId);

        // Left - Hierarchy (Profiler is floating)
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);

        // Right - Properties
        ImGui::DockBuilderDockWindow("Properties", dockRight);
        ImGui::DockBuilderDockWindow("Camera", dockRight);

        // Bottom - Console and File Browser (Tabbed)
        ImGui::DockBuilderDockWindow("Console", dockBottom);
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
            ui::ItemTooltip("Filter the console to error messages.");
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
            ui::ItemTooltip("Filter the console to warning messages.");
        }

        // Module build stamp — right-aligned, left of MCP; orange when a newer DLL is on disk
        RefreshModuleBuild(ImGui::GetTime());
        if (s_moduleBuild.valid)
        {
            const std::string text = s_moduleBuild.stale
                                         ? "Module " + FormatFileTime(s_moduleBuild.loaded) + "  ->  NEW BUILD " +
                                               FormatFileTime(s_moduleBuild.onDisk) + ": restart the editor"
                                         : "Module " + FormatFileTime(s_moduleBuild.loaded);
            const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float w = ImGui::CalcTextSize(text.c_str()).x + pad + ImGui::CalcTextSize("MCP").x + pad + 8.f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - w);
            ImGui::PushStyleColor(ImGuiCol_Text, s_moduleBuild.stale ? ImVec4(1.f, 0.55f, 0.15f, 1.f) : ImVec4(0.45f, 0.45f, 0.45f, 1.f));
            ImGui::SmallButton(text.c_str());
            ImGui::PopStyleColor();
            ui::ItemTooltip(s_moduleBuild.stale
                                ? "A newer PhasmaEditorModule is on disk. The editor keeps the copy it loaded at start: close it and start it again."
                                : "Build time of the editor module this process loaded. A rebuilt module needs an editor restart.");
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
            ui::ItemTooltip(mcpRunning ? "Disable the running editor MCP server." : "Start the editor MCP server.");
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
            const bool fileMenuOpen = ImGui::BeginMenu("File");
            ui::ItemTooltip("Scene loading, saving, import, and exit commands.");
            if (fileMenuOpen)
            {
                ShowImportModelMenuItem();
                ShowLoadModelMenuItem();
                if (ImGui::MenuItem("New Scene"))
                    NewScene();
                ui::ItemTooltip("Create a new empty scene.");
                ShowLoadSceneMenuItem();
                ShowSaveSceneMenuItem();
                if (ImGui::MenuItem("Save Scene As..."))
                    ShowSaveSceneMenuItem_Action();
                ui::ItemTooltip("Save the current scene to a new .pescene path.");
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Module"))
                    EventSystem::PushEvent(EventType::ReloadModule);
                ui::ItemTooltip("Reload the hot-reload editor module.");
                ImGui::Separator();
                ShowExitMenuItem();
                ImGui::EndMenu();
            }

            const bool editMenuOpen = ImGui::BeginMenu("Edit");
            ui::ItemTooltip("Undo and redo scene edits.");
            if (editMenuOpen)
            {
                auto &undoRedo = UndoRedo::Instance();
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoRedo.CanUndo()))
                {
                    RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        undoRedo.Undo(rs->GetScene());
                }
                ui::ItemTooltip("Undo the most recent scene edit.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoRedo.CanRedo()))
                {
                    RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        undoRedo.Redo(rs->GetScene());
                }
                ui::ItemTooltip("Redo the most recently undone scene edit.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
                ImGui::EndMenu();
            }

            const bool connectionMenuOpen = ImGui::BeginMenu("Connection");
            ui::ItemTooltip("Editor service and indexing commands.");
            if (connectionMenuOpen)
            {
                const bool mcpRunning = IsMcpServerRunning();
                if (ImGui::MenuItem("MCP Server", nullptr, mcpRunning))
                    SetMcpServerEnabled(!mcpRunning);
                ui::ItemTooltip("Toggle the editor MCP server.");
                if (ImGui::MenuItem("Index Codebase"))
                    StartCodebaseIndexing();
                ui::ItemTooltip("Start or refresh the editor codebase index.");
                ImGui::EndMenu();
            }

            const bool windowMenuOpen = ImGui::BeginMenu("Window");
            ui::ItemTooltip("Show, hide, and configure editor windows.");
            if (windowMenuOpen)
            {
                for (auto &widget : m_menuWindowWidgets)
                {
                    ImGui::MenuItem(widget->GetName().c_str(), nullptr, widget->GetOpen());
                    ui::ItemTooltip("Show or hide this editor panel.");
                }

                const bool viewportMenuOpen = ImGui::BeginMenu("Viewport");
                ui::ItemTooltip("Configure viewport visibility and aspect behavior.");
                if (viewportMenuOpen)
                {
                    auto &gSettings = Settings::Get<SceneSettings>();
                    if (auto *sv = GetWidget<SceneView>())
                    {
                        ImGui::MenuItem("Enabled", nullptr, sv->GetOpen());
                        ui::ItemTooltip("Show or hide the docked viewport.");
                    }

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
                    ui::ItemTooltip("Move the viewport into its own floating window.");
                    if (ImGui::MenuItem("Redock", nullptr, false, GUIState::s_sceneViewFloating))
                    {
                        GUIState::s_sceneViewFloating = false;
                        GUIState::s_sceneViewRedockQueued = true;
                    }
                    ui::ItemTooltip("Return the floating viewport to the docked layout.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
                    const bool aspectMenuOpen = ImGui::BeginMenu("Aspect Ratio");
                    ui::ItemTooltip("Constrain the viewport render image to a fixed aspect ratio.");
                    if (aspectMenuOpen)
                    {
                        for (SceneViewAspectMode mode : {SceneViewAspectMode::Free,
                                                         SceneViewAspectMode::Landscape16x9,
                                                         SceneViewAspectMode::Portrait9x16,
                                                         SceneViewAspectMode::Landscape19_5x9,
                                                         SceneViewAspectMode::Portrait9x19_5,
                                                         SceneViewAspectMode::Square1x1})
                        {
                            if (ImGui::MenuItem(SceneViewAspectLabel(mode),
                                                nullptr,
                                                gSettings.scene_view_aspect_mode == mode))
                            {
                                SetSceneViewAspectMode(mode);
                            }
                            ui::ItemTooltip("Use this viewport aspect ratio.");
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            const bool gizmosMenuOpen = ImGui::BeginMenu("Gizmos");
            ui::ItemTooltip("Toggle viewport helper gizmos.");
            if (gizmosMenuOpen)
            {
                auto &gSettings = Settings::Get<SceneSettings>();
                ImGui::MenuItem("Transform", nullptr, &GUIState::s_useTransformGizmo);
                ui::ItemTooltip("Show the transform gizmo for selected nodes.");
                ImGui::MenuItem("Lights", nullptr, &GUIState::s_useLightGizmos);
                ui::ItemTooltip("Show editor light gizmos in the viewport.");
                ImGui::MenuItem("Cameras", nullptr, &GUIState::s_useCameraGizmos);
                ui::ItemTooltip("Show editor camera gizmos in the viewport.");
                ImGui::MenuItem("Orientation", nullptr, &GUIState::s_useOrientationGizmo);
                ui::ItemTooltip("Show the viewport orientation gizmo.");
                ImGui::MenuItem("Grid", nullptr, &gSettings.draw_grid);
                ui::ItemTooltip("Show or hide the editor grid.");
                ImGui::EndMenu();
            }

            const bool renderMenuOpen = ImGui::BeginMenu("Render");
            ui::ItemTooltip("Global rendering toggles.");
            if (renderMenuOpen)
            {
                auto &gSettings = Settings::Get<SceneSettings>();
                ImGui::MenuItem("Mesh LOD", nullptr, &gSettings.lod_enabled);
                ui::ItemTooltip("Master switch for discrete mesh level-of-detail. Per-mesh LOD settings live "
                                "in the Mesh Component; the distance/levels live in Scene Settings.");
                ImGui::EndMenu();
            }

            const bool layoutMenuOpen = ImGui::BeginMenu("Layout");
            ui::ItemTooltip("Adjust editor appearance, scale, and layout.");
            if (layoutMenuOpen)
            {
                const bool styleMenuOpen = ImGui::BeginMenu("Style");
                ui::ItemTooltip("Choose the editor color theme.");
                if (styleMenuOpen)
                {
                    bool isClassic = GUIState::s_guiStyle == GUIStyle::Classic;
                    bool isModern = GUIState::s_guiStyle == GUIStyle::Modern;
                    bool isDark = GUIState::s_guiStyle == GUIStyle::Dark;
                    bool isLight = GUIState::s_guiStyle == GUIStyle::Light;
                    bool isUnity = GUIState::s_guiStyle == GUIStyle::Unity;
                    bool isUnreal = GUIState::s_guiStyle == GUIStyle::Unreal;

                    std::string styleError;
                    if (ImGui::MenuItem("Classic", nullptr, isClassic))
                        ApplyEditorStyle("classic", styleError);
                    ui::ItemTooltip("Use the classic editor color theme.");
                    if (ImGui::MenuItem("Dark", nullptr, isDark))
                        ApplyEditorStyle("dark", styleError);
                    ui::ItemTooltip("Use the dark editor color theme.");
                    if (ImGui::MenuItem("Light", nullptr, isLight))
                        ApplyEditorStyle("light", styleError);
                    ui::ItemTooltip("Use the light editor color theme.");
                    if (ImGui::MenuItem("Modern", nullptr, isModern))
                        ApplyEditorStyle("modern", styleError);
                    ui::ItemTooltip("Use the modern editor color theme.");
                    if (ImGui::MenuItem("Unity", nullptr, isUnity))
                        ApplyEditorStyle("unity", styleError);
                    ui::ItemTooltip("Use the Unity-inspired editor theme.");
                    if (ImGui::MenuItem("Unreal", nullptr, isUnreal))
                        ApplyEditorStyle("unreal", styleError);
                    ui::ItemTooltip("Use the Unreal-inspired editor theme.");
                    ImGui::EndMenu();
                }
                const bool fontSizeMenuOpen = ImGui::BeginMenu("Font Size");
                ui::ItemTooltip("Choose the global editor font scale.");
                if (fontSizeMenuOpen)
                {
                    ImGuiIO &io = ImGui::GetIO();
                    float scale = io.FontGlobalScale;

                    if (ImGui::MenuItem("Small", nullptr, scale < 0.95f))
                        SetEditorFontScale(0.85f);
                    ui::ItemTooltip("Use a compact editor font scale.");
                    if (ImGui::MenuItem("Medium", nullptr, scale >= 0.95f && scale < 1.15f))
                        SetEditorFontScale(1.0f);
                    ui::ItemTooltip("Use the default editor font scale.");
                    if (ImGui::MenuItem("Large", nullptr, scale >= 1.15f && scale < 1.35f))
                        SetEditorFontScale(1.25f);
                    ui::ItemTooltip("Use a larger editor font scale.");
                    if (ImGui::MenuItem("Extra Large", nullptr, scale >= 1.35f))
                        SetEditorFontScale(1.5f);
                    ui::ItemTooltip("Use the largest editor font scale.");
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset to Default Layout", "Ctrl+Shift+L"))
                    m_requestDockReset = true;
                ui::ItemTooltip("Reset all editor panels to the default dock layout.");
                ImGui::EndMenu();
            }

            const bool helpMenuOpen = ImGui::BeginMenu("Help");
            ui::ItemTooltip("Testing and ImGui diagnostic tools.");
            if (helpMenuOpen)
            {
                ShowRunScriptTestsMenu();
                ImGui::Separator();
                ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_show_demo_window);
                ui::ItemTooltip("Show the Dear ImGui demo window.");
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

        Path::Init();
        m_iniFilePath = Path::Assets + "imgui.ini";
        m_hasIniFile = std::filesystem::exists(m_iniFilePath);

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
        io.IniFilename = nullptr;
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
            std::string iconFontPath = Path::EditorAssets + "Fonts/fa-solid-900.ttf";
            std::string interFontPath = Path::EditorAssets + "Fonts/Inter-Regular.ttf";
            std::string robotoFontPath = Path::EditorAssets + "Fonts/Roboto-Regular.ttf";
            // std::string sourceSansFontPath = Path::EditorAssets + "Fonts/SourceSans3-Regular.ttf";
            std::string openSansFontPath = Path::EditorAssets + "Fonts/OpenSans-Regular.ttf";
            // std::string latoFontPath = Path::EditorAssets + "Fonts/Lato-Regular.ttf";
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
            std::string fallbackFontPath = Path::EditorAssets + "Fonts/DejaVuSans.ttf";

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
            ImVector<ImFont *> &fonts = io.Fonts->Fonts;
            int idx = 0;
            auto nextFont = [&](ImFont *fallback) -> ImFont *
            { return idx < fonts.Size ? fonts[idx++] : fallback; };

            const std::string fontDir = Path::EditorAssets + "Fonts/";
            GUIState::s_fontClassic = nextFont(fonts[0]); // AddFontDefault() is always first
            GUIState::s_fontUnity = std::filesystem::exists(fontDir + "Inter-Regular.ttf")
                                        ? nextFont(GUIState::s_fontClassic)
                                        : GUIState::s_fontClassic;
            GUIState::s_fontUnreal = std::filesystem::exists(fontDir + "Roboto-Regular.ttf")
                                         ? nextFont(GUIState::s_fontClassic)
                                         : GUIState::s_fontClassic;
            GUIState::s_fontLight = std::filesystem::exists(fontDir + "OpenSans-Regular.ttf")
                                        ? nextFont(GUIState::s_fontClassic)
                                        : GUIState::s_fontClassic;
            GUIState::s_fontDark = GUIState::s_fontLight;
            GUIState::s_fontModern = GUIState::s_fontLight;
        }

        GUIBackend::CreateFontsTexture();

        ApplyPersistedEditorLayoutSettings();

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
        auto prefabViewer = std::make_shared<PrefabViewer>();
        auto profiler = std::make_shared<ProfilerWidget>();
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
        auto scriptEditor = std::make_shared<ScriptEditor>();
        auto shaderEditor = std::make_shared<ShaderEditor>();
        auto spriteEditor = std::make_shared<SpriteEditor>();
        auto mapPainter = std::make_shared<MapPainter>();
        auto terrainBrush = std::make_shared<TerrainBrush>();
        auto runtimeUiPalette = std::make_shared<RuntimeUiPalette>();
        auto sceneScripts = std::make_shared<SceneScripts>();
        auto animTimeline = std::make_shared<AnimationTimeline>();
        auto rigEditor = std::make_shared<RigEditor>();
#ifdef PE_PHYSICS
        auto physicsWidget = std::make_shared<PhysicsWidget>();
#endif
#ifdef PE_PHYSICS2D
        auto physics2DWidget = std::make_shared<Physics2DWidget>();
#endif
#ifdef PE_AUDIO
        auto audioWidget = std::make_shared<AudioWidget>();
#endif
        // Console added early to potentially influence tab ordering (Leftmost)
        m_widgets = {
            console,
            properties,
            prefabViewer,
            profiler,
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
            scriptEditor,
            shaderEditor,
            spriteEditor,
            mapPainter,
            terrainBrush,
            runtimeUiPalette,
            sceneScripts,
            animTimeline,
            rigEditor,
#ifdef PE_PHYSICS
            physicsWidget,
#endif
#ifdef PE_PHYSICS2D
            physics2DWidget,
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
                               prefabViewer,
                               fileBrowser,
                               hierarchy,
                               particles,
                               scriptEditor,
                               shaderEditor,
                               spriteEditor,
                               mapPainter,
                               terrainBrush,
                               runtimeUiPalette,
                               sceneScripts,
                               animTimeline,
                               rigEditor};
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
            ImGui::LoadIniSettingsFromDisk(m_iniFilePath.c_str());
        else
            m_requestDockReset = true;

        LoadWindowState();

        ImGui::GetIO().IniFilename = m_iniFilePath.c_str();

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

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();
        m_attachment->image = displayRT;

        if (RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi())
            runtimeUi->Render(cmd, displayRT);

        if (!m_render || ImGui::GetDrawData()->TotalVtxCount <= 0)
            return;

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
            sceneViewBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(sceneViewBarrier);
        }

        // Live render targets that widgets (e.g. profiler thumbnails) sample this frame must be in
        // SHADER_READ_ONLY before the GUI pass draws them. The render graph re-transitions them next
        // frame when their producing pass runs. (displayRT is never queued here -- it is the GUI
        // pass's own color attachment, so its preview goes through the scene-view copy instead.)
        for (Image *preview : m_previewImageTransitions)
        {
            ImageBarrierInfo previewBarrier{};
            previewBarrier.image = preview;
            previewBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            previewBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            previewBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(previewBarrier);
        }
        m_previewImageTransitions.clear();

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

        if (!GUIState::s_playMode && !m_playModeSnapshot.empty())
            Stop();
        else if (GUIState::s_playMode && !m_restoreRenderAfterPlay)
        {
            m_prePlayRender = m_render;
            m_restoreRenderAfterPlay = true;
            m_render = false;
        }
        else if (!GUIState::s_playMode && m_restoreRenderAfterPlay && m_playModeSnapshot.empty())
        {
            m_render = m_prePlayRender;
            m_restoreRenderAfterPlay = false;
        }
        if (GUIState::s_playMode)
            m_render = false;

        // Runtime UI input must not use stale Viewport coordinates when the editor GUI is hidden.
        GUIState::s_sceneViewImageRectValid = false;
        GUIState::s_sceneViewFocused = false;

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
            if (io.KeyCtrl)
            {
                if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
                {
                    if (undoRedoRS && undoRedo.CanUndo())
                        undoRedo.Undo(undoRedoRS->GetScene());
                }
                if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    if (undoRedoRS && undoRedo.CanRedo())
                        undoRedo.Redo(undoRedoRS->GetScene());
                }
                if (!io.WantTextInput && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false))
                    m_requestDockReset = true;

                // Ctrl+S - save scene (asks for confirmation before overwriting)
                if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                {
                    RequestSaveScene();
                }
            }

            // Delete key - remove selected entity
            if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            {
                auto &selection = SelectionManager::Instance();
                if (selection.HasSelection())
                {
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
                                    if (undoRedoRS)
                                        undoRedo.RecordSnapshot(undoRedoRS->GetScene(), "Deleted Node");
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
                                    if (undoRedoRS)
                                        undoRedo.RecordSnapshot(undoRedoRS->GetScene(), "Deleted Emitter");
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
            DrawConfirmSavePopup();
            Toolbar();
            BuildDockspace();
        }

        if (GUIState::s_playMode && !m_render)
        {
            if (currentFont)
                ImGui::PopFont();
            return;
        }

        if (m_show_demo_window)
        {
            ApplyEditorDockedWindowClass();
            ImGui::ShowDemoWindow(&m_show_demo_window);
            CloseWidgetOnHovered("Dear ImGui Demo", m_show_demo_window);
        }

        {
            PE_PROFILE_SCOPE("Widgets");
            for (auto &widget : m_widgets)
            {
                if (widget->IsOpen())
                {
                    ApplyEditorDockedWindowClass();
                    widget->Update();
                    CloseWidgetOnHovered(widget->GetName().c_str(), *widget->GetOpen());
                }
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
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kToolbarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Toolbar", nullptr, windowFlags);
        ImGui::PopStyleVar();

        float buttonSize = 25.0f;
        float centerX = ImGui::GetWindowWidth() * 0.5f;
        float centerY = (kToolbarHeight - buttonSize) * 0.5f;
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
            ui::ItemTooltip("Undo the most recent scene edit.");
            if (!canUndo)
                ImGui::PopStyleColor();

            // Undo history arrow
            ImGui::SameLine(0, 1);
            if (!canUndo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::ArrowButton("##undoArrow", ImGuiDir_Down) && canUndo)
                ImGui::OpenPopup("##UndoHistory");
            ui::ItemTooltip("Open undo history.");
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
                        ui::ItemTooltip("Undo back to this recorded scene state.");
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
            ui::ItemTooltip("Redo the most recently undone scene edit.");
            if (!canRedo)
                ImGui::PopStyleColor();

            // Redo history arrow
            ImGui::SameLine(0, 1);
            if (!canRedo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::ArrowButton("##redoArrow", ImGuiDir_Down) && canRedo)
                ImGui::OpenPopup("##RedoHistory");
            ui::ItemTooltip("Open redo history.");
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
                        ui::ItemTooltip("Redo forward to this recorded scene state.");
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
            ui::ItemTooltip("Stop play mode and restore the editor scene.");

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
            ui::ItemTooltip(GUIState::s_isPaused ? "Resume play mode simulation." : "Pause play mode simulation.");
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
            ui::ItemTooltip("Enter play mode from the current scene.");
        }

        ImGui::PopStyleVar();    // Pop FramePadding
        ImGui::PopStyleColor(3); // Pop button colors
        ImGui::End();
    }

    void GUI::Play()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs && !GUIState::s_playMode)
        {
            rs->WaitAllFramesCommands();
            m_playModeSnapshot = rs->GetScene().TakeSnapshot();
            // Remember which scene we entered play from (and its saved/dirty state).
            // Play may scene.load() elsewhere (a game's ESC->menu); on Stop we revert
            // geometry AND this identity so the editor doesn't end up pointed at — and
            // an accidental Ctrl+S doesn't overwrite — the wrong scene file.
            m_prePlayScenePath = rs->GetScene().GetScenePath();
            m_prePlayDirty = rs->GetScene().IsDirty();
            m_prePlayRender = m_render;
            m_restoreRenderAfterPlay = true;
            m_render = false;
            GUIState::s_isPaused = false;
            SetRuntimePlaySessionPaused(false);
            SetScriptPlayMode(true);
            StartRuntimePlaySession(rs->GetScene());
        }
    }

    void GUI::Stop()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            StopRuntimePlaySession();
            SetScriptPlayMode(false);
            if (m_restoreRenderAfterPlay)
            {
                m_render = m_prePlayRender;
                m_restoreRenderAfterPlay = false;
            }
            if (!m_playModeSnapshot.empty())
            {
                rs->WaitAllFramesCommands();
                rs->GetScene().RestoreSnapshot(m_playModeSnapshot);
                m_playModeSnapshot.clear();
                // RestoreSnapshot only reverts geometry; restore the scene identity we
                // entered play with so the title/save target points back at the right
                // file even if play mode navigated away via scene.load().
                rs->GetScene().SetScenePath(m_prePlayScenePath);
                if (m_prePlayDirty)
                    rs->GetScene().MarkDirty();
                else
                    rs->GetScene().ClearDirty();
            }
            m_prePlayScenePath.clear();
            m_prePlayDirty = false;
            ClearRuntimeAnimationState();
            UndoRedo::Instance().Clear();
        }
    }
} // namespace pe
