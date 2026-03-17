#include "AgentWidget.h"
#include "GUI/GUI.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/Primitives.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Camera/Camera.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "Script/ScriptSystem.h"
#include "PhasmaAgent/AgentUtils.h"
#include "imgui/imgui.h"

#include "PhasmaAgent/GoogleEmbedding.h"
#include "PhasmaAgent/OpenAIEmbedding.h"
#include "PhasmaAgent/OllamaEmbedding.h"
#include "PhasmaAgent/VectorStore.h"

#include "stb/stb_image.h"

#if defined(PE_WIN32)
#include <Windows.h>
#endif

using namespace pagent;

namespace pe
{
    AgentWidget::AgentWidget() : Widget("Agent"), m_agent(pagent::AgentConfig{})
    {
    }

    AgentWidget::~AgentWidget()
    {
        *m_alive = false; // signal background threads to stop accessing this
        SaveConfig();
        auto storePath = GetVectorStorePath();
        if (m_vectorStore && !storePath.empty())
            m_vectorStore->SaveToFile(storePath);
        pagent::Agent::CancelPull(m_pullCancel);
        pagent::Agent::CancelPull(m_pullEmbeddingCancel);
        if (m_agent)
            m_agent->CancelPending();
        m_agentScriptSystem.Destroy();
    }

    void AgentWidget::Init(GUI *gui)
    {
        pe::Widget::Init(gui);
        m_agentScriptSystem.InitRestricted(nullptr);

        m_providers = pagent::DiscoverProviders();
        // Add "External" provider (file-based, for Claude Code / Cursor / any AI tool)
        m_providers.push_back({pagent::Provider::Ollama, "External", "", "external"});

        // Restore saved config (provider, model, embedding settings)
        if (std::filesystem::exists(Path::Assets + "Agent/agent_config.json"))
        {
            LoadConfig();
        }
        else
        {
            // First launch defaults
            const char *providerEnv = std::getenv("PAGENT_PROVIDER");
            m_selectedProviderIndex = providerEnv ? pagent::GetDefaultProviderIndex(m_providers)
                                                  : static_cast<int>(m_providers.size()) - 1;
            if (std::getenv("PAGENT_GEMINI_API_KEY"))
            {
                m_embeddingEnabled = true;
                m_selectedEmbeddingProvider = 0; // Google
            }
            UpdateEmbeddingModels();
        }
        ConfigureAgent(m_providers[m_selectedProviderIndex].provider);

        // Populate model list for the restored provider
        FetchAvailableModels();

        // Fetch local models for Ollama at startup (no remote)
        // Other providers have hardcoded model lists
        {
            auto aliveRef = m_alive;
            for (int pi = 0; pi < static_cast<int>(m_providers.size()); ++pi)
            {
                if (m_providers[pi].name == "External" || m_providers[pi].provider != pagent::Provider::Ollama)
                    continue;
                auto prov = m_providers[pi].provider;
                auto key = m_providers[pi].apiKey;
                std::thread([this, aliveRef, prov, key, pi]()
                            {
                    std::vector<pagent::Agent::ModelInfo> modelInfos;
                    for (int attempt = 0; attempt < 5; ++attempt)
                    {
                        if (!*aliveRef) return;
                        if (attempt > 0)
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        modelInfos = pagent::Agent::FetchModelInfos(prov, key, "", true);
                        if (!modelInfos.empty()) break;
                    }
                    if (!*aliveRef) return;

                    std::vector<std::string> names;
                    std::vector<bool> localFlags;
                    for (auto &mi : modelInfos)
                    {
                        names.push_back(std::move(mi.name));
                        localFlags.push_back(mi.local);
                    }
                    if (!*aliveRef) return;

                    QueueAction([this, aliveRef, names = std::move(names), localFlags = std::move(localFlags), pi]()
                    {
                        if (!*aliveRef) return;
                        m_modelCache[pi] = {names, localFlags};
                        if (m_selectedProviderIndex == pi)
                        {
                            m_isFetchingModels = false;
                            m_availableModels = m_modelCache[pi].names;
                            m_modelIsLocal = m_modelCache[pi].local;
                            m_selectedModelIndex = 0;
                            for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                                if (m_availableModels[i] == m_modelName) { m_selectedModelIndex = i; break; }
                            if (m_modelName.empty() && !m_availableModels.empty())
                            {
                                m_modelName = m_availableModels[0];
                                if (m_agent)
                                    m_agent->SetModel(m_modelName);
                            }
                        }
                    }); })
                    .detach();
            }
        }
    }

    void AgentWidget::ConfigureAgent(pagent::Provider provider)
    {
        // Find the provider info matching current selection
        const pagent::ProviderInfo *info = &m_providers[m_selectedProviderIndex];

        m_isExternalAI = (info->name == "External");
        if (m_isExternalAI)
        {
            m_modelName = m_externalFile;
            m_availableModels = {m_externalFile};
            m_selectedModelIndex = 0;
            m_agentConfigured = true;

            // Ensure agent directory exists
            std::string agentDir = Path::Assets + "Agent/";
            if (!std::filesystem::exists(agentDir))
                std::filesystem::create_directories(agentDir);

            UpdateExternalFileWatch();
            return;
        }

        pagent::AgentConfig config;
        // Derive project root from Assets path (go up from Assets/)
        std::string projectRoot = std::filesystem::path(Path::Assets).parent_path().parent_path().string();
        if (!projectRoot.empty() && projectRoot.back() != '/')
            projectRoot += '/';

        config.system_prompt =
            "You are an AI assistant inside PhasmaEditor (Vulkan 3D engine). "
            "FIRST THING: use read_agent_file to read START.md for your full API reference and rules. "
            "Control the editor via execute_lua. Be very concise. ASCII only, no emoji. "
            "Chain ALL operations in ONE execute_lua call. Check results for errors. "
            "To load 3D models: 1) call find_loadable_model tool, 2) execute_lua with: local m, err = load_model('path/from/step1') "
            "The Lua function is load_model (NOT pe_load_model). Do NOT use fs.find/fs.list for models. "
            "Set unique labels on created models. Use request_feature for missing capabilities. "
            "Workspace: " +
            Path::Assets + "Agent/ | Assets: " + Path::Assets + ".";

        config.log_callback = [](const std::string &msg)
        { PE_INFO("%s", msg.c_str()); };
        config.max_tool_rounds = 30;
        config.max_tool_result_chars = 500;
        config.max_history_messages = 20;
        config.summarize_after_messages = 30;
        config.provider = info->provider;
        config.api_key = info->apiKey;
        config.model = info->defaultModel;

        // Set up Gemini vision fallback key (used when main provider lacks vision)
        const char *geminiKey = std::getenv("PAGENT_GEMINI_API_KEY");
        if (geminiKey)
            config.gemini_api_key_for_vision = geminiKey;

        // Set up embedding provider based on current selection
        config.embedding_provider = CreateEmbeddingProvider();

        if (config.embedding_provider && !m_vectorStore)
        {
            m_vectorStore = std::make_shared<pagent::VectorStore>();
            auto sp = GetVectorStorePath();
            if (!sp.empty())
                m_vectorStore->LoadFromFile(sp);
            m_turnsSinceSave = 0;
        }

        // Use saved model name if set, otherwise use provider default
        if (m_modelName.empty())
            m_modelName = config.model;
        config.model = m_modelName;
        m_selectedModelIndex = 0;
        m_agentConfigured = true;

        // Use cached models if available, otherwise set default
        auto cacheIt = m_modelCache.find(m_selectedProviderIndex);
        if (cacheIt != m_modelCache.end())
        {
            m_availableModels = cacheIt->second.names;
            m_modelIsLocal = cacheIt->second.local;
            for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                if (m_availableModels[i] == m_modelName)
                {
                    m_selectedModelIndex = i;
                    break;
                }
        }
        else
        {
            m_availableModels.clear();
            m_modelIsLocal.clear();
            if (!m_isFetchingModels && !config.model.empty())
            {
                m_availableModels = {config.model};
                m_modelIsLocal = {provider != pagent::Provider::Ollama};
            }
        }

        m_agent = pagent::Agent(std::move(config));
        m_agent->SetEventCallback([this](const pagent::AgentEvent &ev)
                                  { OnAgentEvent(ev); });

        // Connect vector store to the agent's request worker
        if (m_vectorStore)
            m_agent->SetVectorStore(m_vectorStore.get());

        RegisterTools();
    }

    std::shared_ptr<pagent::IEmbeddingProvider> AgentWidget::CreateEmbeddingProvider()
    {
        if (!m_embeddingEnabled || m_embeddingModels.empty())
            return nullptr;

        const std::string &model = m_embeddingModels[m_selectedEmbeddingModel];
        const char *geminiKey = std::getenv("PAGENT_GEMINI_API_KEY");
        const char *openaiKey = std::getenv("PAGENT_OPENAI_API_KEY");

        switch (m_selectedEmbeddingProvider)
        {
        case 0: // Google
            if (geminiKey)
                return std::make_shared<pagent::GoogleEmbedding>(geminiKey, model);
            break;
        case 1: // OpenAI
        {
            if (!openaiKey)
                break;
            int dims = 1536;
            if (model.find("large") != std::string::npos)
                dims = 3072;
            return std::make_shared<pagent::OpenAIEmbedding>(openaiKey, model, dims);
        }
        case 2: // Ollama
            return std::make_shared<pagent::OllamaEmbedding>(model);
        default:
            break;
        }
        return nullptr;
    }

