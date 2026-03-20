#include "AgentWidget.h"
#include "PhasmaAgent/AgentUtils.h"

using namespace pagent;

namespace pe
{
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

        static const char *embProviderNames[] = {"Google", "OpenAI", "Ollama", "Voyage"};
        std::string embProviderName = embProviderNames[std::clamp(m_selectedEmbeddingProvider, 0, 3)];

        nlohmann::ordered_json j;
        j["agent"] = nlohmann::ordered_json{
            {"provider", providerName},
            {"model", m_modelName},
            {"fetch_require_tools", m_fetchRequireTools},
            {"fetch_require_vision", m_fetchRequireVision}};
        j["embeddings"] = nlohmann::ordered_json{
            {"enabled", m_embeddingEnabled},
            {"provider", embProviderName},
            {"model", embModelName},
            {"indexing", nlohmann::ordered_json{
                             {"directories", m_indexDirectories},
                             {"include_files", m_includeFiles},
                             {"skip_directories", m_skipDirectories},
                             {"skip_files", m_skipFiles},
                             {"skip_extensions", m_skipExtensions},
                             {"skip_regex", m_skipRegex}}}};

        f << j.dump(4) << "\n";
    }

    static void ApplyDefaultIndexingConfig(
        const std::filesystem::path &repoRoot,
        std::vector<std::string> &dirs,
        std::vector<std::string> &includeFiles,
        std::vector<std::string> &skipDirs,
        std::vector<std::string> &skipFiles,
        std::vector<std::string> &skipExts,
        std::vector<std::string> &skipRegex)
    {
        auto p = [&](const std::string &rel)
        { return (repoRoot / rel).string(); };

        auto addIfExists = [&](std::vector<std::string> &vec, const std::string &path)
        {
            if (std::filesystem::exists(path))
                vec.push_back(path);
        };

        addIfExists(dirs, p("PhasmaAgent"));
        addIfExists(dirs, p("PhasmaCore"));
        addIfExists(dirs, p("PhasmaEditor"));

        addIfExists(includeFiles, p("PhasmaEditor/Assets/Agent/START.md"));

        addIfExists(skipDirs, p("PhasmaAgent/third_party"));
        addIfExists(skipDirs, p("PhasmaCore/third_party"));
        addIfExists(skipDirs, p("PhasmaEditor/third_party"));
        addIfExists(skipDirs, p("PhasmaEditor/Assets/Agent"));
        skipFiles = {};
        skipRegex = {};
        skipExts = {
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

    void AgentWidget::LoadConfig()
    {
        std::string path = Path::Assets + "Agent/agent_config.json";
        std::ifstream f(path);
        if (!f)
        {
            // No config file — apply full defaults
            m_embeddingEnabled = true;
            m_selectedEmbeddingProvider = 2; // Ollama
            UpdateEmbeddingModels(false, "qwen3-embedding:0.6b");
            ApplyDefaultIndexingConfig(GetRepoRootFromAssets(), m_indexDirectories, m_includeFiles,
                                       m_skipDirectories, m_skipFiles,
                                       m_skipExtensions, m_skipRegex);
            return;
        }

        try
        {
            auto j = nlohmann::json::parse(f);

            // Restore agent provider; default to first Ollama entry when unset
            std::string providerName = j.value("/agent/provider"_json_pointer, std::string{});
            int defaultIdx = 0;
            for (int i = 0; i < static_cast<int>(m_providers.size()); ++i)
                if (m_providers[i].provider == pagent::Provider::Ollama)
                {
                    defaultIdx = i;
                    break;
                }
            m_selectedProviderIndex = defaultIdx;
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

            m_fetchRequireTools = j.value("/agent/fetch_require_tools"_json_pointer, true);
            m_fetchRequireVision = j.value("/agent/fetch_require_vision"_json_pointer, false);

            // Restore embedding config
            m_embeddingEnabled = j.value("/embeddings/enabled"_json_pointer, false);

            std::string embProviderName = j.value("/embeddings/provider"_json_pointer, std::string{"Ollama"});
            static const std::pair<const char *, int> embProviderMap[] = {{"Google", 0}, {"OpenAI", 1}, {"Ollama", 2}, {"Voyage", 3}};
            m_selectedEmbeddingProvider = 2; // default Ollama
            for (auto &[name, idx] : embProviderMap)
                if (embProviderName == name)
                {
                    m_selectedEmbeddingProvider = idx;
                    break;
                }

            std::string embModelName = j.value("/embeddings/model"_json_pointer, std::string{});
            UpdateEmbeddingModels(false, embModelName); // async for Ollama; callback selects preferred or default

            // Restore indexing config (nested under embeddings)
            if (j.contains("embeddings") && j["embeddings"].contains("indexing"))
            {
                auto &idx = j["embeddings"]["indexing"];
                if (idx.contains("directories"))
                    m_indexDirectories = idx["directories"].get<std::vector<std::string>>();
                if (idx.contains("include_files"))
                    m_includeFiles = idx["include_files"].get<std::vector<std::string>>();
                if (idx.contains("skip_directories"))
                    m_skipDirectories = idx["skip_directories"].get<std::vector<std::string>>();
                if (idx.contains("skip_files"))
                    m_skipFiles = idx["skip_files"].get<std::vector<std::string>>();
                if (idx.contains("skip_extensions"))
                    m_skipExtensions = idx["skip_extensions"].get<std::vector<std::string>>();
                if (idx.contains("skip_regex"))
                    m_skipRegex = idx["skip_regex"].get<std::vector<std::string>>();
            }

            // Fall back to default indexing dirs if none were configured
            if (m_indexDirectories.empty())
                ApplyDefaultIndexingConfig(GetRepoRootFromAssets(), m_indexDirectories, m_includeFiles,
                                           m_skipDirectories, m_skipFiles,
                                           m_skipExtensions, m_skipRegex);
        }
        catch (...)
        {
            // Parsing or access error — apply defaults
            ApplyDefaultIndexingConfig(GetRepoRootFromAssets(), m_indexDirectories, m_includeFiles,
                                       m_skipDirectories, m_skipFiles,
                                       m_skipExtensions, m_skipRegex);
        }
    }

    // -------------------------------------------------------------------------
    // Session persistence
    // -------------------------------------------------------------------------

    static std::string SessionsDir()
    {
        return Path::Assets + "Agent/sessions/";
    }

    void AgentWidget::SaveSession()
    {
        if (!m_agent && !IsAnyCLI() && !m_isExternalAI)
            return;

        // Create sessions directory if needed
        std::filesystem::create_directories(SessionsDir());

        // Generate a filename from the current time if we don't have one yet
        if (m_currentSessionPath.empty())
        {
            // Derive a short title from the first user message
            std::string title = "session";
            {
                std::lock_guard lock(m_chatMutex);
                for (const auto &msg : m_chat)
                {
                    if (msg.role == ChatMessage::Role::User && !msg.text.empty())
                    {
                        title = msg.text.substr(0, 40);
                        // Replace chars that are unsafe in filenames
                        for (auto &c : title)
                            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                                c == '"' || c == '<' || c == '>' || c == '|' || c == '\n')
                                c = '_';
                        break;
                    }
                }
            }
            // Timestamp prefix for chronological sorting
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", &tm);
            m_currentSessionPath = SessionsDir() + stamp + "_" + title + ".json";
        }

        nlohmann::json j;
        j["version"] = 1;
        j["provider"] = (m_selectedProviderIndex < static_cast<int>(m_providers.size()))
                            ? m_providers[m_selectedProviderIndex].name
                            : "";
        j["model"] = m_modelName;

        nlohmann::json msgs = nlohmann::json::array();
        {
            std::lock_guard lock(m_chatMutex);
            for (const auto &msg : m_chat)
            {
                // Skip system messages (status notes, session-restored banners, etc.)
                // They are UI-only and should not accumulate across restarts.
                if (msg.role == ChatMessage::Role::System)
                    continue;

                const char *role = msg.role == ChatMessage::Role::User ? "user" : "assistant";
                nlohmann::json m;
                m["role"] = role;
                m["text"] = msg.text;
                if (!msg.thinking.empty())
                    m["thinking"] = msg.thinking;
                if (!msg.tools.empty())
                    m["tools"] = msg.tools;
                msgs.push_back(std::move(m));
            }
        }
        j["messages"] = std::move(msgs);

        std::ofstream f(m_currentSessionPath);
        if (f)
            f << j.dump(2) << "\n";
    }

    void AgentWidget::LoadSession(const std::string &path)
    {
        if (!m_agent && !IsAnyCLI() && !m_isExternalAI)
            return;

        // Restoring a saved session — do not inject START.md; context is already in the history
        m_cliSystemContext.clear();
        m_codexHasSession = false;
        m_claudeHasSession = false;
        m_geminiHasSession = false;

        std::ifstream f(path);
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

        m_currentSessionPath = path;

        // Restore display chat
        std::vector<ChatMessage> loaded;
        std::vector<pagent::HistoryEntry> historyEntries;

        for (const auto &m : j.value("messages", nlohmann::json::array()))
        {
            std::string role = m.value("role", "system");
            std::string text = m.value("text", "");
            std::string thinking = m.value("thinking", "");
            std::string tools = m.value("tools", "");

            ChatMessage::Role chatRole = ChatMessage::Role::System;
            if (role == "user")
                chatRole = ChatMessage::Role::User;
            else if (role == "assistant")
                chatRole = ChatMessage::Role::Assistant;

            ChatMessage loadedMsg;
            loadedMsg.role = chatRole;
            loadedMsg.text = std::move(text);
            loadedMsg.thinking = std::move(thinking);
            loadedMsg.tools = std::move(tools);
            loaded.push_back(std::move(loadedMsg));

            // Reconstruct lightweight LLM history from user/assistant turns only
            if (role == "user" || role == "assistant")
            {
                pagent::NeutralMessage nm;
                nm.role = (role == "user") ? pagent::NeutralMessage::Role::User
                                           : pagent::NeutralMessage::Role::Assistant;
                nm.content = text;
                historyEntries.push_back({std::move(nm), 0});
            }
        }

        // Apply to agent
        if (m_agent)
        {
            m_agent->ClearHistory();
            m_agent->LoadHistory(historyEntries);
            m_agent->InjectSystemMessage(
                "Session restored from a previous editor run. "
                "The codebase may have changed since this conversation. "
                "Always verify file contents and line numbers with tools before acting on past assumptions.");
        }

        {
            std::lock_guard lock(m_chatMutex);
            m_chat = std::move(loaded);
            m_chat.push_back({ChatMessage::Role::System,
                              "[Session restored - code may have changed since last run]"});
            m_scrollToBottom = 3;
        }

        const std::string savedProvider = j.value("provider", "");
        const bool hasTurns = !historyEntries.empty();
        m_codexHasSession = hasTurns && savedProvider == "Codex CLI" && m_isCodexCLI;
        m_claudeHasSession = hasTurns && savedProvider == "Claude CLI" && m_isClaudeCLI;
        m_geminiHasSession = hasTurns && savedProvider == "Gemini CLI" && m_isGeminiCLI;

        // If the last message was from the user in an external session, we should be waiting for a response.
        if (m_isExternalAI)
        {
            std::lock_guard lock(m_chatMutex);
            auto lastMsgIt = std::find_if(m_chat.rbegin(), m_chat.rend(),
                                          [](const ChatMessage &msg)
                                          { return msg.role != ChatMessage::Role::System; });
            if (lastMsgIt != m_chat.rend() && lastMsgIt->role == ChatMessage::Role::User)
            {
                m_isStreaming = true;
            }
        }

        // Pick up any external response that was already written before the watcher was attached.
        if (m_isExternalAI && m_isStreaming)
            QueueAction([this]()
                        { PollExternalResponse(); });
    }

    void AgentWidget::NewSession()
    {
        SaveSession(); // persist current session before clearing
        m_currentSessionPath.clear();
        if (m_agent)
        {
            m_agent->ClearHistory();
        }
        if (m_isExternalAI)
        {
            std::error_code ec;
            std::filesystem::remove(Path::Assets + "Agent/codex_external_has_session.flag", ec);
            std::filesystem::remove(Path::Assets + "Agent/codex_external_thread.txt", ec);
        }
        if (IsAnyCLI())
        {
            // Load START.md so the CLI tool has project context on its first message of this session
            m_cliSystemContext.clear();
            std::ifstream sf(Path::Assets + "Agent/START.md");
            if (sf)
            {
                std::ostringstream ss;
                ss << sf.rdbuf();
                m_cliSystemContext = ss.str();
            }
        }
        m_codexHasSession = false;
        m_claudeHasSession = false;
        m_geminiHasSession = false;
        std::lock_guard lock(m_chatMutex);
        m_chat.clear();
        m_scrollToBottom = 3;
    }

    std::vector<std::string> AgentWidget::ListSessions() const
    {
        std::vector<std::string> sessions;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(SessionsDir(), ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                sessions.push_back(entry.path().string());
        }
        // Sort newest first (filenames start with timestamp)
        std::sort(sessions.rbegin(), sessions.rend());
        return sessions;
    }

} // namespace pe
