#include "Base/Log.h"
#include "Base/Path.h"
#include "API/GraphicsApiSelection.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "SDL.h"
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
#include "SDL_syswm.h"
#else
#include <sys/types.h>
#include <sys/wait.h>
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
            const std::filesystem::path workingDirectory = executablePath.parent_path();
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                _exit(127);
            execl(executablePath.c_str(), executablePath.filename().c_str(), "--api", apiName.c_str(), nullptr);
            _exit(127);
        }
        return true;
#endif
    }

    constexpr int kLauncherWidth = 1060;
    constexpr int kLauncherHeight = 300;
    constexpr int kLabelX = 18;
    constexpr int kFieldX = 136;
    constexpr int kFieldWidth = 876;
    constexpr int kSceneComboWidth = 760;
    constexpr int kLaunchButtonX = 820;
    constexpr int kCancelButtonX = 922;

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
        const std::filesystem::path fontPath = std::filesystem::path(pe::Path::Assets) / "Fonts/Inter-Regular.ttf";
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

    bool PickStartupScene(SDL_Window *owner, const std::string &projectPath, std::string &selectedPath, std::string &error)
    {
#if defined(PE_WIN32)
        char fileName[MAX_PATH] = {};
        const std::string initialDir = (std::filesystem::path(projectPath) / "Scenes").string();

        OPENFILENAMEA openFileName{};
        openFileName.lStructSize = sizeof(openFileName);
        openFileName.hwndOwner = GetNativeWindowHandle(owner);
        openFileName.lpstrFilter = "Phasma Scene (*.pescene)\0*.pescene\0All Files (*.*)\0*.*\0";
        openFileName.lpstrFile = fileName;
        openFileName.nMaxFile = static_cast<DWORD>(sizeof(fileName));
        openFileName.lpstrInitialDir = initialDir.c_str();
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

            const std::filesystem::path sceneDir = std::filesystem::path(projectPath) / "Scenes";
            const std::string filenameArg = "--filename=" + EnsureTrailingSlash(sceneDir.generic_string());
            execlp("zenity",
                   "zenity",
                   "--file-selection",
                   "--title=Select startup scene",
                   filenameArg.c_str(),
                   "--file-filter=Phasma Scene (*.pescene) | *.pescene",
                   "--file-filter=All Files | *",
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
            error = "Browse requires zenity; choose a discovered scene or edit editor_config.json.";
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

#if !defined(PE_WIN32)
    bool HasConsolePrompt()
    {
        return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    }

    int PromptIndex(const char *title, const std::vector<std::string> &labels, int currentIndex)
    {
        if (labels.empty())
            return 0;

        std::cout << '\n' << title << '\n';
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

        if (LaunchesEditor(selection))
        {
            std::vector<std::string> sceneLabels;
            sceneLabels.reserve(selection.startupScenes.size());
            for (const std::string &scene : selection.startupScenes)
                sceneLabels.push_back(SceneDisplayName(selection, scene));

            int sceneIndex = 0;
            const auto currentScene = std::find(selection.startupScenes.begin(),
                                                selection.startupScenes.end(),
                                                selection.startupScene);
            if (currentScene != selection.startupScenes.end())
                sceneIndex = static_cast<int>(currentScene - selection.startupScenes.begin());

            sceneIndex = PromptIndex("Startup scene", sceneLabels, sceneIndex);
            if (sceneIndex >= 0 && sceneIndex < static_cast<int>(selection.startupScenes.size()))
                selection.startupScene = selection.startupScenes[sceneIndex];
        }

        if (!selection.apiLocked)
            selection.api = PE_GRAPHICS_API_VULKAN;

        std::cout << "\nProject: " << selection.projectPath << '\n'
                  << "Backend: " << pe::GraphicsApiConfigName(selection.api) << '\n'
                  << "Settings: " << RuntimeSettingsPath().generic_string() << '\n';

        selection.accepted = PromptLaunch();
        return selection.accepted ? LauncherDialogResult::Launch : LauncherDialogResult::Cancel;
    }
#endif

    LauncherDialogResult ShowSdlLauncher(LauncherSelection &selection,
                                         const std::vector<LaunchTarget> &targets,
                                         int targetIndex,
                                         std::string &error)
    {
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
        while (running)
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
            }

            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
            ImGui::Begin("Phasma Launcher",
                         nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

            ImGui::TextUnformatted("Target");
            ImGui::SameLine(kFieldX);
            ImGui::SetNextItemWidth(kFieldWidth);
            if (ImGui::BeginCombo("##target", targets[targetIndex].label.c_str()))
            {
                for (int i = 0; i < static_cast<int>(targets.size()); ++i)
                {
                    const bool selected = i == targetIndex;
                    if (ImGui::Selectable(targets[i].label.c_str(), selected))
                    {
                        targetIndex = i;
                        selection.launchTarget = targets[targetIndex].configValue;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const bool editorSelected = LaunchesEditor(selection);
            ImGui::TextUnformatted("Project");
            ImGui::SameLine(kFieldX);
            ImGui::BeginDisabled(!editorSelected);
            ImGui::TextUnformatted(selection.projectPath.c_str());
            ImGui::EndDisabled();

            ImGui::TextUnformatted("Startup scene");
            ImGui::SameLine(kFieldX);
            ImGui::BeginDisabled(!editorSelected);
            std::string sceneLabel = SceneDisplayName(selection, selection.startupScene);
            ImGui::SetNextItemWidth(kSceneComboWidth);
            if (ImGui::BeginCombo("##scene", sceneLabel.c_str()))
            {
                for (int i = 0; i < static_cast<int>(selection.startupScenes.size()); ++i)
                {
                    const bool selected = selection.startupScenes[i] == selection.startupScene;
                    const std::string label = SceneDisplayName(selection, selection.startupScenes[i]);
                    if (ImGui::Selectable(label.c_str(), selected))
                        selection.startupScene = selection.startupScenes[i];
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(100.0f, 0.0f)))
            {
                std::string selectedPath;
                std::string browseError;
                if (PickStartupScene(window, selection.projectPath, selectedPath, browseError))
                {
                    selection.projectPath = InferProjectPathFromScene(selectedPath);
                    selection.startupScenes = DiscoverStartupScenes(selection.projectPath, "");
                    selection.startupScene = MakeRuntimeRelativePath(selectedPath);
                    AddUniqueScene(selection.startupScenes, selection.startupScene);
                    statusText.clear();
                }
                else
                {
                    statusText = browseError;
                }
            }
            ImGui::EndDisabled();

            ImGui::TextUnformatted("Backend");
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

            ImGui::TextUnformatted("Settings");
            ImGui::SameLine(kFieldX);
            ImGui::TextUnformatted(RuntimeSettingsPath().generic_string().c_str());

            if (!statusText.empty())
            {
                ImGui::SetCursorPosY(kLauncherHeight - 68.0f);
                ImGui::TextDisabled("%s", statusText.c_str());
            }

            ImGui::SetCursorPos(ImVec2(kLaunchButtonX, kLauncherHeight - 54.0f));
            if (ImGui::Button("Launch", ImVec2(90.0f, 28.0f)))
            {
                selection.accepted = true;
                result = LauncherDialogResult::Launch;
                running = false;
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

        if (!LaunchExternalTarget(targets[targetIndex], selectedApi, error))
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
