#include "AgentWidget.h"
#include "FileBrowser.h"
#include "GUI/GUI.h"
#include "Systems/RendererSystem.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "PhasmaAgent/AgentUtils.h"
#include "PhasmaAgent/VectorStore.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/IncludeGraph.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_vulkan.h"
#include "API/Image.h"
#include "stb/stb_image.h"
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace pagent;

namespace pe
{

    static std::string ToLower(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

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

    void AgentWidget::RegisterTools()
    {
        // Derive project root (repo root) as the single workspace for all tools.
        // Resolve the repo root from the runtime Assets path so CLI tools and file tools
        // stay anchored to the real workspace instead of the build output folder.
        // All tool paths are then relative to repo root:
        //   source  -> PhasmaEditor/Code/App/App.cpp
        //   assets  -> PhasmaEditor/Assets/Shaders/Tonemap.hlsl
        //   core    -> PhasmaCore/Code/Base/Path.h
        std::string projectRoot = GetRepoRootFromAssets().string();
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
                                              "Preferred workflow: use grep_project to find the relevant line number first, "
                                              "then read only the surrounding lines with start_line/end_line. "
                                              "Avoid reading whole files - surgical reads of 30-100 lines cost far fewer tokens.",
                               .properties = {
                                   {"path", "File path relative to project root (e.g. 'PhasmaCore/Code/Base/Path.h') or absolute", pagent::SchemaType::String, true},
                                   {"start_line", "First line to read, 1-based (default: 1). Use after grep_project to read just the relevant area.", pagent::SchemaType::Integer, false},
                                   {"end_line", "Last line to read, inclusive (default: read all). Combine with start_line for surgical reads.", pagent::SchemaType::Integer, false},
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

                                   int64_t startLine = ExtractArgInt(args, "start_line", 1);
                                   int64_t endLine = ExtractArgInt(args, "end_line", 0); // 0 = no limit
                                   if (startLine < 1)
                                       startLine = 1;

                                   std::ifstream file(fpath);
                                   if (!file.is_open())
                                       return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

                                   std::string content;
                                   content.reserve(8192);
                                   std::string line;
                                   int64_t lineNum = 0;
                                   int64_t totalLines = 0;
                                   while (std::getline(file, line))
                                   {
                                       ++lineNum;
                                       ++totalLines;
                                       if (lineNum < startLine)
                                           continue;
                                       if (endLine > 0 && lineNum > endLine)
                                           break;
                                       content += line;
                                       content += '\n';
                                   }
                                   // Count remaining lines for total
                                   while (std::getline(file, line))
                                       ++totalLines;

                                   nlohmann::json result = {
                                       {"path", std::filesystem::relative(fpath, projectRoot).string()},
                                       {"content", content},
                                       {"start_line", startLine},
                                       {"end_line", endLine > 0 ? std::min(endLine, lineNum) : lineNum},
                                       {"total_lines", totalLines},
                                   };
                                   return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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

                                   std::string queryLower = ToLower(query);

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

                                       std::string nameLower = ToLower(entry.path().filename().string());

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

        m_agent->RegisterTool(
            {.name = "grep_project",
             .description =
                 "Search for a literal string or regex pattern in project source files. "
                 "Returns matching lines with file path and line number. "
                 "Use for finding function definitions, usages, class members, shader code, etc. "
                 "Literal search (default) is fast and sufficient for identifiers. "
                 "Set regex=true for pattern matching (ECMAScript syntax).",
             .properties = {
                 {"pattern", "Text to search for. Literal string by default; ECMAScript regex if regex=true.", pagent::SchemaType::String, true},
                 {"path", "Subdirectory to search (e.g. 'PhasmaEditor/Code'). Defaults to project root.", pagent::SchemaType::String, false},
                 {"glob", "File extension filter (e.g. '*.cpp', '*.hlsl', '*.h'). Defaults to all files.", pagent::SchemaType::String, false},
                 {"regex", "Set to true to treat pattern as an ECMAScript regex. Default: false (literal).", pagent::SchemaType::Boolean, false},
                 {"case_sensitive", "Case-sensitive match. Default: true.", pagent::SchemaType::Boolean, false},
                 {"max_results", "Maximum number of matching lines to return. Default: 50.", pagent::SchemaType::Integer, false},
             },
             .handler = [projectRoot](const std::string &args) -> std::string
             {
                 std::string pattern = JsonUnescape(ExtractArgStr(args, "pattern"));
                 std::string searchDir = JsonUnescape(ExtractArgStr(args, "path"));
                 std::string globFilter = JsonUnescape(ExtractArgStr(args, "glob"));
                 bool useRegex = ExtractArgStr(args, "regex") == "true";
                 bool caseSens = ExtractArgStr(args, "case_sensitive") != "false"; // default true
                 int maxResults = static_cast<int>(ExtractArgInt(args, "max_results", 50));

                 if (pattern.empty())
                     return "{\"error\":\"missing pattern\"}";
                 if (maxResults <= 0 || maxResults > 500)
                     maxResults = 50;

                 // Resolve search root
                 std::filesystem::path searchPath = searchDir.empty()
                                                        ? std::filesystem::path(projectRoot)
                                                        : std::filesystem::path(projectRoot) / searchDir;
                 if (!IsPathSafe(searchPath.string(), projectRoot))
                     return "{\"error\":\"path outside project directory\"}";
                 if (!std::filesystem::exists(searchPath))
                     return JsonObj({{"error", JsonStr("directory not found: " + searchPath.string())}});

                 // Build glob suffix filter (e.g. "*.cpp" -> ".cpp")
                 std::string extFilter;
                 if (!globFilter.empty())
                 {
                     auto star = globFilter.find('*');
                     extFilter = (star != std::string::npos) ? globFilter.substr(star + 1) : globFilter;
                     if (!caseSens)
                         extFilter = ToLower(extFilter);
                 }

                 // Compile regex if requested
                 std::regex rx;
                 bool regexValid = false;
                 if (useRegex)
                 {
                     try
                     {
                         auto flags = std::regex::ECMAScript | std::regex::optimize;
                         if (!caseSens)
                             flags |= std::regex::icase;
                         rx = std::regex(pattern, flags);
                         regexValid = true;
                     }
                     catch (const std::regex_error &e)
                     {
                         return JsonObj({{"error", JsonStr(std::string("invalid regex: ") + e.what())}});
                     }
                 }

                 // Case-fold literal pattern once
                 std::string literalLower = caseSens ? pattern : ToLower(pattern);

                 nlohmann::json matchesArray = nlohmann::json::array();
                 int count = 0;

                 auto processFile = [&](const std::filesystem::path &filePath) -> bool
                 {
                     std::ifstream file(filePath, std::ios::binary);
                     if (!file.is_open())
                         return true; // continue

                     // Quick binary pre-check on first 1024 bytes - avoids loading a
                     // 50 MB binary into RAM via getline before we hit a null byte
                     char buf[1024];
                     std::streamsize n = (file.read(buf, sizeof(buf)), file.gcount());
                     if (std::find(buf, buf + n, '\0') != buf + n)
                         return true; // binary file, skip
                     file.clear();
                     file.seekg(0);

                     std::string relPath = std::filesystem::relative(filePath, projectRoot).string();
                     std::replace(relPath.begin(), relPath.end(), '\\', '/');

                     std::string line;
                     int lineNum = 0;
                     bool firstLine = true;
                     while (std::getline(file, line))
                     {
                         ++lineNum;

                         // Strip UTF-8 BOM (EF BB BF) from the very first line
                         if (firstLine)
                         {
                             if (line.size() >= 3 &&
                                 static_cast<unsigned char>(line[0]) == 0xEF &&
                                 static_cast<unsigned char>(line[1]) == 0xBB &&
                                 static_cast<unsigned char>(line[2]) == 0xBF)
                                 line.erase(0, 3);
                             firstLine = false;
                         }

                         // Strip trailing \r (CRLF files)
                         if (!line.empty() && line.back() == '\r')
                             line.pop_back();

                         bool matched = false;
                         if (useRegex && regexValid)
                         {
                             matched = std::regex_search(line, rx);
                         }
                         else
                         {
                             std::string haystack = caseSens ? line : ToLower(line);
                             matched = haystack.find(literalLower) != std::string::npos;
                         }

                         if (!matched)
                             continue;

                         // Trim leading whitespace for readability
                         std::string trimmed = line;
                         auto ws = trimmed.find_first_not_of(" \t");
                         if (ws != std::string::npos)
                             trimmed = trimmed.substr(ws);

                         matchesArray.push_back({{"file", relPath}, {"line", lineNum}, {"text", trimmed}});

                         if (++count >= maxResults)
                             return false; // stop
                     }
                     return true; // continue
                 };

                 auto matchesExt = [&](const std::filesystem::path &p) -> bool
                 {
                     if (extFilter.empty())
                         return true;
                     std::string ext = p.extension().string();
                     if (!caseSens)
                         ext = ToLower(ext);
                     // extFilter may be ".cpp" or "cpp"
                     if (extFilter[0] == '.')
                         return ext == extFilter;
                     return ext == ("." + extFilter);
                 };

                 // Use the iterator directly so we can call disable_recursion_pending()
                 // on directories we want to skip entirely (build/, .git/, .vs/, etc.)
                 // rather than entering them and discarding files one by one.
                 bool keepGoing = true;
                 auto it = std::filesystem::recursive_directory_iterator(
                     searchPath, std::filesystem::directory_options::skip_permission_denied);
                 auto end = std::filesystem::recursive_directory_iterator();
                 for (; it != end && keepGoing; ++it)
                 {
                     if (it->is_directory())
                     {
                         std::string dirName = it->path().filename().string();
                         if (dirName == "build" || (!dirName.empty() && dirName[0] == '.'))
                             it.disable_recursion_pending();
                         continue;
                     }
                     if (!it->is_regular_file())
                         continue;
                     if (!matchesExt(it->path()))
                         continue;
                     keepGoing = processFile(it->path());
                 }

                 nlohmann::json result = {{"count", count}, {"matches", matchesArray}};
                 return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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

                                   std::string queryLower = ToLower(query);

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

                                       std::string ext = ToLower(entry.path().extension().string());
                                       if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx")
                                           continue;

                                       auto u8rel = std::filesystem::relative(entry.path(), Path::Assets + "Objects").u8string();
                                       std::string relPath(u8rel.begin(), u8rel.end());
                                       std::replace(relPath.begin(), relPath.end(), '\\', '/');

                                       std::string relLower = ToLower(relPath);

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

        // =====================================================================
        // find_symbol — query BM25 index for a symbol by name
        // =====================================================================

        m_agent->RegisterTool(
            {.name = "find_symbol",
             .description = "Searches the codebase index for a single class, function, method, or struct by name. "
                            "Returns file path and exact line range — use with read_project_file(start_line, end_line). "
                            "Far more efficient than grep_project for symbol lookup. "
                            "Requires the codebase index to be built. "
                            "Tip: when you need to look up several symbols at once, use search_codebase instead.",
             .properties = {
                 {"name", "Symbol name to search for (e.g. 'CommandBuffer', 'CreatePipeline', 'RenderGraph')", pagent::SchemaType::String, true},
                 {"max_results", "Maximum matches to return, 1-20 (default: 5)", pagent::SchemaType::Integer, false},
             },
             .handler = [bm25 = m_codebaseBM25, store = m_codebaseStore](const std::string &args) -> std::string
             {
                 std::string symbolName = JsonUnescape(ExtractArgStr(args, "name"));
                 if (symbolName.empty())
                     return "{\"error\":\"missing name\"}";

                 int64_t maxResults = ExtractArgInt(args, "max_results", 5);
                 if (maxResults < 1)
                     maxResults = 1;
                 if (maxResults > 20)
                     maxResults = 20;

                 if (!bm25 || bm25->Size() == 0)
                     return "{\"error\":\"codebase index not built - use the Index button first\"}";

                 auto results = bm25->Search(symbolName, static_cast<int>(maxResults) * 6);
                 if (results.empty())
                     return JsonObj({{"error", JsonStr("no symbols found matching: " + symbolName)}});

                 // Build id -> (file, lines) from vector store metadata
                 std::unordered_map<std::string, std::pair<std::string, std::string>> idToMeta;
                 if (store)
                 {
                     store->ForEachEntry([&](const pagent::VectorEntry &entry)
                                         {
                         try
                         {
                             auto meta = nlohmann::json::parse(entry.metadata);
                             std::string file  = meta.value("file",  "");
                             std::string lines = meta.value("lines", "");
                             if (!file.empty() && !lines.empty())
                                 idToMeta[entry.id] = {file, lines};
                         }
                         catch (...) {} });
                 }

                 // Case-insensitive helpers
                 auto toLower = [](std::string s)
                 {
                     std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                                    { return std::tolower(c); });
                     return s;
                 };
                 std::string nameLower = toLower(symbolName);

                 // Definition-priority scoring:
                 //   3 — file stem matches symbol name (e.g. RendererSystem.h)
                 //   2 — metadata header contains "Class: <name>" or "Method: <name>"
                 //   1 — file path contains the symbol name
                 //   0 — plain reference (BM25 rank only)
                 auto definitionPriority = [&](const std::string &file, const std::string &header) -> int
                 {
                     std::string fileLower = toLower(file);
                     std::string headerLower = toLower(header);

                     // Strip directory prefix to get stem
                     auto slash = fileLower.rfind('/');
                     if (slash == std::string::npos)
                         slash = fileLower.rfind('\\');
                     std::string stem = (slash != std::string::npos) ? fileLower.substr(slash + 1) : fileLower;
                     auto dot = stem.rfind('.');
                     if (dot != std::string::npos)
                         stem = stem.substr(0, dot);

                     if (stem == nameLower)
                         return 3;

                     // "class: rendersystem" or "method: rendersystem" in the header
                     for (const char *tag : {"class: ", "method: ", "struct: "})
                     {
                         auto pos = headerLower.find(tag);
                         if (pos != std::string::npos && headerLower.substr(pos + strlen(tag), nameLower.size()) == nameLower)
                             return 2;
                     }

                     if (fileLower.find(nameLower) != std::string::npos)
                         return 1;

                     return 0;
                 };

                 // Attach priority to each candidate then stable-sort definitions first
                 struct Candidate
                 {
                     std::string id;
                     std::string content;
                     float bm25Score;
                     int priority;
                 };
                 std::vector<Candidate> candidates;
                 candidates.reserve(results.size());
                 for (auto &r : results)
                 {
                     auto it = idToMeta.find(r.id);
                     if (it == idToMeta.end())
                         continue;
                     std::string header;
                     if (auto nl = r.content.find('\n'); nl != std::string::npos)
                         header = r.content.substr(0, nl);
                     else
                         header = r.content.substr(0, std::min(r.content.size(), (size_t)200));
                     candidates.push_back({r.id, r.content, r.score, definitionPriority(it->second.first, header)});
                 }
                 std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b)
                                  { return a.priority > b.priority; });

                 nlohmann::json matches = nlohmann::json::array();
                 std::set<std::string> seen;
                 for (auto &r : candidates)
                 {
                     if (static_cast<int64_t>(matches.size()) >= maxResults)
                         break;

                     auto it = idToMeta.find(r.id);
                     if (it == idToMeta.end())
                         continue;

                     const std::string &file = it->second.first;
                     const std::string &lines = it->second.second;
                     std::string key = file + ":" + lines;
                     if (!seen.insert(key).second)
                         continue;

                     int startLine = 0, endLine = 0;
                     if (auto dash = lines.find('-'); dash != std::string::npos)
                     {
                         startLine = std::stoi(lines.substr(0, dash));
                         endLine = std::stoi(lines.substr(dash + 1));
                     }

                     // First line of chunk content is the metadata header, e.g.:
                     // "// File: API/RHI.h | Namespace: pe | Class: RHI | Method: CreateBuffer"
                     std::string header;
                     if (auto nl = r.content.find('\n'); nl != std::string::npos)
                         header = r.content.substr(0, nl);
                     else
                         header = r.content;

                     // Extract structured fields from the header so the model never needs to parse it
                     auto extractField = [&](const std::string &tag) -> std::string
                     {
                         auto pos = header.find(tag);
                         if (pos == std::string::npos)
                             return "";
                         pos += tag.size();
                         auto end = header.find(" |", pos);
                         return end != std::string::npos ? header.substr(pos, end - pos) : header.substr(pos);
                     };

                     nlohmann::json match = {
                         {"file", file},
                         {"start_line", startLine},
                         {"end_line", endLine},
                     };
                     std::string ns = extractField("Namespace: ");
                     std::string cls = extractField("Class: ");
                     std::string method = extractField("Method: ");
                     if (!ns.empty())
                         match["namespace"] = ns;
                     if (!cls.empty())
                         match["class"] = cls;
                     if (!method.empty())
                         match["method"] = method;

                     matches.push_back(std::move(match));
                 }

                 if (matches.empty())
                     return JsonObj({{"error", JsonStr("no indexed symbols found for: " + symbolName)}});

                 return nlohmann::json{{"matches", matches}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
             }});

        // =====================================================================
        // search_codebase — parallel multi-query BM25 + vector search
        // =====================================================================

        m_agent->RegisterTool(
            {.name = "search_codebase",
             .description = "Search the indexed codebase for multiple symbols or concepts simultaneously. "
                            "Runs all queries in parallel (BM25 keyword + semantic vector) and merges results. "
                            "Use this instead of calling find_symbol multiple times — pass all related names "
                            "in one call to save round-trips. Requires the codebase index to be built. "
                            "Example: search for 'RenderGraph', 'AddPass', and 'IRenderPass' all at once.",
             .properties = {
                 {"queries", "Array of symbol names or concepts to search simultaneously (e.g. [\"RenderGraph\", \"AddPass\"])", pagent::SchemaType::Array, true},
                 {"max_results", "Maximum total matches to return, 1-30 (default: 10)", pagent::SchemaType::Integer, false},
             },
             .handler = [bm25 = m_codebaseBM25, store = m_codebaseStore, embProvider = m_embeddingProvider](const std::string &args) -> std::string
             {
                 auto queries = ExtractArgArray(args, "queries");
                 if (queries.empty())
                     return "{\"error\":\"missing queries array\"}";
                 if (queries.size() > 16)
                     queries.resize(16);

                 int64_t maxResults = ExtractArgInt(args, "max_results", 10);
                 if (maxResults < 1)
                     maxResults = 1;
                 if (maxResults > 30)
                     maxResults = 30;

                 if (!bm25 || bm25->Size() == 0)
                     return "{\"error\":\"codebase index not built - use the Index button first\"}";

                 // --- BM25 parallel search across all queries ---
                 auto bm25Results = bm25->SearchMulti(queries, static_cast<int>(maxResults) * 6);

                 // Build id -> (file, lines) from vector store metadata
                 std::unordered_map<std::string, std::pair<std::string, std::string>> idToMeta;
                 if (store)
                 {
                     store->ForEachEntry([&](const pagent::VectorEntry &entry)
                                         {
                         try
                         {
                             auto meta = nlohmann::json::parse(entry.metadata);
                             std::string file  = meta.value("file",  "");
                             std::string lines = meta.value("lines", "");
                             if (!file.empty() && !lines.empty())
                                 idToMeta[entry.id] = {file, lines};
                         }
                         catch (...) {} });
                 }

                 // --- Optional: semantic vector search ---
                 // Embed queries sequentially (HTTP calls; provider may not be re-entrant)
                 // then run SearchMulti in parallel across all embeddings.
                 std::unordered_map<std::string, float> vectorScores; // id -> best cosine score
                 if (embProvider && store && store->Size() > 0)
                 {
                     std::vector<std::vector<float>> embeddings;
                     embeddings.reserve(queries.size());
                     for (const auto &q : queries)
                     {
                         auto emb = embProvider->Embed(q);
                         if (!emb.empty())
                             embeddings.push_back(std::move(emb));
                     }
                     if (!embeddings.empty())
                     {
                         auto vecResults = store->SearchMulti(embeddings, static_cast<int>(maxResults) * 4);
                         for (const auto &r : vecResults)
                             if (r.entry)
                                 vectorScores[r.entry->id] = r.score;
                     }
                 }

                 // --- Merge BM25 + vector scores ---
                 // Normalize BM25 to [0,1] against the top result, then blend.
                 float bm25Max = bm25Results.empty() ? 1.0f : std::max(bm25Results[0].score, 1e-6f);

                 struct ScoredEntry
                 {
                     std::string id;
                     std::string content;
                     float score;
                     int priority;
                 };
                 std::unordered_map<std::string, ScoredEntry> merged;

                 for (auto &r : bm25Results)
                 {
                     float normBm25 = r.score / bm25Max;
                     float vecScore = 0.0f;
                     auto vit = vectorScores.find(r.id);
                     if (vit != vectorScores.end())
                         vecScore = vit->second;

                     float combined = vectorScores.empty()
                                          ? normBm25
                                          : normBm25 * 0.65f + vecScore * 0.35f;
                     merged[r.id] = {r.id, r.content, combined, 0};
                 }

                 // Lift vector-only hits that BM25 missed
                 if (!vectorScores.empty() && store)
                 {
                     store->ForEachEntry([&](const pagent::VectorEntry &entry)
                                         {
                         if (!vectorScores.count(entry.id)) return;
                         if (merged.count(entry.id))        return;
                         merged[entry.id] = {entry.id, entry.content, vectorScores[entry.id] * 0.35f, 0}; });
                 }

                 // --- Definition-priority re-ranking (same logic as find_symbol) ---
                 auto toLowerLocal = [](std::string s)
                 {
                     std::transform(s.begin(), s.end(), s.begin(),
                                    [](unsigned char c)
                                    { return std::tolower(c); });
                     return s;
                 };

                 std::vector<std::string> queriesLower;
                 queriesLower.reserve(queries.size());
                 for (const auto &q : queries)
                     queriesLower.push_back(toLowerLocal(q));

                 auto defPriority = [&](const std::string &file, const std::string &header) -> int
                 {
                     std::string fileLower = toLowerLocal(file);
                     std::string headerLower = toLowerLocal(header);

                     auto slash = fileLower.rfind('/');
                     if (slash == std::string::npos)
                         slash = fileLower.rfind('\\');
                     std::string stem = (slash != std::string::npos) ? fileLower.substr(slash + 1) : fileLower;
                     auto dot = stem.rfind('.');
                     if (dot != std::string::npos)
                         stem = stem.substr(0, dot);

                     for (const auto &nameLower : queriesLower)
                     {
                         if (stem == nameLower)
                             return 3;
                         for (const char *tag : {"class: ", "method: ", "struct: "})
                         {
                             auto pos = headerLower.find(tag);
                             if (pos != std::string::npos &&
                                 headerLower.substr(pos + strlen(tag), nameLower.size()) == nameLower)
                                 return 2;
                         }
                         if (fileLower.find(nameLower) != std::string::npos)
                             return 1;
                     }
                     return 0;
                 };

                 std::vector<ScoredEntry> candidates;
                 candidates.reserve(merged.size());
                 for (auto &[id, e] : merged)
                 {
                     auto it = idToMeta.find(id);
                     if (it == idToMeta.end())
                         continue;
                     std::string header;
                     if (auto nl = e.content.find('\n'); nl != std::string::npos)
                         header = e.content.substr(0, nl);
                     else
                         header = e.content.substr(0, std::min(e.content.size(), (size_t)200));
                     e.priority = defPriority(it->second.first, header);
                     candidates.push_back(std::move(e));
                 }

                 std::stable_sort(candidates.begin(), candidates.end(),
                                  [](const ScoredEntry &a, const ScoredEntry &b)
                                  { return a.priority != b.priority ? a.priority > b.priority : a.score > b.score; });

                 // --- Format output (same structure as find_symbol) ---
                 nlohmann::json matches = nlohmann::json::array();
                 std::set<std::string> seen;
                 for (auto &e : candidates)
                 {
                     if (static_cast<int64_t>(matches.size()) >= maxResults)
                         break;

                     auto it = idToMeta.find(e.id);
                     if (it == idToMeta.end())
                         continue;

                     const std::string &file = it->second.first;
                     const std::string &lines = it->second.second;
                     std::string key = file + ":" + lines;
                     if (!seen.insert(key).second)
                         continue;

                     int startLine = 0, endLine = 0;
                     if (auto dash = lines.find('-'); dash != std::string::npos)
                     {
                         startLine = std::stoi(lines.substr(0, dash));
                         endLine = std::stoi(lines.substr(dash + 1));
                     }

                     std::string header;
                     if (auto nl = e.content.find('\n'); nl != std::string::npos)
                         header = e.content.substr(0, nl);
                     else
                         header = e.content;

                     auto extractField = [&](const std::string &tag) -> std::string
                     {
                         auto pos = header.find(tag);
                         if (pos == std::string::npos)
                             return "";
                         pos += tag.size();
                         auto end = header.find(" |", pos);
                         return end != std::string::npos ? header.substr(pos, end - pos) : header.substr(pos);
                     };

                     nlohmann::json match = {{"file", file}, {"start_line", startLine}, {"end_line", endLine}};
                     std::string ns = extractField("Namespace: ");
                     std::string cls = extractField("Class: ");
                     std::string mtd = extractField("Method: ");
                     if (!ns.empty())
                         match["namespace"] = ns;
                     if (!cls.empty())
                         match["class"] = cls;
                     if (!mtd.empty())
                         match["method"] = mtd;
                     matches.push_back(std::move(match));
                 }

                 if (matches.empty())
                     return "{\"error\":\"no indexed symbols found for the given queries\"}";

                 return nlohmann::json{{"matches", matches}, {"queries", queries}}
                     .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
             }});

        // =====================================================================
        // patch_project_file — surgical line-range replacement (token-efficient edits)
        // =====================================================================

        m_agent->RegisterTool(
            {.name = "patch_project_file",
             .description = "Replaces a specific range of lines in a project source file. "
                            "Token-efficient alternative to write_project_file: send only the changed lines, not the whole file. "
                            "Preferred for all C++ source, header, and shader edits. "
                            "Workflow: (1) read_project_file with start_line/end_line to see current code, "
                            "(2) patch_project_file with the exact replacement. "
                            "IMPORTANT — to INSERT a line without losing existing content: include the existing line(s) "
                            "in the replacement. Example: to insert '// comment' before line 8 without removing it, "
                            "use start_line=8 end_line=8 replacement='// comment\\n<original line 8 content>'. "
                            "Replacing an empty line removes it — always include it in the replacement if it must be kept. "
                            "Use expected_context to guard against wrong-offset edits.",
             .properties = {
                 {"path", "File path relative to project root (e.g. 'PhasmaEditor/Code/App/App.cpp')", pagent::SchemaType::String, true},
                 {"start_line", "First line to replace, 1-based inclusive", pagent::SchemaType::Integer, true},
                 {"end_line", "Last line to replace, 1-based inclusive (must be >= start_line)", pagent::SchemaType::Integer, true},
                 {"replacement", "New text replacing the specified lines. May contain newlines. Pass empty string to delete lines. To insert without losing content, include the displaced lines in this string.", pagent::SchemaType::String, true},
                 {"expected_context", "Optional safety check: a short substring that must appear in the target region. Patch is rejected if not found.", pagent::SchemaType::String, false},
             },
             .handler = [projectRoot](const std::string &args) -> std::string
             {
                 std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                 std::string replacement = JsonUnescape(ExtractArgStr(args, "replacement"));
                 std::string expectedContext = JsonUnescape(ExtractArgStr(args, "expected_context"));
                 int64_t startLine = ExtractArgInt(args, "start_line", 0);
                 int64_t endLine = ExtractArgInt(args, "end_line", 0);

                 if (path.empty())
                     return "{\"error\":\"missing path\"}";
                 if (startLine < 1 || endLine < startLine)
                     return "{\"error\":\"start_line must be >= 1 and end_line must be >= start_line\"}";

                 std::filesystem::path fpath(path);
                 if (fpath.is_relative())
                     fpath = std::filesystem::path(projectRoot) / fpath;

                 if (!IsPathSafe(fpath.string(), projectRoot))
                     return "{\"error\":\"path outside project directory\"}";

                 if (!std::filesystem::exists(fpath))
                     return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

                 // Read all lines in binary mode to detect and preserve line endings exactly.
                 std::vector<std::string> lines;
                 bool trailingNewline = false;
                 bool useCRLF = false;
                 {
                     std::ifstream file(fpath, std::ios::binary);
                     if (!file.is_open())
                         return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

                     // Peek at first \n to detect CRLF vs LF
                     std::string firstLine;
                     if (std::getline(file, firstLine))
                     {
                         if (!firstLine.empty() && firstLine.back() == '\r')
                         {
                             useCRLF = true;
                             firstLine.pop_back();
                         }
                         lines.push_back(std::move(firstLine));
                         std::string line;
                         while (std::getline(file, line))
                         {
                             if (useCRLF && !line.empty() && line.back() == '\r')
                                 line.pop_back();
                             lines.push_back(std::move(line));
                         }
                     }

                     // Detect trailing newline from last byte
                     file.clear();
                     file.seekg(-1, std::ios::end);
                     if (file.good())
                     {
                         char last;
                         file.get(last);
                         trailingNewline = (last == '\n');
                     }
                 }

                 int64_t totalLines = static_cast<int64_t>(lines.size());
                 if (startLine > totalLines + 1 || endLine > totalLines)
                     return JsonObj({{"error", JsonStr("line range out of bounds: file has " + std::to_string(totalLines) + " lines")}});

                 // Validate expected_context if provided
                 if (!expectedContext.empty())
                 {
                     std::string region;
                     for (int64_t i = startLine - 1; i < endLine; ++i)
                         region += lines[static_cast<size_t>(i)] + "\n";
                     if (region.find(expectedContext) == std::string::npos)
                         return "{\"error\":\"expected_context not found in target region — patch rejected to prevent wrong-offset edit\"}";
                 }

                 // Split replacement into lines
                 std::vector<std::string> repLines;
                 if (!replacement.empty())
                 {
                     std::istringstream ss(replacement);
                     std::string line;
                     while (std::getline(ss, line))
                         repLines.push_back(std::move(line));
                     // getline leaves empty entry when replacement ends with '\n' — drop it
                     if (!repLines.empty() && repLines.back().empty() && replacement.back() == '\n')
                         repLines.pop_back();
                 }

                 // Splice: prefix + replacement + suffix
                 std::vector<std::string> result;
                 result.reserve(static_cast<size_t>(totalLines - (endLine - startLine + 1)) + repLines.size());
                 for (int64_t i = 0; i < startLine - 1; ++i)
                     result.push_back(lines[static_cast<size_t>(i)]);
                 for (auto &l : repLines)
                     result.push_back(l);
                 for (int64_t i = endLine; i < totalLines; ++i)
                     result.push_back(lines[static_cast<size_t>(i)]);

                 // Write back in binary mode, restoring the original line ending style
                 {
                     std::ofstream file(fpath, std::ios::binary | std::ios::trunc);
                     if (!file.is_open())
                         return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
                     const std::string lineEnding = useCRLF ? "\r\n" : "\n";
                     for (size_t i = 0; i < result.size(); ++i)
                     {
                         file << result[i];
                         if (i + 1 < result.size() || trailingNewline)
                             file << lineEnding;
                     }
                 }

                 return nlohmann::json{
                     {"status", "patched"},
                     {"path", std::filesystem::relative(fpath, projectRoot).string()},
                     {"lines_replaced", endLine - startLine + 1},
                     {"lines_inserted", static_cast<int64_t>(repLines.size())},
                     {"new_total_lines", static_cast<int64_t>(result.size())},
                 }
                     .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
             }});

        // =====================================================================
        // UI interaction tools
        // =====================================================================

        m_agent->RegisterTool(
            {.name = "take_screenshot",
             .description = "Takes a screenshot of the current editor state and returns it as a base64 PNG image for visual inspection. "
                            "The image is embedded in the tool result and is visible to vision-capable models. "
                            "Use this to see the current layout before deciding where to click with inject_mouse_input.",
             .properties = {},
             .handler = [this](const std::string &) -> std::string
             {
                 auto *renderer = GetGlobalSystem<RendererSystem>();
                 if (!renderer)
                     return "{\"error\":\"RendererSystem not available\"}";

                 auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
                 std::string screenshotPath = Path::Assets + "Agent/ss_" + std::to_string(ts) + ".png";
                 std::filesystem::remove(screenshotPath);

                 // Capture mouse position before the screenshot so we can overlay a cursor.
                 int mouseX = 0, mouseY = 0;
                 SDL_GetMouseState(&mouseX, &mouseY);

                 EventSystem::PushEvent(EventType::Screenshot, screenshotPath);

                 auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                 while (!std::filesystem::exists(screenshotPath))
                 {
                     if (std::chrono::steady_clock::now() > deadline)
                         return "{\"error\":\"screenshot timeout\"}";
                     std::this_thread::sleep_for(std::chrono::milliseconds(50));
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(50));

                 int w = 0, h = 0, c = 0;
                 uint8_t *pixels = stbi_load(screenshotPath.c_str(), &w, &h, &c, 4);
                 std::filesystem::remove(screenshotPath);
                 if (!pixels)
                     return "{\"error\":\"failed to load screenshot\"}";

                 // Draw a crosshair at the current cursor position so the agent can
                 // see exactly where the mouse is and reason about click targets.
                 auto drawCursorOverlay = [](uint8_t *px, int imgW, int imgH, int cx, int cy)
                 {
                     if (cx < 0 || cy < 0 || cx >= imgW || cy >= imgH)
                         return;
                     constexpr int kRadius = 10;
                     constexpr int kThick = 2;
                     auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b)
                     {
                         if (x < 0 || y < 0 || x >= imgW || y >= imgH)
                             return;
                         uint8_t *p = px + (y * imgW + x) * 4;
                         p[0] = r;
                         p[1] = g;
                         p[2] = b;
                         p[3] = 255;
                     };
                     // White crosshair with black outline for visibility on any background
                     for (int d = -kRadius; d <= kRadius; ++d)
                     {
                         for (int t = -kThick; t <= kThick; ++t)
                         {
                             setPixel(cx + d, cy + t, 0, 0, 0);
                             setPixel(cx + t, cy + d, 0, 0, 0);
                         }
                     }
                     for (int d = -kRadius; d <= kRadius; ++d)
                     {
                         setPixel(cx + d, cy, 255, 255, 255);
                         setPixel(cx, cy + d, 255, 255, 255);
                     }
                 };
                 drawCursorOverlay(pixels, w, h, mouseX, mouseY);

                 int rw = w, rh = h;
                 std::vector<uint8_t> resizedPixels;
                 const int kMaxDim = 1024; // conservative start — avoids Anthropic's 5 MB base64 limit
                 if (w > kMaxDim || h > kMaxDim)
                 {
                     resizedPixels = ResizeRGBA(pixels, w, h, rw, rh, kMaxDim);
                     stbi_image_free(pixels);
                     pixels = nullptr;
                 }

                 // Encode and shrink further if the PNG still exceeds the provider's limit (5 MB).
                 constexpr size_t kMaxBytes = 5 * 1024 * 1024;
                 std::vector<uint8_t> pngData;
                 for (int attempt = 0; attempt < 4; ++attempt)
                 {
                     const uint8_t *src = pixels ? pixels : resizedPixels.data();
                     pngData = pagent::EncodeRGBA_PNG(src, rw, rh);
                     if (pngData.empty() || pngData.size() <= kMaxBytes)
                         break;
                     // Still too large — scale down by 75% and retry
                     int nw = rw * 3 / 4, nh = rh * 3 / 4;
                     resizedPixels = ResizeRGBA(pixels ? pixels : resizedPixels.data(), rw, rh, nw, nh, std::max(nw, nh));
                     if (pixels)
                     {
                         stbi_image_free(pixels);
                         pixels = nullptr;
                     }
                     rw = nw;
                     rh = nh;
                 }
                 if (pixels)
                     stbi_image_free(pixels);

                 if (pngData.empty())
                     return "{\"error\":\"failed to encode screenshot as PNG\"}";

                 std::string b64 = pagent::Base64Encode(pngData.data(), pngData.size());
                 return nlohmann::json{
                     {"image_base64", b64},
                     {"mime_type", "image/png"},
                     {"width", rw},
                     {"height", rh},
                     {"cursor_x", mouseX},
                     {"cursor_y", mouseY},
                 }
                     .dump();
             }});

        m_agent->RegisterTool(
            {.name = "query_imgui_windows",
             .description = "Returns all currently visible ImGui panels with their screen positions and sizes, "
                            "plus 'tab_bars': an array of tab bars each listing every tab with its exact click coordinates. "
                            "Use tab_bars to find precise pixel coordinates for clicking specific tabs. "
                            "Use window positions for clicking inside panels.",
             .properties = {},
             .handler = [this](const std::string &) -> std::string
             {
                 nlohmann::json result;
                 result["windows"] = nlohmann::json::array();
                 result["tab_bars"] = nlohmann::json::array();
                 std::mutex mtx;
                 std::condition_variable cv;
                 bool done = false;

                 QueueAction([&]()
                             {
                     ImGuiContext *ctx = ImGui::GetCurrentContext();
                     if (ctx)
                     {
                         for (ImGuiWindow *win : ctx->Windows)
                         {
                             if (!win->WasActive || win->Size.x <= 0 || win->Size.y <= 0)
                                 continue;
                             result["windows"].push_back({
                                 {"name", win->Name},
                                 {"x", static_cast<int>(win->Pos.x)},
                                 {"y", static_cast<int>(win->Pos.y)},
                                 {"width", static_cast<int>(win->Size.x)},
                                 {"height", static_cast<int>(win->Size.y)},
                                 {"collapsed", win->Collapsed},
                                 {"focused", ctx->NavWindow == win},
                             });
                         }

                         // Emit tab bars from dock nodes with per-tab click coordinates.
                         // Docked tab bars live on ImGuiDockNode::TabBar, not in ctx->TabBars.
                         auto emitTabBar = [&](ImGuiTabBar *tb)
                         {
                             if (!tb || tb->Tabs.Size == 0)
                                 return;
                             int barY = static_cast<int>((tb->BarRect.Min.y + tb->BarRect.Max.y) / 2.0f);
                             nlohmann::json tabsArr = nlohmann::json::array();
                             for (int t = 0; t < tb->Tabs.Size; ++t)
                             {
                                 ImGuiTabItem *tab = &tb->Tabs[t];
                                 const char *name = ImGui::TabBarGetTabName(tb, tab);
                                 std::string tabName = name ? name : "";
                                 if (auto pos = tabName.find("##"); pos != std::string::npos)
                                     tabName = tabName.substr(0, pos);
                                 int tabCenterX = static_cast<int>(tb->BarRect.Min.x + tab->Offset + tab->Width / 2.0f);
                                 tabsArr.push_back({
                                     {"name", tabName},
                                     {"click_x", tabCenterX},
                                     {"click_y", barY},
                                     {"selected", tab->ID == tb->SelectedTabId},
                                 });
                             }
                             result["tab_bars"].push_back({
                                 {"bar_x", static_cast<int>(tb->BarRect.Min.x)},
                                 {"bar_y", static_cast<int>(tb->BarRect.Min.y)},
                                 {"tabs", tabsArr},
                             });
                         };

                         // Walk all dock nodes (ImGuiStorage maps ID -> ImGuiDockNode*)
                         for (auto &kv : ctx->DockContext.Nodes.Data)
                         {
                             auto *node = static_cast<ImGuiDockNode *>(kv.val_p);
                             if (node)
                                 emitTabBar(node->TabBar);
                         }
                     }
                     {
                         std::lock_guard lock(mtx);
                         done = true;
                     }
                     cv.notify_one(); });

                 std::unique_lock lock(mtx);
                 if (!cv.wait_for(lock, std::chrono::seconds(5), [&]
                                  { return done; }))
                     return "{\"error\":\"timeout\"}";

                 return result.dump();
             }});

        m_agent->RegisterTool(
            {.name = "inject_mouse_input",
             .description = "Simulates mouse input to interact with editor UI elements. "
                            "Actions: 'move', 'click', 'right_click', 'double_click', 'scroll'. "
                            "Use u/v (normalized 0.0-1.0 fractions of screen width/height) when clicking from a screenshot — "
                            "the tool converts to real pixel coordinates automatically. "
                            "Use click_x/click_y from query_imgui_windows tab_bars directly (already real coords, no u/v needed). "
                            "After clicking call take_screenshot to verify.",
             .properties = {
                 {"u", "Normalized horizontal position 0.0 (left) to 1.0 (right) — use when clicking from a screenshot", pagent::SchemaType::Number, false},
                 {"v", "Normalized vertical position 0.0 (top) to 1.0 (bottom) — use when clicking from a screenshot", pagent::SchemaType::Number, false},
                 {"x", "Real screen X in pixels — use only for coordinates from query_imgui_windows (tab_bars)", pagent::SchemaType::Integer, false},
                 {"y", "Real screen Y in pixels — use only for coordinates from query_imgui_windows (tab_bars)", pagent::SchemaType::Integer, false},
                 {"action", "Action: 'move', 'click', 'right_click', 'double_click', 'scroll'", pagent::SchemaType::String, true},
                 {"scroll_x", "Horizontal scroll delta for 'scroll' action", pagent::SchemaType::Integer, false},
                 {"scroll_y", "Vertical scroll delta for 'scroll' action (negative=down)", pagent::SchemaType::Integer, false},
             },
             .handler = [this](const std::string &args) -> std::string
             {
                 std::string action = ExtractArgStr(args, "action");
                 if (action.empty())
                     return "{\"error\":\"missing action\"}";

                 int scrollX = static_cast<int>(ExtractArgInt(args, "scroll_x", 0));
                 int scrollY = static_cast<int>(ExtractArgInt(args, "scroll_y", 0));

                 // Resolve final pixel coordinates — u/v (normalized) take priority over raw x/y.
                 int x = -1, y = -1;
                 float u = ExtractArgNum(args, "u");
                 float v = ExtractArgNum(args, "v");
                 if (u > 0.0f || v > 0.0f)
                 {
                     int winW = 0, winH = 0;
                     SDL_GetWindowSize(SDL_GL_GetCurrentWindow(), &winW, &winH);
                     x = static_cast<int>(u * winW);
                     y = static_cast<int>(v * winH);
                 }
                 else
                 {
                     x = static_cast<int>(ExtractArgInt(args, "x", -1));
                     y = static_cast<int>(ExtractArgInt(args, "y", -1));
                 }

                 if (x < 0 || y < 0)
                     return "{\"error\":\"provide u/v (normalized) or x/y (real pixels)\"}";

                 std::mutex mtx;
                 std::condition_variable cv;
                 bool done = false;

                 std::string tabHit; // name of tab hit, if any
                 QueueAction([&]()
                             {
                     ImGuiContext *ctx = ImGui::GetCurrentContext();
                     ImGuiIO &io = ImGui::GetIO();
                     ImVec2 pos{static_cast<float>(x), static_cast<float>(y)};

                     // Warp the OS cursor so the user can see where the agent clicked.
                     if (SDL_Window *win = SDL_GetMouseFocus() ? SDL_GetMouseFocus() : SDL_GL_GetCurrentWindow())
                         SDL_WarpMouseInWindow(win, x, y);

                     // For left clicks: check if (x,y) hits a tab in any dock node tab bar.
                     // If so, call TabBarQueueFocus directly — this is instant and immune to
                     // the trickle/timing issues that prevent io.AddMouseButtonEvent from
                     // registering on tab bars rendered before FlushActions() runs.
                     bool handledByTabBar = false;
                     if ((action == "click" || action == "double_click") && ctx)
                     {
                         for (auto &kv : ctx->DockContext.Nodes.Data)
                         {
                             auto *node = static_cast<ImGuiDockNode *>(kv.val_p);
                             if (!node || !node->TabBar)
                                 continue;
                             ImGuiTabBar *tb = node->TabBar;
                             if (!tb->BarRect.Contains(pos))
                                 continue;
                             for (int t = 0; t < tb->Tabs.Size; ++t)
                             {
                                 ImGuiTabItem *tab = &tb->Tabs[t];
                                 ImRect tabRect{
                                     {tb->BarRect.Min.x + tab->Offset, tb->BarRect.Min.y},
                                     {tb->BarRect.Min.x + tab->Offset + tab->Width, tb->BarRect.Max.y}};
                                 if (!tabRect.Contains(pos))
                                     continue;
                                 ImGui::TabBarQueueFocus(tb, tab);
                                 const char *name = ImGui::TabBarGetTabName(tb, tab);
                                 tabHit = name ? name : "";
                                 if (auto p = tabHit.find("##"); p != std::string::npos)
                                     tabHit = tabHit.substr(0, p);
                                 handledByTabBar = true;
                                 break;
                             }
                             if (handledByTabBar)
                                 break;
                         }
                     }

                     // Fall back to IO injection for non-tab clicks.
                     // Press and release must land in SEPARATE frames: if both are queued in
                     // the same FlushActions(), they end up in the same NewFrame() and
                     // MouseDown ends at false → MouseClicked is never set.
                     // Solution: push only the press now, then queue the release for next frame.
                     if (!handledByTabBar)
                     {
                         io.AddMousePosEvent(pos.x, pos.y);
                         if (action == "scroll")
                         {
                             io.AddMouseWheelEvent(static_cast<float>(scrollX), static_cast<float>(scrollY));
                         }
                         else
                         {
                             int btn = (action == "right_click") ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
                             io.AddMouseButtonEvent(btn, true); // press this frame
                             // Release next frame so NewFrame() sees a real click transition
                             QueueAction([btn]()
                             {
                                 ImGui::GetIO().AddMouseButtonEvent(btn, false);
                             });
                         }
                     }

                     {
                         std::lock_guard lock(mtx);
                         done = true;
                     }
                     cv.notify_one(); });

                 std::unique_lock lock(mtx);
                 if (!cv.wait_for(lock, std::chrono::seconds(5), [&]
                                  { return done; }))
                     return "{\"error\":\"timeout waiting for main thread\"}";

                 nlohmann::json res{{"ok", true}, {"x", x}, {"y", y}, {"action", action}};
                 if (!tabHit.empty())
                     res["tab_focused"] = tabHit;
                 return res.dump();
             }});
    }

} // namespace pe
