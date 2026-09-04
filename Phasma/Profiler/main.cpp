// PhasmaProfiler — live viewer for ProfilerStreamServer (Player --profiler).
#include "Base/Path.h"
#include "Base/ProfilerStream.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr size_t kMaxFrameHistory = 1800;
    constexpr size_t kMaxScopeRows = 65536;
    constexpr size_t kMaxCounterRows = 4096;
    constexpr size_t kStatWindow = 40;
    constexpr size_t kMaxTraceEvents = 250000;

    constexpr ImVec4 kAccent = {0.20f, 0.68f, 0.96f, 1.0f};
    constexpr ImVec4 kCpuColor = {0.25f, 0.70f, 1.00f, 1.0f};
    constexpr ImVec4 kGpuColor = {0.74f, 0.42f, 1.00f, 1.0f};
    constexpr ImVec4 kFrameColor = {0.98f, 0.73f, 0.24f, 1.0f};
    constexpr ImVec4 kGoodColor = {0.24f, 0.82f, 0.55f, 1.0f};
    constexpr ImVec4 kWarnColor = {0.98f, 0.66f, 0.20f, 1.0f};
    constexpr ImVec4 kBadColor = {0.96f, 0.30f, 0.35f, 1.0f};

    ImU32 U32(const ImVec4 &color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    ImVec4 WithAlpha(ImVec4 color, float alpha)
    {
        color.w = alpha;
        return color;
    }

    ImVec4 Heat(float fraction)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        if (fraction < 0.65f)
        {
            const float t = fraction / 0.65f;
            return {kGoodColor.x + (kWarnColor.x - kGoodColor.x) * t,
                    kGoodColor.y + (kWarnColor.y - kGoodColor.y) * t,
                    kGoodColor.z + (kWarnColor.z - kGoodColor.z) * t,
                    1.0f};
        }
        const float t = (fraction - 0.65f) / 0.35f;
        return {kWarnColor.x + (kBadColor.x - kWarnColor.x) * t,
                kWarnColor.y + (kBadColor.y - kWarnColor.y) * t,
                kWarnColor.z + (kBadColor.z - kWarnColor.z) * t,
                1.0f};
    }

    ImVec4 ScopeColor(const std::string &name)
    {
        const float hue = static_cast<float>(std::hash<std::string>{}(name) % 997) / 997.0f;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        ImGui::ColorConvertHSVtoRGB(hue, 0.62f, 0.88f, r, g, b);
        return {r, g, b, 1.f};
    }

    bool ContainsCaseInsensitive(const std::string &text, const char *filter)
    {
        if (!filter || filter[0] == '\0')
            return true;
        const std::string_view haystack(text);
        const std::string_view needle(filter);
        return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                           [](unsigned char a, unsigned char b)
                           { return std::tolower(a) == std::tolower(b); }) != haystack.end();
    }

    void ApplyProfilerStyle()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowPadding = {14.f, 12.f};
        style.FramePadding = {9.f, 6.f};
        style.CellPadding = {8.f, 5.f};
        style.ItemSpacing = {9.f, 7.f};
        style.ItemInnerSpacing = {7.f, 5.f};
        style.ScrollbarSize = 13.f;
        style.WindowRounding = 0.f;
        style.ChildRounding = 7.f;
        style.FrameRounding = 5.f;
        style.PopupRounding = 6.f;
        style.ScrollbarRounding = 8.f;
        style.GrabRounding = 5.f;
        style.TabRounding = 5.f;

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_WindowBg] = {0.043f, 0.050f, 0.066f, 1.00f};
        colors[ImGuiCol_ChildBg] = {0.060f, 0.070f, 0.090f, 1.00f};
        colors[ImGuiCol_PopupBg] = {0.060f, 0.070f, 0.090f, 0.98f};
        colors[ImGuiCol_Border] = {0.18f, 0.22f, 0.29f, 0.65f};
        colors[ImGuiCol_FrameBg] = {0.085f, 0.102f, 0.132f, 1.00f};
        colors[ImGuiCol_FrameBgHovered] = {0.13f, 0.18f, 0.24f, 1.00f};
        colors[ImGuiCol_FrameBgActive] = {0.16f, 0.24f, 0.32f, 1.00f};
        colors[ImGuiCol_TitleBg] = colors[ImGuiCol_WindowBg];
        colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_WindowBg];
        colors[ImGuiCol_Button] = {0.10f, 0.14f, 0.19f, 1.00f};
        colors[ImGuiCol_ButtonHovered] = {0.15f, 0.26f, 0.36f, 1.00f};
        colors[ImGuiCol_ButtonActive] = {0.18f, 0.37f, 0.52f, 1.00f};
        colors[ImGuiCol_Header] = {0.12f, 0.22f, 0.31f, 0.75f};
        colors[ImGuiCol_HeaderHovered] = {0.15f, 0.31f, 0.43f, 0.85f};
        colors[ImGuiCol_HeaderActive] = {0.18f, 0.40f, 0.56f, 0.90f};
        colors[ImGuiCol_Tab] = {0.07f, 0.09f, 0.12f, 1.00f};
        colors[ImGuiCol_TabHovered] = {0.12f, 0.29f, 0.42f, 1.00f};
        colors[ImGuiCol_TabSelected] = {0.11f, 0.23f, 0.33f, 1.00f};
        colors[ImGuiCol_CheckMark] = kAccent;
        colors[ImGuiCol_SliderGrab] = kAccent;
        colors[ImGuiCol_SliderGrabActive] = {0.42f, 0.80f, 1.00f, 1.00f};
        colors[ImGuiCol_Separator] = {0.16f, 0.20f, 0.27f, 0.8f};
        colors[ImGuiCol_TableHeaderBg] = {0.075f, 0.095f, 0.125f, 1.0f};
        colors[ImGuiCol_TableRowBgAlt] = {1.f, 1.f, 1.f, 0.025f};
        colors[ImGuiCol_Text] = {0.90f, 0.93f, 0.97f, 1.00f};
        colors[ImGuiCol_TextDisabled] = {0.48f, 0.54f, 0.64f, 1.00f};
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
                out.color = {static_cast<Uint8>(color & 0xFF),
                             static_cast<Uint8>((color >> 8) & 0xFF),
                             static_cast<Uint8>((color >> 16) & 0xFF),
                             static_cast<Uint8>((color >> 24) & 0xFF)};
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
                {
                    const ImDrawIdx index = cmdList->IdxBuffer[cmd.IdxOffset + i];
                    indices.push_back(static_cast<int>(index + cmd.VtxOffset));
                }

                SDL_Texture *texture = reinterpret_cast<SDL_Texture *>(static_cast<intptr_t>(cmd.GetTexID()));
                SDL_RenderSetClipRect(renderer, &clipRect);
                SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()),
                                   indices.data(), static_cast<int>(indices.size()));
            }
        }

        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    }

    bool CreateFontTexture(SDL_Renderer *renderer, SDL_Texture *&fontTexture)
    {
        ImGuiIO &io = ImGui::GetIO();
        const std::string fontPath = pe::Path::ResolveAsset("Fonts/Inter-Regular.ttf");
        if (!std::filesystem::exists(fontPath) || !io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.f))
            io.Fonts->AddFontDefault();

        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        fontTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!fontTexture)
            return false;
        SDL_UpdateTexture(fontTexture, nullptr, pixels, width * 4);
        SDL_SetTextureBlendMode(fontTexture, SDL_BLENDMODE_BLEND);
        io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(fontTexture)));
        return true;
    }

    struct ScopeRow
    {
        std::string name;
        unsigned depth = 0;
        float curMs = 0.f;
        float startOffsetMs = 0.f;
    };

    struct CounterRow
    {
        std::string name;
        uint64_t value = 0;
    };

    struct FrameSample
    {
        uint64_t id = 0;
        float frameMs = 0.f;
        float cpuTotalMs = 0.f;
        float cpuUpdateMs = 0.f;
        float cpuDrawMs = 0.f;
        float gpuTotalMs = 0.f;
    };

    struct TraceEvent
    {
        std::string name;
        double timestampUs = 0.0;
        double durationUs = 0.0;
        unsigned depth = 0;
        unsigned track = 0; // 0 frame, 1 CPU, 2 GPU
    };

    struct LiveFrame
    {
        float fps = 0.f;
        float frameMs = 0.f;
        float cpuTotalMs = 0.f;
        float cpuUpdateMs = 0.f;
        float cpuDrawMs = 0.f;
        float cpuScopeTotalMs = 0.f;
        float gpuTotalMs = 0.f;
        uint32_t renderDocCaptureCount = 0;
        bool renderDocAvailable = false;
        uint64_t ramTotalMb = 0;
        uint64_t ramUsedMb = 0;
        uint64_t ramProcessMb = 0;
        uint64_t gpuVramAppMb = 0;
        uint64_t gpuVramOtherMb = 0;
        uint64_t gpuVramBudgetMb = 0;
        uint64_t gpuHostAppMb = 0;
        uint64_t gpuHostOtherMb = 0;
        uint64_t gpuHostBudgetMb = 0;
        std::vector<ScopeRow> cpu;
        std::vector<ScopeRow> gpu;
        std::vector<CounterRow> counters;
        std::vector<FrameSample> frameBatch;
    };

    struct ScopeStats
    {
        std::deque<float> samples;
        float minMs = 0.f;
        float maxMs = 0.f;
        float avgMs = 0.f;

        void Push(float value)
        {
            samples.push_back(value);
            if (samples.size() > kStatWindow)
                samples.pop_front();
            minMs = std::numeric_limits<float>::max();
            maxMs = 0.f;
            float sum = 0.f;
            for (const float sample : samples)
            {
                minMs = std::min(minMs, sample);
                maxMs = std::max(maxMs, sample);
                sum += sample;
            }
            avgMs = samples.empty() ? 0.f : sum / static_cast<float>(samples.size());
        }
    };

    struct SessionData
    {
        LiveFrame live;
        std::deque<FrameSample> history;
        std::unordered_map<std::string, ScopeStats> cpuStats;
        std::unordered_map<std::string, ScopeStats> gpuStats;
        std::unordered_map<std::string, std::deque<float>> counterHistory;
        std::string latestJson;
        uint64_t packets = 0;
        uint64_t nextFrameId = 1;
        uint64_t pinnedFrameId = 0;
        std::vector<TraceEvent> traceEvents;
        uint64_t traceStartUs = 0;
        uint64_t tracePackets = 0;
        bool hasData = false;
        bool traceRecording = false;
        bool traceFull = false;

        void StartTrace()
        {
            traceEvents.clear();
            traceStartUs = SDL_GetTicks64() * 1000;
            tracePackets = 0;
            traceRecording = true;
            traceFull = false;
        }

        void CaptureTrace(const LiveFrame &frame)
        {
            if (!traceRecording)
                return;

            const size_t eventCount = 1 + frame.cpu.size() + frame.gpu.size();
            if (traceEvents.size() + eventCount > kMaxTraceEvents)
            {
                traceRecording = false;
                traceFull = true;
                return;
            }

            const uint64_t nowUs = SDL_GetTicks64() * 1000;
            const double elapsedUs = nowUs >= traceStartUs ? static_cast<double>(nowUs - traceStartUs) : 0.0;
            const double frameStartUs = std::max(0.0, elapsedUs - frame.frameMs * 1000.0);
            traceEvents.push_back({"Frame", frameStartUs, frame.frameMs * 1000.0, 0, 0});
            for (const ScopeRow &scope : frame.cpu)
                traceEvents.push_back({scope.name, frameStartUs + scope.startOffsetMs * 1000.0,
                                       scope.curMs * 1000.0, scope.depth, 1});
            for (const ScopeRow &scope : frame.gpu)
                traceEvents.push_back({scope.name, frameStartUs + scope.startOffsetMs * 1000.0,
                                       scope.curMs * 1000.0, scope.depth, 2});
            tracePackets++;
        }

        void Accept(LiveFrame frame, std::string json)
        {
            CaptureTrace(frame);
            for (FrameSample &sample : frame.frameBatch)
            {
                sample.id = nextFrameId++;
                history.push_back(sample);
            }
            while (history.size() > kMaxFrameHistory)
                history.pop_front();

            if (pinnedFrameId != 0 &&
                std::none_of(history.begin(), history.end(), [this](const FrameSample &sample)
                             { return sample.id == pinnedFrameId; }))
                pinnedFrameId = 0;

            for (const ScopeRow &scope : frame.cpu)
                cpuStats[scope.name].Push(scope.curMs);
            for (const ScopeRow &scope : frame.gpu)
                gpuStats[scope.name].Push(scope.curMs);
            for (const CounterRow &counter : frame.counters)
            {
                auto &samples = counterHistory[counter.name];
                samples.push_back(static_cast<float>(counter.value));
                if (samples.size() > 180)
                    samples.pop_front();
            }

            live = std::move(frame);
            latestJson = std::move(json);
            packets++;
            hasData = true;
        }

        void Reset()
        {
            *this = {};
        }
    };

    bool ReadFloat(const rapidjson::Value &object, const char *key, float &value)
    {
        if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
            return false;
        value = object[key].GetFloat();
        return true;
    }

    bool ReadUint64(const rapidjson::Value &object, const char *key, uint64_t &value)
    {
        if (!object.IsObject() || !object.HasMember(key) || !object[key].IsUint64())
            return false;
        value = object[key].GetUint64();
        return true;
    }

    std::string ReadName(const rapidjson::Value &object)
    {
        if (!object.IsObject() || !object.HasMember("name") || !object["name"].IsString())
            return {};
        constexpr rapidjson::SizeType kMaxNameLength = 512;
        const rapidjson::SizeType length = std::min(object["name"].GetStringLength(), kMaxNameLength);
        return {object["name"].GetString(), length};
    }

    void ParseScopes(const rapidjson::Value &array, std::vector<ScopeRow> &rows)
    {
        if (!array.IsArray())
            return;
        rows.reserve(std::min<size_t>(array.Size(), kMaxScopeRows));
        for (const auto &scope : array.GetArray())
        {
            if (!scope.IsObject() || rows.size() >= kMaxScopeRows)
                break;
            ScopeRow row;
            row.name = ReadName(scope);
            if (scope.HasMember("depth") && scope["depth"].IsUint())
                row.depth = std::min(scope["depth"].GetUint(), 64u);
            ReadFloat(scope, "cur_ms", row.curMs);
            ReadFloat(scope, "start_offset_ms", row.startOffsetMs);
            row.curMs = std::max(0.f, row.curMs);
            row.startOffsetMs = std::max(0.f, row.startOffsetMs);
            rows.push_back(std::move(row));
        }
    }

    bool ParseFrame(const std::string &json, LiveFrame &frame)
    {
        rapidjson::Document doc;
        if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject())
            return false;

        if (doc.HasMember("overview") && doc["overview"].IsObject())
        {
            const auto &overview = doc["overview"];
            ReadFloat(overview, "fps", frame.fps);
            ReadFloat(overview, "frame_ms", frame.frameMs);
            ReadFloat(overview, "cpu_total_ms", frame.cpuTotalMs);
            ReadFloat(overview, "cpu_update_ms", frame.cpuUpdateMs);
            ReadFloat(overview, "cpu_draw_ms", frame.cpuDrawMs);
            ReadFloat(overview, "gpu_total_ms", frame.gpuTotalMs);
            if (overview.HasMember("renderdoc_available") && overview["renderdoc_available"].IsBool())
                frame.renderDocAvailable = overview["renderdoc_available"].GetBool();
            if (overview.HasMember("renderdoc_capture_count") && overview["renderdoc_capture_count"].IsUint())
                frame.renderDocCaptureCount = overview["renderdoc_capture_count"].GetUint();
            if (overview.HasMember("memory") && overview["memory"].IsObject())
            {
                const auto &memory = overview["memory"];
                ReadUint64(memory, "ram_total_mb", frame.ramTotalMb);
                ReadUint64(memory, "ram_used_mb", frame.ramUsedMb);
                ReadUint64(memory, "ram_process_mb", frame.ramProcessMb);
                ReadUint64(memory, "gpu_vram_app_mb", frame.gpuVramAppMb);
                ReadUint64(memory, "gpu_vram_other_mb", frame.gpuVramOtherMb);
                ReadUint64(memory, "gpu_vram_budget_mb", frame.gpuVramBudgetMb);
                ReadUint64(memory, "gpu_host_app_mb", frame.gpuHostAppMb);
                ReadUint64(memory, "gpu_host_other_mb", frame.gpuHostOtherMb);
                ReadUint64(memory, "gpu_host_budget_mb", frame.gpuHostBudgetMb);
            }
        }

        if (doc.HasMember("frame_history") && doc["frame_history"].IsArray())
        {
            const auto &history = doc["frame_history"];
            frame.frameBatch.reserve(std::min<rapidjson::SizeType>(history.Size(), 512));
            for (const auto &entry : history.GetArray())
            {
                if (!entry.IsObject() || frame.frameBatch.size() >= 512)
                    break;
                FrameSample sample;
                ReadFloat(entry, "frame_ms", sample.frameMs);
                ReadFloat(entry, "cpu_total_ms", sample.cpuTotalMs);
                ReadFloat(entry, "cpu_update_ms", sample.cpuUpdateMs);
                ReadFloat(entry, "cpu_draw_ms", sample.cpuDrawMs);
                ReadFloat(entry, "gpu_total_ms", sample.gpuTotalMs);
                frame.frameBatch.push_back(sample);
            }
        }
        if (frame.frameBatch.empty())
            frame.frameBatch.push_back({0, frame.frameMs, frame.cpuTotalMs, frame.cpuUpdateMs,
                                        frame.cpuDrawMs, frame.gpuTotalMs});

        if (doc.HasMember("cpu") && doc["cpu"].IsObject())
        {
            const auto &cpu = doc["cpu"];
            ReadFloat(cpu, "total_ms", frame.cpuScopeTotalMs);
            if (cpu.HasMember("scopes"))
                ParseScopes(cpu["scopes"], frame.cpu);
        }

        if (doc.HasMember("gpu") && doc["gpu"].IsObject())
        {
            const auto &gpu = doc["gpu"];
            ReadFloat(gpu, "total_ms", frame.gpuTotalMs);
            if (gpu.HasMember("passes"))
                ParseScopes(gpu["passes"], frame.gpu);
        }

        if (doc.HasMember("counters") && doc["counters"].IsArray())
        {
            frame.counters.reserve(std::min<size_t>(doc["counters"].Size(), kMaxCounterRows));
            for (const auto &counter : doc["counters"].GetArray())
            {
                if (!counter.IsObject() || frame.counters.size() >= kMaxCounterRows)
                    break;
                CounterRow row;
                row.name = ReadName(counter);
                if (counter.HasMember("value") && counter["value"].IsUint64())
                    row.value = counter["value"].GetUint64();
                frame.counters.push_back(std::move(row));
            }
        }
        return true;
    }

    float FrameBudgetMs(int targetFps)
    {
        return targetFps > 0 ? 1000.f / static_cast<float>(targetFps) : 16.667f;
    }

    void ItemTooltip(const char *text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayShort)
    {
        if (ImGui::IsItemHovered(flags))
            ImGui::SetTooltip("%s", text);
    }

    void DrawMetricCard(const char *id, const char *label, float value, const char *unit,
                        const char *detail, const char *explanation, float budget, ImVec4 accent)
    {
        ImGui::PushID(id);
        const ImVec2 size = {ImGui::GetContentRegionAvail().x, 90.f};
        ImGui::InvisibleButton("##card", size);
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, U32(ImVec4(0.065f, 0.078f, 0.102f, 1.f)), 7.f);
        draw->AddRect(p0, p1, U32(ImVec4(0.16f, 0.20f, 0.27f, 0.75f)), 7.f);
        draw->AddRectFilled(p0, {p0.x + 4.f, p1.y}, U32(accent), 7.f);
        draw->AddText({p0.x + 14.f, p0.y + 10.f}, U32(ImVec4(0.58f, 0.64f, 0.74f, 1.f)), label);

        char valueText[64];
        std::snprintf(valueText, sizeof(valueText), value >= 100.f ? "%.0f %s" : "%.2f %s", value, unit);
        draw->AddText(ImGui::GetFont(), 21.f, {p0.x + 14.f, p0.y + 31.f}, U32(ImVec4(0.94f, 0.96f, 0.99f, 1.f)), valueText);
        draw->AddText({p0.x + 14.f, p0.y + 61.f}, U32(ImVec4(0.46f, 0.53f, 0.63f, 1.f)), detail);

        if (budget > 0.f)
        {
            const float fraction = std::clamp(value / budget, 0.f, 1.f);
            const ImVec2 b0 = {p0.x + 14.f, p1.y - 8.f};
            const ImVec2 b1 = {p1.x - 12.f, p1.y - 5.f};
            draw->AddRectFilled(b0, b1, U32(ImVec4(1.f, 1.f, 1.f, 0.07f)), 2.f);
            draw->AddRectFilled(b0, {b0.x + (b1.x - b0.x) * fraction, b1.y}, U32(Heat(value / budget)), 2.f);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(explanation);
            ImGui::Separator();
            ImGui::Text("Current: %.3f %s", value, unit);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    void DrawMetrics(const LiveFrame &frame, int targetFps)
    {
        const float budget = FrameBudgetMs(targetFps);
        if (ImGui::BeginTable("##metric_cards", 4, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            char fpsDetail[48];
            std::snprintf(fpsDetail, sizeof(fpsDetail), "target %d FPS", targetFps);
            DrawMetricCard("fps", "FRAME RATE", frame.fps, "FPS", fpsDetail,
                           "Rendered frames per second measured over the latest streamed sample window.", 0.f,
                           frame.fps + 0.5f >= targetFps ? kGoodColor : kWarnColor);
            ImGui::TableNextColumn();
            char frameDetail[48];
            std::snprintf(frameDetail, sizeof(frameDetail), "%.2f ms budget", budget);
            DrawMetricCard("frame", "FRAME TIME", frame.frameMs, "ms", frameDetail,
                           "Total wall-clock duration of the latest rendered frame.", budget, kFrameColor);
            ImGui::TableNextColumn();
            char cpuDetail[48];
            std::snprintf(cpuDetail, sizeof(cpuDetail), "update %.2f / draw %.2f", frame.cpuUpdateMs, frame.cpuDrawMs);
            DrawMetricCard("cpu", "CPU", frame.cpuTotalMs, "ms", cpuDetail,
                           "Main-thread CPU time split into game/update and rendering/draw work.", budget, kCpuColor);
            ImGui::TableNextColumn();
            DrawMetricCard("gpu", "GPU", frame.gpuTotalMs, "ms", "timestamped render passes",
                           "Sum of top-level GPU timestamp regions from the latest completed GPU frame.", budget, kGpuColor);
            ImGui::EndTable();
        }
    }

    const FrameSample *FindFrame(const SessionData &session, uint64_t id)
    {
        const auto it = std::find_if(session.history.begin(), session.history.end(), [id](const FrameSample &sample)
                                     { return sample.id == id; });
        return it == session.history.end() ? nullptr : &*it;
    }

    size_t FindFrameIndex(const SessionData &session, uint64_t id)
    {
        for (size_t i = 0; i < session.history.size(); ++i)
            if (session.history[i].id == id)
                return i;
        return session.history.size();
    }

    void PinPreviousHitch(SessionData &session, float budget)
    {
        size_t index = session.pinnedFrameId == 0 ? session.history.size()
                                                  : FindFrameIndex(session, session.pinnedFrameId);
        while (index > 0)
        {
            --index;
            if (session.history[index].frameMs > budget)
            {
                session.pinnedFrameId = session.history[index].id;
                return;
            }
        }
    }

    void PinNextHitch(SessionData &session, float budget)
    {
        size_t index = session.pinnedFrameId == 0 ? 0 : FindFrameIndex(session, session.pinnedFrameId) + 1;
        for (; index < session.history.size(); ++index)
        {
            if (session.history[index].frameMs > budget)
            {
                session.pinnedFrameId = session.history[index].id;
                return;
            }
        }
    }

    void DrawFrameNavigator(SessionData &session, int targetFps)
    {
        const size_t count = session.history.size();
        const size_t selectedIndex = session.pinnedFrameId == 0 ? count : FindFrameIndex(session, session.pinnedFrameId);
        const float budget = FrameBudgetMs(targetFps);

        ImGui::BeginDisabled(count == 0 || selectedIndex == 0);
        if (ImGui::SmallButton("|<"))
            session.pinnedFrameId = session.history.front().id;
        ItemTooltip("Select the oldest retained frame.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("<"))
        {
            const size_t index = selectedIndex < count ? selectedIndex - 1 : count - 1;
            session.pinnedFrameId = session.history[index].id;
        }
        ItemTooltip("Select the previous retained frame.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(count == 0 || selectedIndex >= count - 1);
        if (ImGui::SmallButton(">"))
            session.pinnedFrameId = session.history[selectedIndex + 1].id;
        ItemTooltip("Select the next retained frame.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(session.pinnedFrameId == 0);
        if (ImGui::SmallButton(">| Live"))
            session.pinnedFrameId = 0;
        ItemTooltip("Return to the latest live frame.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();

        ImGui::SameLine(0.f, 18.f);
        ImGui::BeginDisabled(count == 0);
        if (ImGui::SmallButton("< Hitch"))
            PinPreviousHitch(session, budget);
        ItemTooltip("Jump backward to the previous frame over the selected frame budget.",
                    ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        ImGui::BeginDisabled(session.pinnedFrameId == 0);
        if (ImGui::SmallButton("Hitch >"))
            PinNextHitch(session, budget);
        ItemTooltip("Jump forward to the next frame over the selected frame budget.",
                    ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine(0.f, 18.f);
        if (session.pinnedFrameId == 0)
            ImGui::TextColored(kGoodColor, "LIVE");
        else
            ImGui::Text("Frame #%llu", static_cast<unsigned long long>(session.pinnedFrameId));
        ItemTooltip("Pinned frame summary. Detailed scope tables remain the latest streamed snapshot.");
    }

    void DrawFrameChart(SessionData &session, int targetFps, bool showFrame, bool showCpu, bool showGpu)
    {
        const ImVec2 requested = {ImGui::GetContentRegionAvail().x, 230.f};
        ImGui::InvisibleButton("##frame_chart", requested);
        const ImVec2 outer0 = ImGui::GetItemRectMin();
        const ImVec2 outer1 = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(outer0, outer1, U32(ImVec4(0.038f, 0.046f, 0.061f, 1.f)), 6.f);
        draw->AddRect(outer0, outer1, U32(ImVec4(0.15f, 0.19f, 0.26f, 0.8f)), 6.f);

        if (session.history.empty())
        {
            const char *message = "Waiting for frame samples...";
            const ImVec2 textSize = ImGui::CalcTextSize(message);
            draw->AddText({(outer0.x + outer1.x - textSize.x) * 0.5f,
                           (outer0.y + outer1.y - textSize.y) * 0.5f},
                          U32(ImVec4(0.46f, 0.53f, 0.63f, 1.f)), message);
            return;
        }

        const ImVec2 p0 = {outer0.x + 45.f, outer0.y + 12.f};
        const ImVec2 p1 = {outer1.x - 10.f, outer1.y - 23.f};
        const float budget = FrameBudgetMs(targetFps);
        float maxValue = budget * 2.f;
        for (const FrameSample &sample : session.history)
            maxValue = std::max({maxValue, sample.frameMs * 1.1f, sample.cpuTotalMs * 1.1f, sample.gpuTotalMs * 1.1f});
        maxValue = std::max(maxValue, 1.f);

        for (int line = 0; line <= 4; ++line)
        {
            const float t = static_cast<float>(line) / 4.f;
            const float y = p1.y - t * (p1.y - p0.y);
            draw->AddLine({p0.x, y}, {p1.x, y}, U32(ImVec4(1.f, 1.f, 1.f, line == 0 ? 0.16f : 0.055f)));
            char label[20];
            std::snprintf(label, sizeof(label), "%.1f", maxValue * t);
            draw->AddText({outer0.x + 6.f, y - 7.f}, U32(ImVec4(0.38f, 0.44f, 0.53f, 1.f)), label);
        }

        const float budgetY = p1.y - std::clamp(budget / maxValue, 0.f, 1.f) * (p1.y - p0.y);
        draw->AddLine({p0.x, budgetY}, {p1.x, budgetY}, U32(WithAlpha(kGoodColor, 0.5f)), 1.5f);
        char budgetText[32];
        std::snprintf(budgetText, sizeof(budgetText), "%d FPS budget", targetFps);
        draw->AddText({p1.x - ImGui::CalcTextSize(budgetText).x - 4.f, budgetY - 16.f},
                      U32(WithAlpha(kGoodColor, 0.8f)), budgetText);

        const size_t count = session.history.size();
        auto point = [&](size_t index, float value)
        {
            const float x = count > 1 ? p0.x + static_cast<float>(index) / static_cast<float>(count - 1) * (p1.x - p0.x) : p1.x;
            const float y = p1.y - std::clamp(value / maxValue, 0.f, 1.f) * (p1.y - p0.y);
            return ImVec2{x, y};
        };

        const float plotWidth = std::max(p1.x - p0.x, 1.f);
        const float plotHeight = p1.y - p0.y;
        auto valueY = [&](float value)
        {
            return p1.y - std::clamp(value / maxValue, 0.f, 1.f) * plotHeight;
        };

        auto line = [&](auto getter, ImVec4 color)
        {
            const size_t columns = std::min(count, static_cast<size_t>(std::max(1.f, std::floor(plotWidth))));
            std::vector<ImVec2> points;
            points.reserve(columns);
            for (size_t column = 0; column < columns; ++column)
            {
                const size_t begin = column * count / columns;
                const size_t end = std::max(begin + 1, (column + 1) * count / columns);
                float minimum = std::numeric_limits<float>::max();
                float maximum = 0.f;
                float sum = 0.f;
                for (size_t i = begin; i < std::min(end, count); ++i)
                {
                    const float value = getter(session.history[i]);
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                    sum += value;
                }
                const float x = columns > 1 ? p0.x + static_cast<float>(column) / static_cast<float>(columns - 1) * plotWidth
                                            : p1.x;
                if (end - begin > 1)
                    draw->AddLine({x, valueY(minimum)}, {x, valueY(maximum)}, U32(WithAlpha(color, 0.28f)));
                points.push_back({x, valueY(sum / static_cast<float>(end - begin))});
            }
            if (points.size() == 1)
                draw->AddCircleFilled(points.front(), 2.5f, U32(color));
            else
                draw->AddPolyline(points.data(), static_cast<int>(points.size()), U32(color), ImDrawFlags_None, 1.6f);
        };

        if (showFrame)
            line([](const FrameSample &sample)
                 { return sample.frameMs; },
                 kFrameColor);
        if (showCpu)
            line([](const FrameSample &sample)
                 { return sample.cpuTotalMs; },
                 kCpuColor);
        if (showGpu)
            line([](const FrameSample &sample)
                 { return sample.gpuTotalMs; },
                 kGpuColor);

        size_t hoverIndex = count;
        if (ImGui::IsItemHovered())
        {
            const float mouseX = std::clamp(ImGui::GetIO().MousePos.x, p0.x, p1.x);
            hoverIndex = count > 1 ? static_cast<size_t>(std::lround((mouseX - p0.x) / (p1.x - p0.x) * static_cast<float>(count - 1))) : 0;
            hoverIndex = std::min(hoverIndex, count - 1);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                session.pinnedFrameId = session.history[hoverIndex].id;
        }

        const FrameSample *highlight = nullptr;
        size_t highlightIndex = count;
        if (hoverIndex < count)
        {
            highlight = &session.history[hoverIndex];
            highlightIndex = hoverIndex;
        }
        else if (session.pinnedFrameId != 0)
        {
            highlight = FindFrame(session, session.pinnedFrameId);
            if (highlight)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (session.history[i].id == session.pinnedFrameId)
                    {
                        highlightIndex = i;
                        break;
                    }
                }
            }
        }

        if (highlight && highlightIndex < count)
        {
            const float x = point(highlightIndex, 0.f).x;
            draw->AddLine({x, p0.y}, {x, p1.y}, U32(ImVec4(0.9f, 0.94f, 1.f, 0.7f)), 1.f);
            draw->AddCircleFilled(point(highlightIndex, highlight->frameMs), 3.5f, U32(kFrameColor));
            if (hoverIndex < count)
            {
                ImGui::BeginTooltip();
                ImGui::Text("Frame #%llu", static_cast<unsigned long long>(highlight->id));
                ImGui::Separator();
                ImGui::TextColored(kFrameColor, "Frame  %.3f ms", highlight->frameMs);
                ImGui::TextColored(kCpuColor, "CPU    %.3f ms", highlight->cpuTotalMs);
                ImGui::TextColored(kGpuColor, "GPU    %.3f ms", highlight->gpuTotalMs);
                ImGui::TextDisabled("Click to pin");
                ImGui::EndTooltip();
            }
        }

        draw->AddText({p0.x, p1.y + 4.f}, U32(ImVec4(0.38f, 0.44f, 0.53f, 1.f)), "older");
        const char *now = "latest";
        draw->AddText({p1.x - ImGui::CalcTextSize(now).x, p1.y + 4.f}, U32(ImVec4(0.38f, 0.44f, 0.53f, 1.f)), now);
    }

    struct BarSegment
    {
        const char *name;
        float value;
        ImVec4 color;
    };

    void DrawStackedBar(const char *label, const std::vector<BarSegment> &segments, float budget)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        float total = 0.f;
        for (const BarSegment &segment : segments)
            total += segment.value;
        ImGui::TextDisabled("%.3f ms / %.3f ms", total, budget);

        const ImVec2 size = {ImGui::GetContentRegionAvail().x, 20.f};
        ImGui::InvisibleButton(label, size);
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, U32(ImVec4(1.f, 1.f, 1.f, 0.06f)), 4.f);
        float x = p0.x;
        for (const BarSegment &segment : segments)
        {
            const float width = std::clamp(segment.value / std::max(budget, 0.001f), 0.f, 1.f) * (p1.x - p0.x);
            draw->AddRectFilled({x, p0.y}, {std::min(x + width, p1.x), p1.y}, U32(segment.color), 4.f);
            x = std::min(x + width, p1.x);
        }
        draw->AddRect(p0, p1, U32(ImVec4(1.f, 1.f, 1.f, 0.14f)), 4.f);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const BarSegment &segment : segments)
                ImGui::TextColored(segment.color, "%s  %.3f ms", segment.name, segment.value);
            ImGui::Separator();
            ImGui::TextDisabled("Bar length is relative to the selected frame budget.");
            ImGui::EndTooltip();
        }
    }

    void DrawMemoryBar(const char *label, uint64_t appMb, uint64_t otherMb, uint64_t budgetMb, ImVec4 color)
    {
        budgetMb = std::max<uint64_t>(budgetMb, 1);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::TextDisabled("app %llu MB   other %llu MB   budget %llu MB",
                            static_cast<unsigned long long>(appMb), static_cast<unsigned long long>(otherMb),
                            static_cast<unsigned long long>(budgetMb));
        const ImVec2 size = {ImGui::GetContentRegionAvail().x, 16.f};
        ImGui::InvisibleButton(label, size);
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, U32(ImVec4(1.f, 1.f, 1.f, 0.055f)), 4.f);
        const float appFraction = std::clamp(static_cast<float>(appMb) / static_cast<float>(budgetMb), 0.f, 1.f);
        const float otherFraction = std::clamp(static_cast<float>(otherMb) / static_cast<float>(budgetMb), 0.f, 1.f - appFraction);
        const float appX = p0.x + appFraction * (p1.x - p0.x);
        const float otherX = appX + otherFraction * (p1.x - p0.x);
        draw->AddRectFilled(p0, {appX, p1.y}, U32(color), 4.f);
        draw->AddRectFilled({appX, p0.y}, {otherX, p1.y}, U32(WithAlpha(color, 0.35f)), 4.f);
        draw->AddRect(p0, p1, U32(ImVec4(1.f, 1.f, 1.f, 0.13f)), 4.f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s budget usage", label);
            ImGui::Separator();
            ImGui::Text("Phasma process: %llu MB", static_cast<unsigned long long>(appMb));
            ImGui::Text("Other usage: %llu MB", static_cast<unsigned long long>(otherMb));
            ImGui::Text("Available budget: %llu MB", static_cast<unsigned long long>(budgetMb));
            ImGui::TextDisabled("Solid = Phasma, translucent = other usage.");
            ImGui::EndTooltip();
        }
    }

    void DrawMiniBar(float fraction)
    {
        fraction = std::clamp(fraction, 0.f, 1.f);
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 p0 = {ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 3.f};
        const ImVec2 p1 = {p0.x + ImGui::GetContentRegionAvail().x, p0.y + rowHeight - 6.f};
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, U32(ImVec4(1.f, 1.f, 1.f, 0.06f)), 3.f);
        draw->AddRectFilled(p0, {p0.x + fraction * (p1.x - p0.x), p1.y}, U32(Heat(fraction)), 3.f);
        char text[16];
        std::snprintf(text, sizeof(text), "%.1f%%", fraction * 100.f);
        draw->AddText({p0.x + 4.f, p0.y - 2.f}, U32(ImVec4(0.91f, 0.94f, 0.98f, 0.9f)), text);
        ImGui::Dummy({0.f, rowHeight});
    }

    void DrawHotspots(const char *id, const std::vector<ScopeRow> &rows, float totalMs, int &selected,
                      int &requestedTab, int tabIndex)
    {
        std::vector<int> order;
        order.reserve(rows.size());
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            if (rows[i].depth > 0 && rows[i].curMs > 0.f)
                order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&rows](int a, int b)
                  { return rows[a].curMs > rows[b].curMs; });
        if (order.size() > 6)
            order.resize(6);

        if (order.empty())
        {
            ImGui::TextDisabled("No timing samples yet.");
            return;
        }

        if (ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            for (const int index : order)
            {
                const ScopeRow &row = rows[index];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(index);
                if (ImGui::Selectable(row.name.c_str(), selected == index, ImGuiSelectableFlags_SpanAllColumns))
                {
                    selected = index;
                    requestedTab = tabIndex;
                }
                ItemTooltip(tabIndex == 1 ? "Open this scope in the CPU detail tab."
                                          : "Open this pass in the GPU detail tab.");
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f ms", row.curMs);
                ImGui::TableSetColumnIndex(2);
                DrawMiniBar(totalMs > 0.f ? row.curMs / totalMs : 0.f);
            }
            ImGui::EndTable();
        }
    }

    void DrawOverview(SessionData &session, int targetFps, int &requestedTab, int &selectedCpu, int &selectedGpu,
                      bool &showFrame, bool &showCpu, bool &showGpu)
    {
        const LiveFrame &frame = session.live;
        ImGui::TextColored(kFrameColor, "Frame");
        ImGui::SameLine();
        ImGui::Checkbox("##show_frame", &showFrame);
        ItemTooltip("Show or hide total frame time in the history graph.");
        ImGui::SameLine();
        ImGui::TextColored(kCpuColor, "CPU");
        ImGui::SameLine();
        ImGui::Checkbox("##show_cpu", &showCpu);
        ItemTooltip("Show or hide CPU frame time in the history graph.");
        ImGui::SameLine();
        ImGui::TextColored(kGpuColor, "GPU");
        ImGui::SameLine();
        ImGui::Checkbox("##show_gpu", &showGpu);
        ItemTooltip("Show or hide GPU frame time in the history graph.");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu frames retained", session.history.size());
        ItemTooltip("Rendered-frame summaries kept for graphs and session statistics. Dense history is reduced to a per-pixel average and min/max envelope so spikes remain visible without aliasing.");
        DrawFrameNavigator(session, targetFps);
        DrawFrameChart(session, targetFps, showFrame, showCpu, showGpu);

        const float budget = FrameBudgetMs(targetFps);
        const FrameSample *selectedFrame = session.pinnedFrameId == 0
                                               ? (session.history.empty() ? nullptr : &session.history.back())
                                               : FindFrame(session, session.pinnedFrameId);
        if (selectedFrame)
        {
            const float budgetDelta = selectedFrame->frameMs - budget;
            ImGui::SeparatorText(session.pinnedFrameId == 0 ? "Latest frame" : "Selected frame");
            ItemTooltip(session.pinnedFrameId == 0
                            ? "Summary of the newest retained rendered frame."
                            : "Historical frame summary retained by the live stream. CPU/GPU scope tables below remain the latest detailed snapshot.");
            if (ImGui::BeginTable("##selected_frame", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg))
            {
                const std::array<std::pair<const char *, float>, 5> metrics = {
                    std::pair{"FRAME", selectedFrame->frameMs}, std::pair{"CPU", selectedFrame->cpuTotalMs},
                    std::pair{"GPU", selectedFrame->gpuTotalMs}, std::pair{"UPDATE", selectedFrame->cpuUpdateMs},
                    std::pair{"DRAW", selectedFrame->cpuDrawMs}};
                for (const auto &[label, value] : metrics)
                {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", label);
                    ImGui::TextColored(Heat(value / budget), "%.3f ms", value);
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("BUDGET");
                if (budgetDelta > 0.f)
                    ImGui::TextColored(kBadColor, "+%.3f ms HITCH", budgetDelta);
                else
                    ImGui::TextColored(kGoodColor, "%.3f ms headroom", -budgetDelta);
                ImGui::EndTable();
            }
        }

        if (ImGui::BeginTable("##overview_columns", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Frame composition");
            ItemTooltip("CPU update/draw and GPU work compared with the selected frame budget.");
            DrawStackedBar("CPU", {{"Update", frame.cpuUpdateMs, kCpuColor}, {"Draw", frame.cpuDrawMs, WithAlpha(kFrameColor, 0.9f)}},
                           budget);
            DrawStackedBar("GPU", {{"Render passes", frame.gpuTotalMs, kGpuColor}}, budget);

            ImGui::Spacing();
            ImGui::SeparatorText("CPU hotspots");
            ItemTooltip("The most expensive nested CPU scopes in the latest detailed snapshot. Click one for details.");
            DrawHotspots("##cpu_hotspots", frame.cpu, std::max(frame.cpuScopeTotalMs, 0.001f), selectedCpu,
                         requestedTab, 1);

            ImGui::TableNextColumn();
            ImGui::SeparatorText("Memory pressure");
            ItemTooltip("Current process and system/GPU memory use compared with the reported budget.");
            const uint64_t ramOther = frame.ramUsedMb > frame.ramProcessMb ? frame.ramUsedMb - frame.ramProcessMb : 0;
            DrawMemoryBar("System RAM", frame.ramProcessMb, ramOther, frame.ramTotalMb, kCpuColor);
            DrawMemoryBar("GPU local", frame.gpuVramAppMb, frame.gpuVramOtherMb, frame.gpuVramBudgetMb, kGpuColor);
            DrawMemoryBar("GPU host", frame.gpuHostAppMb, frame.gpuHostOtherMb, frame.gpuHostBudgetMb, kFrameColor);

            ImGui::Spacing();
            ImGui::SeparatorText("GPU hotspots");
            ItemTooltip("The most expensive nested GPU passes in the latest completed GPU frame. Click one for details.");
            DrawHotspots("##gpu_hotspots", frame.gpu, std::max(frame.gpuTotalMs, 0.001f), selectedGpu,
                         requestedTab, 2);
            ImGui::EndTable();
        }
    }

    void DrawTimingTable(const char *id, const std::vector<ScopeRow> &rows,
                         const std::unordered_map<std::string, ScopeStats> &stats, float totalMs,
                         const char *filter, int &selected)
    {
        if (rows.empty())
        {
            ImGui::TextDisabled("No timing scopes received.");
            return;
        }
        totalMs = std::max(totalMs, 0.001f);
        const bool filtering = filter[0] != '\0';
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings;
        if (!ImGui::BeginTable(id, 6, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
            return;

        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.46f);
        ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 62.f);
        ImGui::TableSetupColumn("current", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 62.f);
        ImGui::TableSetupColumn("average", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, 0.24f);
        ImGui::TableHeadersRow();

        int skipDepth = std::numeric_limits<int>::max();
        int openTree = 0;
        for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        {
            const ScopeRow &row = rows[index];
            if (!ContainsCaseInsensitive(row.name, filter))
                continue;
            const float relative = row.curMs / totalMs;
            const auto statIt = stats.find(row.name);
            const ScopeStats *stat = statIt == stats.end() ? nullptr : &statIt->second;

            if (!filtering)
            {
                const int depth = static_cast<int>(row.depth);
                if (depth > skipDepth)
                    continue;
                skipDepth = std::numeric_limits<int>::max();
                while (openTree > depth)
                {
                    ImGui::TreePop();
                    openTree--;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(index);
            const bool hasChildren = !filtering && index + 1 < static_cast<int>(rows.size()) && rows[index + 1].depth > row.depth;
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
            if (selected == index)
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            if (filtering || !hasChildren)
                nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            else
                nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
            const bool open = ImGui::TreeNodeEx(row.name.c_str(), nodeFlags);
            if (ImGui::IsItemClicked())
                selected = selected == index ? -1 : index;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::TextColored(Heat(relative), "%.3f ms  (%.1f%%)", row.curMs, relative * 100.f);
                if (stat)
                    ImGui::TextDisabled("min %.3f   avg %.3f   max %.3f", stat->minMs, stat->avgMs, stat->maxMs);
                ImGui::EndTooltip();
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(stat ? "%.3f" : "--", stat ? stat->minMs : 0.f);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(Heat(relative), "%.3f", row.curMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled(stat ? "%.3f" : "--", stat ? stat->maxMs : 0.f);
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled(stat ? "%.3f" : "--", stat ? stat->avgMs : 0.f);
            ImGui::TableSetColumnIndex(5);
            DrawMiniBar(relative);

            if (!filtering && hasChildren)
            {
                if (open)
                    openTree++;
                else
                    skipDepth = static_cast<int>(row.depth);
            }
        }
        while (openTree > 0)
        {
            ImGui::TreePop();
            openTree--;
        }
        ImGui::EndTable();
    }

    struct AggregatedScope
    {
        std::string name;
        int firstIndex = -1;
        size_t calls = 0;
        float inclusiveMs = 0.f;
        float selfMs = 0.f;
        float maxMs = 0.f;
    };

    std::vector<AggregatedScope> AggregateScopes(const std::vector<ScopeRow> &rows, const char *filter)
    {
        std::vector<float> directChildMs(rows.size(), 0.f);
        std::vector<int> parentStack;
        parentStack.reserve(64);
        for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        {
            const size_t depth = std::min(static_cast<size_t>(rows[index].depth), parentStack.size());
            parentStack.resize(depth);
            if (!parentStack.empty())
                directChildMs[parentStack.back()] += rows[index].curMs;
            parentStack.push_back(index);
        }

        std::vector<AggregatedScope> result;
        std::unordered_map<std::string, size_t> byName;
        for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        {
            const ScopeRow &row = rows[index];
            if (!ContainsCaseInsensitive(row.name, filter))
                continue;
            const auto [it, inserted] = byName.try_emplace(row.name, result.size());
            if (inserted)
                result.push_back({row.name, index});
            AggregatedScope &scope = result[it->second];
            scope.calls++;
            scope.inclusiveMs += row.curMs;
            scope.selfMs += std::max(0.f, row.curMs - directChildMs[index]);
            scope.maxMs = std::max(scope.maxMs, row.curMs);
        }
        return result;
    }

    void DrawAggregatedTimingTable(const char *id, const std::vector<ScopeRow> &rows, float totalMs,
                                   const char *filter, int &selected)
    {
        if (rows.empty())
        {
            ImGui::TextDisabled("No timing scopes received.");
            return;
        }

        std::vector<AggregatedScope> scopes = AggregateScopes(rows, filter);
        if (scopes.empty())
        {
            ImGui::TextDisabled("No scopes match the filter.");
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Sortable | ImGuiTableFlags_NoSavedSettings;
        if (!ImGui::BeginTable(id, 7, flags, {-FLT_MIN, ImGui::GetContentRegionAvail().y}))
            return;

        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 58.f);
        ImGui::TableSetupColumn("Inclusive", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending,
                                76.f);
        ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableHeadersRow();

        if (const ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0)
        {
            const ImGuiTableColumnSortSpecs &sort = sortSpecs->Specs[0];
            std::stable_sort(scopes.begin(), scopes.end(), [&](const AggregatedScope &a, const AggregatedScope &b)
                             {
                                 int comparison = 0;
                                 auto compare = [&](auto lhs, auto rhs)
                                 {
                                     comparison = lhs < rhs ? -1 : lhs > rhs ? 1
                                                                              : 0;
                                 };
                                 switch (sort.ColumnIndex)
                                 {
                                 case 0:
                                     comparison = a.name.compare(b.name);
                                     break;
                                 case 1:
                                     compare(a.calls, b.calls);
                                     break;
                                 case 2:
                                     compare(a.inclusiveMs, b.inclusiveMs);
                                     break;
                                 case 3:
                                     compare(a.selfMs, b.selfMs);
                                     break;
                                 case 4:
                                     compare(a.inclusiveMs / static_cast<float>(a.calls),
                                             b.inclusiveMs / static_cast<float>(b.calls));
                                     break;
                                 case 5:
                                     compare(a.maxMs, b.maxMs);
                                     break;
                                 default:
                                     compare(a.inclusiveMs, b.inclusiveMs);
                                     break;
                                 }
                                 if (comparison == 0)
                                     comparison = a.name.compare(b.name);
                                 return sort.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0; });
        }

        totalMs = std::max(totalMs, 0.001f);
        for (const AggregatedScope &scope : scopes)
        {
            const float share = scope.inclusiveMs / totalMs;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(scope.firstIndex);
            if (ImGui::Selectable(scope.name.c_str(), selected == scope.firstIndex, ImGuiSelectableFlags_SpanAllColumns))
                selected = selected == scope.firstIndex ? -1 : scope.firstIndex;
            ItemTooltip("Select the first matching occurrence; the selection is shared with Hierarchy and Timeline views.");
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", scope.calls);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(Heat(share), "%.3f", scope.inclusiveMs);
            ItemTooltip("Inclusive time: this scope plus all nested child scopes, summed across matching calls.");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", scope.selfMs);
            ItemTooltip("Self time: inclusive time minus direct child-scope time.");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", scope.inclusiveMs / static_cast<float>(scope.calls));
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.3f", scope.maxMs);
            ImGui::TableSetColumnIndex(6);
            DrawMiniBar(share);
        }
        ImGui::EndTable();
    }

    float NiceTick(float span)
    {
        const float rough = std::max(span / 10.f, 0.01f);
        const float magnitude = std::pow(10.f, std::floor(std::log10(rough)));
        const float normalized = rough / magnitude;
        const float nice = normalized < 2.f ? 1.f : normalized < 5.f ? 2.f
                                                                     : 5.f;
        return nice * magnitude;
    }

    void DrawTimeline(const char *id, const std::vector<ScopeRow> &rows, float totalMs,
                      float &horizontalZoom, float &verticalZoom, int &selected)
    {
        std::vector<int> visible;
        visible.reserve(std::min<size_t>(rows.size(), 512));
        float spanMs = 0.f;
        for (int i = 0; i < static_cast<int>(rows.size()) && visible.size() < 512; ++i)
        {
            if (rows[i].curMs <= 0.002f)
                continue;
            visible.push_back(i);
            spanMs = std::max(spanMs, rows[i].startOffsetMs + rows[i].curMs);
        }
        if (visible.empty())
        {
            ImGui::TextDisabled("No positive timing scopes received.");
            return;
        }
        spanMs = std::max({spanMs * 1.03f, totalMs, 0.1f});

        ImGui::TextDisabled("H");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.f);
        ImGui::SliderFloat("##hzoom", &horizontalZoom, 0.5f, 24.f, "%.1fx");
        ItemTooltip("Horizontal time-axis zoom. The mouse wheel also zooms around the cursor.");
        ImGui::SameLine();
        ImGui::TextDisabled("V");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.f);
        ImGui::SliderFloat("##vzoom", &verticalZoom, 0.7f, 2.5f, "%.1fx");
        ItemTooltip("Vertical row-height zoom for separating dense scope bars.");
        ImGui::SameLine();
        ImGui::TextDisabled("Wheel to zoom around cursor  |  click a bar to inspect");
        if (rows.size() > visible.size())
        {
            ImGui::SameLine();
            ImGui::TextColored(kWarnColor, "| showing first %zu significant scopes", visible.size());
        }

        const float rowHeight = 23.f * verticalZoom;
        const float gap = 2.f;
        const float chartHeight = static_cast<float>(visible.size()) * (rowHeight + gap) + 23.f;
        const float childHeight = std::max(100.f, ImGui::GetContentRegionAvail().y - 52.f);
        ImGui::PushID(id);
        ImGui::BeginChild("##timeline_scroll", {-1.f, childHeight}, false,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (ImGui::IsWindowHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
            {
                const float localMouseX = ImGui::GetIO().MousePos.x - ImGui::GetWindowPos().x;
                const float contentMouseX = localMouseX + ImGui::GetScrollX();
                const float oldZoom = horizontalZoom;
                horizontalZoom = std::clamp(horizontalZoom * (1.f + wheel * 0.15f), 0.5f, 24.f);
                ImGui::SetScrollX(std::max(0.f, contentMouseX * (horizontalZoom / oldZoom) - localMouseX));
            }
        }

        const float chartWidth = std::max(ImGui::GetContentRegionAvail().x, 400.f) * horizontalZoom;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + chartWidth, origin.y + chartHeight},
                            U32(ImVec4(0.032f, 0.039f, 0.052f, 1.f)));

        const float tick = NiceTick(spanMs);
        for (float time = 0.f; time <= spanMs; time += tick)
        {
            const float x = origin.x + time / spanMs * chartWidth;
            draw->AddLine({x, origin.y}, {x, origin.y + chartHeight}, U32(ImVec4(1.f, 1.f, 1.f, 0.07f)));
            char label[24];
            std::snprintf(label, sizeof(label), "%.2f ms", time);
            draw->AddText({x + 3.f, origin.y + 3.f}, U32(ImVec4(0.40f, 0.46f, 0.55f, 1.f)), label);
        }

        for (int rowIndex = 0; rowIndex < static_cast<int>(visible.size()); ++rowIndex)
        {
            const int index = visible[rowIndex];
            const ScopeRow &scope = rows[index];
            const float y0 = origin.y + 22.f + rowIndex * (rowHeight + gap);
            const float y1 = y0 + rowHeight;
            const float x0 = origin.x + scope.startOffsetMs / spanMs * chartWidth;
            const float x1 = std::max(x0 + 3.f, origin.x + (scope.startOffsetMs + scope.curMs) / spanMs * chartWidth);
            const ImVec4 color = ScopeColor(scope.name);
            draw->AddRectFilled({x0, y0}, {x1, y1}, U32(WithAlpha(color, 0.80f)), 4.f);
            draw->AddRect({x0, y0}, {x1, y1}, selected == index ? U32(kFrameColor) : U32(WithAlpha(color, 1.f)),
                          4.f, ImDrawFlags_None, selected == index ? 2.f : 1.f);
            if (x1 - x0 > 24.f)
            {
                draw->PushClipRect({x0 + 2.f, y0}, {x1 - 2.f, y1}, true);
                draw->AddText({x0 + 5.f, y0 + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f},
                              U32(ImVec4(0.98f, 0.99f, 1.f, 1.f)), scope.name.c_str());
                draw->PopClipRect();
            }

            ImGui::SetCursorScreenPos({x0, y0});
            ImGui::InvisibleButton(("##scope_" + std::to_string(index)).c_str(), {std::max(x1 - x0, 4.f), rowHeight});
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(scope.name.c_str());
                ImGui::Separator();
                ImGui::Text("Start %.3f ms", scope.startOffsetMs);
                ImGui::Text("Duration %.3f ms", scope.curMs);
                ImGui::Text("Share %.1f%%", totalMs > 0.f ? scope.curMs / totalMs * 100.f : 0.f);
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked())
                selected = selected == index ? -1 : index;
        }

        ImGui::SetCursorPos({chartWidth, chartHeight});
        ImGui::Dummy({1.f, 1.f});
        ImGui::EndChild();
        ImGui::PopID();

        if (selected >= 0 && selected < static_cast<int>(rows.size()))
        {
            const ScopeRow &scope = rows[selected];
            ImGui::TextColored(ScopeColor(scope.name), "%s", scope.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("start %.3f ms   duration %.3f ms   share %.1f%%", scope.startOffsetMs,
                                scope.curMs, totalMs > 0.f ? scope.curMs / totalMs * 100.f : 0.f);
        }
    }

    void DrawTimingTab(const char *kind, const std::vector<ScopeRow> &rows,
                       const std::unordered_map<std::string, ScopeStats> &stats, float totalMs,
                       char *filter, size_t filterSize, int &viewMode, float &zoomH, float &zoomV, int &selected)
    {
        static const char *views[] = {"Hierarchy", "Timeline", "Aggregated"};
        ImGui::SetNextItemWidth(140.f);
        ImGui::Combo("##view", &viewMode, views, IM_ARRAYSIZE(views));
        ItemTooltip("Hierarchy preserves nesting and rolling statistics. Timeline shows when scopes ran. Aggregated groups repeated names and separates inclusive from self time.");
        ImGui::SameLine();
        ImGui::TextColored(std::strcmp(kind, "CPU") == 0 ? kCpuColor : kGpuColor, "%s %.3f ms", kind, totalMs);
        if (viewMode != 1)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##filter", "Filter scopes...", filter, filterSize);
            ItemTooltip(viewMode == 0
                            ? "Filter scope names case-insensitively. Filtering flattens the hierarchy so every match remains visible."
                            : "Filter aggregated scope names case-insensitively.");
        }
        ImGui::Separator();
        if (viewMode == 0)
            DrawTimingTable("##timings", rows, stats, totalMs, filter, selected);
        else if (viewMode == 1)
            DrawTimeline("##timeline", rows, totalMs, zoomH, zoomV, selected);
        else
            DrawAggregatedTimingTable("##aggregated", rows, totalMs, filter, selected);
    }

    void DrawCounters(const SessionData &session, char *filter, size_t filterSize)
    {
        ImGui::SetNextItemWidth(320.f);
        ImGui::InputTextWithHint("##counter_filter", "Filter counters...", filter, filterSize);
        ItemTooltip("Filter PE_PROFILE_COUNTER names case-insensitively.");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu counters", session.live.counters.size());
        if (session.live.counters.empty())
        {
            ImGui::TextDisabled("No PE_PROFILE_COUNTER values received for this frame.");
            return;
        }

        if (ImGui::BeginTable("##counters", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollY,
                              {-1.f, -1.f}))
        {
            ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 140.f);
            ImGui::TableSetupColumn("Recent trend", ImGuiTableColumnFlags_WidthStretch, 0.40f);
            ImGui::TableHeadersRow();
            for (const CounterRow &counter : session.live.counters)
            {
                if (!ContainsCaseInsensitive(counter.name, filter))
                    continue;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 38.f);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(counter.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", static_cast<unsigned long long>(counter.value));
                ImGui::TableSetColumnIndex(2);
                const auto it = session.counterHistory.find(counter.name);
                if (it != session.counterHistory.end() && !it->second.empty())
                {
                    std::vector<float> samples(it->second.begin(), it->second.end());
                    ImGui::PlotLines(("##" + counter.name).c_str(), samples.data(), static_cast<int>(samples.size()),
                                     0, nullptr, FLT_MAX, FLT_MAX, {-1.f, 28.f});
                    ItemTooltip("Recent streamed values: oldest on the left, newest on the right.");
                }
            }
            ImGui::EndTable();
        }
    }

    std::string TimestampedPath(const char *stem, const char *extension)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
