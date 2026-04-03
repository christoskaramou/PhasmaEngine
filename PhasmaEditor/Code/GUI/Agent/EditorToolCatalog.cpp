#include "GUI/Agent/EditorToolCatalog.h"
#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/GUI.h"
#include "GUI/Widgets/Console.h"
#include "Base/Path.h"
#include "PhasmaAgent/AgentUtils.h"
#include "PhasmaAgent/BM25Index.h"

using namespace pagent;

namespace pe
{
    namespace
    {
        static std::string DumpJson(const nlohmann::json &json)
        {
            return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        static std::string ToLower(std::string value)
        {
            for (auto &c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        static bool ExtractBoolArg(const std::string &args, const char *key, bool defaultValue)
        {
            auto json = ParseArgs(args);
            if (!json.contains(key))
                return defaultValue;
            if (json[key].is_boolean())
                return json[key].get<bool>();
            if (json[key].is_string())
            {
                const std::string value = ToLower(json[key].get<std::string>());
                if (value == "true")
                    return true;
                if (value == "false")
                    return false;
            }
            return defaultValue;
        }

        static std::string NormalizeRelativePath(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            std::string relative = std::filesystem::relative(path, root).string();
            std::replace(relative.begin(), relative.end(), '\\', '/');
            return relative;
        }

        static void AppendProjectFileTools(std::vector<pagent::ToolDefinition> &tools, const std::string &projectRoot);
        static void AppendWorkspaceTools(std::vector<pagent::ToolDefinition> &tools, EditorToolRuntime *runtime, const std::function<void(const std::string &)> &onFeatureRequest);
        static void AppendCodebaseManagementTools(std::vector<pagent::ToolDefinition> &tools, GUI *gui);
        static void AppendCodebaseTools(std::vector<pagent::ToolDefinition> &tools,
                                        const std::shared_ptr<pagent::BM25Index> &codebaseBM25);
        static void AppendVisualTools(std::vector<pagent::ToolDefinition> &tools, EditorToolRuntime *runtime, GUI *gui);
    } // namespace

    std::filesystem::path GetEditorRepoRootFromAssets()
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

        return assetsPath.parent_path().parent_path();
    }

    std::vector<pagent::ToolDefinition> BuildEditorToolDefinitions(const EditorToolCatalogContext &context)
    {
        std::vector<pagent::ToolDefinition> tools;

        std::string projectRoot = context.projectRoot.empty() ? GetEditorRepoRootFromAssets().string() : context.projectRoot;
        if (!projectRoot.empty() && projectRoot.back() != '/')
            projectRoot += '/';

        AppendProjectFileTools(tools, projectRoot);
        AppendWorkspaceTools(tools, context.runtime, context.onFeatureRequest);
        AppendCodebaseManagementTools(tools, context.gui);
        AppendCodebaseTools(tools, context.codebaseBM25);
        AppendVisualTools(tools, context.runtime, context.gui);
        return tools;
    }

    namespace
    {
        void AppendProjectFileTools(std::vector<pagent::ToolDefinition> &tools, const std::string &projectRoot)
        {
            tools.push_back({.name = "read_project_file",
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

                                 std::filesystem::path filePath(path);
                                 if (filePath.is_relative())
                                     filePath = std::filesystem::path(projectRoot) / filePath;

                                 if (!IsPathSafe(filePath.string(), projectRoot))
                                     return "{\"error\":\"path outside project directory\"}";
                                 if (!std::filesystem::exists(filePath))
                                     return JsonObj({{"error", JsonStr("file not found: " + filePath.string())}});
                                 if (std::filesystem::is_directory(filePath))
                                     return "{\"error\":\"path is a directory, use find_project_file or list_project_dir\"}";

                                 int64_t startLine = ExtractArgInt(args, "start_line", 1);
                                 int64_t endLine = ExtractArgInt(args, "end_line", 0);
                                 if (startLine < 1)
                                     startLine = 1;

                                 std::ifstream file(filePath);
                                 if (!file.is_open())
                                     return JsonObj({{"error", JsonStr("cannot open: " + filePath.string())}});

                                 std::string content;
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
                                 while (std::getline(file, line))
                                     ++totalLines;

                                 return DumpJson(nlohmann::json{
                                     {"path", NormalizeRelativePath(filePath, projectRoot)},
                                     {"content", content},
                                     {"start_line", startLine},
                                     {"end_line", endLine > 0 ? std::min(endLine, lineNum) : lineNum},
                                     {"total_lines", totalLines},
                                 });
                             }});

            tools.push_back({.name = "write_project_file",
                             .description = "Writes content to a file in PhasmaEditor/Code/ or PhasmaEditor/Assets/. "
                                            "Use this to modify C++ source, headers, shaders, Lua scripts, or config files. "
                                            "Creates parent directories automatically. "
                                            "Path relative to project root or absolute.",
                             .properties = {
                                 {"path", "File path relative to project root (e.g. 'PhasmaEditor/Code/App/App.cpp')", pagent::SchemaType::String, true},
                                 {"content", "Text content to write", pagent::SchemaType::String, true},
                                 {"append", "Append instead of overwriting when true", pagent::SchemaType::Boolean, false},
                             },
                             .handler = [projectRoot](const std::string &args) -> std::string
                             {
                                 std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                 std::string content = JsonUnescape(ExtractArgStr(args, "content"));
                                 bool appendMode = ExtractBoolArg(args, "append", false);
                                 if (path.empty())
                                     return "{\"error\":\"missing path\"}";
                                 if (content.empty())
                                     return "{\"error\":\"missing content\"}";

                                 std::filesystem::path filePath(path);
                                 if (filePath.is_relative())
                                     filePath = std::filesystem::path(projectRoot) / filePath;

                                 if (!IsPathSafe(filePath.string(), projectRoot))
                                     return "{\"error\":\"path outside project directory\"}";

                                 std::filesystem::path parentDir = filePath.parent_path();
                                 if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                                 {
                                     std::error_code ec;
                                     std::filesystem::create_directories(parentDir, ec);
                                     if (ec)
                                         return JsonObj({{"error", JsonStr("cannot create directory: " + ec.message())}});
                                 }

                                 auto flags = std::ios::out;
                                 flags |= appendMode ? std::ios::app : std::ios::trunc;

                                 std::ofstream file(filePath, flags);
                                 if (!file.is_open())
                                     return JsonObj({{"error", JsonStr("cannot write: " + filePath.string())}});
                                 file << content;
                                 file.flush();
                                 if (!file.good())
                                     return JsonObj({{"error", JsonStr("write failed (disk full or I/O error): " + filePath.string())}});

                                 return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(filePath.string())}});
                             }});

