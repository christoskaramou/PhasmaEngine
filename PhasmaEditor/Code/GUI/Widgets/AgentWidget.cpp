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

using namespace pagent;

namespace pe
{
    AgentWidget::AgentWidget() : Widget("Agent"), m_agent(pagent::AgentConfig{})
    {
    }

    AgentWidget::~AgentWidget()
    {
        pagent::Agent::CancelPull(m_pullCancel);
        if (m_agent)
            m_agent->CancelPending();
    }

    void AgentWidget::Init(GUI *gui)
    {
        Widget::Init(gui);

        m_providers = pagent::DiscoverProviders();
        // Add "External" provider (file-based, for Claude Code / Cursor / any AI tool)
        m_providers.push_back({pagent::Provider::Ollama, "External", "", "external"});
        // Default to External unless PAGENT_PROVIDER is explicitly set
        const char *providerEnv = std::getenv("PAGENT_PROVIDER");
        m_selectedProviderIndex = providerEnv ? pagent::GetDefaultProviderIndex(m_providers)
                                              : static_cast<int>(m_providers.size()) - 1;
        ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
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
            "Control the editor via execute_lua. API ref is in START.md. "
            "Rules: ASCII only, no emoji. Be very concise. Show Lua before executing. "
            "Chain ALL operations in ONE execute_lua call. Check results for errors. "
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

        m_modelName = config.model;
        m_availableModels = {config.model};
        m_selectedModelIndex = 0;
        m_agentConfigured = true;

        m_agent = pagent::Agent(std::move(config));
        m_agent->SetEventCallback([this](const pagent::AgentEvent &ev)
                                  { OnAgentEvent(ev); });

        RegisterTools();

        // Load START.md instructions if present in the agent workspace
        {
            std::string startPath = Path::Assets + "Agent/START.md";
            if (std::filesystem::exists(startPath))
            {
                std::ifstream file(startPath, std::ios::in);
                if (file.is_open())
                {
                    std::string content((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                    if (!content.empty())
                        m_agent->InjectSystemMessage("Instructions from START.md:\n" + content);
                }
            }
        }

        FetchAvailableModels();
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
                                       auto *ss = GetGlobalSystem<ScriptSystem>();
                                       if (!ss || !ss->IsInitialized())
                                           result = "error: ScriptSystem not available";
                                       else
                                           result = ss->ExecuteLua(code);
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

                                   std::string normalized = fpath.lexically_normal().string();
                                   std::string rootNorm = std::filesystem::path(projectRoot).lexically_normal().string();
                                   if (normalized.find(rootNorm) != 0)
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

                                   return JsonObj({{"path", JsonStr(normalized)}, {"content", JsonStr(content)}});
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

                                   std::string normalized = fpath.lexically_normal().string();
                                   std::string rootNorm = std::filesystem::path(projectRoot).lexically_normal().string();
                                   if (normalized.find(rootNorm) != 0)
                                       return "{\"error\":\"path outside project directory\"}";

                                   // Only allow writing inside PhasmaEditor/
                                   std::string editorDir = (std::filesystem::path(projectRoot) / "PhasmaEditor").lexically_normal().string();
                                   if (normalized.find(editorDir) != 0)
                                       return "{\"error\":\"writes only allowed inside PhasmaEditor/\"}";

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

                                   return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(normalized)}});
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

                                   std::string normalized = searchPath.lexically_normal().string();
                                   std::string rootNorm = std::filesystem::path(projectRoot).lexically_normal().string();
                                   if (normalized.find(rootNorm) != 0)
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

                                   std::string normalized = dirPath.lexically_normal().string();
                                   std::string rootNorm = std::filesystem::path(projectRoot).lexically_normal().string();
                                   if (normalized.find(rootNorm) != 0)
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
                                   return JsonObj({{"path", JsonStr(normalized)}, {"files", files}, {"dirs", dirs}});
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