    void AgentWidget::UpdateEmbeddingModels(bool fetchRemote)
    {
        m_embeddingModels.clear();
        m_selectedEmbeddingModel = 0;

        switch (m_selectedEmbeddingProvider)
        {
        case 0: // Google
            m_embeddingModels = {"gemini-embedding-2-preview", "gemini-embedding-001"};
            break;
        case 1: // OpenAI
            m_embeddingModels = {"text-embedding-3-small", "text-embedding-3-large"};
            break;
        case 2: // Ollama — local only by default, Fetch button gets remote too
        {
            m_embeddingModelIsLocal.clear();
            m_isFetchingEmbeddingModels = true;
            auto alive = m_alive;
            bool localOnly = !fetchRemote;
            std::thread([this, alive, localOnly]()
                        {
                auto modelInfos = pagent::Agent::FetchOllamaEmbeddingModels("", localOnly);
                if (!*alive) return;
                QueueAction([this, alive, modelInfos]()
                {
                    m_isFetchingEmbeddingModels = false;
                    if (m_selectedEmbeddingProvider != 2) return;
                    m_embeddingModels.clear();
                    m_embeddingModelIsLocal.clear();
                    for (const auto &mi : modelInfos)
                    {
                        m_embeddingModels.push_back(mi.name);
                        m_embeddingModelIsLocal.push_back(mi.local);
                    }
                    if (m_selectedEmbeddingModel >= static_cast<int>(m_embeddingModels.size()))
                        m_selectedEmbeddingModel = 0;
                }); })
                .detach();
            break;
        }
        default:
            break;
        }
    }

    void AgentWidget::SaveConfig()
    {
        std::string path = Path::Assets + "Agent/agent_config.json";
        std::ofstream f(path);
        if (!f)
            return;

        std::string providerName = (m_selectedProviderIndex < static_cast<int>(m_providers.size()))
                                       ? m_providers[m_selectedProviderIndex].name
                                       : "";
        std::string embModelName = (m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size()))
                                       ? m_embeddingModels[m_selectedEmbeddingModel]
                                       : "";

        static const char *embProviderNames[] = {"Google", "OpenAI", "Ollama"};
        std::string embProviderName = embProviderNames[std::clamp(m_selectedEmbeddingProvider, 0, 2)];

        nlohmann::ordered_json j;
        j["agent"] = nlohmann::ordered_json{{"provider", providerName}, {"model", m_modelName}};
        j["embeddings"] = nlohmann::ordered_json{{"enabled", m_embeddingEnabled}, {"provider", embProviderName}, {"model", embModelName}};

