#include "Base/ProfilerSnapshot.h"
#include "API/Debug.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        void AppendEscaped(std::string &out, const char *s)
        {
            if (!s)
                return;
            for (const char *p = s; *p; ++p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                    out.push_back(static_cast<char>(c));
                }
                else if (c < 0x20 || c >= 0x7f)
                {
                    // Keep stream UTF-8 JSON-safe; dangling/dynamic scope names can be garbage.
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
            }
        }

        void AppendEscaped(std::string &out, const std::string &s)
        {
            AppendEscaped(out, s.c_str());
        }
    } // namespace

    ProfilerSnapshot ProfilerSnapshot::Gather(std::vector<GpuTimerSample> gpuSamples)
    {
        ProfilerSnapshot d;
        const double dt = FrameTimer::Instance().GetDelta();
        d.fps = dt > 0.0 ? static_cast<float>(1.0 / dt) : 0.f;
        d.frameMs = Profiler::GetFrameTimeMs();

        FrameTimer &ft = FrameTimer::Instance();
        d.cpuTotalMs = static_cast<float>(MILLI(ft.GetCpuTotal()));
        d.cpuUpdateMs = static_cast<float>(MILLI(ft.GetUpdatesStamp()));
        d.cpuDrawMs = static_cast<float>(MILLI(ft.GetCpuTotal() - ft.GetUpdatesStamp()));
        d.renderDocAvailable = Debug::IsCaptureApiAvailable();
        d.renderDocCaptureCount = Debug::GetNumCaptures();

        const SystemProcMem ram = RHII.GetSystemAndProcessMemory();
        const GpuMemorySnapshot gpu = RHII.GetGpuMemorySnapshot();
        d.ramTotalMb = ram.sysTotal >> 20;
        d.ramUsedMb = ram.sysUsed >> 20;
        d.ramProcessMb = ram.procPrivateBytes >> 20;
        d.gpuVramAppMb = gpu.vram.app >> 20;
        d.gpuVramOtherMb = gpu.vram.other >> 20;
        d.gpuVramBudgetMb = gpu.vram.budget >> 20;
        d.gpuHostAppMb = gpu.host.app >> 20;
        d.gpuHostOtherMb = gpu.host.other >> 20;
        d.gpuHostBudgetMb = gpu.host.budget >> 20;

        d.cpuEntries = Profiler::GetEntries();
        for (const auto &e : d.cpuEntries)
        {
            if (e.depth == 0)
                d.cpuScopeTotalMs += e.timeMs;
        }
        d.counters = Profiler::GetCounters();

        d.gpuSamples = std::move(gpuSamples);
        for (const auto &s : d.gpuSamples)
        {
            if (s.depth == 0)
                d.gpuTotalMs += s.timeMs;
        }
        return d;
    }

    std::string ProfilerSnapshot::ToJson() const
    {
        std::string out;
        out.reserve(4096 + cpuEntries.size() * 64 + gpuSamples.size() * 80);

        char num[256];
        out += '{';

        out += "\"overview\":{";
        std::snprintf(num, sizeof(num), "\"fps\":%.1f,", fps);
        out += num;
        std::snprintf(num, sizeof(num), "\"frame_ms\":%.3f,", frameMs);
        out += num;
        std::snprintf(num, sizeof(num), "\"cpu_total_ms\":%.3f,", cpuTotalMs);
        out += num;
        std::snprintf(num, sizeof(num), "\"cpu_update_ms\":%.3f,", cpuUpdateMs);
        out += num;
        std::snprintf(num, sizeof(num), "\"cpu_draw_ms\":%.3f,", cpuDrawMs);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_total_ms\":%.3f,", gpuTotalMs);
        out += num;
        out += renderDocAvailable ? "\"renderdoc_available\":true," : "\"renderdoc_available\":false,";
        std::snprintf(num, sizeof(num), "\"renderdoc_capture_count\":%u,", renderDocCaptureCount);
        out += num;
        out += "\"memory\":{";
        std::snprintf(num, sizeof(num), "\"ram_total_mb\":%llu,", (unsigned long long)ramTotalMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"ram_used_mb\":%llu,", (unsigned long long)ramUsedMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"ram_process_mb\":%llu,", (unsigned long long)ramProcessMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_vram_app_mb\":%llu,", (unsigned long long)gpuVramAppMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_vram_other_mb\":%llu,", (unsigned long long)gpuVramOtherMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_vram_budget_mb\":%llu,", (unsigned long long)gpuVramBudgetMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_host_app_mb\":%llu,", (unsigned long long)gpuHostAppMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_host_other_mb\":%llu,", (unsigned long long)gpuHostOtherMb);
        out += num;
        std::snprintf(num, sizeof(num), "\"gpu_host_budget_mb\":%llu", (unsigned long long)gpuHostBudgetMb);
        out += num;
        out += "}},";

        out += "\"frame_history\":[";
        for (size_t i = 0; i < frameHistory.size(); ++i)
        {
            const auto &s = frameHistory[i];
            std::snprintf(num, sizeof(num),
                          "{\"frame_ms\":%.3f,\"cpu_total_ms\":%.3f,\"cpu_update_ms\":%.3f,"
                          "\"cpu_draw_ms\":%.3f,\"gpu_total_ms\":%.3f}",
                          s.frameMs, s.cpuTotalMs, s.cpuUpdateMs, s.cpuDrawMs, s.gpuTotalMs);
            out += num;
            if (i + 1 < frameHistory.size())
                out += ',';
        }
        out += "],";

        out += "\"gpu\":{";
        std::snprintf(num, sizeof(num), "\"total_ms\":%.3f,", gpuTotalMs);
        out += num;
        out += "\"passes\":[";
        for (size_t i = 0; i < gpuSamples.size(); ++i)
        {
            const auto &s = gpuSamples[i];
            out += "{\"name\":\"";
            AppendEscaped(out, s.name);
            std::snprintf(num, sizeof(num), "\",\"depth\":%u,\"cur_ms\":%.3f,\"start_offset_ms\":%.3f}",
                          (unsigned)s.depth, s.timeMs, s.startOffsetMs);
            out += num;
            if (i + 1 < gpuSamples.size())
                out += ',';
        }
        out += "]},";

        out += "\"cpu\":{";
        std::snprintf(num, sizeof(num), "\"total_ms\":%.3f,", cpuScopeTotalMs);
        out += num;
        out += "\"scopes\":[";
        for (size_t i = 0; i < cpuEntries.size(); ++i)
        {
            const auto &e = cpuEntries[i];
            out += "{\"name\":\"";
            AppendEscaped(out, e.name);
            std::snprintf(num, sizeof(num),
                          "\",\"depth\":%u,\"cur_ms\":%.3f,\"start_offset_ms\":%.3f}",
                          e.depth, e.timeMs, e.startOffsetMs);
            out += num;
            if (i + 1 < cpuEntries.size())
                out += ',';
        }
        out += "]},";

        out += "\"counters\":[";
        for (size_t i = 0; i < counters.size(); ++i)
        {
            const auto &c = counters[i];
            out += "{\"name\":\"";
            AppendEscaped(out, c.name);
            std::snprintf(num, sizeof(num), "\",\"value\":%llu}", (unsigned long long)c.value);
            out += num;
            if (i + 1 < counters.size())
                out += ',';
        }
        out += "]}";
        return out;
    }
} // namespace pe
