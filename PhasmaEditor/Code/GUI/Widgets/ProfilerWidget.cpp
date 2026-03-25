#include "ProfilerWidget.h"
#include "API/Debug.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Base/EventSystem.h"
#include "Base/Path.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"

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

    // ─── GPU timing table (shared between Overview and GPU tab) ─────────────────

    static void DrawGpuTimingTable(const std::vector<GpuTimerSample> &samples,
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
        if (ImGui::BeginTable("##gpu_timing", 3, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.65f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
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

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                if (ImGui::Selectable(("##sel_" + std::to_string(i)).c_str(),
                                      selected,
                                      ImGuiSelectableFlags_SpanAllColumns,
                                      {0, rowH}))
                    selectedPass = selected ? -1 : i;

                ImGui::SameLine();
                ui::DrawHierarchyCell(s.name.c_str(), static_cast<int>(s.depth) - 1, rel);
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ui::Heat(rel), "%.3f", s.timeMs);

                ImGui::TableSetColumnIndex(2);
                CenteredTinyBar(rel);
            }
            ImGui::EndTable();
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
                    if (e.depth == 0)
                        d.cpuScopeTotal += e.timeMs;

                if (!latestGpuData.empty())
                {
                    d.gpuSamples = std::move(latestGpuData);
                    for (const auto &s : d.gpuSamples)
                        if (s.depth == 0)
                            d.gpuTotal += s.timeMs;
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

        // Header line: stats + Snapshot right-aligned
        ImGui::Text("%.1f FPS  |  CPU %.3f ms  |  GPU %.3f ms",
                    m_data.fps, m_data.frameMs, m_data.gpuTotal);
        ImGui::SameLine();
        {
            const float btnW = ImGui::CalcTextSize("Snapshot").x + ImGui::GetStyle().FramePadding.x * 2.f;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > btnW)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - btnW);
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
            if (ImGui::BeginTabItem("Shaders"))
            {
                DrawShadersTab();
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
            fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"time_ms\": %.3f, \"start_offset_ms\": %.3f}%s\n",
                    s.name.c_str(), (unsigned)s.depth, s.timeMs, s.startOffsetMs,
                    (i + 1 < m_data.gpuSamples.size()) ? "," : "");
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
            fprintf(f, "      {\"name\": \"%s\", \"depth\": %u, \"time_ms\": %.3f}%s\n",
                    e.name, e.depth, e.timeMs,
                    (i + 1 < m_data.cpuEntries.size()) ? "," : "");
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
            DrawCpuTimingTable(m_data.cpuEntries, m_data.cpuScopeTotal, m_cpuFilter);
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

    // ─── Shaders Tab ─────────────────────────────────────────────────────────────

    void ProfilerWidget::ScanShaderFiles()
    {
        m_shaderFiles.clear();
        m_shaderRelPaths.clear();

        const std::string shaderDir = Path::Assets + "Shaders/";
        if (!std::filesystem::exists(shaderDir))
            return;

        for (auto &entry : std::filesystem::recursive_directory_iterator(shaderDir))
        {
            if (!entry.is_regular_file())
                continue;
            const auto &p = entry.path();
            if (p.extension() != ".hlsl" && p.extension() != ".glsl" && p.extension() != ".h")
                continue;

            m_shaderFiles.push_back(p.string());
            // Make a relative display path
            std::string rel = p.string();
            if (rel.size() > shaderDir.size())
                rel = rel.substr(shaderDir.size());
            std::replace(rel.begin(), rel.end(), '\\', '/');
            m_shaderRelPaths.push_back(rel);
        }
        m_shaderFilesScanned = true;
    }

    void ProfilerWidget::LoadShaderFile(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            return;

        std::string source((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
        m_shaderOriginalSource = source;
        m_shaderModified = false;

        auto ext = std::filesystem::path(path).extension().string();
        if (ext == ".glsl")
            m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
        else
            m_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());

        m_editor.SetText(source);
    }

    void ProfilerWidget::SaveAndRecompile()
    {
        if (m_selectedShader < 0 || m_selectedShader >= (int)m_shaderFiles.size())
            return;

        const std::string &path = m_shaderFiles[m_selectedShader];
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            PE_WARN("ProfilerWidget: cannot write shader '%s'", path.c_str());
            return;
        }
        std::string src = m_editor.GetText();
        f << src;
        f.close();

        m_shaderOriginalSource = src;
        m_shaderModified = false;

        EventSystem::PushEvent(EventType::CompileShaders);
    }

    void ProfilerWidget::DrawShadersTab()
    {
        // Catppuccin Mocha — IM_COL32 format is 0xAABBGGRR
        static const TextEditor::Palette soft = {{
            0xFFF4D6CD, // Default          lavender
            0xFFF7A6CB, // Keyword          mauve
            0xFF87B3FA, // Number           peach
            0xFFA1E3A6, // String           green
            0xFFEBDC89, // CharLiteral      sky
            0xFFDEC2BA, // Punctuation      subtext
            0xFFEBDC89, // Preprocessor     sky
            0xFFF4D6CD, // Identifier       lavender
            0xFFAFE2F9, // KnownIdentifier  yellow
            0xFFFAB489, // PreprocIdentifier blue
            0xFF86706C, // Comment          overlay0
            0xFF705B58, // MultiLineComment  surface2
            0xFF2E1E1E, // Background       base
            0xFFE7C2F5, // Cursor           pink
            0x40443231, // Selection        surface0
            0x80A88BF3, // ErrorMarker      red
            0xFF87B3FA, // Breakpoint       peach
            0xFF86706C, // LineNumber       overlay0
            0x40251818, // CurrentLineFill
            0x20251818, // CurrentLineFillInactive
            0x605A4745, // CurrentLineEdge
        }};

        // VS Code
        static const TextEditor::Palette vscode = {{
            0xFFded9d6, // Default          #d6d9de
            0xFFcc9c56, // Keyword          #569ccc
            0xFFa7cdb4, // Number           #b4cda7
            0xFF5485ce, // String           #ce8554
            0xFF5485ce, // CharLiteral      #ce8554
            0xFFb1d4c9, // Punctuation      #c9d4b1
            0xFFb686c5, // Preprocessor     #c586b6
            0xFFfedc9c, // Identifier       #9cdcfe
            0xFFaadcdc, // KnownIdentifier  #dcdcaa
            0xFFcc6e2a, // PreprocIdentifier #2a6ecc
            0xFF55996a, // Comment          #6a9955
            0xFF55996a, // MultiLineComment  #6a9955
            0xFF1f1f1f, // Background       #1f1f1f
            0xFFadafae, // Cursor           #aeafad
            0x40784f26, // Selection        #264f78
            0x804745c7, // ErrorMarker      #c74547
            0xFF0014e5, // Breakpoint       #e51400
            0xFF747664, // LineNumber       #647674
            0x401f1f1f, // CurrentLineFill  #1f1f1f
            0x201f1f1f, // CurrentLineFillInactive #1f1f1f
            0x601f1f1f, // CurrentLineEdge  #1f1f1f
        }};

        if (!m_shaderFilesScanned)
        {
            ScanShaderFiles();
            // Apply initial palette (VSCode by default)
            static bool paletteInit = false;
            if (!paletteInit)
            {
                paletteInit = true;
                m_editor.SetPalette(vscode);
                m_editor.SetShowWhitespaces(false);
            }
        }

        // Recompile button
        if (m_shaderModified)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
            if (ImGui::Button("Save & Recompile", {-1, 0}))
                SaveAndRecompile();
            ImGui::PopStyleColor();
        }
        else
        {
            if (ImGui::Button("Recompile All Shaders", {-1, 0}))
                EventSystem::PushEvent(EventType::CompileShaders);
        }

        // Toolbar: search + palette + font-size + rescan
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 270.f);
        ImGui::InputTextWithHint("##shsearch", "filter shaders...", m_shaderSearchFilter, IM_ARRAYSIZE(m_shaderSearchFilter));

        ImGui::SameLine();
        static const char *paletteNames[] = {"Dark", "Light", "Retro", "Soft", "VSCode"};
        ImGui::SetNextItemWidth(100.f);
        if (ImGui::Combo("##palette", &m_editorPalette, paletteNames, 5))
        {
            if (m_editorPalette == 0)
                m_editor.SetPalette(TextEditor::GetDarkPalette());
            else if (m_editorPalette == 1)
                m_editor.SetPalette(TextEditor::GetLightPalette());
            else if (m_editorPalette == 2)
                m_editor.SetPalette(TextEditor::GetRetroBluePalette());
            else if (m_editorPalette == 3)
                m_editor.SetPalette(soft);
            else
                m_editor.SetPalette(vscode);
        }

        ImGui::SameLine();
        static const char *sizeNames[] = {"S", "M", "L", "XL"};
        static const float sizeScales[] = {0.85f, 1.0f, 1.25f, 1.5f};
        ImGui::SetNextItemWidth(55.f);
        if (ImGui::Combo("##fontsize", &m_editorFontSizeIdx, sizeNames, 4))
            m_editorFontScale = sizeScales[m_editorFontSizeIdx];

        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan"))
        {
            m_shaderFilesScanned = false;
            m_selectedShader = -1;
            m_editor.SetText("");
            m_shaderModified = false;
        }

        ImGui::Separator();

        // Split: left = file list, right = source editor
        float listW = 200.f;
        float editorW = ImGui::GetContentRegionAvail().x - listW - 8.f;
        float availH = ImGui::GetContentRegionAvail().y;

        // Left panel: shader file list
        ImGui::BeginChild("##shader_list", {listW, availH}, true);
        for (int i = 0; i < (int)m_shaderRelPaths.size(); ++i)
        {
            if (m_shaderSearchFilter[0] != '\0')
            {
                std::string_view hay(m_shaderRelPaths[i]), needle(m_shaderSearchFilter);
                bool found = std::search(hay.begin(), hay.end(),
                                         needle.begin(), needle.end(),
                                         [](unsigned char a, unsigned char b)
                                         { return std::tolower(a) == std::tolower(b); }) != hay.end();
                if (!found)
                    continue;
            }

            bool selected = (m_selectedShader == i);
            if (ImGui::Selectable(m_shaderRelPaths[i].c_str(), selected))
            {
                if (m_selectedShader != i)
                {
                    m_selectedShader = i;
                    LoadShaderFile(m_shaderFiles[i]);
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel: source editor
        ImGui::BeginChild("##shader_editor", {editorW, availH}, false);

        // Ctrl+scroll to zoom font size
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
                m_editorFontScale = std::clamp(m_editorFontScale + wheel * 0.05f, 0.5f, 2.0f);
        }
        ImGui::SetWindowFontScale(m_editorFontScale);

        if (m_selectedShader >= 0)
        {
            m_editor.Render("##src", {-1, availH});
            if (m_editor.IsTextChanged())
                m_shaderModified = (m_editor.GetText() != m_shaderOriginalSource);

            if (m_shaderModified && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveAndRecompile();
        }
        else
        {
            ImGui::TextDisabled("Select a shader file to view and edit its source.");
        }
        ImGui::EndChild();
    }

    // ─── Capture Tab ─────────────────────────────────────────────────────────────

    void ProfilerWidget::DrawCaptureTab()
    {
        ImGui::TextWrapped(
            "Trigger a GPU frame capture using RenderDoc.\n"
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

        if (ImGui::Button("Trigger RenderDoc Capture", {-1, 40.f}))
        {
            m_captureCountBefore = Debug::GetNumCaptures();
            m_open = false;
            m_waitingForCapture = true;
            Debug::TriggerCapture();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Manual capture range:");
        if (ImGui::Button("Start Frame Capture"))
            Debug::StartFrameCapture();
        ImGui::SameLine();
        if (ImGui::Button("End Frame Capture"))
            Debug::EndFrameCapture();

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
