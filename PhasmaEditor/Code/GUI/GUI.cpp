#include "GUI.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "API/Vertex.h"
#include "API/Shader.h"
#include "API/RHI.h"
#include "Scene/Model.h"
#include "Systems/CameraSystem.h"
#include "Window/Window.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/Framebuffer.h"
#include "API/RenderPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Semaphore.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    GUI::GUI()
        : m_render{true},
          m_attachment{std::make_unique<Attachment>()},
          m_show_demo_window{false}
    {
    }

    GUI::~GUI()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    static void ApplyNiceTheme()
    {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowRounding = 8.0f;
        s.ChildRounding = 8.0f;
        s.FrameRounding = 6.0f;
        s.GrabRounding = 6.0f;
        s.PopupRounding = 6.0f;
        s.ScrollbarRounding = 6.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 8);
        s.WindowPadding = ImVec2(14, 10);
        s.SeparatorTextPadding = ImVec2(12, 6);

        auto &c = s.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
        c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.60f);
        c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.34f, 1.00f);
        c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.18f);
        c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.12f);
    }

    static void DrawBarBackground(const ImVec2 &p0, const ImVec2 &p1, ImDrawList *dl)
    {
        const ImU32 bg = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        const ImU32 brdr = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f));
        const float r = 6.0f;

        // soft shadow (bottom)
        dl->AddRectFilled(ImVec2(p0.x, p0.y + 1), ImVec2(p1.x, p1.y + 2), IM_COL32(0, 0, 0, 45), r);
        // background + border
        dl->AddRectFilled(p0, p1, bg, r);
        dl->AddRect(p0, p1, brdr, r);

        // ticks at 25/50/75%
        const float W = (p1.x - p0.x);
        const float H = (p1.y - p0.y);
        for (int i : {25, 50, 75})
        {
            float x = p0.x + (i / 100.0f) * W;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + H), IM_COL32(255, 255, 255, 30), 1.0f);
        }

        // top glossy hairline
        dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p0.y + 1), IM_COL32(255, 255, 255, 35), 1.0f);
    }

    // https://github.com/ocornut/imgui/issues/1901#issuecomment-444929973
    void LoadingIndicatorCircle(const char *label, const float indicator_radius,
                                const ImVec4 &main_color, const ImVec4 &backdrop_color,
                                const int circle_count, const float speed)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext &g = *GImGui;
        const ImGuiID id = window->GetID(label);

        const ImVec2 pos = window->DC.CursorPos;
        const float circle_radius = indicator_radius / 10.f;
        const ImRect bb(pos, ImVec2(pos.x + indicator_radius * 2.0f, pos.y + indicator_radius * 2.0f));
        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return;

        const float t = (float)g.Time;
        const auto degree_offset = 2.0f * IM_PI / circle_count;
        for (int i = 0; i < circle_count; ++i)
        {
            const auto x = indicator_radius * std::sin(degree_offset * i) * 0.9f;
            const auto y = indicator_radius * std::cos(degree_offset * i) * 0.9f;
            const auto growth = std::max(0.0f, std::sin(t * speed - i * degree_offset));
            ImVec4 color;
            color.x = main_color.x * growth + backdrop_color.x * (1.0f - growth);
            color.y = main_color.y * growth + backdrop_color.y * (1.0f - growth);
            color.z = main_color.z * growth + backdrop_color.z * (1.0f - growth);
            color.w = 1.0f;
            window->DrawList->AddCircleFilled(ImVec2(pos.x + indicator_radius + x,
                                                     pos.y + indicator_radius - y),
                                              circle_radius + growth * circle_radius,
                                              ImGui::GetColorU32(color));
        }
    }

    static std::atomic_bool s_modelLoading = false;

    void GUI::async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle,
                                             const std::vector<const char *> &filter)
    {
        if (ImGui::MenuItem(menuLabel))
        {
            if (s_modelLoading)
                return;

            auto lambda = [dialogTitle, filter]()
            {
                const char *result = tinyfd_openFileDialog(dialogTitle, "", static_cast<int>(filter.size()), filter.data(), "", 0);
                if (result)
                {
                    auto loadAsync = [result]()
                    {
                        s_modelLoading = true;

                        std::filesystem::path path(result);
                        ModelGltf::Load(path);

                        s_modelLoading = false;
                    };
                    e_GUI_ThreadPool.Enqueue(loadAsync);
                }
            };

            e_GUI_ThreadPool.Enqueue(lambda);
        }
    }

    void GUI::async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message)
    {
        if (ImGui::MenuItem(menuLabel))
        {
            auto lambda = [messageBoxTitle, message]()
            {
                int result = tinyfd_messageBox(messageBoxTitle, message, "yesno", "warning", 0);
                if (result == 1)
                {
                    EventSystem::PushEvent(EventQuit);
                }
            };
            e_GUI_ThreadPool.Enqueue(lambda);
        }
    }

    // ---------- helpers ----------
    static inline std::string FormatBytes(uint64_t b)
    {
        const double KB = 1024.0, MB = KB * KB, GB = KB * KB * KB;
        char buf[64];
        if (b >= (uint64_t)GB)
            snprintf(buf, sizeof(buf), "%.2f GB", b / GB);
        else if (b >= (uint64_t)MB)
            snprintf(buf, sizeof(buf), "%.2f MB", b / MB);
        else
            snprintf(buf, sizeof(buf), "%.0f KB", b / KB);
        return buf;
    }

    static inline ImU32 U32(const ImVec4 &c) { return ImGui::GetColorU32(c); }

    static inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    static ImVec4 UsageColor(float frac)
    { // 0..1 → green→yellow→red
        frac = Clamp01(frac);
        ImVec4 g(0.20f, 0.80f, 0.25f, 0.95f);
        ImVec4 y(0.95f, 0.80f, 0.25f, 0.95f);
        ImVec4 r(0.85f, 0.25f, 0.25f, 0.95f);
        return (frac < 0.5f)
                   ? ImVec4(g.x + (y.x - g.x) * (frac / 0.5f), g.y + (y.y - g.y) * (frac / 0.5f), g.z + (y.z - g.z) * (frac / 0.5f), 0.95f)
                   : ImVec4(y.x + (r.x - y.x) * ((frac - 0.5f) / 0.5f), y.y + (r.y - y.y) * ((frac - 0.5f) / 0.5f), y.z + (r.z - y.z) * ((frac - 0.5f) / 0.5f), 0.95f);
    }

    // RAM bar: [System Used (others, grey)] + [App Used (gradient)] over System Total
    static void DrawRamBar(const char *label,
                           uint64_t sysTotal,
                           uint64_t appUsed, // process (Private Bytes)
                           uint64_t sysUsed, // total system used (includes your app)
                           ImVec2 size = ImVec2(-FLT_MIN, 20.0f))
    {
        if (sysTotal == 0)
            sysTotal = 1;

        appUsed = std::min(appUsed, sysTotal);
        sysUsed = std::min(sysUsed, sysTotal);

        // System used excluding our app
        const uint64_t sysOtherUsed = (sysUsed > appUsed) ? (sysUsed - appUsed) : 0;

        const float fApp = (float)appUsed / (float)sysTotal;
        const float fOther = (float)sysOtherUsed / (float)sysTotal;

        auto UsageColor = [](float f) -> ImVec4
        {
            f = std::clamp(f, 0.0f, 1.0f);
            ImVec4 g(0.20f, 0.80f, 0.25f, 0.95f), y(0.95f, 0.80f, 0.25f, 0.95f), r(0.85f, 0.25f, 0.25f, 0.95f);
            return (f < 0.5f)
                       ? ImVec4(g.x + (y.x - g.x) * (f / 0.5f), g.y + (y.y - g.y) * (f / 0.5f), g.z + (y.z - g.z) * (f / 0.5f), 0.95f)
                       : ImVec4(y.x + (r.x - y.x) * ((f - 0.5f) / 0.5f), y.y + (r.y - y.y) * ((f - 0.5f) / 0.5f), y.z + (r.z - y.z) * ((f - 0.5f) / 0.5f), 0.95f);
        };

        const ImVec4 colBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        const ImVec4 colBrdr = ImVec4(1, 1, 1, 0.25f);
        const ImVec4 colOther = ImVec4(0.60f, 0.60f, 0.60f, 0.90f); // grey base
        const ImVec4 colApp = UsageColor(fApp);

        ImGui::TextUnformatted(label);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (size.x <= 0)
            size.x = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
        auto *dl = ImGui::GetWindowDrawList();
        const float W = (p1.x - p0.x);

        // frame
        DrawBarBackground(p0, p1, dl);

        // 1) System Used (others) base
        float xOther = p0.x + fOther * W;
        dl->AddRectFilled(p0, ImVec2(xOther, p1.y), ImGui::GetColorU32(colOther), 4.0f);

        // 2) App Used overlay (stacked after others)
        float xApp0 = xOther;
        float xApp1 = p0.x + (fOther + fApp) * W;
        dl->AddRectFilled(ImVec2(xApp0, p0.y), ImVec2(xApp1, p1.y), ImGui::GetColorU32(colApp), 4.0f);

        // Center text: ONLY app / total (%)
        ImGui::PushID(label);
        ImGui::InvisibleButton("##ram_bar", size);
        {
            char overlay[128];
            const int pctApp = (int)std::round(100.0 * fApp);
            snprintf(overlay, sizeof(overlay), "%s / %s  (%d%%)",
                     FormatBytes(appUsed).c_str(), FormatBytes(sysTotal).c_str(), pctApp);
            ImVec2 ts = ImGui::CalcTextSize(overlay);
            ImVec2 tp = ImVec2(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f);
            dl->AddText(tp, ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Text]), overlay);
        }

        // Tooltip: app / total + system used (other)
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", label);
            ImGui::Separator();
            ImGui::Text("App Used:            %s", FormatBytes(appUsed).c_str());
            ImGui::Text("System Used (other): %s", FormatBytes(sysOtherUsed).c_str());
            ImGui::Separator();
            ImGui::Text("Total RAM:           %s", FormatBytes(sysTotal).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    static void DrawGpuBar(const char *label,
                           uint64_t used, uint64_t total, // total = budget
                           ImVec2 size = ImVec2(-FLT_MIN, 20.0f))
    {
        if (total == 0)
            total = 1;
        used = std::min(used, total);
        const float f = (float)used / (float)total;
        const ImVec4 colFill = UsageColor(f);

        ImGui::TextUnformatted(label);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (size.x <= 0)
            size.x = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
        auto *dl = ImGui::GetWindowDrawList();

        const ImVec4 colBg = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        const ImVec4 colBrdr = ImVec4(1, 1, 1, 0.25f);

        dl->AddRectFilled(p0, p1, U32(colBg), 4.0f);
        dl->AddRect(p0, p1, U32(colBrdr), 4.0f);

        float x = p0.x + Clamp01(f) * (p1.x - p0.x);
        dl->AddRectFilled(p0, ImVec2(x, p1.y), U32(colFill), 4.0f);

        ImGui::PushID(label);
        ImGui::InvisibleButton("##gpu_bar", size);

        // centered overlay with %
        char overlay[96];
        int pct = (int)std::round(100.0 * f);
        snprintf(overlay, sizeof(overlay), "%s / %s  (%d%%)",
                 FormatBytes(used).c_str(), FormatBytes(total).c_str(), pct);
        ImVec2 ts = ImGui::CalcTextSize(overlay);
        ImVec2 tp = ImVec2(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f);
        dl->AddText(tp, U32(ImGui::GetStyle().Colors[ImGuiCol_Text]), overlay);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", label);
            ImGui::Separator();
            ImGui::Text("Used:  %s (%d%% of total)", FormatBytes(used).c_str(), pct);
            ImGui::Text("Total: %s", FormatBytes(total).c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    static bool metrics_open = true;
    static bool properties_open = true;
    static bool shaders_open = false;
    static bool models_open = false;
    static bool scripts_open = false;

    void GUI::Menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                static std::vector<const char *> filter{"*.gltf", "*.glb"};
                async_fileDialog_ImGuiMenuItem("Load...", "Choose Model", filter);
                async_messageBox_ImGuiMenuItem("Exit", "Exit", "Are you sure you want to exit?");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Windows"))
            {
                ImGui::Checkbox("Metrics", &metrics_open);
                ImGui::Checkbox("Properties", &properties_open);
                ImGui::Checkbox("Shaders", &shaders_open);
                ImGui::Checkbox("Models", &models_open);
                ImGui::Checkbox("Scripts", &scripts_open);

                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void GUI::Loading()
    {
        static const ImVec4 color{0, 232.f / 256, 224.f / 256, 1.f};
        static const ImVec4 bdcolor{0.f, 168.f / 256, 162.f / 256, 1.f};
        static const float radius = 25.f;
        static const int flags = ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBackground;

        auto &gSettings = Settings::Get<GlobalSettings>();
        if (s_modelLoading)
        {
            int x, y;
            const float topBarHeight = 16.f;
            SDL_GetWindowPosition(RHII.GetWindow(), &x, &y);
            ImVec2 size(-0.001f, 25.f);
            ImGui::SetNextWindowPos(ImVec2(x + RHII.GetWidthf() / 2 - 200.f, y + topBarHeight + RHII.GetHeightf() / 2 - size.y / 2));
            ImGui::SetNextWindowSize(ImVec2(400.f, 100.f));
            ImGui::Begin(gSettings.loading_name.c_str(), &metrics_open, flags);
            // LoadingIndicatorCircle("Loading", radius, color, bdcolor, 10, 4.5f);
            const float progress = gSettings.loading_current / static_cast<float>(gSettings.loading_total);
            ImGui::ProgressBar(progress, size);
            ImGui::End();
        }
    }

    void SetTextColorTemp(float time, float maxTime)
    {
        const ImVec4 startColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // White
        const ImVec4 endColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);   // Red

        time = std::clamp(time / maxTime, 0.0f, 1.0f);

        ImVec4 resultColor(
            startColor.x + time * (endColor.x - startColor.x), // Red
            startColor.y + time * (endColor.y - startColor.y), // Green
            startColor.z + time * (endColor.z - startColor.z), // Blue
            1.0f                                               // Alpha
        );

        ImGui::GetStyle().Colors[ImGuiCol_Text] = resultColor;
    }

    void ResetTextColor()
    {
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    }

#if PE_DEBUG_MODE
    float GUI::FetchTotalGPUTime()
    {
        float total = 0.f;
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
        {
            // Top level timers only
            if (gpuTimerInfo.depth == 0)
                total += gpuTimerInfo.timer->GetTime();
        }

        return total;
    }

    void GUI::ShowGpuTimings(float maxTime)
    {
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
        {
            // Top level timers are in CommandBuffer::Begin
            if (gpuTimerInfo.depth == 0)
                continue;

            if (gpuTimerInfo.depth > 0)
                ImGui::Indent(gpuTimerInfo.depth * 16.0f);

            float time = gpuTimerInfo.timer->GetTime();
            SetTextColorTemp(time, maxTime);
            ImGui::Text("%s: %.3f ms", gpuTimerInfo.name.c_str(), time);

            if (gpuTimerInfo.depth > 0)
                ImGui::Unindent(gpuTimerInfo.depth * 16.0f);
        }
    }
#endif

    void GUI::Metrics()
    {
        if (!metrics_open)
            return;

        static auto &gSettings = Settings::Get<GlobalSettings>();
        static std::deque<float> fpsDeque(100, 0.0f);
        static std::vector<float> fpsVector(100, 0.0f);
        static float highestValue = 100.0f;

        static Timer delay;
        float framerate = ImGui::GetIO().Framerate;
        if (delay.Count() > 0.2)
        {
            delay.Start();

            fpsDeque.pop_front();
            fpsDeque.push_back(framerate);
            highestValue = max(highestValue, framerate + 60.0f);
            std::copy(fpsDeque.begin(), fpsDeque.end(), fpsVector.begin());
        }

        ImGui::Begin("Metrics", &metrics_open);
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Average %.3f ms (%.1f FPS)", 1000.0f / framerate, framerate);
        ImGui::PlotLines("##FrameTimes", fpsVector.data(), static_cast<int>(fpsVector.size()), 0, NULL, 0.0f, highestValue, ImVec2(0, 80));
        ImGui::Checkbox("Unlock FPS", &gSettings.unlock_fps);
        if (!gSettings.unlock_fps)
        {
            ImGui::DragInt("FPS", &gSettings.target_fps, 0.1f);
            gSettings.target_fps = max(gSettings.target_fps, 10);
        }

        // Memory Usage
        ImGui::Separator();
        const auto ram = RHII.GetSystemAndProcessMemory();
        const auto gpu = RHII.GetGpuMemorySnapshot();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f)); // compact Y spacing
        // 1) RAM — system used (other) / process (Private Bytes) / system total
        DrawRamBar("RAM", ram.sysTotal, ram.procPrivateBytes, ram.sysUsed);
        // 2) GPU (Host Visible) — used / budget
        DrawGpuBar("GPU (Host Visible)", gpu.host.used, gpu.host.budget);
        // 3) GPU (Local/VRAM) — used / budget
        DrawGpuBar("GPU (Local)", gpu.vram.used, gpu.vram.budget);
        ImGui::PopStyleVar();
        ImGui::Separator();

        FrameTimer &frameTimer = FrameTimer::Instance();
        ImGui::Text("CPU Total: %.3f ms", static_cast<float>(MILLI(frameTimer.GetCpuTotal())));
        ImGui::Indent(16.0f);
        SetTextColorTemp(static_cast<float>(frameTimer.GetUpdatesStamp()), static_cast<float>(frameTimer.GetCpuTotal()));
        ImGui::Text("CPU Updates: %.3f ms", static_cast<float>(MILLI(frameTimer.GetUpdatesStamp())));
        SetTextColorTemp(static_cast<float>(frameTimer.GetCpuTotal() - frameTimer.GetUpdatesStamp()), static_cast<float>(frameTimer.GetCpuTotal()));
        ImGui::Text("CPU Draw: %.3f ms", static_cast<float>(MILLI(frameTimer.GetCpuTotal() - frameTimer.GetUpdatesStamp())));
        ResetTextColor();
        ImGui::Unindent(16.0f);

#if PE_DEBUG_MODE
        float gpuTotal = FetchTotalGPUTime();
        ImGui::Text("GPU Total: %.3f ms", gpuTotal);
        ShowGpuTimings(gpuTotal);
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        m_gpuTimerInfos.clear();
#endif
        ImGui::End();
    }

    void GUI::Shaders()
    {
        if (!shaders_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        ImGui::Begin("Shaders Folder", &shaders_open);
        if (ImGui::Button("Compile Shaders"))
        {
            EventSystem::PushEvent(EventCompileShaders);
        }
        for (uint32_t i = 0; i < gSettings.shader_list.size(); i++)
        {
            std::string name = gSettings.shader_list[i] + "##" + std::to_string(i);
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    std::string s = Path::Executable + "Assets/Shaders/" + gSettings.shader_list[i];
                    auto lambda = [s]()
                    { system(s.c_str()); };
                    e_GUI_ThreadPool.Enqueue(lambda);
                }
            }
        }
        ImGui::End();
    }

    void GUI::Properties()
    {
        if (!properties_open)
            return;

        static bool initialized = false;
        if (!initialized)
        {
            ImGui::SetNextWindowPos(ImVec2(RHII.GetWidthf() - 200.f, 100.f));
            ImGui::SetNextWindowSize(ImVec2(200.f, RHII.GetHeightf() - 150.f));
            initialized = true;
        }

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &srSettings = Settings::Get<SRSettings>();

        static float rtScale = gSettings.render_scale;
        ImGui::Begin("Global Properties", &properties_open);
        ImGui::Text("Resolution: %d x %d", static_cast<int>(RHII.GetWidthf() * gSettings.render_scale), static_cast<int>(RHII.GetHeightf() * gSettings.render_scale));
        ImGui::DragFloat("Quality", &rtScale, 0.01f, 0.05f, 1.0f);
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = clamp(rtScale, 0.1f, 4.0f);
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventResize);
        }
        ImGui::Separator();

        ImGui::Text("Present Mode");
        static vk::PresentModeKHR currentPresentMode = RHII.GetSurface()->GetPresentMode();

        if (ImGui::BeginCombo("##present_mode", PresentModeToString(currentPresentMode)))
        {
            const auto &presentModes = RHII.GetSurface()->GetSupportedPresentModes();
            for (uint32_t i = 0; i < static_cast<uint32_t>(presentModes.size()); i++)
            {
                const bool isSelected = (currentPresentMode == presentModes[i]);
                if (ImGui::Selectable(PresentModeToString(presentModes[i]), isSelected))
                {
                    if (currentPresentMode != presentModes[i])
                    {
                        currentPresentMode = presentModes[i];
                        gSettings.preferred_present_mode = currentPresentMode;
                        EventSystem::PushEvent(EventPresentMode);
                    }
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();

        ImGui::Checkbox("IBL", &gSettings.IBL);
        if (gSettings.IBL)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("IBL Intensity", &gSettings.IBL_intensity, 0.01f, 0.0f, 10.0f);
            ImGui::Unindent(16.0f);
        }
        ImGui::Checkbox("SSR", &gSettings.ssr);
        ImGui::Checkbox("SSAO", &gSettings.ssao);
        ImGui::Checkbox("Depth of Field", &gSettings.dof);
        if (gSettings.dof)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Scale##DOF", &gSettings.dof_focus_scale, 0.05f, 0.5f);
            ImGui::DragFloat("Range##DOF", &gSettings.dof_blur_range, 0.05f, 0.5f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Motion Blur", &gSettings.motion_blur);
        if (gSettings.motion_blur)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Strength#mb", &gSettings.motion_blur_strength, 0.05f, 0.2f);
            ImGui::DragInt("Samples#mb", &gSettings.motion_blur_samples, 0.1f, 8, 64);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Tone Mapping", &gSettings.tonemapping);
#if 0
        ImGui::Checkbox("FSR2", &srSettings.enable);
#else
        // TODO: FSR2 is out of sync and out of date
        ImGui::Checkbox("FSR2 (broken)", &srSettings.enable);
        srSettings.enable = false;
#endif
        if (srSettings.enable)
        {
            ImGui::Indent(16.0f);
            ImGui::InputFloat2("Motion", (float *)&srSettings.motionScale);
            ImGui::InputFloat2("Proj", (float *)&srSettings.projScale);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        ImGui::Checkbox("FXAA", &gSettings.fxaa);

        ImGui::Checkbox("Bloom", &gSettings.bloom);
        if (gSettings.bloom)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength", &gSettings.bloom_strength, 0.01f, 10.f);
            ImGui::SliderFloat("Range", &gSettings.bloom_range, 0.1f, 20.f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        if (ImGui::Checkbox("Sun Light", &gSettings.shadows))
        {
            // Update light pass descriptor sets for the skybox change
            LightOpaquePass &lightOpaquePass = *GetGlobalComponent<LightOpaquePass>();
            lightOpaquePass.UpdateDescriptorSets();
            LightTransparentPass &lightTransparentPass = *GetGlobalComponent<LightTransparentPass>();
            lightTransparentPass.UpdateDescriptorSets();
        }
        if (gSettings.shadows)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Intst", &gSettings.sun_intensity, 0.1f, 50.f);
            ImGui::DragFloat3("Dir", gSettings.sun_direction.data(), 0.01f, -1.f, 1.f);
            ImGui::DragFloat("Slope", &gSettings.depth_bias[2], 0.15f, 0.5f);
            ImGui::Separator();
            {
                vec3 direction = normalize(make_vec3(&gSettings.sun_direction[0]));
                gSettings.sun_direction[0] = direction.x;
                gSettings.sun_direction[1] = direction.y;
                gSettings.sun_direction[2] = direction.z;
            }
            ImGui::Unindent(16.0f);
        }

        ImGui::DragFloat("CamSpeed", &gSettings.camera_speed, 0.1f, 1.f);
        ImGui::DragFloat("TimeScale", &gSettings.time_scale, 0.05f, 0.2f);
        ImGui::Separator();
        if (ImGui::Button("Randomize Lights"))
            gSettings.randomize_lights = true;
        ImGui::SliderFloat("Light Intst", &gSettings.lights_intensity, 0.01f, 30.f);
        ImGui::SliderFloat("Light Rng", &gSettings.lights_range, 0.1f, 30.f);
        ImGui::Checkbox("Frustum Culling", &gSettings.frustum_culling);
        ImGui::Checkbox("FreezeCamCull", &gSettings.freeze_frustum_culling);
        ImGui::Checkbox("Draw AABBs", &gSettings.draw_aabbs);
        if (gSettings.draw_aabbs)
        {
            bool depthAware = gSettings.aabbs_depth_aware;
            ImGui::Indent(16.0f);
            ImGui::Checkbox("Depth Aware", &gSettings.aabbs_depth_aware);
            ImGui::Unindent(16.0f);
        }
        ImGui::DragInt("Culls Per Task", &gSettings.culls_per_task, 1.f, 1, 100);

        ImGui::End();
    }

    void GUI::Init()
    {
        //                   counter clock wise
        // x, y, z coords orientation   // u, v coords orientation
        //          |  /|               // (0,0)-------------> u
        //          | /  +z             //     |
        //          |/                  //     |
        //  --------|-------->          //     |
        //         /|       +x          //     |
        //        /	|                   //     |
        //       /  |/ +y               //     |/ v

        auto &gSettings = Settings::Get<GlobalSettings>();
        std::string directory = Path::Executable + "Assets/Scripts";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (pathStr.ends_with(".cs"))
                gSettings.file_list.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
        }

        directory = Path::Executable + "Assets/Shaders";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (pathStr.ends_with(".vert") || pathStr.ends_with(".frag") || pathStr.ends_with(".comp") ||
                pathStr.ends_with(".glsl"))
            {
                gSettings.shader_list.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
            }
        }

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;          // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;        // Enable multiple viewports
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports; // Backend Renderer supports multiple viewports.

        ImGui::StyleColorsClassic();
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.3f);
        ApplyNiceTheme();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = vk::AttachmentLoadOp::eLoad;
        VkFormat format = static_cast<VkFormat>(RHII.GetSurface()->GetFormat());
        Queue *queue = RHII.GetMainQueue();

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = RHII.GetInstance();
        init_info.PhysicalDevice = RHII.GetGpu();
        init_info.Device = RHII.GetDevice();
        init_info.QueueFamily = queue->GetFamilyId();
        init_info.Queue = queue->ApiHandle();
        init_info.PipelineCache = nullptr;
        init_info.DescriptorPool = RHII.GetDescriptorPool()->ApiHandle();
        init_info.Subpass = 0;
        init_info.MinImageCount = RHII.GetSwapchain()->GetImageCount();
        init_info.ImageCount = RHII.GetSwapchain()->GetImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;
        if (gSettings.dynamic_rendering)
        {
            init_info.UseDynamicRendering = true;
            init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        }
        else
        {
            RenderPass *renderPass = CommandBuffer::GetRenderPass(1, m_attachment.get());
            init_info.UseDynamicRendering = false;
            init_info.RenderPass = renderPass->ApiHandle();
        }

        ImGui_ImplVulkan_Init(&init_info);
        ImGui_ImplVulkan_CreateFontsTexture();

        auto &gpuTimerInfos = m_gpuTimerInfos;
        auto AddGpuTimerInfo = [&gpuTimerInfos](std::any &&data)
        {
            try
            {
                auto &commandTimerInfos = std::any_cast<std::vector<GpuTimerInfo> &>(data);
                auto *cmd = commandTimerInfos[0].timer->GetCommandBuffer();
                uint32_t count = cmd->GetGpuTimerInfosCount();
                gpuTimerInfos.insert(gpuTimerInfos.end(), commandTimerInfos.begin(), commandTimerInfos.begin() + count);
            }
            catch (const std::bad_any_cast &)
            {
                PE_ERROR("Bad any cast in GUI::Init()::AddGpuTimerInfo " + e.what());
            }
        };
        EventSystem::RegisterCallback(EventAfterCommandWait, AddGpuTimerInfo);

        queue->WaitIdle();
    }

    void GUI::Draw(CommandBuffer *cmd)
    {
        if (!m_render || ImGui::GetDrawData()->TotalVtxCount <= 0)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();
        m_attachment->image = displayRT;

        ImageBarrierInfo barrierInfo{};
        barrierInfo.image = displayRT;
        barrierInfo.layout = vk::ImageLayout::eAttachmentOptimal;
        barrierInfo.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrierInfo.accessMask = vk::AccessFlagBits2::eColorAttachmentRead;

        cmd->ImageBarrier(barrierInfo);
        cmd->BeginPass(1, m_attachment.get(), "GUI", !Settings::Get<GlobalSettings>().dynamic_rendering);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->ApiHandle());
        cmd->EndPass();

        ImGui::RenderPlatformWindowsDefault(); // draws the windows outside the surface
    }

    void GUI::Update()
    {
        if (!m_render)
            return;

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        Menu();
        Loading();
        Metrics();
        Properties();
        Shaders();
    }
}
