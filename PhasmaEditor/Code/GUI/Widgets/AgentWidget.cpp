#include "AgentWidget.h"
#include "FileBrowser.h"
#include "GUI/GUI.h"
#include "Systems/RendererSystem.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "PhasmaAgent/AgentUtils.h"
#include "PhasmaAgent/VectorStore.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_vulkan.h"
#include "API/Image.h"

#include "stb/stb_image.h"

#if defined(PE_WIN32)
#include <Windows.h>
#elif defined(PE_LINUX)
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace pagent;

namespace pe
{
    // --- Local helpers ---

    std::filesystem::path AgentWidget::GetRepoRootFromAssets()
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::path assetsPath = fs::weakly_canonical(Path::Assets, ec);
        if (ec)
        {
            ec.clear();
            assetsPath = fs::absolute(Path::Assets, ec);
        }
        if (ec || assetsPath.empty())
            assetsPath = fs::path(Path::Assets).lexically_normal();

        // Path::Assets resolves to .../PhasmaEditor/Assets, so two parents up is the repo root.
        return assetsPath.parent_path().parent_path();
    }

    std::string AgentWidget::GetEnvOrEmpty(const char *name)
    {
#if defined(PE_WIN32)
        char *value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, name) != 0 || !value)
            return {};
        std::string result(value);
        free(value);
        return result;
#else
        const char *value = std::getenv(name);
        return value ? value : "";
#endif
    }

    // Runs `gcloud auth print-access-token` and returns the token string, or "" on failure.
    static std::string FetchGcloudToken()
    {
#if defined(PE_WIN32)
        FILE *pipe = _popen("gcloud auth print-access-token 2>NUL", "r");
#else
        FILE *pipe = popen("gcloud auth print-access-token 2>/dev/null", "r");
#endif
        if (!pipe)
            return {};
        std::string token;
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe))
            token += buf;
