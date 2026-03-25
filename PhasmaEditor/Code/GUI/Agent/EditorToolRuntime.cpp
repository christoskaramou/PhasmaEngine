#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/Widgets/ProfilerWidget.h"
#include "Systems/RendererSystem.h"
#include "PhasmaAgent/AgentUtils.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "stb/stb_image.h"

using namespace pagent;

namespace pe
{
    namespace
    {
        static std::string ToLower(std::string s)
        {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        static std::vector<uint8_t> ResizeRGBA(const uint8_t *src, int srcW, int srcH, int &outW, int &outH, int maxDim = 1024)
        {
            if (srcW <= 0 || srcH <= 0 || maxDim <= 0)
            {
                outW = outH = 0;
                return {};
            }
            float scale = static_cast<float>(maxDim) / static_cast<float>(std::max(srcW, srcH));
            outW = static_cast<int>(srcW * scale);
            outH = static_cast<int>(srcH * scale);
            std::vector<uint8_t> resized(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
            for (int dy = 0; dy < outH; dy++)
            {
                int sy = dy * srcH / outH;
                for (int dx = 0; dx < outW; dx++)
                {
                    int sx = dx * srcW / outW;
                    std::memcpy(&resized[(static_cast<size_t>(dy) * outW + dx) * 4],
                                &src[(static_cast<size_t>(sy) * srcW + sx) * 4], 4);
                }
            }
            return resized;
        }
    } // namespace

    EditorToolRuntime::EditorToolRuntime(ScriptSystem *scriptSystem, QueueActionFn queueAction, void *sdlWindow)
        : m_scriptSystem(scriptSystem), m_queueAction(std::move(queueAction)), m_sdlWindow(sdlWindow)
    {
    }

    void EditorToolRuntime::QueueAction(std::function<void()> fn) const
    {
        if (m_queueAction)
            m_queueAction(std::move(fn));
    }

    std::string EditorToolRuntime::ExecuteLua(const std::string &code) const
    {
        if (code.empty())
            return "{\"error\":\"missing code\"}";

        // Heap-allocate shared state so the lambda is safe to run even if wait_for times out
        // and this function has already returned (otherwise the lambda writes into freed stack).
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([this, state, code]()
                    {
            if (!m_scriptSystem || !m_scriptSystem->IsInitialized())
                state->result = "error: ScriptSystem not available";
            else
                state->result = m_scriptSystem->ExecuteLua(code);
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for Lua execution\"}";
        }

        if (state->result.rfind("error:", 0) == 0)
            return JsonObj({{"error", JsonStr(state->result)}});
        return JsonObj({{"output", JsonStr(state->result)}});
    }

    std::string EditorToolRuntime::FindLoadableModel(const std::string &query) const
    {
        if (query.empty())
            return "{\"error\":\"missing query\"}";

        std::string queryLower = ToLower(query);
        std::filesystem::path objectsDir(Path::Assets + "Objects");
        if (!std::filesystem::exists(objectsDir))
            return "{\"error\":\"Objects directory not found\"}";

        std::string arr = "[";
        bool first = true;
        int count = 0;
        try
        {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(
                     objectsDir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;

                std::string ext = ToLower(entry.path().extension().string());
                if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx")
                    continue;

                std::string relPath = std::filesystem::relative(entry.path(), Path::Assets + "Objects").string();
                std::replace(relPath.begin(), relPath.end(), '\\', '/');

                if (ToLower(relPath).find(queryLower) == std::string::npos)
                    continue;

                if (!first)
                    arr += ",";
                arr += JsonStr(relPath);
                first = false;
                if (++count >= 20)
                    break;
            }
        }
        catch (const std::filesystem::filesystem_error &)
        {
            // Ignore filesystem errors; return whatever was found so far
        }
        arr += "]";
        return JsonObj({{"count", std::to_string(count)}, {"models", arr}});
    }

    std::string EditorToolRuntime::ReadAgentFile(const std::string &path) const
    {
        if (path.empty())
            return "{\"error\":\"missing path\"}";

        const std::string agentWorkspace = Path::Assets + "Agent/";
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

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // Return the path relative to the workspace to avoid leaking host filesystem layout
        std::string displayPath = path.empty() ? fpath.filename().string() : path;
        return JsonObj({{"path", JsonStr(displayPath)}, {"content", JsonStr(content)}});
    }

    std::string EditorToolRuntime::WriteAgentFile(const std::string &path, const std::string &content, bool append) const
    {
        if (path.empty())
            return "{\"error\":\"missing path\"}";

        const std::string agentWorkspace = Path::Assets + "Agent/";
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
        flags |= append ? std::ios::app : std::ios::trunc;

        std::ofstream file(fpath, flags);
        if (!file.is_open())
            return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
        file << content;
        file.close();

        return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(fpath.string())}});
    }

