#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include "Project/ProjectSelection.h"
#include "Runtime/RuntimeStartup.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#if defined(PE_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include "SDL_syswm.h"
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    constexpr const char *k_graphicsApiKey = "graphics_api";
    constexpr const char *k_launchTargetKey = "launch_target";
    constexpr const char *k_displayIndexKey = "display_index";
    constexpr const char *kVulkanValidationModeKey = "vulkan_validation_mode";
    constexpr const char *kDx12ValidationModeKey = "dx12_validation_mode";
    constexpr const char *kVulkanCoreValidationKey = "vulkan_core_validation";
    constexpr const char *kDx12CoreValidationKey = "dx12_core_validation";
    constexpr const char *kLiveProfilerKey = "live_profiler";
    constexpr const char *kLaunchArgumentsKey = "launch_arguments";
    constexpr const char *k_editorLaunchTarget = "PhasmaEditor";
    constexpr const char *k_playerLaunchTarget = "PhasmaPlayer";
    constexpr size_t kSettingsTextBufferSize = 64 * 1024;
    constexpr size_t kLaunchArgumentsBufferSize = 2048;

    bool PathElementEquals(const std::filesystem::path &a, const std::filesystem::path &b)
    {
#if defined(PE_WIN32)
        std::string left = a.generic_string();
        std::string right = b.generic_string();
        std::transform(left.begin(), left.end(), left.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        std::transform(right.begin(), right.end(), right.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return left == right;
#else
        return a == b;
#endif
    }

    bool IsPathInside(const std::filesystem::path &path, const std::filesystem::path &root)
    {
        const std::filesystem::path normalizedPath = path.lexically_normal();
        std::filesystem::path normalizedRoot = root.lexically_normal();
        if (normalizedRoot.filename().empty())
            normalizedRoot = normalizedRoot.parent_path();

        auto pathIt = normalizedPath.begin();
        auto rootIt = normalizedRoot.begin();
        for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt)
        {
            if (pathIt == normalizedPath.end() || !PathElementEquals(*pathIt, *rootIt))
                return false;
        }

        return true;
    }

    std::filesystem::path RuntimeSettingsPath()
    {
        return std::filesystem::path(pe::Path::Root) / pe::kRuntimeSettingsFileName;
    }

    std::filesystem::path EditorExecutablePath()
    {
#if defined(PE_WIN32)
        return std::filesystem::path(pe::Path::Root) / "PhasmaEditor.exe";
#else
        return std::filesystem::path(pe::Path::Root) / "PhasmaEditor";
#endif
    }

    std::filesystem::path PlayerExecutablePath()
    {
#if defined(PE_WIN32)
        return std::filesystem::path(pe::Path::Root) / "PhasmaPlayer.exe";
#else
        return std::filesystem::path(pe::Path::Root) / "PhasmaPlayer";
#endif
    }

    std::filesystem::path ProfilerExecutablePath()
    {
#if defined(PE_WIN32)
        return std::filesystem::path(pe::Path::Root) / "PhasmaProfiler.exe";
#else
        return std::filesystem::path(pe::Path::Root) / "PhasmaProfiler";
#endif
    }

    std::string EnsureTrailingSlash(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        if (!path.empty() && path.back() != '/')
            path.push_back('/');
        return path;
    }

    std::string NormalizeProjectPath(const std::filesystem::path &path)
    {
        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec)
            return EnsureTrailingSlash(path.generic_string());
        return EnsureTrailingSlash(absolutePath.generic_string());
    }

    std::optional<pe::ProjectConfig> TryLoadProjectAtRoot(const std::filesystem::path &projectRoot)
    {
        std::error_code ec;
        const std::filesystem::path manifestPath = pe::ProjectConfig::DefaultManifestPath(projectRoot);
        if (!std::filesystem::exists(manifestPath, ec))
            return std::nullopt;

        std::string error;
        std::optional<pe::ProjectConfig> project = pe::ProjectConfig::TryLoadManifest(manifestPath, &error);
        if (!project && !error.empty())
            PE_WARN("[Runtime] %s", error.c_str());
        return project;
    }

    bool PathEquivalent(const std::filesystem::path &a, const std::filesystem::path &b)
    {
        std::error_code ec;
        const std::filesystem::path absoluteA = std::filesystem::absolute(a, ec).lexically_normal();
        if (ec)
            return false;

        const std::filesystem::path absoluteB = std::filesystem::absolute(b, ec).lexically_normal();
        if (ec)
            return false;

        return IsPathInside(absoluteA, absoluteB) && IsPathInside(absoluteB, absoluteA);
    }

    std::optional<pe::ProjectConfig> FindContainingProject(const std::filesystem::path &path)
    {
        std::error_code ec;
        std::filesystem::path absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec)
            absolutePath = path.lexically_normal();

        std::filesystem::path cursor = std::filesystem::is_regular_file(absolutePath, ec)
                                           ? absolutePath.parent_path()
                                           : absolutePath;
        while (!cursor.empty())
        {
            if (std::optional<pe::ProjectConfig> project = TryLoadProjectAtRoot(cursor))
            {
                if (IsPathInside(absolutePath, project->AssetsRoot()))
                    return project;
            }

            const std::filesystem::path parent = cursor.parent_path();
            if (parent == cursor)
                break;
            cursor = parent;
        }

        return std::nullopt;
    }

    std::filesystem::path ProjectAssetsRoot(const std::string &projectPath)
    {
        if (std::optional<pe::ProjectConfig> project = TryLoadProjectAtRoot(projectPath))
            return project->AssetsRoot();

        return std::filesystem::path(projectPath);
    }

    std::string NormalizeConfiguredProjectPath(const std::filesystem::path &path)
    {
        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec)
            return NormalizeProjectPath(path);

        if (std::optional<pe::ProjectConfig> project = TryLoadProjectAtRoot(absolutePath))
            return NormalizeProjectPath(project->root);

        if (std::optional<pe::ProjectConfig> project = FindContainingProject(absolutePath))
        {
            if (PathEquivalent(absolutePath, project->AssetsRoot()))
                return NormalizeProjectPath(project->root);
        }

        return NormalizeProjectPath(absolutePath);
    }

    std::string InferProjectPathFromScene(const std::filesystem::path &scenePath)
    {
        if (std::optional<pe::ProjectConfig> project = FindContainingProject(scenePath))
            return NormalizeProjectPath(project->root);

        const std::filesystem::path parent = scenePath.parent_path();
        if (parent.filename() == "Scenes" && !parent.parent_path().empty())
            return NormalizeProjectPath(parent.parent_path());
        if (!parent.empty())
            return NormalizeProjectPath(parent);
        return NormalizeProjectPath(std::filesystem::path(pe::Path::Assets));
    }

    std::string MakeRuntimeRelativePath(const std::filesystem::path &path)
    {
        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec)
            return path.generic_string();

        const std::filesystem::path executablePath =
            std::filesystem::absolute(std::filesystem::path(pe::Path::Executable), ec).lexically_normal();
        if (ec || !IsPathInside(absolutePath, executablePath))
            return absolutePath.generic_string();

        const std::filesystem::path relative = std::filesystem::relative(absolutePath, executablePath, ec);
        if (ec || relative.empty())
            return absolutePath.generic_string();

        return relative.generic_string();
    }

    std::string MakeRootRelativeDisplayPath(const std::string &value, const std::string &root)
    {
        if (value.empty())
            return value;

        std::error_code ec;
        std::filesystem::path path(value);
        if (path.is_relative())
            path = std::filesystem::path(pe::Path::Executable) / path;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec)
            return value;

        std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec).lexically_normal();
        if (!ec && absoluteRoot.filename().empty())
            absoluteRoot = absoluteRoot.parent_path();
        if (ec || !IsPathInside(absolutePath, absoluteRoot))
            return value;

        const std::filesystem::path relative = std::filesystem::relative(absolutePath, absoluteRoot, ec);
        if (ec || relative.empty())
            return value;

        return relative.generic_string();
    }

    bool TryLoadJsonObject(const std::filesystem::path &path, rapidjson::Document &document, std::string &warning)
    {
        document.SetObject();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return true;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            warning = "Could not open " + path.string();
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();
        if (text.empty())
            return true;

        document.Parse(text.c_str(), text.size());
        if (document.HasParseError())
        {
            warning = "Could not parse " + path.string() + ": " +
                      rapidjson::GetParseError_En(document.GetParseError());
            document.SetObject();
            return false;
        }

        if (!document.IsObject())
        {
            warning = path.string() + " must contain a JSON object";
            document.SetObject();
            return false;
        }

        return true;
    }

    std::string ReadJsonStringField(const std::filesystem::path &path, const char *key)
    {
        rapidjson::Document document;
        std::string warning;
        if (!TryLoadJsonObject(path, document, warning))
        {
            if (!warning.empty())
                PE_WARN("%s", warning.c_str());
            return {};
        }

        if (!document.HasMember(key) || !document[key].IsString())
            return {};

        return document[key].GetString();
    }

    void SetJsonStringMember(rapidjson::Document &document, const char *key, const std::string &value)
    {
        rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
        rapidjson::Value jsonKey(key, allocator);
        rapidjson::Value jsonValue;
        jsonValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);

        if (document.HasMember(key))
            document[key] = jsonValue;
        else
            document.AddMember(jsonKey.Move(), jsonValue.Move(), allocator);
    }

    void SetJsonBoolMember(rapidjson::Document &document, const char *key, bool value)
    {
        rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
        rapidjson::Value jsonKey(key, allocator);
        rapidjson::Value jsonValue(value);

        if (document.HasMember(key))
            document[key] = jsonValue;
        else
            document.AddMember(jsonKey.Move(), jsonValue.Move(), allocator);
    }

    void SetJsonIntMember(rapidjson::Document &document, const char *key, int value)
    {
        rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
        rapidjson::Value jsonKey(key, allocator);
        rapidjson::Value jsonValue(value);

        if (document.HasMember(key))
            document[key] = jsonValue;
        else
            document.AddMember(jsonKey.Move(), jsonValue.Move(), allocator);
    }

    void RemoveJsonMember(rapidjson::Document &document, const char *key)
    {
        if (document.HasMember(key))
            document.RemoveMember(key);
    }

    bool WriteJsonObject(const std::filesystem::path &path, const rapidjson::Document &document, std::string &error)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Could not create " + path.parent_path().string() + ": " + ec.message();
            return false;
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            error = "Could not open " + path.string() + " for writing";
            return false;
        }

        rapidjson::OStreamWrapper stream(file);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(stream);
        writer.SetIndent(' ', 2);
        if (!document.Accept(writer))
        {
            error = "Could not serialize " + path.string();
            return false;
        }

        file << '\n';
        return true;
    }

    bool ReadTextFile(const std::filesystem::path &path, std::string &text, std::string &error)
    {
        text.clear();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            text = "{\n}\n";
            return true;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            error = "Could not open " + path.string();
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        text = buffer.str();
        if (text.empty())
            text = "{\n}\n";
        return true;
    }

    bool ValidateSettingsText(const std::string &text, std::string &error)
    {
        const auto firstContent = std::find_if(text.begin(), text.end(), [](unsigned char ch)
                                               { return !std::isspace(ch); });
        if (firstContent == text.end())
            return true;

        rapidjson::Document document;
        document.Parse(text.c_str(), text.size());
        if (document.HasParseError())
        {
            error = "Settings JSON parse error: " + std::string(rapidjson::GetParseError_En(document.GetParseError()));
            return false;
        }
        if (!document.IsObject())
        {
            error = "Settings JSON must be an object";
            return false;
        }
        return true;
    }

    bool WriteTextFile(const std::filesystem::path &path, const std::string &text, std::string &error)
    {
        if (!ValidateSettingsText(text, error))
            return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Could not create " + path.parent_path().string() + ": " + ec.message();
            return false;
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            error = "Could not open " + path.string() + " for writing";
            return false;
        }

        const std::string output = text.empty() ? "{\n}\n" : text;
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!output.empty() && output.back() != '\n')
            file << '\n';
        return true;
    }

    bool CopyTextToBuffer(std::vector<char> &buffer, const std::string &text)
    {
        if (buffer.empty())
            return false;

        std::fill(buffer.begin(), buffer.end(), '\0');
        const size_t copied = std::min(text.size(), buffer.size() - 1);
        std::memcpy(buffer.data(), text.data(), copied);
        return copied == text.size();
    }

    template <size_t Size>
    void CopyStringToArray(char (&buffer)[Size], const std::string &text)
    {
        static_assert(Size > 0);
        const size_t copied = std::min(text.size(), Size - 1);
        std::memcpy(buffer, text.data(), copied);
        buffer[copied] = '\0';
    }

    std::string BufferText(const std::vector<char> &buffer)
    {
        if (buffer.empty())
            return {};
        return std::string(buffer.data());
    }

    bool SaveSettingsBuffer(const std::vector<char> &buffer, std::string &error)
    {
        return WriteTextFile(RuntimeSettingsPath(), BufferText(buffer), error);
    }

    std::string ReadEditorConfigStartupScene()
    {
        return pe::ReadEditorStartupScene();
    }

    void AddUniqueScene(std::vector<std::string> &scenes,
                        const std::string &scene,
                        const std::string &projectPath)
    {
        const std::string displayPath = MakeRootRelativeDisplayPath(scene, projectPath);
        const auto duplicate = std::find_if(scenes.begin(), scenes.end(), [&](const std::string &candidate)
                                            { return MakeRootRelativeDisplayPath(candidate, projectPath) == displayPath; });
        if (duplicate == scenes.end())
            scenes.push_back(scene);
    }

    std::vector<std::string> DiscoverStartupScenes(const std::string &projectPath, const std::string &currentScene)
    {
        std::vector<std::string> scenes;
        AddUniqueScene(scenes, "", projectPath);
        if (!currentScene.empty())
            AddUniqueScene(scenes, currentScene, projectPath);

        std::filesystem::path scenesPath = ProjectAssetsRoot(projectPath) / "Scenes";
        std::error_code ec;
        if (!std::filesystem::exists(scenesPath, ec))
            return scenes;

        std::vector<std::string> discovered;
        for (const auto &entry : std::filesystem::directory_iterator(scenesPath, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".pescene")
                continue;
            discovered.push_back(MakeRuntimeRelativePath(entry.path()));
        }

        std::sort(discovered.begin(), discovered.end());
        for (const std::string &scene : discovered)
            AddUniqueScene(scenes, scene, projectPath);

        return scenes;
    }

    bool WriteEditorConfigStartupScene(const std::string &scene, std::string &error)
    {
        return pe::WriteEditorStartupScene({}, scene, &error);
    }

    struct PresentModeOption
    {
        const char *label = "";
        std::optional<PePresentMode> mode;
    };

    std::array<PresentModeOption, 5> PresentModeOptions()
    {
        return {{
            {"Default", std::nullopt},
            {"Immediate", PE_PRESENT_MODE_IMMEDIATE},
            {"Mailbox", PE_PRESENT_MODE_MAILBOX},
            {"FIFO", PE_PRESENT_MODE_FIFO},
            {"FIFO relaxed", PE_PRESENT_MODE_FIFO_RELAXED},
        }};
    }

    int FindPresentModeOptionIndex(const std::optional<PePresentMode> &mode)
    {
        const std::array<PresentModeOption, 5> options = PresentModeOptions();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            if (options[i].mode == mode)
                return i;
        }
        return 0;
    }

    struct GpuAdapterPreferenceOption
    {
        const char *label = "";
        pe::GpuAdapterPreference preference = pe::GpuAdapterPreference::Auto;
    };

    std::array<GpuAdapterPreferenceOption, 4> GpuAdapterPreferenceOptions()
    {
        return {{
            {"Auto", pe::GpuAdapterPreference::Auto},
            {"Integrated GPU", pe::GpuAdapterPreference::IntegratedGpu},
            {"Discrete GPU", pe::GpuAdapterPreference::DiscreteGpu},
            {"CPU / Software", pe::GpuAdapterPreference::Cpu},
        }};
    }

    int FindGpuAdapterPreferenceOptionIndex(pe::GpuAdapterPreference preference)
    {
        const std::array<GpuAdapterPreferenceOption, 4> options = GpuAdapterPreferenceOptions();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            if (options[i].preference == preference)
                return i;
        }
        return 0;
    }

    bool PersistEditorPresentMode(const std::optional<PePresentMode> &mode, std::string &error)
    {
        if (mode)
            return pe::WriteEditorPresentMode({}, *mode, &error);
        return pe::ClearEditorPresentMode({}, &error);
    }

    struct ValidationOptions
    {
        bool vulkanCoreValidation = false;
        bool dx12CoreValidation = false;
    };

    bool PersistLauncherSettings(PeGraphicsApi api,
                                 const std::string &projectPath,
                                 const std::string &startupScene,
                                 const std::string &launchTarget,
                                 const std::string &launchArguments,
                                 const ValidationOptions &validation,
                                 bool liveProfiler,
                                 int displayIndex,
                                 pe::GpuAdapterPreference gpuAdapterPreference,
                                 const std::optional<PePresentMode> &presentModeOverride,
                                 bool launchesEditor,
                                 std::string &error)
    {
        rapidjson::Document document;
        std::string warning;
        if (!TryLoadJsonObject(RuntimeSettingsPath(), document, warning) && !warning.empty())
            PE_WARN("%s; rewriting launcher settings", warning.c_str());

        SetJsonStringMember(document, k_graphicsApiKey, pe::GraphicsApiConfigName(api));
        SetJsonStringMember(document, pe::kProjectPathSettingsKey, projectPath);
        SetJsonStringMember(document, pe::kStartupSceneSettingsKey, startupScene);
        SetJsonStringMember(document, k_launchTargetKey, launchTarget.empty() ? k_editorLaunchTarget : launchTarget);
        SetJsonStringMember(document, kLaunchArgumentsKey, launchArguments);
        SetJsonIntMember(document, k_displayIndexKey, displayIndex < 0 ? 0 : displayIndex);
        SetJsonStringMember(document,
                            pe::kGpuAdapterPreferenceSettingsKey,
                            pe::GpuAdapterPreferenceConfigName(gpuAdapterPreference));
        SetJsonBoolMember(document, kVulkanCoreValidationKey, validation.vulkanCoreValidation);
        SetJsonBoolMember(document, kDx12CoreValidationKey, validation.dx12CoreValidation);
        SetJsonBoolMember(document, kLiveProfilerKey, liveProfiler);
        RemoveJsonMember(document, kVulkanValidationModeKey);
        RemoveJsonMember(document, kDx12ValidationModeKey);
        RemoveJsonMember(document, "vulkan_configurator_path");
        RemoveJsonMember(document, "dx12_configurator_path");

        const std::filesystem::path projectManifest = pe::ProjectConfig::DefaultManifestPath(projectPath);
        std::error_code ec;
        if (std::filesystem::exists(projectManifest, ec))
            SetJsonStringMember(document, pe::kProjectManifestSettingsKey, projectManifest.generic_string());
        else
            RemoveJsonMember(document, pe::kProjectManifestSettingsKey);

        if (!WriteJsonObject(RuntimeSettingsPath(), document, error))
            return false;

        if (!PersistEditorPresentMode(presentModeOverride, error))
            return false;

        if (launchesEditor && !WriteEditorConfigStartupScene(startupScene, error))
            return false;

        return true;
    }

    enum class LauncherTab
    {
        Editor,
        Player
    };

    enum class LaunchTargetKind
    {
        Editor,
        Player,
        Sample
    };

    struct LaunchProfile
    {
        std::string projectPath;
        std::string startupScene;
        std::vector<std::string> startupScenes;
    };

    struct LauncherSelection
    {
        PeGraphicsApi api = PE_GRAPHICS_API_VULKAN;
        LauncherTab activeTab = LauncherTab::Editor;
        LaunchProfile editor;
        LaunchProfile player;
        ValidationOptions validation;
        std::string launchTarget;
        char launchArguments[kLaunchArgumentsBufferSize] = {};
        int displayIndex = 0;
        pe::GpuAdapterPreference gpuAdapterPreference = pe::GpuAdapterPreference::Auto;
        std::optional<PePresentMode> presentModeOverride;
        bool liveProfiler = false;
        bool apiLocked = false;
        bool accepted = false;
    };

    struct LaunchTarget
    {
        std::string configValue;
        std::string label;
        std::filesystem::path executablePath;
        LaunchTargetKind kind = LaunchTargetKind::Sample;
    };

    enum class LauncherDialogResult
    {
        Launch,
        Cancel,
        Error
    };

    enum class BrowseDialogKind
    {
        None,
        Project,
        StartupScene,
        SettingsFile
    };

    struct BrowseDialogState
    {
        BrowseDialogKind kind = BrowseDialogKind::None;
        LaunchProfile *profile = nullptr;
        std::filesystem::path currentDirectory;
        std::filesystem::path selectedPath;
        bool selectedPathIsDirectory = false;
        bool directoriesOnly = false;
        bool openRequested = false;
        bool closeRequested = false;
        const char *extensionFilter = nullptr;
        char pathBuffer[2048] = {};
        std::string error;
    };

    struct BrowseEntry
    {
        std::filesystem::path path;
        std::string label;
        bool isDirectory = false;
    };

    // Defined further down; forward-declared so the non-Windows browse buttons
    // (RenderProjectPanel/RenderStartupSceneControls) can open the dialog.
    void OpenBrowseDialog(BrowseDialogState &dialog,
                          BrowseDialogKind kind,
                          LaunchProfile *profile,
                          const std::filesystem::path &initialDirectory,
                          bool directoriesOnly,
                          const char *extensionFilter);

    std::string SceneDisplayName(const LaunchProfile &profile, const std::string &scene)
    {
        if (scene.empty())
            return "Empty scene";
        return MakeRootRelativeDisplayPath(scene, profile.projectPath);
    }

    bool IsExecutableFile(const std::filesystem::path &path)
    {
#if defined(PE_WIN32)
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return extension == ".exe";
#else
        std::error_code ec;
        const auto status = std::filesystem::status(path, ec);
        if (ec || !std::filesystem::is_regular_file(status))
            return false;
        const auto permissions = status.permissions();
        return (permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
#endif
    }

    bool IsDiscoverableSampleExecutable(const std::filesystem::path &path)
    {
        if (!IsExecutableFile(path))
            return false;

        const std::string stem = path.stem().string();
        return stem.rfind("WebGPU", 0) == 0;
    }

    std::vector<std::filesystem::path> DiscoverTargetDirectories()
    {
        std::vector<std::filesystem::path> directories;
        std::filesystem::path runtimePath = std::filesystem::path(pe::Path::Root);
        if (runtimePath.filename().empty())
            runtimePath = runtimePath.parent_path();
        runtimePath = runtimePath.lexically_normal();
        directories.push_back((runtimePath / "Samples" / "WebGPU").lexically_normal());
        directories.push_back(runtimePath);

        return directories;
    }

    void AddSampleTargetsFromDirectory(std::vector<LaunchTarget> &targets,
                                       std::vector<std::string> &seenNames,
                                       const std::filesystem::path &directory)
    {
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec))
            return;

        for (const auto &entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || !IsDiscoverableSampleExecutable(entry.path()))
                continue;

            const std::string fileName = entry.path().filename().generic_string();
            if (std::find(seenNames.begin(), seenNames.end(), fileName) != seenNames.end())
                continue;

            seenNames.push_back(fileName);
            targets.push_back({fileName, entry.path().stem().string(), entry.path().lexically_normal(), LaunchTargetKind::Sample});
        }
    }

    std::vector<LaunchTarget> DiscoverLaunchTargets(const std::string &preferredTarget)
    {
        std::vector<LaunchTarget> targets;
        targets.push_back({k_editorLaunchTarget, "Phasma Editor", EditorExecutablePath(), LaunchTargetKind::Editor});
        targets.push_back({k_playerLaunchTarget, "Phasma Player", PlayerExecutablePath(), LaunchTargetKind::Player});

        std::vector<LaunchTarget> sampleTargets;
        std::vector<std::string> seenNames;
        for (const std::filesystem::path &directory : DiscoverTargetDirectories())
            AddSampleTargetsFromDirectory(sampleTargets, seenNames, directory);

        std::sort(sampleTargets.begin(),
                  sampleTargets.end(),
                  [](const LaunchTarget &a, const LaunchTarget &b)
                  {
                      return a.label < b.label;
                  });
        targets.insert(targets.end(), sampleTargets.begin(), sampleTargets.end());

        if (!preferredTarget.empty())
        {
            const auto found = std::find_if(targets.begin(),
                                            targets.end(),
                                            [&preferredTarget](const LaunchTarget &target)
                                            {
                                                return target.configValue == preferredTarget;
                                            });
            if (found == targets.end())
            {
                std::filesystem::path preferredPath(preferredTarget);
                if (preferredPath.is_relative())
                    preferredPath = std::filesystem::path(pe::Path::Root) / preferredPath;

                std::error_code ec;
                if (std::filesystem::exists(preferredPath, ec) && IsExecutableFile(preferredPath))
                {
                    targets.push_back(
                        {preferredTarget, preferredPath.stem().string(), preferredPath.lexically_normal(), LaunchTargetKind::Sample});
                }
            }
        }

        return targets;
    }

    int FindLaunchTargetIndex(const std::vector<LaunchTarget> &targets, const std::string &value)
    {
        for (size_t i = 0; i < targets.size(); ++i)
        {
            if (targets[i].configValue == value)
                return static_cast<int>(i);
        }
        return 0;
    }

    bool LaunchesEditor(const LauncherSelection &selection)
    {
        return selection.activeTab == LauncherTab::Editor;
    }

    bool LaunchesProjectScene(const LaunchTarget &target)
    {
        return target.kind == LaunchTargetKind::Editor || target.kind == LaunchTargetKind::Player;
    }

    LaunchProfile &ActiveProfile(LauncherSelection &selection)
    {
        return LaunchesEditor(selection) ? selection.editor : selection.player;
    }

    const LaunchProfile &ActiveProfile(const LauncherSelection &selection)
    {
        return LaunchesEditor(selection) ? selection.editor : selection.player;
    }

    // Validate the selection before launching. Requires a runnable target, and for
    // project/scene targets (editor/player): a project folder that exists and holds a
    // phasma_project.json, plus a startup scene that resolves to a real file (an empty
    // startup scene is allowed — the runtime falls back to the manifest's scene).
    bool ValidateLaunchSelection(const LauncherSelection &selection,
                                 const std::vector<LaunchTarget> &targets,
                                 std::string &error)
    {
        const int index = FindLaunchTargetIndex(targets, selection.launchTarget);
        if (index < 0)
        {
            error = "Select something to run.";
            return false;
        }

        std::error_code ec;
        const LaunchTarget &target = targets[index];
        if (!std::filesystem::exists(target.executablePath, ec))
        {
            error = "Run target not found: " + target.executablePath.string();
            return false;
        }
        if (!LaunchesProjectScene(target))
            return true;

        const LaunchProfile &profile = ActiveProfile(selection);
        if (profile.projectPath.empty())
        {
            error = "Select a project folder.";
            return false;
        }
        const std::filesystem::path projectRoot(profile.projectPath);
        if (!std::filesystem::exists(projectRoot, ec))
        {
            error = "Project folder does not exist: " + profile.projectPath;
            return false;
        }
        if (!std::filesystem::exists(projectRoot / pe::kProjectManifestFileName, ec))
        {
            error = "Not a project (missing " + std::string(pe::kProjectManifestFileName) + "): " + profile.projectPath;
            return false;
        }

        if (!profile.startupScene.empty())
        {
            const std::filesystem::path scene(profile.startupScene);
            const bool found = scene.is_absolute()
                                   ? std::filesystem::exists(scene, ec)
                                   : (std::filesystem::exists(std::filesystem::path(pe::Path::Executable) / scene, ec) ||
                                      std::filesystem::exists(projectRoot / scene, ec));
            if (!found)
            {
                error = "Startup scene not found: " + profile.startupScene;
                return false;
            }
        }
        return true;
    }

    bool ParseLaunchArguments(const std::string &text,
                              std::vector<std::string> &arguments,
                              std::string &error)
    {
        enum class Quote
        {
            None,
            Single,
            Double
        };

        arguments.clear();
        std::string argument;
        Quote quote = Quote::None;
        bool started = false;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char ch = text[i];
            const bool matchingQuote =
                (quote == Quote::Single && ch == '\'') || (quote == Quote::Double && ch == '"');
            if (matchingQuote)
            {
                quote = Quote::None;
                started = true;
                continue;
            }
            if (quote == Quote::None && (ch == '\'' || ch == '"'))
            {
                quote = ch == '\'' ? Quote::Single : Quote::Double;
                started = true;
                continue;
            }
            if (ch == '\\' && quote != Quote::Single && i + 1 < text.size() &&
                (text[i + 1] == '\\' || text[i + 1] == '"' || text[i + 1] == '\''))
            {
                argument.push_back(text[++i]);
                started = true;
                continue;
            }
            if (quote == Quote::None && std::isspace(static_cast<unsigned char>(ch)))
            {
                if (started)
                {
                    arguments.push_back(argument);
                    argument.clear();
                    started = false;
                }
                continue;
            }
            argument.push_back(ch);
            started = true;
        }

        if (quote != Quote::None)
        {
            error = "Launch parameters contain an unterminated quote.";
            return false;
        }
        if (started)
            arguments.push_back(argument);
        return true;
    }