#if defined(PE_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        // Strip trailing whitespace/newline
        while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' '))
            token.pop_back();
        return token;
    }

    static std::string ToLower(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static void *LoadIcon(const std::string &path, Image *&outImage)
    {
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        outImage = Image::LoadRGBA8(cmd, path);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
        if (outImage && outImage->GetSampler() && outImage->GetSRV())
            return (void *)ImGui_ImplVulkan_AddTexture(outImage->GetSampler()->ApiHandle(), outImage->GetSRV()->ApiHandle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return nullptr;
    }

    // Nearest-neighbor resize RGBA image to fit within maxDim on longest side
    static std::vector<uint8_t> ResizeRGBA(const uint8_t *src, int srcW, int srcH, int &outW, int &outH, int maxDim = 1024)
    {
        float scale = static_cast<float>(maxDim) / static_cast<float>(std::max(srcW, srcH));
        outW = static_cast<int>(srcW * scale);
        outH = static_cast<int>(srcH * scale);
        std::vector<uint8_t> resized(outW * outH * 4);
        for (int dy = 0; dy < outH; dy++)
        {
            int sy = dy * srcH / outH;
            for (int dx = 0; dx < outW; dx++)
            {
                int sx = dx * srcW / outW;
                std::memcpy(&resized[(dy * outW + dx) * 4], &src[(sy * srcW + sx) * 4], 4);
            }
        }
        return resized;
    }
    AgentWidget::AgentWidget() : Widget("Agent"), m_agent(pagent::AgentConfig{})
    {
    }

    AgentWidget::~AgentWidget()
    {
        *m_alive = false; // signal background threads to stop accessing this
        SaveSession();
        SaveConfig();
        auto codebasePath = GetCodebaseStorePath();
        if (m_codebaseStore && !codebasePath.empty())
            m_codebaseStore->SaveToBinary(codebasePath);
        pagent::Agent::CancelPull(m_pullCancel);
        pagent::Agent::CancelPull(m_pullEmbeddingCancel);
        if (m_agent)
            m_agent->CancelPending();
        m_agentScriptSystem.Destroy();

        for (auto *ds : m_chatImguiDescriptors)
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)ds);
        for (auto *img : m_chatGpuImages)
            Image::Destroy(img);

        auto destroyIcon = [](void *ds, Image *img)
        {
            if (ds)
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)ds);
            Image::Destroy(img);
        };
        destroyIcon(m_fileIconDS, m_fileIcon);
        destroyIcon(m_txtIconDS, m_txtIcon);
        destroyIcon(m_shaderIconDS, m_shaderIcon);
        destroyIcon(m_modelIconDS, m_modelIcon);
        destroyIcon(m_scriptIconDS, m_scriptIcon);
    }

    void AgentWidget::Init(GUI *gui)
    {
        pe::Widget::Init(gui);
        m_agentScriptSystem.InitRestricted(nullptr);

        m_providers = pagent::DiscoverProviders();
        // Add "External" provider (file-based, for Claude Code / Cursor / any AI tool)
        m_providers.push_back({pagent::Provider::CLI, "External", "", "external"});
        // Add CLI providers — each spawns a subprocess
        m_providers.push_back({pagent::Provider::CLI, "Codex CLI", "", "codex"});
        m_providers.push_back({pagent::Provider::CLI, "Claude CLI", "", "claude"});
        m_providers.push_back({pagent::Provider::CLI, "Gemini CLI", "", "gemini"});

        // Restore saved config (provider, model, embedding settings)
        if (std::filesystem::exists(Path::Assets + "Agent/agent_config.json"))
        {
            LoadConfig();
        }
        else
        {
            // First launch defaults — prefer Ollama unless PAGENT_PROVIDER env var says otherwise
            std::string providerEnv = GetEnvOrEmpty("PAGENT_PROVIDER");
            if (!providerEnv.empty())
            {
                m_selectedProviderIndex = pagent::GetDefaultProviderIndex(m_providers);
            }
            else
            {
                m_selectedProviderIndex = 0;
                for (int i = 0; i < static_cast<int>(m_providers.size()); ++i)
                    if (m_providers[i].provider == pagent::Provider::Ollama && m_providers[i].name != "External")
                    {
                        m_selectedProviderIndex = i;
                        break;
                    }
            }
            if (!GetEnvOrEmpty("PAGENT_GEMINI_API_KEY").empty())
            {
                m_embeddingEnabled = true;
                m_selectedEmbeddingProvider = 0; // Google
            }
            UpdateEmbeddingModels();
        }

        // Default indexing directory: Shaders (always present in assets)
        if (m_indexDirectories.empty())
            m_indexDirectories.push_back(Path::Assets + "Shaders");

        // Load file type icons for pending attachments
        m_fileIconDS = LoadIcon(Path::Assets + "Icons/file_icon.png", m_fileIcon);
        m_txtIconDS = LoadIcon(Path::Assets + "Icons/txt_icon.png", m_txtIcon);
        m_shaderIconDS = LoadIcon(Path::Assets + "Icons/shader_icon.png", m_shaderIcon);
        m_modelIconDS = LoadIcon(Path::Assets + "Icons/model_icon.png", m_modelIcon);
        m_scriptIconDS = LoadIcon(Path::Assets + "Icons/script_icon.png", m_scriptIcon);

        // Register for OS-level file drop events (drag from Explorer onto the window)
        EventSystem::RegisterCallback(EventType::FileDrop, [this](const std::any &data)
                                      {
            const auto &paths = std::any_cast<const std::vector<std::string> &>(data);
            std::vector<std::filesystem::path> fsPaths(paths.begin(), paths.end());
            ProcessDroppedFiles(fsPaths); });

        ConfigureAgent(m_providers[m_selectedProviderIndex].provider);

        // Populate model list for the restored provider
        FetchAvailableModels();

        // Auto-load most recent session if one exists
        {
            auto sessions = ListSessions();
            if (!sessions.empty())
                LoadSession(sessions.front());
        }

        // Fetch local models for Ollama at startup (no remote)
        // Other providers have hardcoded model lists
        {
            auto aliveRef = m_alive;
            for (int pi = 0; pi < static_cast<int>(m_providers.size()); ++pi)
            {
                if (m_providers[pi].provider != pagent::Provider::Ollama)
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
        m_isCodexCLI = (info->name == "Codex CLI");
        m_isClaudeCLI = (info->name == "Claude CLI");
        m_isGeminiCLI = (info->name == "Gemini CLI");

        if (IsAnyCLI())
        {
            char *modelBuf = m_isCodexCLI    ? m_codexModelBuf
                             : m_isClaudeCLI ? m_claudeModelBuf
                                             : m_geminiModelBuf;
            const char *defaultModel = m_isCodexCLI    ? "gpt-5.4"
                                       : m_isClaudeCLI ? "claude-sonnet-4-6"
                                                       : "";
            if (modelBuf[0] == '\0' && defaultModel[0] != '\0')
                std::strncpy(modelBuf, defaultModel, 127);
            m_modelName = modelBuf;
            m_availableModels = {m_modelName};
            m_selectedModelIndex = 0;
            m_agentConfigured = true;
            return;
        }

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
        std::string projectRoot = GetRepoRootFromAssets().string();
        if (!projectRoot.empty() && projectRoot.back() != '/')
            projectRoot += '/';

        config.system_prompt =
            "You are an AI assistant inside PhasmaEditor (Vulkan 3D engine). " + std::string(m_embeddingEnabled ? "" : "FIRST THING: use read_agent_file to read START.md for your full API reference and rules. ") +
            "Control the editor via execute_lua. Be very concise. ASCII only, no emoji. "
            "Chain ALL operations in ONE execute_lua call. Check results for errors. "
            "To load 3D models: ALWAYS call find_loadable_model tool first (even if you think you know the path), then execute_lua with: local m, err = load_model('path/from/tool') "
            "The Lua function is load_model (NOT pe_load_model). NEVER guess model paths. Do NOT use fs.find/fs.list for models. "
            "Set unique labels on created models. Use request_feature for missing capabilities. "
            "Screenshots: take_screenshot tool (returns base64 image for vision). "
            "UI interaction: when clicking based on a screenshot use u/v (normalized 0.0-1.0, e.g. u=pixel_x/image_width). "
            "Code converts u/v to real screen pixels automatically. "
            "tab_bars from query_imgui_windows already have real click_x/click_y — pass those as x/y directly. "
            "Workspace: " +
            Path::Assets + "Agent/ | Assets: " + Path::Assets + ".";

        config.log_callback = [](const std::string &msg)
        { PE_INFO("%s", msg.c_str()); };
        config.max_tool_rounds = 12;
        config.max_tool_result_chars = 500;
        config.summarize_tool_result_chars = 1500; // summarize grep/find/list results >1500 chars instead of truncating
        config.max_history_messages = 20;
        config.summarize_after_messages = 8;
        config.provider = info->provider;
        config.api_key = info->apiKey;
        config.model = info->defaultModel;
        config.base_url = info->base_url;

        // Vertex AI: read project/location from env and fetch an OAuth2 token via gcloud
        if (info->provider == pagent::Provider::GoogleVertex)
        {
            if (auto proj = GetEnvOrEmpty("PAGENT_VERTEX_PROJECT_ID"); !proj.empty())
                config.vertex_project_id = proj;
            if (auto loc = GetEnvOrEmpty("PAGENT_VERTEX_LOCATION"); !loc.empty())
                config.vertex_location = loc;
            config.api_key = FetchGcloudToken();
            if (config.api_key.empty())
                PE_WARN("Google Vertex: could not fetch access token via gcloud. "
                        "Make sure 'gcloud auth application-default login' has been run.");
            if (m_modelName.empty())
                m_modelName = config.model; // default to gemini-2.5-flash-002
        }

        // Set up Gemini vision fallback key (used when main provider lacks vision)
        if (auto geminiKey = GetEnvOrEmpty("PAGENT_GEMINI_API_KEY"); !geminiKey.empty())
            config.gemini_api_key_for_vision = geminiKey;

        // Set up embedding provider based on current selection
        m_embeddingProvider = CreateEmbeddingProvider();
        config.embedding_provider = m_embeddingProvider;

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

        InitCodebaseStore();
        RegisterTools();
    }

    void AgentWidget::SetRepoMap(std::string map)
    {
        if (m_agent.has_value())
            m_agent->SetRepoMap(map);
    }

    void AgentWidget::SetIncludeGraph(std::shared_ptr<pagent::IncludeGraph> graph)
    {
        m_includeGraph = std::move(graph);
        if (m_agent.has_value())
            m_agent->SetIncludeGraph(m_includeGraph.get());
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

        bool requireTools = m_fetchRequireTools;
        bool requireVision = m_fetchRequireVision;

        std::thread([this, alive, provider, apiKey, currentModel, providerIdx, fetchRemote, requireTools, requireVision]
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
                modelInfos = pagent::Agent::FetchModelInfos(provider, apiKey, "", localOnly, requireTools, requireVision);
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
        HandleScreenshotEvent();

        // Deferred GPU upload of pending image thumbnails (must happen outside render pass)
        for (auto &img : m_pendingImages)
        {
            if (!img.imguiDescriptor && !img.base64.empty() && img.width > 0 && img.height > 0)
            {
                auto pngBytes = pagent::Base64Decode(img.base64);
                int tw = 0, th = 0, tc = 0;
                uint8_t *pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()), &tw, &th, &tc, 4);
                if (pixels)
                {
                    img.imguiDescriptor = UploadChatImage(pixels, tw, th);
                    img.width = tw;
                    img.height = th;
                    stbi_image_free(pixels);
                }
            }
        }

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

        const bool busy = (m_isExternalAI || IsAnyCLI()) ? m_isStreaming : (m_agent && m_agent->IsBusy());

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
                const auto &curProvider = m_providers[m_selectedProviderIndex];
                float providerWidth = ImGui::CalcTextSize(curProvider.name.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::PushItemWidth(providerWidth);
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
            // Model selector / External file input / Codex model input
            if (m_isCodexCLI)
            {
                ImGui::PushItemWidth(120.0f);
                if (ImGui::InputText("##codexmodel", m_codexModelBuf, sizeof(m_codexModelBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    m_modelName = m_codexModelBuf;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Codex model (e.g. gpt-5.4, gpt-5.4-mini, gpt-5.2)\nPress Enter to apply.");
                ImGui::PopItemWidth();
            }
            else if (m_isClaudeCLI)
            {
                ImGui::PushItemWidth(160.0f);
                if (ImGui::InputText("##claudemodel", m_claudeModelBuf, sizeof(m_claudeModelBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    m_modelName = m_claudeModelBuf;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Claude model (e.g. claude-sonnet-4-6, claude-opus-4-6)\nPress Enter to apply.");
                ImGui::PopItemWidth();
            }
            else if (m_isGeminiCLI)
            {
                ImGui::PushItemWidth(160.0f);
                if (ImGui::InputText("##geminimodel", m_geminiModelBuf, sizeof(m_geminiModelBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                    m_modelName = m_geminiModelBuf;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Gemini model (e.g. gemini-2.5-pro, gemini-2.5-flash)\nPress Enter to apply.");
                ImGui::PopItemWidth();
            }
            else if (m_isExternalAI)
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
                float modelWidth = ImGui::CalcTextSize(comboLabel.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::PushItemWidth(modelWidth);
                if (ImGui::BeginCombo("##model", comboLabel.c_str()))
                {
                    // Filter input at the top of the dropdown
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint("##modelfilter", "Filter...", m_modelFilter, sizeof(m_modelFilter));

                    std::string filter = ToLower(m_modelFilter);

                    for (int i = 0; i < static_cast<int>(m_availableModels.size()); ++i)
                    {
                        // Apply filter
                        if (!filter.empty())
                        {
                            if (ToLower(m_availableModels[i]).find(filter) == std::string::npos)
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
                                    m_scrollToBottom = 3;
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
                                            m_scrollToBottom = 3; }); }, [this, aliveRef, i, pullModel](bool success)
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
                                            m_scrollToBottom = 3;
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
                else if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::GoogleVertex)
                {
                    // Vertex AI tokens expire after ~1 hour — refresh on demand
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Refresh Token"))
                        ConfigureAgent(pagent::Provider::GoogleVertex);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Re-run 'gcloud auth print-access-token' to get a fresh OAuth2 token.\nTokens expire after ~1 hour.");
                }
                else if (m_providers[m_selectedProviderIndex].provider == pagent::Provider::Ollama &&
                         !m_isExternalAI)
                {
                    // Unload button - only shown when model is loaded
                    if (m_ollamaModelLoaded)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Unload"))
                        {
                            pagent::Agent::UnloadModel(pagent::Provider::Ollama, m_modelName);
                            m_ollamaModelLoaded = false;
                            std::lock_guard lock(m_chatMutex);
                            m_chat.push_back({ChatMessage::Role::System, "Model " + m_modelName + " unloaded from GPU."});
                            m_scrollToBottom = 3;
                        }
                    }
                    // Fetch button - fetches remote models from ollama.com
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
                    ImGui::SameLine();
                    if (ImGui::SmallButton("\xe2\x96\xbe##fetchopts"))
                        ImGui::OpenPopup("FetchOptions");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Fetch filter options");
                    if (ImGui::BeginPopup("FetchOptions"))
                    {
                        ImGui::TextDisabled("Filter by capability");
                        ImGui::Separator();
                        if (ImGui::Checkbox("Require tools", &m_fetchRequireTools))
                            SaveConfig();
                        if (ImGui::Checkbox("Require vision", &m_fetchRequireVision))
                            SaveConfig();
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopItemWidth();
            }
            // Compact token display
            if (m_agent)
            {
                ImGui::SameLine();
                auto usage = m_agent->GetUsage();
                char buf[64];
                snprintf(buf, sizeof(buf), "\xe2\x86\x91%dk \xe2\x86\x93%dk",
                         usage.totalInput / 1000, usage.totalOutput / 1000);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(buf);
                if (ImGui::IsItemHovered())
                {
                    char tip[128];
                    snprintf(tip, sizeof(tip), "In: %dk  Out: %dk  Cached: %dk",
                             usage.totalInput / 1000, usage.totalOutput / 1000,
                             usage.totalCacheRead / 1000);
                    ImGui::SetTooltip("%s", tip);
                }
                ImGui::PopStyleColor();
            }

            // RAG colored text indicator
            if (!m_isExternalAI)
            {
                ImGui::SameLine();
                const bool ragIndexing = m_gui && m_gui->IsIndexing();
                const bool ragLoading = m_codebaseLoading.load();
                const bool ragChecking = m_indexStatusChecking.load();
                const bool ragBusy = ragIndexing || ragLoading || ragChecking || m_isPullingEmbedding || m_isFetchingEmbeddingModels;
                const bool ragFunctional = m_embeddingEnabled && m_embeddingProvider != nullptr;
                const bool statusReady = m_indexStatusReady.load();
                const bool partiallyIdx = statusReady && m_indexStatusOutdated > 0 && m_indexStatusOutdated < m_indexStatusTotal;
                const bool fullyOutdated = statusReady && m_indexStatusOutdated > 0 && m_indexStatusOutdated >= m_indexStatusTotal;
                const bool ragUnavailable = m_embeddingEnabled && !ragFunctional && !ragBusy;
                const ImVec4 ragColor = !m_embeddingEnabled ? ImVec4(0.4f, 0.4f, 0.4f, 1.0f)    // disabled — gray
                                        : ragBusy           ? ImVec4(1.0f, 0.78f, 0.2f, 1.0f)   // busy — yellow
                                        : ragUnavailable    ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) // unavailable — red
                                        : statusReady && !partiallyIdx && !fullyOutdated
                                            ? ImVec4(0.31f, 0.86f, 0.39f, 1.0f)          // up to date — green
                                        : partiallyIdx ? ImVec4(1.0f, 0.55f, 0.1f, 1.0f) // partial — orange
                                                       : ImVec4(0.7f, 0.7f, 0.2f, 1.0f); // not indexed / unknown — amber
                ImGui::PushStyleColor(ImGuiCol_Text, ragColor);
                ImGui::TextUnformatted("RAG");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    const char *state = !m_embeddingEnabled           ? "disabled"
                                        : m_isPullingEmbedding        ? "downloading embedding model..."
                                        : m_isVerifyingEmbeddingModel ? "verifying configured local embedding model..."
                                        : m_isFetchingEmbeddingModels ? "refreshing embedding model list..."
                                        : ragIndexing                 ? "indexing codebase..."
                                        : ragLoading                  ? "loading vectors..."
                                        : ragChecking                 ? "checking index status..."
                                        : ragUnavailable              ? "embedding model unavailable"
                                        : partiallyIdx                ? "partially indexed"
                                        : fullyOutdated               ? "not indexed"
                                        : statusReady                 ? "ready"
                                                                      : "not indexed";
                    const std::string embModel = (m_selectedEmbeddingProvider == 2 &&
                                                  m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size()))
                                                     ? m_embeddingModels[m_selectedEmbeddingModel]
                                                     : std::string{};
                    if (embModel.empty())
                        ImGui::SetTooltip("RAG: %s", state);
                    else
                        ImGui::SetTooltip("RAG: %s\n%s", state, embModel.c_str());
                }
            }

            // ••• menu button
            ImGui::SameLine();
            if (ImGui::SmallButton("\xe2\x8b\xaf"))
                ImGui::OpenPopup("AgentMenu");
        }

        // Track indexing state every frame to auto-check status when done
        if (!m_isExternalAI)
        {
            const bool indexingNow = m_gui && m_gui->IsIndexing();
            if (m_wasIndexing && !indexingNow)
                CheckIndexStatus();
            m_wasIndexing = indexingNow;
        }

        // ••• popup menu
        if (ImGui::BeginPopup("AgentMenu"))
        {
            if (ImGui::MenuItem("New Session"))
                NewSession();
            if (ImGui::MenuItem("Sessions..."))
                m_showSessionBrowser = !m_showSessionBrowser;
            if (busy)
                ImGui::BeginDisabled();
            if (ImGui::MenuItem("Compact"))
            {
                auto aliveRef = m_alive;
                const size_t histCount = m_agent->GetHistory().size();
                std::thread([this, aliveRef, histCount]()
                            {
                    const bool ok = m_agent->ForceCompact(2);
                    if (!*aliveRef) return;
                    QueueAction([this, aliveRef, ok, histCount]()
                    {
                        if (!*aliveRef) return;
                        std::lock_guard lock(m_chatMutex);
                        std::string msg = ok ? "History compacted."
                            : "Nothing to compact (" + std::to_string(histCount) + " messages, need more than 2).";
                        m_chat.push_back({ChatMessage::Role::System, msg});
                        m_scrollToBottom = 3;
                    }); })
                    .detach();
            }
            if (busy)
                ImGui::EndDisabled();

            if (!m_isExternalAI)
            {
                const bool indexing = m_gui && m_gui->IsIndexing();
                const bool loading = m_codebaseLoading.load();
                ImGui::Separator();
                if (indexing || loading)
                    ImGui::BeginDisabled();
                if (ImGui::MenuItem("RAG", nullptr, &m_embeddingEnabled))
                {
                    if (m_embeddingEnabled)
                    {
                        m_selectedEmbeddingProvider = 2; // Ollama / nomic-embed-text
                        UpdateEmbeddingModels();
                    }
                    SaveConfig();
                    ConfigureAgent(m_providers[m_selectedProviderIndex].provider);
                }
                if (indexing || loading)
                    ImGui::EndDisabled();

                if (m_embeddingEnabled)
                {
                    ImGui::Separator();
                    if (indexing)
                    {
                        if (ImGui::MenuItem("Stop Indexing"))
                            m_gui->CancelCodebaseIndexing();
                        int p = m_gui->GetIndexProgress();
                        int t = m_gui->GetIndexTotal();
                        char progBuf[64];
                        snprintf(progBuf, sizeof(progBuf), "  %d/%d files", p, t);
                        ImGui::TextDisabled("%s", progBuf);
                    }
                    else if (loading)
                    {
                        ImGui::TextDisabled("  Loading vectors...");
                    }
                    else
                    {
                        const bool hasModel = m_embeddingProvider != nullptr &&
                                              !m_embeddingModels.empty() &&
                                              m_selectedEmbeddingModel < static_cast<int>(m_embeddingModels.size());
                        const bool upToDate = hasModel && m_indexStatusReady.load() && m_indexStatusOutdated == 0;
                        if (!hasModel || upToDate)
                            ImGui::BeginDisabled();
                        if (ImGui::MenuItem("Index Codebase"))
                        {
                            m_gui->StartCodebaseIndexing();
                            m_indexStatusReady.store(false);
                            ImGui::CloseCurrentPopup();
                        }
                        if (!hasModel || upToDate)
                            ImGui::EndDisabled();

                        const bool checking = m_indexStatusChecking.load();
                        if (!hasModel || checking || m_isFetchingEmbeddingModels || m_isVerifyingEmbeddingModel)
                            ImGui::BeginDisabled();
                        if (ImGui::MenuItem("Check Index"))
                            CheckIndexStatus();
                        if (!hasModel || checking || m_isFetchingEmbeddingModels || m_isVerifyingEmbeddingModel)
                            ImGui::EndDisabled();

                        if (m_indexStatusReady.load())
                        {
                            if (m_indexStatusOutdated > 0)
                            {
                                char statusBuf[64];
                                snprintf(statusBuf, sizeof(statusBuf), "  %d/%d outdated",
                                         m_indexStatusOutdated, m_indexStatusTotal);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                                ImGui::TextUnformatted(statusBuf);
                                ImGui::PopStyleColor();
                            }
                            else
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                                ImGui::TextUnformatted("  up to date");
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        // Session browser popup
        if (m_showSessionBrowser)
        {
            ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Sessions##browser", &m_showSessionBrowser))
            {
                auto sessions = ListSessions();
                if (sessions.empty())
                {
                    ImGui::TextDisabled("No saved sessions.");
                }
                else
                {
                    ImGui::TextDisabled("%d session(s) - click to load", static_cast<int>(sessions.size()));
                    ImGui::Separator();
                    for (const auto &path : sessions)
                    {
                        std::string name = std::filesystem::path(path).stem().string();
                        bool isCurrent = (path == m_currentSessionPath);
                        if (isCurrent)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                        if (ImGui::Selectable(name.c_str(), isCurrent))
                        {
                            if (!isCurrent)
                            {
                                SaveSession();
                                LoadSession(path);
                                m_showSessionBrowser = false;
                            }
                        }
                        if (isCurrent)
                            ImGui::PopStyleColor();
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
                        std::string deleteId = "Del##" + name;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                        if (ImGui::SmallButton(deleteId.c_str()))
                        {
                            std::error_code ec;
                            std::filesystem::remove(path, ec);
                            if (isCurrent)
                                m_currentSessionPath.clear();
                        }
                        ImGui::PopStyleColor();
                    }
                }
            }
            ImGui::End();
        }

        ImGui::Separator();

        // chat log
        const float separatorH = ImGui::GetStyle().ItemSpacing.y + 1.0f;
        const float inputHeight = ImGui::GetTextLineHeight() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
        const bool hasAttachments = !m_pendingImages.empty() || !m_pendingFiles.empty();
        const float pendingImgHeight = hasAttachments ? (60.0f + ImGui::GetStyle().ItemSpacing.y) : 0.0f;
        ImGui::BeginChild("ChatLog", ImVec2(0, -(separatorH + inputHeight + pendingImgHeight)), false);
        {
            std::lock_guard lock(m_chatMutex);
            for (const auto &msg : m_chat)
                RenderMessage(msg);

            if (m_isStreaming)
            {
                auto renderStreamingSection = [](const char *label, const std::string &content, const ImVec4 &color)
                {
                    if (content.empty())
                        return;

                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::TreeNode(label))
                    {
                        ImGui::TextWrapped("%s", content.c_str());
                        ImGui::TreePop();
                    }
                    ImGui::PopStyleColor();
                };

                // Show thinking / tools in dimmed collapsible sections
                renderStreamingSection("Thinking...", m_streamingThinking, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
                renderStreamingSection("Tools...", m_streamingTools, ImVec4(0.62f, 0.58f, 0.52f, 1.0f));

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                if (!m_streamingText.empty())
                {
                    ImGui::TextWrapped("%s", m_streamingText.c_str());
                }
                else if (m_streamingThinking.empty() && m_streamingTools.empty())
                {
                    // animated dots while waiting for first token
                    const int dots = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
                    const char *anim[] = {"[AI] .", "[AI] ..", "[AI] ...", "[AI] .."};
                    ImGui::TextUnformatted(anim[dots]);
                }
                ImGui::PopStyleColor();
            }

            if (m_scrollToBottom > 0)
            {
                ImGui::SetScrollHereY(1.0f);
                --m_scrollToBottom;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Ctrl+V image paste (only when not busy and not in external mode)
        if (!busy && !m_isExternalAI && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
            ImGui::GetFrameCount() > m_lastPasteFrame + 1)
        {
            m_lastPasteFrame = ImGui::GetFrameCount();
            HandlePaste();
        }

        RenderPendingAttachments();

        // Steering queue indicator — shown above the input while a message is queued
        if (!m_pendingSteer.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(("Queued: \"" + m_pendingSteer + "\"").c_str());
            ImGui::PopStyleColor();
        }

        bool submit = false;
        // Enter sends. Shift+Enter inserts a newline. Up/Down for history.
        const float inputWidth = ImGui::GetContentRegionAvail().x - 60.0f;

        // Up/Down arrow history - queue text before InputText; apply inside callback
        if (!m_inputHistory.empty() && !busy)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
            {
                if (m_historyIndex < 0)
                    m_historyIndex = static_cast<int>(m_inputHistory.size()) - 1;
                else if (m_historyIndex > 0)
                    m_historyIndex--;
                m_pendingHistoryText = m_inputHistory[m_historyIndex];
                m_pendingHistoryUpdate = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
            {
                if (m_historyIndex >= 0)
                {
                    m_historyIndex++;
                    if (m_historyIndex >= static_cast<int>(m_inputHistory.size()))
                    {
                        m_historyIndex = -1;
                        m_pendingHistoryText.clear();
                    }
                    else
                        m_pendingHistoryText = m_inputHistory[m_historyIndex];
                    m_pendingHistoryUpdate = true;
                }
            }
        }

        auto inputCallback = [](ImGuiInputTextCallbackData *data) -> int
        {
            auto *self = static_cast<AgentWidget *>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && self->m_pendingHistoryUpdate)
            {
                data->DeleteChars(0, data->BufTextLen);
                if (!self->m_pendingHistoryText.empty())
                    data->InsertChars(0, self->m_pendingHistoryText.c_str());
                self->m_pendingHistoryUpdate = false;
            }
            else if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
            {
                if (data->EventChar == '\n' && !ImGui::GetIO().KeyShift)
                    return 1;
            }
            return 0;
        };

        ImGui::InputTextMultiline("##input", m_inputBuf, sizeof(m_inputBuf),
                                  ImVec2(inputWidth, inputHeight),
                                  ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
                                  inputCallback, this);
        bool inputActive = ImGui::IsItemActive();

        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().KeyShift)
            submit = true;
        ImGui::SameLine();
        if (busy)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(55, 0)))
            {
                m_pendingSteer.clear(); // discard any queued steering on manual stop
                if (m_isExternalAI)
                    m_isStreaming = false;
                else if (IsAnyCLI())
                {
                    m_isStreaming = false;
                    m_cliCancelled = true;
                    std::lock_guard lock(m_cliProcessMutex);
                    if (m_cliProcessId != 0)
                    {
#if defined(PE_WIN32)
                        // m_cliProcessId holds the Job Object handle — kills cmd.exe + CLI child
                        TerminateJobObject(reinterpret_cast<HANDLE>(m_cliProcessId), 1);
#else
                        kill(static_cast<pid_t>(m_cliProcessId), SIGTERM);
#endif
                    }
                }
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
        {
            if (busy)
            {
                CommitStreamingNow(); // flush partial response before interrupting
                m_pendingSteer = m_inputBuf;
                ClearInputField();
                m_scrollToBottom = 3;
                if (m_isExternalAI)
                {
                    m_isStreaming = false;
                    FirePendingSteer();
                }
                else if (IsAnyCLI())
                {
                    m_isStreaming = false;
                    m_cliCancelled = true;
                    std::lock_guard lock(m_cliProcessMutex);
                    if (m_cliProcessId != 0)
                    {
#if defined(PE_WIN32)
                        TerminateJobObject(reinterpret_cast<HANDLE>(m_cliProcessId), 1);
#else
                        kill(static_cast<pid_t>(m_cliProcessId), SIGTERM);
#endif
                    }
                }
                else if (m_agent)
                    m_agent->CancelAfterCurrentRound();
            }
            else
            {
                SubmitInput();
            }
        }

        ImGui::End();

        // Full-size image preview window
        if (m_popupImageDescriptor)
        {
            float winW = std::clamp(static_cast<float>(m_popupImageWidth) + 16.0f, 100.0f, 1000.0f);
            float winH = std::clamp(static_cast<float>(m_popupImageHeight) + 40.0f, 100.0f, 1000.0f);
            ImGuiCond sizeFlag = (m_popupImageDescriptor != m_prevPopupImageDescriptor) ? ImGuiCond_Always : ImGuiCond_Appearing;
            m_prevPopupImageDescriptor = m_popupImageDescriptor;
            ImGui::SetNextWindowSize(ImVec2(winW, winH), sizeFlag);
            ImGui::SetNextWindowFocus();
            bool open = true;
            ImGui::Begin("Image Preview", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
            if (!open)
            {
                m_popupImageDescriptor = nullptr;
                m_prevPopupImageDescriptor = nullptr;
            }
            else
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scaleW = avail.x / static_cast<float>(m_popupImageWidth);
                float scaleH = avail.y / static_cast<float>(m_popupImageHeight);
                float scale = std::min(1.0f, std::min(scaleW, scaleH));
                ImGui::Image(reinterpret_cast<ImTextureID>(m_popupImageDescriptor),
                             ImVec2(m_popupImageWidth * scale, m_popupImageHeight * scale));
            }
            ImGui::End();
        }
    }

    void AgentWidget::CommitStreamingNow()
    {
        if (m_streamingText.empty() && m_streamingThinking.empty() && m_streamingTools.empty())
            return;
        ChatMessage msg;
        msg.role = ChatMessage::Role::Assistant;
        msg.text = m_streamingText;
        msg.thinking = m_streamingThinking;
        msg.tools = m_streamingTools;
        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back(std::move(msg));
        }
        m_streamingText.clear();
        m_streamingThinking.clear();
        m_streamingTools.clear();
        m_scrollToBottom = 3;
    }

    void AgentWidget::FirePendingSteer()
    {
        if (m_pendingSteer.empty())
            return;

        std::string steer = std::move(m_pendingSteer);
        m_pendingSteer.clear();

        // Always defer queued steering to the next action flush so we never
        // re-enter SubmitInput while some completion/error path still holds
        // m_chatMutex.
        QueueAction([this, steer = std::move(steer)]()
                    { SubmitInputText(steer); });
    }

    void AgentWidget::ClearInputField()
    {
        m_inputBuf[0] = '\0';
        m_pendingHistoryText.clear();
        m_pendingHistoryUpdate = true;
        m_historyIndex = -1;
    }

    void AgentWidget::SubmitInput()
    {
        std::string text(m_inputBuf);
        ClearInputField();
        SubmitInputText(text);
    }

    void AgentWidget::SubmitInputText(const std::string &text)
    {
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
        for (const auto &pf : m_pendingFiles)
            chatMsg.attachments.push_back({pf.name, pf.iconDS});

        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back(std::move(chatMsg));
            m_scrollToBottom = 3;
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
        else if (IsAnyCLI())
        {
            // Save pending images to Temp/ so the CLI tool can open them by path
            std::string prompt = fullText;
            if (!m_pendingImages.empty())
            {
                std::string tempDir = Path::Assets + "Temp/";
                if (!std::filesystem::exists(tempDir))
                    std::filesystem::create_directories(tempDir);
                auto now = std::chrono::system_clock::now();
                auto t = std::chrono::system_clock::to_time_t(now);
                char timeBuf[32];
                std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", std::localtime(&t));

                prompt += "\n\n[Attached images:";
                for (size_t i = 0; i < m_pendingImages.size(); ++i)
                {
                    const auto &img = m_pendingImages[i];
                    if (img.base64.empty())
                        continue;
                    auto bytes = pagent::Base64Decode(img.base64);
                    if (bytes.empty())
                        continue;
                    std::string ext = (img.mime_type == "image/jpeg") ? ".jpg" : ".png";
                    std::string filename = std::string("paste_") + timeBuf;
                    if (m_pendingImages.size() > 1)
                        filename += "_" + std::to_string(i);
                    filename += ext;
                    std::string fullPath = tempDir + filename;
                    std::ofstream file(fullPath, std::ios::binary);
                    if (file.is_open())
                        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                    prompt += "\n- " + fullPath;
                }
                prompt += "]";
            }
            m_isStreaming = true;
            std::thread([this, prompt]()
                        {
                if (m_isCodexCLI)       RunCodexCLI(prompt);
                else if (m_isClaudeCLI) RunClaudeCLI(prompt);
                else if (m_isGeminiCLI) RunGeminiCLI(prompt); })
                .detach();
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
        // Save all pasted content to Assets/Temp/
        if (!m_pendingImages.empty() || !m_pendingFiles.empty())
        {
            std::string tempDir = Path::Assets + "Temp/";
            if (!std::filesystem::exists(tempDir))
                std::filesystem::create_directories(tempDir);

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            char timeBuf[32];
            std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", std::localtime(&time));

            for (size_t i = 0; i < m_pendingImages.size(); ++i)
            {
                const auto &img = m_pendingImages[i];
                if (img.base64.empty())
                    continue;
                auto bytes = pagent::Base64Decode(img.base64);
                if (bytes.empty())
                    continue;

                std::string ext = (img.mime_type == "image/jpeg") ? ".jpg" : ".png";
                std::string filename = std::string("paste_") + timeBuf;
                if (m_pendingImages.size() > 1)
                    filename += "_" + std::to_string(i);
                filename += ext;

                std::ofstream file(tempDir + filename, std::ios::binary);
                if (file.is_open())
                    file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            }

            for (const auto &pf : m_pendingFiles)
            {
                if (pf.content.empty())
                    continue;
                std::string filename = std::string("paste_") + timeBuf + "_" + pf.name;
                std::ofstream file(tempDir + filename, std::ios::out);
                if (file.is_open())
                    file << pf.content;
            }
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

            if (!base64.empty())
            {
                PendingImage img;
                img.base64 = std::move(base64);
                img.mime_type = std::move(mime_type);
                img.width = w;
                img.height = h;
                m_pendingImages.push_back(std::move(img));
                CloseClipboard();
                return;
            }
        }

        // Try CF_HDROP: files copied from Explorer
        if (base64.empty())
        {
            HANDLE hDrop = GetClipboardData(CF_HDROP);
            if (hDrop)
            {
                HDROP drop = static_cast<HDROP>(hDrop);
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                std::vector<std::filesystem::path> paths;
                paths.reserve(count);
                for (UINT fi = 0; fi < count; fi++)
                {
                    wchar_t filePath[MAX_PATH] = {};
                    DragQueryFileW(drop, fi, filePath, MAX_PATH);
                    paths.emplace_back(filePath);
                }
                if (!paths.empty())
                    ProcessDroppedFiles(paths);
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
                int nw, nh;
                rgba = ResizeRGBA(rgba.data(), w, h, nw, nh);
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
        m_pendingImages.push_back(std::move(img));
#endif
    }

    void AgentWidget::ProcessDroppedFiles(const std::vector<std::filesystem::path> &paths)
    {
        for (const auto &fp : paths)
        {
            if (!std::filesystem::is_regular_file(fp))
                continue;
            auto fileSize = std::filesystem::file_size(fp);
            if (fileSize == 0 || fileSize > 50 * 1024 * 1024) // skip empty or >50MB
                continue;

            std::string ext = ToLower(fp.extension().string());

            bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                            ext == ".bmp" || ext == ".tga" || ext == ".gif" || ext == ".webp");
            if (isImage)
            {
                std::string mime_type;
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
                size_t fsize = static_cast<size_t>(file.tellg());
                file.seekg(0);
                std::vector<uint8_t> fileData(fsize);
                file.read(reinterpret_cast<char *>(fileData.data()), fsize);

                int w = 0, h = 0;
                std::string base64;
                bool needsDecode = (ext != ".png" && ext != ".jpg" && ext != ".jpeg") || fsize > 4 * 1024 * 1024;
                if (!needsDecode)
                {
                    int comp = 0;
                    stbi_info_from_memory(fileData.data(), static_cast<int>(fsize), &w, &h, &comp);
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
                        if (w > 1024 || h > 1024)
                        {
                            int nw, nh;
                            auto resized = ResizeRGBA(pixels, w, h, nw, nh);
                            stbi_image_free(pixels);
                            auto pngData = pagent::EncodeRGBA_PNG(resized.data(), nw, nh);
                            base64 = pagent::Base64Encode(pngData.data(), pngData.size());
                            w = nw;
                            h = nh;
                        }
                        else
                        {
                            auto pngData = pagent::EncodeRGBA_PNG(pixels, w, h);
                            base64 = pagent::Base64Encode(pngData.data(), pngData.size());
                            stbi_image_free(pixels);
                        }
                        mime_type = "image/png";
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
                }
            }
            else
            {
                PendingFile pf;
                pf.name = fp.filename().string();

                if (FileBrowser::IsModelFile(fp))
                {
                    pf.content = "[Model file: " + fp.string() + "]";
                    pf.iconDS = m_modelIconDS ? m_modelIconDS : m_fileIconDS;
                }
                else
                {
                    constexpr size_t maxChars = 20000;
                    std::ifstream file(fp, std::ios::in);
                    if (!file.is_open())
                        continue;
                    std::string content((std::istreambuf_iterator<char>(file)), {});
                    if (content.empty())
                        continue;
                    content = SanitizeUTF8(std::move(content));
                    if (content.size() > maxChars)
                    {
                        content.resize(maxChars);
                        content += "\n... [truncated at 20k chars]";
                    }
                    pf.content = std::move(content);
                    if (FileBrowser::IsShaderFile(fp))
                        pf.iconDS = m_shaderIconDS ? m_shaderIconDS : m_fileIconDS;
                    else if (FileBrowser::IsScriptFile(fp))
                        pf.iconDS = m_scriptIconDS ? m_scriptIconDS : m_fileIconDS;
                    else if (FileBrowser::IsTextFile(fp))
                        pf.iconDS = m_txtIconDS ? m_txtIconDS : m_fileIconDS;
                    else
                        pf.iconDS = m_fileIconDS;
                }

                m_pendingFiles.push_back(std::move(pf));
            }
        }
    }

    void AgentWidget::RenderPendingAttachments()
    {
        if (m_pendingImages.empty() && m_pendingFiles.empty())
            return;

        float thumbH = 48.0f;
        float totalH = thumbH + 12.0f; // padding
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.2f, 1.0f));
        ImGui::BeginChild("PendingAttachments", ImVec2(0, totalH), true);

        for (size_t i = 0; i < m_pendingImages.size(); i++)
        {
            const auto &img = m_pendingImages[i];
            if (img.imguiDescriptor && img.width > 0 && img.height > 0)
            {
                float scale = thumbH / static_cast<float>(img.height);
                ImGui::PushID(static_cast<int>(i));
                ImGui::ImageButton("##pendimg", reinterpret_cast<ImTextureID>(img.imguiDescriptor), ImVec2(img.width * scale, thumbH));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to preview (%dx%d)", img.width, img.height);
                if (ImGui::IsItemClicked())
                {
                    m_popupImageDescriptor = img.imguiDescriptor;
                    m_popupImageWidth = img.width;
                    m_popupImageHeight = img.height;
                }
                ImGui::PopID();
            }
            else
            {
                ImGui::Text("[Image %dx%d]", img.width, img.height);
            }
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
            if (pf.iconDS)
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(pf.iconDS), ImVec2(thumbH, thumbH));
                ImGui::SameLine();
            }
            ImGui::Text("%s\n(%d bytes)", pf.name.c_str(), static_cast<int>(pf.content.size()));
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

    void *AgentWidget::UploadChatImage(const uint8_t *rgba, int width, int height)
    {
        static int s_chatImageCounter = 0;
        std::string name = "ChatThumbnail_" + std::to_string(s_chatImageCounter++);

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        Image *gpuImage = Image::LoadRawFromMemory(cmd, const_cast<uint8_t *>(rgba),
                                                   static_cast<uint32_t>(width),
                                                   static_cast<uint32_t>(height),
                                                   vk::Format::eR8G8B8A8Unorm,
                                                   name);

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (!gpuImage)
            return nullptr;

        if (!gpuImage->HasSRV())
            gpuImage->CreateSRV(vk::ImageViewType::e2D);

        if (!gpuImage->GetSampler())
        {
            vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
            Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
            gpuImage->SetSampler(sampler);
        }

        VkSampler sampler = gpuImage->GetSampler()->ApiHandle();
        VkImageView view = gpuImage->GetSRV()->ApiHandle();
        void *descriptor = (void *)ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_chatGpuImages.push_back(gpuImage);
        m_chatImguiDescriptors.push_back(descriptor);

        return descriptor;
    }

    void AgentWidget::HandleScreenshotEvent()
    {
        auto *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return;

        // Check if a screenshot was just saved (path is set after save)
        std::string path = renderer->GetScreenshotSavedPath();
        if (path.empty())
            return;

        // Load the saved BMP as a thumbnail
        int tw = 0, th = 0, tc = 0;
        uint8_t *pixels = stbi_load(path.c_str(), &tw, &th, &tc, 4);

        ChatMessage msg;
        msg.role = ChatMessage::Role::System;
        msg.text = "Screenshot saved: " + path;

        if (pixels && tw > 0 && th > 0)
        {
            ChatImage chatImg;
            chatImg.width = tw;
            chatImg.height = th;
            chatImg.imguiDescriptor = UploadChatImage(pixels, tw, th);
            msg.images.push_back(chatImg);
            stbi_image_free(pixels);
        }

        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back(std::move(msg));
            m_scrollToBottom = 3;
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
            m_scrollToBottom = 3;
            break;
        case pagent::AgentEventType::ThinkingComplete:
            // Thinking is stored and will be attached to the next TextComplete message
            m_scrollToBottom = 3;
            break;
        case pagent::AgentEventType::TextDelta:
            m_streamingText += ev.text;
            m_isStreaming = true;
            m_scrollToBottom = 3;
            break;
        case pagent::AgentEventType::TextComplete:
        {
            ChatMessage msg;
            msg.role = ChatMessage::Role::Assistant;
            msg.text = ev.text;
            msg.thinking = m_streamingThinking;
            msg.tools = m_streamingTools;
            if (!msg.text.empty() || !msg.thinking.empty() || !msg.tools.empty())
                m_chat.push_back(std::move(msg));
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            m_isStreaming = false;
            m_scrollToBottom = 3;
            break;
        }
        case pagent::AgentEventType::ToolCallBegin:
            if (!m_streamingTools.empty())
                m_streamingTools += "\n\n";
            m_streamingTools += "[Tool: " + ev.tool_name + "]";
            m_isStreaming = true;
            m_scrollToBottom = 3;
            break;
        case pagent::AgentEventType::ToolCallComplete:
        {
            if (!ev.tool_input_json.empty())
            {
                if (!m_streamingTools.empty())
                    m_streamingTools += "\n";
                m_streamingTools += ev.tool_input_json;
            }
            m_isStreaming = true;
            m_scrollToBottom = 3;
            break;
        }
        case pagent::AgentEventType::Usage:
            break; // handled internally by Agent::Impl::Poll()
        case pagent::AgentEventType::TurnComplete:
            if (!m_streamingText.empty() || !m_streamingThinking.empty() || !m_streamingTools.empty())
            {
                ChatMessage msg;
                msg.role = ChatMessage::Role::Assistant;
                msg.text = m_streamingText;
                msg.thinking = m_streamingThinking;
                msg.tools = m_streamingTools;
                m_chat.push_back(std::move(msg));
            }
            m_isStreaming = false;
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            // Auto-save session after every completed turn (mutex already held - defer to next frame)
            {
                auto *self = this;
                QueueAction([self]()
                            {
                                self->SaveSession();
                                self->FirePendingSteer(); });
            }
            break;
        case pagent::AgentEventType::Error:
            // Suppress "cancelled" error when it was triggered by a steering interrupt
            if (m_pendingSteer.empty())
                m_chat.push_back({ChatMessage::Role::System, "[error: " + ev.error_message + "]"});
            m_isStreaming = false;
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            m_scrollToBottom = 3;
            FirePendingSteer();
            break;
        case pagent::AgentEventType::Info:
            m_chat.push_back({ChatMessage::Role::System, ev.text});
            m_scrollToBottom = 3;
            break;
        default:
            break;
        }
    }

    void AgentWidget::RenderMessage(const ChatMessage &msg)
    {
        // Helper: render a clickable thumbnail that opens full-size popup on click
        auto renderImages = [this](const std::vector<ChatImage> &images)
        {
            for (size_t i = 0; i < images.size(); i++)
            {
                const auto &img = images[i];
                if (img.imguiDescriptor)
                {
                    // Thumbnail: max 128px height
                    float thumbH = std::min(128.0f, static_cast<float>(img.height));
                    float scale = thumbH / static_cast<float>(img.height);
                    float thumbW = img.width * scale;
                    ImGui::PushID(img.imguiDescriptor);
                    ImGui::ImageButton("##chatimg", reinterpret_cast<ImTextureID>(img.imguiDescriptor), ImVec2(thumbW, thumbH));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to view full size (%dx%d)", img.width, img.height);
                    if (ImGui::IsItemClicked())
                    {
                        m_popupImageDescriptor = img.imguiDescriptor;
                        m_popupImageWidth = img.width;
                        m_popupImageHeight = img.height;
                    }
                    ImGui::PopID();
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
                    ImGui::Text("  [Image %dx%d]", img.width, img.height);
                    ImGui::PopStyleColor();
                }
            }
        };

        // Helper: render file attachments with icons
        auto renderAttachments = [](const std::vector<ChatFileAttachment> &attachments)
        {
            for (const auto &fa : attachments)
            {
                if (fa.iconDS)
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(fa.iconDS), ImVec2(16.0f, 16.0f));
                    ImGui::SameLine();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                ImGui::Text("[Attached: %s]", fa.name.c_str());
                ImGui::PopStyleColor();
            }
        };

        switch (msg.role)
        {
        case ChatMessage::Role::User:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::TextWrapped("[You] %s", msg.text.c_str());
            ImGui::PopStyleColor();
            renderImages(msg.images);
            renderAttachments(msg.attachments);
            break;
        case ChatMessage::Role::Assistant:
        {
            auto renderAssistantSection = [&msg](const char *label, const std::string &content, const ImVec4 &color)
            {
                if (content.empty())
                    return;

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::PushID(&msg);
                if (ImGui::TreeNode(label))
                {
                    ImGui::TextWrapped("%s", content.c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
                ImGui::PopStyleColor();
            };

            renderAssistantSection("Thinking", msg.thinking, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
            renderAssistantSection("Tools", msg.tools, ImVec4(0.62f, 0.58f, 0.52f, 1.0f));
            if (!msg.text.empty())
                ImGui::TextWrapped("[AI] %s", msg.text.c_str());
            break;
        }
        case ChatMessage::Role::System:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::PopStyleColor();
            renderImages(msg.images);
            break;
        }
        ImGui::Spacing();
    }
} // namespace pe