#if defined(PE_WIN32)
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        std::ostringstream path;
        path << "ProfilerCaptures/" << stem << '_' << std::put_time(&local, "%Y%m%d_%H%M%S") << extension;
        return path.str();
    }

    bool SaveTextFile(const std::string &path, const std::string &text)
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
        if (error)
            return false;
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return false;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }

    std::string SaveSnapshot(const SessionData &session)
    {
        if (session.latestJson.empty())
            return "No live snapshot to save";
        const std::string path = TimestampedPath("snapshot", ".json");
        return SaveTextFile(path, session.latestJson) ? "Saved " + path : "Failed to save " + path;
    }

    std::string SaveSessionCsv(const SessionData &session)
    {
        if (session.history.empty())
            return "No frame history to save";
        std::ostringstream csv;
        csv << "frame_id,frame_ms,cpu_total_ms,cpu_update_ms,cpu_draw_ms,gpu_total_ms\n";
        for (const FrameSample &sample : session.history)
            csv << sample.id << ',' << sample.frameMs << ',' << sample.cpuTotalMs << ',' << sample.cpuUpdateMs << ','
                << sample.cpuDrawMs << ',' << sample.gpuTotalMs << '\n';
        const std::string path = TimestampedPath("session", ".csv");
        return SaveTextFile(path, csv.str()) ? "Saved " + path : "Failed to save " + path;
    }

    std::string SaveChromeTrace(const SessionData &session)
    {
        if (session.traceEvents.empty())
            return "No recorded trace to save";

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("displayTimeUnit");
        writer.String("ms");
        writer.Key("traceEvents");
        writer.StartArray();

        constexpr std::array<const char *, 3> trackNames = {"Frame", "CPU main thread", "GPU"};
        for (unsigned track = 0; track < trackNames.size(); ++track)
        {
            writer.StartObject();
            writer.Key("name");
            writer.String("thread_name");
            writer.Key("ph");
            writer.String("M");
            writer.Key("pid");
            writer.Uint(1);
            writer.Key("tid");
            writer.Uint(track);
            writer.Key("args");
            writer.StartObject();
            writer.Key("name");
            writer.String(trackNames[track]);
            writer.EndObject();
            writer.EndObject();
        }

        constexpr std::array<const char *, 3> categories = {"frame", "cpu", "gpu"};
        for (const TraceEvent &event : session.traceEvents)
        {
            writer.StartObject();
            writer.Key("name");
            writer.String(event.name.c_str(), static_cast<rapidjson::SizeType>(event.name.size()));
            writer.Key("cat");
            writer.String(categories[event.track]);
            writer.Key("ph");
            writer.String("X");
            writer.Key("ts");
            writer.Double(event.timestampUs);
            writer.Key("dur");
            writer.Double(event.durationUs);
            writer.Key("pid");
            writer.Uint(1);
            writer.Key("tid");
            writer.Uint(event.track);
            writer.Key("args");
            writer.StartObject();
            writer.Key("depth");
            writer.Uint(event.depth);
            writer.EndObject();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        const std::string path = TimestampedPath("trace", ".json");
        return SaveTextFile(path, {buffer.GetString(), buffer.GetSize()}) ? "Saved " + path : "Failed to save " + path;
    }

    float Percentile(std::vector<float> values, float percentile)
    {
        if (values.empty())
            return 0.f;
        std::sort(values.begin(), values.end());
        const size_t index = static_cast<size_t>(std::clamp(percentile, 0.f, 1.f) * static_cast<float>(values.size() - 1));
        return values[index];
    }

    void DrawHistogram(const std::deque<FrameSample> &history, float budget)
    {
        constexpr int kBins = 24;
        std::array<float, kBins> bins{};
        float maxMs = budget * 3.f;
        for (const FrameSample &sample : history)
            maxMs = std::max(maxMs, sample.frameMs);
        for (const FrameSample &sample : history)
        {
            const int bin = std::clamp(static_cast<int>(sample.frameMs / maxMs * kBins), 0, kBins - 1);
            bins[bin] += 1.f;
        }
        const float maxCount = *std::max_element(bins.begin(), bins.end());
        const ImVec2 size = {ImGui::GetContentRegionAvail().x, 170.f};
        ImGui::InvisibleButton("##histogram", size);
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, U32(ImVec4(0.038f, 0.046f, 0.061f, 1.f)), 6.f);
        const float gap = 3.f;
        const float barWidth = (p1.x - p0.x - gap * (kBins + 1)) / kBins;
        int hovered = -1;
        for (int i = 0; i < kBins; ++i)
        {
            const float height = maxCount > 0.f ? bins[i] / maxCount * (p1.y - p0.y - 24.f) : 0.f;
            const float x0 = p0.x + gap + i * (barWidth + gap);
            const ImVec2 b0 = {x0, p1.y - 19.f - height};
            const ImVec2 b1 = {x0 + barWidth, p1.y - 19.f};
            const float binMs = (static_cast<float>(i) + 0.5f) / kBins * maxMs;
            const ImVec4 color = Heat(binMs / std::max(budget, 0.001f));
            draw->AddRectFilled(b0, b1, U32(WithAlpha(color, 0.8f)), 3.f);
            if (ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x >= b0.x && ImGui::GetIO().MousePos.x <= b1.x)
                hovered = i;
        }
        draw->AddText({p0.x + 8.f, p1.y - 17.f}, U32(ImVec4(0.42f, 0.48f, 0.57f, 1.f)), "0 ms");
        char maxText[24];
        std::snprintf(maxText, sizeof(maxText), "%.1f ms", maxMs);
        draw->AddText({p1.x - ImGui::CalcTextSize(maxText).x - 8.f, p1.y - 17.f},
                      U32(ImVec4(0.42f, 0.48f, 0.57f, 1.f)), maxText);
        if (hovered >= 0)
        {
            const float low = static_cast<float>(hovered) / kBins * maxMs;
            const float high = static_cast<float>(hovered + 1) / kBins * maxMs;
            ImGui::SetTooltip("%.2f - %.2f ms\n%.0f frames", low, high, bins[hovered]);
        }
    }

    void DrawSessionTab(SessionData &session, int targetFps, const char *host, int port,
                        bool connected, pe::ProfilerStreamClient &client, int &renderDocFrameCount,
                        bool &autoConnect, bool &requestReconnect, int &requestedTab, std::string &notice)
    {
        ImGui::SeparatorText("Connection");
        ImGui::TextColored(connected ? kGoodColor : kWarnColor, connected ? "CONNECTED" : "DISCONNECTED");
        ItemTooltip("Whether this viewer currently has a live TCP connection to the Player profiler stream.");
        ImGui::SameLine();
        ImGui::TextDisabled("%s:%d   |   %llu packets", host, port, static_cast<unsigned long long>(session.packets));
        ItemTooltip("Loopback stream endpoint and detailed snapshot packets received this session.");
        ImGui::SameLine();
        if (ImGui::Button(connected || autoConnect ? "Disconnect" : "Connect"))
        {
            if (connected || autoConnect)
                autoConnect = false;
            else
            {
                autoConnect = true;
                requestReconnect = true;
            }
        }
        ItemTooltip(connected || autoConnect ? "Stop receiving data and disable automatic reconnection."
                                             : "Enable automatic connection attempts to the Player.");
        ImGui::SameLine();
        if (ImGui::Button("Reconnect"))
        {
            autoConnect = true;
            requestReconnect = true;
        }
        ItemTooltip("Drop the current connection and immediately connect again.");

        ImGui::SeparatorText("RenderDoc GPU capture");
        ItemTooltip("Request consecutive GPU frame captures from the connected Player through its existing RenderDoc API.");
        ImGui::TextColored(session.live.renderDocAvailable ? kGoodColor : kWarnColor,
                           session.live.renderDocAvailable ? "AVAILABLE" : "UNAVAILABLE");
        ImGui::SameLine();
        ImGui::TextDisabled("%u captures saved by Player", session.live.renderDocCaptureCount);
        ItemTooltip("RenderDoc captures reported by this Player process since startup.");

        const bool canCapture = connected && session.live.renderDocAvailable;
        constexpr ImGuiHoveredFlags disabledTooltipFlags =
            ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled;
        ImGui::BeginDisabled(!canCapture);
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputInt("Frames##renderdoc", &renderDocFrameCount);
        renderDocFrameCount = std::clamp(renderDocFrameCount, 1, static_cast<int>(pe::kProfilerMaxRenderDocCaptureFrames));
        ItemTooltip("Number of consecutive Player GPU frames to capture.", disabledTooltipFlags);
        ImGui::SameLine();
        if (ImGui::Button("Capture with RenderDoc"))
        {
            if (client.TriggerRenderDocCapture(static_cast<uint8_t>(renderDocFrameCount)))
                notice = "Requested RenderDoc capture for " + std::to_string(renderDocFrameCount) + " frame(s)";
            else
                notice = "Failed to send RenderDoc capture request";
        }
        ItemTooltip("Trigger the Player's RenderDoc capture and open its replay UI.", disabledTooltipFlags);
        ImGui::EndDisabled();
        if (!session.live.renderDocAvailable)
            ImGui::TextDisabled("Requires a debug Player built with PE_ENABLE_RENDERDOC_CAPTURE=ON and RenderDoc installed.");

        ImGui::SeparatorText("Session analysis");
        ItemTooltip("Statistics calculated from the retained rendered-frame history.");
        std::vector<float> frameTimes;
        frameTimes.reserve(session.history.size());
        for (const FrameSample &sample : session.history)
            frameTimes.push_back(sample.frameMs);
        const float average = frameTimes.empty() ? 0.f : std::accumulate(frameTimes.begin(), frameTimes.end(), 0.f) / frameTimes.size();
        const float p50 = Percentile(frameTimes, 0.50f);
        const float p95 = Percentile(frameTimes, 0.95f);
        const float p99 = Percentile(frameTimes, 0.99f);
        const float worst = frameTimes.empty() ? 0.f : *std::max_element(frameTimes.begin(), frameTimes.end());
        const float budget = FrameBudgetMs(targetFps);

        if (ImGui::BeginTable("##session_metrics", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg))
        {
            const std::array<std::pair<const char *, float>, 5> metrics = {
                std::pair{"AVERAGE", average}, std::pair{"P50", p50}, std::pair{"P95", p95},
                std::pair{"P99", p99}, std::pair{"WORST", worst}};
            constexpr std::array<const char *, 5> descriptions = {
                "Mean frame time across retained frames.",
                "Median frame time: half the retained frames are faster.",
                "95th-percentile frame time: 95% of retained frames are faster.",
                "99th-percentile frame time: 99% of retained frames are faster.",
                "Slowest retained frame time.",
            };
            for (size_t i = 0; i < metrics.size(); ++i)
            {
                const auto &[label, value] = metrics[i];
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ItemTooltip(descriptions[i]);
                ImGui::TextColored(Heat(value / budget), "%.3f ms", value);
                ItemTooltip(descriptions[i]);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Frame-time distribution (%zu frames)", session.history.size());
        ItemTooltip("Histogram of retained frame times. Hover a bar for its time range and frame count.");
        DrawHistogram(session.history, budget);

        std::vector<const FrameSample *> slowFrames;
        for (const FrameSample &sample : session.history)
            if (sample.frameMs > budget)
                slowFrames.push_back(&sample);
        std::sort(slowFrames.begin(), slowFrames.end(), [](const FrameSample *a, const FrameSample *b)
                  { return a->frameMs > b->frameMs; });
        const size_t missCount = slowFrames.size();
        if (slowFrames.size() > 10)
            slowFrames.resize(10);

        ImGui::SeparatorText("Budget misses");
        ItemTooltip("Frames slower than the selected visualization budget, ranked by total frame time.");
        const float missRate = session.history.empty() ? 0.f : static_cast<float>(missCount) / session.history.size() * 100.f;
        ImGui::TextColored(missCount == 0 ? kGoodColor : kWarnColor, "%zu / %zu frames (%.1f%%) exceeded %.3f ms",
                           missCount, session.history.size(), missRate, budget);
        if (slowFrames.empty())
        {
            ImGui::TextDisabled("No retained frame exceeded the current budget.");
        }
        else if (ImGui::BeginTable("##slow_frames", 5,
                                   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Frame ms", ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 75.f);
            ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 75.f);
            ImGui::TableSetupColumn("Over budget", ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableHeadersRow();
            for (const FrameSample *sample : slowFrames)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string label = "#" + std::to_string(sample->id);
                ImGui::PushID(static_cast<int>(sample->id));
                if (ImGui::Selectable(label.c_str(), session.pinnedFrameId == sample->id,
                                      ImGuiSelectableFlags_SpanAllColumns))
                {
                    session.pinnedFrameId = sample->id;
                    requestedTab = 0;
                }
                ItemTooltip("Pin this frame and open it in Overview.");
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kBadColor, "%.3f", sample->frameMs);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.3f", sample->cpuTotalMs);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.3f", sample->gpuTotalMs);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(kWarnColor, "+%.3f", sample->frameMs - budget);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Trace recording");
        ItemTooltip("Record streamed CPU/GPU scope timelines for Perfetto or chrome://tracing.");
        ImGui::TextColored(session.traceRecording ? kBadColor : session.traceFull ? kWarnColor
                                                                                  : kGoodColor,
                           session.traceRecording ? "RECORDING" : session.traceFull ? "LIMIT REACHED"
                                                                                    : "IDLE");
        ImGui::SameLine();
        ImGui::TextDisabled("%llu detailed packets   |   %zu / %zu events",
                            static_cast<unsigned long long>(session.tracePackets), session.traceEvents.size(),
                            kMaxTraceEvents);
        ItemTooltip("Trace events are bounded in memory. Recording stops before an incomplete packet would exceed the limit.");
        if (ImGui::Button(session.traceRecording ? "Stop trace" : "Start new trace"))
        {
            if (session.traceRecording)
                session.traceRecording = false;
            else
                session.StartTrace();
        }
        ItemTooltip(session.traceRecording ? "Stop recording and keep the captured events for export."
                                           : "Clear the previous trace and record detailed snapshots at the selected Sample refresh rate.");
        ImGui::SameLine();
        if (ImGui::Button("Export Chrome trace"))
            notice = SaveChromeTrace(session);
        ItemTooltip("Write Chrome Trace Event JSON for Perfetto or chrome://tracing.");
        ImGui::SameLine();
        if (ImGui::Button("Discard trace"))
        {
            session.traceEvents.clear();
            session.tracePackets = 0;
            session.traceRecording = false;
            session.traceFull = false;
            notice = "Trace discarded";
        }
        ItemTooltip("Stop recording and discard all currently captured trace events.");
        ImGui::TextDisabled("Trace detail follows Sample refresh; use Per frame for continuous frame-by-frame tracing.");
        ItemTooltip("Lower refresh rates sample detailed scope frames sparsely while retaining low profiler overhead.");

        ImGui::SeparatorText("Capture and export");
        ItemTooltip("Write the current detailed snapshot or retained frame history to disk.");
        if (ImGui::Button("Save current snapshot"))
            notice = SaveSnapshot(session);
        ItemTooltip("Save the latest raw profiler snapshot as timestamped JSON.");
        ImGui::SameLine();
        if (ImGui::Button("Export frame history CSV"))
            notice = SaveSessionCsv(session);
        ItemTooltip("Export retained per-frame timing summaries as CSV for external analysis.");
        ImGui::SameLine();
        if (ImGui::Button("Reset session"))
        {
            session.Reset();
            notice = "Session cleared";
        }
        ItemTooltip("Clear retained frames, rolling scope statistics, counter history, and the pinned frame.");
        if (!notice.empty())
            ImGui::TextColored(kAccent, "%s", notice.c_str());
        ImGui::TextDisabled("Captures are written to ProfilerCaptures beside the profiler working directory.");
        ItemTooltip("Relative output folder used by both JSON and CSV capture actions.");
    }

    void DrawStatusBadge(bool connected, bool paused, const std::string &status)
    {
        const ImVec4 color = paused ? kWarnColor : connected ? kGoodColor
                                                             : kBadColor;
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cursor.x + 5.f, cursor.y + lineHeight * 0.5f), 4.f, ImGui::GetColorU32(color));
        ImGui::Dummy(ImVec2(10.f, lineHeight));
        ImGui::SameLine();
        ImGui::TextUnformatted(paused ? "PAUSED" : status.c_str());
        ItemTooltip(paused      ? "Display updates are paused; the connection remains open. Press Space or Resume to continue."
                    : connected ? "Live profiler data is arriving from the Player."
                                : "The viewer is waiting for or reconnecting to the Player profiler stream.");
    }
} // namespace