            tools.push_back({.name = "find_project_file",
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

                                 std::filesystem::path searchPath = dir.empty() ? std::filesystem::path(projectRoot) : std::filesystem::path(projectRoot) / dir;
                                 if (!IsPathSafe(searchPath.string(), projectRoot))
                                     return "{\"error\":\"path outside project directory\"}";
                                 if (!std::filesystem::exists(searchPath))
                                     return JsonObj({{"error", JsonStr("directory not found: " + searchPath.string())}});

                                 const std::string queryLower = ToLower(query);
                                 std::string files = "[";
                                 bool first = true;
                                 int count = 0;
                                 for (auto it = std::filesystem::recursive_directory_iterator(
                                          searchPath, std::filesystem::directory_options::skip_permission_denied);
                                      it != std::filesystem::recursive_directory_iterator(); ++it)
                                 {
                                     if (it->is_directory())
                                     {
                                         const std::string dirName = it->path().filename().string();
                                         if (dirName == "build" || dirName == "third_party" ||
                                             (!dirName.empty() && dirName[0] == '.'))
                                             it.disable_recursion_pending();
                                         continue;
                                     }
                                     if (!it->is_regular_file())
                                         continue;
                                     const auto &entry = *it;

                                     std::string pathStr = entry.path().string();
                                     std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                     if (ToLower(entry.path().filename().string()).find(queryLower) == std::string::npos)
                                         continue;

                                     if (!first)
                                         files += ",";
                                     files += JsonStr(NormalizeRelativePath(entry.path(), projectRoot));
                                     first = false;
                                     if (++count >= 30)
                                         break;
                                 }
                                 files += "]";
                                 return JsonObj({{"count", std::to_string(count)}, {"files", files}});
                             }});

            tools.push_back({.name = "grep_project",
                             .description = "Search for a literal string or regex pattern in project source files. "
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
                                 bool useRegex = ExtractBoolArg(args, "regex", false);
                                 bool caseSensitive = ExtractBoolArg(args, "case_sensitive", true);
                                 int maxResults = static_cast<int>(ExtractArgInt(args, "max_results", 50));

                                 if (pattern.empty())
                                     return "{\"error\":\"missing pattern\"}";
                                 if (maxResults <= 0 || maxResults > 500)
                                     maxResults = 50;

                                 std::filesystem::path searchPath = searchDir.empty() ? std::filesystem::path(projectRoot) : std::filesystem::path(projectRoot) / searchDir;
                                 if (!IsPathSafe(searchPath.string(), projectRoot))
                                     return "{\"error\":\"path outside project directory\"}";
                                 if (!std::filesystem::exists(searchPath))
                                     return JsonObj({{"error", JsonStr("directory not found: " + searchPath.string())}});

                                 std::string extFilter;
                                 if (!globFilter.empty())
                                 {
                                     auto star = globFilter.find('*');
                                     extFilter = (star != std::string::npos) ? globFilter.substr(star + 1) : globFilter;
                                     if (!caseSensitive)
                                         extFilter = ToLower(extFilter);
                                 }

                                 std::regex regex;
                                 bool regexValid = false;
                                 if (useRegex)
                                 {
                                     try
                                     {
                                         auto flags = std::regex::ECMAScript | std::regex::optimize;
                                         if (!caseSensitive)
                                             flags |= std::regex::icase;
                                         regex = std::regex(pattern, flags);
                                         regexValid = true;
                                     }
                                     catch (const std::regex_error &error)
                                     {
                                         return JsonObj({{"error", JsonStr(std::string("invalid regex: ") + error.what())}});
                                     }
                                 }

                                 const std::string literalLower = caseSensitive ? pattern : ToLower(pattern);
                                 nlohmann::json matches = nlohmann::json::array();
                                 int count = 0;

                                 auto processFile = [&](const std::filesystem::path &filePath) -> bool
                                 {
                                     std::ifstream file(filePath, std::ios::binary);
                                     if (!file.is_open())
                                         return true;

                                     char buffer[1024];
                                     const std::streamsize bytesRead = (file.read(buffer, sizeof(buffer)), file.gcount());
                                     if (std::find(buffer, buffer + bytesRead, '\0') != buffer + bytesRead)
                                         return true;
                                     file.clear();
                                     file.seekg(0);

                                     std::string line;
                                     int lineNum = 0;
                                     bool firstLine = true;
                                     while (std::getline(file, line))
                                     {
                                         ++lineNum;
                                         if (firstLine)
                                         {
                                             if (line.size() >= 3 &&
                                                 static_cast<unsigned char>(line[0]) == 0xEF &&
                                                 static_cast<unsigned char>(line[1]) == 0xBB &&
                                                 static_cast<unsigned char>(line[2]) == 0xBF)
                                                 line.erase(0, 3);
                                             firstLine = false;
                                         }

                                         if (!line.empty() && line.back() == '\r')
                                             line.pop_back();

                                         bool matched = false;
                                         if (useRegex && regexValid)
                                             matched = std::regex_search(line, regex);
                                         else
                                             matched = (caseSensitive ? line : ToLower(line)).find(literalLower) != std::string::npos;

                                         if (!matched)
                                             continue;

                                         std::string trimmed = line;
                                         auto ws = trimmed.find_first_not_of(" \t");
                                         if (ws != std::string::npos)
                                             trimmed = trimmed.substr(ws);

                                         matches.push_back({{"file", NormalizeRelativePath(filePath, projectRoot)}, {"line", lineNum}, {"text", trimmed}});
                                         if (++count >= maxResults)
                                             return false;
                                     }

                                     return true;
                                 };

                                 auto matchesExtension = [&](const std::filesystem::path &path) -> bool
                                 {
                                     if (extFilter.empty())
                                         return true;
                                     std::string ext = path.extension().string();
                                     if (!caseSensitive)
                                         ext = ToLower(ext);
                                     if (extFilter[0] == '.')
                                         return ext == extFilter;
                                     return ext == ("." + extFilter);
                                 };

                                 bool keepGoing = true;
                                 auto iter = std::filesystem::recursive_directory_iterator(searchPath, std::filesystem::directory_options::skip_permission_denied);
                                 const auto end = std::filesystem::recursive_directory_iterator();
                                 for (; iter != end && keepGoing; ++iter)
                                 {
                                     if (iter->is_directory())
                                     {
                                         const std::string dirName = iter->path().filename().string();
                                         if (dirName == "build" || (!dirName.empty() && dirName[0] == '.'))
                                             iter.disable_recursion_pending();
                                         continue;
                                     }
                                     if (!iter->is_regular_file() || !matchesExtension(iter->path()))
                                         continue;
                                     keepGoing = processFile(iter->path());
                                 }

                                 return DumpJson(nlohmann::json{{"count", count}, {"matches", matches}});
                             }});

            tools.push_back({.name = "list_project_dir",
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

                                 std::string files = "[";
                                 std::string dirs = "[";
                                 bool firstFile = true;
                                 bool firstDir = true;
                                 for (const auto &entry : std::filesystem::directory_iterator(dirPath))
                                 {
                                     const std::string name = entry.path().filename().string();
                                     if (name.empty() || name[0] == '.')
                                         continue;

                                     if (entry.is_directory())
                                     {
                                         if (!firstDir)
                                             dirs += ",";
                                         dirs += JsonStr(name + "/");
                                         firstDir = false;
                                     }
                                     else
                                     {
                                         if (!firstFile)
                                             files += ",";
                                         files += JsonStr(name);
                                         firstFile = false;
                                     }
                                 }
                                 files += "]";
                                 dirs += "]";
                                 return JsonObj({{"path", JsonStr(dirPath.string())}, {"files", files}, {"dirs", dirs}});
                             }});

            tools.push_back({.name = "patch_project_file",
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

                                 std::filesystem::path filePath(path);
                                 if (filePath.is_relative())
                                     filePath = std::filesystem::path(projectRoot) / filePath;

                                 if (!IsPathSafe(filePath.string(), projectRoot))
                                     return "{\"error\":\"path outside project directory\"}";
                                 if (!std::filesystem::exists(filePath))
                                     return JsonObj({{"error", JsonStr("file not found: " + filePath.string())}});

                                 std::vector<std::string> lines;
                                 bool trailingNewline = false;
                                 bool useCRLF = false;
                                 {
                                     std::ifstream file(filePath, std::ios::binary);
                                     if (!file.is_open())
                                         return JsonObj({{"error", JsonStr("cannot open: " + filePath.string())}});

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

                                     if (!lines.empty())
                                     {
                                         file.clear();
                                         file.seekg(-1, std::ios::end);
                                         if (file.good())
                                         {
                                             char last = 0;
                                             file.get(last);
                                             trailingNewline = (last == '\n');
                                         }
                                     }
                                 }

                                 const int64_t totalLines = static_cast<int64_t>(lines.size());
                                 // Allow startLine == totalLines + 1 only when appending (endLine == startLine - 1 is impossible
                                 // due to the earlier guard, but endLine == totalLines is valid for empty files).
                                 // Simplified: both must be within [1, totalLines] OR the file is empty and we're inserting at line 1.
                                 if (startLine > totalLines + 1 || (endLine > totalLines && totalLines > 0) ||
                                     (totalLines == 0 && endLine > 0))
                                     return JsonObj({{"error", JsonStr("line range out of bounds: file has " + std::to_string(totalLines) + " lines")}});

                                 if (!expectedContext.empty())
                                 {
                                     // Strip \r so agents sending Windows line endings don't cause spurious mismatches
                                     std::string ctx = expectedContext;
                                     ctx.erase(std::remove(ctx.begin(), ctx.end(), '\r'), ctx.end());
                                     std::string region;
                                     for (int64_t i = startLine - 1; i < endLine && i < totalLines; ++i)
                                         region += lines[static_cast<size_t>(i)] + "\n";
                                     if (region.find(ctx) == std::string::npos)
                                         return "{\"error\":\"expected_context not found in target region — patch rejected to prevent wrong-offset edit\"}";
                                 }

                                 std::vector<std::string> replacementLines;
                                 if (!replacement.empty())
                                 {
                                     std::istringstream stream(replacement);
                                     std::string line;
                                     while (std::getline(stream, line))
                                     {
                                         // Strip \r so agents sending \r\n don't produce double-CR in CRLF files
                                         if (!line.empty() && line.back() == '\r')
                                             line.pop_back();
                                         replacementLines.push_back(std::move(line));
                                     }
                                     if (!replacementLines.empty() && replacementLines.back().empty() && replacement.back() == '\n')
                                         replacementLines.pop_back();
                                 }

                                 std::vector<std::string> result;
                                 result.reserve(static_cast<size_t>(totalLines - (endLine - startLine + 1)) + replacementLines.size());
                                 for (int64_t i = 0; i < startLine - 1; ++i)
                                     result.push_back(lines[static_cast<size_t>(i)]);
                                 for (auto &line : replacementLines)
                                     result.push_back(line);
                                 for (int64_t i = endLine; i < totalLines; ++i)
                                     result.push_back(lines[static_cast<size_t>(i)]);

                                 std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
                                 if (!file.is_open())
                                     return JsonObj({{"error", JsonStr("cannot write: " + filePath.string())}});

                                 const std::string lineEnding = useCRLF ? "\r\n" : "\n";
                                 for (size_t i = 0; i < result.size(); ++i)
                                 {
                                     file << result[i];
                                     if (i + 1 < result.size() || trailingNewline)
                                         file << lineEnding;
                                 }

                                 return DumpJson(nlohmann::json{
                                     {"status", "patched"},
                                     {"path", NormalizeRelativePath(filePath, projectRoot)},
                                     {"lines_replaced", endLine - startLine + 1},
                                     {"lines_inserted", static_cast<int64_t>(replacementLines.size())},
                                     {"new_total_lines", static_cast<int64_t>(result.size())},
                                 });
                             }});
        }

        void AppendWorkspaceTools(std::vector<pagent::ToolDefinition> &tools, EditorToolRuntime *runtime, const std::function<void(const std::string &)> &onFeatureRequest)
        {
            tools.push_back({.name = "execute_lua",
                             .description = "Executes Lua code in the engine's ScriptSystem. Use this for ALL scene manipulation: "
                                            "models, camera, lights, materials, settings, shaders, particles, skybox, scene save/load. "
                                            "The full Lua API is documented in the system prompt. "
                                            "Use pe_log() to output information. Returns captured output or 'ok' on success.",
                             .properties = {
                                 {"code", "Lua code to execute", pagent::SchemaType::String, true},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 std::string code = JsonUnescape(ExtractArgStr(args, "code"));
                                 return runtime ? runtime->ExecuteLua(code)
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            // --- High-level scene authoring tools (C++ implementations, no Lua) ---

            tools.push_back({.name = "create_node",
                             .description = "Creates an empty scene node. Returns stable id (use in other tools). "
                                            "Parent can be a name or stable id (node:index:revision).",
                             .properties = {
                                 {"name", "Node name", pagent::SchemaType::String, true},
                                 {"parent", "Parent node name or id (omit for root)", pagent::SchemaType::String, false},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 auto json = ParseArgs(args);
                                 return runtime->CreateNode(json.value("name", "Node"), json.value("parent", ""));
                             }});

            tools.push_back({.name = "set_node_transform",
                             .description = "Sets position, rotation (degrees), and/or scale on a node. "
                                            "Node can be a name or stable id (node:index:revision).",
                             .properties = {
                                 {"node", "Node name or id", pagent::SchemaType::String, true},
                                 {"position", "Position as [x,y,z]", pagent::SchemaType::Array, false, pagent::SchemaType::Number},
                                 {"rotation", "Rotation in degrees as [x,y,z]", pagent::SchemaType::Array, false, pagent::SchemaType::Number},
                                 {"scale", "Scale as [x,y,z]", pagent::SchemaType::Array, false, pagent::SchemaType::Number},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 auto json = ParseArgs(args);
                                 std::string node = json.value("node", "");
                                 if (node.empty())
                                     return R"({"error":"node required"})";
                                 float pos[3], rot[3], scale[3];
                                 float *pPos = nullptr, *pRot = nullptr, *pScale = nullptr;
                                 auto readVec3 = [](const nlohmann::json &arr, float out[3]) -> bool {
                                     if (!arr.is_array() || arr.size() != 3) return false;
                                     out[0] = arr[0].get<float>(); out[1] = arr[1].get<float>(); out[2] = arr[2].get<float>();
                                     return true;
                                 };
                                 if (json.contains("position") && readVec3(json["position"], pos)) pPos = pos;
                                 if (json.contains("rotation") && readVec3(json["rotation"], rot)) pRot = rot;
                                 if (json.contains("scale") && readVec3(json["scale"], scale)) pScale = scale;
                                 return runtime->SetNodeTransform(node, pPos, pRot, pScale);
                             }});

            tools.push_back({.name = "add_mesh_to_node",
                             .description = "Attaches a primitive mesh to a node (synchronous). "
                                            "Node can be a name or stable id (node:index:revision). "
                                            "Valid primitives: cube, sphere, plane, cylinder, cone, quad.",
                             .properties = {
                                 {"node", "Node name or id", pagent::SchemaType::String, true},
                                 {"primitive", "Primitive type", pagent::SchemaType::String, true},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 auto json = ParseArgs(args);
                                 return runtime->AddMeshToNode(json.value("node", ""), json.value("primitive", ""));
                             }});

            tools.push_back({.name = "set_node_material",
                             .description = "Sets material properties on a node's mesh. "
                                            "Node can be a name or stable id (node:index:revision).",
                             .properties = {
                                 {"node", "Node name or id", pagent::SchemaType::String, true},
                                 {"slot", "Mesh slot index (default 0)", pagent::SchemaType::Integer, false},
                                 {"base_color", "Base color as [r,g,b,a] (0-1)", pagent::SchemaType::Array, false, pagent::SchemaType::Number},
                                 {"metallic", "Metallic factor (0-1)", pagent::SchemaType::Number, false},
                                 {"roughness", "Roughness factor (0-1)", pagent::SchemaType::Number, false},
                                 {"transmission", "Transmission factor (0-1)", pagent::SchemaType::Number, false},
                                 {"render_type", "Render type: opaque, alpha_cut, alpha_blend, transmission", pagent::SchemaType::String, false},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 auto json = ParseArgs(args);
                                 std::string node = json.value("node", "");
                                 if (node.empty())
                                     return R"({"error":"node required"})";
                                 int slot = json.value("slot", 0);
                                 // Build props sub-object for the runtime
                                 nlohmann::json props;
                                 for (auto &key : {"base_color", "metallic", "roughness", "transmission", "render_type"})
                                     if (json.contains(key)) props[key] = json[key];
                                 return runtime->SetNodeMaterial(node, slot, props.dump());
                             }});

            tools.push_back({.name = "add_light",
                             .description = "Adds a light to the scene (synchronous). "
                                            "Valid types: directional, point, spot, area.",
                             .properties = {
                                 {"type", "Light type", pagent::SchemaType::String, true},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 auto json = ParseArgs(args);
                                 return runtime->AddLight(json.value("type", ""));
                             }});

            tools.push_back({.name = "query_scene",
                             .description = "Returns scene tree with stable node ids, mesh counts, cameras. "
                                            "Use the id field in other tools to address nodes unambiguously.",
                             .properties = {},
                             .handler = [runtime](const std::string &) -> std::string
                             {
                                 return runtime->QueryScene();
                             }});

            tools.push_back({.name = "find_loadable_model",
                             .description = "Searches for 3D model files (.glb, .gltf, .obj, .fbx) in Assets/Objects/ by name. "
                                            "Returns paths ready to use with load_model(). "
                                            "Example: query 'helmet' finds 'DamagedHelmet/glTF-Binary/DamagedHelmet.glb'.",
                             .properties = {
                                 {"query", "Model name to search for (e.g. 'helmet', 'avocado', 'sponza')", pagent::SchemaType::String, true},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 std::string query = JsonUnescape(ExtractArgStr(args, "query"));
                                 return runtime ? runtime->FindLoadableModel(query)
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "read_agent_file",
                             .description = "Reads a text file from the agent workspace (Assets/Agent/). "
                                            "Use this to read START.md, MEMORY.md, TASKS.md, PROGRESSION.md, or any workspace file.",
                             .properties = {
                                 {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", pagent::SchemaType::String, true},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                 return runtime ? runtime->ReadAgentFile(path)
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "write_agent_file",
                             .description = "Writes or appends to a text file in the agent workspace (Assets/Agent/). "
                                            "Use for MEMORY.md, TASKS.md, PROGRESSION.md, Lua scripts, or any persistent notes. "
                                            "Creates parent directories automatically.",
                             .properties = {
                                 {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", pagent::SchemaType::String, true},
                                 {"content", "Text content to write", pagent::SchemaType::String, true},
                                 {"append", "Append instead of overwriting when true", pagent::SchemaType::Boolean, false},
                             },
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 std::string path = JsonUnescape(ExtractArgStr(args, "path"));
                                 std::string content = JsonUnescape(ExtractArgStr(args, "content"));
                                 bool append = ExtractBoolArg(args, "append", false);
                                 return runtime ? runtime->WriteAgentFile(path, content, append)
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            const std::string requestsPath = Path::Assets + "Agent/REQUESTS.md";

            static std::mutex s_requestsMutex;

            tools.push_back({.name = "request_feature",
                             .description = "Request a new feature, tool, or Lua binding that is not currently available. "
                                            "The request will be shown to the user and saved to REQUESTS.md. "
                                            "Use this when you need a capability that doesn't exist yet.",
                             .properties = {
                                 {"title", "Short title for the feature request", pagent::SchemaType::String, true},
                                 {"description", "Detailed description of what is needed and why", pagent::SchemaType::String, true},
                             },
                             .handler = [onFeatureRequest, requestsPath](const std::string &args) -> std::string
                             {
                                 std::string title = JsonUnescape(ExtractArgStr(args, "title"));
                                 std::string description = JsonUnescape(ExtractArgStr(args, "description"));
                                 if (title.empty())
                                     return "{\"error\":\"missing title\"}";

                                 const std::string message = "[FEATURE REQUEST] " + title + ": " + description;
                                 PE_WARN("%s", message.c_str());
                                 if (onFeatureRequest)
                                     onFeatureRequest(message);

                                 std::filesystem::path parentDir = std::filesystem::path(requestsPath).parent_path();
                                 if (!std::filesystem::exists(parentDir))
                                 {
                                     std::error_code ec;
                                     std::filesystem::create_directories(parentDir, ec);
                                 }

                                 std::lock_guard lock(s_requestsMutex);
                                 std::ofstream file(requestsPath, std::ios::app);
                                 if (file.is_open())
                                     file << "- **" << title << "**: " << description << "\n\n";

                                 return JsonObj({{"status", JsonStr("request saved to REQUESTS.md")}, {"title", JsonStr(title)}});
                             }});

            tools.push_back({.name = "complete_feature",
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

                                 std::lock_guard lock(s_requestsMutex);

                                 if (!std::filesystem::exists(requestsPath))
                                     return "{\"error\":\"REQUESTS.md not found\"}";

                                 std::string content;
                                 {
                                     std::ifstream file(requestsPath, std::ios::in);
                                     if (!file.is_open())
                                         return "{\"error\":\"cannot open REQUESTS.md\"}";
                                     content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                                 }

                                 const std::string marker = "- **" + title + "**";
                                 const auto pos = content.find(marker);
                                 if (pos == std::string::npos)
                                     return JsonObj({{"error", JsonStr("request not found: " + title)}});

                                 auto entryEnd = content.find("\n- **", pos + marker.size());
                                 if (entryEnd == std::string::npos)
                                     entryEnd = content.size();
                                 else
                                     entryEnd += 1;

                                 content.erase(pos, entryEnd - pos);

                                 // Collapse any triple-newlines left by the removal into double-newlines
                                 static const std::string tripleNL = "\n\n\n";
                                 static const std::string doubleNL = "\n\n";
                                 size_t nlPos;
                                 while ((nlPos = content.find(tripleNL)) != std::string::npos)
                                     content.replace(nlPos, tripleNL.size(), doubleNL);

                                 std::ofstream file(requestsPath, std::ios::out | std::ios::trunc);
                                 if (!file.is_open())
                                     return "{\"error\":\"cannot write REQUESTS.md\"}";
                                 file << content;

                                 return JsonObj({{"status", JsonStr("removed")}, {"title", JsonStr(title)}});
                             }});
        }

        void AppendCodebaseTools(std::vector<pagent::ToolDefinition> &tools,
                                 const std::shared_ptr<pagent::BM25Index> &codebaseBM25)
        {
            tools.push_back({.name = "find_symbol",
                             .description = "Searches the codebase index for a single class, function, method, or struct by name. "
                                            "Returns file path and exact line range — use with read_project_file(start_line, end_line). "
                                            "Far more efficient than grep_project for symbol lookup. "
                                            "Requires the codebase index to be built. "
                                            "Tip: when you need to look up several symbols at once, use search_codebase instead.",
                             .properties = {
                                 {"name", "Symbol name to search for (e.g. 'CommandBuffer', 'CreatePipeline', 'RenderGraph')", pagent::SchemaType::String, true},
                                 {"max_results", "Maximum matches to return, 1-20 (default: 5)", pagent::SchemaType::Integer, false},
                             },
                             .handler = [codebaseBM25](const std::string &args) -> std::string
                             {
                                 std::string symbolName = JsonUnescape(ExtractArgStr(args, "name"));
                                 if (symbolName.empty())
                                     return "{\"error\":\"missing name\"}";

                                 int64_t maxResults = ExtractArgInt(args, "max_results", 5);
                                 if (maxResults < 1)
                                     maxResults = 1;
                                 if (maxResults > 20)
                                     maxResults = 20;

                                 if (!codebaseBM25 || codebaseBM25->Size() == 0)
                                     return "{\"error\":\"codebase index not built\"}";

                                 auto results = codebaseBM25->Search(symbolName, static_cast<int>(maxResults) * 6);
                                 if (results.empty())
                                     return JsonObj({{"error", JsonStr("no symbols found matching: " + symbolName)}});

                                 // Parse file/lines from chunk header: "// File: path (lines X-Y) | ..."
                                 auto parseHeader = [](const std::string &content, std::string &file, std::string &lines)
                                 {
                                     constexpr std::string_view prefix = "// File: ";
                                     if (content.compare(0, prefix.size(), prefix) != 0)
                                         return false;
                                     auto start = prefix.size();
                                     auto paren = content.find(" (lines ", start);
                                     if (paren == std::string::npos)
                                         return false;
                                     file = content.substr(start, paren - start);
                                     auto linesStart = paren + 8; // len(" (lines ")
                                     auto linesEnd = content.find(')', linesStart);
                                     if (linesEnd == std::string::npos)
                                         return false;
                                     lines = content.substr(linesStart, linesEnd - linesStart);
                                     return true;
                                 };

                                 const std::string nameLower = ToLower(symbolName);
                                 auto definitionPriority = [&](const std::string &file, const std::string &header) -> int
                                 {
                                     const std::string fileLower = ToLower(file);
                                     const std::string headerLower = ToLower(header);
                                     auto slash = fileLower.rfind('/');
                                     if (slash == std::string::npos)
                                         slash = fileLower.rfind('\\');
                                     std::string stem = (slash != std::string::npos) ? fileLower.substr(slash + 1) : fileLower;
                                     auto dot = stem.rfind('.');
                                     if (dot != std::string::npos)
                                         stem = stem.substr(0, dot);
                                     if (stem == nameLower)
                                         return 3;
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

                                 struct Candidate
                                 {
                                     std::string content;
                                     std::string file;
                                     std::string lines;
                                     int priority;
                                 };

                                 std::vector<Candidate> candidates;
                                 candidates.reserve(results.size());
                                 for (auto &result : results)
                                 {
                                     std::string file, lines;
                                     if (!parseHeader(result.content, file, lines))
                                         continue;
                                     std::string header;
                                     if (auto nl = result.content.find('\n'); nl != std::string::npos)
                                         header = result.content.substr(0, nl);
                                     else
                                         header = result.content.substr(0, std::min(result.content.size(), static_cast<size_t>(200)));
                                     candidates.push_back({result.content, file, lines, definitionPriority(file, header)});
                                 }

                                 std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b)
                                                  { return a.priority > b.priority; });

                                 nlohmann::json matches = nlohmann::json::array();
                                 std::set<std::string> seen;
                                 for (auto &candidate : candidates)
                                 {
                                     if (static_cast<int64_t>(matches.size()) >= maxResults)
                                         break;

                                     const std::string key = candidate.file + ":" + candidate.lines;
                                     if (!seen.insert(key).second)
                                         continue;

                                     int startLine = 0;
                                     int endLine = 0;
                                     if (auto dash = candidate.lines.find('-'); dash != std::string::npos)
                                     {
                                         try
                                         {
                                             startLine = std::stoi(candidate.lines.substr(0, dash));
                                             endLine = std::stoi(candidate.lines.substr(dash + 1));
                                         }
                                         catch (...)
                                         {
                                         }
                                     }

                                     std::string header;
                                     if (auto nl = candidate.content.find('\n'); nl != std::string::npos)
                                         header = candidate.content.substr(0, nl);
                                     else
                                         header = candidate.content;

                                     auto extractField = [&](const std::string &tag) -> std::string
                                     {
                                         auto pos = header.find(tag);
                                         if (pos == std::string::npos)
                                             return "";
                                         pos += tag.size();
                                         auto end = header.find(" |", pos);
                                         return end != std::string::npos ? header.substr(pos, end - pos) : header.substr(pos);
                                     };

                                     nlohmann::json match = {{"file", candidate.file}, {"start_line", startLine}, {"end_line", endLine}};
                                     const std::string ns = extractField("Namespace: ");
                                     const std::string cls = extractField("Class: ");
                                     const std::string method = extractField("Method: ");
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
                                 return DumpJson(nlohmann::json{{"matches", matches}});
                             }});

            tools.push_back({.name = "search_codebase",
                             .description = "Search the indexed codebase for multiple symbols or concepts simultaneously. "
                                            "Use this instead of calling find_symbol multiple times — pass all related names "
                                            "in one call to save round-trips. Requires the codebase index to be built. "
                                            "Example: search for 'RenderGraph', 'AddPass', and 'IRenderPass' all at once.",
                             .properties = {
                                 {"queries", "Array of symbol names or concepts to search simultaneously (e.g. [\"RenderGraph\", \"AddPass\"])", pagent::SchemaType::Array, true, pagent::SchemaType::String},
                                 {"max_results", "Maximum total matches to return, 1-30 (default: 10)", pagent::SchemaType::Integer, false},
                             },
                             .handler = [codebaseBM25](const std::string &args) -> std::string
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

                                 if (!codebaseBM25 || codebaseBM25->Size() == 0)
                                     return "{\"error\":\"codebase index not built\"}";

                                 auto bm25Results = codebaseBM25->SearchMulti(queries, static_cast<int>(maxResults) * 6);

                                 // Parse file/lines from chunk header: "// File: path (lines X-Y) | ..."
                                 auto parseHeader = [](const std::string &content, std::string &file, std::string &lines)
                                 {
                                     constexpr std::string_view prefix = "// File: ";
                                     if (content.compare(0, prefix.size(), prefix) != 0)
                                         return false;
                                     auto start = prefix.size();
                                     auto paren = content.find(" (lines ", start);
                                     if (paren == std::string::npos)
                                         return false;
                                     file = content.substr(start, paren - start);
                                     auto linesStart = paren + 8;
                                     auto linesEnd = content.find(')', linesStart);
                                     if (linesEnd == std::string::npos)
                                         return false;
                                     lines = content.substr(linesStart, linesEnd - linesStart);
                                     return true;
                                 };

                                 std::vector<std::string> queriesLower;
                                 queriesLower.reserve(queries.size());
                                 for (const auto &query : queries)
                                     queriesLower.push_back(ToLower(query));

                                 auto definitionPriority = [&](const std::string &file, const std::string &header) -> int
                                 {
                                     std::string fileLower = ToLower(file);
                                     std::string headerLower = ToLower(header);
                                     auto slash = fileLower.rfind('/');
                                     if (slash == std::string::npos)
                                         slash = fileLower.rfind('\\');
                                     std::string stem = (slash != std::string::npos) ? fileLower.substr(slash + 1) : fileLower;
                                     auto dot = stem.rfind('.');
                                     if (dot != std::string::npos)
                                         stem = stem.substr(0, dot);
                                     for (const auto &queryLower : queriesLower)
                                     {
                                         if (stem == queryLower)
                                             return 3;
                                         for (const char *tag : {"class: ", "method: ", "struct: "})
                                         {
                                             auto pos = headerLower.find(tag);
                                             if (pos != std::string::npos &&
                                                 headerLower.substr(pos + strlen(tag), queryLower.size()) == queryLower)
                                                 return 2;
                                         }
                                         if (fileLower.find(queryLower) != std::string::npos)
                                             return 1;
                                     }
                                     return 0;
                                 };

                                 struct ScoredEntry
                                 {
                                     std::string content;
                                     std::string file;
                                     std::string lines;
                                     float score;
                                     int priority;
                                 };

                                 std::vector<ScoredEntry> candidates;
                                 candidates.reserve(bm25Results.size());
                                 for (auto &result : bm25Results)
                                 {
                                     std::string file, lines;
                                     if (!parseHeader(result.content, file, lines))
                                         continue;
                                     std::string header;
                                     if (auto nl = result.content.find('\n'); nl != std::string::npos)
                                         header = result.content.substr(0, nl);
                                     else
                                         header = result.content.substr(0, std::min(result.content.size(), static_cast<size_t>(200)));
                                     candidates.push_back({result.content, file, lines, result.score, definitionPriority(file, header)});
                                 }

                                 std::stable_sort(candidates.begin(), candidates.end(),
                                                  [](const ScoredEntry &a, const ScoredEntry &b)
                                                  { return a.priority != b.priority ? a.priority > b.priority : a.score > b.score; });

                                 nlohmann::json matches = nlohmann::json::array();
                                 std::set<std::string> seen;
                                 for (auto &candidate : candidates)
                                 {
                                     if (static_cast<int64_t>(matches.size()) >= maxResults)
                                         break;

                                     const std::string key = candidate.file + ":" + candidate.lines;
                                     if (!seen.insert(key).second)
                                         continue;

                                     int startLine = 0;
                                     int endLine = 0;
                                     if (auto dash = candidate.lines.find('-'); dash != std::string::npos)
                                     {
                                         try
                                         {
                                             startLine = std::stoi(candidate.lines.substr(0, dash));
                                             endLine = std::stoi(candidate.lines.substr(dash + 1));
                                         }
                                         catch (...)
                                         {
                                         }
                                     }

                                     std::string header;
                                     if (auto nl = candidate.content.find('\n'); nl != std::string::npos)
                                         header = candidate.content.substr(0, nl);
                                     else
                                         header = candidate.content;

                                     auto extractField = [&](const std::string &tag) -> std::string
                                     {
                                         auto pos = header.find(tag);
                                         if (pos == std::string::npos)
                                             return "";
                                         pos += tag.size();
                                         auto end = header.find(" |", pos);
                                         return end != std::string::npos ? header.substr(pos, end - pos) : header.substr(pos);
                                     };

                                     nlohmann::json match = {{"file", candidate.file}, {"start_line", startLine}, {"end_line", endLine}};
                                     const std::string ns = extractField("Namespace: ");
                                     const std::string cls = extractField("Class: ");
                                     const std::string method = extractField("Method: ");
                                     if (!ns.empty())
                                         match["namespace"] = ns;
                                     if (!cls.empty())
                                         match["class"] = cls;
                                     if (!method.empty())
                                         match["method"] = method;
                                     matches.push_back(std::move(match));
                                 }

                                 if (matches.empty())
                                     return "{\"error\":\"no indexed symbols found for the given queries\"}";

                                 return DumpJson(nlohmann::json{{"matches", matches}, {"queries", queries}});
                             }});
        }

        void AppendCodebaseManagementTools(std::vector<pagent::ToolDefinition> &tools, GUI *gui)
        {
            tools.push_back({.name = "get_codebase_index_status",
                             .description = "Returns the status of the BM25 codebase index used by find_symbol and search_codebase. "
                                            "Call this first if you are unsure whether the index is ready. "
                                            "Indexing is asynchronous — poll this after rebuild_codebase_index until index_ready is true. "
                                            "Key fields: has_index, index_ready, is_indexing, entry_count, progress/total, current_file.",
                             .properties = {},
                             .handler = [gui](const std::string &) -> std::string
                             {
                                 if (!gui)
                                     return "{\"error\":\"GUI not available\"}";

                                 const auto status = gui->GetCodebaseStatus();
                                 return DumpJson(nlohmann::json{
                                     {"has_index", gui->HasCodebaseIndex()},
                                     {"is_indexing", gui->IsIndexing()},
                                     {"index_ready", status.ready},
                                     {"progress", gui->GetIndexProgress()},
                                     {"total", gui->GetIndexTotal()},
                                     {"current_file", gui->GetIndexCurrentFile()},
                                     {"entry_count", gui->GetCodebaseEntryCount()},
                                 });
                             }});

            tools.push_back({.name = "rebuild_codebase_index",
                             .description = "Builds or rebuilds the BM25 codebase index required by find_symbol and search_codebase. "
                                            "Indexing runs asynchronously in the background — use get_codebase_index_status to poll for completion. "
                                            "Use full_rebuild=true (default) to clear stale data before indexing. "
                                            "Call this when get_codebase_index_status reports has_index=false or the index is clearly out of date.",
                             .properties = {
                                 {"full_rebuild", "When true, clears the current index before indexing. Default: true.", pagent::SchemaType::Boolean, false},
                             },
                             .handler = [gui](const std::string &args) -> std::string
                             {
                                 if (!gui)
                                     return "{\"error\":\"GUI not available\"}";
                                 if (gui->IsIndexing())
                                     return "{\"status\":\"already_running\"}";

                                 const bool fullRebuild = ExtractBoolArg(args, "full_rebuild", true);
                                 gui->QueueMainThreadAction([gui, fullRebuild]()
                                                            { gui->StartCodebaseIndexing(fullRebuild); });
                                 return DumpJson(nlohmann::json{
                                     {"status", "started"},
                                     {"full_rebuild", fullRebuild},
                                 });
                             }});

            tools.push_back({.name = "cancel_codebase_index",
                             .description = "Cancels a running rebuild_codebase_index job. "
                                            "Only call this if indexing is confirmed running via get_codebase_index_status and you need to abort it.",
                             .properties = {},
                             .handler = [gui](const std::string &) -> std::string
                             {
                                 if (!gui)
                                     return "{\"error\":\"GUI not available\"}";

                                 gui->QueueMainThreadAction([gui]()
                                                            { gui->CancelCodebaseIndexing(); });
                                 return "{\"status\":\"cancelling\"}";
                             }});
        }

        void AppendVisualTools(std::vector<pagent::ToolDefinition> &tools, EditorToolRuntime *runtime, GUI *gui)
        {
            tools.push_back({.name = "profiler_snapshot",
                             .description = "Takes a snapshot of the current profiler data (Overview, CPU, GPU) and saves it as a JSON file "
                                            "to Assets/Profiler/profiler_snapshot_<datetime>.json. "
                                            "Returns the saved file path. "
                                            "Use this to capture frame timings, memory usage, GPU pass breakdown, and CPU scope timings.",
                             .properties = {},
                             .handler = [runtime](const std::string &) -> std::string
                             {
                                 return runtime ? runtime->TakeProfilerSnapshot()
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "take_screenshot",
                             .description = "Takes a screenshot of the current editor state and returns it as a base64 PNG image for visual inspection. "
                                            "The image is embedded in the tool result and is visible to vision-capable models. "
                                            "Use this to see the current layout before deciding where to click with inject_mouse_input.",
                             .properties = {},
                             .handler = [runtime](const std::string &) -> std::string
                             {
                                 return runtime ? runtime->TakeScreenshot()
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "query_imgui_windows",
                             .description = "Returns all currently visible ImGui panels with their screen positions and sizes, "
                                            "plus 'tab_bars': an array of tab bars each listing every tab with its exact click coordinates. "
                                            "Use tab_bars to find precise pixel coordinates for clicking specific tabs. "
                                            "Use window positions for clicking inside panels.",
                             .properties = {},
                             .handler = [runtime](const std::string &) -> std::string
                             {
                                 return runtime ? runtime->QueryImGuiWindows()
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "inject_mouse_input",
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
                             .handler = [runtime](const std::string &args) -> std::string
                             {
                                 return runtime ? runtime->InjectMouseInput(args)
                                                : "{\"error\":\"EditorToolRuntime not available\"}";
                             }});

            tools.push_back({.name = "get_console_log",
                             .description = "Returns recent log entries from the editor console (Info, Warn, Error). "
                                            "Use 'count' to limit how many recent entries to return (default 100). "
                                            "Use 'level' to filter by severity: 'info', 'warn', 'error', or 'all' (default).",
                             .properties = {
                                 {"count", "Maximum number of recent entries to return (default 100)", pagent::SchemaType::Integer, false},
                                 {"level", "Filter by severity: 'info', 'warn', 'error', or 'all' (default)", pagent::SchemaType::String, false},
                             },
                             .handler = [gui](const std::string &args) -> std::string
                             {
                                 if (!gui)
                                     return DumpJson({{"error", "GUI not available"}});
                                 Console *console = gui->GetWidget<Console>();
                                 if (!console)
                                     return DumpJson({{"error", "Console not available"}});

                                 nlohmann::json j = nlohmann::json::parse(args, nullptr, false);
                                 int count = 100;
                                 std::string level = "all";
                                 if (j.is_object())
                                 {
                                     if (j.contains("count") && j["count"].is_number_integer())
                                         count = j["count"].get<int>();
                                     if (j.contains("level") && j["level"].is_string())
                                         level = j["level"].get<std::string>();
                                 }

                                 const auto &logs = console->GetLogs();
                                 nlohmann::json entries = nlohmann::json::array();
                                 int start = std::max(0, (int)logs.size() - count);
                                 for (int i = start; i < (int)logs.size(); ++i)
                                 {
                                     const auto &e = logs[i];
                                     std::string typeStr = e.type == LogType::Warn    ? "warn"
                                                           : e.type == LogType::Error ? "error"
                                                                                      : "info";
                                     if (level != "all" && typeStr != level)
                                         continue;
                                     entries.push_back({{"level", typeStr}, {"text", e.text}});
                                 }
                                 return DumpJson({{"entries", entries}, {"total", (int)logs.size()}});
                             }});
        }
    } // namespace
} // namespace pe