#if defined(PE_WIN32)
    std::string QuoteCommandLineArg(const std::string &arg)
    {
        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos)
            return arg;

        std::string quoted = "\"";
        size_t slashCount = 0;
        for (char ch : arg)
        {
            if (ch == '\\')
            {
                ++slashCount;
            }
            else if (ch == '"')
            {
                quoted.append(slashCount * 2 + 1, '\\');
                quoted.push_back(ch);
                slashCount = 0;
            }
            else
            {
                quoted.append(slashCount, '\\');
                slashCount = 0;
                quoted.push_back(ch);
            }
        }
        quoted.append(slashCount * 2, '\\');
        quoted.push_back('"');
        return quoted;
    }
#endif

    bool SetLaunchEnvironmentFlag(const char *name, bool enabled, std::string &error)
    {
        const char *value = enabled ? "1" : "0";
#if defined(PE_WIN32)
        if (!SetEnvironmentVariableA(name, value))
        {
            error = std::string("Could not set ") + name + ": " + std::to_string(GetLastError());
            return false;
        }
#else
        if (setenv(name, value, 1) != 0)
        {
            error = std::string("Could not set ") + name;
            return false;
        }
#endif
        return true;
    }

    bool SetLaunchEnvironmentString(const char *name, const char *value, std::string &error)
    {
#if defined(PE_WIN32)
        if (!SetEnvironmentVariableA(name, value))
        {
            error = std::string("Could not set ") + name + ": " + std::to_string(GetLastError());
            return false;
        }
#else
        if (setenv(name, value, 1) != 0)
        {
            error = std::string("Could not set ") + name;
            return false;
        }
#endif
        return true;
    }

    bool ApplyValidationEnvironment(const ValidationOptions &validation, std::string &error)
    {
        if (!SetLaunchEnvironmentFlag("PE_VULKAN_VALIDATION", validation.vulkanCoreValidation, error))
            return false;
        if (!SetLaunchEnvironmentFlag("PE_DX12_DEBUG", validation.dx12CoreValidation, error))
            return false;
        if (!SetLaunchEnvironmentFlag("PE_DX12_GBV", validation.dx12CoreValidation, error))
            return false;
        if (!SetLaunchEnvironmentFlag("PE_DX12_DRED", validation.dx12CoreValidation, error))
            return false;
        return true;
    }

    bool ApplyGpuAdapterEnvironment(pe::GpuAdapterPreference preference, std::string &error)
    {
        return SetLaunchEnvironmentString(pe::kGpuAdapterPreferenceEnvVar,
                                          pe::GpuAdapterPreferenceConfigName(preference),
                                          error);
    }

    bool LaunchExternalTarget(const LaunchTarget &target,
                              PeGraphicsApi api,
                              int displayIndex,
                              bool liveProfiler,
                              const std::string &launchArguments,
                              std::string &error)
    {
        const std::filesystem::path executablePath = target.executablePath.lexically_normal();
        if (!std::filesystem::exists(executablePath))
        {
            error = "Launch target not found: " + executablePath.string();
            return false;
        }

        const std::string displayValue = std::to_string(displayIndex < 0 ? 0 : displayIndex);
        const bool playerProfiler = liveProfiler && target.kind == LaunchTargetKind::Player;
        std::vector<std::string> arguments = {
            "--api", pe::GraphicsApiConfigName(api), "--display", displayValue};
        if (playerProfiler)
            arguments.emplace_back("--profiler");
        std::vector<std::string> extraArguments;
        if (!ParseLaunchArguments(launchArguments, extraArguments, error))
            return false;
        arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());

