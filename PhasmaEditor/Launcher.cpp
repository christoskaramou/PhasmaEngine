#include "Base/Log.h"
#include "Base/Path.h"
#include "API/GraphicsApiSelection.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <cctype>

#if defined(PE_WIN32)
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
    constexpr const char *k_runtimeSettingsFileName = "phasma_settings.json";
    constexpr const char *k_editorConfigPath = "Assets/editor_config.json";
    constexpr const char *k_graphicsApiKey = "graphics_api";
    constexpr const char *k_projectPathKey = "project_path";
    constexpr const char *k_startupSceneKey = "startup_scene";
    constexpr const char *k_launchTargetKey = "launch_target";
    constexpr const char *k_editorLaunchTarget = "PhasmaEditor";

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
        return std::filesystem::path(pe::Path::Executable) / k_runtimeSettingsFileName;
    }

    std::filesystem::path EditorConfigPath()
    {
        return std::filesystem::path(pe::Path::Executable) / k_editorConfigPath;
    }

    std::filesystem::path EditorExecutablePath()
    {
#if defined(PE_WIN32)
        return std::filesystem::path(pe::Path::Executable) / "PhasmaEditor.exe";
#else
        return std::filesystem::path(pe::Path::Executable) / "PhasmaEditor";
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

    std::string InferProjectPathFromScene(const std::filesystem::path &scenePath)
    {
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

    std::string ReadEditorConfigStartupScene()
    {
        return ReadJsonStringField(EditorConfigPath(), "last_scene");
    }

    void AddUniqueScene(std::vector<std::string> &scenes, const std::string &scene)
    {
        if (std::find(scenes.begin(), scenes.end(), scene) == scenes.end())
            scenes.push_back(scene);
    }

    std::vector<std::string> DiscoverStartupScenes(const std::string &projectPath, const std::string &currentScene)
    {
        std::vector<std::string> scenes;
        AddUniqueScene(scenes, "");
        if (!currentScene.empty())
            AddUniqueScene(scenes, currentScene);

        std::filesystem::path scenesPath = std::filesystem::path(projectPath) / "Scenes";
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
            AddUniqueScene(scenes, scene);

        return scenes;
    }

    bool WriteEditorConfigStartupScene(const std::string &scene, std::string &error)
    {
        rapidjson::Document document;
        std::string warning;
        TryLoadJsonObject(EditorConfigPath(), document, warning);
        SetJsonStringMember(document, "last_scene", scene);
        return WriteJsonObject(EditorConfigPath(), document, error);
    }

    bool PersistLauncherSettings(PeGraphicsApi api,
                                 const std::string &projectPath,
                                 const std::string &startupScene,
                                 const std::string &launchTarget,
                                 bool launchesEditor,
                                 std::string &error)
    {
        rapidjson::Document document;
        std::string warning;
        if (!TryLoadJsonObject(RuntimeSettingsPath(), document, warning) && !warning.empty())
            PE_WARN("%s; rewriting launcher settings", warning.c_str());

        SetJsonStringMember(document, k_graphicsApiKey, pe::GraphicsApiConfigName(api));
        SetJsonStringMember(document, k_projectPathKey, projectPath);
        SetJsonStringMember(document, k_startupSceneKey, startupScene);
        SetJsonStringMember(document, k_launchTargetKey, launchTarget.empty() ? k_editorLaunchTarget : launchTarget);

        if (!WriteJsonObject(RuntimeSettingsPath(), document, error))
            return false;

        if (launchesEditor && !WriteEditorConfigStartupScene(startupScene, error))
            return false;

        return true;
    }

    struct LauncherSelection
    {
        PeGraphicsApi api = PE_GRAPHICS_API_VULKAN;
        std::string projectPath;
        std::string startupScene;
        std::vector<std::string> startupScenes;
        std::string launchTarget;
        bool apiLocked = false;
        bool accepted = false;
    };

    struct LaunchTarget
    {
        std::string configValue;
        std::string label;
        std::filesystem::path executablePath;
        bool editor = false;
    };

    enum class LauncherDialogResult
    {
        Launch,
        Cancel,
        Error
    };

    std::string SceneDisplayName(const LauncherSelection &selection, const std::string &scene)
    {
        if (scene.empty())
            return "Empty editor";
        return MakeRootRelativeDisplayPath(scene, selection.projectPath);
    }

    bool IsExecutableFile(const std::filesystem::path &path)
    {
#if defined(PE_WIN32)
        return path.extension() == ".exe";
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
        std::filesystem::path runtimePath = std::filesystem::path(pe::Path::Executable);
        if (runtimePath.filename().empty())
            runtimePath = runtimePath.parent_path();
        runtimePath = runtimePath.lexically_normal();
        directories.push_back(runtimePath);

        const std::filesystem::path buildRoot = runtimePath.parent_path();
        if (!buildRoot.empty())
            directories.push_back((buildRoot / "bin").lexically_normal());

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
            targets.push_back({fileName, entry.path().stem().string(), entry.path().lexically_normal(), false});
        }
    }

    std::vector<LaunchTarget> DiscoverLaunchTargets(const std::string &preferredTarget)
    {
        std::vector<LaunchTarget> targets;
        targets.push_back({k_editorLaunchTarget, "Phasma Editor", EditorExecutablePath(), true});

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
                    preferredPath = std::filesystem::path(pe::Path::Executable) / preferredPath;

                std::error_code ec;
                if (std::filesystem::exists(preferredPath, ec) && IsExecutableFile(preferredPath))
                {
                    targets.push_back(
                        {preferredTarget, preferredPath.stem().string(), preferredPath.lexically_normal(), false});
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
        return selection.launchTarget.empty() || selection.launchTarget == k_editorLaunchTarget;
    }

#if defined(PE_WIN32)
    std::string QuoteCommandLineArg(const std::string &arg)
    {
        if (arg.find_first_of(" \t\"") == std::string::npos)
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

    bool LaunchExternalTarget(const LaunchTarget &target, PeGraphicsApi api, std::string &error)
    {
        const std::filesystem::path executablePath = target.executablePath.lexically_normal();
        if (!std::filesystem::exists(executablePath))
        {
            error = "Launch target not found: " + executablePath.string();
            return false;
        }

#if defined(PE_WIN32)
        std::string commandLine =
            QuoteCommandLineArg(executablePath.string()) + " --api " + pe::GraphicsApiConfigName(api);

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        std::string mutableCommandLine = commandLine;
        const std::string workingDirectory = executablePath.parent_path().string();
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
            const std::string apiName = pe::GraphicsApiConfigName(api);
            execl(executablePath.c_str(), executablePath.filename().c_str(), "--api", apiName.c_str(), nullptr);
            _exit(127);
        }
        return true;
#endif
    }

#if defined(PE_WIN32)
    constexpr int kLauncherWidth = 1060;
    constexpr int kLauncherHeight = 300;
    constexpr int kLabelX = 18;
    constexpr int kFieldX = 136;
    constexpr int kFieldWidth = 876;
    constexpr int kSceneComboWidth = 760;
    constexpr int kBrowseX = 912;
    constexpr int kDropDownWidth = 960;
    constexpr int kLaunchButtonX = 820;
    constexpr int kCancelButtonX = 922;
    constexpr int kIdTargetCombo = 1000;
    constexpr int kIdSceneCombo = 1001;
    constexpr int kIdSceneBrowse = 1002;
    constexpr int kIdVulkan = 1003;
    constexpr int kIdDx12 = 1004;
    constexpr int kIdLaunch = 1005;
    constexpr int kIdCancel = 1006;

    struct WindowsLauncherDialog
    {
        LauncherSelection *selection = nullptr;
        std::vector<LaunchTarget> *targets = nullptr;
        HWND targetCombo = nullptr;
        HWND projectLabel = nullptr;
        HWND sceneLabel = nullptr;
        HWND sceneCombo = nullptr;
        HWND sceneBrowse = nullptr;
        HWND vulkanRadio = nullptr;
        HWND dx12Radio = nullptr;
    };

    void RefreshBackendRadioButtons(const WindowsLauncherDialog &dialog)
    {
        SendMessageA(dialog.vulkanRadio,
                     BM_SETCHECK,
                     dialog.selection->api == PE_GRAPHICS_API_VULKAN ? BST_CHECKED : BST_UNCHECKED,
                     0);
        SendMessageA(dialog.dx12Radio,
                     BM_SETCHECK,
                     dialog.selection->api == PE_GRAPHICS_API_DX12 ? BST_CHECKED : BST_UNCHECKED,
                     0);

        const BOOL backendEnabled = dialog.selection->apiLocked ? FALSE : TRUE;
        EnableWindow(dialog.vulkanRadio, backendEnabled);
        EnableWindow(dialog.dx12Radio, backendEnabled);
    }

    void AddSceneToCombo(HWND combo, LauncherSelection &selection, const std::string &scene)
    {
        AddUniqueScene(selection.startupScenes, scene);
        SendMessageA(combo, CB_RESETCONTENT, 0, 0);
        SendMessageA(combo, CB_SETDROPPEDWIDTH, kDropDownWidth, 0);
        for (const std::string &item : selection.startupScenes)
        {
            const std::string label = SceneDisplayName(selection, item);
            SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }

        const auto it = std::find(selection.startupScenes.begin(), selection.startupScenes.end(), scene);
        const int index = it == selection.startupScenes.end() ? 0 : static_cast<int>(it - selection.startupScenes.begin());
        SendMessageA(combo, CB_SETCURSEL, index, 0);
        selection.startupScene = scene;
    }

    void BrowseStartupScene(HWND owner, WindowsLauncherDialog &dialog)
    {
        char fileName[MAX_PATH] = {};
        std::string initialDir = (std::filesystem::path(dialog.selection->projectPath) / "Scenes").string();

        OPENFILENAMEA openFileName{};
        openFileName.lStructSize = sizeof(openFileName);
        openFileName.hwndOwner = owner;
        openFileName.lpstrFilter = "Phasma Scene (*.pescene)\0*.pescene\0All Files (*.*)\0*.*\0";
        openFileName.lpstrFile = fileName;
        openFileName.nMaxFile = static_cast<DWORD>(sizeof(fileName));
        openFileName.lpstrInitialDir = initialDir.c_str();
        openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (!GetOpenFileNameA(&openFileName))
            return;

        dialog.selection->projectPath = InferProjectPathFromScene(fileName);
        dialog.selection->startupScenes = DiscoverStartupScenes(dialog.selection->projectPath, "");
        SetWindowTextA(dialog.projectLabel, dialog.selection->projectPath.c_str());
        AddSceneToCombo(dialog.sceneCombo, *dialog.selection, MakeRuntimeRelativePath(fileName));
    }

    void RefreshEditorOnlyControls(const WindowsLauncherDialog &dialog)
    {
        const BOOL enabled = LaunchesEditor(*dialog.selection) ? TRUE : FALSE;
        EnableWindow(dialog.projectLabel, enabled);
        EnableWindow(dialog.sceneLabel, enabled);
        EnableWindow(dialog.sceneCombo, enabled);
        EnableWindow(dialog.sceneBrowse, enabled);
    }

    LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto *dialog = reinterpret_cast<WindowsLauncherDialog *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_NCCREATE:
        {
            const auto *createStruct = reinterpret_cast<CREATESTRUCTA *>(lParam);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            dialog = reinterpret_cast<WindowsLauncherDialog *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            CreateWindowExA(0, "STATIC", "Target", WS_CHILD | WS_VISIBLE, kLabelX, 20, 100, 20, hwnd, nullptr, nullptr, nullptr);
            dialog->targetCombo = CreateWindowExA(0,
                                                  "COMBOBOX",
                                                  "",
                                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                  kFieldX,
                                                  16,
                                                  kFieldWidth,
                                                  260,
                                                  hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTargetCombo)),
                                                  nullptr,
                                                  nullptr);
            SendMessageA(dialog->targetCombo, CB_SETDROPPEDWIDTH, kDropDownWidth, 0);
            CreateWindowExA(0, "STATIC", "Project", WS_CHILD | WS_VISIBLE, kLabelX, 58, 100, 20, hwnd, nullptr, nullptr, nullptr);
            dialog->projectLabel = CreateWindowExA(0,
                                                   "STATIC",
                                                   dialog->selection->projectPath.c_str(),
                                                   WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                                   kFieldX,
                                                   58,
                                                   kFieldWidth,
                                                   20,
                                                   hwnd,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr);
            dialog->sceneLabel = CreateWindowExA(0,
                                                 "STATIC",
                                                 "Startup scene",
                                                 WS_CHILD | WS_VISIBLE,
                                                 kLabelX,
                                                 96,
                                                 100,
                                                 20,
                                                 hwnd,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr);
            dialog->sceneCombo = CreateWindowExA(0,
                                                 "COMBOBOX",
                                                 "",
                                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                 kFieldX,
                                                 92,
                                                 kSceneComboWidth,
                                                 260,
                                                 hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSceneCombo)),
                                                 nullptr,
                                                 nullptr);
            SendMessageA(dialog->sceneCombo, CB_SETDROPPEDWIDTH, kDropDownWidth, 0);
            dialog->sceneBrowse = CreateWindowExA(0,
                                                  "BUTTON",
                                                  "Browse...",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  kBrowseX,
                                                  91,
                                                  100,
                                                  24,
                                                  hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSceneBrowse)),
                                                  nullptr,
                                                  nullptr);
            CreateWindowExA(0, "STATIC", "Backend", WS_CHILD | WS_VISIBLE, kLabelX, 134, 100, 20, hwnd, nullptr, nullptr, nullptr);
            dialog->vulkanRadio = CreateWindowExA(0,
                                                  "BUTTON",
                                                  "Vulkan",
                                                  WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                                  kFieldX,
                                                  132,
                                                  90,
                                                  24,
                                                  hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdVulkan)),
                                                  nullptr,
                                                  nullptr);
            dialog->dx12Radio = CreateWindowExA(0,
                                                "BUTTON",
                                                "DX12",
                                                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                                234,
                                                132,
                                                90,
                                                24,
                                                hwnd,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDx12)),
                                                nullptr,
                                                nullptr);
            CreateWindowExA(0,
                            "STATIC",
                            "Settings",
                            WS_CHILD | WS_VISIBLE,
                            kLabelX,
                            172,
                            100,
                            20,
                            hwnd,
                            nullptr,
                            nullptr,
                            nullptr);
            CreateWindowExA(0,
                            "STATIC",
                            RuntimeSettingsPath().generic_string().c_str(),
                            WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                            kFieldX,
                            172,
                            kFieldWidth,
                            20,
                            hwnd,
                            nullptr,
                            nullptr,
                            nullptr);
            CreateWindowExA(0,
                            "BUTTON",
                            "Launch",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            kLaunchButtonX,
                            226,
                            90,
                            28,
                            hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLaunch)),
                            nullptr,
                            nullptr);
            CreateWindowExA(0,
                            "BUTTON",
                            "Cancel",
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            kCancelButtonX,
                            226,
                            90,
                            28,
                            hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)),
                            nullptr,
                            nullptr);

            for (const LaunchTarget &target : *dialog->targets)
                SendMessageA(dialog->targetCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(target.label.c_str()));
            const int targetIndex = FindLaunchTargetIndex(*dialog->targets, dialog->selection->launchTarget);
            SendMessageA(dialog->targetCombo, CB_SETCURSEL, static_cast<WPARAM>(targetIndex), 0);
            dialog->selection->launchTarget = (*dialog->targets)[targetIndex].configValue;

            for (const std::string &scene : dialog->selection->startupScenes)
            {
                const std::string label = SceneDisplayName(*dialog->selection, scene);
                SendMessageA(dialog->sceneCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }

            auto it = std::find(dialog->selection->startupScenes.begin(),
                                dialog->selection->startupScenes.end(),
                                dialog->selection->startupScene);
            if (it == dialog->selection->startupScenes.end())
                it = dialog->selection->startupScenes.begin();
            SendMessageA(dialog->sceneCombo,
                         CB_SETCURSEL,
                         static_cast<WPARAM>(it - dialog->selection->startupScenes.begin()),
                         0);

            RefreshBackendRadioButtons(*dialog);
            RefreshEditorOnlyControls(*dialog);
            return 0;
        }
        case WM_COMMAND:
        {
            if (!dialog)
                break;

            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            if (id == kIdTargetCombo && notification == CBN_SELCHANGE)
            {
                const int index = static_cast<int>(SendMessageA(dialog->targetCombo, CB_GETCURSEL, 0, 0));
                if (index >= 0 && index < static_cast<int>(dialog->targets->size()))
                {
                    dialog->selection->launchTarget = (*dialog->targets)[index].configValue;
                    RefreshEditorOnlyControls(*dialog);
                }
            }
            else if (id == kIdSceneCombo && notification == CBN_SELCHANGE)
            {
                const int index = static_cast<int>(SendMessageA(dialog->sceneCombo, CB_GETCURSEL, 0, 0));
                if (index >= 0 && index < static_cast<int>(dialog->selection->startupScenes.size()))
                    dialog->selection->startupScene = dialog->selection->startupScenes[index];
            }
            else if (id == kIdSceneBrowse)
            {
                BrowseStartupScene(hwnd, *dialog);
            }
            else if (id == kIdVulkan && !dialog->selection->apiLocked)
            {
                dialog->selection->api = PE_GRAPHICS_API_VULKAN;
                RefreshBackendRadioButtons(*dialog);
            }
            else if (id == kIdDx12 && !dialog->selection->apiLocked)
            {
                dialog->selection->api = PE_GRAPHICS_API_DX12;
                RefreshBackendRadioButtons(*dialog);
            }
            else if (id == kIdLaunch)
            {
                dialog->selection->accepted = true;
                DestroyWindow(hwnd);
            }
            else if (id == kIdCancel)
            {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    LauncherDialogResult ShowLauncherWindow(LauncherSelection &selection)
    {
        const HINSTANCE instance = GetModuleHandleA(nullptr);
        const char *className = "PhasmaLauncherWindow";

        WNDCLASSA windowClass{};
        windowClass.lpfnWndProc = LauncherWndProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassA(&windowClass);

        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = std::max(0, (screenWidth - kLauncherWidth) / 2);
        const int y = std::max(0, (screenHeight - kLauncherHeight) / 2);

        std::vector<LaunchTarget> targets = DiscoverLaunchTargets(selection.launchTarget);
        WindowsLauncherDialog dialog{};
        dialog.selection = &selection;
        dialog.targets = &targets;

        HWND window = CreateWindowExA(WS_EX_DLGMODALFRAME,
                                      className,
                                      "Phasma Launcher",
                                      WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                      x,
                                      y,
                                      kLauncherWidth,
                                      kLauncherHeight,
                                      nullptr,
                                      nullptr,
                                      instance,
                                      &dialog);
        if (!window)
        {
            pe::Log::Error("Could not create Phasma Launcher window: " + std::to_string(GetLastError()));
            return LauncherDialogResult::Error;
        }

        MSG message{};
        while (GetMessageA(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        return selection.accepted ? LauncherDialogResult::Launch : LauncherDialogResult::Cancel;
    }
#else
    LauncherDialogResult ShowLauncherWindow(LauncherSelection &selection)
    {
        const std::vector<LaunchTarget> targets = DiscoverLaunchTargets(selection.launchTarget);
        const int targetIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
        if (targetIndex >= 0 && targetIndex < static_cast<int>(targets.size()))
            selection.launchTarget = targets[targetIndex].configValue;

        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Launch"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        };
        const std::string message = "Target: " + targets[targetIndex].label +
                                    "\nProject: " + selection.projectPath +
                                    "\nStartup scene: " + SceneDisplayName(selection, selection.startupScene) +
                                    "\nSettings: " + RuntimeSettingsPath().generic_string() +
                                    "\nBackend: " + pe::GraphicsApiConfigName(selection.api);
        const SDL_MessageBoxData box = {
            SDL_MESSAGEBOX_INFORMATION,
            nullptr,
            "Phasma Launcher",
            message.c_str(),
            SDL_arraysize(buttons),
            buttons,
            nullptr,
        };

        int buttonId = 0;
        if (SDL_ShowMessageBox(&box, &buttonId) < 0)
        {
            PE_WARN("[SDL] launcher prompt failed: %s", SDL_GetError());
            selection.accepted = true;
            return LauncherDialogResult::Launch;
        }

        if (buttonId == 1)
            selection.accepted = true;

        return selection.accepted ? LauncherDialogResult::Launch : LauncherDialogResult::Cancel;
    }
#endif

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

        std::string currentScene = ReadEditorConfigStartupScene();
        if (currentScene.empty())
            currentScene = ReadJsonStringField(RuntimeSettingsPath(), k_startupSceneKey);

        std::string currentProjectPath = ReadJsonStringField(RuntimeSettingsPath(), k_projectPathKey);
        if (currentProjectPath.empty() || !std::filesystem::exists(currentProjectPath))
            currentProjectPath = NormalizeProjectPath(std::filesystem::path(pe::Path::Assets));
        else
            currentProjectPath = NormalizeProjectPath(currentProjectPath);

        std::string currentLaunchTarget = ReadJsonStringField(RuntimeSettingsPath(), k_launchTargetKey);
        if (currentLaunchTarget.empty())
            currentLaunchTarget = k_editorLaunchTarget;

        LauncherSelection selection{};
        selection.api = apiSelection.api;
        selection.apiLocked = IsHardApiOverride(apiSelection.source);
        selection.projectPath = currentProjectPath;
        selection.startupScene = currentScene;
        selection.startupScenes = DiscoverStartupScenes(currentProjectPath, currentScene);
        selection.launchTarget = currentLaunchTarget;

        const LauncherDialogResult dialogResult = ShowLauncherWindow(selection);
        if (dialogResult == LauncherDialogResult::Cancel)
            return 0;
        if (dialogResult == LauncherDialogResult::Error)
            return 1;

        pe::Path::Assets = EnsureTrailingSlash(selection.projectPath);
        const bool launchesEditor = LaunchesEditor(selection);
        const PeGraphicsApi selectedApi = selection.apiLocked ? apiSelection.api : selection.api;

        std::string error;
        if (!PersistLauncherSettings(
                selectedApi, selection.projectPath, selection.startupScene, selection.launchTarget, launchesEditor, error))
        {
            pe::Log::Error(error);
#if defined(PE_WIN32)
            MessageBoxA(nullptr, error.c_str(), "Phasma Launcher", MB_ICONERROR | MB_OK);
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
#endif
            return 1;
        }

        const std::vector<LaunchTarget> targets = DiscoverLaunchTargets(selection.launchTarget);
        const int targetIndex = FindLaunchTargetIndex(targets, selection.launchTarget);
        if (targetIndex < 0 || targetIndex >= static_cast<int>(targets.size()))
        {
            pe::Log::Error("No launch target selected");
            return 1;
        }

        if (!LaunchExternalTarget(targets[targetIndex], selectedApi, error))
        {
            pe::Log::Error(error);
#if defined(PE_WIN32)
            MessageBoxA(nullptr, error.c_str(), "Phasma Launcher", MB_ICONERROR | MB_OK);
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phasma Launcher", error.c_str(), nullptr);
#endif
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
