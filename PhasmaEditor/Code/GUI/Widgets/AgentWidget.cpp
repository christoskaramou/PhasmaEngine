#include "AgentWidget.h"
#include "GUI/GUI.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "Camera/Camera.h"
#include "Base/Timer.h"
#include "Base/Settings.h"
#include "Base/Path.h"
#include "PhasmaAgent/AgentUtils.h"
#include "imgui/imgui.h"

using namespace pagent;

namespace pe
{
    AgentWidget::AgentWidget() : Widget("Agent"), m_agent(pagent::AgentConfig{})
    {
    }

    void AgentWidget::Init(GUI *gui)
    {
        Widget::Init(gui);

        pagent::AgentConfig config;
        config.system_prompt =
            "You are an AI assistant inside PhasmaEditor, a Vulkan 3D engine editor. "
            "Use the available tools to inspect the scene and renderer. Be concise. "
            "Executable directory: " +
            Path::Executable + " | "
                               "Assets directory: " +
            Path::Assets + " | "
                           "To load a model, always use find_file first to locate it, then call load_model with the full path.";
        config.log_callback = [](const std::string &msg)
        { PE_INFO("%s", msg.c_str()); };
        config.max_tool_rounds = 20;

        const char *ollamaUrl = std::getenv("PAGENT_OLLAMA_URL");
        const char *apiKey = std::getenv("PAGENT_API_KEY");
        const char *modelEnv = std::getenv("PAGENT_MODEL");
        const char *providerEnv = std::getenv("PAGENT_PROVIDER"); // "anthropic" | "openai" | "gemini" | "ollama"

        std::string providerStr = providerEnv ? providerEnv : "";
        for (auto &c : providerStr)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Resolve effective Ollama URL: explicit env var, or default when provider=ollama
        std::string effectiveOllamaUrl;
        if (ollamaUrl)
            effectiveOllamaUrl = ollamaUrl;
        else if (providerStr == "ollama")
            effectiveOllamaUrl = "http://localhost:11434";

        if (!effectiveOllamaUrl.empty())
        {
            config.provider = pagent::Provider::OpenAI;
            config.base_url = effectiveOllamaUrl;
            config.model = modelEnv ? modelEnv : "llama3.2";
            m_agentConfigured = true;
        }
        else if (apiKey)
        {
            if (providerStr == "openai")
            {
                config.provider = pagent::Provider::OpenAI;
                config.api_key = apiKey;
                config.model = modelEnv ? modelEnv : "gpt-4o";
            }
            else if (providerStr == "gemini")
            {
                config.provider = pagent::Provider::OpenAI;
                config.base_url = "https://generativelanguage.googleapis.com/v1beta/openai";
                config.api_key = apiKey;
                config.model = modelEnv ? modelEnv : "gemini-2.0-flash";
            }
            else if (providerStr == "anthropic")
            {
                config.provider = pagent::Provider::Anthropic;
                config.api_key = apiKey;
                config.model = modelEnv ? modelEnv : "claude-sonnet-4-6";
            }
            else
            {
                PE_WARN("Unknown PAGENT_PROVIDER '%s', defaulting to Anthropic. Supported values: 'anthropic', 'openai', 'gemini', 'ollama'.", providerStr.c_str());
                config.provider = pagent::Provider::Anthropic;
                config.api_key = apiKey;
                config.model = modelEnv ? modelEnv : "claude-sonnet-4-6";
            }
            m_agentConfigured = true;
        }
        else
        {
            PE_WARN("AgentWidget: Set PAGENT_API_KEY (+ optionally PAGENT_PROVIDER=anthropic|openai) or PAGENT_OLLAMA_URL to enable the agent.");
        }
        m_modelName = config.model;

        m_agent = pagent::Agent(std::move(config));
        m_agent.SetEventCallback([this](const pagent::AgentEvent &ev)
                                 { OnAgentEvent(ev); });

        RegisterTools();
    }

