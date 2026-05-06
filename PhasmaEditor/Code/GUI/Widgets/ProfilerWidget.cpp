#include "ProfilerWidget.h"
#include "API/Debug.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#include "Base/EventSystem.h"
#include "Base/Path.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui_impl_vulkan.h"

namespace pe
{
    // ─── helpers ────────────────────────────────────────────────────────────────

    static void CenteredTinyBar(float frac, float barHeight = 12.f)
    {
        frac = ui::Clamp01(frac);
        float rowH = ImGui::GetTextLineHeightWithSpacing();
        float yOff = (rowH - barHeight) * 0.5f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        p0.y += yOff;
        ImVec2 p1 = {p0.x + ImGui::GetContentRegionAvail().x, p0.y + barHeight};
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, ui::U32(ImVec4(1, 1, 1, 0.08f)), 4.f);
        dl->AddRect(p0, p1, ui::U32(ImVec4(1, 1, 1, 0.20f)), 4.f);
        ImVec2 f1 = {p0.x + frac * (p1.x - p0.x), p1.y};
        dl->AddRectFilled(p0, f1, ui::U32(ui::Heat(frac)), 4.f);
        ImGui::Dummy({0, rowH});
    }

    // ─── GPU timing table ────────────────────────────────────────────────────────

    void ProfilerWidget::DrawGpuTimingTable(const std::vector<GpuTimerSample> &samples,
                                            float totalMs, const char *filter,
                                            int &selectedPass)
    {
        if (samples.empty())
        {
            ImGui::TextDisabled("No GPU timer data. Run at least one frame.");
            return;
        }

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY;

        const float rowH = ImGui::GetTextLineHeightWithSpacing() + 2.f;
        if (ImGui::BeginTable("##gpu_timing", 6, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.40f);
            ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("cur", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)samples.size(); ++i)
            {
                const auto &s = samples[i];
                if (s.depth == 0)
                    continue;
                if (s.timeMs <= 0.f)
                    continue;

                if (filter[0] != '\0')
                {
                    std::string_view hay(s.name), needle(filter);
                    bool found = std::search(hay.begin(), hay.end(),
                                             needle.begin(), needle.end(),
                                             [](unsigned char a, unsigned char b)
                                             { return std::tolower(a) == std::tolower(b); }) != hay.end();
                    if (!found)
                        continue;
                }

                float rel = totalMs > 0.f ? s.timeMs / totalMs : 0.f;
                bool selected = (selectedPass == i);

                auto statsIt = m_gpuStats.find(s.name);
                const ScopeStats *st = (statsIt != m_gpuStats.end()) ? &statsIt->second : nullptr;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                if (ImGui::Selectable(("##sel_" + std::to_string(i)).c_str(),
                                      selected,
                                      ImGuiSelectableFlags_SpanAllColumns,
                                      {0, rowH}))
                    selectedPass = selected ? -1 : i;

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", s.name.c_str());
                    ImGui::TextColored(ui::Heat(rel), "%.3f ms  (%.1f%%)", s.timeMs, rel * 100.f);
                    if (st)
                    {
                        ImGui::TextDisabled("min %.3f  avg %.3f  max %.3f",
                                            st->minMs, st->avgMs, st->maxMs);
                    }
                    DrawPassImageTooltip(s.name);
                    ImGui::EndTooltip();
                }

                ImGui::SameLine();
                ui::DrawHierarchyCell(s.name.c_str(), static_cast<int>(s.depth) - 1, rel);
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->minMs : 0.f);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ui::Heat(rel), "%.3f", s.timeMs);
                ImGui::TableSetColumnIndex(3);
                if (st)
                    ImGui::TextColored(ui::Heat(st->maxMs / totalMs), "%.3f", st->maxMs);
                else
                    ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->avgMs : 0.f);
                ImGui::TableSetColumnIndex(5);
                CenteredTinyBar(rel);
            }
            ImGui::EndTable();
        }
    }

    // ─── Destructor ──────────────────────────────────────────────────────────────

    ProfilerWidget::~ProfilerWidget()
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
            for (auto &[img, ds] : m_rtDescriptorCache)
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)ds);
    }

    // ─── RT descriptor cache for hover previews ──────────────────────────────────

    void *ProfilerWidget::GetRTDescriptor(Image *image)
    {
        if (!image || !image->HasSRV() || !image->GetSampler())
            return nullptr;
        if (RHII.GetApi() != PE_GRAPHICS_API_VULKAN)
            return nullptr;

        auto it = m_rtDescriptorCache.find(image);
        if (it != m_rtDescriptorCache.end())
            return it->second;

        void *ds = (void *)ImGui_ImplVulkan_AddTexture(
            pe::GetVulkanSampler(image->GetSampler()),
            pe::GetVulkanImageView(image->GetSRV()),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_rtDescriptorCache[image] = ds;
        return ds;
    }

    void ProfilerWidget::DrawPassImageTooltip(const std::string &passName)
    {
        struct PassMapping
        {
            const char *pattern;
            const char *colorRTs[8]; // null-terminated color RT names (via GetRenderTarget)
            const char *depthRTs[3]; // null-terminated depth RT names (via GetDepthStencilTarget)
        };

        // Map GPU timer name substrings → render target names.
        // Pass names come from BeginPass("Foo") → "Foo_pass", or BeginDebugRegion("Bar") → "Bar".
        static const PassMapping kTable[] = {
            {"GbufferOpaque", {"normal", "albedo", "srm", "emissive", "velocity", "viewport", "transparency", nullptr}, {"depthStencil", nullptr}},
            {"GbufferTransparent", {"normal", "albedo", "srm", "emissive", "velocity", "viewport", "transparency", nullptr}, {"depthStencil", nullptr}},
            {"DepthPass", {nullptr}, {"depthStencil", nullptr}},
            {"LightOpaque", {"viewport", nullptr}, {nullptr}},
            {"LightTransparent", {"viewport", nullptr}, {nullptr}},
            {"SSAOPass", {"ssao", nullptr}, {nullptr}},
            {"SSR", {"ssr", nullptr}, {nullptr}},
            {"FXAA", {"viewport", nullptr}, {nullptr}},
            {"TAA", {"viewport", nullptr}, {nullptr}},
            {"RCAS", {"display", nullptr}, {nullptr}},
            {"Tonemap", {"display", nullptr}, {nullptr}},
            {"BrightFilter", {"brightFilter", nullptr}, {nullptr}},
            {"BlurHorizontal", {"gaussianBlurHorizontal", nullptr}, {nullptr}},
            {"BlurVertical", {"gaussianBlurVertical", nullptr}, {nullptr}},
            {"DOF", {"viewport", nullptr}, {nullptr}},
            {"MotionBlur", {"viewport", nullptr}, {nullptr}},
            {"GridPass", {"viewport", nullptr}, {nullptr}},
            {"ParticlePass", {"viewport", nullptr}, {nullptr}},
            {"AabbsPass", {"viewport", nullptr}, {nullptr}},
            {"RayTracing", {"viewport", nullptr}, {nullptr}},
            {"ShadowPass", {nullptr}, {"depthStencil", nullptr}},
            {"Cascade", {nullptr}, {"depthStencil", nullptr}},
        };

        const PassMapping *match = nullptr;
        for (const auto &entry : kTable)
        {
            if (passName.find(entry.pattern) != std::string::npos)
            {
                match = &entry;
                break;
            }
        }
        if (!match || (!match->colorRTs[0] && !match->depthRTs[0]))
            return;

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;

        // If render targets were recreated (resize), the old Image* pointers in the
        // cache are dangling. Detect this by comparing the viewport RT pointer.
        {
            Image *viewportRT = rs->GetRenderTarget("viewport");
            if (viewportRT != m_viewportRTSnapshot && !m_rtDescriptorCache.empty())
            {
                if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
                    for (auto &[img, ds] : m_rtDescriptorCache)
                        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)ds);
                m_rtDescriptorCache.clear();
            }
            m_viewportRTSnapshot = viewportRT;
        }

        struct ThumbEntry
        {
            void *ds; // null for depth placeholders
            const char *name;
            float aspect;
            bool isDepth;
        };
        std::vector<ThumbEntry> thumbs;

        for (int k = 0; k < 8 && match->colorRTs[k]; ++k)
        {
            Image *image = rs->GetRenderTarget(match->colorRTs[k]);
            if (!image)
                continue;
            void *ds = GetRTDescriptor(image);
            if (!ds)
                continue;
            float aspect = image->GetHeight() > 0 ? image->GetWidth_f() / image->GetHeight_f() : 1.f;
            thumbs.push_back({ds, match->colorRTs[k], aspect, false});
        }

        for (int k = 0; k < 3 && match->depthRTs[k]; ++k)
        {
            Image *image = rs->GetDepthStencilTarget(match->depthRTs[k]);
            if (!image)
                continue;
            float aspect = image->GetHeight() > 0 ? image->GetWidth_f() / image->GetHeight_f() : 1.f;
            thumbs.push_back({nullptr, match->depthRTs[k], aspect, true});
        }

        if (thumbs.empty())
            return;

        ImGui::Separator();

        const float thumbH = 80.f;
        ImDrawList *dl = ImGui::GetWindowDrawList();

        for (int k = 0; k < (int)thumbs.size(); ++k)
        {
            if (k > 0)
                ImGui::SameLine(0.f, 6.f);
            const float thumbW = std::max(thumbH * thumbs[k].aspect, 40.f);
            ImGui::BeginGroup();

            if (thumbs[k].isDepth)
            {
                // Placeholder for depth images: hatched dark rect with centered label.
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = {p0.x + thumbW, p0.y + thumbH};
                dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 35, 255));
                // diagonal hatch lines
                const float step = 10.f;
                for (float t = -thumbH; t < thumbW + thumbH; t += step)
                {
                    ImVec2 a = {p0.x + t, p0.y};
                    ImVec2 b = {p0.x + t + thumbH, p1.y};
                    dl->AddLine(a, b, IM_COL32(55, 55, 65, 200), 1.f);
                }
                dl->AddRect(p0, p1, IM_COL32(90, 90, 100, 200));
                // "DEPTH" label centered
                const char *label = "DEPTH";
                ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText({p0.x + (thumbW - ts.x) * 0.5f, p0.y + (thumbH - ts.y) * 0.5f},
                            IM_COL32(160, 160, 170, 255), label);
                ImGui::Dummy(ImVec2(thumbW, thumbH));
            }
            else
            {
                ImGui::Image((ImTextureID)(intptr_t)thumbs[k].ds, ImVec2(thumbW, thumbH));
            }

            ImGui::TextDisabled("%s", thumbs[k].name);
            ImGui::EndGroup();
        }
    }

    // ─── CPU timing table ────────────────────────────────────────────────────────

    static void DrawCpuTimingTable(const std::vector<Profiler::Entry> &entries,
                                   float scopeTotal, const char *filter = "")
    {
        if (entries.empty())
        {
            ImGui::TextDisabled("No CPU scopes. Add PE_PROFILE_SCOPE() to instrument code.");
            return;
        }
        if (scopeTotal <= 0.f)
            scopeTotal = 1.f;

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY;

        const bool filtering = filter[0] != '\0';

        if (ImGui::BeginTable("##cpu_timing", 3, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
        {
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.65f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableHeadersRow();

            int skipDepth = INT_MAX;
            int openTree = 0;

            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto &e = entries[i];
                int depth = static_cast<int>(e.depth);
                float rel = e.timeMs / scopeTotal;

                if (filtering)
                {
                    std::string_view hay(e.name), needle(filter);
                    bool found = std::search(hay.begin(), hay.end(),
                                             needle.begin(), needle.end(),
                                             [](unsigned char a, unsigned char b)
                                             { return std::tolower(a) == std::tolower(b); }) != hay.end();
                    if (!found)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TreeNodeEx(e.name, ImGuiTreeNodeFlags_Leaf |
                                                  ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                  ImGuiTreeNodeFlags_SpanFullWidth);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ui::Heat(rel), "%.3f", e.timeMs);
                    ImGui::TableSetColumnIndex(2);
                    CenteredTinyBar(rel);
                    ImGui::PopID();
                    continue;
                }

                if (depth > skipDepth)
                    continue;
                skipDepth = INT_MAX;

                while (openTree > depth)
                {
                    ImGui::TreePop();
                    openTree--;
                }

                bool hasChildren = (i + 1 < entries.size() &&
                                    entries[i + 1].depth > e.depth);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
                if (hasChildren)
                    nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                else
                    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                bool open = ImGui::TreeNodeEx(e.name, nodeFlags);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ui::Heat(rel), "%.3f", e.timeMs);
                ImGui::TableSetColumnIndex(2);
                CenteredTinyBar(rel);
                ImGui::PopID();

                if (hasChildren)
                {
                    if (open)
                        openTree++;
                    else
                        skipDepth = depth;
                }
            }
            while (openTree > 0)
            {
                ImGui::TreePop();
                openTree--;
            }
            ImGui::EndTable();
        }
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  ProfilerWidget::Update
    // ────────────────────────────────────────────────────────────────────────────

    void ProfilerWidget::Update()
    {
        // Reopen after a RenderDoc capture completes (or immediately if API unavailable)
        if (m_waitingForCapture)
        {
            if (!Debug::IsCaptureApiAvailable() || Debug::GetNumCaptures() > m_captureCountBefore)
            {
                m_waitingForCapture = false;
                m_open = true;
            }
        }

        if (!m_open)
            return;

        // Always drain the GPU timer queue every frame to prevent buffer accumulation
        // and ensure the latest single-frame data is available for display.
        auto latestGpuData = m_gui->PopGpuTimerInfos();

        // Refresh displayed data on a 250 ms interval.
        bool doUpdate = m_firstFrame || m_delay.Count() > 0.25;
        if (doUpdate)
        {
            m_delay.Start();
            m_firstFrame = false;

            if (!m_paused)
            {
                ProfilerData d;

                d.fps = ImGui::GetIO().Framerate;
                d.frameMs = Profiler::GetFrameTimeMs();

                static std::deque<float> hist(200, 0.f);
                hist.pop_front();
                hist.push_back(d.frameMs);
                d.frameTimeVec.assign(hist.begin(), hist.end());
                float histMax = *std::max_element(hist.begin(), hist.end());
                d.frameTimeMax = std::max(2.f, histMax * 1.3f);

                FrameTimer &ft = FrameTimer::Instance();
                d.cpuTotalMs = static_cast<float>(MILLI(ft.GetCpuTotal()));
                d.cpuUpdateMs = static_cast<float>(MILLI(ft.GetUpdatesStamp()));
                d.cpuDrawMs = static_cast<float>(MILLI(ft.GetCpuTotal() - ft.GetUpdatesStamp()));

                d.ram = RHII.GetSystemAndProcessMemory();
                d.gpu = RHII.GetGpuMemorySnapshot();

                d.cpuEntries = Profiler::GetEntries();
                for (const auto &e : d.cpuEntries)
                {
                    if (e.depth == 0)
                        d.cpuScopeTotal += e.timeMs;
                    m_cpuStats[e.name].Push(e.timeMs);
                }

                if (!latestGpuData.empty())
                {
                    d.gpuSamples = std::move(latestGpuData);
                    for (const auto &s : d.gpuSamples)
                    {
                        if (s.depth == 0)
                            d.gpuTotal += s.timeMs;
                        m_gpuStats[s.name].Push(s.timeMs);
                    }
                }

                m_data = std::move(d);
            }
        }

        ui::SetInitialWindowSizeFraction(1.f / 4.f, 2.f / 3.f);
        if (!ImGui::Begin("Profiler", &m_open))
        {
            ImGui::End();
            return;
        }

        // Header line: stats + Reset/Snapshot right-aligned
        ImGui::Text("%.1f FPS  |  CPU %.3f ms  |  GPU %.3f ms",
                    m_data.fps, m_data.frameMs, m_data.gpuTotal);
        ImGui::SameLine();
        {
            const float pad = ImGui::GetStyle().FramePadding.x * 2.f;
            const float resetW = ImGui::CalcTextSize("Reset").x + pad;
            const float snapW = ImGui::CalcTextSize("Snapshot").x + pad;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float totalW = resetW + spacing + snapW;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > totalW)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW);
            if (ImGui::SmallButton("Reset"))
                ResetStats();
            ImGui::SameLine();
            if (ImGui::SmallButton("Snapshot"))
                TakeSnapshot();
        }

        // Pause button on its own line, below the metrics
        {
            const bool wasPaused = m_paused;
            if (wasPaused)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.f));
            if (ImGui::SmallButton(wasPaused ? "Resume" : "Pause"))
                m_paused = !m_paused;
            if (wasPaused)
                ImGui::PopStyleColor();
            if (m_paused)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("PAUSED");
            }
        }

        if (ImGui::BeginTabBar("##profiler_tabs"))
        {
            if (ImGui::BeginTabItem("Overview"))
            {
                DrawOverviewTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("CPU"))
            {
                DrawCpuTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("GPU"))
            {
                DrawGpuTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Capture"))
            {
                DrawCaptureTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // ─── Snapshot ────────────────────────────────────────────────────────────────

    std::string ProfilerWidget::TakeSnapshot()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(PE_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%S", &tm);

        const std::string dir = Path::Assets + "Profiler/";
        std::filesystem::create_directories(dir);
        const std::string path = dir + "profiler_snapshot_" + ts + ".json";

        FILE *f = fopen(path.c_str(), "w");
        if (!f)
        {
            Log::Error("ProfilerWidget: failed to open " + path);
            return {};
        }

        const auto &ram = m_data.ram;
        const auto &gpu = m_data.gpu;

        fprintf(f, "{\n");
        fprintf(f, "  \"timestamp\": \"%s\",\n", ts);

        // overview
        fprintf(f, "  \"overview\": {\n");
        fprintf(f, "    \"fps\": %.1f,\n", m_data.fps);
        fprintf(f, "    \"frame_ms\": %.3f,\n", m_data.frameMs);
        fprintf(f, "    \"cpu_total_ms\": %.3f,\n", m_data.cpuTotalMs);
        fprintf(f, "    \"cpu_update_ms\": %.3f,\n", m_data.cpuUpdateMs);
        fprintf(f, "    \"cpu_draw_ms\": %.3f,\n", m_data.cpuDrawMs);
        fprintf(f, "    \"gpu_total_ms\": %.3f,\n", m_data.gpuTotal);
        fprintf(f, "    \"memory\": {\n");
        fprintf(f, "      \"ram_total_mb\": %llu,\n", (unsigned long long)(ram.sysTotal >> 20));
        fprintf(f, "      \"ram_used_mb\": %llu,\n", (unsigned long long)(ram.sysUsed >> 20));
        fprintf(f, "      \"ram_process_mb\": %llu,\n", (unsigned long long)(ram.procPrivateBytes >> 20));
        fprintf(f, "      \"gpu_vram_app_mb\": %llu,\n", (unsigned long long)(gpu.vram.app >> 20));
        fprintf(f, "      \"gpu_vram_other_mb\": %llu,\n", (unsigned long long)(gpu.vram.other >> 20));
        fprintf(f, "      \"gpu_vram_budget_mb\": %llu,\n", (unsigned long long)(gpu.vram.budget >> 20));
        fprintf(f, "      \"gpu_host_app_mb\": %llu,\n", (unsigned long long)(gpu.host.app >> 20));
        fprintf(f, "      \"gpu_host_other_mb\": %llu,\n", (unsigned long long)(gpu.host.other >> 20));
        fprintf(f, "      \"gpu_host_budget_mb\": %llu\n", (unsigned long long)(gpu.host.budget >> 20));
        fprintf(f, "    }\n");
        fprintf(f, "  },\n");

        // gpu
        fprintf(f, "  \"gpu\": {\n");
        fprintf(f, "    \"total_ms\": %.3f,\n", m_data.gpuTotal);
        fprintf(f, "    \"passes\": [\n");
        for (size_t i = 0; i < m_data.gpuSamples.size(); ++i)
        {
            const auto &s = m_data.gpuSamples[i];
            auto it = m_gpuStats.find(s.name);
            if (it != m_gpuStats.end() && !it->second.samples.empty())
            {
                const auto &st = it->second;
                fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"min_ms\": %.3f, \"cur_ms\": %.3f, \"max_ms\": %.3f, \"avg_ms\": %.3f, \"start_offset_ms\": %.3f}%s\n",
                        s.name.c_str(), (unsigned)s.depth,
                        st.minMs, s.timeMs, st.maxMs, st.avgMs,
                        s.startOffsetMs,
                        (i + 1 < m_data.gpuSamples.size()) ? "," : "");
            }
            else
            {
                fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"cur_ms\": %.3f, \"start_offset_ms\": %.3f}%s\n",
                        s.name.c_str(), (unsigned)s.depth, s.timeMs, s.startOffsetMs,
                        (i + 1 < m_data.gpuSamples.size()) ? "," : "");
            }
        }
        fprintf(f, "    ]\n");
        fprintf(f, "  },\n");

        // cpu
        fprintf(f, "  \"cpu\": {\n");
        fprintf(f, "    \"total_ms\": %.3f,\n", m_data.cpuScopeTotal);
        fprintf(f, "    \"scopes\": [\n");
        for (size_t i = 0; i < m_data.cpuEntries.size(); ++i)
        {
            const auto &e = m_data.cpuEntries[i];
            auto it = m_cpuStats.find(e.name);
            if (it != m_cpuStats.end() && !it->second.samples.empty())
            {
                const auto &st = it->second;
                fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"min_ms\": %.3f, \"cur_ms\": %.3f, \"max_ms\": %.3f, \"avg_ms\": %.3f}%s\n",
                        e.name, e.depth,
                        st.minMs, e.timeMs, st.maxMs, st.avgMs,
                        (i + 1 < m_data.cpuEntries.size()) ? "," : "");
            }
            else
            {
                fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"cur_ms\": %.3f}%s\n",
                        e.name, e.depth, e.timeMs,
                        (i + 1 < m_data.cpuEntries.size()) ? "," : "");
            }
        }
        fprintf(f, "    ]\n");
        fprintf(f, "  }\n");

        fprintf(f, "}\n");
        fclose(f);

        Log::Info("Profiler snapshot saved: " + path);
        return path;
    }

    // ─── Overview Tab ────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawOverviewTab()
    {
        // Frame time graph
        {
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.2f ms", m_data.frameMs);
            ImGui::PlotLines("##ft", m_data.frameTimeVec.data(),
                             static_cast<int>(m_data.frameTimeVec.size()), 0,
                             overlay, 0.f, m_data.frameTimeMax, {-1, 60});
        }

        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            {ImGui::GetStyle().ItemSpacing.x, 2.f});
        ui::ShowCpuGpuSummary(m_data.cpuTotalMs, m_data.cpuUpdateMs, m_data.cpuDrawMs, m_data.gpuTotal);
        ImGui::PopStyleVar();

        if (ImGui::CollapsingHeader("Memory"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                {ImGui::GetStyle().ItemSpacing.x, 2.f});
            ui::DrawRamBar("RAM", m_data.ram.sysTotal, m_data.ram.procPrivateBytes, m_data.ram.sysUsed);
            ui::DrawGpuBar("GPU (Local)", m_data.gpu.vram.app, m_data.gpu.vram.other,
                           std::max<uint64_t>(m_data.gpu.vram.budget, 1));
            ui::DrawGpuBar("GPU (Host Visible)", m_data.gpu.host.app, m_data.gpu.host.other,
                           std::max<uint64_t>(m_data.gpu.host.budget, 1));
            ImGui::PopStyleVar();
        }
    }

    // ─── Timeline Tab ────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawTimelineTab()
    {
        if (m_data.gpuSamples.empty())
        {
            ImGui::TextDisabled("No GPU timer data. At least one frame must complete.");
            return;
        }

        // Collect pass entries (depth >= 1, positive time).
        struct PassEntry
        {
            const GpuTimerSample *sample;
            int idx;
        };
        std::vector<PassEntry> passes;
        for (int i = 0; i < (int)m_data.gpuSamples.size(); ++i)
        {
            const auto &s = m_data.gpuSamples[i];
            if (s.depth >= 1 && s.timeMs > 0.f)
                passes.push_back({&s, i});
        }

        if (passes.empty())
        {
            ImGui::TextDisabled("No per-pass timing data available.");
            return;
        }

        // Determine total time span for X axis.
        float spanMs = 0.f;
        for (const auto &entry : passes)
            spanMs = std::max(spanMs, entry.sample->startOffsetMs + entry.sample->timeMs);
        if (spanMs <= 0.f)
            spanMs = m_data.gpuTotal > 0.f ? m_data.gpuTotal : 16.67f;
        spanMs *= 1.05f; // a bit of right-side padding

        // ── Toolbar ──────────────────────────────────────────────────────────────
        ImGui::TextDisabled("H:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##zoom", &m_timelineZoom, 0.2f, 32.f, "%.1fx");
        ImGui::SameLine();
        ImGui::TextDisabled("V:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##vzoom", &m_timelineZoomV, 0.2f, 4.f, "%.1fx");

        // ── Layout ───────────────────────────────────────────────────────────────
        const float rowH = 22.f * m_timelineZoomV;
        const float rowGap = 2.f;
        const float indentW = 14.f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const float chartH = static_cast<float>(passes.size()) * (rowH + rowGap) + 4.f;
        const float childH = std::min(chartH + 20.f, ImGui::GetContentRegionAvail().y - 60.f);

        ImGui::BeginChild("##timeline_scroll", {-1, std::max(childH, 60.f)}, false,
                          ImGuiWindowFlags_HorizontalScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

        // ── Scroll-wheel zoom centered on mouse ───────────────────────────────
        if (ImGui::IsWindowHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
            {
                const float mouseScreenX = ImGui::GetIO().MousePos.x;
                const float mouseLocalX = mouseScreenX - ImGui::GetWindowPos().x;
                const float scrollX = ImGui::GetScrollX();
                const float mouseContent = mouseLocalX + scrollX; // content-space x

                const float oldZoom = m_timelineZoom;
                m_timelineZoom = std::clamp(m_timelineZoom * (1.f + wheel * 0.15f), 0.2f, 32.f);

                // Keep the time point under the mouse fixed
                const float newScrollX = mouseContent * (m_timelineZoom / oldZoom) - mouseLocalX;
                ImGui::SetScrollX(std::max(0.f, newScrollX));
            }
        }

        const float chartW = availW * m_timelineZoom;

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(origin, {origin.x + chartW, origin.y + chartH},
                          IM_COL32(18, 18, 22, 255));

        // Tick lines every ~5ms
        float tickInterval = 5.f;
        for (float t = 0.f; t < spanMs; t += tickInterval)
        {
            float x = origin.x + (t / spanMs) * chartW;
            dl->AddLine({x, origin.y}, {x, origin.y + chartH}, IM_COL32(60, 60, 70, 180), 1.f);
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%.0fms", t);
            dl->AddText({x + 2.f, origin.y + 2.f}, IM_COL32(120, 120, 130, 200), lbl);
        }

        // Pass bars
        for (int row = 0; row < (int)passes.size(); ++row)
        {
            const PassEntry &entry = passes[row];
            const GpuTimerSample &s = *entry.sample;
            int depth = static_cast<int>(s.depth) - 1;

            float y0 = origin.y + row * (rowH + rowGap);
            float y1 = y0 + rowH;

            float xStart = origin.x + (s.startOffsetMs / spanMs) * chartW + depth * indentW;
            float xEnd = origin.x + ((s.startOffsetMs + s.timeMs) / spanMs) * chartW;
            if (xEnd < xStart + 2.f)
                xEnd = xStart + 2.f;

            float rel = m_data.gpuTotal > 0.f ? s.timeMs / m_data.gpuTotal : 0.f;
            ImVec4 colorV = ui::Heat(rel);
            ImU32 fillColor = IM_COL32(
                (int)(colorV.x * 200), (int)(colorV.y * 200), (int)(colorV.z * 200), 210);
            ImU32 borderColor = IM_COL32(255, 255, 255, 60);
            if (m_selectedGpuPass == entry.idx)
                borderColor = IM_COL32(255, 220, 80, 255);

            dl->AddRectFilled({xStart, y0 + 1.f}, {xEnd, y1 - 1.f}, fillColor, 3.f);
            dl->AddRect({xStart, y0 + 1.f}, {xEnd, y1 - 1.f}, borderColor, 3.f);

            float barW = xEnd - xStart;
            if (barW > 20.f)
            {
                ImVec2 textPos = {xStart + 4.f, y0 + (rowH - ImGui::GetTextLineHeight()) * 0.5f};
                dl->PushClipRect({xStart, y0}, {xEnd, y1}, true);
                dl->AddText(textPos, IM_COL32(255, 255, 255, 230), s.name.c_str());
                dl->PopClipRect();
            }

            ImGui::SetCursorScreenPos({xStart, y0});
            ImGui::InvisibleButton(("##bar" + std::to_string(entry.idx)).c_str(),
                                   {std::max(xEnd - xStart, 4.f), rowH});
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", s.name.c_str());
                ImGui::Text("Start: %.3f ms  Duration: %.3f ms", s.startOffsetMs, s.timeMs);
                ImGui::Text("GPU share: %.1f%%", rel * 100.f);
                DrawPassImageTooltip(s.name);
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked())
                m_selectedGpuPass = (m_selectedGpuPass == entry.idx) ? -1 : entry.idx;
        }

        // Extend scroll region to full chart size (content-local coords, scroll-independent)
        ImGui::SetCursorPos({chartW, chartH});
        ImGui::Dummy({1.f, 1.f});

        ImGui::EndChild();

        // Selected pass detail
        if (m_selectedGpuPass >= 0 && m_selectedGpuPass < (int)m_data.gpuSamples.size())
        {
            const auto &sel = m_data.gpuSamples[m_selectedGpuPass];
            ImGui::Separator();
            ImGui::Text("Selected: %s", sel.name.c_str());
            ImGui::Text("Start offset: %.3f ms   Duration: %.3f ms   GPU share: %.1f%%",
                        sel.startOffsetMs, sel.timeMs,
                        m_data.gpuTotal > 0.f ? sel.timeMs / m_data.gpuTotal * 100.f : 0.f);
            ImGui::TextDisabled("To inspect draw calls, use Capture tab (RenderDoc).");
        }
    }

    // ─── GPU Tab ─────────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawGpuTab()
    {
        // View selector + stats + filter (table mode only)
        static const char *viewNames[] = {"Table", "Timeline"};
        ImGui::SetNextItemWidth(110.f);
        ImGui::Combo("##gpuview", &m_gpuViewMode, viewNames, 2);
        ImGui::SameLine();
        ImGui::TextDisabled("Total GPU: %.3f ms", m_data.gpuTotal);
        if (m_gpuViewMode == 0)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##gpuf", "filter...", m_gpuFilter, IM_ARRAYSIZE(m_gpuFilter));
        }

        ImGui::Separator();

        if (m_gpuViewMode == 0)
        {
            float totalMs = m_data.gpuTotal > 0.f ? m_data.gpuTotal : 1.f;
            DrawGpuTimingTable(m_data.gpuSamples, totalMs, m_gpuFilter, m_selectedGpuPass);
        }
        else
        {
            DrawTimelineTab();
        }
    }

    // ─── CPU timing table with stats ────────────────────────────────────────────

    void ProfilerWidget::DrawCpuTimingTableWithStats(const std::vector<Profiler::Entry> &entries,
                                                     float scopeTotal, const char *filter)
    {
        if (entries.empty())
        {
            ImGui::TextDisabled("No CPU scopes. Add PE_PROFILE_SCOPE() to instrument code.");
            return;
        }
        if (scopeTotal <= 0.f)
            scopeTotal = 1.f;

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY;

        const bool filtering = filter[0] != '\0';

        if (ImGui::BeginTable("##cpu_stats", 6, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
        {
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("cur", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 52.f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableHeadersRow();

            int skipDepth = INT_MAX;
            int openTree = 0;

            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto &e = entries[i];
                int depth = static_cast<int>(e.depth);
                float rel = e.timeMs / scopeTotal;

                auto statsIt = m_cpuStats.find(e.name);
                const ScopeStats *st = (statsIt != m_cpuStats.end()) ? &statsIt->second : nullptr;

                if (filtering)
                {
                    std::string_view hay(e.name), needle(filter);
                    bool found = std::search(hay.begin(), hay.end(),
                                             needle.begin(), needle.end(),
                                             [](unsigned char a, unsigned char b)
                                             { return std::tolower(a) == std::tolower(b); }) != hay.end();
                    if (!found)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TreeNodeEx(e.name, ImGuiTreeNodeFlags_Leaf |
                                                  ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                  ImGuiTreeNodeFlags_SpanFullWidth);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->minMs : 0.f);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(ui::Heat(rel), "%.3f", e.timeMs);
                    ImGui::TableSetColumnIndex(3);
                    if (st)
                        ImGui::TextColored(ui::Heat(st->maxMs / scopeTotal), "%.3f", st->maxMs);
                    else
                        ImGui::TextDisabled("--");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->avgMs : 0.f);
                    ImGui::TableSetColumnIndex(5);
                    CenteredTinyBar(rel);
                    ImGui::PopID();
                    continue;
                }

                if (depth > skipDepth)
                    continue;
                skipDepth = INT_MAX;

                while (openTree > depth)
                {
                    ImGui::TreePop();
                    openTree--;
                }

                bool hasChildren = (i + 1 < entries.size() && entries[i + 1].depth > e.depth);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
                if (hasChildren)
                    nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                else
                    nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                bool open = ImGui::TreeNodeEx(e.name, nodeFlags);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->minMs : 0.f);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ui::Heat(rel), "%.3f", e.timeMs);
                ImGui::TableSetColumnIndex(3);
                if (st)
                    ImGui::TextColored(ui::Heat(st->maxMs / scopeTotal), "%.3f", st->maxMs);
                else
                    ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled(st ? "%.3f" : "--", st ? st->avgMs : 0.f);
                ImGui::TableSetColumnIndex(5);
                CenteredTinyBar(rel);
                ImGui::PopID();

                if (hasChildren)
                {
                    if (open)
                        openTree++;
                    else
                        skipDepth = depth;
                }
            }
            while (openTree > 0)
            {
                ImGui::TreePop();
                openTree--;
            }
            ImGui::EndTable();
        }
    }

    // ─── CPU Tab ─────────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawCpuTab()
    {
        // View selector + stats + filter (table mode only)
        static const char *viewNames[] = {"Table", "Timeline"};
        ImGui::SetNextItemWidth(110.f);
        ImGui::Combo("##cpuview", &m_cpuViewMode, viewNames, 2);
        ImGui::SameLine();
        ImGui::TextDisabled("Total CPU: %.3f ms", m_data.frameMs);
        if (m_cpuViewMode == 0)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##cpuf", "filter...", m_cpuFilter, IM_ARRAYSIZE(m_cpuFilter));
        }

        ImGui::Separator();

        if (m_cpuViewMode == 0)
        {
            ImGui::TextDisabled("Scopes: %.3f ms  (update: %.3f ms  draw: %.3f ms)",
                                m_data.frameMs, m_data.cpuUpdateMs, m_data.cpuDrawMs);
            DrawCpuTimingTableWithStats(m_data.cpuEntries, m_data.cpuScopeTotal, m_cpuFilter);
        }
        else
        {
            DrawCpuTimelineView();
        }
    }

    void ProfilerWidget::DrawCpuTimelineView()
    {
        if (m_data.cpuEntries.empty())
        {
            ImGui::TextDisabled("No CPU scope data. Add PE_PROFILE_SCOPE() markers.");
            return;
        }

        // Compute start offsets from DFS order (Profiler::Entry has no startOffsetMs)
        struct CpuPassEntry
        {
            const Profiler::Entry *entry;
            int idx;
            float startMs;
        };
        std::vector<CpuPassEntry> passes;
        passes.reserve(m_data.cpuEntries.size());
        {
            constexpr int kMaxDepth = 32;
            float cursor[kMaxDepth] = {};
            float parentStart[kMaxDepth] = {};
            int prevDepth = 0;
            for (int i = 0; i < (int)m_data.cpuEntries.size(); ++i)
            {
                const auto &e = m_data.cpuEntries[i];
                int d = std::min((int)e.depth, kMaxDepth - 1);
                if (i > 0 && d > prevDepth)
                    cursor[d] = parentStart[prevDepth];
                float start = cursor[d];
                parentStart[d] = start;
                cursor[d] += e.timeMs;
                if (e.timeMs > 0.f)
                    passes.push_back({&e, i, start});
                prevDepth = d;
            }
        }

        if (passes.empty())
        {
            ImGui::TextDisabled("No positive-time CPU scopes found.");
            return;
        }

        float spanMs = m_data.cpuScopeTotal > 0.f ? m_data.cpuScopeTotal : m_data.frameMs;
        if (spanMs <= 0.f)
            spanMs = 16.67f;
        spanMs *= 1.05f;

        // Zoom sliders
        ImGui::TextDisabled("H:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##cpuzoom", &m_cpuTimelineZoom, 0.2f, 32.f, "%.1fx");
        ImGui::SameLine();
        ImGui::TextDisabled("V:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##cpuvzoom", &m_cpuTimelineZoomV, 0.2f, 4.f, "%.1fx");

        const float rowH = 22.f * m_cpuTimelineZoomV;
        const float rowGap = 2.f;
        const float indentW = 14.f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const float chartH = static_cast<float>(passes.size()) * (rowH + rowGap) + 4.f;
        const float childH = std::min(chartH + 20.f, ImGui::GetContentRegionAvail().y - 60.f);

        ImGui::BeginChild("##cpu_timeline_scroll", {-1, std::max(childH, 60.f)}, false,
                          ImGuiWindowFlags_HorizontalScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

        // Scroll-wheel zoom centered on mouse
        if (ImGui::IsWindowHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
            {
                const float mouseLocalX = ImGui::GetIO().MousePos.x - ImGui::GetWindowPos().x;
                const float scrollX = ImGui::GetScrollX();
                const float mouseContent = mouseLocalX + scrollX;
                const float oldZoom = m_cpuTimelineZoom;
                m_cpuTimelineZoom = std::clamp(m_cpuTimelineZoom * (1.f + wheel * 0.15f), 0.2f, 32.f);
                ImGui::SetScrollX(std::max(0.f, mouseContent * (m_cpuTimelineZoom / oldZoom) - mouseLocalX));
            }
        }

        const float chartW = availW * m_cpuTimelineZoom;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(origin, {origin.x + chartW, origin.y + chartH}, IM_COL32(18, 18, 22, 255));

        // Tick lines
        float tickInterval = 1.f;
        if (spanMs > 50.f)
            tickInterval = 5.f;
        if (spanMs > 200.f)
            tickInterval = 10.f;
        for (float t = 0.f; t < spanMs; t += tickInterval)
        {
            float x = origin.x + (t / spanMs) * chartW;
            dl->AddLine({x, origin.y}, {x, origin.y + chartH}, IM_COL32(60, 60, 70, 180), 1.f);
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%.0fms", t);
            dl->AddText({x + 2.f, origin.y + 2.f}, IM_COL32(120, 120, 130, 200), lbl);
        }

        // Scope bars
        for (int row = 0; row < (int)passes.size(); ++row)
        {
            const CpuPassEntry &pe = passes[row];
            int depth = std::min((int)pe.entry->depth, 8);

            float y0 = origin.y + row * (rowH + rowGap);
            float y1 = y0 + rowH;

            float xStart = origin.x + (pe.startMs / spanMs) * chartW + depth * indentW;
            float xEnd = origin.x + ((pe.startMs + pe.entry->timeMs) / spanMs) * chartW;
            if (xEnd < xStart + 2.f)
                xEnd = xStart + 2.f;

            float rel = m_data.cpuScopeTotal > 0.f ? pe.entry->timeMs / m_data.cpuScopeTotal : 0.f;
            ImVec4 colorV = ui::Heat(rel);
            ImU32 fillColor = IM_COL32(
                (int)(colorV.x * 200), (int)(colorV.y * 200), (int)(colorV.z * 200), 210);
            ImU32 borderColor = (m_selectedCpuScope == pe.idx)
                                    ? IM_COL32(255, 220, 80, 255)
                                    : IM_COL32(255, 255, 255, 60);

            dl->AddRectFilled({xStart, y0 + 1.f}, {xEnd, y1 - 1.f}, fillColor, 3.f);
            dl->AddRect({xStart, y0 + 1.f}, {xEnd, y1 - 1.f}, borderColor, 3.f);

            if (xEnd - xStart > 20.f)
            {
                ImVec2 textPos = {xStart + 4.f, y0 + (rowH - ImGui::GetTextLineHeight()) * 0.5f};
                dl->PushClipRect({xStart, y0}, {xEnd, y1}, true);
                dl->AddText(textPos, IM_COL32(255, 255, 255, 230), pe.entry->name);
                dl->PopClipRect();
            }

            ImGui::SetCursorScreenPos({xStart, y0});
            ImGui::InvisibleButton(("##cbar" + std::to_string(pe.idx)).c_str(),
                                   {std::max(xEnd - xStart, 4.f), rowH});
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", pe.entry->name);
                ImGui::Text("Start: %.3f ms  Duration: %.3f ms", pe.startMs, pe.entry->timeMs);
                ImGui::Text("CPU share: %.1f%%", rel * 100.f);
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked())
                m_selectedCpuScope = (m_selectedCpuScope == pe.idx) ? -1 : pe.idx;
        }

        ImGui::SetCursorPos({chartW, chartH});
        ImGui::Dummy({1.f, 1.f});
        ImGui::EndChild();

        // Selected scope detail
        if (m_selectedCpuScope >= 0 && m_selectedCpuScope < (int)m_data.cpuEntries.size())
        {
            const auto &sel = m_data.cpuEntries[m_selectedCpuScope];
            ImGui::Separator();
            ImGui::Text("Selected: %s", sel.name);
            ImGui::Text("Duration: %.3f ms   CPU share: %.1f%%",
                        sel.timeMs, m_data.cpuScopeTotal > 0.f ? sel.timeMs / m_data.cpuScopeTotal * 100.f : 0.f);
        }
    }

    // ─── Capture Tab ─────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawCaptureTab()
    {
        ImGui::TextWrapped(
            "Capture one or more consecutive GPU frames using RenderDoc.\n"
            "RenderDoc must be installed for this to work.\n"
            "After capture the RenderDoc UI will open automatically.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

#if PE_DEBUG_MODE
        const bool rdocAvailable = Debug::IsCaptureApiAvailable();
        if (!rdocAvailable)
            ImGui::TextColored({1.f, 0.6f, 0.1f, 1.f}, "RenderDoc not detected — install RenderDoc and relaunch.");

        ImGui::BeginDisabled(!rdocAvailable);

        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("Frames##captureN", &m_captureFrameCount);
        m_captureFrameCount = std::max(1, m_captureFrameCount);
        ImGui::SameLine();
        if (ImGui::Button("Capture", {-1, 0.f}))
        {
            m_captureCountBefore = Debug::GetNumCaptures();
            m_open = false;
            m_waitingForCapture = true;
            Debug::TriggerMultiFrameCapture(static_cast<uint32_t>(m_captureFrameCount));
        }

        ImGui::EndDisabled();
#else
        ImGui::TextDisabled("GPU captures require a debug build (PE_DEBUG_MODE).");
#endif

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Capture without GUI:");
        ImGui::TextWrapped("Press Ctrl+G to hide the editor UI, then press Ctrl+T to trigger "
                           "a capture of a clean frame with no overlay draw calls.");
        ImGui::Spacing();
        ImGui::TextDisabled("GPU profiling data (pass names, timings) is visible");
        ImGui::TextDisabled("in the Timeline and GPU tabs above.");
    }

} // namespace pe
