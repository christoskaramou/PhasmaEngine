#include "GUI/Agent/EditorToolCatalog.h"
#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/GUI.h"
#include "GUI/Widgets/Console.h"
#include "Base/Path.h"
#include "PhasmaMCP/Utils.h"
#include "PhasmaMCP/Codebase/BM25Index.h"

namespace pe
{
    namespace
    {
        using pmcp::CallToolResult;
        using pmcp::Context;
        using pmcp::ToolDefinition;
        namespace schema = pmcp::schema;

        std::string ToLower(std::string value)
        {
            for (auto &c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        std::string NormalizeRelativePath(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            std::string relative = std::filesystem::relative(path, root).string();
            std::replace(relative.begin(), relative.end(), '\\', '/');
            return relative;
        }

        // Skip predicate for project-wide source traversal. Excludes hidden dirs (.git,
        // .fetchcontent-ninja, .fetchcontent-vs, .vs, ...) and any CMake build output dir
        // (build, build-vs, build-ninja-full, build-ninja-full-tracy, ...). Without the
        // build-* prefix match, MCP code-search would crawl every preset's generated tree.
        bool IsBuildOrHiddenDir(const std::string &dirName)
        {
            if (dirName.empty())
                return false;
            if (dirName[0] == '.')
                return true;
            if (dirName == "build")
                return true;
            if (dirName.size() >= 6 && dirName.compare(0, 6, "build-") == 0)
                return true;
            return false;
        }

        // Tools whose handlers delegate to EditorToolRuntime parse the runtime's JSON-string return
        // into a structured CallToolResult so MCP clients see structuredContent + a text mirror.
        CallToolResult RuntimeJsonResult(const std::string &raw)
        {
            try
            {
                auto json = nlohmann::json::parse(raw.empty() ? "{}" : raw);
                if (json.is_object() && json.contains("error") && json["error"].is_string())
                    return CallToolResult::Error(json["error"].get<std::string>());
                return CallToolResult::Json(std::move(json));
            }
            catch (...)
            {
                return CallToolResult::Json(nlohmann::json{{"raw", raw}});
            }
        }

        CallToolResult RuntimeUnavailable()
        {
            return CallToolResult::Error("EditorToolRuntime not available");
        }

        void AppendProjectFileTools(std::vector<ToolDefinition> &tools, const std::string &projectRoot);
        void AppendWorkspaceTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime,
                                  const std::function<void(const std::string &)> &onFeatureRequest);
        void AppendEditorActionTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime);
        void AppendCodebaseManagementTools(std::vector<ToolDefinition> &tools, GUI *gui);
        void AppendCodebaseTools(std::vector<ToolDefinition> &tools,
                                 const std::shared_ptr<pmcp::BM25Index> &codebaseBM25);
        void AppendVisualTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime, GUI *gui);
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

        for (fs::path candidate = assetsPath; !candidate.empty(); candidate = candidate.parent_path())
        {
            if (fs::exists(candidate / "PhasmaEditor/Code/GUI/GUI.cpp") &&
                fs::exists(candidate / "PhasmaCore") &&
                fs::exists(candidate / "PhasmaMCP"))
            {
                return candidate;
            }

            if (candidate == candidate.parent_path())
                break;
        }

