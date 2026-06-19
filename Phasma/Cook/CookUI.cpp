// PhasmaCook UI mode — a self-contained SDL_Renderer + Dear ImGui tool window for cooking source
// models to ".pemesh" without a terminal.
//
// Deliberately has NO engine Vulkan and NO native file dialogs:
//   - the UI process renders with SDL_Renderer (software-capable) and a hand-rolled ImGui backend,
//     so it never fights the engine's Vulkan device for a window surface;
//   - each cook shells out to PhasmaCook's own headless mode ("PhasmaCook <src> <out>"), which
//     brings up the Vulkan RHI in a separate process — the proven cook path;
//   - file selection uses an in-window ImGui browser (+ drag-drop) instead of comdlg32/zenity, so it
//     works identically on Windows, Linux, and WSL/WSLg (where zenity is often absent).

#include "Base/Log.h"
#include "Base/Path.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    constexpr const char *kModelExtensions[] = {
        ".gltf", ".glb", ".fbx", ".obj", ".dae", ".ply", ".stl", ".3ds", ".blend"};

    void ItemTooltip(const char *text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayShort)
    {
        if (!text || !text[0] || !ImGui::IsItemHovered(flags))
            return;

        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    std::string PathUtf8(const std::filesystem::path &p)
    {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
    }

    std::filesystem::path PathFromUtf8(const std::string &s)
    {
        return std::filesystem::path(reinterpret_cast<const char8_t *>(s.c_str()));
    }

    bool IsModelFile(const std::filesystem::path &p)
    {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        for (const char *e : kModelExtensions)
            if (ext == e)
                return true;
        return false;
    }

    // --- ImGui-on-SDL_Renderer glue (adapted from PhasmaLauncher) ---------------------------------
    bool CreateFontTexture(SDL_Renderer *renderer, SDL_Texture *&fontTexture)
    {
        ImGuiIO &io = ImGui::GetIO();
        const std::filesystem::path fontPath = std::filesystem::path(pe::Path::ResolveAsset("Fonts/Inter-Regular.ttf"));
        if (std::filesystem::exists(fontPath))
            io.Fonts->AddFontFromFileTTF(PathUtf8(fontPath).c_str(), 16.0f);

        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        fontTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!fontTexture)
            return false;
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
                out.color = {static_cast<Uint8>(color & 0xFF), static_cast<Uint8>((color >> 8) & 0xFF),
                             static_cast<Uint8>((color >> 16) & 0xFF), static_cast<Uint8>((color >> 24) & 0xFF)};
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
                    indices.push_back(static_cast<int>(cmdList->IdxBuffer[cmd.IdxOffset + i] + cmd.VtxOffset));
                SDL_Texture *texture = reinterpret_cast<SDL_Texture *>(static_cast<intptr_t>(cmd.GetTexID()));
                SDL_RenderSetClipRect(renderer, &clipRect);
                SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()),
                                   indices.data(), static_cast<int>(indices.size()));
            }
        }
        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    }

    // Run "PhasmaCook <args...>" and wait. true iff it exits 0. The engine Vulkan device lives only
    // in the (hidden) child process; the UI itself stays pure SDL_Renderer.
    bool RunPhasmaCook(const std::filesystem::path &exe, const std::vector<std::filesystem::path> &args)
    {
#if defined(_WIN32)
        std::wstring cmd = L"\"" + exe.wstring() + L"\"";
        for (const std::filesystem::path &a : args)
            cmd += L" \"" + a.wstring() + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi))
            return false;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return code == 0;
