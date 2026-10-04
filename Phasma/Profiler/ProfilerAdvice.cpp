#include "ProfilerAdvice.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace pe
{
    namespace
    {
        // ponytail: ~4 min of frames at 500 fps (33 at 60) and the last 256 spike-like worst frames;
        // longer studies use the JSONL captures.
        constexpr size_t kMaxFrames = 120000;
        constexpr size_t kMaxWorstFrames = 256;
        // A spike takes at least twice its local baseline (the median of the last 64 frames) and
        // this much more: visible stutter even when every frame fits the budget.
        constexpr double kSpikeMarginMs = 2.0;
        // A slowdown: at least this much slower than its local baseline but not a spike. Many of them
        // can cost more time than the spikes do.
        constexpr double kSlowdownRatio = 1.25;
        constexpr size_t kBaselineFrames = 64;
        // Spikes are counted over the last minute of frames, judged only with at least 30 s of them,
        // and two or more a minute are recurring.
        constexpr double kSpikeWindowMs = 60000, kMinSpikeWindowMs = 30000, kRecurringPerMinute = 2;
        // The session timeline: 10 s rows; Jev reads at most 60 of them, merged.
        constexpr double kBucketMs = 10000;
        constexpr size_t kJevRows = 60;
        // ponytail: ~27 h of spikes at 30 a minute; past it a session keeps its timeline rows only.
        constexpr size_t kMaxEvents = 50000;
        constexpr const char *kSideNames[] = {"gpu", "cpu", "wait", "other"};

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

        float ClampedMs(const rapidjson::Value &object, const char *key)
        {
            return static_cast<float>(std::clamp(Number(object, key), 0.0, 60000.0));
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

        double Round(double value)
        {
            return std::round(value * 1000.0) / 1000.0;
        }

        std::string Ms(double value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f ms", value);
            return text;
        }
    } // namespace

    void ProfilerAdvice::Reset()
    {
        m_samples.clear();
        m_steadyCpu.clear();
        m_steadyGpu.clear();
        m_frames.clear();
        m_recent.clear();
        m_worst.clear();
        m_hitch.reset();
    }

    // Scopes that ran in under half the frames but still cost time, by total time; never waits.
    std::vector<std::pair<const std::string *, const ProfilerAdvice::Total *>> ProfilerAdvice::Intermittent(
        const std::unordered_map<std::string, Total> &costs, unsigned frames)
    {
        std::vector<std::pair<const std::string *, const Total *>> out;
        for (const auto &[name, total] : costs)
            if (2 * total.frames < frames && total.ms >= 0.01 * frames && !IsWait(name))
                out.emplace_back(&name, &total);
        std::sort(out.begin(), out.end(), [](const auto &l, const auto &r)
                  { return l.second->ms > r.second->ms; });
        return out;
    }

    void ProfilerAdvice::ResetSession()
    {
        Reset();
        m_timeline.clear();
        m_events.clear();
        m_sessionStartMs = 0;
        m_contextChanged = false;
    }

    bool ProfilerAdvice::IsSpike(const FrameTimes &times, const FrameTimes &base)
    {
        return times.frameMs > std::max(2.0 * base.frameMs, base.frameMs + kSpikeMarginMs);
    }

    // ponytail: the engine's wait scopes by name; a new blocking scope needs adding here.
    bool ProfilerAdvice::IsWait(std::string_view scope)
    {
        return scope.find("Wait") != std::string_view::npos || scope.find("Present") != std::string_view::npos ||
               scope == "Acquire Image" || scope == "Queue Submit";
    }

    // The timing that holds at least half of the extra time; GPU first, because CPU totals include
    // waits on the GPU.
    ProfilerAdvice::Side ProfilerAdvice::SideOf(const FrameTimes &times, double gpuMs, const FrameTimes &base)
    {
        const double excess = times.frameMs - base.frameMs;
        if (gpuMs - base.gpuMs >= 0.5 * excess)
            return Gpu;
        if (times.cpuMs - base.cpuMs >= 0.5 * excess)
            return Cpu;
        return Other;
    }

    // The deepest scope/pass holding at least half of the spike's extra time against its steady
    // median, else the one holding the most; the share tells one culprit from cost spread over many.
    ProfilerAdvice::SpikeSource ProfilerAdvice::Attribute(const FrameTimes &times, const FrameTimes &base, const Costs &cpu,
                                                          const Costs &gpu) const
    {
        SpikeSource source;
        source.side = SideOf(times, times.gpuMs, base);
        source.excessMs = times.frameMs - base.frameMs;
        // Right after a reset the steady medians hold a sample or two: too noisy to name a culprit.
        if (source.side == Other || m_steadyCpu.size() < 4)
            return source;
        const bool onGpu = source.side == Gpu;
        const double sideExcess = onGpu ? times.gpuMs - base.gpuMs : times.cpuMs - base.cpuMs;
        const std::string *half = nullptr, *most = nullptr;
        double halfExcess = 0, mostExcess = 0;
        uint32_t halfDepth = 0;
        for (const auto &[name, cost] : onGpu ? gpu : cpu)
        {
            // Command-buffer roots span the whole GPU frame; a scope's excess is at most its time.
            if ((onGpu && cost.depth == 0) || (cost.ms <= mostExcess && cost.ms < 0.5 * sideExcess))
                continue;
            std::vector<double> steady;
            for (const Costs &costs : onGpu ? m_steadyGpu : m_steadyCpu)
            {
                const auto found = costs.find(name);
                steady.push_back(found == costs.end() ? 0.0 : found->second.ms);
            }
            const double excess = cost.ms - Median(std::move(steady));
            if (excess > mostExcess)
            {
                most = &name;
                mostExcess = excess;
            }
            if (excess >= 0.5 * sideExcess && (!half || cost.depth > halfDepth || (cost.depth == halfDepth && excess > halfExcess)))
            {
                half = &name;
                halfExcess = excess;
                halfDepth = cost.depth;
            }
        }
        if (half || most)
        {
            source.name = half ? *half : *most;
            source.excessMs = half ? halfExcess : mostExcess;
            source.share = std::min(1.0, source.excessMs / sideExcess);
            // CPU time spent blocked on the GPU, driver or presentation: the cause is elsewhere.
            if (!onGpu && IsWait(source.name))
                source.side = Wait;
        }
        return source;
    }

    bool ProfilerAdvice::AddSnapshot(std::string_view json)
    {
        rapidjson::Document doc;
        if (json.size() <= 16 * 1024 * 1024)
            doc.Parse(json.data(), json.size());
        if (doc.HasParseError() || !doc.IsObject())
        {
            ResetSession();
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
            ResetSession();
            m_context.clear();
            m_session.clear();
            m_status = "This capture lacks valid v0.1 context/timing metadata. Capture again with the updated engine.";
            return true;
        }

        const std::string contextJson = Json(context);
        const std::string sessionId(session.GetString(), session.GetStringLength());
        if (sessionId != m_session)
            ResetSession();
        else if (contextJson != m_context)
        {
            Reset();
            m_contextChanged = HasSession();
        }
        else if (!m_samples.empty() && (timestamp < m_samples.back().timestampMs || timestamp - m_samples.back().timestampMs > 2000))
            Reset();
        m_context = contextJson;
        m_session = sessionId;

        auto readCosts = [](const rapidjson::Value &scopes)
        {
            Costs costs;
            if (!scopes.IsArray())
                return costs;
            // Scopes arrive in begin order with their depth, so the open parents form a stack.
            std::vector<std::pair<uint32_t, ScopeCost *>> parents;
            for (const auto &scope : scopes.GetArray())
            {
                const auto &name = Member(scope, "name");
                const double depth = Number(scope, "depth", -1);
                if (!name.IsString() || depth < 0 || depth > 64)
                    continue;
                const float ms = ClampedMs(scope, "cur_ms");
                const auto level = static_cast<uint32_t>(depth);
                auto [it, inserted] = costs.try_emplace(std::string(name.GetString(), name.GetStringLength()));
                ScopeCost &cost = it->second;
                cost.depth = inserted ? level : std::min(cost.depth, level);
                cost.ms += ms;
                cost.selfMs += ms;
                while (!parents.empty() && parents.back().first >= level)
                    parents.pop_back();
                if (!parents.empty() && parents.back().first + 1 == level)
                    parents.back().second->selfMs -= ms;
                parents.emplace_back(level, &cost);
            }
            return costs;
        };

        // The first packet of a window carries the connect warm-up frame; frame and spike evidence
        // starts with the second packet.
        if (!m_samples.empty())
        {
            const auto &history = Member(doc, "frame_history");
            std::vector<FrameTimes> packet;
            if (history.IsArray())
                for (const auto &entry : history.GetArray())
                {
                    const double ms = Number(entry, "frame_ms");
                    if (ms > 0 && ms <= 60000)
                        packet.push_back({static_cast<float>(ms), ClampedMs(entry, "cpu_total_ms"), ClampedMs(entry, "gpu_total_ms")});
                }
            for (const FrameTimes &times : packet)
            {
                m_recent.push_back(times);
                if (m_recent.size() > kBaselineFrames)
                    m_recent.pop_front();
            }
            // A packet's frames are judged against the median of the last 64 frames, so a fight that
            // slowly gets heavier is not counted as spikes while a hitch still is.
            auto baseline = [this](float FrameTimes::*field)
            {
                std::vector<double> values;
                for (const FrameTimes &times : m_recent)
                    values.push_back(times.*field);
                return static_cast<float>(Median(std::move(values)));
            };
            const FrameTimes base{baseline(&FrameTimes::frameMs), baseline(&FrameTimes::cpuMs), baseline(&FrameTimes::gpuMs)};
            for (const FrameTimes &times : packet)
                m_frames.push_back({times, base});
            while (m_frames.size() > kMaxFrames)
                m_frames.pop_front();
            // A spiking worst frame is attributed once, now, and both the window and the timeline reuse it.
            const auto &worst = Member(doc, "worst_frame");
            const float worstMs = static_cast<float>(Number(worst, "frame_ms"));
            const FrameTimes worstTimes{worstMs, ClampedMs(worst, "cpu_total_ms"), ClampedMs(worst, "gpu_total_ms")};
            const bool attributed = worstMs > 0 && worstMs <= 60000 && !m_recent.empty() && IsSpike(worstTimes, base);
            if (attributed)
            {
                m_worst.push_back({worstTimes, base, timestamp,
                                   Attribute(worstTimes, base, readCosts(Member(worst, "scopes")), readCosts(Member(worst, "passes")))});
                if (m_worst.size() > kMaxWorstFrames)
                    m_worst.pop_front();
            }

            // The session timeline: every frame into its 10 s row, each spike as an event.
            double packetMs = 0; // the packet's frames end at its timestamp
            for (const FrameTimes &times : packet)
                packetMs += times.frameMs;
            if (m_sessionStartMs <= 0)
                m_sessionStartMs = timestamp - packetMs;
            const auto row = static_cast<size_t>(std::max(0.0, timestamp - m_sessionStartMs) / kBucketMs);
            if (m_timeline.size() <= row)
                m_timeline.resize(row + 1);
            Bucket &bucket = m_timeline[row];
            bucket.contextChanged |= std::exchange(m_contextChanged, false);
            double after = packetMs; // frame time between a frame and the packet's publish
            const auto &totals = Member(doc, "totals");
            if (totals.IsObject())
            {
                bucket.costFrames += static_cast<unsigned>(std::clamp(Number(totals, "frames"), 0.0, 1e6));
                bucket.gpuCostFrames += static_cast<unsigned>(std::clamp(Number(totals, "gpu_frames"), 0.0, 1e6));
                for (const auto &[key, costs] : {std::pair{"cpu", &bucket.cpuCost}, std::pair{"gpu", &bucket.gpuCost}})
                {
                    const auto &list = Member(totals, key);
                    if (!list.IsArray())
                        continue;
                    for (const auto &entry : list.GetArray())
                    {
                        const auto &name = Member(entry, "name");
                        if (!name.IsString())
                            continue;
                        Total &total = (*costs)[std::string(name.GetString(), name.GetStringLength())];
                        total.ms += std::clamp(Number(entry, "ms"), 0.0, 1e9);
                        total.sq += std::clamp(Number(entry, "sq"), 0.0, 1e12);
                        total.frames += static_cast<unsigned>(std::clamp(Number(entry, "frames"), 0.0, 1e6));
                    }
                }
            }
            for (size_t i = 0; i < packet.size(); ++i)
            {
                const FrameTimes &times = packet[i];
                after -= times.frameMs;
                ++bucket.hist[std::min(static_cast<size_t>(kHistBins - 1), static_cast<size_t>(times.frameMs / kHistBinMs))];
                ++bucket.frames;
                bucket.maxMs = std::max(bucket.maxMs, times.frameMs);
                bucket.frameMsSum += times.frameMs;
                if (!IsSpike(times, base))
                {
                    m_hitch.reset();
                    continue;
                }
                // A GPU timestamp can land one frame before or after the frame it slowed.
                float gpuMs = times.gpuMs;
                if (i > 0)
                    gpuMs = std::max(gpuMs, packet[i - 1].gpuMs);
                if (i + 1 < packet.size())
                    gpuMs = std::max(gpuMs, packet[i + 1].gpuMs);
                const Side side = SideOf(times, gpuMs, base);
                if (!m_hitch)
                {
                    // Consecutive spike frames are one hitch, timed and sided by its slowest frame.
                    m_hitch = Hitch{row, SIZE_MAX, times.frameMs, side};
                    ++bucket.spikes[side];
                    if (m_events.size() < kMaxEvents)
                    {
                        m_hitch->event = m_events.size();
                        m_events.push_back({timestamp - m_sessionStartMs - after, times.frameMs, base.frameMs, side, {}, 0, 0, 0});
                    }
                }
                else if (times.frameMs > m_hitch->peakMs)
                {
                    --m_timeline[m_hitch->row].spikes[m_hitch->side];
                    ++m_timeline[m_hitch->row].spikes[side];
                    m_hitch->peakMs = times.frameMs;
                    m_hitch->side = side;
                    if (m_hitch->event != SIZE_MAX)
                    {
                        SpikeEvent &event = m_events[m_hitch->event];
                        event.frameMs = times.frameMs;
                        event.baseMs = base.frameMs;
                        event.side = side;
                    }
                }
                if (m_hitch->event != SIZE_MAX)
                {
                    ++m_events[m_hitch->event].frames;
                    m_events[m_hitch->event].durationMs += times.frameMs;
                }
                if (attributed && !m_hitch->sourced && times.frameMs == worstMs)
                {
                    m_hitch->sourced = true;
                    const SpikeSource &source = m_worst.back().source;
                    if (m_hitch->event != SIZE_MAX)
                    {
                        m_events[m_hitch->event].source = source.name;
                        m_events[m_hitch->event].share = static_cast<float>(source.share);
                    }
                    if (source.name != "unattributed")
                        ++m_timeline[m_hitch->row].sources[source.name];
                }
            }
        }
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
            Reset();
            m_status = "Invalid metric range; collect a fresh measurement window.";
            return true;
        }
        m_samples.push_back(sample);
        m_steadyCpu.push_back(readCosts(Member(Member(doc, "cpu"), "scopes")));
        m_steadyGpu.push_back(readCosts(passes));
        if (m_samples.size() > 40)
        {
            m_samples.pop_front();
            m_steadyCpu.pop_front();
            m_steadyGpu.pop_front();
        }
        m_status = "Collect at least eight snapshots over one second with unchanged settings.";
        return true;
    }

    std::string ProfilerAdvice::Analyze(int targetFps, bool full) const
    {
        rapidjson::Document report(rapidjson::kObjectType);
        auto &a = report.GetAllocator();
        auto text = [&a](const std::string &value)
        { return rapidjson::Value(value.data(), static_cast<rapidjson::SizeType>(value.size()), a); };
        report.AddMember("schema_version", 1, a);
        report.AddMember("advisor", "Phasma AI v0.2", a);
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
        const double budget = 1000.0 / targetFps;

        // The whole session in time order. Rows of `per` 10 s buckets merged; percentiles come from
        // 0.25 ms histogram bins.
        auto rows = [&](size_t per)
        {
            rapidjson::Value list(rapidjson::kArrayType);
            for (size_t start = 0; start < m_timeline.size(); start += per)
            {
                Bucket merged;
                for (size_t i = start; i < std::min(start + per, m_timeline.size()); ++i)
                {
                    const Bucket &b = m_timeline[i];
                    for (int bin = 0; bin < kHistBins; ++bin)
                        merged.hist[bin] += b.hist[bin];
                    merged.frames += b.frames;
                    merged.maxMs = std::max(merged.maxMs, b.maxMs);
                    merged.costFrames += b.costFrames;
                    for (const auto &[name, total] : b.cpuCost)
                    {
                        Total &sum = merged.cpuCost[name];
                        sum.ms += total.ms;
                        sum.frames += total.frames;
                    }
                    for (int side = Gpu; side <= Other; ++side)
                        merged.spikes[side] += b.spikes[side];
                    for (const auto &[name, count] : b.sources)
                        merged.sources[name] += count;
                    merged.contextChanged |= b.contextChanged;
                }
                if (merged.frames == 0)
                    continue;
                auto percentile = [&merged](double p)
                {
                    double seen = 0;
                    for (int bin = 0; bin < kHistBins; ++bin)
                        if ((seen += merged.hist[bin]) >= p * merged.frames && merged.hist[bin])
                            return bin == kHistBins - 1 ? static_cast<double>(merged.maxMs) : (bin + 0.5) * kHistBinMs;
                    return static_cast<double>(merged.maxMs);
                };
                unsigned overBudget = 0;
                for (int bin = 0; bin < kHistBins; ++bin)
                    if (bin * kHistBinMs >= budget)
                        overBudget += merged.hist[bin];
                rapidjson::Value item(rapidjson::kObjectType);
                item.AddMember("t_s", Round(start * kBucketMs / 1000.0), a);
                item.AddMember("frames", merged.frames, a);
                item.AddMember("median_ms", Round(percentile(0.5)), a);
                item.AddMember("p99_ms", Round(percentile(0.99)), a);
                item.AddMember("max_ms", Round(merged.maxMs), a);
                item.AddMember("over_budget", overBudget, a);
                item.AddMember("spikes", merged.spikes[Gpu] + merged.spikes[Cpu] + merged.spikes[Wait] + merged.spikes[Other], a);
                item.AddMember("gpu_spikes", merged.spikes[Gpu], a);
                item.AddMember("cpu_spikes", merged.spikes[Cpu], a);
                const auto top = std::max_element(merged.sources.begin(), merged.sources.end(), [](const auto &l, const auto &r)
                                                  { return l.second < r.second; });
                if (top != merged.sources.end())
                {
                    item.AddMember("top_source", text(top->first), a);
                    item.AddMember("top_source_spikes", top->second, a);
                }
                if (const auto top = Intermittent(merged.cpuCost, merged.costFrames); !top.empty())
                {
                    item.AddMember("top_intermittent", text(*top.front().first), a);
                    item.AddMember("top_intermittent_ms_per_frame", Round(top.front().second->ms / merged.costFrames), a);
                }
                if (merged.contextChanged)
                    item.AddMember("settings_changed", true, a);
                list.PushBack(item, a);
            }
            return list;
        };
        // Where all the time went over rows [firstRow, end): each scope's or pass's average time per
        // frame across every frame, its share of the frame and how many frames it ran in.
        auto costJson = [&](size_t firstRow)
        {
            std::unordered_map<std::string, Total> cpu, gpu;
            unsigned frames = 0, gpuFrames = 0, streamed = 0;
            double streamedMs = 0;
            for (size_t i = firstRow; i < m_timeline.size(); ++i)
            {
                const Bucket &b = m_timeline[i];
                frames += b.costFrames;
                gpuFrames += b.gpuCostFrames;
                streamed += b.frames;
                streamedMs += b.frameMsSum;
                for (const auto &[from, to] : {std::pair{&b.cpuCost, &cpu}, std::pair{&b.gpuCost, &gpu}})
                    for (const auto &[name, total] : *from)
                    {
                        Total &sum = (*to)[name];
                        sum.ms += total.ms;
                        sum.sq += total.sq;
                        sum.frames += total.frames;
                    }
            }
            const double meanFrameMs = streamed ? streamedMs / streamed : 0;
            auto list = [&](std::vector<std::pair<const std::string *, const Total *>> ranked, unsigned n, size_t limit)
            {
                rapidjson::Value out(rapidjson::kArrayType);
                for (size_t i = 0; i < std::min(limit, ranked.size()); ++i)
                {
                    const auto &[name, total] = ranked[i];
                    const double perFrame = total->ms / n;
                    rapidjson::Value item(rapidjson::kObjectType);
                    item.AddMember("name", text(*name), a);
                    item.AddMember("ms_per_frame", Round(perFrame), a);
                    item.AddMember("share_pct", Round(meanFrameMs > 0 ? 100.0 * perFrame / meanFrameMs : 0.0), a);
                    item.AddMember("in_frames_pct", Round(100.0 * total->frames / n), a);
                    if (IsWait(*name))
                        item.AddMember("wait", true, a);
                    out.PushBack(item, a);
                }
                return out;
            };
            auto ranked = [](const std::unordered_map<std::string, Total> &totals, bool roots)
            {
                std::vector<std::pair<const std::string *, const Total *>> out;
                for (const auto &[name, total] : totals)
                    if (roots || !name.starts_with("CommandBuffer"))
                        out.emplace_back(&name, &total);
                std::sort(out.begin(), out.end(), [](const auto &l, const auto &r)
                          { return l.second->ms > r.second->ms; });
                return out;
            };
            // How much each varies frame to frame (standard deviation over every frame, zeros included):
            // the work behind slowdowns too small to be spikes.
            auto variation = [&](const std::unordered_map<std::string, Total> &totals, unsigned n, size_t limit)
            {
                std::vector<std::pair<double, const std::string *>> ranked;
                for (const auto &[name, total] : totals)
                    if (n > 0 && !name.starts_with("CommandBuffer"))
                        ranked.emplace_back(std::sqrt(std::max(0.0, total.sq / n - (total.ms / n) * (total.ms / n))), &name);
                std::sort(ranked.begin(), ranked.end(), [](const auto &l, const auto &r)
                          { return l.first > r.first; });
                rapidjson::Value out(rapidjson::kArrayType);
                for (size_t i = 0; i < std::min(limit, ranked.size()) && ranked[i].first >= 0.01; ++i)
                {
                    rapidjson::Value item(rapidjson::kObjectType);
                    item.AddMember("name", text(*ranked[i].second), a);
                    item.AddMember("sd_ms", Round(ranked[i].first), a);
                    item.AddMember("ms_per_frame", Round(totals.at(*ranked[i].second).ms / n), a);
                    if (IsWait(*ranked[i].second))
                        item.AddMember("wait", true, a);
                    out.PushBack(item, a);
                }
                return out;
            };
            rapidjson::Value value(rapidjson::kObjectType);
            value.AddMember("frames", frames, a);
            value.AddMember("cpu_variation", variation(cpu, frames, 6), a);
            if (gpuFrames > 0)
                value.AddMember("gpu_variation", variation(gpu, gpuFrames, 4), a);
            value.AddMember("cpu", list(ranked(cpu, true), frames, 8), a);
            value.AddMember("intermittent", list(Intermittent(cpu, frames), frames, 5), a);
            if (gpuFrames > 0)
                value.AddMember("gpu", list(ranked(gpu, false), gpuFrames, 6), a);
            return value;
        };
        const bool haveCosts = std::any_of(m_timeline.begin(), m_timeline.end(), [](const Bucket &b)
                                           { return b.costFrames > 0; });
        // Trend: the spike rate of the session's second half against its first, by row time.
        double halfRate[2] = {};
        const char *trend = "insufficient";
        if (m_timeline.size() >= 6)
        {
            const size_t mid = m_timeline.size() / 2;
            for (size_t i = 0; i < m_timeline.size(); ++i)
                halfRate[i >= mid] += m_timeline[i].spikes[Gpu] + m_timeline[i].spikes[Cpu] + m_timeline[i].spikes[Other];
            halfRate[0] *= 60000.0 / (mid * kBucketMs);
            halfRate[1] *= 60000.0 / ((m_timeline.size() - mid) * kBucketMs);
            trend = halfRate[1] >= halfRate[0] + kRecurringPerMinute && halfRate[1] >= 1.5 * halfRate[0]   ? "rising"
                    : halfRate[0] >= halfRate[1] + kRecurringPerMinute && halfRate[0] >= 1.5 * halfRate[1] ? "falling"
                                                                                                           : "steady";
        }
        if (HasSession())
        {
            rapidjson::Value session(rapidjson::kObjectType);
            session.AddMember("start_unix_ms", m_sessionStartMs, a);
            session.AddMember("duration_s", Round(m_timeline.size() * kBucketMs / 1000.0), a);
            session.AddMember("trend", rapidjson::StringRef(trend), a);
            session.AddMember("first_half_spikes_per_minute", Round(halfRate[0]), a);
            session.AddMember("second_half_spikes_per_minute", Round(halfRate[1]), a);
            const size_t per = (m_timeline.size() + kJevRows - 1) / kJevRows;
            session.AddMember("row_s", Round(per * kBucketMs / 1000.0), a);
            session.AddMember("summary", rows(per), a);
            if (haveCosts)
                session.AddMember("costs", costJson(0), a);
            if (full)
            {
                session.AddMember("timeline", rows(1), a);
                rapidjson::Value events(rapidjson::kArrayType);
                for (const SpikeEvent &e : m_events)
                {
                    rapidjson::Value item(rapidjson::kObjectType);
                    item.AddMember("t_s", Round(e.atMs / 1000.0), a);
                    item.AddMember("frame_ms", Round(e.frameMs), a);
                    item.AddMember("base_ms", Round(e.baseMs), a);
                    item.AddMember("side", rapidjson::StringRef(kSideNames[e.side]), a);
                    item.AddMember("frames", e.frames, a);
                    item.AddMember("duration_ms", Round(e.durationMs), a);
                    if (!e.source.empty())
                    {
                        item.AddMember("source", text(e.source), a);
                        item.AddMember("share", std::round(e.share * 100.0) / 100.0, a);
                    }
                    events.PushBack(item, a);
                }
                session.AddMember("spike_events", events, a);
            }
            report.AddMember("session", session, a);
        }
        if (m_samples.size() < 8 || duration < 1000)
            return finish(m_status);

        auto median = [&](double Sample::*field)
        {
            std::vector<double> values;
            for (const Sample &sample : m_samples)
                values.push_back(sample.*field);
            return Median(std::move(values));
        };
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

        // Every streamed frame of the window, not only the sampled snapshots.
        struct Stats
        {
            double avg = 0, median = 0, p95 = 0, p99 = 0, max = 0;
        };
        auto stats = [&](float FrameTimes::*field)
        {
            Stats s;
            std::vector<double> values;
            values.reserve(m_frames.size());
            for (const StreamFrame &frame : m_frames)
                values.push_back(frame.times.*field);
            if (values.empty())
                return s;
            for (const double value : values)
                s.avg += value;
            s.avg /= static_cast<double>(values.size());
            auto at = [&values](double p)
            {
                const auto nth = values.begin() + static_cast<ptrdiff_t>(p * static_cast<double>(values.size() - 1) + 0.5);
                std::nth_element(values.begin(), nth, values.end());
                return *nth;
            };
            s.median = at(0.5);
            s.p95 = at(0.95);
            s.p99 = at(0.99);
            s.max = *std::max_element(values.begin(), values.end());
            return s;
        };
        auto statsJson = [&](const Stats &s)
        {
            rapidjson::Value value(rapidjson::kObjectType);
            value.AddMember("avg_ms", Round(s.avg), a);
            value.AddMember("median_ms", Round(s.median), a);
            value.AddMember("p95_ms", Round(s.p95), a);
            value.AddMember("p99_ms", Round(s.p99), a);
            value.AddMember("max_ms", Round(s.max), a);
            return value;
        };
        const Stats frameStats = stats(&FrameTimes::frameMs);
        const Stats cpuStats = stats(&FrameTimes::cpuMs);
        const Stats gpuStats = stats(&FrameTimes::gpuMs);
        double elapsed = 0;
        unsigned overBudget = 0;
        for (const StreamFrame &frame : m_frames)
        {
            elapsed += frame.times.frameMs;
            overBudget += frame.times.frameMs > budget;
        }
        rapidjson::Value frames(rapidjson::kObjectType);
        frames.AddMember("count", static_cast<unsigned>(m_frames.size()), a);
        frames.AddMember("duration_ms", Round(elapsed), a);
        frames.AddMember("over_budget", overBudget, a);
        frames.AddMember("frame", statsJson(frameStats), a);
        frames.AddMember("cpu", statsJson(cpuStats), a);
        frames.AddMember("gpu", statsJson(gpuStats), a);
        evidence.AddMember("frames", frames, a);

        // Spikes over the last minute of frames, each against its local baseline.
        size_t first = m_frames.size();
        double windowMs = 0;
        while (first > 0 && windowMs < kSpikeWindowMs)
            windowMs += m_frames[--first].times.frameMs;
        // Hitches: consecutive spike frames count once, sided by their slowest frame.
        unsigned spikeCount = 0, spikeFrames = 0, slowFrames = 0;
        double spikeExtraMs = 0, slowExtraMs = 0;
        unsigned sideCounts[4] = {};
        double worstSpike = 0, clock = 0, lastSpike = -1;
        std::optional<std::pair<float, Side>> hitch; // the open hitch's peak frame and side
        std::vector<double> intervals, baselines;
        baselines.reserve(m_frames.size() - first);
        for (size_t i = first; i < m_frames.size(); ++i)
        {
            const auto &[times, base] = m_frames[i];
            clock += times.frameMs;
            baselines.push_back(base.frameMs);
            if (!IsSpike(times, base))
            {
                if (hitch)
                    ++sideCounts[hitch->second];
                hitch.reset();
                if (times.frameMs >= kSlowdownRatio * base.frameMs)
                {
                    ++slowFrames;
                    slowExtraMs += times.frameMs - base.frameMs;
                }
                continue;
            }
            ++spikeFrames;
            spikeExtraMs += times.frameMs - base.frameMs;
            worstSpike = std::max(worstSpike, static_cast<double>(times.frameMs));
            // A GPU timestamp can land one frame before or after the frame it slowed.
            float gpuMs = times.gpuMs;
            if (i > 0)
                gpuMs = std::max(gpuMs, m_frames[i - 1].times.gpuMs);
            if (i + 1 < m_frames.size())
                gpuMs = std::max(gpuMs, m_frames[i + 1].times.gpuMs);
            if (!hitch)
            {
                ++spikeCount;
                if (lastSpike >= 0)
                    intervals.push_back(clock - lastSpike);
                lastSpike = clock;
                hitch.emplace(times.frameMs, SideOf(times, gpuMs, base));
            }
            else if (times.frameMs > hitch->first)
                hitch.emplace(times.frameMs, SideOf(times, gpuMs, base));
        }
        if (hitch)
            ++sideCounts[hitch->second];
        const double perMinute = windowMs > 0 ? spikeCount * 60000.0 / windowMs : 0.0;
        const bool enoughFrames = windowMs >= kMinSpikeWindowMs;
        const char *verdict = !enoughFrames ? "insufficient" : spikeCount == 0                ? "smooth"
                                                           : perMinute >= kRecurringPerMinute ? "recurring"
                                                                                              : "occasional";

        // Where: the attributions made as the spike frames arrived.
        struct Source
        {
            std::string name;
            Side side;
            unsigned count = 0;
            double excessSum = 0;
            double excessMax = 0;
            double shareSum = 0;
        };
        std::vector<Source> sources;
        unsigned breakdowns = 0;
        for (const WorstFrame &worst : m_worst)
        {
            if (worst.timestampMs < m_samples.back().timestampMs - kSpikeWindowMs)
                continue;
            ++breakdowns;
            const SpikeSource &found = worst.source;
            auto it = std::find_if(sources.begin(), sources.end(), [&](const Source &s)
                                   { return s.side == found.side && s.name == found.name; });
            if (it == sources.end())
                it = sources.insert(sources.end(), Source{found.name, found.side});
            it->count++;
            it->excessSum += found.excessMs;
            it->excessMax = std::max(it->excessMax, found.excessMs);
            it->shareSum += found.share;
        }
        std::sort(sources.begin(), sources.end(), [](const Source &l, const Source &r)
                  { return l.count != r.count ? l.count > r.count : l.excessSum > r.excessSum; });
        if (sources.size() > 5)
            sources.resize(5);

        rapidjson::Value spikes(rapidjson::kObjectType);
        spikes.AddMember("verdict", rapidjson::StringRef(verdict), a);
        spikes.AddMember("window_s", Round(windowMs / 1000.0), a);
        spikes.AddMember("local_median_ms", Round(Median(std::move(baselines))), a);
        spikes.AddMember("count", spikeCount, a);
        spikes.AddMember("frames", spikeFrames, a);
        spikes.AddMember("extra_ms_per_minute", windowMs > 0 ? Round(spikeExtraMs * 60000.0 / windowMs) : 0.0, a);
        spikes.AddMember("per_minute", Round(perMinute), a);
        spikes.AddMember("worst_ms", Round(worstSpike), a);
        if (!intervals.empty())
            spikes.AddMember("median_interval_ms", Round(Median(intervals)), a);
        for (const Side side : {Gpu, Cpu, Other})
            spikes.AddMember(rapidjson::StringRef(kSideNames[side]), sideCounts[side], a);
        spikes.AddMember("with_breakdown", breakdowns, a);
        rapidjson::Value sourceList(rapidjson::kArrayType);
        for (const Source &source : sources)
        {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("name", text(source.name), a);
            item.AddMember("side", rapidjson::StringRef(kSideNames[source.side]), a);
            item.AddMember("count", source.count, a);
            item.AddMember("avg_excess_ms", Round(source.excessSum / source.count), a);
            item.AddMember("max_excess_ms", Round(source.excessMax), a);
            item.AddMember("avg_share", std::round(source.shareSum / source.count * 100.0) / 100.0, a);
            sourceList.PushBack(item, a);
        }
        spikes.AddMember("sources", sourceList, a);
        evidence.AddMember("spikes", spikes, a);
        rapidjson::Value slowdowns(rapidjson::kObjectType);
        slowdowns.AddMember("frames", slowFrames, a);
        slowdowns.AddMember("extra_ms_per_minute", windowMs > 0 ? Round(slowExtraMs * 60000.0 / windowMs) : 0.0, a);
        slowdowns.AddMember("share_pct", windowMs > 0 ? Round(100.0 * slowExtraMs / windowMs) : 0.0, a);
        evidence.AddMember("slowdowns", slowdowns, a);

        // Steady top costs: GPU passes by inclusive time, CPU scopes by self time.
        auto topList = [&](const std::deque<Costs> &window, float ScopeCost::*field, bool passes)
        {
            std::unordered_set<std::string> names;
            for (const Costs &costs : window)
                for (const auto &[name, cost] : costs)
                    if (!passes || cost.depth == 1)
                        names.insert(name);
            std::vector<std::pair<double, const std::string *>> ranked;
            for (const std::string &name : names)
            {
                std::vector<double> values;
                for (const Costs &costs : window)
                {
                    const auto found = costs.find(name);
                    values.push_back(found == costs.end() ? 0.0 : std::max(0.0f, found->second.*field));
                }
                ranked.emplace_back(Median(std::move(values)), &name);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto &l, const auto &r)
                      { return l.first > r.first; });
            rapidjson::Value list(rapidjson::kArrayType);
            for (size_t i = 0; i < ranked.size() && i < 5 && ranked[i].first > 0; ++i)
            {
                rapidjson::Value item(rapidjson::kObjectType);
                item.AddMember("name", text(*ranked[i].second), a);
                item.AddMember("median_ms", Round(ranked[i].first), a);
                list.PushBack(item, a);
            }
            return list;
        };
        evidence.AddMember("top_gpu_passes", topList(m_steadyGpu, &ScopeCost::ms, true), a);
        evidence.AddMember("top_cpu_scopes", topList(m_steadyCpu, &ScopeCost::selfMs, false), a);
        if (haveCosts) // the spike window's last minute, as 10 s rows
            evidence.AddMember("costs", costJson(m_timeline.size() > 6 ? m_timeline.size() - 6 : 0), a);
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
        const bool shadowsOn = Member(settings, "shadows").IsTrue();
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
            if (shadowsOn && shadow >= 1 && shadow / gpu >= 0.20 && shadowBias > 0 && shadowBias < 4)
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
        const Source *spikeSource = nullptr;
        for (const Source &source : sources)
            if (source.name != "unattributed" && source.side != Wait)
            {
                spikeSource = &source;
                break;
            }
        if (spikeSource && spikeSource->count >= 2)
            suggest("spike_source", "Investigate spike source: " + spikeSource->name,
                    std::to_string(spikeSource->count) + " of " + std::to_string(breakdowns) + " spike breakdowns spend +" +
                        Ms(spikeSource->excessSum / spikeSource->count) + " in " + spikeSource->name + " (" +
                        kSideNames[spikeSource->side] + "); " + std::to_string(spikeCount) + " spikes in the last " +
                        std::to_string(static_cast<int>(windowMs / 1000.0)) + " s.",
                    "Lowering quality settings rarely removes a recurring spike; fix or spread the work that causes it.",
                    "Record a trace across a spike, confirm the scope, then compare the spike rate after one change.");

        if (std::strcmp(trend, "rising") == 0)
            suggest("spike_trend", "Spikes rise over the session",
                    "Spikes went from " + std::to_string(static_cast<int>(halfRate[0] + 0.5)) + " a minute in the session's first half to " +
                        std::to_string(static_cast<int>(halfRate[1] + 0.5)) + " in its second.",
                    "A load that grows over time (more entities, effects or allocations) is not fixed by a setting tuned for the start.",
                    "Find the timeline rows where spikes jump, note what the game does then, and capture a trace there.");

        // The decision space for an external chooser (Jev): each change with the measured GPU time of
        // the passes it affects, which is a cost, not a predicted saving.
        auto passMs = [&](std::string_view prefix)
        {
            std::vector<double> values;
            bool seen = false;
            for (const Costs &costs : m_steadyGpu)
            {
                // A region and its nested *_pass both match; count the shallowest level only.
                uint32_t depth = UINT32_MAX;
                double sum = 0;
                for (const auto &[name, cost] : costs)
                {
                    if (!name.starts_with(prefix) || cost.depth > depth)
                        continue;
                    if (cost.depth < depth)
                    {
                        depth = cost.depth;
                        sum = 0;
                    }
                    sum += cost.ms;
                }
                seen |= depth != UINT32_MAX;
                values.push_back(sum);
            }
            return seen ? Median(std::move(values)) : -1.0;
        };
        rapidjson::Value options(rapidjson::kArrayType);
        auto option = [&](const std::string &id, const std::string &description, double affectedMs)
        {
            rapidjson::Value item(rapidjson::kObjectType);
            item.AddMember("id", text(id), a);
            item.AddMember("description", text(description), a);
            item.AddMember("affected_gpu_ms", affectedMs >= 0 ? rapidjson::Value(Round(affectedMs)) : rapidjson::Value(), a);
            options.PushBack(item, a);
        };
        struct Effect
        {
            const char *key, *passPrefix, *id, *impact;
        };
        constexpr Effect effects[] = {
            {"ssao", "SSAO", "disable_ssao", "flatter contact shading in corners and under objects"},
            {"ssr", "SSR", "disable_ssr", "glossy surfaces lose screen-space reflections"},
            {"taa", "TAA", "disable_taa", "edges alias and shimmer in motion"},
            {"fxaa", "FXAA", "disable_fxaa", "edges lose FXAA smoothing"},
            {"cas_sharpening", "RCAS", "disable_sharpening", "slightly softer image"},
            {"bloom", "Bloom", "disable_bloom", "bright lights lose their glow"},
            {"dof", "DOF", "disable_dof", "no depth-of-field blur"},
            {"motion_blur", "MotionBlur", "disable_motion_blur", "no motion blur"},
        };
        for (const Effect &effect : effects)
            if (Member(settings, effect.key).IsTrue())
                option(effect.id, std::string("Disable ") + effect.key + ": " + effect.impact + ".", passMs(effect.passPrefix));
        char line[384];
        if (scale > 0.5)
        {
            std::snprintf(line, sizeof(line), "Render scale %.2f -> %.2f: softer image and less fine detail; every per-pixel pass gets cheaper.",
                          scale, std::max(0.5, scale * 0.9));
            option("lower_render_scale", line, gpu);
        }
        if (shadowsOn)
        {
            const double shadowMs = passMs("ShadowPass");
            const double mapSize = Number(settings, "shadow_map_size");
            if (mapSize > 512)
            {
                std::snprintf(line, sizeof(line), "Shadow map %.0f -> %.0f: blurrier, more aliased shadow edges; recompiles lighting shaders when applied.",
                              mapSize, mapSize / 2);
                option("halve_shadow_map", line, shadowMs);
            }
            const double cascades = Number(settings, "num_cascades");
            if (cascades > 1)
            {
                std::snprintf(line, sizeof(line), "Shadow cascades %.0f -> %.0f: lower shadow resolution at mid distance.", cascades, cascades - 1);
                option("fewer_cascades", line, shadowMs);
            }
            const double distance = Number(settings, "shadow_distance");
            if (distance > 0)
            {
                std::snprintf(line, sizeof(line), "Shadow distance %.0f -> %.0f: distant objects lose shadows.", distance, distance * 0.75);
                option("shorter_shadow_distance", line, shadowMs);
            }
            if (shadowBias > 0 && shadowBias < 4)
            {
                std::snprintf(line, sizeof(line), "Shadow LOD bias %.2f -> %.2f: simpler distant shadow silhouettes; needs lower mesh LODs.",
                              shadowBias, std::min(4.0, shadowBias * 1.5));
                option("raise_shadow_lod_bias", line, shadowMs);
            }
        }
        const double geometryMs = passMs("DepthPass") < 0 && passMs("GbufferOpaquePass") < 0
                                      ? -1.0
                                      : std::max(0.0, passMs("DepthPass")) + std::max(0.0, passMs("GbufferOpaquePass"));
        if (Member(settings, "lod_enabled").IsFalse())
            option("enable_mesh_lod", "Enable mesh LODs: distant meshes switch to lower detail (needs authored LODs).", geometryMs);
        for (const auto &[key, id] : {std::pair{"frustum_culling", "enable_frustum_culling"}, std::pair{"occlusion_culling", "enable_occlusion_culling"}})
            if (Member(settings, key).IsFalse())
                option(id, std::string("Enable ") + key + ": no visual change; skips objects outside the view or hidden behind others.", geometryMs);
        if (gpuValid && frame > budget * 1.05 && gpu <= budget)
        {
            std::snprintf(line, sizeof(line), "Keep quality and fix the CPU or presentation wait: GPU work (%.2f ms) already fits the %.2f ms budget, so lower GPU settings cannot reach it.",
                          gpu, budget);
            option("fix_cpu_or_present_wait", line, -1.0);
        }
        int spikeOption = 0;
        for (const Source &source : sources)
        {
            // A source seen in one spike is not recurring: it stays in the evidence, not the options.
            if (source.name == "unattributed" || source.side == Wait || source.count < 2 || spikeOption == 3)
                continue;
            // The total it loses ranks sources; the script scope is the whole game, not one system.
            std::snprintf(line, sizeof(line), "Keep settings and fix the recurring spike source '%s' (%s): %.1f ms lost a minute, in %u of %u spike breakdowns, +%.2f ms each, %.0f%% of their extra time%s",
                          source.name.c_str(), kSideNames[source.side], source.excessSum * 60000.0 / windowMs, source.count, breakdowns,
                          source.excessSum / source.count, 100.0 * source.shareSum / source.count,
                          source.name == "Script System" ? "; this is the game's own code: scopes inside the game would name the part." : ".");
            option("fix_spike_" + std::to_string(++spikeOption), line, source.side == Gpu ? source.excessSum / source.count : -1.0);
        }
        // Work that runs in only some frames but adds up: the cost a frame-by-frame view misses.
        if (report["evidence"].HasMember("costs"))
        {
            int costOption = 0;
            for (const auto &item : report["evidence"]["costs"]["intermittent"].GetArray())
            {
                if (item["share_pct"].GetDouble() < 3.0 || costOption == 2)
                    continue;
                std::snprintf(line, sizeof(line), "Reduce '%s': %.2f ms per frame on average (%.0f%% of frame time) though it runs in only %.0f%% of frames: small costs that add up.",
                              item["name"].GetString(), item["ms_per_frame"].GetDouble(), item["share_pct"].GetDouble(), item["in_frames_pct"].GetDouble());
                option("reduce_cost_" + std::to_string(++costOption), line, -1.0);
            }
        }
        if (report["evidence"].HasMember("costs") && slowExtraMs > spikeExtraMs && slowExtraMs >= 0.01 * windowMs)
        {
            std::string variable;
            for (const auto &item : report["evidence"]["costs"]["cpu_variation"].GetArray())
                if (!item.HasMember("wait") && variable.size() < 120)
                    variable += (variable.empty() ? "" : ", ") + std::string(item["name"].GetString()) + " +/-" + Ms(item["sd_ms"].GetDouble());
            std::snprintf(line, sizeof(line), "Steady the frame-to-frame variation: frames %.2gx-2x slower than usual lose %.0f ms a minute (%.1f%% of frame time), more than spikes (%.0f ms). Most variable: %s.",
                          kSlowdownRatio, slowExtraMs * 60000.0 / windowMs, 100.0 * slowExtraMs / windowMs, spikeExtraMs * 60000.0 / windowMs,
                          variable.empty() ? "no single CPU scope" : variable.c_str());
            option("smooth_slowdowns", line, -1.0);
        }
        // Recurring spikes with a repeating source are measured, not a judgement call: keeping everything
        // is then not offered. Scattered hitches with no common source leave it, with what they cost.
        if (std::strcmp(verdict, "recurring") != 0 || spikeOption == 0)
        {
            std::snprintf(line, sizeof(line), "Keep current settings: hitches lose %.0f ms and slowdowns %.0f ms a minute (%.2f%% of frame time)%s",
                          windowMs > 0 ? spikeExtraMs * 60000.0 / windowMs : 0.0, windowMs > 0 ? slowExtraMs * 60000.0 / windowMs : 0.0,
                          windowMs > 0 ? 100.0 * (spikeExtraMs + slowExtraMs) / windowMs : 0.0,
                          spikeOption == 0 ? " and no spike source repeats." : ".");
            option("no_change", line, -1.0);
        }
        report.AddMember("options", options, a);
        report.AddMember("jev_ready", enoughFrames, a);

        report.AddMember("limitations", "Rules use sampled, asynchronous GPU timings and a short stable-context window. Spike attribution uses the slowest frame of each stream packet, so spikes inside one packet share a breakdown, and all scripts share the 'Script System' scope. Suggestions require an A/B test; scene content and camera motion are not controlled by the advisor.", a);
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
        std::puts(advice.Analyze(targetFps, true).c_str());
        return 0;
    }
} // namespace pe