        // No source tree (e.g. release zip): use the runtime root. addIfExists checks in
        // ApplyDefaultCodebaseIndexingConfig will skip missing source dirs, leaving the
        // indexing arrays empty rather than pointing at bogus paths.
        return fs::path(Path::Root).lexically_normal();
    }

    std::vector<ToolDefinition> BuildEditorToolDefinitions(const EditorToolCatalogContext &context)
    {
        std::vector<ToolDefinition> tools;

        std::string projectRoot = context.projectRoot.empty() ? GetEditorRepoRootFromAssets().string() : context.projectRoot;
        if (!projectRoot.empty() && projectRoot.back() != '/')
            projectRoot += '/';

        AppendProjectFileTools(tools, projectRoot);
        AppendWorkspaceTools(tools, context.runtime, context.onFeatureRequest);
        AppendEditorActionTools(tools, context.runtime);
        AppendCodebaseManagementTools(tools, context.gui);
        AppendCodebaseTools(tools, context.codebaseBM25);
        AppendVisualTools(tools, context.runtime, context.gui);
        return tools;
    }

    namespace
    {
        void AppendProjectFileTools(std::vector<ToolDefinition> &tools, const std::string &projectRoot)
        {
            {
                ToolDefinition tool;
                tool.name = "read_project_file";
                tool.description = "Reads a source file from the project (C++ headers, source, shaders, configs). "
                                   "Preferred workflow: use grep_project to find the relevant line number first, "
                                   "then read only the surrounding lines with start_line/end_line. "
                                   "Avoid reading whole files - surgical reads of 30-100 lines cost far fewer tokens.";
                tool.inputSchema = schema::Object({
                    {"path", "File path relative to project root (e.g. 'PhasmaCore/Code/Base/Path.h') or absolute", schema::String(), true},
                    {"start_line", "First line to read, 1-based (default: 1). Use after grep_project to read just the relevant area.", schema::Integer()},
                    {"end_line", "Last line to read, inclusive (default: read all). Combine with start_line for surgical reads.", schema::Integer()},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string path = args.value("path", "");
                    if (path.empty())
                        return CallToolResult::Error("missing path");

                    std::filesystem::path filePath(path);
                    if (filePath.is_relative())
                        filePath = std::filesystem::path(projectRoot) / filePath;

                    if (!pmcp::IsPathSafe(filePath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");
                    if (!std::filesystem::exists(filePath))
                        return CallToolResult::Error("file not found: " + filePath.string());
                    if (std::filesystem::is_directory(filePath))
                        return CallToolResult::Error("path is a directory, use find_project_file or list_project_dir");

                    int64_t startLine = args.value("start_line", static_cast<int64_t>(1));
                    int64_t endLine = args.value("end_line", static_cast<int64_t>(0));
                    if (startLine < 1)
                        startLine = 1;

                    std::ifstream file(filePath);
                    if (!file.is_open())
                        return CallToolResult::Error("cannot open: " + filePath.string());

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

                    return CallToolResult::Json(nlohmann::json{
                        {"path", NormalizeRelativePath(filePath, projectRoot)},
                        {"content", content},
                        {"start_line", startLine},
                        {"end_line", endLine > 0 ? std::min(endLine, lineNum) : lineNum},
                        {"total_lines", totalLines},
                    });
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "write_project_file";
                tool.description = "Writes content to a file in PhasmaEditor/Code/ or PhasmaEditor/Assets/. "
                                   "Use this to modify C++ source, headers, shaders, Lua scripts, or config files. "
                                   "Creates parent directories automatically. "
                                   "Path relative to project root or absolute.";
                tool.inputSchema = schema::Object({
                    {"path", "File path relative to project root (e.g. 'PhasmaEditor/Code/App/App.cpp')", schema::String(), true},
                    {"content", "Text content to write", schema::String(), true},
                    {"append", "Append instead of overwriting when true", schema::Boolean()},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string path = args.value("path", "");
                    std::string content = args.value("content", "");
                    bool appendMode = args.value("append", false);
                    if (path.empty())
                        return CallToolResult::Error("missing path");
                    if (content.empty())
                        return CallToolResult::Error("missing content");

                    std::filesystem::path filePath(path);
                    if (filePath.is_relative())
                        filePath = std::filesystem::path(projectRoot) / filePath;

                    if (!pmcp::IsPathSafe(filePath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");

                    std::filesystem::path parentDir = filePath.parent_path();
                    if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                    {
                        std::error_code ec;
                        std::filesystem::create_directories(parentDir, ec);
                        if (ec)
                            return CallToolResult::Error("cannot create directory: " + ec.message());
                    }

                    auto flags = std::ios::out;
                    flags |= appendMode ? std::ios::app : std::ios::trunc;

                    std::ofstream file(filePath, flags);
                    if (!file.is_open())
                        return CallToolResult::Error("cannot write: " + filePath.string());
                    file << content;
                    file.flush();
                    if (!file.good())
                        return CallToolResult::Error("write failed (disk full or I/O error): " + filePath.string());

                    return CallToolResult::Json(nlohmann::json{{"status", "ok"}, {"path", filePath.string()}});
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "find_project_file";
                tool.description = "Recursively searches for files in the project by name substring (case-insensitive). "
                                   "Use to find C++ headers, source files, shaders, configs, models, etc.";
                tool.inputSchema = schema::Object({
                    {"query", "Filename substring to search for (e.g. 'Camera.h', '.hlsl', 'sponza')", schema::String(), true},
                    {"dir", "Subdirectory to search (e.g. 'PhasmaCore/Code'). Defaults to project root.", schema::String()},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string query = args.value("query", "");
                    std::string dir = args.value("dir", "");
                    if (query.empty())
                        return CallToolResult::Error("missing query");

                    std::filesystem::path searchPath = dir.empty() ? std::filesystem::path(projectRoot)
                                                                   : std::filesystem::path(projectRoot) / dir;
                    if (!pmcp::IsPathSafe(searchPath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");
                    if (!std::filesystem::exists(searchPath))
                        return CallToolResult::Error("directory not found: " + searchPath.string());

                    const std::string queryLower = ToLower(query);
                    nlohmann::json files = nlohmann::json::array();
                    int count = 0;
                    for (auto it = std::filesystem::recursive_directory_iterator(
                             searchPath, std::filesystem::directory_options::skip_permission_denied);
                         it != std::filesystem::recursive_directory_iterator(); ++it)
                    {
                        if (it->is_directory())
                        {
                            const std::string dirName = it->path().filename().string();
                            if (IsBuildOrHiddenDir(dirName) || dirName == "third_party")
                                it.disable_recursion_pending();
                            continue;
                        }
                        if (!it->is_regular_file())
                            continue;

                        if (ToLower(it->path().filename().string()).find(queryLower) == std::string::npos)
                            continue;

                        files.push_back(NormalizeRelativePath(it->path(), projectRoot));
                        if (++count >= 30)
                            break;
                    }
                    return CallToolResult::Json(nlohmann::json{{"count", count}, {"files", std::move(files)}});
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "grep_project";
                tool.description = "Search for a literal string or regex pattern in project source files. "
                                   "Returns matching lines with file path and line number. "
                                   "Use for finding function definitions, usages, class members, shader code, etc. "
                                   "Literal search (default) is fast and sufficient for identifiers. "
                                   "Set regex=true for pattern matching (ECMAScript syntax).";
                tool.inputSchema = schema::Object({
                    {"pattern", "Text to search for. Literal string by default; ECMAScript regex if regex=true.", schema::String(), true},
                    {"path", "Subdirectory to search (e.g. 'PhasmaEditor/Code'). Defaults to project root.", schema::String()},
                    {"glob", "File extension filter (e.g. '*.cpp', '*.hlsl', '*.h'). Defaults to all files.", schema::String()},
                    {"regex", "Set to true to treat pattern as an ECMAScript regex. Default: false (literal).", schema::Boolean()},
                    {"case_sensitive", "Case-sensitive match. Default: true.", schema::Boolean()},
                    {"max_results", "Maximum number of matching lines to return. Default: 50.", schema::Integer()},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string pattern = args.value("pattern", "");
                    std::string searchDir = args.value("path", "");
                    std::string globFilter = args.value("glob", "");
                    bool useRegex = args.value("regex", false);
                    bool caseSensitive = args.value("case_sensitive", true);
                    int maxResults = static_cast<int>(args.value("max_results", static_cast<int64_t>(50)));

                    if (pattern.empty())
                        return CallToolResult::Error("missing pattern");
                    if (maxResults <= 0 || maxResults > 500)
                        maxResults = 50;

                    std::filesystem::path searchPath = searchDir.empty() ? std::filesystem::path(projectRoot)
                                                                         : std::filesystem::path(projectRoot) / searchDir;
                    if (!pmcp::IsPathSafe(searchPath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");
                    if (!std::filesystem::exists(searchPath))
                        return CallToolResult::Error("directory not found: " + searchPath.string());

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
                            return CallToolResult::Error(std::string("invalid regex: ") + error.what());
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

                            matches.push_back({{"file", NormalizeRelativePath(filePath, projectRoot)},
                                               {"line", lineNum},
                                               {"text", trimmed}});
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
                    auto iter = std::filesystem::recursive_directory_iterator(
                        searchPath, std::filesystem::directory_options::skip_permission_denied);
                    const auto end = std::filesystem::recursive_directory_iterator();
                    for (; iter != end && keepGoing; ++iter)
                    {
                        if (iter->is_directory())
                        {
                            const std::string dirName = iter->path().filename().string();
                            if (IsBuildOrHiddenDir(dirName))
                                iter.disable_recursion_pending();
                            continue;
                        }
                        if (!iter->is_regular_file() || !matchesExtension(iter->path()))
                            continue;
                        keepGoing = processFile(iter->path());
                    }

                    return CallToolResult::Json(nlohmann::json{{"count", count}, {"matches", std::move(matches)}});
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "list_project_dir";
                tool.description = "Lists files and subdirectories at a project path. "
                                   "Use to browse project structure (PhasmaCore/Code/, PhasmaEditor/Code/, etc.).";
                tool.inputSchema = schema::Object({
                    {"path", "Directory path relative to project root (e.g. 'PhasmaEditor/Code/Script')", schema::String(), true},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string path = args.value("path", "");
                    if (path.empty())
                        return CallToolResult::Error("missing path");

                    std::filesystem::path dirPath(path);
                    if (dirPath.is_relative())
                        dirPath = std::filesystem::path(projectRoot) / dirPath;

                    if (!pmcp::IsPathSafe(dirPath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");
                    if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
                        return CallToolResult::Error("not a directory: " + dirPath.string());

                    nlohmann::json files = nlohmann::json::array();
                    nlohmann::json dirs = nlohmann::json::array();
                    for (const auto &entry : std::filesystem::directory_iterator(dirPath))
                    {
                        const std::string name = entry.path().filename().string();
                        if (name.empty() || name[0] == '.')
                            continue;
                        if (entry.is_directory())
                            dirs.push_back(name + "/");
                        else
                            files.push_back(name);
                    }
                    return CallToolResult::Json(nlohmann::json{
                        {"path", dirPath.string()},
                        {"files", std::move(files)},
                        {"dirs", std::move(dirs)},
                    });
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "patch_project_file";
                tool.description = "Replaces a specific range of lines in a project source file. "
                                   "Token-efficient alternative to write_project_file: send only the changed lines, not the whole file. "
                                   "Preferred for all C++ source, header, and shader edits. "
                                   "Workflow: (1) read_project_file with start_line/end_line to see current code, "
                                   "(2) patch_project_file with the exact replacement. "
                                   "IMPORTANT - to INSERT a line without losing existing content: include the existing line(s) "
                                   "in the replacement. Example: to insert '// comment' before line 8 without removing it, "
                                   "use start_line=8 end_line=8 replacement='// comment\\n<original line 8 content>'. "
                                   "Replacing an empty line removes it - always include it in the replacement if it must be kept. "
                                   "Use expected_context to guard against wrong-offset edits.";
                tool.inputSchema = schema::Object({
                    {"path", "File path relative to project root (e.g. 'PhasmaEditor/Code/App/App.cpp')", schema::String(), true},
                    {"start_line", "First line to replace, 1-based inclusive", schema::Integer(), true},
                    {"end_line", "Last line to replace, 1-based inclusive (must be >= start_line)", schema::Integer(), true},
                    {"replacement", "New text replacing the specified lines. May contain newlines. Pass empty string to delete lines. To insert without losing content, include the displaced lines in this string.", schema::String(), true},
                    {"expected_context", "Optional safety check: a short substring that must appear in the target region. Patch is rejected if not found.", schema::String()},
                });
                tool.handler = [projectRoot](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string path = args.value("path", "");
                    std::string replacement = args.value("replacement", "");
                    std::string expectedContext = args.value("expected_context", "");
                    int64_t startLine = args.value("start_line", static_cast<int64_t>(0));
                    int64_t endLine = args.value("end_line", static_cast<int64_t>(0));

                    if (path.empty())
                        return CallToolResult::Error("missing path");
                    if (startLine < 1 || endLine < startLine)
                        return CallToolResult::Error("start_line must be >= 1 and end_line must be >= start_line");

                    std::filesystem::path filePath(path);
                    if (filePath.is_relative())
                        filePath = std::filesystem::path(projectRoot) / filePath;

                    if (!pmcp::IsPathSafe(filePath.string(), projectRoot))
                        return CallToolResult::Error("path outside project directory");
                    if (!std::filesystem::exists(filePath))
                        return CallToolResult::Error("file not found: " + filePath.string());

                    std::vector<std::string> lines;
                    bool trailingNewline = false;
                    bool useCRLF = false;
                    {
                        std::ifstream file(filePath, std::ios::binary);
                        if (!file.is_open())
                            return CallToolResult::Error("cannot open: " + filePath.string());

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
                    if (startLine > totalLines + 1 || (endLine > totalLines && totalLines > 0) ||
                        (totalLines == 0 && endLine > 0))
                        return CallToolResult::Error("line range out of bounds: file has " + std::to_string(totalLines) + " lines");

                    if (!expectedContext.empty())
                    {
                        std::string ctx = expectedContext;
                        ctx.erase(std::remove(ctx.begin(), ctx.end(), '\r'), ctx.end());
                        std::string region;
                        for (int64_t i = startLine - 1; i < endLine && i < totalLines; ++i)
                            region += lines[static_cast<size_t>(i)] + "\n";
                        if (region.find(ctx) == std::string::npos)
                            return CallToolResult::Error("expected_context not found in target region - patch rejected to prevent wrong-offset edit");
                    }

                    std::vector<std::string> replacementLines;
                    if (!replacement.empty())
                    {
                        std::istringstream stream(replacement);
                        std::string line;
                        while (std::getline(stream, line))
                        {
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
                        return CallToolResult::Error("cannot write: " + filePath.string());

                    const std::string lineEnding = useCRLF ? "\r\n" : "\n";
                    for (size_t i = 0; i < result.size(); ++i)
                    {
                        file << result[i];
                        if (i + 1 < result.size() || trailingNewline)
                            file << lineEnding;
                    }

                    return CallToolResult::Json(nlohmann::json{
                        {"status", "patched"},
                        {"path", NormalizeRelativePath(filePath, projectRoot)},
                        {"lines_replaced", endLine - startLine + 1},
                        {"lines_inserted", static_cast<int64_t>(replacementLines.size())},
                        {"new_total_lines", static_cast<int64_t>(result.size())},
                    });
                };
                tools.push_back(std::move(tool));
            }
        }

        void AppendWorkspaceTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime,
                                  const std::function<void(const std::string &)> &onFeatureRequest)
        {
            {
                ToolDefinition tool;
                tool.name = "execute_lua";
                tool.description = "Executes Lua code in the engine's ScriptSystem. Use this for ALL scene manipulation: "
                                   "models, camera, lights, materials, settings, shaders, particles, skybox, scene save/load. "
                                   "The full Lua API is documented in the system prompt. "
                                   "Use pe_log() to output information. Returns captured output or 'ok' on success.";
                tool.inputSchema = schema::Object({
                    {"code", "Lua code to execute", schema::String(), true},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->ExecuteLua(args.value("code", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "reload_module";
                tool.description = "Triggers hot-reload of the PhasmaEditor module (equivalent to File > Reload Module). "
                                   "Saves scene state and reloads DLL. Use this to test hot-reload workflows.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->ReloadModule());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "create_node";
                tool.description = "Creates an empty scene node. Returns stable id (use in other tools). "
                                   "Parent can be a name or stable id (node:index:revision).";
                tool.inputSchema = schema::Object({
                    {"name", "Node name", schema::String(), true},
                    {"parent", "Parent node name or id (omit for root)", schema::String()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->CreateNode(args.value("name", "Node"), args.value("parent", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "set_node_transform";
                tool.description = "Sets position, rotation (degrees), and/or scale on a node. "
                                   "Node can be a name or stable id (node:index:revision).";
                tool.inputSchema = schema::Object({
                    {"node", "Node name or id", schema::String(), true},
                    {"position", "Position as [x,y,z]", schema::ArrayOf(schema::Number())},
                    {"rotation", "Rotation in degrees as [x,y,z]", schema::ArrayOf(schema::Number())},
                    {"scale", "Scale as [x,y,z]", schema::ArrayOf(schema::Number())},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    std::string node = args.value("node", "");
                    if (node.empty())
                        return CallToolResult::Error("node required");
                    float pos[3], rot[3], scale[3];
                    float *pPos = nullptr, *pRot = nullptr, *pScale = nullptr;
                    auto readVec3 = [](const nlohmann::json &arr, float out[3]) -> bool
                    {
                        if (!arr.is_array() || arr.size() != 3)
                            return false;
                        out[0] = arr[0].get<float>();
                        out[1] = arr[1].get<float>();
                        out[2] = arr[2].get<float>();
                        return true;
                    };
                    if (args.contains("position") && readVec3(args["position"], pos))
                        pPos = pos;
                    if (args.contains("rotation") && readVec3(args["rotation"], rot))
                        pRot = rot;
                    if (args.contains("scale") && readVec3(args["scale"], scale))
                        pScale = scale;
                    return RuntimeJsonResult(runtime->SetNodeTransform(node, pPos, pRot, pScale));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "add_mesh_to_node";
                tool.description = "Attaches a primitive mesh to a node (synchronous). "
                                   "Node can be a name or stable id (node:index:revision). "
                                   "Valid primitives: cube, sphere, plane, cylinder, cone, quad.";
                tool.inputSchema = schema::Object({
                    {"node", "Node name or id", schema::String(), true},
                    {"primitive", "Primitive type", schema::String(), true},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->AddMeshToNode(args.value("node", ""), args.value("primitive", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "set_node_material";
                tool.description = "Sets material properties on a node's mesh. "
                                   "Node can be a name or stable id (node:index:revision).";
                tool.inputSchema = schema::Object({
                    {"node", "Node name or id", schema::String(), true},
                    {"slot", "Mesh slot index (default 0)", schema::Integer()},
                    {"base_color", "Base color as [r,g,b,a] (0-1)", schema::ArrayOf(schema::Number())},
                    {"metallic", "Metallic factor (0-1)", schema::Number()},
                    {"roughness", "Roughness factor (0-1)", schema::Number()},
                    {"transmission", "Transmission factor (0-1)", schema::Number()},
                    {"render_type", "Render type: opaque, alpha_cut, alpha_blend, transmission", schema::String()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    std::string node = args.value("node", "");
                    if (node.empty())
                        return CallToolResult::Error("node required");
                    int slot = args.value("slot", 0);
                    nlohmann::json props;
                    for (auto &key : {"base_color", "metallic", "roughness", "transmission", "render_type"})
                        if (args.contains(key))
                            props[key] = args[key];
                    return RuntimeJsonResult(runtime->SetNodeMaterial(node, slot, props.dump()));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "add_light";
                tool.description = "Adds a light to the scene (synchronous). "
                                   "Valid types: directional, point, spot, area.";
                tool.inputSchema = schema::Object({
                    {"type", "Light type", schema::String(), true},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->AddLight(args.value("type", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "query_scene";
                tool.description = "Returns scene tree with stable node ids, mesh counts, cameras. "
                                   "Use the id field in other tools to address nodes unambiguously.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->QueryScene());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "find_loadable_model";
                tool.description = "Searches for 3D model files (.glb, .gltf, .obj, .fbx) in Assets/Objects/ by name. "
                                   "Returns paths ready to use with load_model(). "
                                   "Example: query 'helmet' finds 'DamagedHelmet/glTF-Binary/DamagedHelmet.glb'.";
                tool.inputSchema = schema::Object({
                    {"query", "Model name to search for (e.g. 'helmet', 'avocado', 'sponza')", schema::String(), true},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->FindLoadableModel(args.value("query", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "read_agent_file";
                tool.description = "Reads a text file from the agent workspace (Assets/Agent/). "
                                   "Use this to read MEMORY.md, TASKS.md, PROGRESSION.md, or any workspace file.";
                tool.inputSchema = schema::Object({
                    {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", schema::String(), true},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->ReadAgentFile(args.value("path", "")));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "write_agent_file";
                tool.description = "Writes or appends to a text file in the agent workspace (Assets/Agent/). "
                                   "Use for MEMORY.md, TASKS.md, PROGRESSION.md, Lua scripts, or any persistent notes. "
                                   "Creates parent directories automatically.";
                tool.inputSchema = schema::Object({
                    {"path", "File path relative to workspace (e.g. 'MEMORY.md') or absolute", schema::String(), true},
                    {"content", "Text content to write", schema::String(), true},
                    {"append", "Append instead of overwriting when true", schema::Boolean()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->WriteAgentFile(
                        args.value("path", ""), args.value("content", ""), args.value("append", false)));
                };
                tools.push_back(std::move(tool));
            }

            const std::string requestsPath = Path::Assets + "Agent/REQUESTS.md";
            static std::mutex s_requestsMutex;

            {
                ToolDefinition tool;
                tool.name = "request_feature";
                tool.description = "Request a new feature, tool, or Lua binding that is not currently available. "
                                   "The request will be shown to the user and saved to REQUESTS.md. "
                                   "Use this when you need a capability that doesn't exist yet.";
                tool.inputSchema = schema::Object({
                    {"title", "Short title for the feature request", schema::String(), true},
                    {"description", "Detailed description of what is needed and why", schema::String(), true},
                });
                tool.handler = [onFeatureRequest, requestsPath](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string title = args.value("title", "");
                    std::string description = args.value("description", "");
                    if (title.empty())
                        return CallToolResult::Error("missing title");

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

                    return CallToolResult::Json(nlohmann::json{
                        {"status", "request saved to REQUESTS.md"},
                        {"title", title},
                    });
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "complete_feature";
                tool.description = "Mark a feature request as completed and remove it from REQUESTS.md. "
                                   "Use this when a previously requested feature has been implemented.";
                tool.inputSchema = schema::Object({
                    {"title", "Title of the completed feature request to remove", schema::String(), true},
                });
                tool.handler = [requestsPath](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string title = args.value("title", "");
                    if (title.empty())
                        return CallToolResult::Error("missing title");

                    std::lock_guard lock(s_requestsMutex);

                    if (!std::filesystem::exists(requestsPath))
                        return CallToolResult::Error("REQUESTS.md not found");

                    std::string content;
                    {
                        std::ifstream file(requestsPath, std::ios::in);
                        if (!file.is_open())
                            return CallToolResult::Error("cannot open REQUESTS.md");
                        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                    }

                    const std::string marker = "- **" + title + "**";
                    const auto pos = content.find(marker);
                    if (pos == std::string::npos)
                        return CallToolResult::Error("request not found: " + title);

                    auto entryEnd = content.find("\n- **", pos + marker.size());
                    if (entryEnd == std::string::npos)
                        entryEnd = content.size();
                    else
                        entryEnd += 1;

                    content.erase(pos, entryEnd - pos);

                    static const std::string tripleNL = "\n\n\n";
                    static const std::string doubleNL = "\n\n";
                    size_t nlPos;
                    while ((nlPos = content.find(tripleNL)) != std::string::npos)
                        content.replace(nlPos, tripleNL.size(), doubleNL);

                    std::ofstream file(requestsPath, std::ios::out | std::ios::trunc);
                    if (!file.is_open())
                        return CallToolResult::Error("cannot write REQUESTS.md");
                    file << content;

                    return CallToolResult::Json(nlohmann::json{{"status", "removed"}, {"title", title}});
                };
                tools.push_back(std::move(tool));
            }
        }

        void AppendEditorActionTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime)
        {
            {
                ToolDefinition tool;
                tool.name = "list_editor_actions";
                tool.description = "Lists editor windows and named editor actions that can be invoked without mouse input. "
                                   "Prefer this plus invoke_editor_action/toggle_editor_window over screenshots and injected mouse clicks.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->QueryEditorActions());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "toggle_editor_window";
                tool.description = "Opens, closes, or toggles an editor window by name or id. "
                                   "Use list_editor_actions to discover ids like window.console, window.properties, window.viewport. "
                                   "state defaults to toggle; accepted states are toggle, open, and closed.";
                tool.inputSchema = schema::Object({
                    {"window", "Window name or id, e.g. 'Console', 'Properties', or 'window.console'", schema::String(), true},
                    {"state", "toggle, open, or closed (default: toggle)", schema::String()},
                    {"open", "Optional boolean override for exact open/closed state", schema::Boolean()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->SetEditorWindowOpen(args.value("window", ""), args.dump()));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "invoke_editor_action";
                tool.description = "Invokes a named editor action without mouse input. "
                                   "Call list_editor_actions first, then pass an action id such as file.save_scene, "
                                   "layout.reset, gizmo.grid, play.start, or status.console_errors. "
                                   "Common optional arguments include state/open for toggles, path for file actions, "
                                   "discard_unsaved for scene load/new/exit, overwrite for save, steps for undo/redo, "
                                   "and style/preset/scale for layout actions.";
                tool.inputSchema = schema::Object({
                    {"action", "Action id from list_editor_actions", schema::String(), true},
                    {"state", "toggle, open, closed, on, or off for toggle actions", schema::String()},
                    {"open", "Optional boolean for window/toggle actions", schema::Boolean()},
                    {"path", "Optional path for load/save actions", schema::String()},
                    {"discard_unsaved", "Allow new/load/exit to discard unsaved scene changes", schema::Boolean()},
                    {"overwrite", "Allow save_scene with path to overwrite an existing file", schema::Boolean()},
                    {"full_rebuild", "Force codebase index rebuild for connection.index_codebase", schema::Boolean()},
                    {"steps", "Undo/redo step count", schema::Integer()},
                    {"style", "Style id for layout.style, e.g. classic, dark, light, modern, unity, unreal", schema::String()},
                    {"preset", "Font preset for layout.font, e.g. small, medium, large, extra_large", schema::String()},
                    {"scale", "Exact ImGui font scale for layout.font", schema::Number()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->InvokeEditorAction(args.value("action", ""), args.dump()));
                };
                tools.push_back(std::move(tool));
            }
        }

        // Parse "// File: path (lines X-Y) | ..." chunk header. Used by both find_symbol and
        // search_codebase to surface readable file/line metadata from BM25 results.
        bool ParseChunkHeader(const std::string &content, std::string &file, std::string &lines)
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
        }

        std::pair<int, int> ParseLineRange(const std::string &lines)
        {
            auto dash = lines.find('-');
            if (dash == std::string::npos)
                return {0, 0};
            try
            {
                return {std::stoi(lines.substr(0, dash)), std::stoi(lines.substr(dash + 1))};
            }
            catch (...)
            {
                return {0, 0};
            }
        }

        std::string ExtractHeaderField(const std::string &header, const std::string &tag)
        {
            auto pos = header.find(tag);
            if (pos == std::string::npos)
                return "";
            pos += tag.size();
            auto end = header.find(" |", pos);
            return end != std::string::npos ? header.substr(pos, end - pos) : header.substr(pos);
        }

        nlohmann::json BuildSymbolMatch(const std::string &content, const std::string &file, const std::string &lines)
        {
            const auto [startLine, endLine] = ParseLineRange(lines);
            std::string header;
            if (auto nl = content.find('\n'); nl != std::string::npos)
                header = content.substr(0, nl);
            else
                header = content;

            nlohmann::json match = {{"file", file}, {"start_line", startLine}, {"end_line", endLine}};
            if (auto ns = ExtractHeaderField(header, "Namespace: "); !ns.empty())
                match["namespace"] = ns;
            if (auto cls = ExtractHeaderField(header, "Class: "); !cls.empty())
                match["class"] = cls;
            if (auto method = ExtractHeaderField(header, "Method: "); !method.empty())
                match["method"] = method;
            return match;
        }

        void AppendCodebaseTools(std::vector<ToolDefinition> &tools,
                                 const std::shared_ptr<pmcp::BM25Index> &codebaseBM25)
        {
            {
                ToolDefinition tool;
                tool.name = "find_symbol";
                tool.description = "Searches the codebase index for a single class, function, method, or struct by name. "
                                   "Returns file path and exact line range - use with read_project_file(start_line, end_line). "
                                   "Far more efficient than grep_project for symbol lookup. "
                                   "Requires the codebase index to be built. "
                                   "Tip: when you need to look up several symbols at once, use search_codebase instead.";
                tool.inputSchema = schema::Object({
                    {"name", "Symbol name to search for (e.g. 'CommandBuffer', 'CreatePipeline', 'RenderGraph')", schema::String(), true},
                    {"max_results", "Maximum matches to return, 1-20 (default: 5)", schema::Integer()},
                });
                tool.handler = [codebaseBM25](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::string symbolName = args.value("name", "");
                    if (symbolName.empty())
                        return CallToolResult::Error("missing name");

                    int64_t maxResults = args.value("max_results", static_cast<int64_t>(5));
                    if (maxResults < 1)
                        maxResults = 1;
                    if (maxResults > 20)
                        maxResults = 20;

                    if (!codebaseBM25 || codebaseBM25->Size() == 0)
                        return CallToolResult::Error("codebase index not built");

                    auto results = codebaseBM25->Search(symbolName, static_cast<int>(maxResults) * 6);
                    if (results.empty())
                        return CallToolResult::Error("no symbols found matching: " + symbolName);

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
                            if (pos != std::string::npos &&
                                headerLower.substr(pos + strlen(tag), nameLower.size()) == nameLower)
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
                        if (!ParseChunkHeader(result.content, file, lines))
                            continue;
                        std::string header;
                        if (auto nl = result.content.find('\n'); nl != std::string::npos)
                            header = result.content.substr(0, nl);
                        else
                            header = result.content.substr(0, std::min(result.content.size(), static_cast<size_t>(200)));
                        candidates.push_back({result.content, file, lines, definitionPriority(file, header)});
                    }

                    std::stable_sort(candidates.begin(), candidates.end(),
                                     [](const Candidate &a, const Candidate &b)
                                     { return a.priority > b.priority; });

                    nlohmann::json matches = nlohmann::json::array();
                    std::set<std::string> seen;
                    for (auto &c : candidates)
                    {
                        if (static_cast<int64_t>(matches.size()) >= maxResults)
                            break;
                        const std::string key = c.file + ":" + c.lines;
                        if (!seen.insert(key).second)
                            continue;
                        matches.push_back(BuildSymbolMatch(c.content, c.file, c.lines));
                    }

                    if (matches.empty())
                        return CallToolResult::Error("no indexed symbols found for: " + symbolName);
                    return CallToolResult::Json(nlohmann::json{{"matches", std::move(matches)}});
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "search_codebase";
                tool.description = "Search the indexed codebase for multiple symbols or concepts simultaneously. "
                                   "Use this instead of calling find_symbol multiple times - pass all related names "
                                   "in one call to save round-trips. Requires the codebase index to be built. "
                                   "Example: search for 'RenderGraph', 'AddPass', and 'IRenderPass' all at once.";
                tool.inputSchema = schema::Object({
                    {"queries", "Array of symbol names or concepts to search simultaneously (e.g. [\"RenderGraph\", \"AddPass\"])", schema::ArrayOf(schema::String()), true},
                    {"max_results", "Maximum total matches to return, 1-30 (default: 10)", schema::Integer()},
                });
                tool.handler = [codebaseBM25](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    std::vector<std::string> queries;
                    if (args.contains("queries") && args["queries"].is_array())
                    {
                        for (const auto &q : args["queries"])
                            if (q.is_string())
                                queries.push_back(q.get<std::string>());
                    }
                    if (queries.empty())
                        return CallToolResult::Error("missing queries array");
                    if (queries.size() > 16)
                        queries.resize(16);

                    int64_t maxResults = args.value("max_results", static_cast<int64_t>(10));
                    if (maxResults < 1)
                        maxResults = 1;
                    if (maxResults > 30)
                        maxResults = 30;

                    if (!codebaseBM25 || codebaseBM25->Size() == 0)
                        return CallToolResult::Error("codebase index not built");

                    auto bm25Results = codebaseBM25->SearchMulti(queries, static_cast<int>(maxResults) * 6);

                    std::vector<std::string> queriesLower;
                    queriesLower.reserve(queries.size());
                    for (const auto &q : queries)
                        queriesLower.push_back(ToLower(q));

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
                        if (!ParseChunkHeader(result.content, file, lines))
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
                    for (auto &c : candidates)
                    {
                        if (static_cast<int64_t>(matches.size()) >= maxResults)
                            break;
                        const std::string key = c.file + ":" + c.lines;
                        if (!seen.insert(key).second)
                            continue;
                        matches.push_back(BuildSymbolMatch(c.content, c.file, c.lines));
                    }

                    if (matches.empty())
                        return CallToolResult::Error("no indexed symbols found for the given queries");

                    nlohmann::json queryArr = nlohmann::json::array();
                    for (const auto &q : queries)
                        queryArr.push_back(q);
                    return CallToolResult::Json(nlohmann::json{
                        {"matches", std::move(matches)},
                        {"queries", std::move(queryArr)},
                    });
                };
                tools.push_back(std::move(tool));
            }
        }

        void AppendCodebaseManagementTools(std::vector<ToolDefinition> &tools, GUI *gui)
        {
            {
                ToolDefinition tool;
                tool.name = "get_codebase_index_status";
                tool.description = "Returns the status of the BM25 codebase index used by find_symbol and search_codebase. "
                                   "Call this first if you are unsure whether the index is ready. "
                                   "Indexing is asynchronous - poll this after rebuild_codebase_index until index_ready is true. "
                                   "Key fields: has_index, index_ready, is_indexing, entry_count, progress/total, current_file.";
                tool.inputSchema = schema::Object({});
                tool.handler = [gui](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!gui)
                        return CallToolResult::Error("GUI not available");

                    const auto status = gui->GetCodebaseStatus();
                    return CallToolResult::Json(nlohmann::json{
                        {"has_index", gui->HasCodebaseIndex()},
                        {"is_indexing", gui->IsIndexing()},
                        {"index_ready", status.ready},
                        {"progress", gui->GetIndexProgress()},
                        {"total", gui->GetIndexTotal()},
                        {"current_file", gui->GetIndexCurrentFile()},
                        {"entry_count", gui->GetCodebaseEntryCount()},
                    });
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "rebuild_codebase_index";
                tool.description = "Builds or rebuilds the BM25 codebase index required by find_symbol and search_codebase. "
                                   "Indexing runs asynchronously in the background - use get_codebase_index_status to poll for completion. "
                                   "Use full_rebuild=true (default) to clear stale data before indexing. "
                                   "Call this when get_codebase_index_status reports has_index=false or the index is clearly out of date.";
                tool.inputSchema = schema::Object({
                    {"full_rebuild", "When true, clears the current index before indexing. Default: true.", schema::Boolean()},
                });
                tool.handler = [gui](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!gui)
                        return CallToolResult::Error("GUI not available");
                    if (gui->IsIndexing())
                        return CallToolResult::Json(nlohmann::json{{"status", "already_running"}});

                    const bool fullRebuild = args.value("full_rebuild", true);
                    gui->QueueMainThreadAction([gui, fullRebuild]()
                                               { gui->StartCodebaseIndexing(fullRebuild); });
                    return CallToolResult::Json(nlohmann::json{
                        {"status", "started"},
                        {"full_rebuild", fullRebuild},
                    });
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "cancel_codebase_index";
                tool.description = "Cancels a running rebuild_codebase_index job. "
                                   "Only call this if indexing is confirmed running via get_codebase_index_status and you need to abort it.";
                tool.inputSchema = schema::Object({});
                tool.handler = [gui](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!gui)
                        return CallToolResult::Error("GUI not available");

                    gui->QueueMainThreadAction([gui]()
                                               { gui->CancelCodebaseIndexing(); });
                    return CallToolResult::Json(nlohmann::json{{"status", "cancelling"}});
                };
                tools.push_back(std::move(tool));
            }
        }

        void AppendVisualTools(std::vector<ToolDefinition> &tools, EditorToolRuntime *runtime, GUI *gui)
        {
            {
                ToolDefinition tool;
                tool.name = "profiler_snapshot";
                tool.description = "Takes a snapshot of the current profiler data (Overview, CPU, GPU) and saves it as a JSON file "
                                   "to Assets/Profiler/profiler_snapshot_<datetime>.json. "
                                   "Returns the saved file path. "
                                   "Use this to capture frame timings, memory usage, GPU pass breakdown, and CPU scope timings.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->TakeProfilerSnapshot());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "take_screenshot";
                tool.description = "Captures the current editor frame and writes a downscaled PNG (max 1024 px, cursor overlay drawn) to disk. "
                                   "Returns metadata only: path, width, height, bytes, cursor_x, cursor_y, mime_type. The image is NOT embedded in the response. "
                                   "Pair with query_imgui_windows for layout, then click via inject_mouse_input.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->TakeScreenshot());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "query_imgui_windows";
                tool.description = "Returns all currently visible ImGui panels with their screen positions and sizes, "
                                   "plus 'tab_bars': an array of tab bars each listing every tab with its exact click coordinates. "
                                   "Use tab_bars to find precise pixel coordinates for clicking specific tabs. "
                                   "Use window positions for clicking inside panels.";
                tool.inputSchema = schema::Object({});
                tool.handler = [runtime](const nlohmann::json &, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->QueryImGuiWindows());
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "inject_mouse_input";
                tool.description = "Simulates mouse input to interact with editor UI elements. "
                                   "Actions: 'move', 'click', 'right_click', 'double_click', 'scroll'. "
                                   "Use u/v (normalized 0.0-1.0 fractions of screen width/height) when clicking from a screenshot - "
                                   "the tool converts to real pixel coordinates automatically. "
                                   "Use click_x/click_y from query_imgui_windows tab_bars directly (already real coords, no u/v needed). "
                                   "After clicking call take_screenshot to verify.";
                tool.inputSchema = schema::Object({
                    {"u", "Normalized horizontal position 0.0 (left) to 1.0 (right) - use when clicking from a screenshot", schema::Number()},
                    {"v", "Normalized vertical position 0.0 (top) to 1.0 (bottom) - use when clicking from a screenshot", schema::Number()},
                    {"x", "Real screen X in pixels - use only for coordinates from query_imgui_windows (tab_bars)", schema::Integer()},
                    {"y", "Real screen Y in pixels - use only for coordinates from query_imgui_windows (tab_bars)", schema::Integer()},
                    {"action", "Action: 'move', 'click', 'right_click', 'double_click', 'scroll'", schema::String(), true},
                    {"scroll_x", "Horizontal scroll delta for 'scroll' action", schema::Integer()},
                    {"scroll_y", "Vertical scroll delta for 'scroll' action (negative=down)", schema::Integer()},
                });
                tool.handler = [runtime](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!runtime)
                        return RuntimeUnavailable();
                    return RuntimeJsonResult(runtime->InjectMouseInput(args.dump()));
                };
                tools.push_back(std::move(tool));
            }

            {
                ToolDefinition tool;
                tool.name = "get_console_log";
                tool.description = "Returns recent log entries from the editor console (Info, Warn, Error). "
                                   "Use 'count' to limit how many recent entries to return (default 100). "
                                   "Use 'level' to filter by severity: 'info', 'warn', 'error', or 'all' (default).";
                tool.inputSchema = schema::Object({
                    {"count", "Maximum number of recent entries to return (default 100)", schema::Integer()},
                    {"level", "Filter by severity: 'info', 'warn', 'error', or 'all' (default)", schema::String()},
                });
                tool.handler = [gui](const nlohmann::json &args, Context &) -> CallToolResult
                {
                    if (!gui)
                        return CallToolResult::Error("GUI not available");
                    Console *console = gui->GetWidget<Console>();
                    if (!console)
                        return CallToolResult::Error("Console not available");

                    int count = args.value("count", 100);
                    std::string level = args.value("level", "all");

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
                    return CallToolResult::Json(nlohmann::json{
                        {"entries", std::move(entries)},
                        {"total", static_cast<int>(logs.size())},
                    });
                };
                tools.push_back(std::move(tool));
            }
        }
    } // namespace
} // namespace pe
