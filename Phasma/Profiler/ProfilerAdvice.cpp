#include "ProfilerAdvice.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace pe
{
    namespace
    {
        const rapidjson::Value &Member(const rapidjson::Value &object, const char *key)
        {
            static const rapidjson::Value missing;
            if (object.IsObject())
            {
                auto it = object.FindMember(key);
                if (it != object.MemberEnd())
                    return it->value;
            }
            return missing;
        }

        double Number(const rapidjson::Value &object, const char *key, double fallback = 0)
        {
            const auto &value = Member(object, key);
            return value.IsNumber() && std::isfinite(value.GetDouble()) ? value.GetDouble() : fallback;
        }

        std::string Json(const rapidjson::Value &value)
        {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            value.Accept(writer);
            return {buffer.GetString(), buffer.GetSize()};
        }

        double Median(std::vector<double> values)
        {
            if (values.empty())
                return 0;
            std::sort(values.begin(), values.end());
            const size_t mid = values.size() / 2;
            return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) * 0.5;
        }

        std::string Ms(double value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f ms", value);
            return text;
        }
    } // namespace

    bool ProfilerAdvice::AddSnapshot(std::string_view json)
    {
        rapidjson::Document doc;
        if (json.size() <= 16 * 1024 * 1024)
            doc.Parse(json.data(), json.size());
        if (doc.HasParseError() || !doc.IsObject())
        {
            m_samples.clear();
            m_context.clear();
            m_session.clear();
            m_status = "Invalid snapshot JSON; collect a fresh measurement window.";
            return false;
        }
        const auto &context = Member(doc, "context");
        const auto &session = Member(doc, "capture_session");
        const auto &overview = Member(doc, "overview");
        const double timestamp = Number(doc, "capture_unix_ms");
        const double frameMs = Number(overview, "frame_ms");
        if (Number(doc, "schema_version") != 1 || !context.IsObject() || !Member(context, "settings").IsObject() ||
            !session.IsString() || session.GetStringLength() == 0 || timestamp <= 0 || timestamp > 9e15 || frameMs <= 0 || frameMs > 60000)
        {
            m_samples.clear();
            m_context.clear();
            m_session.clear();
            m_status = "This capture lacks valid v0.1 context/timing metadata. Capture again with the updated engine.";
            return true;
        }

        const std::string contextJson = Json(context);
        const std::string sessionId(session.GetString(), session.GetStringLength());
        if (contextJson != m_context || sessionId != m_session ||
            (!m_samples.empty() && (timestamp < m_samples.back().timestampMs ||
                                    timestamp - m_samples.back().timestampMs > 2000)))
            m_samples.clear();
        m_context = contextJson;
        m_session = sessionId;
        // ponytail: sample at most 10 Hz and retain 40 points; longer studies use the JSONL captures.
        if (!m_samples.empty() && timestamp - m_samples.back().timestampMs < 100)
            return true;

        Sample sample;
        sample.timestampMs = timestamp;
        sample.frameMs = frameMs;
        sample.gpuMs = std::max(0.0, Number(Member(doc, "gpu"), "total_ms"));
        const auto &passes = Member(Member(doc, "gpu"), "passes");
        if (passes.IsArray())
        {
            for (const auto &pass : passes.GetArray())
            {
                const auto &name = Member(pass, "name");
                if (!name.IsString())
                    continue;
                const std::string_view label(name.GetString(), name.GetStringLength());
                const double cost = std::max(0.0, Number(pass, "cur_ms"));
                if (label == "LightOpaquePass_pass" || label == "LightTransparentPass_pass")
                    sample.lightingMs += cost;
                else if (label == "ShadowPass")
                    sample.shadowMs += cost;
            }
        }
        const auto &memory = Member(overview, "memory");
        sample.vramMb = Number(memory, "gpu_vram_app_mb");
        sample.vramBudgetMb = Number(memory, "gpu_vram_budget_mb");
        if (sample.gpuMs > 60000 || sample.lightingMs > 60000 || sample.shadowMs > 60000 ||
            sample.vramMb < 0 || sample.vramMb > 1e12 || sample.vramBudgetMb < 0 || sample.vramBudgetMb > 1e12)
        {
            m_samples.clear();
            m_status = "Invalid metric range; collect a fresh measurement window.";
            return true;
        }
        m_samples.push_back(sample);
        if (m_samples.size() > 40)
            m_samples.pop_front();
        m_status = "Collect at least eight snapshots over one second with unchanged settings.";
        return true;
    }

    std::string ProfilerAdvice::Analyze(int targetFps) const
    {
        rapidjson::Document report(rapidjson::kObjectType);
        auto &a = report.GetAllocator();
        auto text = [&a](const std::string &value)
        { return rapidjson::Value(value.data(), static_cast<rapidjson::SizeType>(value.size()), a); };
        report.AddMember("schema_version", 1, a);
        report.AddMember("advisor", "Phasma AI v0.1", a);
        report.AddMember("mode", "recommendations_only", a);
        report.AddMember("target_fps", targetFps, a);
        report.AddMember("sample_count", static_cast<unsigned>(m_samples.size()), a);
        const double duration = m_samples.empty() ? 0 : m_samples.back().timestampMs - m_samples.front().timestampMs;
        report.AddMember("window_ms", duration, a);
        report.AddMember("capture_session", text(m_session), a);
        if (!m_samples.empty())
        {
            report.AddMember("first_capture_unix_ms", m_samples.front().timestampMs, a);
            report.AddMember("last_capture_unix_ms", m_samples.back().timestampMs, a);
        }
        rapidjson::Document context;
        context.Parse(m_context.c_str());
        if (context.IsObject())
            report.AddMember("context", rapidjson::Value(context, a), a);
        rapidjson::Value recommendations(rapidjson::kArrayType);
        auto finish = [&](const std::string &status)
        {
            report.AddMember("status", text(status), a);
            report.AddMember("recommendations", recommendations, a);
            return Json(report);
        };
        if (targetFps < 1 || targetFps > 1000)
            return finish("Target FPS must be between 1 and 1000.");
        if (m_samples.size() < 8 || duration < 1000)
            return finish(m_status);

        auto median = [&](double Sample::*field)
        {
            std::vector<double> values;
            for (const Sample &sample : m_samples)
                values.push_back(sample.*field);
            return Median(std::move(values));
        };
        const double budget = 1000.0 / targetFps;
        const double frame = median(&Sample::frameMs);
        const double gpu = median(&Sample::gpuMs);
        const double lighting = median(&Sample::lightingMs);
        const double shadow = median(&Sample::shadowMs);
        const double vram = median(&Sample::vramMb);
        const double vramBudget = median(&Sample::vramBudgetMb);
        rapidjson::Value evidence(rapidjson::kObjectType);
        evidence.AddMember("budget_ms", budget, a);
        evidence.AddMember("frame_median_ms", frame, a);
        evidence.AddMember("gpu_median_ms", gpu, a);
        evidence.AddMember("lighting_median_ms", lighting, a);
        evidence.AddMember("shadow_median_ms", shadow, a);
        evidence.AddMember("vram_median_mb", vram, a);
        evidence.AddMember("vram_budget_median_mb", vramBudget, a);
        report.AddMember("evidence", evidence, a);
        auto suggest = [&](const char *id, const std::string &title, const std::string &reason,
                           const char *tradeoff, const char *validation, const char *setting = nullptr,
                           double current = 0, double proposed = 0)
        {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("id", text(id), a);
            item.AddMember("title", text(title), a);
            item.AddMember("reason", text(reason), a);
            item.AddMember("tradeoff", text(tradeoff), a);
            item.AddMember("validation", text(validation), a);
            if (setting)
            {
                rapidjson::Value change(rapidjson::kObjectType);
                change.AddMember("name", text(setting), a);
                change.AddMember("current", current, a);
                change.AddMember("proposed", proposed, a);
                item.AddMember("suggested_setting", change, a);
            }
            recommendations.PushBack(item, a);
        };
        const auto &settings = Member(context, "settings");
        const double scale = Number(settings, "render_scale");
        const double shadowBias = Number(settings, "shadow_lod_bias");
        const bool gpuValid = std::all_of(m_samples.begin(), m_samples.end(), [](const Sample &s)
                                          { return s.gpuMs > 0 && s.lightingMs + s.shadowMs <= s.gpuMs + 0.1; });
        report["evidence"].AddMember("gpu_timing_valid", gpuValid, a);
        if (gpuValid && gpu > budget * 1.05 && frame > budget)
        {
            if (lighting >= 1 && lighting / gpu >= 0.25 && scale > 0.5)
            {
                char title[128];
                std::snprintf(title, sizeof(title), "Test render scale %.2f -> %.2f", scale, std::max(0.5, scale * 0.9));
                suggest("lighting_render_scale", title,
                        "Lighting takes " + Ms(lighting) + " of " + Ms(gpu) + " measured GPU work; the target budget is " + Ms(budget) + ".",
                        "A lower render scale can reduce pixel shading cost, with a softer image and less fine detail.",
                        "Change only render_scale, repeat the same scene/camera capture, compare GPU timings and image quality. No FPS gain is predicted.",
                        "render_scale", scale, std::max(0.5, scale * 0.9));
            }
            const auto &shadows = Member(settings, "shadows");
            if (shadows.IsTrue() && shadow >= 1 && shadow / gpu >= 0.20 && shadowBias > 0 && shadowBias < 4)
            {
                char title[128];
                std::snprintf(title, sizeof(title), "Test shadow LOD bias %.2f -> %.2f", shadowBias, std::min(4.0, shadowBias * 1.5));
                suggest("shadow_lod", title,
                        "ShadowPass takes " + Ms(shadow) + " of " + Ms(gpu) + " measured GPU work above the " + Ms(budget) + " target.",
                        "Earlier shadow LOD transitions can reduce caster geometry cost, but simplify distant shadow silhouettes. Assets need lower LODs.",
                        "Change only shadow_lod_bias, repeat the same view, and compare ShadowPass time and visible shadow transitions.",
                        "shadow_lod_bias", shadowBias, std::min(4.0, shadowBias * 1.5));
            }
            if (recommendations.Empty())
                suggest("inspect_gpu", "Inspect the GPU pass breakdown",
                        "Measured GPU work is " + Ms(gpu) + " against a " + Ms(budget) + " target; no supported tuning rule has enough evidence.",
                        "Inspection does not change rendering quality.", "Find the expensive pass, then test one change against a matching baseline.");
        }
        else if (frame > budget * 1.05)
            suggest("inspect_frame", "Inspect CPU scopes and presentation waits",
                    "Frame time is " + Ms(frame) + "; " + (gpuValid ? "measured GPU work is " + Ms(gpu) : "GPU timing is missing or inconsistent") + ".",
                    "There is insufficient evidence to recommend reducing image quality. CPU total time may include waits.",
                    "Inspect update/draw scopes and wait regions; verify present mode before attributing the delay to CPU computation.");
        if (vramBudget > 0 && vram >= vramBudget * 0.9)
            suggest("vram_pressure", "Inspect resident textures and render targets",
                    "Application VRAM is " + std::to_string(static_cast<unsigned long long>(vram)) + " MB against a " +
                        std::to_string(static_cast<unsigned long long>(vramBudget)) + " MB reported budget.",
                    "Reducing texture residency or resolution can reduce detail; this measurement alone does not prove a memory stall.",
                    "Inspect resource sizes, then compare VRAM and frame-time tails after one targeted change.");
        report.AddMember("limitations", "Rules use sampled, asynchronous GPU timings and a short stable-context window. Suggestions require an A/B test; scene content and camera motion are not controlled by the advisor.", a);
        return finish(recommendations.Empty() ? "No supported optimization suggestion for this measurement window." : "Suggestions ready; no settings were changed.");
    }

    int RunProfilerAdviceCli(int argc, char *argv[])
    {
        if (argc < 3)
        {
            std::fprintf(stderr, "Usage: PhasmaProfiler --advise capture.jsonl [--target-fps 60]\n");
            return 2;
        }
        int targetFps = 60;
        for (int i = 3; i < argc; ++i)
        {
            if (std::string_view(argv[i]) != "--target-fps" || ++i >= argc)
                return 2;
            char *end = nullptr;
            const long value = std::strtol(argv[i], &end, 10);
            if (*end || value < 1 || value > 1000)
                return 2;
            targetFps = static_cast<int>(value);
        }
        const std::filesystem::path capturePath(std::u8string_view(reinterpret_cast<const char8_t *>(argv[2])));
        std::ifstream file(capturePath, std::ios::binary);
        if (!file)
        {
            std::fprintf(stderr, "Cannot open capture file.\n");
            return 2;
        }
        ProfilerAdvice advice;
        if (capturePath.extension() == ".jsonl")
        {
            std::string line;
            while (std::getline(file, line))
            {
                if (line.find_first_not_of(" \t\r") != std::string::npos && !advice.AddSnapshot(line))
                {
                    std::fprintf(stderr, "Invalid snapshot in JSONL capture.\n");
                    return 2;
                }
            }
        }
        else
        {
            const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (!advice.AddSnapshot(json))
                return 2;
        }
        if (file.bad())
            return 2;
        std::puts(advice.Analyze(targetFps).c_str());
        return 0;
    }
} // namespace pe
