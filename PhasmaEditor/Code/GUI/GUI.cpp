#include "GUI.h"
#include "Agent/EditorToolCatalog.h"
#include "Agent/EditorToolServer.h"
#include "Agent/EditorToolRuntime.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "GUIState.h"
#include "Helpers.h"
#include "Particles/ParticleManager.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "IconsFontAwesome.h"
#include "RenderPasses/LightPass.h"
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
#include "Widgets/Metrics.h"
#include "Widgets/Models.h"
#include "Widgets/Particles.h"
#include "Widgets/Properties.h"
#include "Widgets/SceneView.h"
#include "PhasmaAgent/CodebaseIndexer.h"
#include "PhasmaAgent/EmbeddingUtils.h"
#include "Widgets/TransformWidget.h"
#include "UndoRedo.h"
#include "Script/ScriptSystem.h"
#include <nlohmann/json.hpp>
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    namespace
    {
        void WriteAgentConfigFile(const std::string &path, const nlohmann::json &j)
        {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());

            auto appendRemainingKeys = [](nlohmann::ordered_json &dst,
                                          const nlohmann::json &src,
                                          const std::vector<std::string> &knownKeys)
            {
                for (auto it = src.begin(); it != src.end(); ++it)
                {
                    if (std::find(knownKeys.begin(), knownKeys.end(), it.key()) != knownKeys.end())
                        continue;
                    dst[it.key()] = it.value();
                }
            };

            nlohmann::ordered_json ordered = nlohmann::ordered_json::object();
            if (j.is_object())
            {
                if (j.contains("mcp"))
                    ordered["mcp"] = j["mcp"];

                if (j.contains("embeddings") && j["embeddings"].is_object())
                {
                    nlohmann::ordered_json embeddings = nlohmann::ordered_json::object();
                    const auto &srcEmbeddings = j["embeddings"];

                    if (srcEmbeddings.contains("enabled"))
                        embeddings["enabled"] = srcEmbeddings["enabled"];

                    if (srcEmbeddings.contains("provider"))
                        embeddings["provider"] = srcEmbeddings["provider"];
                    if (srcEmbeddings.contains("model"))
                        embeddings["model"] = srcEmbeddings["model"];

                    if (srcEmbeddings.contains("indexing") && srcEmbeddings["indexing"].is_object())
                    {
                        nlohmann::ordered_json indexing = nlohmann::ordered_json::object();
                        const auto &srcIndexing = srcEmbeddings["indexing"];

                        if (srcIndexing.contains("directories"))
                            indexing["directories"] = srcIndexing["directories"];
                        if (srcIndexing.contains("include_files"))
                            indexing["include_files"] = srcIndexing["include_files"];
                        if (srcIndexing.contains("skip_directories"))
                            indexing["skip_directories"] = srcIndexing["skip_directories"];
                        if (srcIndexing.contains("skip_extensions"))
                            indexing["skip_extensions"] = srcIndexing["skip_extensions"];
                        if (srcIndexing.contains("skip_files"))
                            indexing["skip_files"] = srcIndexing["skip_files"];
                        if (srcIndexing.contains("skip_regex"))
                            indexing["skip_regex"] = srcIndexing["skip_regex"];

                        appendRemainingKeys(indexing, srcIndexing,
                                            {"directories", "include_files", "skip_directories", "skip_extensions", "skip_files", "skip_regex"});
                        embeddings["indexing"] = std::move(indexing);
                    }
                    else if (srcEmbeddings.contains("indexing"))
                    {
                        embeddings["indexing"] = srcEmbeddings["indexing"];
                    }

                    appendRemainingKeys(embeddings, srcEmbeddings, {"enabled", "provider", "model", "indexing"});
                    ordered["embeddings"] = std::move(embeddings);
                }
                else if (j.contains("embeddings"))
                {
                    ordered["embeddings"] = j["embeddings"];
                }

                for (auto it = j.begin(); it != j.end(); ++it)
                {
                    if (it.key() == "mcp" || it.key() == "embeddings")
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

        std::string BuildEditorCodebaseStorePath()
        {
            return Path::Assets + "Agent/codebase_mcp.bin";
        }

        void ApplyDefaultCodebaseIndexingConfig(const std::filesystem::path &repoRoot,
                                                pagent::CodebaseIndexingConfig &config)
        {
            auto makePath = [&](const std::string &relative)
            { return (repoRoot / relative).string(); };

            auto addIfExists = [&](std::vector<std::string> &values, const std::string &path)
            {
                if (std::filesystem::exists(path))
                    values.push_back(path);
            };

            addIfExists(config.directories, makePath("PhasmaAgent"));
            addIfExists(config.directories, makePath("PhasmaCore"));
            addIfExists(config.directories, makePath("PhasmaEditor"));

            addIfExists(config.include_files, makePath("PhasmaEditor/Assets/Agent/START.md"));

            addIfExists(config.skip_directories, makePath("PhasmaAgent/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaCore/third_party"));
            addIfExists(config.skip_directories, makePath("PhasmaEditor/third_party"));
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
            pagent::CodebaseIndexingConfig config;
            ApplyDefaultCodebaseIndexingConfig(repoRoot, config);

            return nlohmann::json{
                {"mcp", false},
                {"embeddings",
                 {
                     {"enabled", true},
                     {"provider", ""},
                     {"model", ""},
                     {"indexing",
                      {
                          {"directories", config.directories},
                          {"include_files", config.include_files},
                          {"skip_directories", config.skip_directories},
                          {"skip_files", config.skip_files},
                          {"skip_extensions", config.skip_extensions},
                          {"skip_regex", config.skip_regex},
                      }},
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
        *m_codebaseAlive = false;
        CancelCodebaseIndexing();
        if (m_indexThread.joinable())
            m_indexThread.join();

        if (GUIState::s_viewportTextureId)
        {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)GUIState::s_viewportTextureId);
            GUIState::s_viewportTextureId = nullptr;
        }

        m_menuWindowWidgets.clear();
        m_widgets.clear();
        m_editorToolServer.reset();
        if (m_agentScriptSystem)
            m_agentScriptSystem->Destroy();
        m_editorToolRuntime.reset();
        m_agentScriptSystem.reset();

        Image::Destroy(GUIState::s_sceneViewImage);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    bool GUI::IsMcpServerRunning() const
    {
        return m_editorToolServer && m_editorToolServer->IsRunning();
    }

    bool GUI::IsRagEnabled() const
    {
        return m_ragRequestedEnabled && IsMcpServerRunning();
    }

    void GUI::EnsureCodebaseStoreLoaded()
    {
        if (m_codebaseStoreLoadRequested || m_codebaseStorePath.empty() || !std::filesystem::exists(m_codebaseStorePath))
            return;

        m_codebaseStoreLoadRequested = true;
        m_codebase.LoadStoreAsync(
            m_codebaseStorePath,
            m_codebaseAlive,
            [this]()
            {
                QueueMainThreadAction([this]()
                                      { CheckRagStatus(); });
            });
    }

    void GUI::SetMcpServerEnabled(bool enabled)
    {
        const bool running = IsMcpServerRunning();
        if (enabled == running || !m_editorToolServer)
            return;

        if (enabled)
        {
            m_editorToolServer->Start();
            EnsureCodebaseStoreLoaded();
            if (m_ragRequestedEnabled)
                CheckRagStatus();
            return;
        }

        CancelCodebaseIndexing();
        m_editorToolServer->Stop();
    }

    void GUI::EnableRag()
    {
        m_ragRequestedEnabled = true;
        CheckRagStatus();
    }

    void GUI::DisableRag()
    {
        CancelCodebaseIndexing();
        m_ragRequestedEnabled = false;
    }

    void GUI::CheckRagStatus()
    {
        if (!IsRagEnabled())
            return;

        m_codebase.EnsureStores();

        const auto status = m_codebase.GetStatus();
        if (status.loading)
        {
            PE_INFO("[RAG] Check deferred until the saved index finishes loading");
            return;
        }
        if (status.checking)
            return; // CheckStatusAsync would no-op anyway; skip the redundant log

        m_codebase.MarkStatusDirty();
        m_codebase.CheckStatusAsync(m_codebaseAlive);
        PE_INFO("[RAG] Check started");
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
        if (fs)
        {
            std::vector<std::string> exts = {};
            fs->OpenSelection([](const std::string &path)
                              {
                UndoRedo::Instance().Clear();
                auto loadAsync = [path]()
                {
                    auto* rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        rs->GetScene().LoadScene(path);
                };
                ThreadPool::GUI.Enqueue(loadAsync); return true; }, exts);
        }
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
                        scene.SaveScene(scene.GetScenePath());
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
            rs->GetScene().NewScene();
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
                        scene.SaveScene(scene.GetScenePath());
                    else
                        ShowSaveSceneMenuItem_Action();
                    scene.NewScene();
                    UndoRedo::Instance().Clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(80, 0)))
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                    rs->GetScene().NewScene();
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
                rs->GetScene().SaveScene(rs->GetScene().GetScenePath());
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
                    auto* rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                    {
                        rs->GetScene().SaveScene(savePath);
                        rs->GetScene().ClearDirty();
                    }
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
                    auto *rs = GetGlobalSystem<RendererSystem>();
                    if (rs)
                        rs->GetScene().SaveScene(savePath);
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

    static constexpr const char *kEditorConfigPath = "Assets/editor_config.json";

    void GUI::SaveEditorConfig()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        nlohmann::json j;
        const auto &scenePath = rs->GetScene().GetScenePath();
        j["last_scene"] = scenePath.empty() ? "" : scenePath.generic_string();

        std::ofstream f(kEditorConfigPath);
        if (f)
            f << j.dump(2) << "\n";
    }

    void GUI::LoadAgentConfig()
    {
        const std::string configPath = Path::Assets + "Agent/agent_config.json";
        const auto repoRoot = GetEditorRepoRootFromAssets();
        const nlohmann::json defaultConfig = BuildDefaultAgentConfig(repoRoot);

        m_mcpStartEnabled = false;
        m_ragRequestedEnabled = true;
        m_embeddingProviderKind = -1;
        m_embeddingModel.clear();
        m_codebase.SetEmbeddingProvider(nullptr);

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
            PE_WARN("[RAG] Failed to parse agent_config.json — leaving file unchanged and using defaults for this run");
            ApplyDefaultCodebaseIndexingConfig(repoRoot, m_codebase.MutableIndexingConfig());
            return;
        }

        if (MergeJsonDefaults(j, defaultConfig))
        {
            WriteAgentConfigFile(configPath, j);
            PE_INFO("[MCP] Filled missing defaults in agent_config.json");
        }

        m_mcpStartEnabled = j.value("mcp", false);

        if (!j.contains("embeddings") || !j["embeddings"].is_object())
        {
            ApplyDefaultCodebaseIndexingConfig(repoRoot, m_codebase.MutableIndexingConfig());
            return;
        }

        const auto &emb = j["embeddings"];
        m_ragRequestedEnabled = emb.value("enabled", true);

        // Indexing config — always start from defaults so a partial JSON doesn't leave skip lists empty
        auto &config = m_codebase.MutableIndexingConfig();
        ApplyDefaultCodebaseIndexingConfig(repoRoot, config);
        if (emb.contains("indexing") && emb["indexing"].is_object())
        {
            const auto &idx = emb["indexing"];

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

        // Embedding provider
        const std::string providerStr = emb.value("provider", "");
        const std::string model = emb.value("model", "");
        if (!providerStr.empty() && !model.empty())
        {
            pagent::EmbeddingProviderKind kind;
            if (providerStr == "Ollama")
                kind = pagent::EmbeddingProviderKind::Ollama;
            else if (providerStr == "Google" || providerStr == "Gemini")
                kind = pagent::EmbeddingProviderKind::Google;
            else if (providerStr == "OpenAI")
                kind = pagent::EmbeddingProviderKind::OpenAI;
            else if (providerStr == "Voyage")
                kind = pagent::EmbeddingProviderKind::Voyage;
            else
            {
                PE_WARN("[RAG] Unknown embedding provider '%s'", providerStr.c_str());
                return;
            }

            m_embeddingProviderKind = static_cast<int>(kind);
            m_embeddingModel = model;

            auto provider = pagent::CreateEmbeddingProvider(kind, model);
            if (provider)
            {
                m_codebase.SetEmbeddingProvider(std::move(provider));
                PE_INFO("[RAG] Embedding: %s / %s", providerStr.c_str(), model.c_str());
            }
            else
            {
                PE_WARN("[RAG] Could not create '%s' provider — check API key env var", providerStr.c_str());
            }
        }

        m_codebase.MarkStatusDirty();
        PE_INFO("[MCP] Startup: %s", m_mcpStartEnabled ? "enabled" : "disabled");
        PE_INFO("[RAG] Config loaded from agent_config.json");
    }

    void GUI::LoadEditorConfig()
    {
        std::ifstream f(kEditorConfigPath);
        if (!f)
            return;

        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(f);
        }
        catch (...)
        {
            return;
        }

        std::string lastScene = j.value("last_scene", "");
        if (lastScene.empty())
            return;

        if (!std::filesystem::exists(lastScene))
        {
            Log::Warn("Last scene not found, clearing: " + lastScene);
            j["last_scene"] = "";
            std::ofstream fw(kEditorConfigPath);
            if (fw)
                fw << j.dump(2) << "\n";
            return;
        }

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            rs->GetScene().LoadScene(lastScene);
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
                        scene.SaveScene(scene.GetScenePath());
                        scene.ClearDirty();
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

        // Left - Metrics, Models, Hierarchy
        ImGui::DockBuilderDockWindow("Metrics", dockLeft);
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

        // MCP + RAG status buttons — right-aligned
        {
            const bool mcpRunning = IsMcpServerRunning();
            const bool ragEnabled = IsRagEnabled();
            const auto ragStatus = GetCodebaseStatus();

            const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float mcpW = ImGui::CalcTextSize("MCP").x + pad;
            const float ragW = ImGui::CalcTextSize("RAG").x + pad;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - mcpW - 4.0f - ragW);

            // MCP button
            {
                const ImVec4 mcpColor = mcpRunning ? ImVec4(0.20f, 0.75f, 0.20f, 1.f)
                                                   : ImVec4(0.45f, 0.45f, 0.45f, 1.f);
                ImGui::PushStyleColor(ImGuiCol_Text, mcpColor);
                if (ImGui::SmallButton("MCP"))
                    SetMcpServerEnabled(!mcpRunning);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(mcpRunning ? "MCP server: ready" : "MCP server: off");
            }

            ImGui::SameLine(0, 4);

            // RAG button — color reflects actual index state; click only toggles when MCP is on
            {
                ImVec4 ragColor;
                const char *ragTooltip;
                if (!mcpRunning)
                {
                    ragColor = ImVec4(0.45f, 0.45f, 0.45f, 1.f);
                    ragTooltip = m_ragRequestedEnabled ? "RAG: waiting for MCP" : "RAG: disabled";
                }
                else if (!ragEnabled)
                {
                    ragColor = ImVec4(0.45f, 0.45f, 0.45f, 1.f);
                    ragTooltip = "RAG: disabled";
                }
                else if (ragStatus.loading)
                {
                    ragColor = ImVec4(0.70f, 0.70f, 0.20f, 1.f);
                    ragTooltip = "RAG: loading saved index";
                }
                else if (ragStatus.checking)
                {
                    ragColor = ImVec4(0.80f, 0.80f, 0.20f, 1.f);
                    ragTooltip = "RAG: checking";
                }
                else if (m_isIndexing.load())
                {
                    const int prog = m_indexProgress.load();
                    const int total = m_indexTotal.load();
                    if (prog == 0 || total == 0)
                    {
                        ragColor = ImVec4(0.85f, 0.65f, 0.10f, 1.f);
                        ragTooltip = "RAG: fetching";
                    }
                    else
                    {
                        ragColor = ImVec4(0.70f, 0.80f, 0.15f, 1.f);
                        ragTooltip = "RAG: indexing";
                    }
                }
                else if (ragStatus.ready && ragStatus.outdated > 0)
                {
                    ragColor = ImVec4(0.85f, 0.65f, 0.10f, 1.f);
                    ragTooltip = "RAG: index needs refresh";
                }
                else if (HasCodebaseIndex())
                {
                    ragColor = ImVec4(0.20f, 0.75f, 0.20f, 1.f);
                    ragTooltip = "RAG: indexed";
                }
                else
                {
                    ragColor = ImVec4(0.20f, 0.45f, 0.20f, 1.f);
                    ragTooltip = "RAG: enabled";
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ragColor);
                if (ImGui::SmallButton("RAG") && mcpRunning)
                {
                    if (ragEnabled)
                        DisableRag();
                    else
                        EnableRag();
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", ragTooltip);
            }
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

            if (ImGui::BeginMenu("Assets"))
            {
                if (ImGui::MenuItem("Recompile Shaders", "Ctrl+Shift+R"))
                    EventSystem::PushEvent(EventType::CompileShaders);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Connection"))
            {
                const bool mcpRunning = IsMcpServerRunning();
                if (ImGui::MenuItem("MCP Server", nullptr, mcpRunning))
                    SetMcpServerEnabled(!mcpRunning);

                if (ImGui::BeginMenu("RAG"))
                {
                    if (ImGui::MenuItem("Enable", nullptr, m_ragRequestedEnabled))
                    {
                        if (m_ragRequestedEnabled)
                            DisableRag();
                        else
                            EnableRag();
                    }

                    ImGui::BeginDisabled(!IsRagEnabled());
                    if (ImGui::MenuItem("Index"))
                        StartCodebaseIndexing();
                    if (ImGui::MenuItem("Check"))
                        CheckRagStatus();
                    ImGui::EndDisabled();

                    ImGui::Separator();

                    if (ImGui::BeginMenu("Embedding"))
                    {
                        // Helper: select provider+model and apply
                        const auto selectEmbedding = [this](int kind, const std::string &model)
                        {
                            m_embeddingProviderKind = kind;
                            m_embeddingModel = model;
                            if (kind == -1)
                            {
                                m_codebase.SetEmbeddingProvider(nullptr);
                                PE_INFO("[RAG] Embedding disabled — BM25 only");
                            }
                            else
                            {
                                auto provider = pagent::CreateEmbeddingProvider(
                                    static_cast<pagent::EmbeddingProviderKind>(kind), model);
                                if (provider)
                                {
                                    m_codebase.SetEmbeddingProvider(std::move(provider));
                                    PE_INFO("[RAG] Embedding set: %s (%s) — re-index to apply", model.c_str(),
                                            kind == (int)pagent::EmbeddingProviderKind::Ollama   ? "Ollama"
                                            : kind == (int)pagent::EmbeddingProviderKind::Google ? "Google"
                                            : kind == (int)pagent::EmbeddingProviderKind::OpenAI ? "OpenAI"
                                                                                                 : "Voyage");
                                }
                                else
                                {
                                    PE_WARN("[RAG] Failed to create embedding provider — check API key env var");
                                }
                            }

                            m_codebase.MarkStatusDirty();
                            if (IsRagEnabled())
                                CheckRagStatus();
                        };

                        // None
                        if (ImGui::MenuItem("None", nullptr, m_embeddingProviderKind == -1))
                            selectEmbedding(-1, "");
                        ImGui::Separator();

                        // Provider submenu helper
                        const auto providerMenu = [&](const char *label, pagent::EmbeddingProviderKind kind,
                                                      bool hasKey, const char *keyHint)
                        {
                            const int k = static_cast<int>(kind);
                            const bool active = m_embeddingProviderKind == k;
                            ImGui::BeginDisabled(!hasKey);
                            if (ImGui::BeginMenu(label))
                            {
                                const auto models = pagent::GetKnownEmbeddingModels(kind);
                                if (models.empty())
                                {
                                    const std::string def = pagent::GetDefaultEmbeddingModelName(kind);
                                    if (ImGui::MenuItem(def.c_str(), nullptr, active && m_embeddingModel == def))
                                        selectEmbedding(k, def);
                                }
                                else
                                {
                                    for (const auto &m : models)
                                        if (ImGui::MenuItem(m.c_str(), nullptr, active && m_embeddingModel == m))
                                            selectEmbedding(k, m);
                                }
                                ImGui::EndMenu();
                            }
                            ImGui::EndDisabled();
                            if (!hasKey && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip("Set %s", keyHint);
                        };

                        providerMenu("Ollama", pagent::EmbeddingProviderKind::Ollama, true, "");
                        providerMenu("Google", pagent::EmbeddingProviderKind::Google, std::getenv("PAGENT_GEMINI_API_KEY"), "PAGENT_GEMINI_API_KEY");
                        providerMenu("OpenAI", pagent::EmbeddingProviderKind::OpenAI, std::getenv("PAGENT_OPENAI_API_KEY"), "PAGENT_OPENAI_API_KEY");
                        providerMenu("Voyage", pagent::EmbeddingProviderKind::Voyage, std::getenv("PAGENT_VOYAGE_API_KEY"), "PAGENT_VOYAGE_API_KEY");

                        ImGui::EndMenu();
                    }

                    ImGui::EndMenu();
                }
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
        if (!IsRagEnabled())
        {
            PE_WARN("[RAG] Indexing skipped because MCP/RAG is disabled");
            return;
        }

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
        auto codebaseStore = m_codebase.GetCodebaseStoreShared();
        auto codebaseBM25 = m_codebase.GetCodebaseBM25Shared();
        const bool hasEmbeddingProvider = m_codebase.GetEmbeddingProvider() != nullptr;
        const bool mustResetForBm25Only = !hasEmbeddingProvider;

        if (m_indexThread.joinable())
            m_indexThread.join();

        m_codebase.MarkStatusDirty();
        m_isIndexing.store(true);
        m_indexCancel.store(false);
        m_indexProgress.store(0);
        m_indexTotal.store(0);
        {
            std::lock_guard lock(m_indexMutex);
            m_indexCurrentFile.clear();
        }

        auto embedding = m_codebase.GetEmbeddingProviderShared();
        std::string storePath = m_codebaseStorePath;
        if (fullRebuild || mustResetForBm25Only)
        {
            codebaseStore->Clear();
            codebaseBM25->Clear();
        }

        auto includeFiles = indexingConfig.include_files;
        auto skipDirs = indexingConfig.skip_directories;
        auto skipFiles = indexingConfig.skip_files;
        auto skipExts = indexingConfig.skip_extensions;
        auto skipRegex = indexingConfig.skip_regex;

        m_indexThread = std::thread([this, embedding, codebaseStore, codebaseBM25, storePath, dirs, includeFiles, skipDirs, skipFiles, skipExts, skipRegex]()
                                    {
            pagent::IndexerConfig config;
            config.directories = dirs;
            config.include_files = includeFiles;
            config.skip_directories = skipDirs;
            config.skip_files = skipFiles;
            if (!skipExts.empty())
                config.skip_extensions = skipExts;
            config.skip_regex = skipRegex;

            PE_INFO("[RAG] Indexing started (%d directories, embeddings=%s)", static_cast<int>(config.directories.size()), embedding ? "on" : "off");

            auto saveMtx = std::make_shared<std::mutex>();
            auto pIndexer = std::make_unique<pagent::CodebaseIndexer>(embedding.get(), codebaseStore.get(), codebaseBM25.get(),
                [this, &codebaseStore, &storePath, saveMtx](int done, int total, const std::string &file)
                {
                    m_indexProgress.store(done);
                    m_indexTotal.store(total);
                    {
                        std::lock_guard lock(m_indexMutex);
                        m_indexCurrentFile = file;
                        if (m_indexerPtr)
                        {
                            auto *idx = static_cast<pagent::CodebaseIndexer *>(m_indexerPtr);
                            m_indexActiveThreads.store(idx->GetActiveThreads());
                            m_indexTotalThreads = idx->GetTotalThreads();
                        }
                    }
                    if (done % 50 == 0 || done == total)
                        PE_INFO("[RAG] %d/%d  %s", done, total, file.c_str());
                    // Save after each file - skip if cancelled
                    if (!storePath.empty() && !m_indexCancel.load())
                    {
                        std::lock_guard saveLock(*saveMtx);
                        codebaseStore->SaveToBinary(storePath);
                    }
                });

            // Check cancel flag before each file via the indexer's cancel mechanism
            {
                std::lock_guard lock(m_indexMutex);
                m_indexerPtr = pIndexer.get();
            }

            int chunks = pIndexer->Index(config);

            {
                std::lock_guard lock(m_indexMutex);
                m_indexerPtr = nullptr;
            }

            if (!storePath.empty() && !m_indexCancel.load())
                codebaseStore->SaveToBinary(storePath);

            PE_INFO("[RAG] Indexing %s: %d chunks", m_indexCancel.load() ? "cancelled" : "finished", chunks);
            m_isIndexing.store(false);
            QueueMainThreadAction([this]() { if (IsRagEnabled()) CheckRagStatus(); }); });
    }

    void GUI::CancelCodebaseIndexing()
    {
        m_indexCancel.store(true);
        m_codebase.MarkStatusDirty();
        std::lock_guard lock(m_indexMutex);
        if (m_indexerPtr)
            static_cast<pagent::CodebaseIndexer *>(m_indexerPtr)->Cancel();
    }

    bool GUI::HasCodebaseIndex() const
    {
        auto bm25 = m_codebase.GetCodebaseBM25Shared();
        if (bm25 && bm25->Size() > 0)
            return true;

        auto store = m_codebase.GetCodebaseStoreShared();
        return store && store->Size() > 0;
    }

    size_t GUI::GetCodebaseEntryCount() const
    {
        auto bm25 = m_codebase.GetCodebaseBM25Shared();
        if (bm25)
            return bm25->Size();

        auto store = m_codebase.GetCodebaseStoreShared();
        return store ? store->Size() : 0;
    }

    void GUI::Init()
    {
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

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable multiple viewports
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;          // Enable SRGB support

        ImGui::StyleColorsClassic();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        // Verify the SDL2 backend supports platform windows
        PE_ERROR_IF(!(io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports),
                    "SDL2 backend doesn't support platform viewports!");

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = vk::AttachmentLoadOp::eLoad;
        VkFormat format = static_cast<VkFormat>(RHII.GetSurface()->GetFormat());
        Queue *queue = RHII.GetMainQueue();

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = RHII.GetInstance();
        init_info.PhysicalDevice = RHII.GetGpu();
        init_info.Device = RHII.GetDevice();
        init_info.QueueFamily = queue->GetFamilyId();
        init_info.Queue = queue->ApiHandle();
        init_info.PipelineCache = nullptr;
        init_info.DescriptorPool = RHII.GetDescriptorPool()->ApiHandle();
        init_info.Subpass = 0;
        init_info.MinImageCount = RHII.GetSwapchainImageCount();
        init_info.ImageCount = RHII.GetSwapchainImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;
        // if (gSettings.dynamic_rendering)
        // {
        //     init_info.UseDynamicRendering = true;
        //     init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        //     init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        //     init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        // }
        // else
        {
            RenderPass *renderPass = CommandBuffer::GetRenderPass(1, m_attachment.get());
            init_info.UseDynamicRendering = false;
            init_info.RenderPass = renderPass->ApiHandle();
        }

        ImGui_ImplVulkan_Init(&init_info);

        // Load ALL fonts upfront for dynamic style switching
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

        ImGui_ImplVulkan_CreateFontsTexture();

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

        EventSystem::RegisterCallback(EventType::AfterCommandWait, std::move(AddGpuTimerInfo));

        m_agentScriptSystem = std::make_unique<ScriptSystem>();
        m_agentScriptSystem->InitRestricted(nullptr);
        m_editorToolRuntime = std::make_unique<EditorToolRuntime>(
            m_agentScriptSystem.get(),
            [this](std::function<void()> fn)
            {
                QueueMainThreadAction(std::move(fn));
            },
            RHII.GetWindow());
        m_editorToolServer = std::make_unique<EditorToolServer>(m_editorToolRuntime.get(), this);
        m_codebaseStorePath = BuildEditorCodebaseStorePath();
        LoadAgentConfig();
        if (m_mcpStartEnabled)
        {
            EnsureCodebaseStoreLoaded();
            m_editorToolServer->Start();
            CheckRagStatus();
        }

        auto properties = std::make_shared<Properties>();
        auto metrics = std::make_shared<Metrics>();
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
        // Console added early to potentially influence tab ordering (Leftmost)
        m_widgets = {console,
                     properties,
                     metrics,
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
                     globalWidget};

        // Initialize Core Logging and attach Console
        Log::Attach([console](const std::string &msg, LogType type)
                    { console->AddLog(type, "%s", msg.c_str()); });

        // Populate Menu Vectors
        m_menuWindowWidgets = {console,
                               metrics,
                               properties,
                               models,
                               assetInfo,
                               fileBrowser,
                               hierarchy,
                               particles,
                               globalWidget};
        for (auto &widget : m_widgets)
            widget->Init(this);

        queue->WaitIdle();
    }

    void GUI::ApplyStartupLayout()
    {
        SDL_PumpEvents();

        if (m_hasIniFile)
            ImGui::LoadIniSettingsFromDisk("imgui.ini");
        else
            m_requestDockReset = true;

        // Restore the last open scene
        LoadEditorConfig();
    }

    void GUI::ExecutePass(CommandBuffer *cmd)
    {
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
            sceneViewBarrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sceneViewBarrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
            sceneViewBarrier.accessMask = vk::AccessFlagBits2::eShaderRead;
            cmd->ImageBarrier(sceneViewBarrier);
        }

        ImageBarrierInfo barrierInfo{};
        barrierInfo.image = displayRT;
        barrierInfo.layout = vk::ImageLayout::eAttachmentOptimal;
        barrierInfo.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrierInfo.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;

        cmd->ImageBarrier(barrierInfo);
        cmd->BeginPass(1, m_attachment.get(), "GUI", true);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->ApiHandle());
        cmd->EndPass();
    }

    void GUI::DrawPlatformWindows()
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    void GUI::Update()
    {
        // Drain the MCP/tool action queue before the render-state guard so that
        // queued tool calls (screenshot, Lua exec, mouse input) are never starved
        // when the editor window is minimised or rendering is paused.
        {
            std::vector<std::function<void()>> mainThreadActions;
            {
                std::lock_guard lock(m_mainThreadActionMutex);
                mainThreadActions.swap(m_pendingMainThreadActions);
            }
            for (auto &fn : mainThreadActions)
                fn();
        }

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
                            scene.SaveScene(scene.GetScenePath());
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
                        undoRedo.RecordSnapshot(undoRedoRS->GetScene());

                    SelectionType selType = selection.GetSelectionType();
                    if (selType == SelectionType::Node)
                    {
                        ModelAsset *model = selection.GetSelectedModel();
                        int nodeIndex = selection.GetSelectedNodeIndex();
                        if (model && nodeIndex >= 0)
                            EventSystem::PushEvent(EventType::NodeRemoved,
                                                   std::make_pair(model, nodeIndex));
                    }
                    else if (selType == SelectionType::Mesh)
                    {
                        ModelAsset *model = selection.GetSelectedModel();
                        int nodeIndex = selection.GetSelectedNodeIndex();
                        if (model && nodeIndex >= 0)
                            EventSystem::PushEvent(EventType::MeshRemoved,
                                                   std::make_pair(model, nodeIndex));
                    }
                    else if (selType == SelectionType::Camera)
                    {
                        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                        if (rs)
                        {
                            auto &cameras = rs->GetScene().GetCameras();
                            int idx = selection.GetSelectedNodeIndex();
                            if (cameras.size() > 1 && idx >= 0 && idx < static_cast<int>(cameras.size()))
                                rs->GetScene().RemoveCamera(cameras[idx]);
                        }
                    }
                    else if (selType == SelectionType::Light)
                    {
                        LightSystem *ls = GetGlobalSystem<LightSystem>();
                        if (ls)
                        {
                            int idx = selection.GetSelectedLightIndex();
                            LightType lt = selection.GetSelectedLightType();
                            auto eraseLight = [&](auto &lights)
                            {
                                if (idx >= 0 && idx < static_cast<int>(lights.size()))
                                {
                                    lights.erase(lights.begin() + idx);
                                    selection.ClearSelection();
                                }
                            };

                            switch (lt)
                            {
                            case LightType::Directional:
                                eraseLight(ls->GetDirectionalLights());
                                break;
                            case LightType::Point:
                                eraseLight(ls->GetPointLights());
                                break;
                            case LightType::Spot:
                                eraseLight(ls->GetSpotLights());
                                break;
                            case LightType::Area:
                                eraseLight(ls->GetAreaLights());
                                break;
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

        Menu();
        StatusBar();
        DrawExitPopup();
        DrawSaveBeforeLoadPopup();
        DrawSaveBeforeNewPopup();
        DrawOverwriteConfirmationPopup();
        Toolbar();
        BuildDockspace();

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        for (auto &widget : m_widgets)
        {
            if (widget->IsOpen())
                widget->Update();
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
            ImGui::SetCursorPos(ImVec2(8.0f, centerY));

            bool canUndo = ur.CanUndo();
            if (!canUndo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)) && canUndo)
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                    ur.Undo(rs->GetScene());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Undo (Ctrl+Z)");
            if (!canUndo)
                ImGui::PopStyleColor();

            ImGui::SameLine();

            bool canRedo = ur.CanRedo();
            if (!canRedo)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button(ICON_FA_ROTATE_RIGHT, ImVec2(buttonSize, buttonSize)) && canRedo)
            {
                RendererSystem *rs = GetGlobalSystem<RendererSystem>();
                if (rs)
                    ur.Redo(rs->GetScene());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Redo (Ctrl+Y)");
            if (!canRedo)
                ImGui::PopStyleColor();
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
                GUIState::s_isPaused = !GUIState::s_isPaused;
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
            rs->GetScene().SaveScene("temp_play.pescene");
            GUIState::s_playMode = true;
            GUIState::s_isPaused = false;
        }
    }

    void GUI::Stop()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
        {
            GUIState::s_playMode = false;
            GUIState::s_isPaused = false;
            rs->GetScene().LoadScene("temp_play.pescene");
            UndoRedo::Instance().Clear();
            if (std::filesystem::exists("temp_play.pescene"))
                std::filesystem::remove("temp_play.pescene");
        }
    }
} // namespace pe
