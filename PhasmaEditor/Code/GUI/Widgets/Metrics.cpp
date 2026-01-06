#include "Metrics.h"
#include "API/RHI.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"

namespace pe
{
#if PE_DEBUG_MODE
    static float FetchTotalGPUTime(const std::vector<GpuTimerSample> &gpuTimerInfos)
    {
        float total = 0.f;
        for (auto &gpuTimerInfo : gpuTimerInfos)
        {
            // Top level timers only
            if (gpuTimerInfo.depth == 0)
                total += gpuTimerInfo.timeMs;
        }

        return total;
    }

    static void ShowGpuTimingsTable(const std::vector<GpuTimerSample> &gpuTimerInfos, float totalMs)
    {
        // Optional filter
        static char filter[64] = "";
        ImGui::SetNextItemWidth(140);
        ImGui::InputTextWithHint("##timings_filter", "filter...", filter, IM_ARRAYSIZE(filter));

        auto ContainsCaseInsensitive = [](std::string_view haystack, std::string_view needle)
        {
            if (needle.empty())
                return true;

            auto it = std::search(haystack.begin(), haystack.end(),
                                  needle.begin(), needle.end(),
                                  [](unsigned char a, unsigned char b)
                                  {
                                      return std::tolower(a) == std::tolower(b);
                                  });
            return it != haystack.end();
        };

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_ScrollY;

        // Height: show ~10 rows before scrolling
        const float row_h = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        const float table_h = row_h * 10.0f;

        if (ImGui::BeginTable("##gpu_timings", 3, flags, ImVec2(-FLT_MIN, table_h)))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.65f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableHeadersRow();

            for (const auto &info : gpuTimerInfos)
            {
                // skip your depth==0 “root” if you don’t want it listed
                if (info.depth == 0)
                    continue;

                const float t = info.timeMs;
                if (t <= 0.0f)
                    continue;

                // simple name filter
                if (filter[0] != '\0' && !ContainsCaseInsensitive(info.name, filter))
                    continue;

                const float rel = (totalMs > 0.0f) ? (t / totalMs) : 0.0f;

                ImGui::TableNextRow();

                // col 0: name (pretty hierarchy)
                ImGui::TableSetColumnIndex(0);
                ui::DrawHierarchyCell(info.name.c_str(), static_cast<int>(info.depth), rel);

                // col 1: ms (heat colored)
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ui::Heat(rel), "%.3f", t);

                // col 2: tiny relative bar
                ImGui::TableSetColumnIndex(2);
                ui::TinyBar(rel);
            }

            ImGui::EndTable();
        }
    }
#endif

    void Metrics::Update()
    {
        if (!m_open)
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
            highestValue = std::max(highestValue, framerate + 60.0f);
            std::copy(fpsDeque.begin(), fpsDeque.end(), fpsVector.begin());
        }

        ui::SetInitialWindowSizeFraction(1.0f / 5.5f, 1.0f / 3.0f);
        ImGui::Begin("Metrics", &m_open);
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Average %.3f ms (%.1f FPS)", 1000.0f / framerate, framerate);
        ImGui::PlotLines("##FrameTimes", fpsVector.data(), static_cast<int>(fpsVector.size()), 0, NULL, 0.0f, highestValue, ImVec2(0, 80));

        // Memory Usage
        ImGui::Separator();
        const auto ram = RHII.GetSystemAndProcessMemory();
        const auto gpu = RHII.GetGpuMemorySnapshot();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f)); // compact Y spacing
        // 1) RAM — system used (other) / process (Private Bytes) / system total
        ui::DrawRamBar("RAM", ram.sysTotal, ram.procPrivateBytes, ram.sysUsed);
        // 2) GPU (Local/VRAM) — used / budget
        ui::DrawGpuBar("GPU (Local)", gpu.vram.app, gpu.vram.other, std::max<uint64_t>(gpu.vram.budget, 1), ImVec2(-FLT_MIN, 18.0f));
        // 3) GPU (Host Visible) — used / budget
        ui::DrawGpuBar("GPU (Host Visible)", gpu.host.app, gpu.host.other, std::max<uint64_t>(gpu.host.budget, 1), ImVec2(-FLT_MIN, 18.0f));

        ImGui::PopStyleVar();
        ImGui::Separator();

        FrameTimer &ft = FrameTimer::Instance();
        const float cpuTotal = (float)MILLI(ft.GetCpuTotal());
        const float cpuUpdates = (float)MILLI(ft.GetUpdatesStamp());
        const float cpuDraw = (float)MILLI(ft.GetCpuTotal() - ft.GetUpdatesStamp());
#if PE_DEBUG_MODE
        auto gpuTimerInfos = m_gui->PopGpuTimerInfos();
        const float gpuTotal = FetchTotalGPUTime(gpuTimerInfos);
#else
        const float gpuTotal = 0.0f;
#endif
        // Compact local spacing
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
        ui::ShowCpuGpuSummary(cpuTotal, cpuUpdates, cpuDraw, gpuTotal);
        ImGui::PopStyleVar();
#if PE_DEBUG_MODE
        // keep the nice timings table you added below
        ShowGpuTimingsTable(gpuTimerInfos, gpuTotal);
#endif
        ImGui::End();
    }
} // namespace pe