    void AgentWidget::RegisterTools()
    {
        m_agent.RegisterTool({.name = "get_scene_info",
                              .description = "Returns loaded models and entity count.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";
                                  auto &models = r->GetScene().GetModels();
                                  std::string arr = "[";
                                  bool first = true;
                                  for (const auto &m : models)
                                  {
                                      if (!first)
                                          arr += ",";
                                      arr += "{\"name\":" + JsonStr(m->GetFilePath().filename().string()) + "}";
                                      first = false;
                                  }
                                  arr += "]";
                                  return JsonObj({{"model_count", std::to_string(models.size())}, {"models", arr}});
                              }});

        m_agent.RegisterTool({.name = "get_metrics",
                              .description = "Returns FPS and frame delta time in milliseconds.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  const double dt = FrameTimer::Instance().GetDelta();
                                  const double fps = dt > 0.0 ? 1.0 / dt : 0.0;
                                  return JsonObj({{"fps", std::to_string(fps)}, {"delta_ms", std::to_string(dt * 1000.0)}});
                              }});

        m_agent.RegisterTool({.name = "compile_shaders",
                              .description = "Recompiles all HLSL shaders.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  EventSystem::DispatchEvent(EventType::CompileShaders, {});
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "get_render_settings",
                              .description = "Returns current render pass settings (bloom, ssao, shadows, taa, fxaa, ssr, dof, motion_blur, draw_grid, render_scale).",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  auto &s = Settings::Get<GlobalSettings>();
                                  return JsonObj({
                                      {"bloom", s.bloom ? "true" : "false"},
                                      {"ssao", s.ssao ? "true" : "false"},
                                      {"shadows", s.shadows ? "true" : "false"},
                                      {"taa", s.taa ? "true" : "false"},
                                      {"fxaa", s.fxaa ? "true" : "false"},
                                      {"ssr", s.ssr ? "true" : "false"},
                                      {"dof", s.dof ? "true" : "false"},
                                      {"motion_blur", s.motion_blur ? "true" : "false"},
                                      {"draw_grid", s.draw_grid ? "true" : "false"},
                                      {"render_scale", std::to_string(s.render_scale)},
                                  });
                              }});

        m_agent.RegisterTool({.name = "set_render_setting",
                              .description = "Enable or disable a render setting. name: bloom|ssao|shadows|taa|fxaa|ssr|dof|motion_blur|draw_grid. value: true|false.",
                              .properties = {
                                  {"name", "Setting name (bloom, ssao, shadows, taa, fxaa, ssr, dof, motion_blur, draw_grid)", pagent::SchemaType::String, true},
                                  {"value", "true or false", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string name = ExtractArgStr(args, "name");
                                  std::string value = ExtractArgStr(args, "value");
                                  bool on = (value == "true" || value == "1");
                                  QueueAction([name, on]()
                                              {
                    auto &s = Settings::Get<GlobalSettings>();
                    if      (name == "bloom")       s.bloom       = on;
                    else if (name == "ssao")        s.ssao        = on;
                    else if (name == "shadows")     s.shadows     = on;
                    else if (name == "taa")         s.taa         = on;
                    else if (name == "fxaa")        s.fxaa        = on;
                    else if (name == "ssr")         s.ssr         = on;
                    else if (name == "dof")         s.dof         = on;
                    else if (name == "motion_blur") s.motion_blur = on;
                    else if (name == "draw_grid")   s.draw_grid   = on; });
                                  return JsonObj({{"setting", JsonStr(name)}, {"value", JsonStr(value)}, {"status", JsonStr("queued")}});
                              }});

        m_agent.RegisterTool({.name = "get_camera_info",
                              .description = "Returns active camera position (x,y,z), pitch/yaw in degrees, and horizontal FOV in degrees.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";
                                  auto *cam = r->GetScene().GetActiveCamera();
                                  if (!cam)
                                      return "{\"error\":\"no active camera\"}";
                                  auto pos = cam->GetPosition();
                                  auto euler = cam->GetEuler();
                                  return JsonObj({
                                      {"pos_x", std::to_string(pos.x)},
                                      {"pos_y", std::to_string(pos.y)},
                                      {"pos_z", std::to_string(pos.z)},
                                      {"pitch", std::to_string(glm::degrees(euler.x))},
                                      {"yaw", std::to_string(glm::degrees(euler.y))},
                                      {"fov", std::to_string(glm::degrees(cam->Fovx()))},
                                  });
                              }});

        m_agent.RegisterTool({.name = "set_camera_position",
                              .description = "Moves the active camera to the given world position.",
                              .properties = {
                                  {"x", "World X coordinate", pagent::SchemaType::Number, true},
                                  {"y", "World Y coordinate", pagent::SchemaType::Number, true},
                                  {"z", "World Z coordinate", pagent::SchemaType::Number, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");
                                  QueueAction([x, y, z]()
                                              {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    auto *cam = r->GetScene().GetActiveCamera();
                    if (cam) cam->SetPosition(vec3(x, y, z)); });
                                  return JsonObj({{"status", JsonStr("queued")}, {"x", std::to_string(x)}, {"y", std::to_string(y)}, {"z", std::to_string(z)}});
                              }});

        m_agent.RegisterTool({.name = "load_model",
                              .description = "Loads a 3D model file (glTF, FBX, OBJ, etc.) and adds it to the scene. Use find_file first to locate the model path.",
                              .properties = {
                                  {"path", "Absolute or relative file path to the model", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";
                                  if (!std::filesystem::exists(path))
                                      return JsonObj({{"error", JsonStr("file not found: " + path)}});

                                  QueueAction([path]()
                                              { Model::Load(path); });
                                  return JsonObj({{"status", JsonStr("loading")}, {"path", JsonStr(path)}});
                              }});

        m_agent.RegisterTool({.name = "find_file",
                              .description = "Recursively searches for files whose name contains the given query string (case-insensitive). Use this instead of list_directory to locate models.",
                              .properties = {
                                  {"query", "Filename substring to search for (e.g. 'sponza', '.gltf')", pagent::SchemaType::String, true},
                                  {"root", "Directory to search from. Defaults to Assets directory if empty.", pagent::SchemaType::String, false},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "query"));
                                  std::string root = JsonUnescape(ExtractArgStr(args, "root"));
                                  if (query.empty())
                                      return "{\"error\":\"missing query\"}";
                                  if (root.empty())
                                      root = Path::Assets;
                                  if (!std::filesystem::exists(root))
                                      return JsonObj({{"error", JsonStr("root not found: " + root)}});

                                  // case-insensitive query
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  std::string results = "[";
                                  bool first = true;
                                  int count = 0;
                                  for (const auto &entry : std::filesystem::recursive_directory_iterator(
                                           root, std::filesystem::directory_options::skip_permission_denied))
                                  {
                                      if (!entry.is_regular_file())
                                          continue;
                                      std::string name = entry.path().filename().string();
                                      std::string nameLower = name;
                                      for (auto &c : nameLower)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (nameLower.find(queryLower) == std::string::npos)
                                          continue;
                                      // skip non-ASCII
                                      bool hasNonAscii = false;
                                      for (unsigned char c : name)
                                          if (c > 127)
                                          {
                                              hasNonAscii = true;
                                              break;
                                          }
                                      if (hasNonAscii)
                                          continue;

                                      std::string fullPath = entry.path().string();
                                      std::replace(fullPath.begin(), fullPath.end(), '\\', '/');
                                      if (!first)
                                          results += ",";
                                      results += "{\"name\":" + JsonStr(name) + ",\"full_path\":" + JsonStr(fullPath) + "}";
                                      first = false;
                                      if (++count >= 20)
                                          break; // cap results
                                  }
                                  results += "]";
                                  return JsonObj({{"query", JsonStr(query)}, {"results", results}});
                              }});

        m_agent.RegisterTool({.name = "list_directory",
                              .description = "Lists files and subdirectories at the given path. Prefer find_file for locating models.",
                              .properties = {
                                  {"path", "Directory path to list (use Assets directory from system prompt as starting point)", pagent::SchemaType::String, true},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string dir = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (dir.empty())
                                      return "{\"error\":\"missing path\"}";
                                  if (!std::filesystem::exists(dir))
                                      return JsonObj({{"error", JsonStr("path not found: " + dir)}});
                                  if (!std::filesystem::is_directory(dir))
                                      return JsonObj({{"error", JsonStr("not a directory: " + dir)}});

                                  std::string base = dir;
                                  std::replace(base.begin(), base.end(), '\\', '/');
                                  if (!base.empty() && base.back() != '/')
                                      base += '/';

                                  std::string files = "[", dirs = "[";
                                  bool firstF = true, firstD = true;
                                  for (const auto &entry : std::filesystem::directory_iterator(dir))
                                  {
                                      std::string name = entry.path().filename().string();
                                      // skip non-ASCII filenames to avoid JSON encoding issues
                                      bool hasNonAscii = false;
                                      for (unsigned char c : name)
                                          if (c > 127)
                                          {
                                              hasNonAscii = true;
                                              break;
                                          }
                                      if (hasNonAscii)
                                          continue;

                                      std::string obj = "{\"name\":" + JsonStr(name) + ",\"full_path\":" + JsonStr(base + name) + "}";
                                      if (entry.is_directory())
                                      {
                                          if (!firstD)
                                              dirs += ",";
                                          dirs += obj;
                                          firstD = false;
                                      }
                                      else
                                      {
                                          if (!firstF)
                                              files += ",";
                                          files += obj;
                                          firstF = false;
                                      }
                                  }
                                  files += "]";
                                  dirs += "]";
                                  return JsonObj({{"directory", JsonStr(base)}, {"files", files}, {"subdirs", dirs}});
                              }});
    }

    void AgentWidget::QueueAction(std::function<void()> fn)
    {
        std::lock_guard lock(m_actionMutex);
        m_pendingActions.push_back(std::move(fn));
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
        m_agent.Poll(); // queued events on the main thread

        if (!m_open)
            return;

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }

        const bool busy = m_agent.IsBusy();

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
            if (!m_agentConfigured)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted("Not configured");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextUnformatted(m_modelName.c_str());
            }
        }
        ImGui::Separator();

        // chat log
        const float statusHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        const float inputHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        ImGui::BeginChild("ChatLog", ImVec2(0, -(inputHeight + statusHeight)), false);
        {
            std::lock_guard lock(m_chatMutex);
            for (const auto &msg : m_chat)
                RenderMessage(msg);

            if (m_isStreaming)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                if (!m_streamingText.empty())
                {
                    ImGui::TextWrapped("%s", m_streamingText.c_str());
                }
                else
                {
                    // animated thinking dots while waiting for first token
                    const int dots = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
                    const char *thinking[] = {"[AI] .", "[AI] ..", "[AI] ...", "[AI] .."};
                    ImGui::TextUnformatted(thinking[dots]);
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
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::InputText("##input", m_inputBuf, sizeof(m_inputBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            submit = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (busy)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(55, 0)))
                m_agent.CancelPending();
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
        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back({ChatMessage::Role::User, text});
            m_scrollToBottom = true;
        }
        m_agent.Send(text);
        ImGui::SetKeyboardFocusHere(-1);
    }

    void AgentWidget::OnAgentEvent(const pagent::AgentEvent &ev)
    {
        std::lock_guard lock(m_chatMutex);
        switch (ev.type)
        {
        case pagent::AgentEventType::TextDelta:
            m_streamingText += ev.text;
            m_isStreaming = true;
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::TextComplete:
            if (!ev.text.empty())
                m_chat.push_back({ChatMessage::Role::Assistant, ev.text});
            m_streamingText.clear();
            m_isStreaming = false;
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::ToolCallBegin:
            m_chat.push_back({ChatMessage::Role::System, "[calling: " + ev.tool_name + "]"});
            m_scrollToBottom = true;
            break;
        case pagent::AgentEventType::TurnComplete:
            m_isStreaming = false;
            m_streamingText.clear();
            break;
        case pagent::AgentEventType::Error:
            m_chat.push_back({ChatMessage::Role::System, "[error: " + ev.error_message + "]"});
            m_isStreaming = false;
            m_streamingText.clear();
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