#if defined(PE_WIN32)
        std::string commandLine = QuoteCommandLineArg(executablePath.string());
        for (const std::string &argument : arguments)
            commandLine += " " + QuoteCommandLineArg(argument);

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::string mutableCommandLine = commandLine;
        const std::string workingDirectory = std::filesystem::path(pe::Path::Root).string();
        if (!CreateProcessA(nullptr,
                            mutableCommandLine.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            0,
                            nullptr,
                            workingDirectory.c_str(),
                            &startupInfo,
                            &processInfo))
        {
            error = "Could not launch " + executablePath.string() + ": " + std::to_string(GetLastError());
            return false;
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        if (playerProfiler)
        {
            const std::filesystem::path profilerPath = ProfilerExecutablePath().lexically_normal();
            if (std::filesystem::exists(profilerPath))
            {
                STARTUPINFOA profilerStartup{};
                profilerStartup.cb = sizeof(profilerStartup);
                PROCESS_INFORMATION profilerInfo{};
                std::string profilerCommand = QuoteCommandLineArg(profilerPath.string());
                if (CreateProcessA(nullptr,
                                   profilerCommand.data(),
                                   nullptr,
                                   nullptr,
                                   FALSE,
                                   0,
                                   nullptr,
                                   workingDirectory.c_str(),
                                   &profilerStartup,
                                   &profilerInfo))
                {
                    CloseHandle(profilerInfo.hThread);
                    CloseHandle(profilerInfo.hProcess);
                }
            }
        }
        return true;
#else
        const pid_t pid = fork();
        if (pid < 0)
        {
            error = "Could not fork launcher process";
            return false;
        }
        if (pid == 0)
        {
            const std::filesystem::path workingDirectory = pe::Path::Root;
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                _exit(127);
            std::vector<std::string> argumentStorage;
            argumentStorage.reserve(arguments.size() + 1);
            argumentStorage.push_back(executablePath.filename().string());
            argumentStorage.insert(argumentStorage.end(), arguments.begin(), arguments.end());
            std::vector<char *> argv;
            argv.reserve(argumentStorage.size() + 1);
            for (std::string &argument : argumentStorage)
                argv.push_back(argument.data());
            argv.push_back(nullptr);
            execv(executablePath.c_str(), argv.data());
            _exit(127);
        }

        if (playerProfiler)
        {
            const std::filesystem::path profilerPath = ProfilerExecutablePath().lexically_normal();
            if (std::filesystem::exists(profilerPath))
            {
                const pid_t profilerPid = fork();
                if (profilerPid == 0)
                {
                    const std::filesystem::path workingDirectory = pe::Path::Root;
                    if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                        _exit(127);
                    execl(profilerPath.c_str(), profilerPath.filename().c_str(), nullptr);
                    _exit(127);
                }
            }
        }
        return true;
#endif
    }

    // Locate tools/new_game.py by walking up from the launcher executable. Dev
    // builds live at <repo>/build*/<config>/PhasmaLauncher, so the engine tools/
    // dir is a couple of levels up. Returns an empty path if not found.
    std::filesystem::path FindNewGameScript()
    {
        std::error_code ec;
        std::filesystem::path probe = std::filesystem::path(pe::Path::Root).lexically_normal();
        for (int i = 0; i < 8 && !probe.empty(); ++i)
        {
            const std::filesystem::path candidate = probe / "tools" / "new_game.py";
            if (std::filesystem::exists(candidate, ec))
                return candidate;
            const std::filesystem::path parent = probe.parent_path();
            if (parent == probe)
                break;
            probe = parent;
        }
        return {};
    }

    // Run tools/new_game.py synchronously to scaffold a mini-game project. Mirrors
    // the cross-platform launch split used by LaunchExternalTarget: CreateProcess +
    // wait on Windows, fork/exec + waitpid on POSIX (so it works on Linux/WSL).
    // The interpreter is taken from PATH ("python" on Windows, "python3" elsewhere).
    bool RunNewGameGenerator(const std::string &name,
                             const std::string &templateName,
                             const std::string &projectsDir,
                             std::string &error)
    {
        const std::filesystem::path script = FindNewGameScript();
        if (script.empty())
        {
            error = "Could not locate tools/new_game.py (run the launcher from the engine tree).";
            return false;
        }

#if defined(PE_WIN32)
        const std::string commandLine =
            std::string("python ") + QuoteCommandLineArg(script.string()) +
            " --name " + QuoteCommandLineArg(name) +
            " --template " + QuoteCommandLineArg(templateName) +
            " --dir " + QuoteCommandLineArg(projectsDir);

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::string mutableCommandLine = commandLine;
        if (!CreateProcessA(nullptr,
                            mutableCommandLine.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW,
                            nullptr,
                            nullptr,
                            &startupInfo,
                            &processInfo))
        {
            error = "Could not run python (is it on PATH?): " + std::to_string(GetLastError());
            return false;
        }

        WaitForSingleObject(processInfo.hProcess, 120000);
        DWORD exitCode = 1;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        if (exitCode != 0)
        {
            error = "new_game.py failed (exit " + std::to_string(exitCode) +
                    "); check the name/folder and that Python is installed.";
            return false;
        }
        return true;
#else
        const pid_t pid = fork();
        if (pid < 0)
        {
            error = "Could not fork generator process";
            return false;
        }
        if (pid == 0)
        {
            // NOAIKIDO: fixed executable and argv are passed directly; no shell command is constructed.
            execlp("python3",
                   "python3",
                   script.c_str(),
                   "--name",
                   name.c_str(),
                   "--template",
                   templateName.c_str(),
                   "--dir",
                   projectsDir.c_str(),
                   nullptr);
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            error = "new_game.py failed; check the name/folder and that python3 is installed.";
            return false;
        }
        return true;
#endif
    }

    // A minimal but valid startup scene for a blank project: one perspective
    // camera looking toward the origin and one directional sun, no meshes. The
    // loader treats settings/cameras/active_camera as optional, and a camera
    // authored as a node (component_flags 8 + camera{}) is what it actually uses.
    constexpr const char *kEmptySceneJson = R"JSON({
    "sources": [],
    "meshes": [],
    "nodes": [
        {
            "name": "Camera_0",
            "parent": -1,
            "local_matrix": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 3.0, 8.0, 1.0],
            "component_flags": 8,
            "camera": {
                "projection": "perspective",
                "fovx": 1.2,
                "near_plane": 0.1,
                "far_plane": 1000.0,
                "speed": 3.5,
                "euler": [0.3, 3.14159, 0.0]
            }
        },
        {
            "name": "Sun",
            "parent": -1,
            "local_matrix": [1.0, 0.0, 0.0, 0.0, 0.0, -0.001745, -0.999997, 0.0, 0.0, 0.999997, -0.001745, 0.0, 0.0, 30.0, 0.0, 1.0],
            "component_flags": 2,
            "light": { "type": "directional", "color": [1.0, 0.97, 0.9, 2.6] }
        }
    ],
    "lights": [],
    "cameras": [
        {
            "name": "Camera_0",
            "position": [0.0, 3.0, 8.0],
            "euler": [0.3, 3.14159, 0.0],
            "fovx": 1.2,
            "projection": "perspective",
            "near_plane": 0.1,
            "far_plane": 1000.0,
            "speed": 3.5,
            "node_index": 0
        }
    ],
    "active_camera": 0,
    "scene_scripts": { "on_play": [] }
}
)JSON";

    // Create a blank project natively (no Python) AT the given project root: the
    // phasma_project.json manifest via the shared ProjectConfig writer, plus the
    // minimal startup scene above. `name` is the manifest display name.
    bool CreateEmptyProject(const std::string &name, const std::filesystem::path &projectRoot, std::string &error)
    {
        std::error_code ec;
        if (std::filesystem::exists(projectRoot / pe::kProjectManifestFileName, ec))
        {
            error = "A project already exists at " + projectRoot.string();
            return false;
        }
        const std::filesystem::path scenesDir = projectRoot / "Assets" / "Scenes";
        std::filesystem::create_directories(scenesDir, ec);
        if (ec)
        {
            error = "Could not create project folders: " + ec.message();
            return false;
        }

        pe::ProjectConfig config;
        config.name = name;
        config.root = projectRoot;
        config.assetsDirectory = "Assets";
        config.startupScene = "Assets/Scenes/main.pescene";
        if (!config.WriteManifest(pe::ProjectConfig::DefaultManifestPath(projectRoot), &error))
            return false;

        const std::filesystem::path scenePath = scenesDir / "main.pescene";
        std::ofstream sceneFile(scenePath, std::ios::binary | std::ios::trunc);
        if (!sceneFile.is_open())
        {
            error = "Could not write " + scenePath.string();
            return false;
        }
        sceneFile << kEmptySceneJson;
        if (!sceneFile.good())
        {
            error = "Failed writing " + scenePath.string();
            return false;
        }
        return true;
    }

    struct DisplayOption
    {
        int index = 0;
        std::string label;
    };

    std::vector<DisplayOption> EnumerateDisplays()
    {
        std::vector<DisplayOption> displays;
        const int count = SDL_GetNumVideoDisplays();
        for (int i = 0; i < count; ++i)
        {
            std::string label = std::to_string(i) + ": ";
            const char *name = SDL_GetDisplayName(i);
            label += (name && name[0] != '\0') ? name : "Display";

            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(i, &bounds) == 0)
                label += " (" + std::to_string(bounds.w) + "x" + std::to_string(bounds.h) + ")";

            displays.push_back({i, std::move(label)});
        }
        return displays;
    }

    constexpr int kLauncherWidth = 1120;
    constexpr int kLauncherHeight = 700;
    constexpr int kFieldX = 140;
    constexpr int kFieldWidth = 930;
    constexpr int kSceneComboWidth = 810;
    constexpr int kSettingsPathWidth = 660;
    constexpr int kLaunchButtonX = 878;

    void RenderHelpLabel(const char *label, const char *explanation)
    {
        ImGui::TextUnformatted(label);
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            return;

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(explanation);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    void ApplyLauncherStyle()
    {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.WindowPadding = ImVec2(18.0f, 18.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(10.0f, 10.0f);

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.15f, 1.0f);
        colors[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.91f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.60f, 0.62f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.30f, 0.32f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.35f, 0.38f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.24f, 0.27f, 0.30f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.35f, 0.39f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.50f, 0.42f, 1.0f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.78f, 0.64f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.24f, 0.29f, 0.31f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.41f, 0.39f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.55f, 0.46f, 1.0f);
    }

    bool CreateFontTexture(SDL_Renderer *renderer, SDL_Texture *&fontTexture, std::string &error)
    {
        ImGuiIO &io = ImGui::GetIO();
        const std::filesystem::path fontPath = std::filesystem::path(pe::Path::ResolveAsset("Fonts/Inter-Regular.ttf"));
        if (std::filesystem::exists(fontPath))
            io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 15.0f);

        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        fontTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!fontTexture)
        {
            error = std::string("[SDL] could not create launcher font texture: ") + SDL_GetError();
            return false;
        }

        SDL_UpdateTexture(fontTexture, nullptr, pixels, width * 4);
        SDL_SetTextureBlendMode(fontTexture, SDL_BLENDMODE_BLEND);
        io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(fontTexture)));
        return true;
    }

    void RenderImGuiDrawData(SDL_Renderer *renderer, ImDrawData *drawData)
    {
        const ImVec2 clipOffset = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;
        SDL_RenderSetScale(renderer, clipScale.x, clipScale.y);

        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
        {
            const ImDrawList *cmdList = drawData->CmdLists[listIndex];
            std::vector<SDL_Vertex> vertices;
            vertices.reserve(cmdList->VtxBuffer.Size);

            for (const ImDrawVert &vertex : cmdList->VtxBuffer)
            {
                const ImU32 color = vertex.col;
                SDL_Vertex out{};
                out.position = {vertex.pos.x - clipOffset.x, vertex.pos.y - clipOffset.y};
                out.tex_coord = {vertex.uv.x, vertex.uv.y};
                out.color = {static_cast<Uint8>(color & 0xFF),
                             static_cast<Uint8>((color >> 8) & 0xFF),
                             static_cast<Uint8>((color >> 16) & 0xFF),
                             static_cast<Uint8>((color >> 24) & 0xFF)};
                vertices.push_back(out);
            }

            for (const ImDrawCmd &cmd : cmdList->CmdBuffer)
            {
                if (cmd.UserCallback)
                {
                    cmd.UserCallback(cmdList, &cmd);
                    continue;
                }

                SDL_Rect clipRect{};
                clipRect.x = static_cast<int>(cmd.ClipRect.x - clipOffset.x);
                clipRect.y = static_cast<int>(cmd.ClipRect.y - clipOffset.y);
                clipRect.w = static_cast<int>(cmd.ClipRect.z - cmd.ClipRect.x);
                clipRect.h = static_cast<int>(cmd.ClipRect.w - cmd.ClipRect.y);
                if (clipRect.w <= 0 || clipRect.h <= 0)
                    continue;

                std::vector<int> indices;
                indices.reserve(cmd.ElemCount);
                for (unsigned int i = 0; i < cmd.ElemCount; ++i)
                {
                    const ImDrawIdx index = cmdList->IdxBuffer[cmd.IdxOffset + i];
                    indices.push_back(static_cast<int>(index + cmd.VtxOffset));
                }

                SDL_Texture *texture = reinterpret_cast<SDL_Texture *>(static_cast<intptr_t>(cmd.GetTexID()));
                SDL_RenderSetClipRect(renderer, &clipRect);
                SDL_RenderGeometry(renderer,
                                   texture,
                                   vertices.data(),
                                   static_cast<int>(vertices.size()),
                                   indices.data(),
                                   static_cast<int>(indices.size()));
            }
        }

        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    }

