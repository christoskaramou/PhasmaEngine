#include "Base/ProfilerSnapshot.h"
#include "API/Debug.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Swapchain.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

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

    std::string ProfilerSnapshot::CaptureMetadata(const std::filesystem::path &scenePath, Image *viewport)
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        static const std::string session = std::to_string(now.count());
        const auto scene = scenePath.generic_u8string();
        const SceneSettings &settings = Settings::Get<SceneSettings>();
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("schema_version");
        writer.Int(1);
        writer.Key("capture_session");
        writer.String(session.c_str());
        writer.Key("capture_unix_ms");
        writer.Int64(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        writer.Key("context");
        writer.StartObject();
        writer.Key("scene_path");
        writer.String(reinterpret_cast<const char *>(scene.data()), static_cast<rapidjson::SizeType>(scene.size()));
        writer.Key("graphics_api");
        writer.String(PeGraphicsApiName(RHII.GetApi()));
        writer.Key("gpu_name");
        writer.String(RHII.GetGpuName().c_str());
        writer.Key("platform");
        writer.String(SDL_GetPlatform());
        writer.Key("cpu_logical_cores");
        writer.Int(SDL_GetCPUCount());
        writer.Key("build_configuration");
#if defined(PE_RELEASE)
        writer.String("Release");
#elif defined(PE_RELWITHDEBINFO)
        writer.String("RelWithDebInfo");
#else
        writer.String("Debug/Other");
#endif
        writer.Key("present_mode");
        writer.String(RHII.GetSwapchain() ? RHII.PresentModeToString(RHII.GetSwapchain()->GetPresentMode()) : "Unknown");
        writer.Key("display_width");
        writer.Uint(RHII.GetWidth());
        writer.Key("display_height");
        writer.Uint(RHII.GetHeight());
        writer.Key("render_width");
        writer.Uint(viewport ? viewport->GetWidth() : 0);
        writer.Key("render_height");
        writer.Uint(viewport ? viewport->GetHeight() : 0);
        writer.Key("settings");
        writer.StartObject();
        writer.Key("render_scale");
        writer.Double(settings.render_scale);
        writer.Key("shadows");
        writer.Bool(settings.shadows && SceneSettingsActive());
        writer.Key("shadow_lod_bias");
        writer.Double(settings.shadow_lod_bias);
        writer.Key("shadow_map_size");
        writer.Uint(settings.shadow_map_size);
        writer.Key("num_cascades");
        writer.Uint(settings.num_cascades);
        writer.Key("shadow_distance");
        writer.Double(settings.shadow_distance);
        writer.Key("skinned_instancing");
        writer.Bool(settings.skinned_instancing);
        writer.Key("lod_enabled");
        writer.Bool(settings.lod_enabled);
        writer.Key("lod_bias");
        writer.Double(settings.lod_bias);
        writer.Key("frustum_culling");
        writer.Bool(settings.frustum_culling);
        writer.Key("occlusion_culling");
        writer.Bool(settings.occlusion_culling);
        writer.Key("forward_plus");
        writer.Bool(settings.forward_plus);
        writer.EndObject();
        writer.EndObject();
        writer.EndObject();
        return {buffer.GetString(), buffer.GetSize()};
    }

    ProfilerSnapshot ProfilerSnapshot::Gather(std::vector<GpuTimerSample> gpuSamples,
                                              const std::filesystem::path &scenePath, Image *viewport)
    {
        ProfilerSnapshot d;
        d.metadataJson = CaptureMetadata(scenePath, viewport);
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
        out += metadataJson;
        out.pop_back();
        if (out.size() > 1)
            out += ',';

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