#else
        std::vector<std::string> strings;
        strings.push_back(exe.string());
        for (const std::filesystem::path &a : args)
            strings.push_back(a.string());
        std::vector<char *> argv;
        for (std::string &s : strings)
            argv.push_back(s.data());
        argv.push_back(nullptr);
        const pid_t pid = fork();
        if (pid < 0)
            return false;
        if (pid == 0)
        {
            execv(strings[0].c_str(), argv.data());
            _exit(127);
        }
        int status = 0;
        int rc;
        do
        {
            rc = waitpid(pid, &status, 0);
        }
        while (rc < 0 && errno == EINTR);
        return rc >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    }

    // --- cook queue + UI state --------------------------------------------------------------------
    enum class CookStatus
    {
        Queued,
        Cooking,
        Done,
        Failed
    };

    struct CookItem
    {
        std::filesystem::path src;
        std::filesystem::path out;
        CookStatus status = CookStatus::Queued;
    };

    struct UiState
    {
        std::mutex mutex;             // guards items / log / cooking
        std::vector<CookItem> items;  // guarded
        std::vector<std::string> log; // guarded
        bool cooking = false;         // guarded
        std::thread worker;           // main-thread owned; joined before exit / before re-launch

        // browser + inputs (main thread only)
        std::filesystem::path browseDir;
        char outDirBuf[1024] = {};
    };

    std::filesystem::path ComputeOutputPath(const std::string &outDir, const std::filesystem::path &src)
    {
        const std::filesystem::path stem = src.stem();
        return PathFromUtf8(outDir) / stem / (PathUtf8(stem) + ".pemesh");
    }

    void AddSource(UiState &st, const std::filesystem::path &src)
    {
        if (!IsModelFile(src))
            return;
        std::lock_guard lock(st.mutex);
        for (const auto &it : st.items)
            if (it.src == src)
                return; // dedupe
        CookItem item;
        item.src = src;
        item.out = ComputeOutputPath(st.outDirBuf, src);
        st.items.push_back(std::move(item));
    }

    // Queue every source model under a directory (recursive) — the UI's "cook a folder" path.
    void AddFolderRecursive(UiState &st, const std::filesystem::path &dir)
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code fec;
            if (it->is_regular_file(fec) && !fec && IsModelFile(it->path()))
                AddSource(st, it->path());
        }
    }

    void StartCook(UiState &st, const std::filesystem::path &exe)
    {
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> jobs;
        {
            std::lock_guard lock(st.mutex);
            if (st.cooking || st.items.empty())
                return;
            st.cooking = true;
            for (auto &it : st.items)
            {
                it.status = CookStatus::Cooking;
                jobs.emplace_back(it.src, it.out);
            }
        }
        if (st.worker.joinable())
            st.worker.join(); // previous run already finished (cooking was false)

        st.worker = std::thread([&st, exe, jobs = std::move(jobs)]()
                                {
            // Cook the whole queue in ONE hidden PhasmaCook process via a UTF-8 manifest (one
            // "<src>\t<out>" per line) — a single device bring-up, no per-model windows.
            static std::atomic<unsigned> s_counter{0};
            std::error_code ec;

            // Do NOT delete existing outputs — a failed or unlaunchable cook must not destroy a
            // last-known-good .pemesh. Snapshot state; an item is Done only once its output is
            // freshly written (newly created, or its write-time advances past the snapshot).
            std::vector<bool> existedBefore(jobs.size());
            std::vector<std::filesystem::file_time_type> beforeTime(jobs.size());
            for (size_t i = 0; i < jobs.size(); ++i)
            {
                existedBefore[i] = std::filesystem::exists(jobs[i].second, ec);
                if (existedBefore[i])
                    beforeTime[i] = std::filesystem::last_write_time(jobs[i].second, ec);
            }
            auto freshlyCooked = [&](size_t i) -> bool
            {
                std::error_code fec;
                if (!std::filesystem::exists(jobs[i].second, fec))
                    return false;
                if (!existedBefore[i])
                    return true;
                return std::filesystem::last_write_time(jobs[i].second, fec) != beforeTime[i];
            };

            const std::filesystem::path manifest =
                std::filesystem::temp_directory_path(ec) /
                ("phasmacook_ui_" + std::to_string(s_counter.fetch_add(1)) + ".txt");
            {
                std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
                for (const auto &job : jobs)
                    out << PathUtf8(job.first) << '\t' << PathUtf8(job.second) << '\n';
            }

            // Spawn PhasmaCook on a helper thread; poll output files to flip each item to Done live.
            std::atomic<bool> finished{false};
            std::thread cookThread([&]()
                                   {
                RunPhasmaCook(exe, {std::filesystem::path("--batch"), manifest});
                finished.store(true); });
            while (!finished.load())
            {
                {
                    std::lock_guard l(st.mutex);
                    for (size_t i = 0; i < jobs.size() && i < st.items.size(); ++i)
                        if (st.items[i].status == CookStatus::Cooking && freshlyCooked(i))
                            st.items[i].status = CookStatus::Done;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            cookThread.join();
            std::filesystem::remove(manifest, ec);

            int ok = 0;
            {
                std::lock_guard l(st.mutex);
                for (size_t i = 0; i < jobs.size() && i < st.items.size(); ++i)
                {
                    const bool done = freshlyCooked(i);
                    st.items[i].status = done ? CookStatus::Done : CookStatus::Failed;
                    if (done)
                        ++ok;
                }
                st.log.push_back("cooked " + std::to_string(ok) + " / " + std::to_string(jobs.size()) + " model(s)");
            }

            // Surface which models failed and why, from PhasmaCook's "<manifest>.failed" sidecar.
            std::filesystem::path failedPath = manifest;
            failedPath += ".failed";
            if (std::ifstream failedIn{failedPath, std::ios::binary})
            {
                std::string line;
                while (std::getline(failedIn, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.empty())
                        continue;
                    const size_t tab = line.find('\t');
                    const std::string name = (tab == std::string::npos ? line : line.substr(0, tab));
                    const std::string reason = (tab == std::string::npos ? std::string() : line.substr(tab + 1));
                    std::lock_guard l(st.mutex);
                    st.log.push_back("[FAIL] " + PathUtf8(PathFromUtf8(name).filename()) + ": " + reason);
                }
            }
            std::filesystem::remove(failedPath, ec);

            std::lock_guard l(st.mutex);
            st.cooking = false; });
    }

    void DrawBrowser(UiState &st)
    {
        ImGui::TextDisabled("%s", PathUtf8(st.browseDir).c_str());
        if (ImGui::SmallButton("Queue all models in this folder (recursive)"))
            AddFolderRecursive(st, st.browseDir);
        ItemTooltip("Queue every supported source model found under this folder.");
        ImGui::Separator();

        if (ImGui::Selectable(".. (up)", false) && st.browseDir.has_parent_path())
            st.browseDir = st.browseDir.parent_path();
        ItemTooltip("Navigate to the parent folder.");

        std::error_code ec;
        std::vector<std::filesystem::path> dirs, files;
        for (auto it = std::filesystem::directory_iterator(
                 st.browseDir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        {
            std::error_code fec;
            if (it->is_directory(fec))
                dirs.push_back(it->path());
            else if (IsModelFile(it->path()))
                files.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        std::filesystem::path navigateTo;
        for (const auto &d : dirs)
        {
            const std::string label = "[ + ] " + PathUtf8(d.filename());
            if (ImGui::Selectable(label.c_str(), false))
                navigateTo = d;
            ItemTooltip("Open this folder in the browser.");
        }
        for (const auto &f : files)
        {
            const std::string label = "      " + PathUtf8(f.filename());
            if (ImGui::Selectable(label.c_str(), false))
                AddSource(st, f);
            ItemTooltip("Queue this source model for cooking.");
        }
        if (!navigateTo.empty())
            st.browseDir = navigateTo;
    }

    void DrawQueue(UiState &st, bool cooking)
    {
        std::lock_guard l(st.mutex);
        ImGui::Text("Queue (%zu)", st.items.size());
        ImGui::Separator();
        int removeIndex = -1;
        for (size_t i = 0; i < st.items.size(); ++i)
        {
            const char *tag = "[ ]";
            switch (st.items[i].status)
            {
            case CookStatus::Queued:
                tag = "[ ]";
                break;
            case CookStatus::Cooking:
                tag = "[~]";
                break;
            case CookStatus::Done:
                tag = "[x]";
                break;
            case CookStatus::Failed:
                tag = "[!]";
                break;
            }
            ImGui::PushID(static_cast<int>(i));
            if (!cooking)
            {
                if (ImGui::SmallButton("x"))
                    removeIndex = static_cast<int>(i);
                ItemTooltip("Remove this model from the cook queue.");
                ImGui::SameLine();
            }
            ImGui::Text("%s %s", tag, PathUtf8(st.items[i].src.filename()).c_str());
            ImGui::PopID();
        }
        if (removeIndex >= 0 && removeIndex < static_cast<int>(st.items.size()))
            st.items.erase(st.items.begin() + removeIndex);
    }

    void DrawUI(UiState &st, const std::filesystem::path &exe)
    {
        ImGui::TextUnformatted("Cook source models (glTF / FBX / OBJ / ...) to portable .pemesh.");
        ImGui::TextDisabled("Browse and click a model to queue it, or drag-drop files onto the window.");
        ImGui::Spacing();

        ImGui::TextUnformatted("Output dir:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##outdir", st.outDirBuf, sizeof(st.outDirBuf));
        ItemTooltip("Directory where cooked .pemesh files will be written.");
        ImGui::Spacing();

        bool cooking;
        {
            std::lock_guard l(st.mutex);
            cooking = st.cooking;
        }

        const float panelHeight = ImGui::GetContentRegionAvail().y - 96.0f;
        ImGui::BeginChild("browser", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, panelHeight), true);
        DrawBrowser(st);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("queue", ImVec2(0.0f, panelHeight), true);
        DrawQueue(st, cooking);
        ImGui::EndChild();

        ImGui::Spacing();
        if (cooking)
            ImGui::BeginDisabled();
        if (ImGui::Button("Cook all", ImVec2(120.0f, 0.0f)))
            StartCook(st, exe);
        ItemTooltip("Cook every queued source model.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            std::lock_guard l(st.mutex);
            st.items.clear();
        }
        ItemTooltip("Clear the current cook queue.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (cooking)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("cooking...");
        }

        ImGui::Spacing();
        ImGui::BeginChild("log", ImVec2(0.0f, 60.0f), true);
        {
            std::lock_guard l(st.mutex);
            for (const auto &line : st.log)
                ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();
    }
} // namespace

namespace pe
{
    int RunCookUI()
    {
        Path::Init();
        Log::Init();

        // The cook itself is delegated to this same executable's headless mode.
        const std::filesystem::path exePath = PathFromUtf8(Path::Executable) /
#if defined(_WIN32)
                                              "PhasmaCook.exe";
#else
                                              "PhasmaCook";
#endif

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            PE_ERROR("[CookUI] SDL_Init failed: %s", SDL_GetError());
            return 1;
        }

        SDL_Window *window = SDL_CreateWindow("PhasmaCook", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                              940, 660, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window)
        {
            PE_ERROR("[CookUI] SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer)
        {
            PE_ERROR("[CookUI] SDL_CreateRenderer failed: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui::GetIO().IniFilename = nullptr; // a tool window does not persist layout
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);

        SDL_Texture *fontTexture = nullptr;
        CreateFontTexture(renderer, fontTexture);

        UiState st;
        const std::string defaultOut = Path::Assets.empty() ? std::string("CookedModels") : (Path::Assets + "Models");
        std::snprintf(st.outDirBuf, sizeof(st.outDirBuf), "%s", defaultOut.c_str());
        std::error_code ec;
        st.browseDir = (!Path::Assets.empty() && std::filesystem::exists(Path::Assets, ec))
                           ? std::filesystem::path(Path::Assets)
                           : std::filesystem::current_path(ec);

        bool running = true;
        while (running)
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                    running = false;
                else if (event.type == SDL_DROPFILE && event.drop.file)
                {
                    AddSource(st, PathFromUtf8(event.drop.file));
                    SDL_free(event.drop.file);
                }
            }

            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
            ImGui::Begin("PhasmaCook", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
            DrawUI(st, exePath);
            ImGui::End();

            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 28, 30, 33, 255);
            SDL_RenderClear(renderer);
            RenderImGuiDrawData(renderer, ImGui::GetDrawData());
            SDL_RenderPresent(renderer);
        }

        if (st.worker.joinable())
            st.worker.join(); // wait out any in-flight cook before tearing the window down

        if (fontTexture)
            SDL_DestroyTexture(fontTexture);
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
} // namespace pe