#if defined(PE_WIN32)
    HWND GetNativeWindowHandle(SDL_Window *window)
    {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) == SDL_TRUE && info.subsystem == SDL_SYSWM_WINDOWS)
            return info.info.win.window;
        return nullptr;
    }
#endif

    bool PickFile(SDL_Window *owner,
                  const char *title,
                  const char *filter,
                  const std::filesystem::path &initialDir,
                  std::string &selectedPath,
                  std::string &error)
    {
#if defined(PE_WIN32)
        char fileName[MAX_PATH] = {};
        const std::string initialDirString = initialDir.string();

        OPENFILENAMEA openFileName{};
        openFileName.lStructSize = sizeof(openFileName);
        openFileName.hwndOwner = GetNativeWindowHandle(owner);
        openFileName.lpstrTitle = title;
        openFileName.lpstrFilter = filter;
        openFileName.lpstrFile = fileName;
        openFileName.nMaxFile = static_cast<DWORD>(sizeof(fileName));
        openFileName.lpstrInitialDir = initialDirString.c_str();
        openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&openFileName))
        {
            selectedPath = fileName;
            return true;
        }

        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0)
            error = "Browse failed: " + std::to_string(dialogError);
        return false;
#else
        (void)owner;
        std::vector<std::string> filters;
        if (std::strstr(filter, "*.pescene"))
            filters.push_back("--file-filter=Phasma Scene (*.pescene) | *.pescene");
        else if (std::strstr(filter, "*.json"))
            filters.push_back("--file-filter=JSON Settings (*.json) | *.json");
        filters.push_back("--file-filter=All Files | *");

        int pipeFd[2] = {-1, -1};
        if (pipe(pipeFd) != 0)
        {
            error = "Browse unavailable: could not create pipe";
            return false;
        }

        const pid_t pid = fork();
        if (pid < 0)
        {
            close(pipeFd[0]);
            close(pipeFd[1]);
            error = "Browse unavailable: could not fork zenity";
            return false;
        }

        if (pid == 0)
        {
            close(pipeFd[0]);
            dup2(pipeFd[1], STDOUT_FILENO);
            close(pipeFd[1]);

            const std::string titleArg = std::string("--title=") + title;
            const std::string filenameArg = "--filename=" + EnsureTrailingSlash(initialDir.generic_string());
            std::vector<char *> args;
            args.push_back(const_cast<char *>("zenity"));
            args.push_back(const_cast<char *>("--file-selection"));
            args.push_back(const_cast<char *>(titleArg.c_str()));
            args.push_back(const_cast<char *>(filenameArg.c_str()));
            for (std::string &zenityFilter : filters)
                args.push_back(const_cast<char *>(zenityFilter.c_str()));
            args.push_back(nullptr);
            execvp("zenity", args.data());
            _exit(127);
        }

        close(pipeFd[1]);
        std::string output;
        char buffer[512] = {};
        ssize_t count = 0;
        while ((count = read(pipeFd[0], buffer, sizeof(buffer))) > 0)
            output.append(buffer, static_cast<size_t>(count));
        close(pipeFd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status))
        {
            error = "Browse failed: zenity did not exit normally";
            return false;
        }
        if (WEXITSTATUS(status) == 127)
        {
            error = "Browse requires zenity.";
            return false;
        }
        if (WEXITSTATUS(status) != 0 || output.empty())
            return false;

        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        selectedPath = output;
        return !selectedPath.empty();