    std::string EditorToolRuntime::TakeScreenshot() const
    {
        auto *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return "{\"error\":\"RendererSystem not available\"}";

        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        const std::string agentDir = Path::Assets + "Agent/";
        {
            std::error_code ec;
            std::filesystem::create_directories(agentDir, ec);
        }
        std::string screenshotPath = agentDir + "ss_" + std::to_string(ts) + ".png";
        std::filesystem::remove(screenshotPath);

        int mouseX = 0, mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);

        // Push the event from the main thread (via QueueAction) so it lands AFTER
        // Window::ProcessEvents() has already called ClearPushedEvents() for this frame.
        // If pushed directly from the HTTP thread, it gets wiped before RecordPasses() sees it.
        QueueAction([screenshotPath]()
                    { EventSystem::PushEvent(EventType::Screenshot, screenshotPath); });

        // Poll until the file exists AND its size has been stable for two consecutive checks,
        // ensuring the renderer has finished writing before we read it.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        uintmax_t prevSize = 0;
        int stableCount = 0;
        while (true)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return "{\"error\":\"screenshot timeout\"}";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::error_code ec;
            const uintmax_t sz = std::filesystem::file_size(screenshotPath, ec);
            if (!ec && sz > 0)
            {
                if (sz == prevSize)
                {
                    if (++stableCount >= 2)
                        break;
                }
                else
                {
                    prevSize = sz;
                    stableCount = 0;
                }
            }
        }

        int w = 0, h = 0, c = 0;
        uint8_t *pixels = stbi_load(screenshotPath.c_str(), &w, &h, &c, 4);
        std::filesystem::remove(screenshotPath);
        if (!pixels)
            return "{\"error\":\"failed to load screenshot\"}";

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
        const int kMaxDim = 1024;
        if (w > kMaxDim || h > kMaxDim)
        {
            resizedPixels = ResizeRGBA(pixels, w, h, rw, rh, kMaxDim);
            stbi_image_free(pixels);
            pixels = nullptr;
        }

        constexpr size_t kMaxBytes = 5 * 1024 * 1024;
        std::vector<uint8_t> pngData;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint8_t *src = pixels ? pixels : resizedPixels.data();
            pngData = pagent::EncodeRGBA_PNG(src, rw, rh);
            if (pngData.empty() || pngData.size() <= kMaxBytes)
                break;

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
    }

    std::string EditorToolRuntime::QueryImGuiWindows() const
    {
        // Heap-allocate shared state so the lambda is safe even if wait_for times out first.
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            nlohmann::json result;
        };
        auto state = std::make_shared<State>();
        state->result["windows"] = nlohmann::json::array();
        state->result["tab_bars"] = nlohmann::json::array();

        QueueAction([state]()
                    {
            ImGuiContext *ctx = ImGui::GetCurrentContext();
            if (ctx)
            {
                for (ImGuiWindow *win : ctx->Windows)
                {
                    if (!win->WasActive || win->Size.x <= 0 || win->Size.y <= 0)
                        continue;
                    state->result["windows"].push_back({
                        {"name", win->Name},
                        {"x", static_cast<int>(win->Pos.x)},
                        {"y", static_cast<int>(win->Pos.y)},
                        {"width", static_cast<int>(win->Size.x)},
                        {"height", static_cast<int>(win->Size.y)},
                        {"collapsed", win->Collapsed},
                        {"focused", ctx->NavWindow == win},
                    });
                }

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
                    state->result["tab_bars"].push_back({
                        {"bar_x", static_cast<int>(tb->BarRect.Min.x)},
                        {"bar_y", static_cast<int>(tb->BarRect.Min.y)},
                        {"tabs", tabsArr},
                    });
                };

                for (auto &kv : ctx->DockContext.Nodes.Data)
                {
                    auto *node = static_cast<ImGuiDockNode *>(kv.val_p);
                    if (node)
                        emitTabBar(node->TabBar);
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                { return state->done; }))
            return "{\"error\":\"timeout\"}";

        return state->result.dump();
    }

    std::string EditorToolRuntime::InjectMouseInput(const std::string &args) const
    {
        std::string action = ExtractArgStr(args, "action");
        if (action.empty())
            return "{\"error\":\"missing action\"}";

        int scrollX = static_cast<int>(ExtractArgInt(args, "scroll_x", 0));
        int scrollY = static_cast<int>(ExtractArgInt(args, "scroll_y", 0));

        // Parse coordinates — u/v conversion is deferred to the main thread (QueueAction).
        // Store as float u/v or int x/y.
        bool useUV = false;
        float u = 0.0f, v = 0.0f;
        int x = -1, y = -1;
        {
            auto parsedArgs = ParseArgs(args);
            if (parsedArgs.contains("u") || parsedArgs.contains("v"))
            {
                u = ExtractArgNum(args, "u");
                v = ExtractArgNum(args, "v");
                useUV = true;
            }
            else
            {
                x = static_cast<int>(ExtractArgInt(args, "x", -1));
                y = static_cast<int>(ExtractArgInt(args, "y", -1));
            }
        }

        if (!useUV && (x < 0 || y < 0))
            return "{\"error\":\"provide u/v (normalized) or x/y (real pixels)\"}";

        // Heap-allocate shared state so the lambda is safe even if wait_for times out first.
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string tabHit;
            int resolvedX = 0, resolvedY = 0;
            float resolvedU = 0.0f, resolvedV = 0.0f;
        };
        auto state = std::make_shared<State>();
        state->resolvedX = x;
        state->resolvedY = y;
        state->resolvedU = useUV ? u : -1.0f;
        state->resolvedV = useUV ? v : -1.0f;

        QueueAction([this, state, useUV, u, v, x, y, action, scrollX, scrollY]()
                    {
            // Resolve coordinates using the stored SDL_Window* (safe on any thread).
            int rx = x, ry = y;
            {
                auto *sdlWin = static_cast<SDL_Window *>(m_sdlWindow);
                int winW = 0, winH = 0;
                if (sdlWin)
                    SDL_GetWindowSize(sdlWin, &winW, &winH);
                if (useUV)
                {
                    rx = static_cast<int>(u * winW);
                    ry = static_cast<int>(v * winH);
                    state->resolvedX = rx;
                    state->resolvedY = ry;
                    state->resolvedU = (winW > 0) ? static_cast<float>(rx) / winW : u;
                    state->resolvedV = (winH > 0) ? static_cast<float>(ry) / winH : v;
                }
                else
                {
                    state->resolvedU = (winW > 0) ? static_cast<float>(rx) / winW : 0.0f;
                    state->resolvedV = (winH > 0) ? static_cast<float>(ry) / winH : 0.0f;
                }
            }

            ImGuiContext *ctx = ImGui::GetCurrentContext();
            ImGuiIO &io = ImGui::GetIO();
            ImVec2 pos{static_cast<float>(rx), static_cast<float>(ry)};

            SDL_Window *focusWin = SDL_GetMouseFocus();
            SDL_Window *mainWin = static_cast<SDL_Window *>(m_sdlWindow);
            if (SDL_Window *win = focusWin ? focusWin : mainWin)
                SDL_WarpMouseInWindow(win, rx, ry);

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
                        state->tabHit = name ? name : "";
                        if (auto p = state->tabHit.find("##"); p != std::string::npos)
                            state->tabHit = state->tabHit.substr(0, p);
                        handledByTabBar = true;
                        break;
                    }
                    if (handledByTabBar)
                        break;
                }
            }

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
                    io.AddMouseButtonEvent(btn, true);
                    io.AddMouseButtonEvent(btn, false); // release in the same frame → clean click, no stuck-button risk
                }
            }

            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                { return state->done; }))
            return "{\"error\":\"timeout waiting for main thread\"}";

        nlohmann::json res{{"ok", true}, {"x", state->resolvedX}, {"y", state->resolvedY}, {"u", state->resolvedU}, {"v", state->resolvedV}, {"action", action}};
        if (!state->tabHit.empty())
            res["tab_focused"] = state->tabHit;
        return res.dump();
    }

    std::string EditorToolRuntime::TakeProfilerSnapshot() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            auto *profiler = renderer ? renderer->GetGUI().GetWidget<ProfilerWidget>() : nullptr;
            if (!profiler)
                state->result = "{\"error\":\"ProfilerWidget not available\"}";
            else
            {
                const std::string path = profiler->TakeSnapshot();
                if (path.empty())
                    state->result = "{\"error\":\"snapshot failed\"}";
                else
                    state->result = JsonObj({{"path", JsonStr(path)}});
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for profiler snapshot\"}";
        }
        return state->result;
    }
} // namespace pe
