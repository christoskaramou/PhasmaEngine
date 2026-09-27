#include "Script/Bindings/Filesystem/FilesystemBindings.h"
#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "Base/GamePack.h"

namespace pe
{
    std::optional<std::string> ReadAssetsFile(const std::string &path)
    {
        if (path.empty())
            return std::nullopt;
        const std::filesystem::path fpath = ResolveAssetsPath(path);
        if (!IsUnderAssets(fpath))
            return std::nullopt;
        if (IsGamePackManagedAsset(fpath))
            return ReadGamePackAsset(fpath);
        if (!std::filesystem::exists(fpath))
            return std::nullopt;
        std::ifstream file(fpath, std::ios::in);
        if (!file.is_open())
            return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    bool WriteAssetsFile(const std::string &path, std::string_view content, bool append)
    {
        if (path.empty() || content.empty())
            return false;
        const std::filesystem::path fpath = ResolveAssetsPath(path);
        if (!IsUnderAssets(fpath) || IsGamePackManagedAsset(fpath))
            return false;
        const std::filesystem::path parentDir = fpath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        {
            std::error_code ec;
            std::filesystem::create_directories(parentDir, ec);
            if (ec)
                return false;
        }
        std::ofstream file(fpath, std::ios::out | (append ? std::ios::app : std::ios::trunc));
        if (!file.is_open())
            return false;
        file << content;
        return static_cast<bool>(file);
    }

    // Convert filesystem path to UTF-8 std::string safely on all platforms.
    // On Windows, path::string() throws if the path contains characters outside
    // the current ANSI code page; u8string() always works.
    static std::string pathToUtf8(const std::filesystem::path &p)
    {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    static struct FilesystemBindings
    {
        FilesystemBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua["assets_path"] = Path::Assets;

                sol::table fs = lua.create_named_table("fs");

                fs.set_function("find", [](const std::string &query, sol::optional<std::string> root, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    if (query.empty()) return result;

                    std::filesystem::path rootDir = root.has_value() ? ResolveAssetsPath(root.value()) : std::filesystem::path(Path::Assets);

                    if (!IsUnderAssets(rootDir))
                        return result;

                    const bool managedByPack = IsGamePackManagedAsset(rootDir);
                    if (!managedByPack && !std::filesystem::exists(rootDir))
                        return result;

                    std::string queryLower = query;
                    for (auto &c : queryLower)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    int i = 1;
                    if (!managedByPack)
                    {
                        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                                 rootDir, std::filesystem::directory_options::skip_permission_denied))
                        {
                            if (!entry.is_regular_file())
                                continue;

                            std::string nameLower = pathToUtf8(entry.path().filename());
                            for (auto &c : nameLower)
                                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            if (nameLower.find(queryLower) == std::string::npos)
                                continue;

                            std::string fullPath = pathToUtf8(entry.path());
                            std::replace(fullPath.begin(), fullPath.end(), '\\', '/');
                            result[i++] = fullPath;
                            if (i > 50) break;
                        }
                    }

                    for (const std::string &relative : ListGamePackAssets(rootDir))
                    {
                        std::string nameLower = std::filesystem::path(relative).filename().string();
                        for (auto &c : nameLower)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (nameLower.find(queryLower) == std::string::npos)
                            continue;
                        result[i++] = pathToUtf8(std::filesystem::path(Path::Assets) / relative);
                        if (i > 50) break;
                    }
                    return result;
                });

                fs.set_function("list", [](const std::string &path, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    if (path.empty()) return result;

                    std::filesystem::path fpath = ResolveAssetsPath(path);

                    if (!IsUnderAssets(fpath))
                        return result;

                    const bool managedByPack = IsGamePackManagedAsset(fpath);
                    std::set<std::string> fileNames;
                    std::set<std::string> dirNames;
                    if (!managedByPack && std::filesystem::is_directory(fpath))
                    {
                        for (const auto &entry : std::filesystem::directory_iterator(fpath))
                        {
                            std::string name = pathToUtf8(entry.path().filename());
                            if (entry.is_directory())
                                dirNames.insert(std::move(name));
                            else
                                fileNames.insert(std::move(name));
                        }
                    }

                    std::string relativeRoot = pathToUtf8(fpath.lexically_relative(Path::Assets));
                    std::replace(relativeRoot.begin(), relativeRoot.end(), '\\', '/');
                    if (relativeRoot == ".")
                        relativeRoot.clear();
                    for (const std::string &relative : ListGamePackAssets(fpath))
                    {
                        std::string remainder = relativeRoot.empty() ? relative : relative.substr(relativeRoot.size() + 1);
                        const size_t slash = remainder.find('/');
                        if (slash == std::string::npos)
                            fileNames.insert(std::move(remainder));
                        else
                            dirNames.insert(remainder.substr(0, slash));
                    }
                    if (fileNames.empty() && dirNames.empty())
                        return result;

                    sol::table files = lua.create_table();
                    sol::table dirs = lua.create_table();
                    int fi = 1;
                    for (const std::string &name : fileNames)
                        files[fi++] = name;
                    int di = 1;
                    for (const std::string &name : dirNames)
                        dirs[di++] = name;

                    std::string resolvedPath = pathToUtf8(fpath);
                    std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');
                    result["path"] = resolvedPath;
                    result["files"] = files;
                    result["dirs"] = dirs;
                    return result;
                });

                fs.set_function("read", [](const std::string &path) -> sol::optional<std::string> {
                    std::optional<std::string> content = ReadAssetsFile(path);
                    return content ? sol::optional<std::string>(std::move(*content)) : sol::nullopt;
                });

                fs.set_function("write", [](const std::string &path, const std::string &content, sol::optional<bool> append) -> bool {
                    return WriteAssetsFile(path, content, append.value_or(false));
                }); });
        }
    } s_filesystemBindings;
} // namespace pe