#endif
    }

    bool PickProjectPath(SDL_Window *owner,
                         const std::string &currentProjectPath,
                         std::string &selectedPath,
                         std::string &error)
    {
#if defined(PE_WIN32)
        // Use the modern Vista+ IFileDialog picker (Explorer-style with sidebar,
        // path bar, type-to-search) instead of the legacy SHBrowseForFolder tree.
        const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool comOwned = SUCCEEDED(comInit) && comInit != S_FALSE;

        IFileDialog *dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
        if (FAILED(hr) || !dialog)
        {
            if (comOwned)
                CoUninitialize();
            error = "Project browse failed: could not create file dialog";
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(L"Select project root or assets folder");

        // Seed the dialog at the current project path so the user lands where they expect.
        if (!currentProjectPath.empty())
        {
            std::filesystem::path initial = std::filesystem::absolute(currentProjectPath);
            std::error_code ec;
            if (!std::filesystem::is_directory(initial, ec))
                initial = initial.parent_path();
            const std::wstring initialW = initial.wstring();
            if (!initialW.empty())
            {
                IShellItem *folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(initialW.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder)
                {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }
        }

        hr = dialog->Show(GetNativeWindowHandle(owner));
        if (FAILED(hr))
        {
            // HRESULT_FROM_WIN32(ERROR_CANCELLED) is the normal "user closed it" path.
            dialog->Release();
            if (comOwned)
                CoUninitialize();
            return false;
        }

        IShellItem *result = nullptr;
        bool ok = false;
        if (SUCCEEDED(dialog->GetResult(&result)) && result)
        {
            PWSTR pathW = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &pathW)) && pathW)
            {
                const int needed = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
                if (needed > 1)
                {
                    selectedPath.assign(static_cast<size_t>(needed - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, pathW, -1, selectedPath.data(), needed, nullptr, nullptr);
                    ok = !selectedPath.empty();
                }
                CoTaskMemFree(pathW);
            }
            result->Release();
        }

        dialog->Release();
        if (comOwned)
            CoUninitialize();

        if (!ok)
            error = "Project browse failed";
        return ok;
#else
        (void)owner;
        int pipeFd[2] = {-1, -1};
        if (pipe(pipeFd) != 0)
        {
            error = "Browse unavailable: could not create pipe";
            return false;
        }

        const pid_t pid = fork();
        if (pid < 0)
        {
            close(pipeFd[0]);
            close(pipeFd[1]);
            error = "Browse unavailable: could not fork zenity";
            return false;
        }

        if (pid == 0)
        {
            close(pipeFd[0]);
            dup2(pipeFd[1], STDOUT_FILENO);
            close(pipeFd[1]);

            const std::string filenameArg = "--filename=" + EnsureTrailingSlash(currentProjectPath);
            execlp("zenity",
                   "zenity",
                   "--file-selection",
                   "--directory",
                   "--title=Select project root or assets folder",
                   filenameArg.c_str(),
                   nullptr);
            _exit(127);
        }

        close(pipeFd[1]);
        std::string output;
        char buffer[512] = {};
        ssize_t count = 0;
        while ((count = read(pipeFd[0], buffer, sizeof(buffer))) > 0)
            output.append(buffer, static_cast<size_t>(count));
        close(pipeFd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status))
        {
            error = "Browse failed: zenity did not exit normally";
            return false;
        }
        if (WEXITSTATUS(status) == 127)
        {
            error = "Browse requires zenity.";
            return false;
        }
        if (WEXITSTATUS(status) != 0 || output.empty())
            return false;

        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        selectedPath = output;
        return !selectedPath.empty();
#endif
    }

    bool PickStartupScene(SDL_Window *owner, const std::string &projectPath, std::string &selectedPath, std::string &error)
    {
        return PickFile(owner,
                        "Select startup scene",
                        "Phasma Scene (*.pescene)\0*.pescene\0All Files (*.*)\0*.*\0",
                        ProjectAssetsRoot(projectPath) / "Scenes",
                        selectedPath,
                        error);
    }

    bool PickSettingsFile(SDL_Window *owner, std::string &selectedPath, std::string &error)
    {
        return PickFile(owner,
                        "Select settings file",
                        "JSON Settings (*.json)\0*.json\0All Files (*.*)\0*.*\0",
                        RuntimeSettingsPath().parent_path(),
                        selectedPath,
                        error);
    }

#if !defined(PE_WIN32)
    bool HasConsolePrompt()
    {
        return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    }

    int PromptIndex(const char *title, const std::vector<std::string> &labels, int currentIndex)
    {
        if (labels.empty())
            return 0;

        std::cout << '\n'
                  << title << '\n';
        for (size_t i = 0; i < labels.size(); ++i)
        {
            std::cout << "  " << (i + 1) << ". " << labels[i];
            if (static_cast<int>(i) == currentIndex)
                std::cout << " [default]";
            std::cout << '\n';
        }
        std::cout << "Select [default " << (currentIndex + 1) << "]: " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input) || input.empty())
            return currentIndex;

        char *end = nullptr;
        const long value = std::strtol(input.c_str(), &end, 10);
        if (end == input.c_str() || value < 1 || value > static_cast<long>(labels.size()))
        {
            std::cout << "Invalid selection, keeping default.\n";
            return currentIndex;
        }

        return static_cast<int>(value - 1);
    }

    bool PromptLaunch()
    {
        std::cout << "\nLaunch? [Y/n]: " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input) || input.empty())
            return true;

        const char answer = static_cast<char>(std::tolower(static_cast<unsigned char>(input.front())));
        return answer == 'y';
    }

    bool PromptYesNo(const char *label, bool defaultYes)
    {
        std::cout << label << (defaultYes ? " [Y/n]: " : " [y/N]: ") << std::flush;
        std::string input;
        if (!std::getline(std::cin, input) || input.empty())
            return defaultYes;
        const char answer = static_cast<char>(std::tolower(static_cast<unsigned char>(input.front())));
        return answer == 'y';
    }

    LauncherDialogResult ShowConsoleLauncher(LauncherSelection &selection,
                                             const std::vector<LaunchTarget> &targets,
                                             int targetIndex)
    {
        std::vector<std::string> targetLabels;
        targetLabels.reserve(targets.size());
        for (const LaunchTarget &target : targets)
            targetLabels.push_back(target.label);

        std::cout << "Phasma Launcher\n";
        targetIndex = PromptIndex("Target", targetLabels, targetIndex);
        selection.launchTarget = targets[targetIndex].configValue;
        selection.activeTab = targets[targetIndex].kind == LaunchTargetKind::Editor ? LauncherTab::Editor : LauncherTab::Player;

        if (LaunchesProjectScene(targets[targetIndex]))
        {
            LaunchProfile &profile = ActiveProfile(selection);
            std::vector<std::string> sceneLabels;
            sceneLabels.reserve(profile.startupScenes.size());
            for (const std::string &scene : profile.startupScenes)
                sceneLabels.push_back(SceneDisplayName(profile, scene));

            int sceneIndex = 0;
            const auto currentScene = std::find(profile.startupScenes.begin(), profile.startupScenes.end(), profile.startupScene);
            if (currentScene != profile.startupScenes.end())
                sceneIndex = static_cast<int>(currentScene - profile.startupScenes.begin());

            sceneIndex = PromptIndex("Startup scene", sceneLabels, sceneIndex);
            if (sceneIndex >= 0 && sceneIndex < static_cast<int>(profile.startupScenes.size()))
                profile.startupScene = profile.startupScenes[sceneIndex];
        }

        if (!selection.apiLocked)
            selection.api = PE_GRAPHICS_API_VULKAN;

        const std::array<GpuAdapterPreferenceOption, 4> gpuOptions = GpuAdapterPreferenceOptions();
        std::vector<std::string> gpuLabels;
        gpuLabels.reserve(gpuOptions.size());
        for (const GpuAdapterPreferenceOption &option : gpuOptions)
            gpuLabels.emplace_back(option.label);

        const int gpuIndex =
            PromptIndex("GPU", gpuLabels, FindGpuAdapterPreferenceOptionIndex(selection.gpuAdapterPreference));
        selection.gpuAdapterPreference = gpuOptions[gpuIndex].preference;

        const std::array<PresentModeOption, 5> presentOptions = PresentModeOptions();
        std::vector<std::string> presentLabels;
        presentLabels.reserve(presentOptions.size());
        for (const PresentModeOption &option : presentOptions)
            presentLabels.emplace_back(option.label);

        const int presentIndex =
            PromptIndex("Present mode", presentLabels, FindPresentModeOptionIndex(selection.presentModeOverride));
        selection.presentModeOverride = presentOptions[presentIndex].mode;

        if (targets[targetIndex].kind == LaunchTargetKind::Player)
            selection.liveProfiler = PromptYesNo("Enable live profiler (PhasmaProfiler)", selection.liveProfiler);

        std::cout << "Parameters [" << selection.launchArguments << "]: " << std::flush;
        std::string launchArguments;
        if (std::getline(std::cin, launchArguments) && !launchArguments.empty())
            CopyStringToArray(selection.launchArguments, launchArguments);

        std::cout << "\nProject: " << ActiveProfile(selection).projectPath << '\n'
                  << "Backend: " << pe::GraphicsApiConfigName(selection.api) << '\n'
                  << "GPU: " << pe::GpuAdapterPreferenceDisplayName(selection.gpuAdapterPreference) << '\n'
                  << "Settings: " << RuntimeSettingsPath().generic_string() << '\n';

        selection.accepted = PromptLaunch();
        return selection.accepted ? LauncherDialogResult::Launch : LauncherDialogResult::Cancel;
    }
