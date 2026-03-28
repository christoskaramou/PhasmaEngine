#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"

namespace pe
{
    static struct ShaderFileBindings
    {
        ShaderFileBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table shaders = lua.create_named_table("shaders");

                shaders.set_function("list", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    std::string shadersDir = Path::Assets + "Shaders";
                    if (!std::filesystem::exists(shadersDir))
                        return result;

                    int i = 1;
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
                        result[i++] = relPath;
                    }
                    return result;
                });

                shaders.set_function("read", [](const std::string &path) -> sol::optional<std::string> {
                    if (path.empty()) return sol::nullopt;

                    std::filesystem::path fpath(path);
                    if (fpath.is_relative())
                        fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                    if (!IsUnderAssets(fpath))
                        return sol::nullopt;

                    if (!std::filesystem::exists(fpath))
                        return sol::nullopt;

                    std::ifstream file(fpath, std::ios::in);
                    if (!file.is_open())
                        return sol::nullopt;

                    return std::string(std::istreambuf_iterator<char>(file),
                                      std::istreambuf_iterator<char>());
                });

                shaders.set_function("edit", [](const std::string &path, const std::string &find, const std::string &replace) -> bool {
                    if (path.empty() || find.empty()) return false;

                    std::filesystem::path fpath(path);
                    if (fpath.is_relative())
                        fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                    if (!IsUnderAssets(fpath))
                        return false;

                    if (!std::filesystem::exists(fpath))
                        return false;

                    std::string source;
                    {
                        std::ifstream file(fpath, std::ios::in);
                        if (!file.is_open()) return false;
                        source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                    }

                    auto pos = source.find(find);
                    if (pos == std::string::npos)
                        return false;

                    source.replace(pos, find.size(), replace);

                    {
                        std::ofstream file(fpath, std::ios::out | std::ios::trunc);
                        if (!file.is_open()) return false;
                        file << source;
                    }

                    EventSystem::PushEvent(EventType::CompileShaders);
                    return true;
                });

                shaders.set_function("write", [](const std::string &path, const std::string &source) -> bool {
                    if (path.empty() || source.empty()) return false;

                    std::filesystem::path fpath(path);
                    if (fpath.is_relative())
                        fpath = std::filesystem::path(Path::Assets + "Shaders") / fpath;

                    if (!IsUnderAssets(fpath))
                        return false;

                    // Create parent directories if needed
                    std::filesystem::path parentDir = fpath.parent_path();
                    if (!parentDir.empty() && !std::filesystem::exists(parentDir))
                    {
                        std::error_code ec;
                        std::filesystem::create_directories(parentDir, ec);
                        if (ec) return false;
                    }

                    std::ofstream file(fpath, std::ios::out | std::ios::trunc);
                    if (!file.is_open()) return false;
                    file << source;
                    file.close();

                    std::string resolvedPath = fpath.string();
                    std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');

                    FileWatcher::Add(resolvedPath, [](size_t fileEvent) {
                        EventSystem::PushEvent(EventType::CompileShaders, fileEvent);
                    });
                    EventSystem::PushEvent(EventType::CompileShaders);
                    return true;
                }); });
        }
    } s_shaderFileBindings;
} // namespace pe
