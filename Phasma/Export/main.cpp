#include "Base/GamePack.h"
#include "Project/ProjectConfig.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
}

#include <fstream>
#include <iostream>
#include <unordered_set>

namespace
{
    struct Options
    {
        std::filesystem::path project;
        std::filesystem::path output;
        bool force = false;
    };

    std::string ToUtf8(const std::filesystem::path &path)
    {
        const auto value = path.generic_u8string();
        return std::string(value.begin(), value.end());
    }

    std::filesystem::path NormalizeAbsolute(const std::filesystem::path &path)
    {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec)
            return canonical.lexically_normal();
        ec.clear();
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        return (ec ? path : absolute).lexically_normal();
    }

    bool IsWithin(const std::filesystem::path &child, const std::filesystem::path &parent)
    {
        const std::filesystem::path relative = child.lexically_relative(parent);
        if (relative.empty())
            return child == parent;
        return !relative.is_absolute() && *relative.begin() != "..";
    }

    bool ParseOptions(int argc, char **argv, Options &options)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if ((arg == "--project" || arg == "-p") && i + 1 < argc)
                options.project = argv[++i];
            else if ((arg == "--output" || arg == "-o") && i + 1 < argc)
                options.output = argv[++i];
            else if (arg == "--force")
                options.force = true;
            else if (arg == "--help" || arg == "-h")
                return false;
            else
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
        return !options.project.empty() && !options.output.empty();
    }

    void PrintUsage()
    {
        std::cout << "Usage: PhasmaExport --project <project-dir> --output <export-dir> [--force]\n";
    }

    std::vector<uint8_t> ReadFile(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Failed to read " + ToUtf8(path));
        const std::streamoff size = file.tellg();
        if (size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max())
            throw std::runtime_error("File is too large: " + ToUtf8(path));
        file.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!data.empty() && !file.read(reinterpret_cast<char *>(data.data()), size))
            throw std::runtime_error("Failed to read " + ToUtf8(path));
        return data;
    }

    int DumpLua(lua_State *, const void *data, size_t size, void *output)
    {
        auto &bytes = *static_cast<std::vector<uint8_t> *>(output);
        const auto *begin = static_cast<const uint8_t *>(data);
        bytes.insert(bytes.end(), begin, begin + size);
        return 0;
    }

    std::vector<uint8_t> CompileLua(lua_State *lua,
                                    const std::filesystem::path &sourcePath,
                                    const std::string &packPath)
    {
        const std::vector<uint8_t> source = ReadFile(sourcePath);
        const std::string chunkName = "@" + packPath;
        if (luaL_loadbufferx(lua,
                             reinterpret_cast<const char *>(source.data()),
                             source.size(),
                             chunkName.c_str(),
                             "t") != LUA_OK)
        {
            const char *message = lua_tostring(lua, -1);
            const std::string error = message ? message : "unknown Lua compile error";
            lua_pop(lua, 1);
            throw std::runtime_error(error);
        }

        std::vector<uint8_t> bytecode;
        if (lua_dump(lua, DumpLua, &bytecode) != 0)
        {
            lua_pop(lua, 1);
            throw std::runtime_error("Failed to compile " + packPath);
        }
        lua_pop(lua, 1);
        return bytecode;
    }

    bool HasEditorOnlyMarker(const std::filesystem::path &path)
    {
        std::ifstream file(path);
        std::string line;
        for (int i = 0; i < 16 && std::getline(file, line); ++i)
        {
            if (line.find("phasma: editor-only") != std::string::npos)
                return true;
        }
        return false;
    }

    std::string NormalizeRule(std::string rule)
    {
        std::replace(rule.begin(), rule.end(), '\\', '/');
        while (!rule.empty() && (rule.back() == '/' || std::isspace(static_cast<unsigned char>(rule.back()))))
            rule.pop_back();
        size_t start = 0;
        while (start < rule.size() && std::isspace(static_cast<unsigned char>(rule[start])))
            ++start;
        rule.erase(0, start);
        if (rule.starts_with("./"))
            rule.erase(0, 2);
        return rule;
    }

    std::vector<std::string> ReadIgnoreRules(const std::filesystem::path &projectRoot)
    {
        std::vector<std::string> rules = {
            "Assets/Save",
            "Assets/Agent",
            "Assets/Scripts/Editor",
            "Assets/Scripts/tests",
        };

        std::ifstream file(projectRoot / ".phasmaexportignore");
        std::string line;
        while (std::getline(file, line))
        {
            line = NormalizeRule(std::move(line));
            if (line.empty() || line.front() == '#')
                continue;
            const std::filesystem::path rulePath(line);
            if (rulePath.is_absolute() || std::find(rulePath.begin(), rulePath.end(), "..") != rulePath.end())
                throw std::runtime_error("Invalid .phasmaexportignore entry: " + line);
            rules.push_back(std::move(line));
        }
        return rules;
    }

    bool IsIgnored(const std::string &path, const std::vector<std::string> &rules)
    {
        for (const std::string &rule : rules)
        {
            if (path == rule || (path.size() > rule.size() && path.starts_with(rule) && path[rule.size()] == '/'))
                return true;
        }
        return false;
    }

    bool IsRuntimeBinary(const std::string &name)
    {
        static const std::unordered_set<std::string> exact = {
            "PhasmaPlayer.exe",
            "PhasmaPlayer",
            "PhasmaCore.dll",
            "SDL2.dll",
            "shaderc_shared.dll",
            "dxcompiler.dll",
            "vulkan-1.dll",
        };
        return exact.contains(name) || name.starts_with("libPhasmaCore.so") || name.starts_with("libSDL2") ||
               name.starts_with("libshaderc_shared.so") || name.starts_with("libdxcompiler.so") ||
               name.starts_with("libvulkan.so");
    }

    // Path::Init picks Path::RuntimeAssets by DIRECTORY EXISTENCE and falls back to Assets/ when
    // RuntimeAssets/ is missing -- at which point every engine asset is looked up under the wrong
    // root, misses the pack, and the first shader compile kills the game. Both anchors are empty
    // directories, and an empty directory does not survive being zipped: Compress-Archive, GitHub's
    // "download ZIP" and plenty of others store files only. So each anchor gets a file in it, which
    // is what actually makes an exported game survive the trip to another machine.
    void WriteAnchor(const std::filesystem::path &dir)
    {
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "anchor.txt")
            << "This folder must exist for the game to find its assets. Do not delete it.\n"
               "It is otherwise empty; this file is here so the folder survives being zipped.\n";
    }

    void CopyRuntime(const std::filesystem::path &runtimeDir,
                     const std::filesystem::path &output,
                     lua_State *lua,
                     std::vector<pe::GamePackBuildEntry> &packEntries)
    {
        bool copiedPlayer = false;
        bool copiedCore = false;
        for (const auto &entry : std::filesystem::directory_iterator(runtimeDir))
        {
            if (!entry.is_regular_file() && !entry.is_symlink())
                continue;
            const std::string name = ToUtf8(entry.path().filename());
            if (!IsRuntimeBinary(name))
                continue;
            std::filesystem::copy_file(entry.path(), output / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
            copiedPlayer |= name == "PhasmaPlayer.exe" || name == "PhasmaPlayer";
            copiedCore |= name == "PhasmaCore.dll" || name.starts_with("libPhasmaCore.so");
        }
        if (!copiedPlayer || !copiedCore)
            throw std::runtime_error("PhasmaPlayer and PhasmaCore must be beside PhasmaExport");

        const std::filesystem::path runtimeAssets = runtimeDir / "RuntimeAssets";
        if (!std::filesystem::is_directory(runtimeAssets))
            throw std::runtime_error("RuntimeAssets must be beside PhasmaExport");
        for (const auto &entry : std::filesystem::recursive_directory_iterator(runtimeAssets))
        {
            if (!entry.is_regular_file())
                continue;
            const std::filesystem::path relative = entry.path().lexically_relative(runtimeAssets);
            const std::string relativePath = relative.generic_string();
            if (relativePath.starts_with("Scripts/tests/") ||
                (entry.path().extension() == ".lua" && HasEditorOnlyMarker(entry.path())))
            {
                continue;
            }
            pe::GamePackBuildEntry packedEntry;
            packedEntry.path = "RuntimeAssets/" + relativePath;
            packedEntry.data = entry.path().extension() == ".lua"
                                   ? CompileLua(lua, entry.path(), packedEntry.path)
                                   : ReadFile(entry.path());
            packEntries.push_back(std::move(packedEntry));
        }
        WriteAnchor(output / "RuntimeAssets");
    }

    void ExportAssets(const pe::ProjectConfig &project,
                      const std::filesystem::path &output,
                      lua_State *lua,
                      std::vector<pe::GamePackBuildEntry> &packEntries)
    {
        const std::filesystem::path assetsRoot = project.AssetsRoot();
        const std::vector<std::string> ignoreRules = ReadIgnoreRules(project.root);
        WriteAnchor(output / "Assets");

        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 assetsRoot, std::filesystem::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file())
                continue;

            const std::filesystem::path relative = entry.path().lexically_relative(assetsRoot);
            const std::string assetPath = "Assets/" + relative.generic_string();
            if (IsIgnored(assetPath, ignoreRules))
                continue;
            if (entry.path().extension() == ".lua" && HasEditorOnlyMarker(entry.path()))
                continue;

            const std::string packPath = relative.generic_string();
            pe::GamePackBuildEntry packedEntry;
            packedEntry.path = packPath;
            packedEntry.data = entry.path().extension() == ".lua"
                                   ? CompileLua(lua, entry.path(), packPath)
                                   : ReadFile(entry.path());
            packEntries.push_back(std::move(packedEntry));
        }
    }

    void WriteSettings(const std::filesystem::path &output)
    {
        std::ofstream file(output / "phasma_settings.json", std::ios::trunc);
        file << "{\n  \"project_manifest\": \"phasma_project.json\"\n}\n";
        if (!file)
            throw std::runtime_error("Failed to write phasma_settings.json");
    }
} // namespace