#endif

    void RefreshStartupScenes(LaunchProfile &profile)
    {
        profile.startupScenes = DiscoverStartupScenes(profile.projectPath, profile.startupScene);
    }

    // The startup scene a freshly-selected project should default to: its manifest's
    // startup_scene (in the discovered/relative form) when that resolves to a real file,
    // otherwise "" (the launcher's "Empty scene", which is always valid). This keeps the
    // previous project's scene from sticking around and failing validation.
    std::string ProjectDefaultStartupScene(const std::string &projectPath)
    {
        if (std::optional<pe::ProjectConfig> project = TryLoadProjectAtRoot(projectPath))
        {
            const std::filesystem::path scene = project->ResolveStartupScene();
            std::error_code ec;
            if (!scene.empty() && std::filesystem::exists(scene, ec))
                return MakeRuntimeRelativePath(scene);
        }
        return "";
    }

    void ApplyPickedProject(LaunchProfile &profile, const std::string &selectedPath)
    {
        profile.projectPath = NormalizeConfiguredProjectPath(selectedPath);
        profile.startupScene = ProjectDefaultStartupScene(profile.projectPath);
        RefreshStartupScenes(profile);
    }

    void ApplyPickedScene(LaunchProfile &profile, const std::string &selectedPath)
    {
        profile.projectPath = InferProjectPathFromScene(selectedPath);
        profile.startupScene = MakeRuntimeRelativePath(selectedPath);
        RefreshStartupScenes(profile);
    }

    // "Project" panel: the project to launch, and where new projects are created.
    // The "Project folder" field IS the project directory (browseable, editable) and
    // is the single source of profile.projectPath; Template + Create scaffold a new
    // project AT that folder (its name = the folder leaf; Empty is native, no Python;
    // others shell out to
    // tools/new_game.py). State persists in function-local statics (the launcher is a
    // single short-lived modal, so this is safe).
    void RenderProjectPanel(SDL_Window *window,
                            LaunchProfile &profile,
                            std::string &statusText,
                            BrowseDialogState &browseDialog)
    {
        if (!ImGui::CollapsingHeader("Project", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        (void)window;

        static char folderBuffer[1024] = {};
        static std::string appliedPath;
        static int templateIndex = 0;
        // Index 0 is the native blank project; the rest map to new_game.py templates.
        static const char *kTemplateLabels[] = {"Empty", "Topdown mini-game"};
        static const char *kTemplateScripts[] = {"", "topdown"};

        RenderHelpLabel("Project folder",
                        "The root folder of the project to open, or the destination for Create Project. "
                        "An existing project must contain phasma.project.json before Editor or Player can launch it.");
        ImGui::SameLine(kFieldX);
        ImGui::SetNextItemWidth(kSceneComboWidth);
        ImGui::InputText("##project_folder", folderBuffer, sizeof(folderBuffer));
        const bool folderEditing = ImGui::IsItemActive();
        const bool folderCommitted = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::Button("Open##project_folder", ImVec2(110.0f, 0.0f)))
        {
#if defined(PE_WIN32)
            std::string selectedPath;
            std::string browseError;
            if (PickProjectPath(window, profile.projectPath, selectedPath, browseError))
            {
                ApplyPickedProject(profile, selectedPath);
                CopyStringToArray(folderBuffer, profile.projectPath);
                appliedPath = profile.projectPath;
                statusText.clear();
            }
            else
            {
                statusText = browseError;
            }
#else
            OpenBrowseDialog(browseDialog, BrowseDialogKind::Project, &profile, profile.projectPath, true, nullptr);
            statusText.clear();
#endif
        }

        RenderHelpLabel("Template",
                        "Starting content used only by Create Project. Empty creates a blank project; Topdown mini-game "
                        "creates the sample structure. It never modifies an existing project.");
        ImGui::SameLine(kFieldX);
        ImGui::SetNextItemWidth(240.0f);
        ImGui::Combo("##project_template", &templateIndex, kTemplateLabels, IM_ARRAYSIZE(kTemplateLabels));

        ImGui::SetCursorPosX(kFieldX);
        if (ImGui::Button("Create Project", ImVec2(180.0f, 0.0f)))
        {
            std::filesystem::path projectRoot(folderBuffer);
            if (projectRoot.empty())
            {
                statusText = "New Project: a project folder is required.";
            }
            else
            {
                std::error_code ec;
                projectRoot = std::filesystem::absolute(projectRoot, ec).lexically_normal();
                // The project name is the folder's leaf (no separate Name field).
                const std::string name = projectRoot.filename().string();
                if (std::filesystem::exists(projectRoot / pe::kProjectManifestFileName, ec))
                {
                    statusText = "A project already exists in that folder.";
                }
                else if (std::filesystem::exists(projectRoot, ec) && !std::filesystem::is_empty(projectRoot, ec))
                {
                    statusText = "That folder already exists and is not empty.";
                }
                else
                {
                    std::string error;
                    bool ok = false;
                    if (templateIndex == 0)
                    {
                        ok = CreateEmptyProject(name, projectRoot, error);
                    }
                    else
                    {
                        // new_game.py creates <dir>/<leaf>; target the chosen folder
                        // by splitting it into parent + leaf.
                        ok = RunNewGameGenerator(projectRoot.filename().string(),
                                                 kTemplateScripts[templateIndex],
                                                 projectRoot.parent_path().string(),
                                                 error);
                    }
                    if (ok)
                    {
                        ApplyPickedProject(profile, projectRoot.string());
                        CopyStringToArray(folderBuffer, profile.projectPath);
                        appliedPath = profile.projectPath;
                        for (const std::string &scene : profile.startupScenes)
                        {
                            if (scene.find("main.pescene") != std::string::npos)
                            {
                                profile.startupScene = scene;
                                break;
                            }
                        }
                        statusText = "Created project: " + projectRoot.string();
                    }
                    else
                    {
                        statusText = error;
                    }
                }
            }
        }

        // Keep the folder field and the active project in sync without fighting the
        // cursor. When the user commits an edit (Enter / click-away) we switch to that
        // project, which refreshes the startup-scene list and auto-selects the project's
        // own startup scene. External project changes (browse completion, first load)
        // flow profile -> buffer while the field is not being edited.
        if (folderCommitted && folderBuffer[0] != '\0')
        {
            ApplyPickedProject(profile, std::string(folderBuffer));
            CopyStringToArray(folderBuffer, profile.projectPath);
            appliedPath = profile.projectPath;
        }
        else if (!folderEditing && profile.projectPath != appliedPath)
        {
            CopyStringToArray(folderBuffer, profile.projectPath);
            appliedPath = profile.projectPath;
        }
    }

    std::filesystem::path ExistingBrowseDirectory(std::filesystem::path path)
    {
        if (path.empty())
            path = pe::Path::Root;

        std::error_code ec;
        if (path.is_relative())
            path = std::filesystem::absolute(path, ec);
        if (ec)
        {
            ec.clear();
            path = pe::Path::Root;
        }

        path = path.lexically_normal();
        if (std::filesystem::is_regular_file(path, ec))
            path = path.parent_path();

        while (!path.empty() && !std::filesystem::is_directory(path, ec))
        {
            const std::filesystem::path parent = path.parent_path();
            if (parent == path)
                break;
            path = parent;
        }

        if (!path.empty() && std::filesystem::is_directory(path, ec))
            return path.lexically_normal();

#if defined(PE_WIN32)
        return std::filesystem::path(pe::Path::Root).lexically_normal();
#else
        return std::filesystem::path("/").lexically_normal();
#endif
    }

    void CopyBrowsePath(BrowseDialogState &dialog, const std::filesystem::path &path)
    {
        CopyStringToArray(dialog.pathBuffer, path.generic_string());
    }

    void ClearBrowseSelection(BrowseDialogState &dialog)
    {
        dialog.selectedPath.clear();
        dialog.selectedPathIsDirectory = false;
        CopyBrowsePath(dialog, dialog.currentDirectory);
    }

    void SetBrowseDirectory(BrowseDialogState &dialog, const std::filesystem::path &directory)
    {
        dialog.currentDirectory = ExistingBrowseDirectory(directory);
        dialog.error.clear();
        ClearBrowseSelection(dialog);
    }

    void SetBrowseSelection(BrowseDialogState &dialog, const std::filesystem::path &path, bool isDirectory)
    {
        dialog.selectedPath = path.lexically_normal();
        dialog.selectedPathIsDirectory = isDirectory;
        CopyBrowsePath(dialog, dialog.selectedPath);
    }

    bool BrowseExtensionMatches(const BrowseDialogState &dialog, const std::filesystem::path &path)
    {
        if (!dialog.extensionFilter || dialog.extensionFilter[0] == '\0')
            return true;
        return path.extension() == dialog.extensionFilter;
    }

    std::filesystem::path ResolveBrowseInputPath(const BrowseDialogState &dialog)
    {
        std::filesystem::path path(dialog.pathBuffer);
        if (path.is_relative())
            path = dialog.currentDirectory / path;

        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
        if (ec)
            return path.lexically_normal();
        return absolutePath.lexically_normal();
    }

    bool ApplyBrowsePathBuffer(BrowseDialogState &dialog)
    {
        const std::filesystem::path path = ResolveBrowseInputPath(dialog);

        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            SetBrowseDirectory(dialog, path);
            return true;
        }

        if (!dialog.directoriesOnly && std::filesystem::is_regular_file(path, ec) && BrowseExtensionMatches(dialog, path))
        {
            SetBrowseSelection(dialog, path, false);
            dialog.error.clear();
            return true;
        }

        dialog.error = "Path not found";
        return false;
    }

    void ResetBrowseDialog(BrowseDialogState &dialog)
    {
        dialog = {};
    }

    void OpenBrowseDialog(BrowseDialogState &dialog,
                          BrowseDialogKind kind,
                          LaunchProfile *profile,
                          const std::filesystem::path &initialDirectory,
                          bool directoriesOnly,
                          const char *extensionFilter)
    {
        dialog.kind = kind;
        dialog.profile = profile;
        dialog.currentDirectory = ExistingBrowseDirectory(initialDirectory);
        dialog.directoriesOnly = directoriesOnly;
        dialog.extensionFilter = extensionFilter;
        dialog.openRequested = true;
        dialog.error.clear();
        ClearBrowseSelection(dialog);
    }

    const char *BrowseDialogTitle(BrowseDialogKind kind)
    {
        switch (kind)
        {
        case BrowseDialogKind::Project:
            return "Select project";
        case BrowseDialogKind::StartupScene:
            return "Select startup scene";
        case BrowseDialogKind::SettingsFile:
            return "Select settings file";
        case BrowseDialogKind::None:
            break;
        }
        return "Browse";
    }

    std::vector<BrowseEntry> ReadBrowseEntries(const BrowseDialogState &dialog, std::string &error)
    {
        std::vector<BrowseEntry> directories;
        std::vector<BrowseEntry> files;

        std::error_code ec;
        std::filesystem::directory_iterator iterator(
            dialog.currentDirectory, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            error = "Could not read " + dialog.currentDirectory.string() + ": " + ec.message();
            return {};
        }

        for (const auto &entry : iterator)
        {
            std::error_code entryEc;
            const std::filesystem::path path = entry.path().lexically_normal();
            const std::string fileName = path.filename().generic_string();
            if (fileName.empty())
                continue;

            if (entry.is_directory(entryEc))
            {
                directories.push_back({path, fileName + "/", true});
            }
            else if (!dialog.directoriesOnly && entry.is_regular_file(entryEc) && BrowseExtensionMatches(dialog, path))
            {
                files.push_back({path, fileName, false});
            }
        }

        auto sortByLabel = [](const BrowseEntry &a, const BrowseEntry &b)
        {
            return a.label < b.label;
        };
        std::sort(directories.begin(), directories.end(), sortByLabel);
        std::sort(files.begin(), files.end(), sortByLabel);

        directories.insert(directories.end(), files.begin(), files.end());
        return directories;
    }

    void CompleteBrowseDialog(BrowseDialogState &dialog,
                              const std::filesystem::path &path,
                              std::vector<char> &settingsBuffer,
                              bool &settingsEditorOpen,
                              bool &settingsDirty,
                              std::string &statusText)
    {
        const std::string selectedPath = path.generic_string();
        switch (dialog.kind)
        {
        case BrowseDialogKind::Project:
            if (dialog.profile)
            {
                ApplyPickedProject(*dialog.profile, selectedPath);
                statusText.clear();
            }
            break;
        case BrowseDialogKind::StartupScene:
            if (dialog.profile)
            {
                ApplyPickedScene(*dialog.profile, selectedPath);
                statusText.clear();
            }
            break;
        case BrowseDialogKind::SettingsFile:
        {
            std::string pickedText;
            std::string readError;
            if (ReadTextFile(path, pickedText, readError))
            {
                if (!CopyTextToBuffer(settingsBuffer, pickedText))
                    statusText = "Picked settings file is too large; showing a truncated copy.";
                else
                    statusText = "Picked settings loaded into the editor.";
                settingsEditorOpen = true;
                settingsDirty = true;
            }
            else
            {
                statusText = readError;
            }
            break;
        }
        case BrowseDialogKind::None:
            break;
        }

        ResetBrowseDialog(dialog);
        ImGui::CloseCurrentPopup();
    }

    void RenderBrowseDialog(BrowseDialogState &dialog,
                            std::vector<char> &settingsBuffer,
                            bool &settingsEditorOpen,
                            bool &settingsDirty,
                            std::string &statusText)
    {
        if (dialog.kind == BrowseDialogKind::None)
            return;

        const char *title = BrowseDialogTitle(dialog.kind);
        if (dialog.openRequested)
        {
            ImGui::OpenPopup(title);
            dialog.openRequested = false;
        }

        bool modalOpen = true;
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, &modalOpen, ImGuiWindowFlags_NoSavedSettings))
        {
            if (!modalOpen)
                ResetBrowseDialog(dialog);
            return;
        }

        if (dialog.closeRequested)
        {
            ResetBrowseDialog(dialog);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (ImGui::Button("Up", ImVec2(70.0f, 0.0f)))
        {
            const std::filesystem::path parent = dialog.currentDirectory.parent_path();
            if (!parent.empty() && parent != dialog.currentDirectory)
                SetBrowseDirectory(dialog, parent);
        }
        ImGui::SameLine();
        if (ImGui::Button("Home", ImVec2(70.0f, 0.0f)))
        {
            const char *home = std::getenv("HOME");
            if (home && home[0] != '\0')
                SetBrowseDirectory(dialog, home);
        }
        ImGui::SameLine();
        if (ImGui::Button("Root", ImVec2(70.0f, 0.0f)))
            SetBrowseDirectory(dialog, std::filesystem::path("/"));

        ImGui::SetNextItemWidth(620.0f);
        if (ImGui::InputText("##browse_path",
                             dialog.pathBuffer,
                             sizeof(dialog.pathBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            ApplyBrowsePathBuffer(dialog);
        }
        ImGui::SameLine();
        if (ImGui::Button("Go", ImVec2(70.0f, 0.0f)))
            ApplyBrowsePathBuffer(dialog);

        if (!dialog.error.empty())
            ImGui::TextDisabled("%s", dialog.error.c_str());

        std::string entriesError;
        const std::vector<BrowseEntry> entries = ReadBrowseEntries(dialog, entriesError);
        if (!entriesError.empty())
            ImGui::TextDisabled("%s", entriesError.c_str());

        std::optional<std::filesystem::path> directoryToEnter;
        std::optional<std::filesystem::path> pathToAccept;
        ImGui::BeginChild("##browse_entries", ImVec2(0.0f, 335.0f), true);
        for (const BrowseEntry &entry : entries)
        {
            const bool selected = dialog.selectedPath == entry.path;
            if (ImGui::Selectable(entry.label.c_str(), selected))
                SetBrowseSelection(dialog, entry.path, entry.isDirectory);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.isDirectory)
                    directoryToEnter = entry.path;
                else
                    pathToAccept = entry.path;
            }
        }
        ImGui::EndChild();

        if (directoryToEnter)
            SetBrowseDirectory(dialog, *directoryToEnter);
        if (pathToAccept)
            CompleteBrowseDialog(dialog, *pathToAccept, settingsBuffer, settingsEditorOpen, settingsDirty, statusText);

        const bool canAccept = dialog.pathBuffer[0] != '\0';
        ImGui::BeginDisabled(!canAccept);
        if (ImGui::Button(dialog.directoriesOnly ? "Choose" : "Open", ImVec2(96.0f, 0.0f)))
        {
            if (ApplyBrowsePathBuffer(dialog))
            {
                const bool pathIsDirectory = !dialog.selectedPath.empty() && dialog.selectedPathIsDirectory;
                const bool pathIsFile = !dialog.selectedPath.empty() && !dialog.selectedPathIsDirectory;
                if (pathIsDirectory && !dialog.directoriesOnly)
                {
                    SetBrowseDirectory(dialog, dialog.selectedPath);
                }
                else if (dialog.directoriesOnly || pathIsFile)
                {
                    const std::filesystem::path acceptedPath =
                        dialog.selectedPath.empty() ? dialog.currentDirectory : dialog.selectedPath;
                    CompleteBrowseDialog(dialog, acceptedPath, settingsBuffer, settingsEditorOpen, settingsDirty, statusText);
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
        {
            ResetBrowseDialog(dialog);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void RenderStartupSceneControls(const char *id,
                                    SDL_Window *window,
                                    LaunchProfile &profile,
                                    std::string &statusText,
                                    BrowseDialogState &browseDialog)
    {
        (void)window;
        ImGui::PushID(id);

        RenderHelpLabel("Startup scene",
                        "The .pescene loaded when Editor or Player starts. Choose Empty scene to start without loading "
                        "one.");
        ImGui::SameLine(kFieldX);
        const std::string sceneLabel = SceneDisplayName(profile, profile.startupScene);
        ImGui::SetNextItemWidth(kSceneComboWidth);
        if (ImGui::BeginCombo("##scene", sceneLabel.c_str()))
        {
            for (int i = 0; i < static_cast<int>(profile.startupScenes.size()); ++i)
            {
                const bool selected = profile.startupScenes[i] == profile.startupScene;
                const std::string label = SceneDisplayName(profile, profile.startupScenes[i]);
                ImGui::PushID(i);
                if (ImGui::Selectable(label.c_str(), selected))
                    profile.startupScene = profile.startupScenes[i];
                ImGui::PopID();
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open##startup_scene", ImVec2(110.0f, 0.0f)))
        {
#if defined(PE_WIN32)
            std::string selectedPath;
            std::string browseError;
            if (PickStartupScene(window, profile.projectPath, selectedPath, browseError))
            {
                ApplyPickedScene(profile, selectedPath);
                statusText.clear();
            }
            else
            {
                statusText = browseError;
            }
#else
            OpenBrowseDialog(browseDialog,
                             BrowseDialogKind::StartupScene,
                             &profile,
                             ProjectAssetsRoot(profile.projectPath) / "Scenes",
                             false,
                             ".pescene");
            statusText.clear();
#endif
        }

        ImGui::PopID();
    }

    void RenderValidationControls(const char *id, bool &coreValidation)
    {
        ImGui::PushID(id);

        RenderHelpLabel("Validation",
                        "Enables the selected API's debug validation: Vulkan validation layers, or the DX12 debug layer, "
                        "GPU-based validation and DRED. Use it to diagnose rendering and resource errors. Expect lower "
                        "performance; disable it for performance measurements.");
        ImGui::SameLine(kFieldX);
        ImGui::Checkbox("##validation", &coreValidation);

        ImGui::PopID();
    }

    void RenderLiveProfilerControls(bool &liveProfiler)
    {
        RenderHelpLabel("Live profiler",
                        "Player only. Passes --profiler and opens PhasmaProfiler when available so the running game can "
                        "stream live timing data. Leave it disabled unless profiling to avoid profiling overhead.");
        ImGui::SameLine(kFieldX);
        ImGui::Checkbox("##live_profiler", &liveProfiler);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Pass --profiler to PhasmaPlayer and launch PhasmaProfiler when available.");
    }

    LauncherDialogResult ShowSdlLauncher(LauncherSelection &selection,
                                         const std::vector<LaunchTarget> &targets,
                                         int targetIndex,
                                         std::string &error)
    {
        (void)targetIndex;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            error = std::string("[SDL] launcher init failed: ") + SDL_GetError();
            return LauncherDialogResult::Error;
        }
        const char *videoDriver = SDL_GetCurrentVideoDriver();
        if (videoDriver && std::strcmp(videoDriver, "dummy") == 0)
        {
            error = "[SDL] dummy video driver cannot show the launcher window";
            SDL_Quit();
            return LauncherDialogResult::Error;
        }

        SDL_Window *window = SDL_CreateWindow("Phasma Launcher",
                                              SDL_WINDOWPOS_CENTERED,
                                              SDL_WINDOWPOS_CENTERED,
                                              kLauncherWidth,
                                              kLauncherHeight,
                                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window)
        {
            error = std::string("[SDL] could not create launcher window: ") + SDL_GetError();
            SDL_Quit();
            return LauncherDialogResult::Error;
        }

        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer)
        {
            error = std::string("[SDL] could not create launcher renderer: ") + SDL_GetError();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return LauncherDialogResult::Error;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ApplyLauncherStyle();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);

        SDL_Texture *fontTexture = nullptr;
        if (!CreateFontTexture(renderer, fontTexture, error))
        {
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return LauncherDialogResult::Error;
        }

        LauncherDialogResult result = LauncherDialogResult::Cancel;
        bool running = true;
        std::string statusText;
        std::vector<char> settingsBuffer(kSettingsTextBufferSize, '\0');
        std::string settingsText;
        std::string settingsError;
        if (ReadTextFile(RuntimeSettingsPath(), settingsText, settingsError))
        {
            if (!CopyTextToBuffer(settingsBuffer, settingsText))
                statusText = "Settings file is too large for the inline editor; showing a truncated copy.";
        }
        else
        {
            statusText = settingsError;
            CopyTextToBuffer(settingsBuffer, "{\n}\n");
        }
        bool settingsEditorOpen = false;
        bool settingsDirty = false;
        BrowseDialogState browseDialog;
        const std::vector<DisplayOption> displays = EnumerateDisplays();
        if (!displays.empty() &&
            (selection.displayIndex < 0 || selection.displayIndex >= static_cast<int>(displays.size())))
            selection.displayIndex = 0;
        while (running)
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (browseDialog.kind != BrowseDialogKind::None)
                    {
                        browseDialog.closeRequested = true;
                    }
                    else
                    {
                        running = false;
                    }
                }
            }

            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
            ImGui::Begin("Phasma Launcher",
                         nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

            // Project: the project to launch (and where new projects are created).
            RenderProjectPanel(window, selection.editor, statusText, browseDialog);

            // Launch Options: what to run, against the project above + its startup
            // scene. The Run dropdown (over every launch target) replaces the old
            // Editor/Player tabs; one project selection feeds whichever is launched.
            if (ImGui::CollapsingHeader("Launch Options", ImGuiTreeNodeFlags_DefaultOpen))
            {
                int runIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
                if (runIndex < 0)
                    runIndex = 0;
                RenderHelpLabel("Run",
                                "Selects the executable to start. Editor opens the project for editing, Player runs its "
                                "startup scene.");
                ImGui::SameLine(kFieldX);
                ImGui::SetNextItemWidth(kFieldWidth);
                if (!targets.empty() && ImGui::BeginCombo("##run_target", targets[runIndex].label.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(targets.size()); ++i)
                    {
                        const bool selected = i == runIndex;
                        if (ImGui::Selectable(targets[i].label.c_str(), selected))
                            selection.launchTarget = targets[i].configValue;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const int activeIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
                const bool projectSceneTarget = activeIndex >= 0 && LaunchesProjectScene(targets[activeIndex]);
                ImGui::BeginDisabled(!projectSceneTarget);
                RenderStartupSceneControls("scene", window, selection.editor, statusText, browseDialog);
                ImGui::EndDisabled();

                RenderHelpLabel("Parameters",
                                "Extra command-line arguments appended after the launcher-generated --api and --display "
                                "arguments. Later duplicates override the dropdown values. Separate arguments with "
                                "spaces and quote values that contain spaces.");
                ImGui::SameLine(kFieldX);
                ImGui::InputTextMultiline("##launch_arguments",
                                          selection.launchArguments,
                                          sizeof(selection.launchArguments),
                                          ImVec2(kFieldWidth, ImGui::GetTextLineHeightWithSpacing() * 2.5f));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Available parameters:\n"
                                      "Common: --api <vulkan|dx12>, --display <index>, --screen <index>\n"
                                      "Player: --profiler[=<port>], --profiler-port <port>\n"
                                      "Quotes preserve spaces.");
            }

            // Target-derived state, kept current whether or not Launch Options is
            // expanded: the active tab follows the Run target, and one project
            // selection drives every target.
            {
                const int activeIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
                selection.activeTab = (activeIndex >= 0 && targets[activeIndex].kind == LaunchTargetKind::Editor)
                                          ? LauncherTab::Editor
                                          : LauncherTab::Player;
                selection.player = selection.editor;
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            {
                RenderHelpLabel("Backend",
                                "Selects the graphics API used by the launched executable. Vulkan is cross-platform; "
                                "DX12 is Windows-only. Use the backend you need to test because supported GPU features "
                                "and validation behavior can differ.");
                ImGui::SameLine(kFieldX);
                ImGui::BeginDisabled(selection.apiLocked);
                bool vulkan = selection.api == PE_GRAPHICS_API_VULKAN;
                if (ImGui::RadioButton("Vulkan", vulkan))
                    selection.api = PE_GRAPHICS_API_VULKAN;
                ImGui::SameLine();
                bool dx12 = selection.api == PE_GRAPHICS_API_DX12;
#if defined(PE_WIN32)
                if (ImGui::RadioButton("DX12", dx12))
                    selection.api = PE_GRAPHICS_API_DX12;
#else
                ImGui::BeginDisabled(true);
                ImGui::RadioButton("DX12", dx12);
                ImGui::EndDisabled();
#endif
                ImGui::EndDisabled();

                if (selection.api == PE_GRAPHICS_API_VULKAN)
                    RenderValidationControls("vulkan_validation", selection.validation.vulkanCoreValidation);
#if defined(PE_WIN32)
                if (selection.api == PE_GRAPHICS_API_DX12)
                    RenderValidationControls("dx12_validation", selection.validation.dx12CoreValidation);
#endif

                {
                    const int activeIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
                    const bool isPlayer = activeIndex >= 0 && targets[activeIndex].kind == LaunchTargetKind::Player;
                    ImGui::BeginDisabled(!isPlayer);
                    RenderLiveProfilerControls(selection.liveProfiler);
                    ImGui::EndDisabled();
                }

                const std::array<GpuAdapterPreferenceOption, 4> gpuOptions = GpuAdapterPreferenceOptions();
                const int gpuIndex = FindGpuAdapterPreferenceOptionIndex(selection.gpuAdapterPreference);
                RenderHelpLabel("GPU",
                                "Requests which adapter class the launched executable should use. Auto selects the "
                                "best suitable adapter. Integrated, Discrete, or CPU / Software are useful for targeted "
                                "testing; the engine falls back to Auto when the requested class is unavailable.");
                ImGui::SameLine(kFieldX);
                ImGui::SetNextItemWidth(kFieldWidth);
                if (ImGui::BeginCombo("##gpu_adapter_preference", gpuOptions[gpuIndex].label))
                {
                    for (int i = 0; i < static_cast<int>(gpuOptions.size()); ++i)
                    {
                        const bool selected = (i == gpuIndex);
                        if (ImGui::Selectable(gpuOptions[i].label, selected))
                            selection.gpuAdapterPreference = gpuOptions[i].preference;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (!displays.empty())
                {
                    RenderHelpLabel("Display",
                                    "Selects the monitor where the launched window is created. Use this for multi-monitor "
                                    "testing; the launcher passes the selected display index to the executable.");
                    ImGui::SameLine(kFieldX);
                    ImGui::SetNextItemWidth(kFieldWidth);
                    const char *displayPreview = displays.front().label.c_str();
                    for (const DisplayOption &option : displays)
                    {
                        if (option.index == selection.displayIndex)
                        {
                            displayPreview = option.label.c_str();
                            break;
                        }
                    }
                    if (ImGui::BeginCombo("##display", displayPreview))
                    {
                        for (const DisplayOption &option : displays)
                        {
                            const bool selected = option.index == selection.displayIndex;
                            if (ImGui::Selectable(option.label.c_str(), selected))
                                selection.displayIndex = option.index;
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                const std::array<PresentModeOption, 5> presentOptions = PresentModeOptions();
                const int presentIndex = FindPresentModeOptionIndex(selection.presentModeOverride);
                RenderHelpLabel("Present mode",
                                "Controls swapchain frame presentation. Default keeps the scene or engine setting. FIFO "
                                "uses VSync without tearing; Immediate minimizes latency but may tear; Mailbox offers "
                                "low-latency VSync when supported; FIFO relaxed may tear when frames miss VSync.");
                ImGui::SameLine(kFieldX);
                ImGui::SetNextItemWidth(kFieldWidth);
                if (ImGui::BeginCombo("##present_mode", presentOptions[presentIndex].label))
                {
                    for (int i = 0; i < static_cast<int>(presentOptions.size()); ++i)
                    {
                        const bool selected = (i == presentIndex);
                        if (ImGui::Selectable(presentOptions[i].label, selected))
                            selection.presentModeOverride = presentOptions[i].mode;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            RenderBrowseDialog(browseDialog, settingsBuffer, settingsEditorOpen, settingsDirty, statusText);

            if (!statusText.empty())
            {
                ImGui::SetCursorPosY(kLauncherHeight - 68.0f);
                ImGui::TextDisabled("%s", statusText.c_str());
            }

            ImGui::SetCursorPos(ImVec2(kLaunchButtonX, kLauncherHeight - 54.0f));
            if (ImGui::Button("Launch", ImVec2(90.0f, 28.0f)))
            {
                std::string error;
                if (settingsDirty && !SaveSettingsBuffer(settingsBuffer, error))
                {
                    statusText = error;
                }
                else if (!ValidateLaunchSelection(selection, targets, error))
                {
                    statusText = error;
                }
                else
                {
                    settingsDirty = false;
                    selection.accepted = true;
                    result = LauncherDialogResult::Launch;
                    running = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 28.0f)))
            {
                result = LauncherDialogResult::Cancel;
                running = false;
            }

            ImGui::End();
            ImGui::Render();

            SDL_SetRenderDrawColor(renderer, 31, 34, 37, 255);
            SDL_RenderClear(renderer);
            RenderImGuiDrawData(renderer, ImGui::GetDrawData());
            SDL_RenderPresent(renderer);
        }

        if (fontTexture)
            SDL_DestroyTexture(fontTexture);
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    LauncherDialogResult ShowLauncherWindow(LauncherSelection &selection)
    {
        const std::vector<LaunchTarget> targets = DiscoverLaunchTargets(selection.launchTarget);
        const int targetIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
        if (targetIndex >= 0 && targetIndex < static_cast<int>(targets.size()))
            selection.launchTarget = targets[targetIndex].configValue;

        std::string error;
        const LauncherDialogResult result = ShowSdlLauncher(selection, targets, targetIndex, error);
        if (result != LauncherDialogResult::Error)
            return result;

#if !defined(PE_WIN32)
        if (HasConsolePrompt())
        {
            pe::Log::Warn(error + "; falling back to terminal launcher");
            return ShowConsoleLauncher(selection, targets, targetIndex);
        }
#endif

        pe::Log::Error(error);
        return LauncherDialogResult::Error;
    }

    bool IsHardApiOverride(pe::GraphicsApiSelectionSource source)
    {
        return source == pe::GraphicsApiSelectionSource::CommandLine ||
               source == pe::GraphicsApiSelectionSource::Environment;
    }

    int RunLauncher(int argc, char *argv[])
    {
        pe::GraphicsApiSelection apiSelection = pe::ResolveGraphicsApi(argc, argv);
        if (!apiSelection.Succeeded())
        {
            PE_ERROR("%s", apiSelection.error.c_str());
            return 1;
        }
        if (!apiSelection.warning.empty())
            PE_WARN("%s", apiSelection.warning.c_str());

        std::string currentScene;
        rapidjson::Document runtimeSettings;
        std::string runtimeSettingsWarning;
        const bool loadedRuntimeSettings = TryLoadJsonObject(RuntimeSettingsPath(), runtimeSettings, runtimeSettingsWarning);
        const bool hasRuntimeStartupScene =
            loadedRuntimeSettings &&
            pe::TryReadRuntimeStartupScene({}, currentScene, nullptr);
        if (!hasRuntimeStartupScene)
            currentScene = ReadEditorConfigStartupScene();

        std::string currentProjectPath = ReadJsonStringField(RuntimeSettingsPath(), pe::kProjectPathSettingsKey);
        if (!currentProjectPath.empty() && std::filesystem::exists(currentProjectPath))
        {
            currentProjectPath = NormalizeConfiguredProjectPath(currentProjectPath);
        }
        else
        {
            // No (valid) user project selected: default to the bundled minimal default
            // project copied next to the exe, falling back to the built-in assets root.
            std::error_code ec;
            const std::filesystem::path defaultProject = std::filesystem::path(pe::Path::Root) / "DefaultProject";
            if (std::filesystem::exists(defaultProject / pe::kProjectManifestFileName, ec))
                currentProjectPath = NormalizeProjectPath(defaultProject);
            else
                currentProjectPath = NormalizeProjectPath(std::filesystem::path(pe::Path::Assets));
        }
        std::string currentLaunchTarget = ReadJsonStringField(RuntimeSettingsPath(), k_launchTargetKey);
        if (currentLaunchTarget.empty())
            currentLaunchTarget = k_editorLaunchTarget;
        const std::string currentLaunchArguments =
            ReadJsonStringField(RuntimeSettingsPath(), kLaunchArgumentsKey);

        int currentDisplayIndex = 0;
        if (loadedRuntimeSettings && runtimeSettings.HasMember(k_displayIndexKey) && runtimeSettings[k_displayIndexKey].IsInt())
            currentDisplayIndex = runtimeSettings[k_displayIndexKey].GetInt();
        if (currentDisplayIndex < 0)
            currentDisplayIndex = 0;

        pe::GpuAdapterPreference currentGpuAdapterPreference = pe::GpuAdapterPreference::Auto;
        const std::string currentGpuAdapterPreferenceValue =
            ReadJsonStringField(RuntimeSettingsPath(), pe::kGpuAdapterPreferenceSettingsKey);
        if (!currentGpuAdapterPreferenceValue.empty() &&
            !pe::TryParseGpuAdapterPreferenceName(currentGpuAdapterPreferenceValue, currentGpuAdapterPreference))
        {
            PE_WARN("[Runtime] Invalid %s value '%s'; using Auto",
                    pe::kGpuAdapterPreferenceSettingsKey,
                    currentGpuAdapterPreferenceValue.c_str());
            currentGpuAdapterPreference = pe::GpuAdapterPreference::Auto;
        }

        std::string presentModeWarning;
        const std::optional<PePresentMode> currentPresentMode = pe::ReadEditorPresentMode({}, &presentModeWarning);
        if (!presentModeWarning.empty())
            PE_WARN("[editor_config] %s", presentModeWarning.c_str());

        auto readRuntimeBool = [&](const char *key)
        {
            return loadedRuntimeSettings &&
                   runtimeSettings.HasMember(key) &&
                   runtimeSettings[key].IsBool() &&
                   runtimeSettings[key].GetBool();
        };
        auto readCoreValidation = [&](const char *modeKey, const char *coreKey)
        {
            if (loadedRuntimeSettings && runtimeSettings.HasMember(modeKey) && runtimeSettings[modeKey].IsString())
            {
                const std::string mode = runtimeSettings[modeKey].GetString();
                return mode == "core" || mode == "phasma" || mode == "phasmacore";
            }
            return readRuntimeBool(coreKey);
        };

        LauncherSelection selection{};
        selection.api = apiSelection.api;
        selection.apiLocked = IsHardApiOverride(apiSelection.source);
        selection.editor.projectPath = currentProjectPath;
        selection.editor.startupScene = currentScene;
        selection.editor.startupScenes = DiscoverStartupScenes(currentProjectPath, currentScene);
        selection.player = selection.editor;
        selection.validation.vulkanCoreValidation = readCoreValidation(kVulkanValidationModeKey, kVulkanCoreValidationKey);
        selection.validation.dx12CoreValidation = readCoreValidation(kDx12ValidationModeKey, kDx12CoreValidationKey);
        selection.liveProfiler = readRuntimeBool(kLiveProfilerKey);
        selection.launchTarget = currentLaunchTarget;
        CopyStringToArray(selection.launchArguments, currentLaunchArguments);
        selection.displayIndex = currentDisplayIndex;
        selection.gpuAdapterPreference = currentGpuAdapterPreference;
        selection.presentModeOverride = currentPresentMode;
        selection.activeTab = currentLaunchTarget == k_editorLaunchTarget ? LauncherTab::Editor : LauncherTab::Player;

        const LauncherDialogResult dialogResult = ShowLauncherWindow(selection);
        if (dialogResult == LauncherDialogResult::Cancel)
            return 0;
        if (dialogResult == LauncherDialogResult::Error)
            return 1;

        const LaunchProfile &profile = ActiveProfile(selection);
        pe::Path::Assets = EnsureTrailingSlash(ProjectAssetsRoot(profile.projectPath).generic_string());
        const bool launchesEditor = LaunchesEditor(selection);
        if (launchesEditor)
            selection.launchTarget = k_editorLaunchTarget;
        else if (selection.launchTarget.empty() || selection.launchTarget == k_editorLaunchTarget)
            selection.launchTarget = k_playerLaunchTarget;
        const PeGraphicsApi selectedApi = selection.apiLocked ? apiSelection.api : selection.api;

        std::string error;
        if (!PersistLauncherSettings(
                selectedApi,
                profile.projectPath,
                profile.startupScene,
                selection.launchTarget,
                selection.launchArguments,
                selection.validation,
                selection.liveProfiler,
                selection.displayIndex,
                selection.gpuAdapterPreference,
                selection.presentModeOverride,
                launchesEditor,
                error))
        {
            pe::Log::Error(error);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
            return 1;
        }

        if (!ApplyValidationEnvironment(selection.validation, error))
        {
            pe::Log::Error(error);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
            return 1;
        }

        if (!ApplyGpuAdapterEnvironment(selection.gpuAdapterPreference, error))
        {
            pe::Log::Error(error);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
            return 1;
        }

        const std::vector<LaunchTarget> targets = DiscoverLaunchTargets(selection.launchTarget);
        const int targetIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
        if (targetIndex < 0 || targetIndex >= static_cast<int>(targets.size()))
        {
            pe::Log::Error("No launch target selected");
            return 1;
        }

        if (!LaunchExternalTarget(targets[targetIndex],
                                  selectedApi,
                                  selection.displayIndex,
                                  selection.liveProfiler,
                                  selection.launchArguments,
                                  error))
        {
            pe::Log::Error(error);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
            return 1;
        }

        return 0;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Path::Init();
    pe::Log::Init();

    try
    {
        return RunLauncher(argc, argv);
    }
    catch (const std::exception &e)
    {
        pe::Log::Error(std::string("Unhandled exception in launcher: ") + e.what());
    }
    catch (...)
    {
        pe::Log::Error("Unhandled non-standard exception in launcher");
    }
    return 1;
}
