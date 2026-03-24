#include "ProfilerWidget.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"

namespace pe
{
    static void CenteredTinyBar(float frac, float barHeight = 12.f)
    {
        frac = ui::Clamp01(frac);

        float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        float yOffset = (rowHeight - barHeight) * 0.5f;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        p0.y += yOffset;
        ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + barHeight);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const float r = 4.0f;
        dl->AddRectFilled(p0, p1, ui::U32(ImVec4(1, 1, 1, 0.08f)), r);
        dl->AddRect(p0, p1, ui::U32(ImVec4(1, 1, 1, 0.20f)), r);

        ImVec2 f1 = ImVec2(p0.x + frac * (p1.x - p0.x), p1.y);
        dl->AddRectFilled(p0, f1, ui::U32(ui::Heat(frac)), r);

        ImGui::Dummy(ImVec2(0, rowHeight));
    }

    void ProfilerWidget::Update()
    {
        if (!m_open)
            return;

        // --- Cached display data (updated on interval) ---
        static Timer delay;
        static bool firstFrame = true;

        static float displayedFps = 0.0f;
        static float displayedFrameMs = 0.0f;
        static std::deque<float> frameTimeHistory(200, 0.0f);
        static std::vector<float> frameTimeVector(200, 0.0f);
        static float frameTimeMax = 5.0f;

        static float displayedCpuTotal = 0.0f;
        static float displayedCpuUpdates = 0.0f;
        static float displayedCpuDraw = 0.0f;
        static std::vector<Profiler::Entry> displayedCpuEntries;

#if PE_DEBUG_MODE
        static std::vector<GpuTimerSample> displayedGpuTimerInfos;
        static float displayedGpuTotal = 0.0f;
#endif

        bool updateMetrics = false;
        if (firstFrame || delay.Count() > 0.25)
        {
            delay.Start();
            firstFrame = false;
            updateMetrics = true;

            displayedFps = ImGui::GetIO().Framerate;
            displayedFrameMs = Profiler::GetFrameTimeMs();

            frameTimeHistory.pop_front();
            frameTimeHistory.push_back(displayedFrameMs);
            std::copy(frameTimeHistory.begin(), frameTimeHistory.end(), frameTimeVector.begin());

            // Auto-scale: find max in history, add headroom
            float histMax = *std::max_element(frameTimeHistory.begin(), frameTimeHistory.end());
            frameTimeMax = std::max(2.0f, histMax * 1.3f);

            FrameTimer &ft = FrameTimer::Instance();
            displayedCpuTotal = static_cast<float>(MILLI(ft.GetCpuTotal()));
            displayedCpuUpdates = static_cast<float>(MILLI(ft.GetUpdatesStamp()));
            displayedCpuDraw = static_cast<float>(MILLI(ft.GetCpuTotal() - ft.GetUpdatesStamp()));

            displayedCpuEntries.assign(Profiler::GetEntries().begin(), Profiler::GetEntries().end());
        }

#if PE_DEBUG_MODE
        auto currentGpuTimerInfos = m_gui->PopGpuTimerInfos();
        if (updateMetrics && !currentGpuTimerInfos.empty())
        {
            displayedGpuTimerInfos = std::move(currentGpuTimerInfos);
            displayedGpuTotal = 0.0f;
            for (auto &info : displayedGpuTimerInfos)
            {
                if (info.depth == 0)
                    displayedGpuTotal += info.timeMs;
            }
        }
#endif

        ui::SetInitialWindowSizeFraction(1.0f / 4.0f, 2.0f / 3.0f);
        if (!ImGui::Begin("Profiler", &m_open))
        {
            ImGui::End();
            return;
        }

        // ===== Frame Overview =====
        ImGui::Text("%.1f FPS  (%.3f ms)", displayedFps, displayedFrameMs);

        // Frame time history graph
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f ms", displayedFrameMs);
        ImGui::PlotLines("##FrameTime", frameTimeVector.data(),
                         static_cast<int>(frameTimeVector.size()), 0,
                         overlay, 0.0f, frameTimeMax, ImVec2(-1, 60));

        // ===== CPU / GPU Summary =====
        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
#if PE_DEBUG_MODE
        ui::ShowCpuGpuSummary(displayedCpuTotal, displayedCpuUpdates, displayedCpuDraw, displayedGpuTotal);
#else
        ui::ShowCpuGpuSummary(displayedCpuTotal, displayedCpuUpdates, displayedCpuDraw, 0.0f);
#endif
        ImGui::PopStyleVar();

        // ===== Memory =====
        if (ImGui::CollapsingHeader("Memory"))
        {
            const auto ram = RHII.GetSystemAndProcessMemory();
            const auto gpu = RHII.GetGpuMemorySnapshot();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
            ui::DrawRamBar("RAM", ram.sysTotal, ram.procPrivateBytes, ram.sysUsed);
            ui::DrawGpuBar("GPU (Local)", gpu.vram.app, gpu.vram.other, std::max<uint64_t>(gpu.vram.budget, 1));
            ui::DrawGpuBar("GPU (Host Visible)", gpu.host.app, gpu.host.other, std::max<uint64_t>(gpu.host.budget, 1));
            ImGui::PopStyleVar();
        }

        // ===== CPU Profiler =====
        if (ImGui::CollapsingHeader("CPU Timings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (displayedCpuEntries.empty())
            {
                ImGui::TextDisabled("No CPU scopes recorded. Add PE_PROFILE_SCOPE() to instrument code.");
            }
            else
            {
                // Compute total from top-level scopes for relative bars
                float cpuScopeTotal = 0.0f;
                for (const auto &e : displayedCpuEntries)
                {
                    if (e.depth == 0)
                        cpuScopeTotal += e.timeMs;
                }
                if (cpuScopeTotal <= 0.0f)
                    cpuScopeTotal = 1.0f;

                ImGuiTableFlags flags =
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_ScrollY;

                const float rowH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
                const float tableH = rowH * 16.0f;

                if (ImGui::BeginTable("##cpu_timings", 3, flags, ImVec2(-FLT_MIN, tableH)))
                {
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                    ImGui::TableHeadersRow();

                    int skipDepth = INT_MAX;
                    int openTreeDepth = 0;

                    for (size_t i = 0; i < displayedCpuEntries.size(); i++)
                    {
                        const auto &entry = displayedCpuEntries[i];
                        int depth = static_cast<int>(entry.depth);

                        if (depth > skipDepth)
                            continue;
                        skipDepth = INT_MAX;

                        while (openTreeDepth > depth)
                        {
                            ImGui::TreePop();
                            openTreeDepth--;
                        }

                        bool hasChildren = (i + 1 < displayedCpuEntries.size() &&
                                            displayedCpuEntries[i + 1].depth > entry.depth);

                        float rel = entry.timeMs / cpuScopeTotal;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(static_cast<int>(i));

                        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
                        if (hasChildren)
                            nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                        else
                            nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                        bool open = ImGui::TreeNodeEx(entry.name, nodeFlags);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ui::Heat(rel), "%.3f", entry.timeMs);

                        ImGui::TableSetColumnIndex(2);
                        CenteredTinyBar(rel);

                        ImGui::PopID();

                        if (hasChildren)
                        {
                            if (open)
                                openTreeDepth++;
                            else
                                skipDepth = depth;
                        }
                    }

                    while (openTreeDepth > 0)
                    {
                        ImGui::TreePop();
                        openTreeDepth--;
                    }

                    ImGui::EndTable();
                }
            }
        }

        // ===== GPU Timings =====
#if PE_DEBUG_MODE
        if (ImGui::CollapsingHeader("GPU Timings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (displayedGpuTimerInfos.empty())
            {
                ImGui::TextDisabled("No GPU timer data available.");
            }
            else
            {
                static char gpuFilter[64] = "";
                ImGui::SetNextItemWidth(140);
                ImGui::InputTextWithHint("##gpu_filter", "filter...", gpuFilter, IM_ARRAYSIZE(gpuFilter));

                float totalMs = displayedGpuTotal > 0.0f ? displayedGpuTotal : 1.0f;

                ImGuiTableFlags flags =
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_ScrollY;

                const float rowH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
                const float tableH = rowH * 10.0f;

                if (ImGui::BeginTable("##gpu_timings", 3, flags, ImVec2(-FLT_MIN, tableH)))
                {
                    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                    ImGui::TableHeadersRow();

                    for (const auto &info : displayedGpuTimerInfos)
                    {
                        if (info.depth == 0)
                            continue;
                        if (info.timeMs <= 0.0f)
                            continue;

                        // Simple case-insensitive filter
                        if (gpuFilter[0] != '\0')
                        {
                            std::string_view haystack(info.name);
                            std::string_view needle(gpuFilter);
                            bool found = std::search(haystack.begin(), haystack.end(),
                                                     needle.begin(), needle.end(),
                                                     [](unsigned char a, unsigned char b)
                                                     { return std::tolower(a) == std::tolower(b); }) != haystack.end();
                            if (!found)
                                continue;
                        }

                        float rel = info.timeMs / totalMs;

                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ui::DrawHierarchyCell(info.name.c_str(), static_cast<int>(info.depth), rel);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(ui::Heat(rel), "%.3f", info.timeMs);

                        ImGui::TableSetColumnIndex(2);
                        CenteredTinyBar(rel);
                    }

                    ImGui::EndTable();
                }
            }
        }
#endif

        ImGui::End();
    }
} // namespace pe