#ifdef main
#undef main
#endif

int main(int argc, char **argv)
{
    Options options;
    try
    {
        if (!ParseOptions(argc, argv, options))
        {
            PrintUsage();
            return argc > 1 ? 0 : 2;
        }

        options.project = NormalizeAbsolute(options.project);
        options.output = NormalizeAbsolute(options.output);
        const std::filesystem::path manifest = options.project / pe::kProjectManifestFileName;
        std::string error;
        const std::optional<pe::ProjectConfig> project = pe::ProjectConfig::TryLoadManifest(manifest, &error);
        if (!project)
            throw std::runtime_error(error.empty() ? "Invalid project manifest" : error);

        pe::Path::Init();
        const std::filesystem::path runtimeDir = NormalizeAbsolute(pe::Path::Executable);
        if (options.output == options.output.root_path() ||
            IsWithin(options.output, options.project) || IsWithin(options.project, options.output) ||
            IsWithin(options.output, runtimeDir) || IsWithin(runtimeDir, options.output))
        {
            throw std::runtime_error("Output must not overlap the project or engine build directory");
        }
        if (std::filesystem::exists(options.output) && !options.force)
            throw std::runtime_error("Output already exists; pass --force to replace it");

        std::filesystem::path temporary = options.output.parent_path() /
                                          (options.output.filename().string() + ".exporting");
        std::error_code ec;
        std::filesystem::remove_all(temporary, ec);
        std::filesystem::create_directories(temporary);

        try
        {
            std::unique_ptr<lua_State, decltype(&lua_close)> lua(luaL_newstate(), lua_close);
            if (!lua)
                throw std::runtime_error("Failed to create Lua compiler state");

            std::vector<pe::GamePackBuildEntry> packEntries;
            CopyRuntime(runtimeDir, temporary, lua.get(), packEntries);
            ExportAssets(*project, temporary, lua.get(), packEntries);
            std::sort(packEntries.begin(), packEntries.end(),
                      [](const auto &a, const auto &b)
                      { return a.path < b.path; });

            const std::filesystem::path packPath = temporary / pe::kGamePackFileName;
            if (!pe::WriteGamePack(packPath, packEntries, &error) || !pe::OpenGamePack(packPath, &error) ||
                pe::ListGamePackAssets().size() != packEntries.size())
            {
                throw std::runtime_error(error.empty() ? "Game pack verification failed" : error);
            }
            for (const auto &entry : packEntries)
            {
                const std::optional<std::string> data = pe::ReadGamePackAsset(entry.path);
                if (!data || data->size() != entry.data.size() ||
                    !std::equal(data->begin(), data->end(), entry.data.begin(),
                                [](char packed, uint8_t source)
                                { return static_cast<uint8_t>(packed) == source; }))
                {
                    throw std::runtime_error("Game pack verification failed for " + entry.path);
                }
            }
            pe::CloseGamePack();

            std::filesystem::copy_file(manifest, temporary / pe::kProjectManifestFileName,
                                       std::filesystem::copy_options::overwrite_existing);
            WriteSettings(temporary);

            if (std::filesystem::exists(options.output))
                std::filesystem::remove_all(options.output);
            std::filesystem::rename(temporary, options.output);

            std::cout << "Exported " << project->name << " to " << ToUtf8(options.output) << "\n"
                      << "  packed: " << packEntries.size() << " files\n";
        }
        catch (...)
        {
            pe::CloseGamePack();
            std::filesystem::remove_all(temporary, ec);
            throw;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Export failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