    void AgentWidget::FetchAvailableModels()
    {
        const auto &info = m_providers[m_selectedProviderIndex];
        auto provider = info.provider;
        auto apiKey = info.apiKey;
        auto currentModel = m_modelName;

        std::thread([this, provider, apiKey, currentModel]
                    {
            // For Ollama, the server may still be starting — retry a few times
            std::vector<pagent::Agent::ModelInfo> modelInfos;
            int retries = (provider == pagent::Provider::Ollama) ? 5 : 1;
            for (int attempt = 0; attempt < retries; ++attempt)
            {
                if (attempt > 0)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                modelInfos = pagent::Agent::FetchModelInfos(provider, apiKey);
                if (!modelInfos.empty())
                    break;
            }

            std::vector<std::string> names;
            std::vector<bool> localFlags;
            for (auto &mi : modelInfos)
            {
                names.push_back(std::move(mi.name));
                localFlags.push_back(mi.local);
            }

            // Ensure current model is in the list
            if (std::find(names.begin(), names.end(), currentModel) == names.end())
            {
                names.insert(names.begin(), currentModel);
                // For Ollama, if the model wasn't in the list it likely needs downloading
                localFlags.insert(localFlags.begin(), provider != pagent::Provider::Ollama);
            }

            QueueAction([this, names = std::move(names), localFlags = std::move(localFlags), currentModel]
                        {
                m_availableModels = names;
                m_modelIsLocal = localFlags;
                for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                {
                    if (m_availableModels[i] == currentModel)
                    {
                        m_selectedModelIndex = i;
                        break;
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
                                m_selectedProviderIndex = i;
                                ConfigureAgent(m_providers[i].provider);
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
                std::string comboLabel = m_isPulling ? (m_modelName + " (downloading...)") : m_modelName;
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
                            m_selectedModelIndex = i;
                            m_modelName = m_availableModels[i];
                            m_modelFilter[0] = '\0';

                            if (isLocal)
                            {
                                m_agent->SetModel(m_modelName);
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
                                m_pullCancel = pagent::Agent::PullModel(pullModel, [this](const std::string &status)
                                                                        { QueueAction([this, status]()
                                                                                      {
                                            std::lock_guard lock(m_chatMutex);
                                            if (!m_chat.empty() && m_chat.back().role == ChatMessage::Role::System)
                                                m_chat.back().text = status;
                                            m_scrollToBottom = true; }); }, [this, i, pullModel](bool success)
                                                                        {
                                        // Check tool support on background thread before queuing UI update
                                        bool hasTools = success && pagent::Agent::SupportsTools(
                                            m_providers[m_selectedProviderIndex].provider, pullModel);

                                        QueueAction([this, i, pullModel, success, hasTools]()
                                        {
                                            m_isPulling = false;
                                            m_pullCancel.reset();
                                            std::lock_guard lock(m_chatMutex);
                                            if (!success)
                                            {
                                                m_chat.push_back({ChatMessage::Role::System, "Download cancelled."});
                                            }
                                            else if (!hasTools)
                                            {
                                                // Remove from list -- model doesn't support tools
                                                if (i < static_cast<int>(m_availableModels.size()))
                                                {
                                                    m_availableModels.erase(m_availableModels.begin() + i);
                                                    m_modelIsLocal.erase(m_modelIsLocal.begin() + i);
                                                    if (m_selectedModelIndex >= static_cast<int>(m_availableModels.size()))
                                                        m_selectedModelIndex = 0;
                                                }
                                                m_chat.push_back({ChatMessage::Role::System,
                                                    pullModel + " does not support tool calling. Removed from list."});
                                            }
                                            else
                                            {
                                                if (i < static_cast<int>(m_modelIsLocal.size()))
                                                    m_modelIsLocal[i] = true;
                                                m_agent->SetModel(pullModel);
                                                m_chat.push_back({ChatMessage::Role::System, "Model ready."});
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
                // Unload button for Ollama (frees GPU memory)
                else if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama &&
                         !m_isExternalAI)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Unload"))
                    {
                        pagent::Agent::UnloadModel(pagent::Provider::Ollama, m_modelName);
                        std::lock_guard lock(m_chatMutex);
                        m_chat.push_back({ChatMessage::Role::System, "Model " + m_modelName + " unloaded from GPU."});
                        m_scrollToBottom = true;
                    }
                }
                ImGui::PopItemWidth();
            }
            // Token usage display
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            {
                auto usage = m_agent->GetUsage();
                auto provider = m_providers[m_selectedProviderIndex].provider;
                float cost = pagent::EstimateCostUSD(provider, m_modelName,
                                                     usage.totalInput, usage.totalOutput, usage.totalCacheRead, usage.totalCacheWrite);
                char buf[256];
                bool hasTokens = usage.totalInput > 0 || usage.totalOutput > 0;
                if (cost > 0.001f)
                    snprintf(buf, sizeof(buf), "tokens [in/out]: turn [%dk/%dk]  total: [%dk/%dk]  cached: [%dk] | cost [~$%.4f]",
                             usage.turnInput / 1000, usage.turnOutput / 1000,
                             usage.totalInput / 1000, usage.totalOutput / 1000,
                             usage.totalCacheRead / 1000, cost);
                else if (hasTokens && provider != pagent::Provider::Ollama)
                    snprintf(buf, sizeof(buf), "tokens [in/out]: turn [%dk/%dk]  total: [%dk/%dk]  cached: [%dk] | cost N/A",
                             usage.turnInput / 1000, usage.turnOutput / 1000,
                             usage.totalInput / 1000, usage.totalOutput / 1000,
                             usage.totalCacheRead / 1000);
                else
                    snprintf(buf, sizeof(buf), "tokens [in/out]: turn [%dk/%dk]  total: [%dk/%dk]  cached: [%dk]",
                             usage.turnInput / 1000, usage.turnOutput / 1000,
                             usage.totalInput / 1000, usage.totalOutput / 1000,
                             usage.totalCacheRead / 1000);
                ImGui::TextUnformatted(buf);
            }
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        // chat log
        const float statusHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        const float inputHeight = ImGui::GetTextLineHeight() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
        ImGui::BeginChild("ChatLog", ImVec2(0, -(inputHeight + statusHeight)), false);
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
        const std::string text(m_inputBuf);
        m_inputBuf[0] = '\0';
        m_inputHistory.push_back(text);
        m_historyIndex = -1;
        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back({ChatMessage::Role::User, text});
            m_scrollToBottom = true;
        }

        if (m_isExternalAI)
        {
            // Write user message to file for external AI to read
            std::string inputPath = Path::Assets + "Agent/" + m_externalFile;
            std::ofstream f(inputPath, std::ios::trunc);
            f << text;
            f.close();
            // Clear previous response
            std::ofstream(GetExternalResponsePath(), std::ios::trunc).close();
            m_isStreaming = true;
            WriteExternalHistory();
        }
        else
        {
            m_agent->Send(text);
        }
        ImGui::SetKeyboardFocusHere(-1);
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
