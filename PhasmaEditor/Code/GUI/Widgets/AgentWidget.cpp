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
            "You can build and modify 3D scenes: load/remove models, clone_model, scatter_models, "
            "create primitives (cube/sphere/plane/cylinder/cone) with position, rotation, and per-axis scale in one call, "
            "move/scale/rotate existing objects with set_model_transform, manage lights (add_light, set_light, remove_light, get_lights), "
            "control the camera (set_camera_position, set_camera_rotation, set_camera_fov, focus_camera_on_model, set_camera_dof_focus), "
            "edit materials (get_materials, set_material_property), "
            "tweak post-processing (set_render_setting for booleans, set_render_value for floats like bloom_strength/IBL_intensity), "
            "switch render modes (set_render_mode: raster|hybrid|raytracing), toggle day/night (set_time_of_day), change skybox (set_skybox_hdr), "
            "read, write, and modify HLSL shaders (list_shaders, read_shader, write_shader — changes auto-trigger hot-reload), "
            "and save/load scenes (save_scene, load_scene). "
            "To load a model, use list_models first to find available assets, then call load_model with the full_path from the result. "
            "IMPORTANT: When creating primitives or loading models, always provide a unique label (e.g. 'floor', 'front_wall', 'ball_1'). "
            "Prefer setting position/rotation/scale directly in create_primitive rather than calling set_model_transform separately. "
            "To modify a shader: use read_shader to see its source, then edit_shader with find/replace to make targeted changes (hot-reload happens automatically). Use write_shader only for creating new shader files. "
            "ALWAYS use your tools to perform actions. NEVER tell the user to do something manually when a tool can do it. "
            "Do not use emoji or Unicode symbols in responses. Use plain ASCII text only. "
            "Be concise and chain tool calls to complete multi-step tasks without asking for confirmation. "
            "Executable directory: " +
            Path::Executable + " | "
                               "Assets directory: " +
            Path::Assets + ".";
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
        m_agent.RegisterTool({.name = "list_models",
                              .description = "Lists all available 3D model assets that can be loaded. Returns relative paths under Assets/Objects/. Use this before load_model to find the correct path.",
                              .properties = {
                                  {"filter", "Optional substring to filter model names (case-insensitive)", pagent::SchemaType::String, false},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string filter = ExtractArgStr(args, "filter");
                                  for (auto &c : filter)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  const auto &modelList = Settings::Get<GlobalSettings>().model_list;
                                  std::string arr = "[";
                                  bool first = true;
                                  int count = 0;
                                  for (const auto &entry : modelList)
                                  {
                                      if (!filter.empty())
                                      {
                                          std::string lower = entry;
                                          for (auto &c : lower)
                                              c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (lower.find(filter) == std::string::npos)
                                              continue;
                                      }
                                      if (!first)
                                          arr += ",";
                                      arr += JsonStr(entry);
                                      first = false;
                                      if (++count >= 50)
                                          break;
                                  }
                                  arr += "]";
                                  return JsonObj({{"count", std::to_string(count)}, {"models", arr}});
                              }});

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
                                      arr += JsonStr(m->GetFilePath().filename().string());
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

        m_agent.RegisterTool({.name = "list_shaders",
                              .description = "Lists all HLSL shader files in the Shaders directory. Returns file names and paths relative to Assets/Shaders/.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  std::string shadersDir = Path::Assets + "Shaders";
                                  if (!std::filesystem::exists(shadersDir))
                                      return JsonObj({{"error", JsonStr("Shaders directory not found")}});

                                  std::string arr = "[";
                                  bool first = true;
                                  for (const auto &entry : std::filesystem::recursive_directory_iterator(
                                           shadersDir, std::filesystem::directory_options::skip_permission_denied))
                                  {
                                      if (!entry.is_regular_file())
                                          continue;
                                      std::string ext = entry.path().extension().string();
                                      for (auto &c : ext)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (ext != ".hlsl")
                                          continue;

                                      std::string relPath = std::filesystem::relative(entry.path(), shadersDir).string();
                                      std::replace(relPath.begin(), relPath.end(), '\\', '/');

                                      if (!first)
                                          arr += ",";
                                      arr += JsonStr(relPath);
                                      first = false;
                                  }
                                  arr += "]";
                                  return JsonObj({{"files", arr}});
                              }});

        m_agent.RegisterTool({.name = "read_shader",
                              .description = "Reads the source code of an HLSL shader file. Provide the path relative to Assets/Shaders/ (e.g. 'Bloom/BrightFilterPS.hlsl') or an absolute path.",
                              .properties = {
                                  {"path", "Shader file path (relative to Assets/Shaders/ or absolute)", pagent::SchemaType::String, true},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";

                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative())
                                      fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                                  if (!std::filesystem::exists(fpath))
                                      return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                                  std::ifstream file(fpath, std::ios::in);
                                  if (!file.is_open())
                                      return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

                                  std::string source((std::istreambuf_iterator<char>(file)),
                                                     std::istreambuf_iterator<char>());

                                  std::string resolvedPath = fpath.string();
                                  std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');
                                  return JsonObj({{"path", JsonStr(resolvedPath)}, {"source", JsonStr(source)}});
                              }});

        m_agent.RegisterTool({.name = "edit_shader",
                              .description = "Edits an existing HLSL shader by replacing a specific code section. "
                                             "Use read_shader first to see the current source, then provide the exact text to find and its replacement. "
                                             "Recompilation is triggered automatically. Prefer this over write_shader for modifying existing shaders.",
                              .properties = {
                                  {"path", "Shader file path (relative to Assets/Shaders/ or absolute)", pagent::SchemaType::String, true},
                                  {"find", "Exact text to find in the shader (must match exactly, including whitespace)", pagent::SchemaType::String, true},
                                  {"replace", "Replacement text", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  std::string find = JsonUnescape(ExtractArgStr(args, "find"));
                                  std::string replace = JsonUnescape(ExtractArgStr(args, "replace"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";
                                  if (find.empty())
                                      return "{\"error\":\"missing find\"}";

                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative())
                                      fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                                  if (!std::filesystem::exists(fpath))
                                      return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                                  // Read current source
                                  std::string source;
                                  {
                                      std::ifstream file(fpath, std::ios::in);
                                      if (!file.is_open())
                                          return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});
                                      source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                                  }

                                  // Find and replace
                                  auto pos = source.find(find);
                                  if (pos == std::string::npos)
                                      return JsonObj({{"error", JsonStr("find text not found in shader")}});

                                  source.replace(pos, find.size(), replace);

                                  // Write back
                                  {
                                      std::ofstream file(fpath, std::ios::out | std::ios::trunc);
                                      if (!file.is_open())
                                          return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
                                      file << source;
                                  }

                                  std::string resolvedPath = fpath.string();
                                  std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');

                                  QueueAction([resolvedPath]()
                                              { EventSystem::PushEvent(EventType::CompileShaders); });

                                  return JsonObj({{"status", JsonStr("edited")}, {"path", JsonStr(resolvedPath)}});
                              }});

        m_agent.RegisterTool({.name = "write_shader",
                              .description = "Creates a NEW shader file with HLSL source code. For editing existing shaders, use edit_shader instead. "
                                             "New files are automatically registered for hot-reload. "
                                             "Shader recompilation is triggered automatically after writing. "
                                             "Provide the path relative to Assets/Shaders/ (e.g. 'PostProcess/MyEffect.hlsl') or an absolute path.",
                              .properties = {
                                  {"path", "Shader file path (relative to Assets/Shaders/ or absolute)", pagent::SchemaType::String, true},
                                  {"source", "HLSL source code to write", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  std::string source = JsonUnescape(ExtractArgStr(args, "source"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";
                                  if (source.empty())
                                      return "{\"error\":\"missing source\"}";

                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative())
                                      fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                                  // Create parent directories if needed
                                  std::filesystem::path parentDir = fpath.parent_path();
                                  if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                                  {
                                      std::error_code ec;
                                      std::filesystem::create_directories(parentDir, ec);
                                      if (ec)
                                          return JsonObj({{"error", JsonStr("cannot create directory: " + ec.message())}});
                                  }

                                  // Refuse to overwrite existing files -- use edit_shader instead
                                  if (std::filesystem::exists(fpath))
                                      return JsonObj({{"error", JsonStr("file already exists, use edit_shader to modify it")}});

                                  // Write the shader source
                                  std::ofstream file(fpath, std::ios::out | std::ios::trunc);
                                  if (!file.is_open())
                                      return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
                                  file << source;
                                  file.close();

                                  std::string resolvedPath = fpath.string();
                                  std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');

                                  // Register with FileWatcher and trigger recompilation
                                  QueueAction([resolvedPath]()
                                              {
                                      FileWatcher::Add(resolvedPath, [](size_t fileEvent)
                                      {
                                          EventSystem::PushEvent(fileEvent);
                                          EventSystem::PushEvent(EventType::CompileShaders);
                                      });
                                      EventSystem::PushEvent(EventType::CompileShaders); });

                                  return JsonObj({{"status", JsonStr("created")}, {"path", JsonStr(resolvedPath)}});
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
                                  return "{\"status\":\"ok\"}";
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
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "load_model",
                              .description = "Loads a 3D model file and adds it to the scene. Returns the assigned label — use this label in set_model_transform and other tools to target this specific model. "
                                             "Always provide a unique label when loading multiple models of the same type (e.g. 'front_wall', 'back_wall').",
                              .properties = {
                                  {"path", "Model name from list_models (e.g. 'Sponza/Sponza.gltf') or absolute path", pagent::SchemaType::String, true},
                                  {"label", "Unique label for this model instance (e.g. 'front_wall'). Required when loading duplicates.", pagent::SchemaType::String, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";
                                  std::string label = JsonUnescape(ExtractArgStr(args, "label"));

                                  // If not absolute, resolve relative to Assets/Objects/
                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative())
                                      fpath = std::filesystem::path(Path::Assets + "Objects") / fpath;

                                  std::string resolved = fpath.string();
                                  if (!std::filesystem::exists(resolved))
                                      return JsonObj({{"error", JsonStr("file not found: " + resolved)}});

                                  std::string stem = fpath.stem().string();
                                  QueueAction([resolved, label, stem]()
                                              {
                                      Model *m = Model::Load(resolved);
                                      if (!m) return;
                                      std::string base = label.empty() ? stem : label;
                                      std::string effectiveLabel = base + "_" + std::to_string(m->GetId());
                                      m->SetLabel(effectiveLabel);
                                      auto &nodes = m->GetNodeInfos();
                                      if (!nodes.empty())
                                          nodes[0].name = effectiveLabel;
                                  });
                                  std::string returnLabel = label.empty() ? stem : label;
                                  return JsonObj({{"status", JsonStr("ok")}, {"label", JsonStr(returnLabel)}});
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
                                      results += JsonStr(fullPath);
                                      first = false;
                                      if (++count >= 20)
                                          break; // cap results
                                  }
                                  results += "]";
                                  return JsonObj({{"results", results}});
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
                                  return JsonObj({{"path", JsonStr(base)}, {"files", files}, {"dirs", dirs}});
                              }});

        // ------------------------------------------------------------------ scene objects
        m_agent.RegisterTool({.name = "create_primitive",
                              .description = "Creates a primitive mesh with full transform and adds it to the scene. "
                                             "type: cube|sphere|plane|cylinder|cone. "
                                             "Provide a unique label when creating multiple primitives of the same type. "
                                             "Use sx/sy/sz for per-axis scale (e.g. thin walls), or scale for uniform. "
                                             "Returns the label to use in other tools.",
                              .properties = {
                                  {"type", "Primitive type: cube, sphere, plane, cylinder, cone", pagent::SchemaType::String, true},
                                  {"label", "Unique label (e.g. 'front_wall', 'ball_1')", pagent::SchemaType::String, false},
                                  {"size", "Base size parameter (default 1.0)", pagent::SchemaType::Number, false},
                                  {"height", "Height for cylinder or cone (default 2.0)", pagent::SchemaType::Number, false},
                                  {"x", "Position X", pagent::SchemaType::Number, false},
                                  {"y", "Position Y", pagent::SchemaType::Number, false},
                                  {"z", "Position Z", pagent::SchemaType::Number, false},
                                  {"rx", "Rotation X degrees", pagent::SchemaType::Number, false},
                                  {"ry", "Rotation Y degrees", pagent::SchemaType::Number, false},
                                  {"rz", "Rotation Z degrees", pagent::SchemaType::Number, false},
                                  {"scale", "Uniform scale (default 1.0, ignored if sx/sy/sz set)", pagent::SchemaType::Number, false},
                                  {"sx", "Scale X (per-axis)", pagent::SchemaType::Number, false},
                                  {"sy", "Scale Y (per-axis)", pagent::SchemaType::Number, false},
                                  {"sz", "Scale Z (per-axis)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string type = ExtractArgStr(args, "type");
                                  std::string label = JsonUnescape(ExtractArgStr(args, "label"));
                                  float size = ExtractArgNum(args, "size");
                                  float height = ExtractArgNum(args, "height");
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");
                                  float rx = ExtractArgNum(args, "rx");
                                  float ry = ExtractArgNum(args, "ry");
                                  float rz = ExtractArgNum(args, "rz");
                                  float sxv = ExtractArgNum(args, "sx");
                                  float syv = ExtractArgNum(args, "sy");
                                  float szv = ExtractArgNum(args, "sz");
                                  float s = ExtractArgNum(args, "scale");
                                  if (size <= 0.0f)
                                      size = 1.0f;
                                  if (height <= 0.0f)
                                      height = 2.0f;
                                  if (type.empty())
                                      return "{\"error\":\"missing type\"}";

                                  bool perAxis = (sxv != 0.0f || syv != 0.0f || szv != 0.0f);
                                  if (perAxis)
                                  {
                                      if (sxv <= 0.0f) sxv = 1.0f;
                                      if (syv <= 0.0f) syv = 1.0f;
                                      if (szv <= 0.0f) szv = 1.0f;
                                  }
                                  else
                                  {
                                      if (s <= 0.0f) s = 1.0f;
                                      sxv = syv = szv = s;
                                  }

                                  std::string userLabel = label;

                                  QueueAction([type, userLabel, size, height, x, y, z, rx, ry, rz, sxv, syv, szv]()
                                              {
                                      Model *m = nullptr;
                                      if      (type == "cube")     m = Primitives::CreateCube(size);
                                      else if (type == "sphere")   m = Primitives::CreateSphere(size);
                                      else if (type == "plane")    m = Primitives::CreatePlane(size, size);
                                      else if (type == "cylinder") m = Primitives::CreateCylinder(size, height);
                                      else if (type == "cone")     m = Primitives::CreateCone(size, height);
                                      if (!m) return;
                                      // Always append model ID to guarantee uniqueness
                                      std::string base = userLabel.empty() ? type : userLabel;
                                      std::string effectiveLabel = base + "_" + std::to_string(m->GetId());
                                      m->SetLabel(effectiveLabel);
                                      // Set node name so hierarchy shows the label
                                      auto &nodes = m->GetNodeInfos();
                                      if (!nodes.empty())
                                          nodes[0].name = effectiveLabel;
                                      // Apply full transform to root node so editor widgets reflect it
                                      mat4 T = glm::translate(mat4(1.0f), vec3(x, y, z));
                                      mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rx), glm::radians(ry), glm::radians(rz))));
                                      mat4 S = glm::scale(mat4(1.0f), vec3(sxv, syv, szv));
                                      if (!nodes.empty())
                                          nodes[0].localMatrix = T * R * S;
                                      m->MarkDirty(0);
                                      EventSystem::PushEvent(EventType::ModelLoaded, m); });
                                  // Return the label the agent should use to reference this model
                                  std::string returnLabel = userLabel.empty()
                                      ? type // agent must provide label for reliable targeting
                                      : userLabel;
                                  return JsonObj({{"status", JsonStr("ok")}, {"label", JsonStr(returnLabel)}});
                              }});

        m_agent.RegisterTool({.name = "remove_model",
                              .description = "Removes a loaded model from the scene by its filename (case-insensitive substring match).",
                              .properties = {
                                  {"name", "Filename substring to match (e.g. 'sponza', 'cube')", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  QueueAction([queryLower]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      auto &models = r->GetScene().GetModels();
                                      Model *target = nullptr;
                                      for (auto *m : models)
                                      {
                                          std::string fname = m->GetLabel().empty()
                                              ? m->GetFilePath().filename().string()
                                              : m->GetLabel();
                                          for (auto &c : fname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (fname.find(queryLower) != std::string::npos) { target = m; break; }
                                      }
                                      if (target) EventSystem::PushEvent(EventType::ModelRemoved, target); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_model_transform",
                              .description = "Sets position, rotation (Euler degrees) and scale for a model. "
                                             "Find the model by a filename/label substring. "
                                             "All transform fields are optional — omitted ones default to 0 (scale defaults to 1). "
                                             "Use scale for uniform scaling, or sx/sy/sz for per-axis scaling.",
                              .properties = {
                                  {"name", "Filename/label substring to match (e.g. 'sponza')", pagent::SchemaType::String, true},
                                  {"x", "Position X", pagent::SchemaType::Number, false},
                                  {"y", "Position Y", pagent::SchemaType::Number, false},
                                  {"z", "Position Z", pagent::SchemaType::Number, false},
                                  {"rx", "Rotation X in degrees (pitch)", pagent::SchemaType::Number, false},
                                  {"ry", "Rotation Y in degrees (yaw)", pagent::SchemaType::Number, false},
                                  {"rz", "Rotation Z in degrees (roll)", pagent::SchemaType::Number, false},
                                  {"scale", "Uniform scale (default 1.0, ignored if sx/sy/sz set)", pagent::SchemaType::Number, false},
                                  {"sx", "Scale X (per-axis)", pagent::SchemaType::Number, false},
                                  {"sy", "Scale Y (per-axis)", pagent::SchemaType::Number, false},
                                  {"sz", "Scale Z (per-axis)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");
                                  float rx = ExtractArgNum(args, "rx");
                                  float ry = ExtractArgNum(args, "ry");
                                  float rz = ExtractArgNum(args, "rz");
                                  float sx = ExtractArgNum(args, "sx");
                                  float sy = ExtractArgNum(args, "sy");
                                  float sz = ExtractArgNum(args, "sz");
                                  float s = ExtractArgNum(args, "scale");
                                  // Per-axis scale takes priority; fall back to uniform scale
                                  bool perAxis = (sx != 0.0f || sy != 0.0f || sz != 0.0f);
                                  if (perAxis)
                                  {
                                      if (sx <= 0.0f) sx = 1.0f;
                                      if (sy <= 0.0f) sy = 1.0f;
                                      if (sz <= 0.0f) sz = 1.0f;
                                  }
                                  else
                                  {
                                      if (s <= 0.0f) s = 1.0f;
                                      sx = sy = sz = s;
                                  }
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  QueueAction([queryLower, x, y, z, rx, ry, rz, sx, sy, sz]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      for (auto *m : r->GetScene().GetModels())
                                      {
                                          std::string fname = m->GetLabel().empty()
                                              ? m->GetFilePath().filename().string()
                                              : m->GetLabel();
                                          for (auto &c : fname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (fname.find(queryLower) == std::string::npos) continue;
                                          mat4 T = glm::translate(mat4(1.0f), vec3(x, y, z));
                                          mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rx), glm::radians(ry), glm::radians(rz))));
                                          mat4 S = glm::scale(mat4(1.0f), vec3(sx, sy, sz));
                                          // Set root node's localMatrix so the editor transform widgets reflect the change
                                          auto &nodes = m->GetNodeInfos();
                                          if (!nodes.empty())
                                          {
                                              nodes[0].localMatrix = T * R * S;
                                              m->MarkDirty(0);
                                          }
                                          break;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "get_model_info",
                              .description = "Returns detailed info for a loaded model: node count, mesh count, bounding box, position.",
                              .properties = {
                                  {"name", "Filename/label substring to match", pagent::SchemaType::String, true},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";
                                  for (auto *m : r->GetScene().GetModels())
                                  {
                                      std::string fname = m->GetLabel().empty()
                                                              ? m->GetFilePath().filename().string()
                                                              : m->GetLabel();
                                      for (auto &c : fname)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (fname.find(queryLower) == std::string::npos)
                                          continue;
                                      vec3 pos(m->GetMatrix()[3]);
                                      // compute world bounding box from root nodes
                                      AABB worldBB;
                                      bool first = true;
                                      for (auto &n : m->GetNodeInfos())
                                      {
                                          if (first)
                                          {
                                              worldBB = n.worldBoundingBox;
                                              first = false;
                                          }
                                          else
                                          {
                                              worldBB.min = glm::min(worldBB.min, n.worldBoundingBox.min);
                                              worldBB.max = glm::max(worldBB.max, n.worldBoundingBox.max);
                                          }
                                      }
                                      std::string name = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                      return JsonObj({
                                          {"name", JsonStr(name)},
                                          {"nodes", std::to_string(m->GetNodeCount())},
                                          {"meshes", std::to_string(m->GetMeshCount())},
                                          {"pos", "[" + std::to_string(pos.x) + "," + std::to_string(pos.y) + "," + std::to_string(pos.z) + "]"},
                                          {"bb_min", "[" + std::to_string(worldBB.min.x) + "," + std::to_string(worldBB.min.y) + "," + std::to_string(worldBB.min.z) + "]"},
                                          {"bb_max", "[" + std::to_string(worldBB.max.x) + "," + std::to_string(worldBB.max.y) + "," + std::to_string(worldBB.max.z) + "]"},
                                      });
                                  }
                                  return JsonObj({{"error", JsonStr("model not found: " + query)}});
                              }});

        m_agent.RegisterTool({.name = "clone_model",
                              .description = "Duplicates an already loaded model, optionally placing it at an absolute position.",
                              .properties = {
                                  {"name", "Filename/label substring of the model to clone", pagent::SchemaType::String, true},
                                  {"x", "Absolute Position X (default 0)", pagent::SchemaType::Number, false},
                                  {"y", "Absolute Position Y (default 0)", pagent::SchemaType::Number, false},
                                  {"z", "Absolute Position Z (default 0)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");

                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";

                                  std::string pathToLoad;
                                  for (auto *m : r->GetScene().GetModels())
                                  {
                                      std::string fname = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                      for (auto &c : fname)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (fname.find(queryLower) != std::string::npos)
                                      {
                                          pathToLoad = m->GetFilePath().string();
                                          break;
                                      }
                                  }

                                  if (pathToLoad.empty())
                                      return JsonObj({{"error", JsonStr("model not found: " + query)}});

                                  QueueAction([pathToLoad, x, y, z]()
                                              {
                                      Model *m = Model::Load(pathToLoad);
                                      if (m)
                                      {
                                          mat4 T = glm::translate(mat4(1.0f), vec3(x, y, z));
                                          m->GetMatrix() = T;
                                          m->GetDirtyNodes() = true;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "get_materials",
                              .description = "Returns material properties (base color, emissive, metallic, roughness, alpha_cutoff) for all meshes of a loaded model.",
                              .properties = {
                                  {"name", "Filename/label substring to match", pagent::SchemaType::String, true},
                              },
                              .handler = [](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";
                                  for (auto *m : r->GetScene().GetModels())
                                  {
                                      std::string fname = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                      for (auto &c : fname)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (fname.find(queryLower) == std::string::npos)
                                          continue;

                                      std::string mats = "[";
                                      bool first = true;
                                      int idx = 0;
                                      for (auto &mesh : m->GetMeshInfos())
                                      {
                                          if (!first)
                                              mats += ",";
                                          vec4 bc = mesh.materialFactors[0][0];
                                          float met = mesh.materialFactors[0][2].x;
                                          float rou = mesh.materialFactors[0][2].y;
                                          float alpha = mesh.materialFactors[0][2].z;
                                          vec4 em = mesh.materialFactors[0][1];

                                          mats += JsonObj({{"mesh_index", std::to_string(idx++)},
                                                           {"base_color", "[" + std::to_string(bc.r) + "," + std::to_string(bc.g) + "," + std::to_string(bc.b) + "," + std::to_string(bc.a) + "]"},
                                                           {"emissive", "[" + std::to_string(em.r) + "," + std::to_string(em.g) + "," + std::to_string(em.b) + "]"},
                                                           {"metallic", std::to_string(met)},
                                                           {"roughness", std::to_string(rou)},
                                                           {"alpha_cutoff", std::to_string(alpha)}});
                                          first = false;
                                      }
                                      mats += "]";
                                      return JsonObj({{"model", JsonStr(m->GetLabel())}, {"materials", mats}});
                                  }
                                  return JsonObj({{"error", JsonStr("model not found: " + query)}});
                              }});

        m_agent.RegisterTool({.name = "set_material_property",
                              .description = "Sets material properties for a specific mesh of a model.",
                              .properties = {
                                  {"name", "Filename/label substring of the model", pagent::SchemaType::String, true},
                                  {"mesh_index", "0-based mesh index (from get_materials). Use -1 for all meshes.", pagent::SchemaType::Number, true},
                                  {"base_color_r", "Red 0-1", pagent::SchemaType::Number, false},
                                  {"base_color_g", "Green 0-1", pagent::SchemaType::Number, false},
                                  {"base_color_b", "Blue 0-1", pagent::SchemaType::Number, false},
                                  {"base_color_a", "Alpha 0-1", pagent::SchemaType::Number, false},
                                  {"emissive_r", "Emissive Red", pagent::SchemaType::Number, false},
                                  {"emissive_g", "Emissive Green", pagent::SchemaType::Number, false},
                                  {"emissive_b", "Emissive Blue", pagent::SchemaType::Number, false},
                                  {"metallic", "Metallic 0-1", pagent::SchemaType::Number, false},
                                  {"roughness", "Roughness 0-1", pagent::SchemaType::Number, false},
                                  {"alpha_cutoff", "Alpha Cutoff 0-1", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  int meshIndex = static_cast<int>(ExtractArgNum(args, "mesh_index"));

                                  bool has_bcr = args.find("\"base_color_r\":") != std::string::npos;
                                  bool has_bcg = args.find("\"base_color_g\":") != std::string::npos;
                                  bool has_bcb = args.find("\"base_color_b\":") != std::string::npos;
                                  bool has_bca = args.find("\"base_color_a\":") != std::string::npos;
                                  bool has_er = args.find("\"emissive_r\":") != std::string::npos;
                                  bool has_eg = args.find("\"emissive_g\":") != std::string::npos;
                                  bool has_eb = args.find("\"emissive_b\":") != std::string::npos;
                                  bool has_met = args.find("\"metallic\":") != std::string::npos;
                                  bool has_rou = args.find("\"roughness\":") != std::string::npos;
                                  bool has_alpha = args.find("\"alpha_cutoff\":") != std::string::npos;

                                  float bcr = ExtractArgNum(args, "base_color_r"), bcg = ExtractArgNum(args, "base_color_g"), bcb = ExtractArgNum(args, "base_color_b"), bca = ExtractArgNum(args, "base_color_a");
                                  float er = ExtractArgNum(args, "emissive_r"), eg = ExtractArgNum(args, "emissive_g"), eb = ExtractArgNum(args, "emissive_b");
                                  float met = ExtractArgNum(args, "metallic"), rou = ExtractArgNum(args, "roughness"), alpha = ExtractArgNum(args, "alpha_cutoff");

                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  QueueAction([=]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      for (auto *m : r->GetScene().GetModels())
                                      {
                                          std::string fname = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                          for (auto &c : fname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (fname.find(queryLower) == std::string::npos) continue;

                                          auto applyProps = [&](MeshInfo &mesh) {
                                              if (has_bcr) mesh.materialFactors[0][0].r = bcr;
                                              if (has_bcg) mesh.materialFactors[0][0].g = bcg;
                                              if (has_bcb) mesh.materialFactors[0][0].b = bcb;
                                              if (has_bca) mesh.materialFactors[0][0].a = bca;
                                              if (has_er) mesh.materialFactors[0][1].r = er;
                                              if (has_eg) mesh.materialFactors[0][1].g = eg;
                                              if (has_eb) mesh.materialFactors[0][1].b = eb;
                                              if (has_met) mesh.materialFactors[0][2].x = met;
                                              if (has_rou) mesh.materialFactors[0][2].y = rou;
                                              if (has_alpha) mesh.materialFactors[0][2].z = alpha;
                                          };

                                          if (meshIndex >= 0 && meshIndex < static_cast<int>(m->GetMeshInfos().size()))
                                          {
                                              applyProps(m->GetMeshInfos()[meshIndex]);
                                          }
                                          else if (meshIndex == -1)
                                          {
                                              for (auto &mesh : m->GetMeshInfos())
                                                  applyProps(mesh);
                                          }

                                          for (int i = 0; i < m->GetNodeCount(); ++i)
                                              m->MarkDirty(i);
                                          
                                          r->GetScene().UpdateTextures();
                                          break;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "get_lights",
                              .description = "Returns all lights in the scene: directional, point, spot, area.",
                              .properties = {},
                              .handler = [](const std::string &) -> std::string
                              {
                                  auto *ls = GetGlobalSystem<LightSystem>();
                                  if (!ls)
                                      return "{\"error\":\"no light system\"}";
                                  auto buildArr = [](auto &vec, auto serializeFn) -> std::string
                                  {
                                      std::string arr = "[";
                                      bool first = true;
                                      int idx = 0;
                                      for (auto &l : vec)
                                      {
                                          if (!first)
                                              arr += ",";
                                          arr += serializeFn(l, idx++);
                                          first = false;
                                      }
                                      return arr + "]";
                                  };
                                  std::string dir = buildArr(ls->GetDirectionalLights(), [](const DirectionalLightEditor &l, int i)
                                                             { return JsonObj({{"index", std::to_string(i)}, {"name", JsonStr(l.name)}, {"r", std::to_string(l.color.r)}, {"g", std::to_string(l.color.g)}, {"b", std::to_string(l.color.b)}, {"intensity", std::to_string(l.color.w)}, {"pos_x", std::to_string(l.position.x)}, {"pos_y", std::to_string(l.position.y)}, {"pos_z", std::to_string(l.position.z)}}); });
                                  std::string pt = buildArr(ls->GetPointLights(), [](const PointLightEditor &l, int i)
                                                            { return JsonObj({{"index", std::to_string(i)}, {"name", JsonStr(l.name)}, {"r", std::to_string(l.color.r)}, {"g", std::to_string(l.color.g)}, {"b", std::to_string(l.color.b)}, {"intensity", std::to_string(l.color.w)}, {"pos_x", std::to_string(l.position.x)}, {"pos_y", std::to_string(l.position.y)}, {"pos_z", std::to_string(l.position.z)}, {"radius", std::to_string(l.position.w)}}); });
                                  std::string spot = buildArr(ls->GetSpotLights(), [](const SpotLightEditor &l, int i)
                                                              { return JsonObj({{"index", std::to_string(i)}, {"name", JsonStr(l.name)}, {"r", std::to_string(l.color.r)}, {"g", std::to_string(l.color.g)}, {"b", std::to_string(l.color.b)}, {"intensity", std::to_string(l.color.w)}, {"pos_x", std::to_string(l.position.x)}, {"pos_y", std::to_string(l.position.y)}, {"pos_z", std::to_string(l.position.z)}, {"range", std::to_string(l.position.w)}, {"angle_deg", std::to_string(glm::degrees(l.params.x))}, {"falloff", std::to_string(l.params.y)}}); });
                                  std::string area = buildArr(ls->GetAreaLights(), [](const AreaLightEditor &l, int i)
                                                              { return JsonObj({{"index", std::to_string(i)}, {"name", JsonStr(l.name)}, {"r", std::to_string(l.color.r)}, {"g", std::to_string(l.color.g)}, {"b", std::to_string(l.color.b)}, {"intensity", std::to_string(l.color.w)}, {"pos_x", std::to_string(l.position.x)}, {"pos_y", std::to_string(l.position.y)}, {"pos_z", std::to_string(l.position.z)}, {"width", std::to_string(l.size.x)}, {"height", std::to_string(l.size.y)}}); });
                                  return "{\"directional\":" + dir + ",\"point\":" + pt + ",\"spot\":" + spot + ",\"area\":" + area + "}";
                              }});

        m_agent.RegisterTool({.name = "add_light",
                              .description = "Adds a light to the scene. "
                                             "type: directional|point|spot|area. "
                                             "r,g,b: color (0-1, default 1). intensity: default 1. "
                                             "x,y,z: position. "
                                             "radius: influence radius for point lights (default 10). "
                                             "range: range for spot/area lights (default 10). "
                                             "angle_deg: spot cone half-angle in degrees (default 30). "
                                             "width,height: area light dimensions (default 1).",
                              .properties = {
                                  {"type", "Light type: directional, point, spot, area", pagent::SchemaType::String, true},
                                  {"r", "Red channel 0-1 (default 1)", pagent::SchemaType::Number, false},
                                  {"g", "Green channel 0-1 (default 1)", pagent::SchemaType::Number, false},
                                  {"b", "Blue channel 0-1 (default 1)", pagent::SchemaType::Number, false},
                                  {"intensity", "Light intensity (default 1)", pagent::SchemaType::Number, false},
                                  {"x", "Position X (default 0)", pagent::SchemaType::Number, false},
                                  {"y", "Position Y (default 0)", pagent::SchemaType::Number, false},
                                  {"z", "Position Z (default 0)", pagent::SchemaType::Number, false},
                                  {"radius", "Point light influence radius (default 10)", pagent::SchemaType::Number, false},
                                  {"range", "Spot/area light range (default 10)", pagent::SchemaType::Number, false},
                                  {"angle_deg", "Spot cone half-angle in degrees (default 30)", pagent::SchemaType::Number, false},
                                  {"width", "Area light width (default 1)", pagent::SchemaType::Number, false},
                                  {"height", "Area light height (default 1)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string type = ExtractArgStr(args, "type");
                                  if (type.empty())
                                      return "{\"error\":\"missing type\"}";
                                  float r = ExtractArgNum(args, "r"), g = ExtractArgNum(args, "g"), b = ExtractArgNum(args, "b");
                                  float intensity = ExtractArgNum(args, "intensity");
                                  float x = ExtractArgNum(args, "x"), y = ExtractArgNum(args, "y"), z = ExtractArgNum(args, "z");
                                  float radius = ExtractArgNum(args, "radius");
                                  float range = ExtractArgNum(args, "range");
                                  float angle = ExtractArgNum(args, "angle_deg");
                                  float w = ExtractArgNum(args, "width"), h = ExtractArgNum(args, "height");
                                  // sensible defaults
                                  if (r <= 0.0f && g <= 0.0f && b <= 0.0f)
                                  {
                                      r = g = b = 1.0f;
                                  }
                                  if (intensity <= 0.0f)
                                      intensity = 1.0f;
                                  if (radius <= 0.0f)
                                      radius = 10.0f;
                                  if (range <= 0.0f)
                                      range = 10.0f;
                                  if (angle <= 0.0f)
                                      angle = 30.0f;
                                  if (w <= 0.0f)
                                      w = 1.0f;
                                  if (h <= 0.0f)
                                      h = 1.0f;

                                  QueueAction([type, r, g, b, intensity, x, y, z, radius, range, angle, w, h]()
                                              {
                                      auto *ls = GetGlobalSystem<LightSystem>();
                                      if (!ls) return;
                                      if (type == "directional")
                                      {
                                          ls->CreateDirectionalLight();
                                          auto &l = ls->GetDirectionalLights().back();
                                          l.color = vec4(r, g, b, intensity);
                                          l.position = vec4(x, y, z, 0.0f);
                                      }
                                      else if (type == "point")
                                      {
                                          ls->CreatePointLight();
                                          auto &l = ls->GetPointLights().back();
                                          l.color = vec4(r, g, b, intensity);
                                          l.position = vec4(x, y, z, radius);
                                      }
                                      else if (type == "spot")
                                      {
                                          ls->CreateSpotLight();
                                          auto &l = ls->GetSpotLights().back();
                                          l.color = vec4(r, g, b, intensity);
                                          l.position = vec4(x, y, z, range);
                                          l.params = vec4(glm::radians(angle), 1.0f, 0.0f, 0.0f);
                                      }
                                      else if (type == "area")
                                      {
                                          ls->CreateAreaLight();
                                          auto &l = ls->GetAreaLights().back();
                                          l.color = vec4(r, g, b, intensity);
                                          l.position = vec4(x, y, z, range);
                                          l.size = vec4(w, h, 0.0f, 0.0f);
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_light",
                              .description = "Modifies an existing light. type: directional|point|spot|area. index: 0-based index from get_lights. "
                                             "Settable fields: r,g,b (color 0-1), intensity, x,y,z (position), radius (point), range (spot/area), angle_deg (spot).",
                              .properties = {
                                  {"type", "Light type: directional, point, spot, area", pagent::SchemaType::String, true},
                                  {"index", "0-based light index", pagent::SchemaType::Number, true},
                                  {"r", "Red 0-1", pagent::SchemaType::Number, false},
                                  {"g", "Green 0-1", pagent::SchemaType::Number, false},
                                  {"b", "Blue 0-1", pagent::SchemaType::Number, false},
                                  {"intensity", "Light intensity", pagent::SchemaType::Number, false},
                                  {"x", "Position X", pagent::SchemaType::Number, false},
                                  {"y", "Position Y", pagent::SchemaType::Number, false},
                                  {"z", "Position Z", pagent::SchemaType::Number, false},
                                  {"radius", "Point light radius", pagent::SchemaType::Number, false},
                                  {"range", "Spot/area range", pagent::SchemaType::Number, false},
                                  {"angle_deg", "Spot cone half-angle degrees", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string type = ExtractArgStr(args, "type");
                                  int index = static_cast<int>(ExtractArgNum(args, "index"));

                                  // Pack all float args; -999 sentinel means "not provided"
                                  float r = ExtractArgNum(args, "r");
                                  float g = ExtractArgNum(args, "g");
                                  float bv = ExtractArgNum(args, "b");
                                  float intensity = ExtractArgNum(args, "intensity");
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");
                                  float radius = ExtractArgNum(args, "radius");
                                  float range = ExtractArgNum(args, "range");
                                  float angle = ExtractArgNum(args, "angle_deg");

                                  QueueAction([type, index, r, g, bv, intensity, x, y, z, radius, range, angle]()
                                              {
                                      auto *ls = GetGlobalSystem<LightSystem>();
                                      if (!ls) return;
                                      auto applyColor = [&](vec4 &color) {
                                          if (r > 0.0f || g > 0.0f || bv > 0.0f) { color.r = r; color.g = g; color.b = bv; }
                                          if (intensity > 0.0f) color.w = intensity;
                                      };
                                      auto applyPos = [&](vec4 &pos) {
                                          pos.x = x; pos.y = y; pos.z = z;
                                      };
                                      if (type == "directional" && index < static_cast<int>(ls->GetDirectionalLights().size()))
                                      {
                                          auto &l = ls->GetDirectionalLights()[index];
                                          applyColor(l.color); applyPos(l.position);
                                      }
                                      else if (type == "point" && index < static_cast<int>(ls->GetPointLights().size()))
                                      {
                                          auto &l = ls->GetPointLights()[index];
                                          applyColor(l.color); applyPos(l.position);
                                          if (radius > 0.0f) l.position.w = radius;
                                      }
                                      else if (type == "spot" && index < static_cast<int>(ls->GetSpotLights().size()))
                                      {
                                          auto &l = ls->GetSpotLights()[index];
                                          applyColor(l.color); applyPos(l.position);
                                          if (range > 0.0f) l.position.w = range;
                                          if (angle > 0.0f) l.params.x = glm::radians(angle);
                                      }
                                      else if (type == "area" && index < static_cast<int>(ls->GetAreaLights().size()))
                                      {
                                          auto &l = ls->GetAreaLights()[index];
                                          applyColor(l.color); applyPos(l.position);
                                          if (range > 0.0f) l.position.w = range;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "remove_light",
                              .description = "Removes a light by type and 0-based index. type: directional|point|spot|area.",
                              .properties = {
                                  {"type", "Light type: directional, point, spot, area", pagent::SchemaType::String, true},
                                  {"index", "0-based light index", pagent::SchemaType::Number, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string type = ExtractArgStr(args, "type");
                                  int index = static_cast<int>(ExtractArgNum(args, "index"));
                                  QueueAction([type, index]()
                                              {
                                      auto *ls = GetGlobalSystem<LightSystem>();
                                      if (!ls) return;
                                      auto removeAt = [&](auto &vec) {
                                          if (index >= 0 && index < static_cast<int>(vec.size()))
                                              vec.erase(vec.begin() + index);
                                      };
                                      if      (type == "directional") removeAt(ls->GetDirectionalLights());
                                      else if (type == "point")       removeAt(ls->GetPointLights());
                                      else if (type == "spot")        removeAt(ls->GetSpotLights());
                                      else if (type == "area")        removeAt(ls->GetAreaLights()); });
                                  return "{\"status\":\"ok\"}";
                              }});

        // ------------------------------------------------------------------ camera
        m_agent.RegisterTool({.name = "set_camera_rotation",
                              .description = "Sets the active camera's orientation. pitch and yaw are in degrees.",
                              .properties = {
                                  {"pitch", "Pitch in degrees (rotation around X)", pagent::SchemaType::Number, true},
                                  {"yaw", "Yaw in degrees (rotation around Y)", pagent::SchemaType::Number, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  float pitch = ExtractArgNum(args, "pitch");
                                  float yaw = ExtractArgNum(args, "yaw");
                                  QueueAction([pitch, yaw]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      auto *cam = r->GetScene().GetActiveCamera();
                                      if (cam) cam->SetEuler(vec3(glm::radians(pitch), glm::radians(yaw), 0.0f)); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_camera_fov",
                              .description = "Sets the active camera's horizontal field of view in degrees.",
                              .properties = {
                                  {"fov", "Horizontal FOV in degrees (e.g. 90)", pagent::SchemaType::Number, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  float fov = ExtractArgNum(args, "fov");
                                  if (fov <= 0.0f)
                                      return "{\"error\":\"invalid fov\"}";
                                  QueueAction([fov]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      auto *cam = r->GetScene().GetActiveCamera();
                                      if (cam) cam->SetFovx(glm::radians(fov)); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "focus_camera_on_model",
                              .description = "Moves and aims the active camera to frame a model. "
                                             "Finds the model by filename/label substring and positions the camera in front of its bounding box center.",
                              .properties = {
                                  {"name", "Filename/label substring of the target model", pagent::SchemaType::String, true},
                                  {"distance_multiplier", "Camera distance multiplier relative to model extents (default 2.0)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  float mult = ExtractArgNum(args, "distance_multiplier");
                                  if (mult <= 0.0f)
                                      mult = 2.0f;
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  QueueAction([queryLower, mult]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      for (auto *m : r->GetScene().GetModels())
                                      {
                                          std::string fname = m->GetLabel().empty()
                                              ? m->GetFilePath().filename().string()
                                              : m->GetLabel();
                                          for (auto &c : fname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (fname.find(queryLower) == std::string::npos) continue;
                                          AABB worldBB;
                                          bool first = true;
                                          for (auto &n : m->GetNodeInfos())
                                          {
                                              if (first) { worldBB = n.worldBoundingBox; first = false; }
                                              else
                                              {
                                                  worldBB.min = glm::min(worldBB.min, n.worldBoundingBox.min);
                                                  worldBB.max = glm::max(worldBB.max, n.worldBoundingBox.max);
                                              }
                                          }
                                          vec3 center = worldBB.GetCenter();
                                          float radius = glm::length(worldBB.GetSize()) * 0.5f;
                                          auto *cam = r->GetScene().GetActiveCamera();
                                          if (!cam) break;
                                          vec3 dir = glm::normalize(cam->GetFront());
                                          cam->SetPosition(center - dir * (radius * mult));
                                          break;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_camera_dof_focus",
                              .description = "Automatically sets the Depth of Field focus distance to perfectly match a target model's center.",
                              .properties = {
                                  {"name", "Filename/label substring of the target model", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  QueueAction([queryLower]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      auto *cam = r->GetScene().GetActiveCamera();
                                      if (!cam) return;
                                      for (auto *m : r->GetScene().GetModels())
                                      {
                                          std::string fname = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                          for (auto &c : fname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                          if (fname.find(queryLower) == std::string::npos) continue;

                                          AABB worldBB;
                                          bool first = true;
                                          for (auto &n : m->GetNodeInfos())
                                          {
                                              if (first) { worldBB = n.worldBoundingBox; first = false; }
                                              else
                                              {
                                                  worldBB.min = glm::min(worldBB.min, n.worldBoundingBox.min);
                                                  worldBB.max = glm::max(worldBB.max, n.worldBoundingBox.max);
                                              }
                                          }
                                          vec3 center = worldBB.GetCenter();
                                          float dist = glm::distance(cam->GetPosition(), center);
                                          auto &s = Settings::Get<GlobalSettings>();
                                          s.dof_focus_scale = dist;
                                          s.dof = true;
                                          break;
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_render_value",
                              .description = "Sets a numeric render setting. "
                                             "name: bloom_strength|bloom_range|render_scale|IBL_intensity|lights_intensity|"
                                             "dof_focus_scale|dof_blur_range|motion_blur_strength|motion_blur_samples|"
                                             "cas_sharpness|shadow_map_size|time_scale. value: float.",
                              .properties = {
                                  {"name", "Setting name", pagent::SchemaType::String, true},
                                  {"value", "Numeric value", pagent::SchemaType::Number, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string name = ExtractArgStr(args, "name");
                                  float value = ExtractArgNum(args, "value");
                                  QueueAction([name, value]()
                                              {
                                      auto &s = Settings::Get<GlobalSettings>();
                                      if      (name == "bloom_strength")       s.bloom_strength       = value;
                                      else if (name == "bloom_range")          s.bloom_range          = value;
                                      else if (name == "render_scale")         s.render_scale         = std::clamp(value, 0.1f, 4.0f);
                                      else if (name == "IBL_intensity")        s.IBL_intensity        = value;
                                      else if (name == "lights_intensity")     s.lights_intensity     = value;
                                      else if (name == "dof_focus_scale")      s.dof_focus_scale      = value;
                                      else if (name == "dof_blur_range")       s.dof_blur_range       = value;
                                      else if (name == "motion_blur_strength") s.motion_blur_strength = value;
                                      else if (name == "motion_blur_samples")  s.motion_blur_samples  = static_cast<int>(value);
                                      else if (name == "cas_sharpness")        s.cas_sharpness        = std::clamp(value, 0.0f, 1.0f);
                                      else if (name == "shadow_map_size")      s.shadow_map_size      = static_cast<uint32_t>(value);
                                      else if (name == "time_scale")           s.time_scale           = value; });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_render_mode",
                              .description = "Switches the render pipeline mode. mode: raster|hybrid|raytracing.",
                              .properties = {
                                  {"mode", "Render mode: raster, hybrid, raytracing", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string mode = ExtractArgStr(args, "mode");
                                  QueueAction([mode]()
                                              {
                                      auto &s = Settings::Get<GlobalSettings>();
                                      if      (mode == "raster")     s.render_mode = RenderMode::Raster;
                                      else if (mode == "hybrid")     s.render_mode = RenderMode::Hybrid;
                                      else if (mode == "raytracing") s.render_mode = RenderMode::RayTracing; });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_time_of_day",
                              .description = "Switches between day and night skybox/IBL.",
                              .properties = {
                                  {"time", "day or night", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string time = ExtractArgStr(args, "time");
                                  QueueAction([time]()
                                              {
                                      auto &s = Settings::Get<GlobalSettings>();
                                      s.day = (time == "day"); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "set_skybox_hdr",
                              .description = "Loads a new HDR image as the day or night skybox. Use find_file first to locate the .hdr file.",
                              .properties = {
                                  {"path", "Absolute or relative file path to the .hdr file", pagent::SchemaType::String, true},
                                  {"time", "day or night (default day)", pagent::SchemaType::String, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";
                                  if (!std::filesystem::exists(path))
                                      return JsonObj({{"error", JsonStr("file not found: " + path)}});
                                  std::string time = ExtractArgStr(args, "time");
                                  if (time.empty())
                                      time = "day";

                                  QueueAction([path, time]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (!r) return;
                                      Queue *queue = RHII.GetMainQueue();
                                      CommandBuffer *cmd = queue->AcquireCommandBuffer();
                                      cmd->Begin();
                                      SkyBox &skybox = const_cast<SkyBox&>(time == "night" ? r->GetSkyBoxNight() : r->GetSkyBoxDay());
                                      skybox.Destroy();
                                      skybox.LoadSkyBox(cmd, path);
                                      cmd->End();
                                      queue->Submit(1, &cmd, nullptr, nullptr);
                                      cmd->Wait();
                                      cmd->Return();
                                      
                                      auto &s = Settings::Get<GlobalSettings>();
                                      s.day = (time == "day"); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "scatter_models",
                              .description = "Scatters multiple copies of a loaded model around a center point with randomized rotation and scale.",
                              .properties = {
                                  {"name", "Filename/label substring of the model to scatter", pagent::SchemaType::String, true},
                                  {"count", "Number of copies to create", pagent::SchemaType::Number, true},
                                  {"radius", "Radius of the scatter area", pagent::SchemaType::Number, true},
                                  {"x", "Center X (default 0)", pagent::SchemaType::Number, false},
                                  {"y", "Center Y (default 0)", pagent::SchemaType::Number, false},
                                  {"z", "Center Z (default 0)", pagent::SchemaType::Number, false},
                                  {"min_scale", "Minimum random scale (default 0.8)", pagent::SchemaType::Number, false},
                                  {"max_scale", "Maximum random scale (default 1.2)", pagent::SchemaType::Number, false},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string query = JsonUnescape(ExtractArgStr(args, "name"));
                                  if (query.empty())
                                      return "{\"error\":\"missing name\"}";
                                  int count = static_cast<int>(ExtractArgNum(args, "count"));
                                  float radius = ExtractArgNum(args, "radius");
                                  float x = ExtractArgNum(args, "x");
                                  float y = ExtractArgNum(args, "y");
                                  float z = ExtractArgNum(args, "z");

                                  float min_scale = ExtractArgNum(args, "min_scale");
                                  if (min_scale <= 0.0f)
                                      min_scale = 0.8f;
                                  float max_scale = ExtractArgNum(args, "max_scale");
                                  if (max_scale <= 0.0f)
                                      max_scale = 1.2f;

                                  if (count <= 0 || radius <= 0.0f)
                                      return "{\"error\":\"invalid count or radius\"}";

                                  std::string queryLower = query;
                                  for (auto &c : queryLower)
                                      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                                  auto *r = GetGlobalSystem<RendererSystem>();
                                  if (!r)
                                      return "{\"error\":\"no renderer\"}";

                                  std::string pathToLoad;
                                  for (auto *m : r->GetScene().GetModels())
                                  {
                                      std::string fname = m->GetLabel().empty() ? m->GetFilePath().filename().string() : m->GetLabel();
                                      for (auto &c : fname)
                                          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                      if (fname.find(queryLower) != std::string::npos)
                                      {
                                          pathToLoad = m->GetFilePath().string();
                                          break;
                                      }
                                  }

                                  if (pathToLoad.empty())
                                      return JsonObj({{"error", JsonStr("model not found: " + query)}});

                                  QueueAction([pathToLoad, count, radius, x, y, z, min_scale, max_scale]()
                                              {
                                      for (int i = 0; i < count; ++i)
                                      {
                                          Model *m = Model::Load(pathToLoad);
                                          if (m)
                                          {
                                              float ang = pe::rand(0.0f, 1.0f) * glm::two_pi<float>();
                                              float rad = std::sqrt(pe::rand(0.0f, 1.0f)) * radius;
                                              float ox = std::cos(ang) * rad;
                                              float oz = std::sin(ang) * rad;
                                              
                                              float scale = min_scale + pe::rand(0.0f, 1.0f) * (max_scale - min_scale);
                                              float rotY = pe::rand(0.0f, 1.0f) * glm::two_pi<float>();
                                              
                                              mat4 T = glm::translate(mat4(1.0f), vec3(x + ox, y, z + oz));
                                              mat4 R = glm::rotate(mat4(1.0f), rotY, vec3(0.0f, 1.0f, 0.0f));
                                              mat4 S = glm::scale(mat4(1.0f), vec3(scale));
                                              m->GetMatrix() = T * R * S;
                                              m->GetDirtyNodes() = true;
                                          }
                                      } });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "save_scene",
                              .description = "Saves the current scene to a .json file. "
                                             "If only a filename is given (e.g. 'demo'), it is saved to the Scenes folder inside Assets. "
                                             "A full path overrides the default location.",
                              .properties = {
                                  {"path", "Filename (e.g. 'demo' or 'demo.json') or full path", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";

                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative() && fpath.parent_path().empty())
                                  {
                                      // bare filename — place in Scenes folder
                                      if (fpath.extension().empty())
                                          fpath.replace_extension(".json");
                                      fpath = std::filesystem::path(Path::Assets) / "Scenes" / fpath;
                                  }
                                  std::filesystem::create_directories(fpath.parent_path());
                                  std::string resolved = fpath.string();

                                  QueueAction([resolved]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (r) r->GetScene().SaveScene(resolved); });
                                  return "{\"status\":\"ok\"}";
                              }});

        m_agent.RegisterTool({.name = "load_scene",
                              .description = "Loads a scene from a .json file, replacing the current scene. "
                                             "If only a filename is given (e.g. 'demo'), it is looked up in the Scenes folder inside Assets. "
                                             "A full path overrides the default location.",
                              .properties = {
                                  {"path", "Filename (e.g. 'demo' or 'demo.json') or full path", pagent::SchemaType::String, true},
                              },
                              .handler = [this](const std::string &args) -> std::string
                              {
                                  std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                  if (path.empty())
                                      return "{\"error\":\"missing path\"}";

                                  std::filesystem::path fpath(path);
                                  if (fpath.is_relative() && fpath.parent_path().empty())
                                  {
                                      if (fpath.extension().empty())
                                          fpath.replace_extension(".json");
                                      fpath = std::filesystem::path(Path::Assets) / "Scenes" / fpath;
                                  }
                                  if (!std::filesystem::exists(fpath))
                                      return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                                  std::string resolved = fpath.string();
                                  QueueAction([resolved]()
                                              {
                                      auto *r = GetGlobalSystem<RendererSystem>();
                                      if (r) r->GetScene().LoadScene(resolved); });
                                  return "{\"status\":\"ok\"}";
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
        // Enter submits; Shift+Enter inserts a newline.
        // We detect Enter via IsKeyPressed so that pasted newlines don't trigger send.
        ImGui::BeginDisabled(busy);
        const float inputWidth = ImGui::GetContentRegionAvail().x - 60.0f;
        ImGui::InputTextMultiline("##input", m_inputBuf, sizeof(m_inputBuf),
                                  ImVec2(inputWidth, inputHeight),
                                  ImGuiInputTextFlags_None);
        bool inputActive = ImGui::IsItemActive();
        if (inputActive && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().KeyShift)
        {
            submit = true;
            // Remove the newline that was just inserted by the input widget
            size_t len = std::strlen(m_inputBuf);
            if (len > 0 && m_inputBuf[len - 1] == '\n')
                m_inputBuf[len - 1] = '\0';
        }
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