int main(int argc, char *argv[])
{
    char host[64] = "127.0.0.1";
    int port = pe::ProfilerStreamServer::kDefaultPort;
    if (argc >= 2 && argv[1][0] != '-')
        std::snprintf(host, sizeof(host), "%s", argv[1]);
    if (argc >= 3 && argv[2][0] != '-')
        port = std::atoi(argv[2]);
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc)
            std::snprintf(host, sizeof(host), "%s", argv[++i]);
        else if (std::strncmp(argv[i], "--host=", 7) == 0)
            std::snprintf(host, sizeof(host), "%s", argv[i] + 7);
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (std::strncmp(argv[i], "--port=", 7) == 0)
            port = std::atoi(argv[i] + 7);
    }
    port = std::clamp(port, 1, 65535);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Phasma Profiler",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          1280,
                                          820,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowMinimumSize(window, 900, 620);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyProfilerStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    SDL_Texture *fontTexture = nullptr;
    if (!CreateFontTexture(renderer, fontTexture))
    {
        std::fprintf(stderr, "font texture failed\n");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    pe::ProfilerStreamClient client;
    SessionData session;
    std::string status = "Connecting";
    std::string notice;
    double reconnectAt = 0.0;
    bool autoConnect = true;
    bool paused = false;
    bool showFrame = true;
    bool showCpu = true;
    bool showGpu = true;
    int targetFpsIndex = 1;
    constexpr std::array<int, 6> targetFpsValues = {30, 60, 90, 120, 144, 240};
    int refreshRateIndex = 0;
    bool refreshRateDirty = true;
    constexpr std::array<pe::ProfilerRefreshRate, 5> refreshRates = {
        pe::ProfilerRefreshRate::Hz4,
        pe::ProfilerRefreshRate::Hz10,
        pe::ProfilerRefreshRate::Hz30,
        pe::ProfilerRefreshRate::Hz60,
        pe::ProfilerRefreshRate::PerFrame,
    };
    int renderDocFrameCount = 1;
    int requestedTab = -1;
    int cpuView = 0;
    int gpuView = 0;
    int selectedCpu = -1;
    int selectedGpu = -1;
    float cpuZoomH = 1.f;
    float cpuZoomV = 1.f;
    float gpuZoomH = 1.f;
    float gpuZoomV = 1.f;
    char cpuFilter[96] = {};
    char gpuFilter[96] = {};
    char counterFilter[96] = {};

    bool running = true;
    while (running)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE &&
                !ImGui::GetIO().WantTextInput)
                paused = !paused;
        }

        const double now = SDL_GetTicks64() / 1000.0;
        if (autoConnect && !client.IsConnected() && now >= reconnectAt)
        {
            if (client.Connect(host, port))
            {
                status = "LIVE";
                refreshRateDirty = true;
            }
            else
            {
                status = "WAITING FOR PLAYER";
                reconnectAt = now + 1.0;
            }
        }

        if (client.IsConnected())
        {
            if (refreshRateDirty && client.SetRefreshRate(refreshRates[refreshRateIndex]))
                refreshRateDirty = false;

            std::string json;
            while (client.TryRecvFrame(json))
            {
                LiveFrame frame;
                if (!paused && ParseFrame(json, frame))
                    session.Accept(std::move(frame), std::move(json));
            }
            if (!client.IsConnected())
            {
                status = "RECONNECTING";
                reconnectAt = now + 0.5;
            }
        }

        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Phasma Profiler", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextColored(kAccent, "PHASMA");
        ImGui::SameLine();
        ImGui::TextUnformatted("PROFILER");
        ImGui::SameLine();
        ImGui::TextDisabled("live game performance");
        ImGui::SameLine(0.f, 22.f);
        DrawStatusBadge(client.IsConnected(), paused, status);
        ImGui::SameLine();
        ImGui::TextDisabled("%s:%d", host, port);
        ItemTooltip("Player profiler stream address. The default is localhost port 9876.");
        if (session.traceRecording)
        {
            ImGui::SameLine();
            ImGui::TextColored(kBadColor, "REC");
            ItemTooltip("Chrome trace recording is active. Stop or export it from the Session tab.");
        }

        const float controlsWidth = 620.f;
        if (ImGui::GetContentRegionAvail().x > controlsWidth)
            ImGui::SameLine(ImGui::GetWindowWidth() - controlsWidth);
        const int targetFps = targetFpsValues[targetFpsIndex];
        constexpr const char *budgetTooltip =
            "Visualization target only: controls budget lines, timing bars, and heat colors.\n"
            "It does not cap Player FPS or change profiler sampling.";
        ImGui::TextDisabled("Budget");
        ItemTooltip(budgetTooltip);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85.f);
        const char *fpsLabels[] = {"30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS", "240 FPS"};
        ImGui::Combo("##target_fps", &targetFpsIndex, fpsLabels, IM_ARRAYSIZE(fpsLabels));
        ItemTooltip(budgetTooltip);
        ImGui::SameLine();
        constexpr const char *refreshTooltip =
            "Controls how often the Player builds and sends detailed scope, counter, memory, and GPU snapshots.\n"
            "Rendered-frame summaries are retained for every frame at every setting.\n"
            "Per frame provides maximum detail with the highest profiling overhead.";
        ImGui::TextDisabled("Sample refresh");
        ItemTooltip(refreshTooltip);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.f);
        const char *refreshLabels[] = {"4 Hz", "10 Hz", "30 Hz", "60 Hz", "Per frame"};
        if (ImGui::Combo("##refresh_rate", &refreshRateIndex, refreshLabels, IM_ARRAYSIZE(refreshLabels)))
            refreshRateDirty = true;
        ItemTooltip(refreshTooltip);
        ImGui::SameLine();
        if (ImGui::Button(paused ? "Resume  [Space]" : "Pause  [Space]"))
            paused = !paused;
        ItemTooltip(paused ? "Resume accepting live snapshots into the dashboard and session history."
                           : "Freeze dashboard/session updates while keeping the Player connection open. Space is the shortcut.");
        ImGui::SameLine();
        if (ImGui::Button("Save JSON"))
            notice = SaveSnapshot(session);
        ItemTooltip("Save the latest raw detailed snapshot as timestamped JSON in ProfilerCaptures.");
        ImGui::Separator();

        DrawMetrics(session.live, targetFps);
        ImGui::Spacing();

        const int pendingTab = requestedTab;
        if (ImGui::BeginTabBar("##profiler_tabs", ImGuiTabBarFlags_None))
        {
            const char *tabNames[] = {"Overview", "CPU", "GPU", "Counters", "Session"};
            const char *tabTooltips[] = {
                "Live frame graph, frame/hitch navigation, selected-frame summary, composition, memory pressure, and current hotspots.",
                "CPU scope hierarchy, rolling statistics, timeline, and aggregated inclusive/self timings.",
                "GPU timestamp hierarchy, rolling statistics, timeline, and aggregated inclusive/self timings.",
                "Current PE_PROFILE_COUNTER values and their recent trends.",
                "Connection controls, retained-frame statistics, histogram, ranked budget misses, capture, and export.",
            };
            for (int tab = 0; tab < IM_ARRAYSIZE(tabNames); ++tab)
            {
                const ImGuiTabItemFlags flags = requestedTab == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                const bool tabOpen = ImGui::BeginTabItem(tabNames[tab], nullptr, flags);
                ItemTooltip(tabTooltips[tab]);
                if (!tabOpen)
                    continue;
                if (tab == 0)
                    DrawOverview(session, targetFps, requestedTab, selectedCpu, selectedGpu, showFrame, showCpu, showGpu);
                else if (tab == 1)
                    DrawTimingTab("CPU", session.live.cpu, session.cpuStats,
                                  std::max(session.live.cpuScopeTotalMs, session.live.cpuTotalMs), cpuFilter,
                                  sizeof(cpuFilter), cpuView, cpuZoomH, cpuZoomV, selectedCpu);
                else if (tab == 2)
                    DrawTimingTab("GPU", session.live.gpu, session.gpuStats, session.live.gpuTotalMs, gpuFilter,
                                  sizeof(gpuFilter), gpuView, gpuZoomH, gpuZoomV, selectedGpu);
                else if (tab == 3)
                    DrawCounters(session, counterFilter, sizeof(counterFilter));
                else
                {
                    bool requestReconnect = false;
                    DrawSessionTab(session, targetFps, host, port, client.IsConnected(), client,
                                   renderDocFrameCount, autoConnect, requestReconnect, requestedTab, notice);
                    if (!autoConnect && client.IsConnected())
                    {
                        client.Disconnect();
                        status = "DISCONNECTED";
                    }
                    if (requestReconnect)
                    {
                        client.Disconnect();
                        reconnectAt = 0.0;
                        status = "CONNECTING";
                    }
                }
                ImGui::EndTabItem();
            }
            if (requestedTab == pendingTab)
                requestedTab = -1;
            ImGui::EndTabBar();
        }

        if (!session.hasData && !client.IsConnected())
        {
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::SetNextWindowBgAlpha(0.96f);
            ImGui::Begin("Waiting", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
            ImGui::TextColored(kAccent, "Waiting for PhasmaPlayer");
            ImGui::TextDisabled("Launch a game with --profiler or enable Live profiler in PhasmaLauncher.");
            ImGui::Spacing();
            ImGui::Text("Endpoint  %s:%d", host, port);
            ImGui::End();
        }

        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 11, 13, 17, 255);
        SDL_RenderClear(renderer);
        RenderImGuiDrawData(renderer, ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    client.Disconnect();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (fontTexture)
        SDL_DestroyTexture(fontTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