        f << j.dump(4) << "\n";
    }

    void AgentWidget::LoadConfig()
    {
        std::string path = Path::Assets + "Agent/agent_config.json";
        std::ifstream f(path);
        if (!f)
            return;

        try
        {
            auto j = nlohmann::json::parse(f);

            // Restore agent provider
            std::string providerName = j.value("/agent/provider"_json_pointer, std::string{});
            if (!providerName.empty())
            {
                for (int i = 0; i < static_cast<int>(m_providers.size()); ++i)
                {
                    if (m_providers[i].name == providerName)
                    {
                        m_selectedProviderIndex = i;
                        break;
                    }
                }
            }

            std::string modelName = j.value("/agent/model"_json_pointer, std::string{});
            if (!modelName.empty())
                m_modelName = modelName;

            // Restore embedding config
            m_embeddingEnabled = j.value("/embeddings/enabled"_json_pointer, false);

            std::string embProviderName = j.value("/embeddings/provider"_json_pointer, std::string{"Google"});
            static const std::pair<const char *, int> embProviderMap[] = {{"Google", 0}, {"OpenAI", 1}, {"Ollama", 2}};
            m_selectedEmbeddingProvider = 0;
            for (auto &[name, idx] : embProviderMap)
                if (embProviderName == name) { m_selectedEmbeddingProvider = idx; break; }

            UpdateEmbeddingModels();

            std::string embModelName = j.value("/embeddings/model"_json_pointer, std::string{});
            m_selectedEmbeddingModel = 0;
            for (int i = 0; i < static_cast<int>(m_embeddingModels.size()); i++)
                if (m_embeddingModels[i] == embModelName) { m_selectedEmbeddingModel = i; break; }
        }
        catch (...)
        {
            // Parsing or access error — keep defaults
        }
    }

    std::string AgentWidget::GetVectorStorePath() const
    {
        std::string modelName = (m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size()))
                                    ? m_embeddingModels[m_selectedEmbeddingModel]
                                    : "";
        // Don't create files with placeholder names
        if (modelName.empty() || modelName.find("fetching") != std::string::npos)
            return {};
        // Replace characters that are invalid in filenames
        for (auto &c : modelName)
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        return Path::Assets + "Agent/vectors_" + modelName + ".json";
    }

    void AgentWidget::RegisterTools()
    {
        // Derive project root for source file access
        std::string projectRoot = std::filesystem::path(Path::Assets).parent_path().parent_path().string();
        if (!projectRoot.empty() && projectRoot.back() != '/')
            projectRoot += '/';

        // =====================================================================
        // New Lua-based tool set
        // =====================================================================

        m_agent->RegisterTool({.name = "execute_lua",
                               .description = "Executes Lua code in the engine's ScriptSystem. Use this for ALL scene manipulation: "
                                              "models, camera, lights, materials, settings, shaders, particles, skybox, scene save/load. "
                                              "The full Lua API is documented in the system prompt. "
                                              "Use pe_log() to output information. Returns captured output or 'ok' on success.",
                               .properties = {
                                   {"code", "Lua code to execute", pagent::SchemaType::String, true},
                               },
                               .handler = [this](const std::string &args) -> std::string
                               {
                                   std::string code = JsonUnescape(ExtractArgStr(args, "code"));
                                   if (code.empty())
                                       return "{\"error\":\"missing code\"}";

                                   std::string result;
                                   std::mutex mtx;
                                   std::condition_variable cv;
                                   bool done = false;

                                   QueueAction([&]()
                                               {
                                       if (!m_agentScriptSystem.IsInitialized())
                                           result = "error: ScriptSystem not available";
                                       else
                                           result = m_agentScriptSystem.ExecuteLua(code);
                                       {
                                           std::lock_guard lock(mtx);
                                           done = true;
                                       }
                                       cv.notify_one(); });

                                   // Wait for main thread to execute the action
                                   {
                                       std::unique_lock lock(mtx);
                                       if (!cv.wait_for(lock, std::chrono::seconds(10), [&]
                                                        { return done; }))
                                           return "{\"error\":\"timeout waiting for Lua execution\"}";
                                   }

                                   if (result.rfind("error:", 0) == 0)
                                       return JsonObj({{"error", JsonStr(result)}});
                                   return JsonObj({{"output", JsonStr(result)}});
                               }});

        m_agent->RegisterTool({.name = "read_project_file",
                               .description = "Reads a source file from the project (C++ headers, source, shaders, configs). "
                                              "Use this to understand engine APIs before writing Lua code. "
                                              "Path relative to project root or absolute.",
                               .properties = {
                                   {"path", "File path relative to project root (e.g. 'PhasmaCore/Code/Base/Path.h') or absolute", pagent::SchemaType::String, true},
                               },
                               .handler = [projectRoot](const std::string &args) -> std::string
                               {
                                   std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                   if (path.empty())
                                       return "{\"error\":\"missing path\"}";

                                   std::filesystem::path fpath(path);
                                   if (fpath.is_relative())
                                       fpath = std::filesystem::path(projectRoot) / fpath;

                                   if (!IsPathSafe(fpath.string(), projectRoot))
                                       return "{\"error\":\"path outside project directory\"}";

                                   if (!std::filesystem::exists(fpath))
                                       return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                                   if (std::filesystem::is_directory(fpath))
                                       return "{\"error\":\"path is a directory, use find_project_file or list_project_dir\"}";

                                   auto size = std::filesystem::file_size(fpath);
                                   if (size > 100000)
                                       return JsonObj({{"error", JsonStr("file too large: " + std::to_string(size) + " bytes")}});

                                   std::ifstream file(fpath, std::ios::in);
                                   if (!file.is_open())
                                       return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

                                   std::string content((std::istreambuf_iterator<char>(file)),
                                                       std::istreambuf_iterator<char>());

                                   return JsonObj({{"path", JsonStr(fpath.string())}, {"content", JsonStr(content)}});
                               }});

        m_agent->RegisterTool({.name = "write_project_file",
                               .description = "Writes content to a file in PhasmaEditor/Code/ or PhasmaEditor/Assets/. "
                                              "Use this to modify C++ source, headers, shaders, Lua scripts, or config files. "
                                              "Creates parent directories automatically. "
                                              "Path relative to project root or absolute.",
                               .properties = {
                                   {"path", "File path relative to project root (e.g. 'PhasmaEditor/Code/App/App.cpp')", pagent::SchemaType::String, true},
                                   {"content", "Text content to write", pagent::SchemaType::String, true},
                                   {"append", "If 'true', append instead of overwriting", pagent::SchemaType::String, false},
                               },
                               .handler = [projectRoot](const std::string &args) -> std::string
                               {
                                   std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                   std::string content = JsonUnescape(ExtractArgStr(args, "content"));
                                   std::string appendMode = ExtractArgStr(args, "append");
                                   if (path.empty())
                                       return "{\"error\":\"missing path\"}";
                                   if (content.empty())
                                       return "{\"error\":\"missing content\"}";

                                   std::filesystem::path fpath(path);
                                   if (fpath.is_relative())
                                       fpath = std::filesystem::path(projectRoot) / fpath;

                                   if (!IsPathSafe(fpath.string(), projectRoot))
                                       return "{\"error\":\"path outside project directory\"}";

                                   // Only allow writing inside PhasmaEditor/
                                   std::string editorDir = (std::filesystem::path(projectRoot) / "PhasmaEditor").string();
                                   if (!IsPathSafe(fpath.string(), editorDir))
                                       return "{\"error\":\"writes only allowed inside PhasmaEditor/\"}";

                                   // TODO: Implement UI confirmation here.
                                   // For now, we've at least secured the path traversal and restricted to PhasmaEditor/

                                   std::filesystem::path parentDir = fpath.parent_path();
                                   if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                                   {
                                       std::error_code ec;
                                       std::filesystem::create_directories(parentDir, ec);
                                       if (ec)
                                           return JsonObj({{"error", JsonStr("cannot create directory: " + ec.message())}});
                                   }

                                   auto flags = std::ios::out;
                                   if (appendMode == "true")
                                       flags |= std::ios::app;
                                   else
                                       flags |= std::ios::trunc;

                                   std::ofstream file(fpath, flags);
                                   if (!file.is_open())
                                       return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
                                   file << content;
                                   file.close();

                                   return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(fpath.string())}});
                               }});

        m_agent->RegisterTool({.name = "find_project_file",
                               .description = "Recursively searches for files in the project by name substring (case-insensitive). "
                                              "Use to find C++ headers, source files, shaders, configs, models, etc.",
                               .properties = {
                                   {"query", "Filename substring to search for (e.g. 'Camera.h', '.hlsl', 'sponza')", pagent::SchemaType::String, true},
                                   {"dir", "Subdirectory to search (e.g. 'PhasmaCore/Code'). Defaults to project root.", pagent::SchemaType::String, false},
                               },
                               .handler = [projectRoot](const std::string &args) -> std::string
                               {
                                   std::string query = JsonUnescape(ExtractArgStr(args, "query"));
                                   std::string dir = JsonUnescape(ExtractArgStr(args, "dir"));
                                   if (query.empty())
                                       return "{\"error\":\"missing query\"}";

                                   std::string searchDir = dir.empty() ? projectRoot : dir;
                                   std::filesystem::path searchPath(searchDir);
                                   if (searchPath.is_relative())
                                       searchPath = std::filesystem::path(projectRoot) / searchPath;

                                   if (!IsPathSafe(searchPath.string(), projectRoot))
                                       return "{\"error\":\"path outside project directory\"}";

                                   if (!std::filesystem::exists(searchPath))
                                       return JsonObj({{"error", JsonStr("directory not found: " + searchPath.string())}});

                                   std::string queryLower = query;
                                   for (auto &c : queryLower)
                                       c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                   std::string arr = "[";
                                   bool first = true;
                                   int count = 0;
                                   for (const auto &entry : std::filesystem::recursive_directory_iterator(
                                            searchPath, std::filesystem::directory_options::skip_permission_denied))
                                   {
                                       if (!entry.is_regular_file())
                                           continue;

                                       // Skip build directories and hidden files
                                       std::string pathStr = entry.path().string();
                                       if (pathStr.find("/build/") != std::string::npos ||
                                           pathStr.find("/.") != std::string::npos)
                                           continue;

                                       std::string name = entry.path().filename().string();
                                       std::string nameLower = name;
                                       for (auto &c : nameLower)
                                           c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                       if (nameLower.find(queryLower) == std::string::npos)
                                           continue;

                                       std::string relPath = std::filesystem::relative(entry.path(), projectRoot).string();
                                       std::replace(relPath.begin(), relPath.end(), '\\', '/');

                                       if (!first)
                                           arr += ",";
                                       arr += JsonStr(relPath);
                                       first = false;
                                       if (++count >= 30)
                                           break;
                                   }
                                   arr += "]";
                                   return JsonObj({{"count", std::to_string(count)}, {"files", arr}});
                               }});

        m_agent->RegisterTool({.name = "list_project_dir",
                               .description = "Lists files and subdirectories at a project path. "
                                              "Use to browse project structure (PhasmaCore/Code/, PhasmaEditor/Code/, etc.).",
                               .properties = {
                                   {"path", "Directory path relative to project root (e.g. 'PhasmaEditor/Code/Script')", pagent::SchemaType::String, true},
                               },
                               .handler = [projectRoot](const std::string &args) -> std::string
                               {
                                   std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                   if (path.empty())
                                       return "{\"error\":\"missing path\"}";

                                   std::filesystem::path dirPath(path);
                                   if (dirPath.is_relative())
                                       dirPath = std::filesystem::path(projectRoot) / dirPath;

                                   if (!IsPathSafe(dirPath.string(), projectRoot))
                                       return "{\"error\":\"path outside project directory\"}";

                                   if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
                                       return JsonObj({{"error", JsonStr("not a directory: " + dirPath.string())}});

                                   std::string files = "[", dirs = "[";
                                   bool firstF = true, firstD = true;
                                   for (const auto &entry : std::filesystem::directory_iterator(dirPath))
                                   {
                                       std::string name = entry.path().filename().string();
                                       if (name[0] == '.')
                                           continue; // skip hidden

                                       if (entry.is_directory())
                                       {
                                           if (!firstD)
                                               dirs += ",";
                                           dirs += JsonStr(name + "/");
                                           firstD = false;
                                       }
                                       else
                                       {
                                           if (!firstF)
                                               files += ",";
                                           files += JsonStr(name);
                                           firstF = false;
                                       }
                                   }
                                   files += "]";
                                   dirs += "]";
                                   return JsonObj({{"path", JsonStr(dirPath.string())}, {"files", files}, {"dirs", dirs}});
                               }});

        m_agent->RegisterTool({.name = "find_loadable_model",
                               .description = "Searches for 3D model files (.glb, .gltf, .obj, .fbx) in Assets/Objects/ by name. "
                                              "Returns paths ready to use with load_model(). "
                                              "Example: query 'helmet' finds 'DamagedHelmet/glTF-Binary/DamagedHelmet.glb'.",
                               .properties = {
                                   {"query", "Model name to search for (e.g. 'helmet', 'avocado', 'sponza')", pagent::SchemaType::String, true},
                               },
                               .handler = [](const std::string &args) -> std::string
                               {
                                   std::string query = JsonUnescape(ExtractArgStr(args, "query"));
                                   if (query.empty())
                                       return "{\"error\":\"missing query\"}";

                                   std::string queryLower = query;
                                   for (auto &c : queryLower)
                                       c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                   std::filesystem::path objectsDir(Path::Assets + "Objects");
                                   if (!std::filesystem::exists(objectsDir))
                                       return "{\"error\":\"Objects directory not found\"}";

                                   std::string arr = "[";
                                   bool first = true;
                                   int count = 0;
                                   for (const auto &entry : std::filesystem::recursive_directory_iterator(
                                            objectsDir, std::filesystem::directory_options::skip_permission_denied))
                                   {
                                       if (!entry.is_regular_file())
                                           continue;

                                       auto u8ext = entry.path().extension().u8string();
                                       std::string ext(u8ext.begin(), u8ext.end());
                                       for (auto &c : ext)
                                           c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                       if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx")
                                           continue;

                                       auto u8rel = std::filesystem::relative(entry.path(), Path::Assets + "Objects").u8string();
                                       std::string relPath(u8rel.begin(), u8rel.end());
                                       std::replace(relPath.begin(), relPath.end(), '\\', '/');

                                       std::string relLower = relPath;
                                       for (auto &c : relLower)
                                           c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                       if (relLower.find(queryLower) == std::string::npos)
                                           continue;

                                       if (!first)
                                           arr += ",";
                                       arr += JsonStr(relPath);
                                       first = false;
                                       if (++count >= 20)
                                           break;
                                   }
                                   arr += "]";
                                   return JsonObj({{"count", std::to_string(count)}, {"models", arr}});
                               }});

        const std::string agentWorkspace = Path::Assets + "Agent/";

        m_agent->RegisterTool({.name = "read_agent_file",
                               .description = "Reads a text file from the agent workspace (Assets/Agent/). "
                                              "Use this to read START.md, MEMORY.md, TASKS.md, PROGRESSION.md, or any workspace file.",
                               .properties = {
                                   {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", pagent::SchemaType::String, true},
                               },
                               .handler = [agentWorkspace](const std::string &args) -> std::string
                               {
                                   std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                   if (path.empty())
                                       return "{\"error\":\"missing path\"}";

                                   std::filesystem::path fpath(path);
                                   if (fpath.is_relative())
                                       fpath = std::filesystem::path(agentWorkspace) / fpath;

                                   if (!IsPathSafe(fpath.string(), agentWorkspace))
                                       return "{\"error\":\"path outside workspace directory\"}";

                                   if (!std::filesystem::exists(fpath))
                                       return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                                   std::ifstream file(fpath, std::ios::in);
                                   if (!file.is_open())
                                       return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

                                   std::string content((std::istreambuf_iterator<char>(file)),
                                                       std::istreambuf_iterator<char>());

                                   return JsonObj({{"path", JsonStr(fpath.string())}, {"content", JsonStr(content)}});
                               }});

        m_agent->RegisterTool({.name = "write_agent_file",
                               .description = "Writes or appends to a text file in the agent workspace (Assets/Agent/). "
                                              "Use for MEMORY.md, TASKS.md, PROGRESSION.md, Lua scripts, or any persistent notes. "
                                              "Creates parent directories automatically.",
                               .properties = {
                                   {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", pagent::SchemaType::String, true},
                                   {"content", "Text content to write", pagent::SchemaType::String, true},
                                   {"append", "If 'true', append instead of overwriting", pagent::SchemaType::String, false},
                               },
                               .handler = [agentWorkspace](const std::string &args) -> std::string
                               {
                                   std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                   std::string content = JsonUnescape(ExtractArgStr(args, "content"));
                                   std::string appendMode = ExtractArgStr(args, "append");
                                   if (path.empty())
                                       return "{\"error\":\"missing path\"}";
                                   if (content.empty())
                                       return "{\"error\":\"missing content\"}";

                                   std::filesystem::path fpath(path);
                                   if (fpath.is_relative())
                                       fpath = std::filesystem::path(agentWorkspace) / fpath;

                                   if (!IsPathSafe(fpath.string(), agentWorkspace))
                                       return "{\"error\":\"path outside workspace directory\"}";

                                   std::filesystem::path parentDir = fpath.parent_path();
                                   if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                                   {
                                       std::error_code ec;
                                       std::filesystem::create_directories(parentDir, ec);
                                       if (ec)
                                           return JsonObj({{"error", JsonStr("cannot create directory: " + ec.message())}});
                                   }

                                   auto flags = std::ios::out;
                                   if (appendMode == "true")
                                       flags |= std::ios::app;
                                   else
                                       flags |= std::ios::trunc;

                                   std::ofstream file(fpath, flags);
                                   if (!file.is_open())
                                       return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
                                   file << content;
                                   file.close();

                                   return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(fpath.string())}});
                               }});

        const std::string requestsPath = Path::Assets + "Agent/REQUESTS.md";

        m_agent->RegisterTool({.name = "request_feature",
                               .description = "Request a new feature, tool, or Lua binding that is not currently available. "
                                              "The request will be shown to the user and saved to REQUESTS.md. "
                                              "Use this when you need a capability that doesn't exist yet.",
                               .properties = {
                                   {"title", "Short title for the feature request", pagent::SchemaType::String, true},
                                   {"description", "Detailed description of what is needed and why", pagent::SchemaType::String, true},
                               },
                               .handler = [this, requestsPath](const std::string &args) -> std::string
                               {
                                   std::string title = JsonUnescape(ExtractArgStr(args, "title"));
                                   std::string desc = JsonUnescape(ExtractArgStr(args, "description"));
                                   if (title.empty())
                                       return "{\"error\":\"missing title\"}";

                                   std::string msg = "[FEATURE REQUEST] " + title + ": " + desc;
                                   PE_WARN("%s", msg.c_str());

                                   {
                                       std::lock_guard lock(m_chatMutex);
                                       m_chat.push_back({ChatMessage::Role::System, msg, ""});
                                   }

                                   // Append to REQUESTS.md with blank line separator
                                   {
                                       std::filesystem::path parentDir = std::filesystem::path(requestsPath).parent_path();
                                       if (!std::filesystem::exists(parentDir))
                                       {
                                           std::error_code ec;
                                           std::filesystem::create_directories(parentDir, ec);
                                       }
                                       std::ofstream file(requestsPath, std::ios::app);
                                       if (file.is_open())
                                           file << "- **" << title << "**: " << desc << "\n\n";
                                   }

                                   return JsonObj({{"status", JsonStr("request saved to REQUESTS.md")}, {"title", JsonStr(title)}});
                               }});

        m_agent->RegisterTool({.name = "complete_feature",
                               .description = "Mark a feature request as completed and remove it from REQUESTS.md. "
                                              "Use this when a previously requested feature has been implemented.",
                               .properties = {
                                   {"title", "Title of the completed feature request to remove", pagent::SchemaType::String, true},
                               },
                               .handler = [requestsPath](const std::string &args) -> std::string
                               {
                                   std::string title = JsonUnescape(ExtractArgStr(args, "title"));
                                   if (title.empty())
                                       return "{\"error\":\"missing title\"}";

                                   if (!std::filesystem::exists(requestsPath))
                                       return "{\"error\":\"REQUESTS.md not found\"}";

                                   // Read current contents
                                   std::string content;
                                   {
                                       std::ifstream file(requestsPath, std::ios::in);
                                       if (!file.is_open())
                                           return "{\"error\":\"cannot open REQUESTS.md\"}";
                                       content.assign(std::istreambuf_iterator<char>(file),
                                                      std::istreambuf_iterator<char>());
                                   }

                                   // Find and remove the matching entry (line starting with "- **title**")
                                   std::string marker = "- **" + title + "**";
                                   auto pos = content.find(marker);
                                   if (pos == std::string::npos)
                                       return JsonObj({{"error", JsonStr("request not found: " + title)}});

                                   // Find end of this entry (next "- **" or end of file)
                                   auto entryEnd = content.find("\n- **", pos + marker.size());
                                   if (entryEnd == std::string::npos)
                                       entryEnd = content.size();
                                   else
                                       entryEnd += 1; // keep the newline before next entry

                                   content.erase(pos, entryEnd - pos);

                                   // Write back
                                   {
                                       std::ofstream file(requestsPath, std::ios::out | std::ios::trunc);
                                       if (!file.is_open())
                                           return "{\"error\":\"cannot write REQUESTS.md\"}";
                                       file << content;
                                   }

                                   return JsonObj({{"status", JsonStr("removed")}, {"title", JsonStr(title)}});
                               }});
    }

    void AgentWidget::QueueAction(std::function<void()> fn)
    {
        std::lock_guard lock(m_actionMutex);
        m_pendingActions.push_back(std::move(fn));
    }

    void AgentWidget::FetchAvailableModels(bool fetchRemote)
    {
        int providerIdx = m_selectedProviderIndex;

        // Use cache if available
        auto it = m_modelCache.find(providerIdx);
        if (it != m_modelCache.end())
        {
            m_availableModels = it->second.names;
            m_modelIsLocal = it->second.local;
            m_isFetchingModels = false;
            m_selectedModelIndex = 0;
            for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
            {
                if (m_availableModels[i] == m_modelName)
                {
                    m_selectedModelIndex = i;
                    break;
                }
            }
            return;
        }

        // Already fetching for this provider
        if (m_isFetchingModels)
            return;

        const auto &info = m_providers[providerIdx];
        auto provider = info.provider;
        auto apiKey = info.apiKey;
        auto currentModel = m_modelName;
        auto alive = m_alive;

        m_isFetchingModels = true;

        std::thread([this, alive, provider, apiKey, currentModel, providerIdx, fetchRemote]
                    {
            std::vector<pagent::Agent::ModelInfo> modelInfos;
            bool localOnly = (provider == pagent::Provider::Ollama) && !fetchRemote;
            int retries = (provider == pagent::Provider::Ollama) ? 5 : 1;
            for (int attempt = 0; attempt < retries; ++attempt)
            {
                if (!*alive)
                    return;
                if (attempt > 0)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                modelInfos = pagent::Agent::FetchModelInfos(provider, apiKey, "", localOnly);
                if (!modelInfos.empty())
                    break;
            }

            if (!*alive)
                return;

            std::vector<std::string> names;
            std::vector<bool> localFlags;
            for (auto &mi : modelInfos)
            {
                names.push_back(std::move(mi.name));
                localFlags.push_back(mi.local);
            }

            if (!currentModel.empty() && std::find(names.begin(), names.end(), currentModel) == names.end())
            {
                names.insert(names.begin(), currentModel);
                localFlags.insert(localFlags.begin(), provider != pagent::Provider::Ollama);
            }

            if (!*alive)
                return;

            QueueAction([this, alive, names = std::move(names), localFlags = std::move(localFlags), currentModel, providerIdx]
                        {
                if (!*alive)
                    return;
                m_isFetchingModels = false;
                // Cache the results
                m_modelCache[providerIdx] = {names, localFlags};
                // Only apply if still on this provider
                if (m_selectedProviderIndex != providerIdx)
                    return;
                m_availableModels = names;
                m_modelIsLocal = localFlags;
                m_selectedModelIndex = 0;
                if (m_availableModels.empty())
                {
                    m_modelName.clear();
                }
                else
                {
                    for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                    {
                        if (m_availableModels[i] == currentModel)
                        {
                            m_selectedModelIndex = i;
                            break;
                        }
                    }
                    if (m_modelName.empty() || std::find(m_availableModels.begin(), m_availableModels.end(), m_modelName) == m_availableModels.end())
                    {
                        m_modelName = m_availableModels[m_selectedModelIndex];
                        if (m_agent)
                            m_agent->SetModel(m_modelName);
                    }
                }
            }); })
            .detach();
    }

    void AgentWidget::FlushActions()
    {
        std::vector<std::function<void()>> actions;
        {
            std::lock_guard lock(m_actionMutex);
            actions.swap(m_pendingActions);
        }
        for (auto &fn : actions)
            fn();
    }

    void AgentWidget::Update()
    {
        FlushActions(); // deferred engine writes before drawing
        if (m_agent && m_open)
            m_agent->Poll();

        if (!m_open)
        {
            if (m_agentConfigured)
            {
                pagent::Agent::CancelPull(m_pullCancel);
                m_pullCancel.reset();
                m_isPulling = false;
                if (m_agent)
                    m_agent->CancelPending();
                m_agentConfigured = false;
            }
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }

        const bool busy = m_isExternalAI ? m_isStreaming : (m_agent && m_agent->IsBusy());

        // status bar
        {
            const float r = 5.0f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 center(pos.x + r, pos.y + ImGui::GetTextLineHeight() * 0.5f);
            const ImU32 dotColor = !m_agentConfigured ? IM_COL32(100, 100, 100, 255)
                                   : busy             ? IM_COL32(255, 200, 50, 255)
                                                      : IM_COL32(80, 220, 100, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(center, r, dotColor);
            ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            // Provider selector combo
            {
                ImGui::PushItemWidth(100.0f);
                const auto &curProvider = m_providers[m_selectedProviderIndex];
                if (ImGui::BeginCombo("##provider", curProvider.name.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(m_providers.size()); ++i)
                    {
                        const bool selected = (i == m_selectedProviderIndex);
                        if (ImGui::Selectable(m_providers[i].name.c_str(), selected))
                        {
                            if (i != m_selectedProviderIndex)
                            {
                                // Unload previous Ollama model when switching providers
                                if (m_ollamaModelLoaded &&
                                    m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama &&
                                    !m_modelName.empty())
                                {
                                    std::string prev = m_modelName;
                                    std::thread([prev]()
                                                { pagent::Agent::UnloadModel(pagent::Provider::Ollama, prev); })
                                        .detach();
                                    m_ollamaModelLoaded = false;
                                }
                                m_selectedProviderIndex = i;
                                m_modelName.clear(); // let ConfigureAgent use provider default
                                m_isFetchingModels = false;
                                ConfigureAgent(m_providers[i].provider);
                                FetchAvailableModels();
                                SaveConfig();
                            }
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
            }
            ImGui::SameLine();
            // Model selector / External file input
            if (m_isExternalAI)
            {
                ImGui::PushItemWidth(200.0f);
                if (ImGui::InputText("##externalfile", m_externalFile, sizeof(m_externalFile),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    m_modelName = m_externalFile;
                    UpdateExternalFileWatch();
                }
                if (ImGui::IsItemHovered())
                {
                    std::string responseFile = std::filesystem::path(m_externalFile).stem().string() + "_response" + std::filesystem::path(m_externalFile).extension().string();
                    ImGui::SetTooltip(
                        "File-based IPC for external AI tools (Claude Code, Cursor, etc.).\n\n"
                        "How it works:\n"
                        "  1. Your message is written to: Assets/Agent/%s\n"
                        "  2. An external tool reads that file and writes a response to:\n"
                        "     Assets/Agent/%s\n"
                        "  3. The response is picked up automatically via file watcher.\n\n"
                        "You can rename this file to anything you like.\n"
                        "Press Enter to apply the new name.",
                        m_externalFile, responseFile.c_str());
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "External AI provider uses file-based communication.\n"
                        "Set up a script or AI tool to watch the input file\n"
                        "and write responses to the corresponding response file.");
                ImGui::PopItemWidth();
            }
            else
            {
                std::string comboLabel = m_isPulling                 ? (m_modelName + " (downloading...)")
                                         : m_availableModels.empty() ? "None"
                                         : m_modelName.empty()       ? "None"
                                                                     : m_modelName;
                ImGui::PushItemWidth(200.0f);
                if (ImGui::BeginCombo("##model", comboLabel.c_str()))
                {
                    // Filter input at the top of the dropdown
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint("##modelfilter", "Filter...", m_modelFilter, sizeof(m_modelFilter));

                    std::string filter = m_modelFilter;
                    for (auto &c : filter)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                    {
                        // Apply filter
                        if (!filter.empty())
                        {
                            std::string nameLower = m_availableModels[i];
                            for (auto &c : nameLower)
                                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            if (nameLower.find(filter) == std::string::npos)
                                continue;
                        }

                        const bool selected = (i == m_selectedModelIndex);
                        bool isLocal = i < static_cast<int>(m_modelIsLocal.size()) ? m_modelIsLocal[i] : true;

                        std::string displayName = m_availableModels[i];
                        if (!isLocal)
                            displayName += " (download)";

                        if (ImGui::Selectable(displayName.c_str(), selected))
                        {
                            // Unload previous Ollama model before switching
                            std::string prevModel = m_modelName;
                            m_selectedModelIndex = i;
                            m_modelName = m_availableModels[i];
                            m_modelFilter[0] = '\0';

                            if (isLocal)
                            {
                                if (m_ollamaModelLoaded && !prevModel.empty() && prevModel != m_modelName &&
                                    m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama)
                                {
                                    std::thread([prevModel]()
                                                { pagent::Agent::UnloadModel(pagent::Provider::Ollama, prevModel); })
                                        .detach();
                                    m_ollamaModelLoaded = false;
                                }
                                m_agent->SetModel(m_modelName);
                                SaveConfig();
                            }
                            else if (!m_isPulling)
                            {
                                m_isPulling = true;
                                std::string pullModel = m_modelName;
                                {
                                    std::lock_guard lock(m_chatMutex);
                                    m_chat.push_back({ChatMessage::Role::System, "Downloading model " + pullModel + "..."});
                                    m_scrollToBottom = true;
                                }
                                auto aliveRef = m_alive;
                                m_pullCancel = pagent::Agent::PullModel(pullModel, [this, aliveRef](const std::string &status)
                                                                        { if (!*aliveRef) return;
                                                                          QueueAction([this, aliveRef, status]()
                                                                                      {
                                            if (!*aliveRef) return;
                                            std::lock_guard lock(m_chatMutex);
                                            if (!m_chat.empty() && m_chat.back().role == ChatMessage::Role::System)
                                                m_chat.back().text = status;
                                            m_scrollToBottom = true; }); }, [this, aliveRef, i, pullModel](bool success)
                                                                        {
                                        if (!*aliveRef) return;
                                        // Check tool support on background thread before queuing UI update
                                        auto caps = success ? pagent::Agent::QueryCapabilities(
                                            m_providers[m_selectedProviderIndex].provider, pullModel)
                                            : pagent::Agent::ModelCaps{false, false};

                                        if (!*aliveRef) return;
                                        QueueAction([this, aliveRef, i, pullModel, success, caps]()
                                        {
                                            m_isPulling = false;
                                            m_pullCancel.reset();
                                            std::lock_guard lock(m_chatMutex);
                                            if (!success)
                                            {
                                                m_chat.push_back({ChatMessage::Role::System, "Download cancelled."});
                                            }
                                            else if (!caps.vision || !caps.tools)
                                            {
                                                // Remove from list -- model doesn't support vision+tools
                                                if (i < static_cast<int>(m_availableModels.size()))
                                                {
                                                    m_availableModels.erase(m_availableModels.begin() + i);
                                                    m_modelIsLocal.erase(m_modelIsLocal.begin() + i);
                                                    if (m_selectedModelIndex >= static_cast<int>(m_availableModels.size()))
                                                        m_selectedModelIndex = 0;
                                                }
                                                // Update cache after removal
                                                m_modelCache[m_selectedProviderIndex] = {m_availableModels, m_modelIsLocal};
                                                std::string reason = !caps.vision ? "vision" : "tool calling";
                                                m_chat.push_back({ChatMessage::Role::System,
                                                    pullModel + " does not support " + reason + ". Removed from list."});
                                            }
                                            else
                                            {
                                                if (i < static_cast<int>(m_modelIsLocal.size()))
                                                    m_modelIsLocal[i] = true;
                                                // Update cache so model stays local on provider switch
                                                m_modelCache[m_selectedProviderIndex] = {m_availableModels, m_modelIsLocal};
                                                m_agent->SetModel(pullModel);
                                                m_chat.push_back({ChatMessage::Role::System, "Model ready."});
                                                SaveConfig();
                                            }
                                            m_scrollToBottom = true;
                                        }); });
                            }
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                // Cancel button while downloading
                if (m_isPulling)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cancel"))
                        pagent::Agent::CancelPull(m_pullCancel);
                }
                else if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama &&
                         !m_isExternalAI)
                {
                    // Unload button — only shown when model is loaded
                    if (m_ollamaModelLoaded)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Unload"))
                        {
                            pagent::Agent::UnloadModel(pagent::Provider::Ollama, m_modelName);
                            m_ollamaModelLoaded = false;
                            std::lock_guard lock(m_chatMutex);
                            m_chat.push_back({ChatMessage::Role::System, "Model " + m_modelName + " unloaded from GPU."});
                            m_scrollToBottom = true;
                        }
                    }
                    // Fetch button — fetches remote models from ollama.com
                    ImGui::SameLine();
                    bool fetching = m_isFetchingModels || m_isFetchingEmbeddingModels;
                    if (fetching)
                        ImGui::BeginDisabled();
                    if (ImGui::SmallButton(fetching ? "Fetching...##agent" : "Fetch##agent"))
                    {
                        m_modelCache.erase(m_selectedProviderIndex);
                        FetchAvailableModels(true);
                        if (m_embeddingEnabled)
                            UpdateEmbeddingModels(true);
                    }
                    if (fetching)
                        ImGui::EndDisabled();
                }
                ImGui::PopItemWidth();
            }
            // Token usage display
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            {
                auto usage = m_agent->GetUsage();
                char buf[256];
                snprintf(buf, sizeof(buf), "tokens [in/out]: turn [%dk/%dk]  total: [%dk/%dk]  cached: [%dk]",
                         usage.turnInput / 1000, usage.turnOutput / 1000,
                         usage.totalInput / 1000, usage.totalOutput / 1000,
                         usage.totalCacheRead / 1000);
                ImGui::TextUnformatted(buf);
            }
            ImGui::PopStyleColor();
        }

        // Embedding row (checkbox + provider + model) — right under agent row
        if (!m_isExternalAI)
        {
            if (ImGui::Checkbox("RAG", &m_embeddingEnabled))
            {
                if (m_embeddingEnabled)
                    UpdateEmbeddingModels();
                SaveConfig();
                ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Enable retrieval-augmented generation (RAG).\nEmbeds messages and retrieves relevant past context.");

            if (m_embeddingEnabled)
            {
                static const char *embeddingProviders[] = {"Google", "OpenAI", "Ollama"};

                ImGui::SameLine();
                ImGui::PushItemWidth(100.0f);
                if (ImGui::BeginCombo("##embprov", embeddingProviders[m_selectedEmbeddingProvider]))
                {
                    for (int i = 0; i < 3; i++)
                    {
                        bool available = true;
                        if (i == 0)
                            available = std::getenv("PAGENT_GEMINI_API_KEY") != nullptr;
                        if (i == 1)
                            available = std::getenv("PAGENT_OPENAI_API_KEY") != nullptr;

                        if (!available)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

                        if (ImGui::Selectable(embeddingProviders[i], i == m_selectedEmbeddingProvider, available ? 0 : ImGuiSelectableFlags_Disabled))
                        {
                            // Save current vector store before switching
                            auto sp = GetVectorStorePath();
                            if (m_vectorStore && !sp.empty())
                                m_vectorStore->SaveToFile(sp);
                            m_vectorStore.reset();
                            m_selectedEmbeddingProvider = i;
                            UpdateEmbeddingModels();
                            SaveConfig();
                            ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
                        }

                        if (!available)
                            ImGui::PopStyleColor();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                {
                    ImGui::SameLine();
                    ImGui::PushItemWidth(250.0f);
                    std::string embComboLabel;
                    if (m_isPullingEmbedding && m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size()))
                        embComboLabel = m_embeddingModels[m_selectedEmbeddingModel] + " (downloading...)";
                    else if (m_embeddingModels.empty() || m_selectedEmbeddingModel >= static_cast<int>(m_embeddingModels.size()))
                        embComboLabel = "None";
                    else
                        embComboLabel = m_embeddingModels[m_selectedEmbeddingModel];
                    if (ImGui::BeginCombo("##embmodel", embComboLabel.c_str()))
                    {
                        for (int i = 0; i < static_cast<int>(m_embeddingModels.size()); i++)
                        {
                            bool isLocal = i < static_cast<int>(m_embeddingModelIsLocal.size()) && m_embeddingModelIsLocal[i];
                            std::string label = m_embeddingModels[i];
                            if (!isLocal && m_selectedEmbeddingProvider == 2)
                                label += " (download)";

                            if (ImGui::Selectable(label.c_str(), i == m_selectedEmbeddingModel))
                            {
                                // Save current vector store before switching
                                auto sp2 = GetVectorStorePath();
                                if (m_vectorStore && !sp2.empty())
                                    m_vectorStore->SaveToFile(sp2);
                                m_vectorStore.reset();
                                m_selectedEmbeddingModel = i;
                                SaveConfig();

                                // For Ollama, auto-pull if not local
                                if (m_selectedEmbeddingProvider == 2 && !isLocal && !m_isPullingEmbedding)
                                {
                                    std::string pullModel = m_embeddingModels[i];
                                    auto alive = m_alive;
                                    m_isPullingEmbedding = true;
                                    {
                                        std::lock_guard lock(m_chatMutex);
                                        m_chat.push_back({ChatMessage::Role::System, "Downloading embedding model " + pullModel + "..."});
                                        m_scrollToBottom = true;
                                    }
                                    m_pullEmbeddingCancel = pagent::Agent::PullModel(pullModel, [this, alive](const std::string &status)
                                                                                     {
                                            if (!*alive) return;
                                            QueueAction([this, alive, status]()
                                            {
                                                if (!*alive) return;
                                                std::lock_guard lock(m_chatMutex);
                                                if (!m_chat.empty() && m_chat.back().role == ChatMessage::Role::System)
                                                    m_chat.back().text = status;
                                                m_scrollToBottom = true;
                                            }); }, [this, alive, pullModel, i](bool success)
                                                                                     {
                                            if (!*alive) return;
                                            QueueAction([this, alive, success, pullModel, i]()
                                            {
                                                m_isPullingEmbedding = false;
                                                m_pullEmbeddingCancel.reset();
                                                if (success)
                                                {
                                                    if (i < static_cast<int>(m_embeddingModelIsLocal.size()))
                                                        m_embeddingModelIsLocal[i] = true;
                                                    ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
                                                    std::lock_guard lock(m_chatMutex);
                                                    m_chat.push_back({ChatMessage::Role::System,
                                                        "Embedding model " + pullModel + " ready."});
                                                }
                                                else
                                                {
                                                    std::lock_guard lock(m_chatMutex);
                                                    m_chat.push_back({ChatMessage::Role::System,
                                                        "Failed to download embedding model " + pullModel + "."});
                                                }
                                                m_scrollToBottom = true;
                                            }); });
                                }
                                else
                                {
                                    ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // Fetch button for Ollama embedding models
                    if (m_selectedEmbeddingProvider == 2)
                    {
                        ImGui::SameLine();
                        bool fetching = m_isFetchingEmbeddingModels || m_isFetchingModels;
                        if (fetching)
                            ImGui::BeginDisabled();
                        if (ImGui::SmallButton(fetching ? "Fetching...##emb" : "Fetch##emb"))
                        {
                            m_modelCache.erase(m_selectedProviderIndex);
                            if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama)
                                FetchAvailableModels(true);
                            UpdateEmbeddingModels(true);
                        }
                        if (fetching)
                            ImGui::EndDisabled();
                    }
                }
            }
        }
        ImGui::Separator();

        // chat log
        const float statusHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        const float inputHeight = ImGui::GetTextLineHeight() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
        const bool hasAttachments = !m_pendingImages.empty() || !m_pendingFiles.empty();
        const float pendingImgHeight = hasAttachments ? (60.0f + ImGui::GetStyle().ItemSpacing.y) : 0.0f;
        ImGui::BeginChild("ChatLog", ImVec2(0, -(inputHeight + statusHeight + pendingImgHeight)), false);
        {
            std::lock_guard lock(m_chatMutex);
            for (const auto &msg : m_chat)
                RenderMessage(msg);

            if (m_isStreaming)
            {
                // Show thinking in a dimmed collapsible section
                if (!m_streamingThinking.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
                    if (ImGui::TreeNode("Thinking..."))
                    {
                        ImGui::TextWrapped("%s", m_streamingThinking.c_str());
                        ImGui::TreePop();
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                if (!m_streamingText.empty())
                {
                    ImGui::TextWrapped("%s", m_streamingText.c_str());
                }
                else if (m_streamingThinking.empty())
                {
                    // animated dots while waiting for first token
                    const int dots = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
                    const char *anim[] = {"[AI] .", "[AI] ..", "[AI] ...", "[AI] .."};
                    ImGui::TextUnformatted(anim[dots]);
                }
                ImGui::PopStyleColor();
            }

            if (m_scrollToBottom)
            {
                ImGui::SetScrollHereY(1.0f);
                m_scrollToBottom = false;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Ctrl+V image paste (only when not busy and not in external mode)
#if defined(PE_WIN32)
        if (!busy && !m_isExternalAI && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
        {
            // Check if clipboard has an image before consuming the paste
            UINT cfPng = RegisterClipboardFormatA("PNG");
            bool hasPng = cfPng && IsClipboardFormatAvailable(cfPng);
            bool hasDib = IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_DIB);
            bool hasFiles = IsClipboardFormatAvailable(CF_HDROP) != 0;
            if (hasPng || hasDib || hasFiles)
                HandlePaste();
        }
#endif

        RenderPendingAttachments();

        bool submit = false;
        // Enter sends. Shift+Enter inserts a newline. Up/Down for history.
        ImGui::BeginDisabled(busy);
        const float inputWidth = ImGui::GetContentRegionAvail().x - 60.0f;

        auto inputCallback = [](ImGuiInputTextCallbackData *data) -> int
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
            {
                // Block Enter (newline) unless Shift is held
                if (data->EventChar == '\n' && !ImGui::GetIO().KeyShift)
                    return 1;
            }
            return 0;
        };

        ImGui::InputTextMultiline("##input", m_inputBuf, sizeof(m_inputBuf),
                                  ImVec2(inputWidth, inputHeight),
                                  ImGuiInputTextFlags_CallbackCharFilter,
                                  inputCallback, nullptr);
        bool inputActive = ImGui::IsItemActive();

        // Up/Down arrow history (handled outside callback since Multiline + CallbackHistory is not allowed)
        if (inputActive && !m_inputHistory.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                if (m_historyIndex < 0)
                    m_historyIndex = static_cast<int>(m_inputHistory.size()) - 1;
                else if (m_historyIndex > 0)
                    m_historyIndex--;
                strncpy(m_inputBuf, m_inputHistory[m_historyIndex].c_str(), sizeof(m_inputBuf) - 1);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                if (m_historyIndex >= 0)
                {
                    m_historyIndex++;
                    if (m_historyIndex >= static_cast<int>(m_inputHistory.size()))
                    {
                        m_historyIndex = -1;
                        m_inputBuf[0] = '\0';
                    }
                    else
                        strncpy(m_inputBuf, m_inputHistory[m_historyIndex].c_str(), sizeof(m_inputBuf) - 1);
                }
            }
        }

        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().KeyShift)
            submit = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (busy)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(55, 0)))
            {
                if (m_isExternalAI)
                    m_isStreaming = false;
                else
                    m_agent->CancelPending();
            }
            ImGui::PopStyleColor(3);
        }
        else
        {
            if (ImGui::Button("Send", ImVec2(55, 0)))
                submit = true;
        }

        if (submit && m_inputBuf[0] != '\0')
            SubmitInput();

        ImGui::End();
    }

    void AgentWidget::SubmitInput()
    {
        std::string text(m_inputBuf);
        m_inputBuf[0] = '\0';
        m_historyIndex = -1;

        // Prepend file contents to the message
        std::string fileContext;
        for (const auto &pf : m_pendingFiles)
            fileContext += "--- " + pf.name + " ---\n" + pf.content + "\n\n";

        std::string fullText = fileContext.empty() ? text : fileContext + text;

        m_inputHistory.push_back(text);

        // Build chat message for display
        ChatMessage chatMsg;
        chatMsg.role = ChatMessage::Role::User;
        chatMsg.text = text;
        for (const auto &img : m_pendingImages)
            chatMsg.images.push_back({img.imguiDescriptor, img.width, img.height});
        // Show attached file names
        for (const auto &pf : m_pendingFiles)
            chatMsg.text += "\n[Attached: " + pf.name + "]";

        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back(std::move(chatMsg));
            m_scrollToBottom = true;
        }

        if (m_isExternalAI)
        {
            std::string inputPath = Path::Assets + "Agent/" + m_externalFile;
            std::ofstream f(inputPath, std::ios::trunc);
            f << fullText;
            f.close();
            std::ofstream(GetExternalResponsePath(), std::ios::trunc).close();
            m_isStreaming = true;
            WriteExternalHistory();
        }
        else
        {
            std::vector<pagent::ContentPart> attachments;
            for (const auto &img : m_pendingImages)
                attachments.push_back({pagent::ContentPart::Type::ImageBase64, img.base64, img.mime_type});

            if (attachments.empty())
                m_agent->Send(fullText);
            else
                m_agent->Send(fullText, attachments);

            if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama)
                m_ollamaModelLoaded = true;
        }
        m_pendingImages.clear();
        m_pendingFiles.clear();
        ImGui::SetKeyboardFocusHere(-1);
    }

    void AgentWidget::HandlePaste()
    {
#if defined(PE_WIN32)
        if (!OpenClipboard(nullptr))
            return;

        int w = 0, h = 0;
        std::string base64;
        std::string mime_type;

        // Try PNG clipboard format first (browsers, chat apps, image viewers)
        UINT cfPng = RegisterClipboardFormatA("PNG");
        HANDLE hPng = cfPng ? GetClipboardData(cfPng) : nullptr;
        if (hPng)
        {
            size_t dataSize = GlobalSize(hPng);
            const uint8_t *data = static_cast<const uint8_t *>(GlobalLock(hPng));
            if (data && dataSize > 24)
            {
                // Read dimensions from PNG IHDR chunk (bytes 16-23, big-endian)
                w = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
                h = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
                base64 = pagent::Base64Encode(data, dataSize);
                mime_type = "image/png";
            }
            if (data)
                GlobalUnlock(hPng);
        }

        // Try CF_HDROP: files copied from Explorer
        if (base64.empty())
        {
            HANDLE hDrop = GetClipboardData(CF_HDROP);
            if (hDrop)
            {
                HDROP drop = static_cast<HDROP>(hDrop);
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT fi = 0; fi < count; fi++)
                {
                    wchar_t filePath[MAX_PATH] = {};
                    DragQueryFileW(drop, fi, filePath, MAX_PATH);
                    std::filesystem::path fp(filePath);

                    if (!std::filesystem::is_regular_file(fp))
                        continue;
                    auto fileSize = std::filesystem::file_size(fp);
                    if (fileSize == 0 || fileSize > 5 * 1024 * 1024) // skip empty or >5MB
                        continue;

                    std::string ext = fp.extension().string();
                    for (auto &c : ext)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    // Image files
                    bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                                    ext == ".bmp" || ext == ".tga" || ext == ".gif" || ext == ".webp");
                    if (isImage)
                    {
                        if (ext == ".png")
                            mime_type = "image/png";
                        else if (ext == ".jpg" || ext == ".jpeg")
                            mime_type = "image/jpeg";
                        else if (ext == ".gif")
                            mime_type = "image/gif";
                        else if (ext == ".webp")
                            mime_type = "image/webp";
                        else
                            mime_type = "image/png";

                        std::ifstream file(fp, std::ios::binary | std::ios::ate);
                        if (!file.is_open())
                            continue;
                        size_t fsize = file.tellg();
                        file.seekg(0);
                        std::vector<uint8_t> fileData(fsize);
                        file.read(reinterpret_cast<char *>(fileData.data()), fsize);

                        if (ext == ".png")
                        {
                            if (fsize > 24)
                            {
                                w = (fileData[16] << 24) | (fileData[17] << 16) | (fileData[18] << 8) | fileData[19];
                                h = (fileData[20] << 24) | (fileData[21] << 16) | (fileData[22] << 8) | fileData[23];
                            }
                            base64 = pagent::Base64Encode(fileData.data(), fileData.size());
                        }
                        else if (ext == ".jpg" || ext == ".jpeg")
                        {
                            w = 0;
                            h = 0;
                            for (size_t i = 0; i + 8 < fsize; i++)
                            {
                                if (fileData[i] == 0xFF && (fileData[i + 1] >= 0xC0 && fileData[i + 1] <= 0xC3))
                                {
                                    h = (fileData[i + 5] << 8) | fileData[i + 6];
                                    w = (fileData[i + 7] << 8) | fileData[i + 8];
                                    break;
                                }
                            }
                            if (w == 0)
                            {
                                w = 512;
                                h = 512;
                            }
                            base64 = pagent::Base64Encode(fileData.data(), fileData.size());
                        }
                        else
                        {
                            int comp = 0;
                            auto *pixels = stbi_load_from_memory(fileData.data(), static_cast<int>(fsize), &w, &h, &comp, 4);
                            if (pixels)
                            {
                                auto pngData = pagent::EncodeRGBA_PNG(pixels, w, h);
                                base64 = pagent::Base64Encode(pngData.data(), pngData.size());
                                mime_type = "image/png";
                                stbi_image_free(pixels);
                            }
                        }

                        if (!base64.empty())
                        {
                            PendingImage img;
                            img.base64 = std::move(base64);
                            img.mime_type = std::move(mime_type);
                            img.width = w;
                            img.height = h;
                            m_pendingImages.push_back(std::move(img));
                            base64.clear();
                        }
                    }
                    else
                    {
                        // Text/code file — read content (truncate at 20k chars to avoid token limits)
                        constexpr size_t maxChars = 20000;
                        std::ifstream file(fp, std::ios::in);
                        if (!file.is_open())
                            continue;
                        std::string content((std::istreambuf_iterator<char>(file)), {});
                        if (!content.empty())
                        {
                            PendingFile pf;
                            pf.name = fp.filename().string();
                            if (content.size() > maxChars)
                            {
                                content.resize(maxChars);
                                content += "\n... [truncated at 20k chars]";
                            }
                            pf.content = std::move(content);
                            m_pendingFiles.push_back(std::move(pf));
                        }
                    }
                }
            }
        }

        // Fall back to DIB format (screenshots, PrintScreen, paint apps)
        if (base64.empty())
        {
            HANDLE hData = GetClipboardData(CF_DIBV5);
            if (!hData)
                hData = GetClipboardData(CF_DIB);
            if (!hData)
            {
                CloseClipboard();
                return;
            }

            auto *bmi = static_cast<BITMAPINFOHEADER *>(GlobalLock(hData));
            if (!bmi || (bmi->biCompression != BI_RGB && bmi->biCompression != BI_BITFIELDS) || bmi->biBitCount < 24)
            {
                if (bmi)
                    GlobalUnlock(hData);
                CloseClipboard();
                return;
            }

            w = bmi->biWidth;
            h = std::abs(bmi->biHeight);
            bool topDown = bmi->biHeight < 0;
            int bpp = bmi->biBitCount / 8;
            size_t pixelOffset = bmi->biSize;
            if (bmi->biCompression == BI_BITFIELDS && bmi->biSize == sizeof(BITMAPINFOHEADER))
                pixelOffset += 3 * sizeof(DWORD);
            const uint8_t *pixels = reinterpret_cast<const uint8_t *>(bmi) + pixelOffset;

            int srcStride = ((w * bpp + 3) & ~3);
            std::vector<uint8_t> rgba(w * h * 4);
            for (int y = 0; y < h; y++)
            {
                int srcY = topDown ? y : (h - 1 - y);
                const uint8_t *srcRow = pixels + srcY * srcStride;
                uint8_t *dstRow = rgba.data() + y * w * 4;
                for (int x = 0; x < w; x++)
                {
                    dstRow[x * 4 + 0] = srcRow[x * bpp + 2]; // R
                    dstRow[x * 4 + 1] = srcRow[x * bpp + 1]; // G
                    dstRow[x * 4 + 2] = srcRow[x * bpp + 0]; // B
                    dstRow[x * 4 + 3] = (bpp == 4) ? srcRow[x * bpp + 3] : 255;
                }
            }
            GlobalUnlock(hData);

            // Resize if too large (max 1024px on longest side)
            if (w > 1024 || h > 1024)
            {
                float scale = 1024.0f / static_cast<float>(std::max(w, h));
                int nw = static_cast<int>(w * scale);
                int nh = static_cast<int>(h * scale);
                std::vector<uint8_t> resized(nw * nh * 4);
                for (int dy = 0; dy < nh; dy++)
                {
                    int sy = dy * h / nh;
                    for (int dx = 0; dx < nw; dx++)
                    {
                        int sx = dx * w / nw;
                        std::memcpy(&resized[(dy * nw + dx) * 4], &rgba[(sy * w + sx) * 4], 4);
                    }
                }
                rgba = std::move(resized);
                w = nw;
                h = nh;
            }

            auto pngData = pagent::EncodeRGBA_PNG(rgba.data(), w, h);
            base64 = pagent::Base64Encode(pngData.data(), pngData.size());
            mime_type = "image/png";
        }

        CloseClipboard();

        if (base64.empty())
            return;

        PendingImage img;
        img.base64 = std::move(base64);
        img.mime_type = std::move(mime_type);
        img.width = w;
        img.height = h;
        img.imguiDescriptor = nullptr; // TODO: create Vulkan texture for thumbnail preview
        m_pendingImages.push_back(std::move(img));
#endif
    }

    void AgentWidget::RenderPendingAttachments()
    {
        if (m_pendingImages.empty() && m_pendingFiles.empty())
            return;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.2f, 1.0f));
        ImGui::BeginChild("PendingAttachments", ImVec2(0, 60), true);

        for (size_t i = 0; i < m_pendingImages.size(); i++)
        {
            const auto &img = m_pendingImages[i];
            ImGui::Text("[Image %dx%d]", img.width, img.height);
            ImGui::SameLine();
            std::string removeLabel = "X##rmimg" + std::to_string(i);
            if (ImGui::SmallButton(removeLabel.c_str()))
            {
                m_pendingImages.erase(m_pendingImages.begin() + i);
                i--;
                continue;
            }
            ImGui::SameLine();
        }

        for (size_t i = 0; i < m_pendingFiles.size(); i++)
        {
            const auto &pf = m_pendingFiles[i];
            ImGui::Text("[%s (%d bytes)]", pf.name.c_str(), static_cast<int>(pf.content.size()));
            ImGui::SameLine();
            std::string removeLabel = "X##rmfile" + std::to_string(i);
            if (ImGui::SmallButton(removeLabel.c_str()))
            {
                m_pendingFiles.erase(m_pendingFiles.begin() + i);
                i--;
                continue;
            }
            ImGui::SameLine();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    std::string AgentWidget::GetExternalResponsePath() const
    {
        std::filesystem::path p(m_externalFile);
        std::string parent = p.parent_path().string();
        std::string stem = p.stem().string();
        std::string ext = p.extension().string();
        if (ext.empty())
            ext = ".txt";
        std::string rel = (parent.empty() ? "" : parent + "/") + stem + "_response" + ext;
        return Path::Assets + "Agent/" + rel;
    }

    void AgentWidget::UpdateExternalFileWatch()
    {
        std::string agentDir = Path::Assets + "Agent/";
        std::string responsePath = GetExternalResponsePath();

        // Remove previous watch if path changed
        if (!m_externalResponsePath.empty() && m_externalResponsePath != responsePath)
            FileWatcher::Erase(m_externalResponsePath);
        m_externalResponsePath = responsePath;

        // Ensure parent directories and files exist
        std::string inputPath = agentDir + m_externalFile;
        for (const auto &path : {inputPath, responsePath})
        {
            auto parent = std::filesystem::path(path).parent_path();
            if (!std::filesystem::exists(parent))
                std::filesystem::create_directories(parent);
            if (!std::filesystem::exists(path))
                std::ofstream(path).close();
        }

        // Watch response file
        FileWatcher::Add(responsePath, [this](size_t)
                         { QueueAction([this]()
                                       { PollExternalResponse(); }); });
    }

    void AgentWidget::PollExternalResponse()
    {
        if (!m_isExternalAI || !m_isStreaming)
            return;

        const auto &responsePath = m_externalResponsePath;
        if (!std::filesystem::exists(responsePath))
            return;

        // Check if file has content
        auto fileSize = std::filesystem::file_size(responsePath);
        if (fileSize == 0)
            return;

        std::ifstream f(responsePath);
        if (!f.is_open())
            return;

        std::string response((std::istreambuf_iterator<char>(f)), {});
        f.close();

        if (response.empty())
            return;

        // Clear the response file so we don't re-read it
        std::ofstream(responsePath, std::ios::trunc).close();

        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back({ChatMessage::Role::Assistant, response});
            m_scrollToBottom = true;
        }
        m_isStreaming = false;
        WriteExternalHistory();
    }

    void AgentWidget::WriteExternalHistory()
    {
        std::string historyPath = std::filesystem::path(Path::Assets + "Agent/" + m_externalFile)
                                      .parent_path()
                                      .string() +
                                  "/chat_history.txt";
        std::ofstream f(historyPath, std::ios::trunc);
        if (!f.is_open())
            return;

        std::lock_guard lock(m_chatMutex);
        for (const auto &msg : m_chat)
        {
            const char *role = msg.role == ChatMessage::Role::User        ? "USER"
                               : msg.role == ChatMessage::Role::Assistant ? "ASSISTANT"
                                                                          : "SYSTEM";
            f << "[" << role << "]\n"
              << msg.text << "\n\n";
        }
    }

    void AgentWidget::OnAgentEvent(const pagent::AgentEvent &ev)
    {
        std::lock_guard lock(m_chatMutex);
        switch (ev.type)
        {
        case pagent::AgentEventType::ThinkingDelta:
            m_streamingThinking += ev.text;
            m_isStreaming = true;
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::ThinkingComplete:
            // Thinking is stored and will be attached to the next TextComplete message
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::TextDelta:
            m_streamingText += ev.text;
            m_isStreaming = true;
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::TextComplete:
        {
            ChatMessage msg;
            msg.role = ChatMessage::Role::Assistant;
            msg.text = ev.text;
            msg.thinking = m_streamingThinking;
            if (!msg.text.empty() || !msg.thinking.empty())
                m_chat.push_back(std::move(msg));
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_isStreaming = false;
            m_scrollToBottom = true;
            break;
        }
        case pagent::AgentEventType::ToolCallBegin:
            // Flush any accumulated thinking before tool calls
            if (!m_streamingThinking.empty())
            {
                m_chat.push_back({ChatMessage::Role::Assistant, "", m_streamingThinking});
                m_streamingThinking.clear();
            }
            m_chat.push_back({ChatMessage::Role::System, "[calling: " + ev.tool_name + "]"});
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::ToolCallComplete:
        {
            if (!ev.tool_input_json.empty())
                m_chat.push_back({ChatMessage::Role::System, ev.tool_input_json});
            m_scrollToBottom = true;
            break;
        }
        case pagent::AgentEventType::Usage:
            break; // handled internally by Agent::Impl::Poll()
        case pagent::AgentEventType::TurnComplete:
            m_isStreaming = false;
            m_streamingText.clear();
            m_streamingThinking.clear();
            // Save vector store periodically
            if (m_vectorStore && m_embeddingEnabled)
            {
                m_turnsSinceSave++;
                if (m_turnsSinceSave == 1 || m_turnsSinceSave >= 10)
                {
                    auto sp = GetVectorStorePath();
                    if (!sp.empty())
                        m_vectorStore->SaveToFile(sp);
                    if (m_turnsSinceSave >= 10)
                        m_turnsSinceSave = 0;
                }
            }
            break;
        case pagent::AgentEventType::Error:
            m_chat.push_back({ChatMessage::Role::System, "[error: " + ev.error_message + "]"});
            m_isStreaming = false;
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_scrollToBottom = true;
            break;
        default:
            break;
        }
    }

    void AgentWidget::RenderMessage(const ChatMessage &msg)
    {
        switch (msg.role)
        {
        case ChatMessage::Role::User:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::TextWrapped("[You] %s", msg.text.c_str());
            ImGui::PopStyleColor();
            // Show attached image indicators
            for (const auto &img : msg.images)
            {
                if (img.imguiDescriptor)
                {
                    float maxW = ImGui::GetContentRegionAvail().x * 0.5f;
                    float scale = std::min(1.0f, maxW / static_cast<float>(img.width));
                    ImGui::Image(reinterpret_cast<ImTextureID>(img.imguiDescriptor), ImVec2(img.width * scale, img.height * scale));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
                    ImGui::Text("  [Image %dx%d]", img.width, img.height);
                    ImGui::PopStyleColor();
                }
            }
            break;
        case ChatMessage::Role::Assistant:
            if (!msg.thinking.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
                ImGui::PushID(&msg);
                if (ImGui::TreeNode("Thinking"))
                {
                    ImGui::TextWrapped("%s", msg.thinking.c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
                ImGui::PopStyleColor();
            }
            if (!msg.text.empty())
                ImGui::TextWrapped("[AI] %s", msg.text.c_str());
            break;
        case ChatMessage::Role::System:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::PopStyleColor();
            break;
        }
        ImGui::Spacing();
    }
} // namespace pe
