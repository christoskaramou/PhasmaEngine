#include "AnimationTimeline.h"
#include "Animation/AnimationEvaluator.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Scene/ModelAsset.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    std::string AnimationTimeline::FormatTime(float ticks, float ticksPerSecond)
    {
        if (ticksPerSecond <= 0.f)
            ticksPerSecond = 1.f;
        float seconds = ticks / ticksPerSecond;
        int mins = static_cast<int>(seconds) / 60;
        float secs = seconds - mins * 60.f;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%06.3f", mins, secs);
        return buf;
    }

    float AnimationTimeline::TicksToPixel(float ticks, float trackWidth) const
    {
        float visibleDuration = m_clipDuration / m_zoomLevel;
        if (visibleDuration <= 0.f)
            return 0.f;
        return ((ticks - m_visibleStartTicks) / visibleDuration) * trackWidth;
    }

    float AnimationTimeline::PixelToTicks(float px, float trackWidth) const
    {
        float visibleDuration = m_clipDuration / m_zoomLevel;
        if (trackWidth <= 0.f)
            return 0.f;
        return (px / trackWidth) * visibleDuration + m_visibleStartTicks;
    }

    void AnimationTimeline::CollectAnimatedNodes(Scene &scene, AnimationSystem *anim, NodeId *root)
    {
        if (!scene.IsNodeAlive(root))
            return;

        if (anim->GetAnimationState(root))
            m_animatedNodes.push_back(root);

        const auto &children = scene.GetChildren(root);
        for (NodeId *child : children)
            CollectAnimatedNodes(scene, anim, child);
    }

    // -------------------------------------------------------------------------
    // Phase 2: keyframe editing helpers
    // -------------------------------------------------------------------------
    bool AnimationTimeline::IsKeySelected(const KeyRef &ref) const
    {
        for (const auto &k : m_selectedKeys)
            if (k == ref)
                return true;
        return false;
    }

    void AnimationTimeline::SelectKey(const KeyRef &ref, bool additive)
    {
        if (!additive)
            m_selectedKeys.clear();

        if (!IsKeySelected(ref))
            m_selectedKeys.push_back(ref);
    }

    void AnimationTimeline::SortChannelKeys(AnimationChannel &chan)
    {
        std::sort(chan.positionKeys.begin(), chan.positionKeys.end(),
                  [](const PositionKey &a, const PositionKey &b)
                  { return a.time < b.time; });
        std::sort(chan.rotationKeys.begin(), chan.rotationKeys.end(),
                  [](const RotationKey &a, const RotationKey &b)
                  { return a.time < b.time; });
        std::sort(chan.scaleKeys.begin(), chan.scaleKeys.end(),
                  [](const ScaleKey &a, const ScaleKey &b)
                  { return a.time < b.time; });
    }

    void AnimationTimeline::DeleteSelectedKeys(AnimationClip &clip)
    {
        // Process deletions per channel; work backwards on indices to preserve order
        struct ToDel
        {
            int ci;
            KeyType type;
            int ki;
        };
        std::vector<ToDel> dels;
        for (const auto &ref : m_selectedKeys)
            dels.push_back({ref.channelIdx, ref.type, ref.keyIdx});

        // Sort descending by keyIdx so erasing from back to front is safe per channel/type
        std::sort(dels.begin(), dels.end(), [](const ToDel &a, const ToDel &b)
                  {
                      if (a.ci != b.ci)
                          return a.ci > b.ci;
                      if (a.type != b.type)
                          return a.type > b.type;
                      return a.ki > b.ki; });

        for (const auto &d : dels)
        {
            if (d.ci < 0 || d.ci >= static_cast<int>(clip.channels.size()))
                continue;
            auto &chan = clip.channels[d.ci];
            auto erase = [&](auto &vec)
            {
                if (d.ki >= 0 && d.ki < static_cast<int>(vec.size()))
                    vec.erase(vec.begin() + d.ki);
            };
            switch (d.type)
            {
            case KeyType::Position:
                erase(chan.positionKeys);
                break;
            case KeyType::Rotation:
                erase(chan.rotationKeys);
                break;
            case KeyType::Scale:
                erase(chan.scaleKeys);
                break;
            }
        }
        m_selectedKeys.clear();
    }

    void AnimationTimeline::CopySelectedKeys(const AnimationClip &clip)
    {
        m_clipboard.clear();
        if (m_selectedKeys.empty())
            return;

        // Find earliest time in selection for relative offset calculation
        float earliest = 1e30f;
        for (const auto &ref : m_selectedKeys)
        {
            if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            const auto &chan = clip.channels[ref.channelIdx];
            float t = 0.f;
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                    t = chan.positionKeys[ref.keyIdx].time;
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                    t = chan.rotationKeys[ref.keyIdx].time;
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                    t = chan.scaleKeys[ref.keyIdx].time;
                break;
            }
            earliest = std::min(earliest, t);
        }

        for (const auto &ref : m_selectedKeys)
        {
            if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            const auto &chan = clip.channels[ref.channelIdx];
            ClipboardEntry entry;
            entry.type = ref.type;
            entry.channelIdx = ref.channelIdx;
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                {
                    entry.relTime = chan.positionKeys[ref.keyIdx].time - earliest;
                    entry.posValue = chan.positionKeys[ref.keyIdx].value;
                    m_clipboard.push_back(entry);
                }
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                {
                    entry.relTime = chan.rotationKeys[ref.keyIdx].time - earliest;
                    entry.rotValue = chan.rotationKeys[ref.keyIdx].value;
                    m_clipboard.push_back(entry);
                }
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                {
                    entry.relTime = chan.scaleKeys[ref.keyIdx].time - earliest;
                    entry.sclValue = chan.scaleKeys[ref.keyIdx].value;
                    m_clipboard.push_back(entry);
                }
                break;
            }
        }
    }

    void AnimationTimeline::PasteKeys(AnimationClip &clip, float insertAtTicks)
    {
        m_selectedKeys.clear();
        for (const auto &entry : m_clipboard)
        {
            if (entry.channelIdx < 0 || entry.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            auto &chan = clip.channels[entry.channelIdx];
            float newTime = std::clamp(insertAtTicks + entry.relTime, 0.f, clip.duration);
            switch (entry.type)
            {
            case KeyType::Position:
            {
                int ki = static_cast<int>(chan.positionKeys.size());
                chan.positionKeys.push_back({newTime, entry.posValue});
                SortChannelKeys(chan);
                // Find new index after sort
                for (int i = 0; i < static_cast<int>(chan.positionKeys.size()); ++i)
                    if (chan.positionKeys[i].time == newTime && chan.positionKeys[i].value == entry.posValue)
                    {
                        m_selectedKeys.push_back({entry.channelIdx, KeyType::Position, i});
                        break;
                    }
                break;
            }
            case KeyType::Rotation:
            {
                chan.rotationKeys.push_back({newTime, entry.rotValue});
                SortChannelKeys(chan);
                for (int i = 0; i < static_cast<int>(chan.rotationKeys.size()); ++i)
                    if (chan.rotationKeys[i].time == newTime)
                    {
                        m_selectedKeys.push_back({entry.channelIdx, KeyType::Rotation, i});
                        break;
                    }
                break;
            }
            case KeyType::Scale:
            {
                chan.scaleKeys.push_back({newTime, entry.sclValue});
                SortChannelKeys(chan);
                for (int i = 0; i < static_cast<int>(chan.scaleKeys.size()); ++i)
                    if (chan.scaleKeys[i].time == newTime && chan.scaleKeys[i].value == entry.sclValue)
                    {
                        m_selectedKeys.push_back({entry.channelIdx, KeyType::Scale, i});
                        break;
                    }
                break;
            }
            }
        }
    }

    void AnimationTimeline::InsertKeyframesAtTime(AnimationClip &clip, const Skeleton &skeleton,
                                                  float timeTicks)
    {
        if (m_selectedBone < 0 || m_selectedBone >= static_cast<int>(skeleton.bones.size()))
            return;

        // Find (or create) the channel for this bone
        int chanIdx = -1;
        for (int i = 0; i < static_cast<int>(clip.channels.size()); ++i)
        {
            if (clip.channels[i].boneIndex == m_selectedBone)
            {
                chanIdx = i;
                break;
            }
        }
        if (chanIdx < 0)
        {
            AnimationChannel newChan;
            newChan.boneIndex = m_selectedBone;
            clip.channels.push_back(newChan);
            chanIdx = static_cast<int>(clip.channels.size()) - 1;
        }
        auto &chan = clip.channels[chanIdx];

        // Sample current values at this time using AnimationEvaluator
        vec3 pos = AnimationEvaluator::InterpolatePosition(chan.positionKeys, timeTicks);
        quat rot = AnimationEvaluator::InterpolateRotation(chan.rotationKeys, timeTicks);
        vec3 scl = AnimationEvaluator::InterpolateScale(chan.scaleKeys, timeTicks);

        chan.positionKeys.push_back({timeTicks, pos});
        chan.rotationKeys.push_back({timeTicks, rot});
        chan.scaleKeys.push_back({timeTicks, scl});
        SortChannelKeys(chan);
    }

    void AnimationTimeline::ReevaluatePose(Scene &scene, AnimationSystem *anim,
                                           const AnimationClip &clip, float currentTimeTicks)
    {
        (void)clip;
        ForEachAnimatedNode([&](NodeId *node)
                            { anim->SetPlaybackTime(scene, node, currentTimeTicks); });
    }

    // -------------------------------------------------------------------------
    // Transport bar
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawTransportBar(Scene &scene, AnimationSystem *anim,
                                             const AnimationClip &clip)
    {
        NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
        const auto *state = primary ? anim->GetAnimationState(primary) : nullptr;
        bool isPlaying = state && state->playing;
        float currentTime = state ? state->time : 0.f;
        float speed = state ? state->speed : 1.f;
        bool loop = state ? state->loop : true;

        if (ImGui::Button(isPlaying ? "||" : ">", {28, 0}))
        {
            if (isPlaying)
            {
                ForEachAnimatedNode([&](NodeId *n)
                                    { anim->SetPaused(n, true); });
            }
            else
            {
                ForEachAnimatedNode([&](NodeId *n)
                                    {
                    const auto *s = anim->GetAnimationState(n);
                    if (!s || s->clipIndex != m_selectedClip)
                        anim->PlayAnimation(scene, n, m_selectedClip, loop);
                    else
                        anim->SetPaused(n, false); });
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(isPlaying ? "Pause" : "Play");

        ImGui::SameLine();
        if (ImGui::Button("[]", {28, 0}))
        {
            ForEachAnimatedNode([&](NodeId *n)
                                {
                anim->StopAnimation(n);
                anim->SetPlaybackTime(scene, n, 0.f); });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stop");

        ImGui::SameLine();
        bool loopVal = loop;
        if (ImGui::Checkbox("Loop", &loopVal))
            ForEachAnimatedNode([&](NodeId *n)
                                { anim->SetLoop(n, loopVal); });

        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.f);
        static constexpr const char *speedLabels[] = {"0.25x", "0.5x", "1x", "2x", "4x"};
        static constexpr float speedValues[] = {0.25f, 0.5f, 1.f, 2.f, 4.f};
        int speedIdx = 2;
        for (int i = 0; i < 5; ++i)
            if (std::abs(speed - speedValues[i]) < 0.01f)
            {
                speedIdx = i;
                break;
            }
        if (ImGui::Combo("##speed", &speedIdx, speedLabels, 5))
            ForEachAnimatedNode([&](NodeId *n)
                                { anim->SetSpeed(n, speedValues[speedIdx]); });

        ImGui::SameLine();
        std::string timeStr = FormatTime(currentTime, clip.ticksPerSecond) + " / " +
                              FormatTime(clip.duration, clip.ticksPerSecond);
        ImGui::Text("%s", timeStr.c_str());

        // View mode toggle (right-aligned)
        float toggleW = 130.f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - toggleW + ImGui::GetCursorPosX());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f, 2.f});
        if (ImGui::RadioButton("Dope", !m_curveMode))
            m_curveMode = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Curves", m_curveMode))
        {
            m_curveMode = true;
            m_curveFitDirty = true;
        }
        ImGui::PopStyleVar();
    }

    // -------------------------------------------------------------------------
    // Track view (Phase 1 + Phase 2 keyframe editing)
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawTrackView(Scene &scene, AnimationSystem *anim,
                                          const Skeleton &skeleton, AnimationClip &clip,
                                          float currentTimeTicks)
    {
        if (m_curveMode)
        {
            DrawCurveView(scene, anim, skeleton, clip, currentTimeTicks);
            return;
        }

        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - kStatusHeight;
        if (availH < 20.f)
            return;

        float trackWidth = availW - kBoneListWidth - 8.f;
        if (trackWidth < 50.f)
            trackWidth = 50.f;

        // Content height includes ruler spacer so bone list + track area stay in sync
        float totalContentH =
            static_cast<float>(skeleton.bones.size()) * kTrackRowHeight + kRulerHeight;

        // ----------------------------------------------------------------
        // Left: bone list — synchronized scroll (with ruler-height spacer)
        // ----------------------------------------------------------------
        ImGui::BeginChild("##bone_list", {kBoneListWidth, availH}, true);
        ImGui::SetScrollY(m_boneScrollY);

        // Ruler-height spacer keeps bone rows aligned with track rows
        ImGui::Dummy({kBoneListWidth - ImGui::GetStyle().WindowPadding.x * 2.f, kRulerHeight});

        for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
        {
            const auto &bone = skeleton.bones[i];
            int depth = 0;
            int parent = bone.parentIndex;
            while (parent >= 0 && depth < 10)
            {
                depth++;
                parent = skeleton.bones[parent].parentIndex;
            }
            if (depth > 0)
                ImGui::Indent(depth * 8.f);
            bool selected = (m_selectedBone == i);
            if (ImGui::Selectable(bone.name.c_str(), selected, 0, {0, kTrackRowHeight}))
                m_selectedBone = i;
            if (depth > 0)
                ImGui::Unindent(depth * 8.f);
        }
        m_boneScrollY = ImGui::GetScrollY();

        // Bone list ruler header overlay (fixed, not scrolled)
        {
            ImVec2 blPos = ImGui::GetWindowPos();
            ImVec2 blPad = ImGui::GetStyle().WindowPadding;
            ImDrawList *blDl = ImGui::GetWindowDrawList();
            float rl = blPos.x + blPad.x;
            float rt = blPos.y + blPad.y;
            float rr = blPos.x + kBoneListWidth - blPad.x;
            float rb = rt + kRulerHeight;
            blDl->AddRectFilled({rl, rt}, {rr, rb}, IM_COL32(32, 32, 32, 245));
            blDl->AddLine({rl, rb}, {rr, rb}, IM_COL32(255, 255, 255, 45));
            blDl->AddText({rl + 4.f, rt + 3.f}, IM_COL32(160, 160, 160, 200), "Bone");
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ----------------------------------------------------------------
        // Right: track area
        // ----------------------------------------------------------------
        ImGui::BeginChild("##track_area", {trackWidth, availH}, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetScrollY(m_boneScrollY);

        ImVec2 trackOrigin = ImGui::GetCursorScreenPos();
        ImVec2 trackSize = {trackWidth - ImGui::GetStyle().WindowPadding.x * 2,
                            std::max(availH, totalContentH)};
        ImDrawList *dl = ImGui::GetWindowDrawList();

        m_trackOrigin = trackOrigin;
        m_trackWidth = trackSize.x;

        // Zoom: Ctrl+scroll
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
            {
                float mouseX = ImGui::GetMousePos().x - trackOrigin.x;
                float mouseTimeBefore = PixelToTicks(mouseX, trackSize.x);
                m_zoomLevel = std::clamp(m_zoomLevel + wheel * 0.5f, kMinZoom, kMaxZoom);
                float mouseTimeAfter = PixelToTicks(mouseX, trackSize.x);
                m_visibleStartTicks += mouseTimeBefore - mouseTimeAfter;
            }
        }

        // Pan: middle-click drag
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            float dx = ImGui::GetIO().MouseDelta.x;
            float visibleDuration = m_clipDuration / m_zoomLevel;
            m_visibleStartTicks -= (dx / trackSize.x) * visibleDuration;
        }

        // Vertical scroll: mouse wheel (non-Ctrl)
        if (ImGui::IsWindowHovered() && !ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
                m_boneScrollY =
                    std::clamp(m_boneScrollY - wheel * kTrackRowHeight * 3.f, 0.f,
                               std::max(0.f, totalContentH - availH));
        }

        // Clamp visible range
        float visibleDuration = m_clipDuration / m_zoomLevel;
        m_visibleStartTicks = std::clamp(m_visibleStartTicks, 0.f,
                                         std::max(0.f, m_clipDuration - visibleDuration));

        // ----------------------------------------------------------------
        // Compute grid intervals (shared by grid lines + ruler)
        // ----------------------------------------------------------------
        float targetGridPx = 80.f;
        float gridIntervalSec =
            targetGridPx /
            (trackSize.x / (visibleDuration / m_ticksPerSecond + 0.001f) + 0.001f);
        static constexpr float niceIntervals[] = {0.05f, 0.1f, 0.25f, 0.5f, 1.f,
                                                  2.f, 5.f, 10.f, 30.f, 60.f};
        for (float ni : niceIntervals)
        {
            if (ni >= gridIntervalSec)
            {
                gridIntervalSec = ni;
                break;
            }
        }
        float gridIntervalTicks = gridIntervalSec * m_ticksPerSecond;

        // Background grid lines (start below ruler)
        float trackContentTop = trackOrigin.y + kRulerHeight;
        if (gridIntervalTicks > 0.f)
        {
            float startGrid =
                std::floor(m_visibleStartTicks / gridIntervalTicks) * gridIntervalTicks;
            for (float t = startGrid; t <= m_visibleStartTicks + visibleDuration;
                 t += gridIntervalTicks)
            {
                float px = TicksToPixel(t, trackSize.x);
                if (px >= 0.f && px <= trackSize.x)
                    dl->AddLine({trackOrigin.x + px, trackContentTop},
                                {trackOrigin.x + px, trackOrigin.y + trackSize.y},
                                IM_COL32(255, 255, 255, 30));
            }
        }

        // ----------------------------------------------------------------
        // Draw keyframe diamonds + collect hit areas for input
        // ----------------------------------------------------------------
        struct KeyHit
        {
            KeyRef ref;
            float screenX;
            float screenY;
        };
        std::vector<KeyHit> keyHits;

        ImU32 colPos = IM_COL32(220, 80, 80, 220);
        ImU32 colRot = IM_COL32(80, 200, 80, 220);
        ImU32 colScl = IM_COL32(80, 130, 220, 220);
        ImU32 colSel = IM_COL32(255, 220, 0, 255);

        constexpr float diamondSize = 4.5f; // larger for easier clicking

        for (int ci = 0; ci < static_cast<int>(clip.channels.size()); ++ci)
        {
            const auto &chan = clip.channels[ci];
            if (chan.boneIndex < 0 || chan.boneIndex >= static_cast<int>(skeleton.bones.size()))
                continue;

            // All rows offset by kRulerHeight so they start below the ruler
            float rowY = trackContentTop + chan.boneIndex * kTrackRowHeight;
            float rowCenterY = rowY + kTrackRowHeight * 0.5f;

            if (chan.boneIndex == m_selectedBone)
                dl->AddRectFilled({trackOrigin.x, rowY},
                                  {trackOrigin.x + trackSize.x, rowY + kTrackRowHeight},
                                  IM_COL32(100, 140, 200, 40));
            if (chan.boneIndex % 2 == 0)
                dl->AddRectFilled({trackOrigin.x, rowY},
                                  {trackOrigin.x + trackSize.x, rowY + kTrackRowHeight},
                                  IM_COL32(255, 255, 255, 8));

            // Row separator line
            dl->AddLine({trackOrigin.x, rowY + kTrackRowHeight - 1.f},
                        {trackOrigin.x + trackSize.x, rowY + kTrackRowHeight - 1.f},
                        IM_COL32(255, 255, 255, 18));

            // Track rail lines — a horizontal anchor line per visible channel
            if (m_showPosition && !chan.positionKeys.empty())
                dl->AddLine({trackOrigin.x, rowCenterY - 4.f},
                            {trackOrigin.x + trackSize.x, rowCenterY - 4.f},
                            IM_COL32(220, 80, 80, 55), 1.f);
            if (m_showRotation && !chan.rotationKeys.empty())
                dl->AddLine({trackOrigin.x, rowCenterY},
                            {trackOrigin.x + trackSize.x, rowCenterY},
                            IM_COL32(80, 200, 80, 55), 1.f);
            if (m_showScale && !chan.scaleKeys.empty())
                dl->AddLine({trackOrigin.x, rowCenterY + 4.f},
                            {trackOrigin.x + trackSize.x, rowCenterY + 4.f},
                            IM_COL32(80, 130, 220, 55), 1.f);

            if (m_showPosition)
            {
                for (int ki = 0; ki < static_cast<int>(chan.positionKeys.size()); ++ki)
                {
                    float t = chan.positionKeys[ki].time;
                    float px = TicksToPixel(t, trackSize.x);
                    if (px >= -5.f && px <= trackSize.x + 5.f)
                    {
                        KeyRef ref{ci, KeyType::Position, ki};
                        bool sel = IsKeySelected(ref);
                        float sy = rowCenterY - 4.f;
                        dl->AddNgonFilled({trackOrigin.x + px, sy}, diamondSize,
                                          sel ? colSel : colPos, 4);
                        keyHits.push_back({ref, trackOrigin.x + px, sy});
                    }
                }
            }

            if (m_showRotation)
            {
                for (int ki = 0; ki < static_cast<int>(chan.rotationKeys.size()); ++ki)
                {
                    float t = chan.rotationKeys[ki].time;
                    float px = TicksToPixel(t, trackSize.x);
                    if (px >= -5.f && px <= trackSize.x + 5.f)
                    {
                        KeyRef ref{ci, KeyType::Rotation, ki};
                        bool sel = IsKeySelected(ref);
                        float sy = rowCenterY;
                        dl->AddNgonFilled({trackOrigin.x + px, sy}, diamondSize,
                                          sel ? colSel : colRot, 4);
                        keyHits.push_back({ref, trackOrigin.x + px, sy});
                    }
                }
            }

            if (m_showScale)
            {
                for (int ki = 0; ki < static_cast<int>(chan.scaleKeys.size()); ++ki)
                {
                    float t = chan.scaleKeys[ki].time;
                    float px = TicksToPixel(t, trackSize.x);
                    if (px >= -5.f && px <= trackSize.x + 5.f)
                    {
                        KeyRef ref{ci, KeyType::Scale, ki};
                        bool sel = IsKeySelected(ref);
                        float sy = rowCenterY + 4.f;
                        dl->AddNgonFilled({trackOrigin.x + px, sy}, diamondSize,
                                          sel ? colSel : colScl, 4);
                        keyHits.push_back({ref, trackOrigin.x + px, sy});
                    }
                }
            }
        }

        // Playhead line (starts at ruler bottom, not track origin)
        float playheadPx = TicksToPixel(currentTimeTicks, trackSize.x);
        if (playheadPx >= 0.f && playheadPx <= trackSize.x)
        {
            float tx = trackOrigin.x + playheadPx;
            dl->AddLine({tx, trackContentTop}, {tx, trackOrigin.y + trackSize.y},
                        IM_COL32(255, 60, 60, 200), 2.f);
        }

        // ----------------------------------------------------------------
        // Ruler overlay — drawn last so it paints over track content at top
        // ----------------------------------------------------------------
        {
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winPad = ImGui::GetStyle().WindowPadding;
            float rl = winPos.x + winPad.x;
            float rt = winPos.y + winPad.y;
            float rr = rl + trackSize.x;
            float rb = rt + kRulerHeight;

            // Ruler background
            dl->AddRectFilled({rl, rt}, {rr, rb}, IM_COL32(32, 32, 32, 245));
            dl->AddLine({rl, rb}, {rr, rb}, IM_COL32(255, 255, 255, 45));

            // Ruler grid ticks + labels
            if (gridIntervalTicks > 0.f)
            {
                float startGrid =
                    std::floor(m_visibleStartTicks / gridIntervalTicks) * gridIntervalTicks;
                // Half-interval minor ticks
                float halfTicks = gridIntervalTicks * 0.5f;
                for (float t = startGrid - halfTicks;
                     t <= m_visibleStartTicks + visibleDuration; t += halfTicks)
                {
                    float px = TicksToPixel(t, trackSize.x);
                    if (px < 0.f || px > trackSize.x)
                        continue;
                    bool isMajor =
                        (std::fmod(std::abs(t - startGrid), gridIntervalTicks) < 0.001f * gridIntervalTicks);
                    float tickH = isMajor ? kRulerHeight * 0.55f : kRulerHeight * 0.3f;
                    dl->AddLine({rl + px, rb - tickH}, {rl + px, rb},
                                IM_COL32(255, 255, 255, isMajor ? 100 : 50));
                }
                // Major tick labels: frame number (top line) + seconds (bottom line)
                for (float t = startGrid; t <= m_visibleStartTicks + visibleDuration;
                     t += gridIntervalTicks)
                {
                    float px = TicksToPixel(t, trackSize.x);
                    if (px < 0.f || px > trackSize.x)
                        continue;
                    float lx = rl + px + 3.f;
                    // Top: frame number
                    char frameBuf[16];
                    snprintf(frameBuf, sizeof(frameBuf), "f%d",
                             static_cast<int>(std::round(t)));
                    dl->AddText({lx, rt + 2.f}, IM_COL32(220, 220, 220, 230), frameBuf);
                    // Bottom: seconds
                    float secs = (m_ticksPerSecond > 0.f) ? t / m_ticksPerSecond : 0.f;
                    char secBuf[16];
                    snprintf(secBuf, sizeof(secBuf), "%.2fs", secs);
                    dl->AddText({lx, rt + 16.f}, IM_COL32(140, 140, 140, 180), secBuf);
                }
            }

            // Playhead indicator in ruler: triangle + floating time label
            if (playheadPx >= 0.f && playheadPx <= trackSize.x)
            {
                float tx = rl + playheadPx;
                // Triangle handle
                dl->AddTriangleFilled({tx - 5.f, rt + 1.f}, {tx + 5.f, rt + 1.f},
                                      {tx, rb - 1.f}, IM_COL32(255, 60, 60, 220));
                // Time label with dark background
                // Playhead label: frame# top + seconds bottom, with dark background
                char phFrame[16], phSecs[16];
                snprintf(phFrame, sizeof(phFrame), "f%d",
                         static_cast<int>(std::round(currentTimeTicks)));
                float phS = (m_ticksPerSecond > 0.f) ? currentTimeTicks / m_ticksPerSecond : 0.f;
                snprintf(phSecs, sizeof(phSecs), "%.2fs", phS);
                ImVec2 sz1 = ImGui::CalcTextSize(phFrame);
                ImVec2 sz2 = ImGui::CalcTextSize(phSecs);
                float boxW = std::max(sz1.x, sz2.x) + 4.f;
                float lx = std::clamp(tx - boxW * 0.5f, rl, rr - boxW);
                dl->AddRectFilled({lx - 1.f, rt + 1.f}, {lx + boxW, rb - 1.f},
                                  IM_COL32(50, 10, 10, 210));
                dl->AddText({lx + (boxW - sz1.x) * 0.5f, rt + 2.f},
                            IM_COL32(255, 140, 140, 255), phFrame);
                dl->AddText({lx + (boxW - sz2.x) * 0.5f, rt + 16.f},
                            IM_COL32(220, 100, 100, 200), phSecs);
            }
        }

        // ----------------------------------------------------------------
        // Hit test: find which key (if any) the mouse is over
        // ----------------------------------------------------------------
        KeyRef hoveredKey{};
        ImVec2 mousePos = ImGui::GetMousePos();
        for (const auto &kh : keyHits)
        {
            if (std::abs(mousePos.x - kh.screenX) < kKeyHitRadius &&
                std::abs(mousePos.y - kh.screenY) < kKeyHitRadius)
            {
                hoveredKey = kh.ref;
                break;
            }
        }

        // ----------------------------------------------------------------
        // Input handling
        // ----------------------------------------------------------------
        bool windowHovered = ImGui::IsWindowHovered();

        if (windowHovered)
        {
            // Left-click
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (hoveredKey.IsValid())
                {
                    // Select / add to selection
                    SelectKey(hoveredKey, ImGui::GetIO().KeyShift);
                    // Begin drag
                    m_draggingKeys = true;
                    float mxTicks =
                        PixelToTicks(mousePos.x - trackOrigin.x, trackSize.x);
                    m_dragAnchorTicks = mxTicks;
                    m_dragOrigTimes.clear();
                    for (const auto &ref : m_selectedKeys)
                    {
                        if (ref.channelIdx < 0 ||
                            ref.channelIdx >= static_cast<int>(clip.channels.size()))
                        {
                            m_dragOrigTimes.push_back(0.f);
                            continue;
                        }
                        const auto &chan = clip.channels[ref.channelIdx];
                        float t = 0.f;
                        switch (ref.type)
                        {
                        case KeyType::Position:
                            if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                                t = chan.positionKeys[ref.keyIdx].time;
                            break;
                        case KeyType::Rotation:
                            if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                                t = chan.rotationKeys[ref.keyIdx].time;
                            break;
                        case KeyType::Scale:
                            if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                                t = chan.scaleKeys[ref.keyIdx].time;
                            break;
                        }
                        m_dragOrigTimes.push_back(t);
                    }
                    m_scrubbing = false; // don't scrub when clicking on a key
                }
                else
                {
                    if (!ImGui::GetIO().KeyShift)
                        m_selectedKeys.clear();
                    // Start box select or scrub
                    m_boxSelecting = true;
                    m_boxSelectStart = mousePos;
                    m_boxSelectEnd = mousePos;
                    m_scrubbing = true;
                }
            }

            // Double-click on empty area: insert keyframe at current time
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hoveredKey.IsValid())
            {
                float clickTimeTicks =
                    PixelToTicks(mousePos.x - trackOrigin.x, trackSize.x);
                clickTimeTicks = std::clamp(clickTimeTicks, 0.f, m_clipDuration);
                // Determine which bone row was clicked (offset by ruler height)
                int clickBone = static_cast<int>(
                    (mousePos.y - trackOrigin.y - kRulerHeight) / kTrackRowHeight);
                if (clickBone >= 0 && clickBone < static_cast<int>(skeleton.bones.size()))
                {
                    m_selectedBone = clickBone;
                    InsertKeyframesAtTime(clip, skeleton, clickTimeTicks);
                    ReevaluatePose(scene, anim, clip, currentTimeTicks);
                }
                m_boxSelecting = false;
                m_scrubbing = false;
            }

            // Right-click: context menu
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                if (hoveredKey.IsValid())
                {
                    if (!IsKeySelected(hoveredKey))
                        SelectKey(hoveredKey, false);
                    ImGui::OpenPopup("##keyContextMenu");
                }
            }
        }

        // Drag update
        if (m_draggingKeys && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !m_selectedKeys.empty())
        {
            float currentMxTicks = PixelToTicks(mousePos.x - trackOrigin.x, trackSize.x);
            float delta = currentMxTicks - m_dragAnchorTicks;

            for (int i = 0; i < static_cast<int>(m_selectedKeys.size()); ++i)
            {
                const auto &ref = m_selectedKeys[i];
                if (ref.channelIdx < 0 ||
                    ref.channelIdx >= static_cast<int>(clip.channels.size()))
                    continue;
                float origTime = (i < static_cast<int>(m_dragOrigTimes.size()))
                                     ? m_dragOrigTimes[i]
                                     : 0.f;
                float newTime = std::clamp(origTime + delta, 0.f, m_clipDuration);
                auto &chan = clip.channels[ref.channelIdx];
                switch (ref.type)
                {
                case KeyType::Position:
                    if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                        chan.positionKeys[ref.keyIdx].time = newTime;
                    break;
                case KeyType::Rotation:
                    if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                        chan.rotationKeys[ref.keyIdx].time = newTime;
                    break;
                case KeyType::Scale:
                    if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                        chan.scaleKeys[ref.keyIdx].time = newTime;
                    break;
                }
            }
        }

        // Drag release: sort channels and clear selection
        if (m_draggingKeys && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            // Sort all channels that had keys moved
            for (const auto &ref : m_selectedKeys)
            {
                if (ref.channelIdx >= 0 &&
                    ref.channelIdx < static_cast<int>(clip.channels.size()))
                    SortChannelKeys(clip.channels[ref.channelIdx]);
            }
            m_selectedKeys.clear();
            m_draggingKeys = false;
            ReevaluatePose(scene, anim, clip, currentTimeTicks);
        }

        // Box select update
        if (m_boxSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_boxSelectEnd = mousePos;

            // If the user hasn't moved much, treat as scrub not box select
            float dx = std::abs(m_boxSelectEnd.x - m_boxSelectStart.x);
            float dy = std::abs(m_boxSelectEnd.y - m_boxSelectStart.y);
            if (dx > 5.f || dy > 5.f)
            {
                m_scrubbing = false; // moved enough — do box select, not scrub

                float x0 = std::min(m_boxSelectStart.x, m_boxSelectEnd.x);
                float x1 = std::max(m_boxSelectStart.x, m_boxSelectEnd.x);
                float y0 = std::min(m_boxSelectStart.y, m_boxSelectEnd.y);
                float y1 = std::max(m_boxSelectStart.y, m_boxSelectEnd.y);
                dl->AddRect({x0, y0}, {x1, y1}, IM_COL32(255, 220, 0, 180), 0.f, 0,
                            1.f);
                dl->AddRectFilled({x0, y0}, {x1, y1}, IM_COL32(255, 220, 0, 20));

                // Select all keys inside the box
                if (!ImGui::GetIO().KeyShift)
                    m_selectedKeys.clear();
                for (const auto &kh : keyHits)
                {
                    if (kh.screenX >= x0 && kh.screenX <= x1 && kh.screenY >= y0 &&
                        kh.screenY <= y1)
                    {
                        if (!IsKeySelected(kh.ref))
                            m_selectedKeys.push_back(kh.ref);
                    }
                }
            }
        }

        if (m_boxSelecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_boxSelecting = false;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_scrubbing = false;

        // Context menu popup (must be in same child window)
        if (ImGui::BeginPopup("##keyContextMenu"))
        {
            int selCount = static_cast<int>(m_selectedKeys.size());
            ImGui::TextDisabled("%d key(s) selected", selCount);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del"))
            {
                DeleteSelectedKeys(clip);
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                CopySelectedKeys(clip);
                PasteKeys(clip, currentTimeTicks + m_ticksPerSecond * 0.1f);
                SortChannelKeys(clip.channels[0]); // sort all after paste
                for (auto &chan : clip.channels)
                    SortChannelKeys(chan);
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C"))
                CopySelectedKeys(clip);
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty()))
            {
                PasteKeys(clip, currentTimeTicks);
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }
            ImGui::EndPopup();
        }

        ImGui::Dummy({trackSize.x, totalContentH});
        m_boneScrollY = ImGui::GetScrollY();
        ImGui::EndChild();
    }

    // -------------------------------------------------------------------------
    // Curve editor (Phase 3a — read-only curve display)
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawCurveView(Scene &scene, AnimationSystem *anim,
                                          const Skeleton &skeleton, AnimationClip &clip,
                                          float currentTimeTicks)
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - kStatusHeight;
        if (availH < 20.f)
            return;

        float panelWidth = availW - kBoneListWidth - 8.f;
        if (panelWidth < 50.f)
            panelWidth = 50.f;

        float totalContentH =
            static_cast<float>(skeleton.bones.size()) * kTrackRowHeight + kRulerHeight;

        // ----------------------------------------------------------------
        // Left: bone list (same layout as dope sheet)
        // ----------------------------------------------------------------
        ImGui::BeginChild("##bone_list_cv", {kBoneListWidth, availH}, true);
        ImGui::SetScrollY(m_boneScrollY);
        ImGui::Dummy({kBoneListWidth - ImGui::GetStyle().WindowPadding.x * 2.f, kRulerHeight});
        for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
        {
            const auto &bone = skeleton.bones[i];
            int depth = 0, parent = bone.parentIndex;
            while (parent >= 0 && depth < 10)
            {
                depth++;
                parent = skeleton.bones[parent].parentIndex;
            }
            if (depth > 0)
                ImGui::Indent(depth * 8.f);
            bool sel = (m_selectedBone == i);
            if (ImGui::Selectable(bone.name.c_str(), sel, 0, {0, kTrackRowHeight}))
            {
                m_selectedBone = i;
                m_curveFitDirty = true;
            }
            if (depth > 0)
                ImGui::Unindent(depth * 8.f);
        }
        m_boneScrollY = ImGui::GetScrollY();
        // Header overlay
        {
            ImVec2 p = ImGui::GetWindowPos();
            ImVec2 pad = ImGui::GetStyle().WindowPadding;
            ImDrawList *bd = ImGui::GetWindowDrawList();
            float rl = p.x + pad.x, rt = p.y + pad.y;
            float rr = p.x + kBoneListWidth - pad.x, rb = rt + kRulerHeight;
            bd->AddRectFilled({rl, rt}, {rr, rb}, IM_COL32(32, 32, 32, 245));
            bd->AddLine({rl, rb}, {rr, rb}, IM_COL32(255, 255, 255, 45));
            bd->AddText({rl + 4.f, rt + 3.f}, IM_COL32(160, 160, 160, 200), "Bone");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ----------------------------------------------------------------
        // Right: curve area
        // ----------------------------------------------------------------
        ImGui::BeginChild("##curve_area", {panelWidth, availH}, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winPad = ImGui::GetStyle().WindowPadding;
        ImDrawList *dl = ImGui::GetWindowDrawList();

        float areaW = panelWidth - winPad.x * 2.f;
        float areaH = availH - winPad.y * 2.f;
        float rulerTop = winPos.y + winPad.y;
        float rulerBot = rulerTop + kRulerHeight;
        float axisLeft = winPos.x + winPad.x;
        float curveLeft = axisLeft + kYAxisWidth;
        float curveW = areaW - kYAxisWidth;
        float curveTop = rulerBot;
        float curveH = areaH - kRulerHeight;
        float curveRight = curveLeft + curveW;
        float curveBot = curveTop + curveH;

        // Expose for scrubbing in Update()
        m_trackOrigin = {curveLeft, curveTop};
        m_trackWidth = curveW;

        // ----------------------------------------------------------------
        // Zoom / pan (same as dope sheet)
        // ----------------------------------------------------------------
        float visibleDuration = m_clipDuration / m_zoomLevel;

        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f)
            {
                float mxTicks =
                    PixelToTicks(ImGui::GetMousePos().x - curveLeft, curveW);
                m_zoomLevel = std::clamp(m_zoomLevel + wheel * 0.5f, kMinZoom, kMaxZoom);
                visibleDuration = m_clipDuration / m_zoomLevel;
                float mxTicksAfter =
                    PixelToTicks(ImGui::GetMousePos().x - curveLeft, curveW);
                m_visibleStartTicks += mxTicks - mxTicksAfter;
            }
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            float dx = ImGui::GetIO().MouseDelta.x;
            m_visibleStartTicks -= (dx / curveW) * visibleDuration;
        }
        m_visibleStartTicks = std::clamp(m_visibleStartTicks, 0.f,
                                         std::max(0.f, m_clipDuration - visibleDuration));
        visibleDuration = m_clipDuration / m_zoomLevel;

        // ----------------------------------------------------------------
        // Find channel for selected bone (need mutable idx for editing)
        // ----------------------------------------------------------------
        int selectedChanIdx = -1;
        if (m_selectedBone >= 0)
        {
            for (int ci = 0; ci < static_cast<int>(clip.channels.size()); ++ci)
                if (clip.channels[ci].boneIndex == m_selectedBone)
                {
                    selectedChanIdx = ci;
                    break;
                }
        }
        const AnimationChannel *chan =
            selectedChanIdx >= 0 ? &clip.channels[selectedChanIdx] : nullptr;

        // Value ↔ screen Y inverse (for drag)
        auto yToVal = [&](float sy) -> float
        {
            return m_curveYMax -
                   (sy - curveTop) / curveH * (m_curveYMax - m_curveYMin + 0.0001f);
        };

        // Key value get/set helpers (axis-aware)
        auto getKeyValue = [&](const KeyRef &ref) -> float
        {
            if (ref.channelIdx < 0 ||
                ref.channelIdx >= static_cast<int>(clip.channels.size()))
                return 0.f;
            const auto &ch = clip.channels[ref.channelIdx];
            int ax = std::max(0, ref.axis);
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(ch.positionKeys.size()))
                    return ch.positionKeys[ref.keyIdx].value[ax];
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(ch.rotationKeys.size()))
                    return glm::degrees(
                        glm::eulerAngles(ch.rotationKeys[ref.keyIdx].value))[ax];
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(ch.scaleKeys.size()))
                    return ch.scaleKeys[ref.keyIdx].value[ax];
                break;
            }
            return 0.f;
        };

        auto setKeyValue = [&](const KeyRef &ref, float newVal)
        {
            if (ref.channelIdx < 0 ||
                ref.channelIdx >= static_cast<int>(clip.channels.size()))
                return;
            auto &ch = clip.channels[ref.channelIdx];
            int ax = std::max(0, ref.axis);
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(ch.positionKeys.size()))
                    ch.positionKeys[ref.keyIdx].value[ax] = newVal;
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(ch.rotationKeys.size()))
                {
                    vec3 euler = glm::degrees(
                        glm::eulerAngles(ch.rotationKeys[ref.keyIdx].value));
                    euler[ax] = newVal;
                    ch.rotationKeys[ref.keyIdx].value = glm::quat(glm::radians(euler));
                }
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(ch.scaleKeys.size()))
                    ch.scaleKeys[ref.keyIdx].value[ax] = newVal;
                break;
            }
        };

        auto getKeyTime = [&](const KeyRef &ref) -> float
        {
            if (ref.channelIdx < 0 ||
                ref.channelIdx >= static_cast<int>(clip.channels.size()))
                return 0.f;
            const auto &ch = clip.channels[ref.channelIdx];
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(ch.positionKeys.size()))
                    return ch.positionKeys[ref.keyIdx].time;
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(ch.rotationKeys.size()))
                    return ch.rotationKeys[ref.keyIdx].time;
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(ch.scaleKeys.size()))
                    return ch.scaleKeys[ref.keyIdx].time;
                break;
            }
            return 0.f;
        };

        auto setKeyTime = [&](const KeyRef &ref, float newTime)
        {
            if (ref.channelIdx < 0 ||
                ref.channelIdx >= static_cast<int>(clip.channels.size()))
                return;
            auto &ch = clip.channels[ref.channelIdx];
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx < static_cast<int>(ch.positionKeys.size()))
                    ch.positionKeys[ref.keyIdx].time = newTime;
                break;
            case KeyType::Rotation:
                if (ref.keyIdx < static_cast<int>(ch.rotationKeys.size()))
                    ch.rotationKeys[ref.keyIdx].time = newTime;
                break;
            case KeyType::Scale:
                if (ref.keyIdx < static_cast<int>(ch.scaleKeys.size()))
                    ch.scaleKeys[ref.keyIdx].time = newTime;
                break;
            }
        };

        // Scrub (set only when NOT clicking on a key — handled below)
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_scrubbing = false;

        // selectedChanIdx + chan already resolved above

        // ----------------------------------------------------------------
        // Auto-fit Y range from keyframe values of the selected bone
        // ----------------------------------------------------------------
        if (m_curveFitDirty || (m_curveYMin == m_curveYMax))
        {
            float yMin = FLT_MAX, yMax = -FLT_MAX;
            auto expand = [&](float v)
            {
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            };
            if (chan)
            {
                if (m_showPosition)
                    for (const auto &k : chan->positionKeys)
                    {
                        expand(k.value.x);
                        expand(k.value.y);
                        expand(k.value.z);
                    }
                if (m_showRotation)
                    for (const auto &k : chan->rotationKeys)
                    {
                        vec3 e = glm::degrees(glm::eulerAngles(k.value));
                        expand(e.x);
                        expand(e.y);
                        expand(e.z);
                    }
                if (m_showScale)
                    for (const auto &k : chan->scaleKeys)
                    {
                        expand(k.value.x);
                        expand(k.value.y);
                        expand(k.value.z);
                    }
            }
            if (yMin < yMax)
            {
                float pad = (yMax - yMin) * 0.12f + 0.05f;
                m_curveYMin = yMin - pad;
                m_curveYMax = yMax + pad;
            }
            else
            {
                m_curveYMin = -1.f;
                m_curveYMax = 1.f;
            }
            m_curveFitDirty = false;
        }

        // Value ↔ screen Y helpers
        auto valToY = [&](float v) -> float
        {
            float t = (m_curveYMax - v) / (m_curveYMax - m_curveYMin + 0.0001f);
            return curveTop + t * curveH;
        };

        // ----------------------------------------------------------------
        // Compute grid intervals (reused for time grid + ruler)
        // ----------------------------------------------------------------
        float targetGridPx = 80.f;
        float gridIntervalSec =
            targetGridPx /
            (curveW / (visibleDuration / m_ticksPerSecond + 0.001f) + 0.001f);
        static constexpr float niceIntervals[] = {0.05f, 0.1f, 0.25f, 0.5f, 1.f,
                                                  2.f, 5.f, 10.f, 30.f, 60.f};
        for (float ni : niceIntervals)
        {
            if (ni >= gridIntervalSec)
            {
                gridIntervalSec = ni;
                break;
            }
        }
        float gridIntervalTicks = gridIntervalSec * m_ticksPerSecond;

        // ----------------------------------------------------------------
        // Curve area background
        // ----------------------------------------------------------------
        dl->AddRectFilled({curveLeft, curveTop}, {curveRight, curveBot},
                          IM_COL32(22, 22, 22, 255));

        // Horizontal Y-grid lines + Y-axis labels
        constexpr int kYDivisions = 5;
        for (int i = 0; i <= kYDivisions; ++i)
        {
            float v = m_curveYMin +
                      (m_curveYMax - m_curveYMin) * static_cast<float>(i) / kYDivisions;
            float sy = valToY(v);
            if (sy < curveTop || sy > curveBot)
                continue;
            dl->AddLine({curveLeft, sy}, {curveRight, sy}, IM_COL32(255, 255, 255, 14));
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", v);
            dl->AddText({axisLeft + 2.f, sy - 6.f}, IM_COL32(140, 140, 140, 180), buf);
        }

        // Zero line (brighter)
        if (m_curveYMin < 0.f && m_curveYMax > 0.f)
        {
            float zy = valToY(0.f);
            dl->AddLine({curveLeft, zy}, {curveRight, zy},
                        IM_COL32(255, 255, 255, 38), 1.f);
        }

        // Vertical time grid lines
        if (gridIntervalTicks > 0.f)
        {
            float startGrid =
                std::floor(m_visibleStartTicks / gridIntervalTicks) * gridIntervalTicks;
            for (float t = startGrid; t <= m_visibleStartTicks + visibleDuration;
                 t += gridIntervalTicks)
            {
                float px = TicksToPixel(t, curveW);
                if (px >= 0.f && px <= curveW)
                    dl->AddLine({curveLeft + px, curveTop}, {curveLeft + px, curveBot},
                                IM_COL32(255, 255, 255, 20));
            }
        }

        // Y-axis separator
        dl->AddLine({curveLeft, curveTop}, {curveLeft, curveBot},
                    IM_COL32(255, 255, 255, 50));

        // ----------------------------------------------------------------
        // Sample + draw curves
        // ----------------------------------------------------------------
        if (m_selectedBone < 0 || !chan)
        {
            const char *hint = m_selectedBone < 0 ? "Select a bone to view curves"
                                                  : "Selected bone has no keyframes";
            ImVec2 tsz = ImGui::CalcTextSize(hint);
            dl->AddText({curveLeft + (curveW - tsz.x) * 0.5f,
                         curveTop + (curveH - tsz.y) * 0.5f},
                        IM_COL32(160, 160, 160, 140), hint);
        }
        else
        {
            int numSamples = std::max(4, static_cast<int>(curveW / 2.f));

            // Colors: pos=red/green/blue, rot=orange/lime/sky, scl=purple/yellow/cyan
            static const ImU32 kCurveColors[3][3] = {
                {IM_COL32(255, 80, 80, 210), IM_COL32(80, 220, 80, 210),
                 IM_COL32(80, 130, 255, 210)},
                {IM_COL32(255, 160, 60, 210), IM_COL32(180, 255, 60, 210),
                 IM_COL32(60, 200, 255, 210)},
                {IM_COL32(200, 80, 200, 210), IM_COL32(210, 210, 60, 210),
                 IM_COL32(60, 210, 200, 210)}};
            static const char *kCurveLabels[3][3] = {{"Px", "Py", "Pz"},
                                                     {"Rx", "Ry", "Rz"},
                                                     {"Sx", "Sy", "Sz"}};

            struct CvKeyHit
            {
                KeyRef ref;
                float screenX, screenY;
            };
            std::vector<CvKeyHit> keyHits;

            ImU32 colSel = IM_COL32(255, 220, 0, 255);
            std::vector<ImVec2> pts;
            pts.reserve(numSamples);

            // draw one curve axis: polyline + dots collected into keyHits
            auto drawCurveAxis = [&](KeyType type, int ax, ImU32 col, auto sampleFn)
            {
                // Polyline
                pts.clear();
                for (int s = 0; s < numSamples; ++s)
                {
                    float t = m_visibleStartTicks +
                              static_cast<float>(s) / (numSamples - 1) * visibleDuration;
                    float sy = std::clamp(valToY(sampleFn(t)), curveTop, curveBot);
                    pts.push_back({curveLeft + TicksToPixel(t, curveW), sy});
                }
                if (static_cast<int>(pts.size()) >= 2)
                    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, 0,
                                    1.5f);

                // Keyframe dots
                const auto &ch = *chan;
                int nKeys = (type == KeyType::Position)
                                ? static_cast<int>(ch.positionKeys.size())
                            : (type == KeyType::Rotation)
                                ? static_cast<int>(ch.rotationKeys.size())
                                : static_cast<int>(ch.scaleKeys.size());
                for (int ki = 0; ki < nKeys; ++ki)
                {
                    KeyRef ref{selectedChanIdx, type, ki, ax};
                    float kTime = getKeyTime(ref);
                    float kVal = getKeyValue(ref);
                    float kpx = TicksToPixel(kTime, curveW);
                    if (kpx < -8.f || kpx > curveW + 8.f)
                        continue;
                    float ksy = std::clamp(valToY(kVal), curveTop, curveBot);
                    bool sel = IsKeySelected(ref);
                    float r = sel ? 5.5f : 3.5f;
                    dl->AddCircleFilled({curveLeft + kpx, ksy}, r, sel ? colSel : col);
                    dl->AddCircle({curveLeft + kpx, ksy}, r,
                                  IM_COL32(255, 255, 255, sel ? 230 : 140), 12, 1.f);
                    keyHits.push_back({ref, curveLeft + kpx, ksy});
                }
            };

            if (m_showPosition)
                for (int ax = 0; ax < 3; ++ax)
                    drawCurveAxis(KeyType::Position, ax, kCurveColors[0][ax],
                                  [&](float t)
                                  {
                                      return AnimationEvaluator::InterpolatePosition(
                                          chan->positionKeys, t)[ax];
                                  });

            if (m_showRotation)
                for (int ax = 0; ax < 3; ++ax)
                    drawCurveAxis(KeyType::Rotation, ax, kCurveColors[1][ax],
                                  [&](float t)
                                  {
                                      return glm::degrees(glm::eulerAngles(
                                          AnimationEvaluator::InterpolateRotation(
                                              chan->rotationKeys, t)))[ax];
                                  });

            if (m_showScale)
                for (int ax = 0; ax < 3; ++ax)
                    drawCurveAxis(KeyType::Scale, ax, kCurveColors[2][ax],
                                  [&](float t)
                                  {
                                      return AnimationEvaluator::InterpolateScale(
                                          chan->scaleKeys, t)[ax];
                                  });

            // ----------------------------------------------------------------
            // Hit test
            // ----------------------------------------------------------------
            ImVec2 mousePos = ImGui::GetMousePos();
            CvKeyHit hovHit{{}, 0, 0};
            bool hovered = false;
            for (const auto &kh : keyHits)
            {
                if (std::abs(mousePos.x - kh.screenX) < kKeyHitRadius &&
                    std::abs(mousePos.y - kh.screenY) < kKeyHitRadius)
                {
                    hovHit = kh;
                    hovered = true;
                    break;
                }
            }

            // ----------------------------------------------------------------
            // Input: click to select, drag to edit value + time
            // ----------------------------------------------------------------
            if (ImGui::IsWindowHovered())
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (hovered)
                    {
                        SelectKey(hovHit.ref, ImGui::GetIO().KeyShift);
                        m_draggingKeys = true;
                        m_dragAnchorTicks =
                            PixelToTicks(mousePos.x - curveLeft, curveW);
                        m_dragAnchorValue = yToVal(mousePos.y);
                        m_dragOrigTimes.clear();
                        m_dragOrigValues.clear();
                        for (const auto &ref : m_selectedKeys)
                        {
                            m_dragOrigTimes.push_back(getKeyTime(ref));
                            m_dragOrigValues.push_back(getKeyValue(ref));
                        }
                        m_scrubbing = false;
                    }
                    else
                    {
                        if (!ImGui::GetIO().KeyShift)
                            m_selectedKeys.clear();
                        m_scrubbing = true;
                    }
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered)
                {
                    if (!IsKeySelected(hovHit.ref))
                        SelectKey(hovHit.ref, false);
                    ImGui::OpenPopup("##cvKeyMenu");
                }
            }

            // Drag update
            if (m_draggingKeys && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !m_selectedKeys.empty())
            {
                float curTicks = PixelToTicks(mousePos.x - curveLeft, curveW);
                float curVal = yToVal(mousePos.y);
                float dticks = curTicks - m_dragAnchorTicks;
                float dvalue = curVal - m_dragAnchorValue;

                // De-duplicate time updates: only retime each (chan,type,key) once
                std::vector<std::tuple<int, KeyType, int>> retimed;
                for (int i = 0; i < static_cast<int>(m_selectedKeys.size()); ++i)
                {
                    const auto &ref = m_selectedKeys[i];
                    float origTime =
                        i < static_cast<int>(m_dragOrigTimes.size())
                            ? m_dragOrigTimes[i]
                            : 0.f;
                    float origVal =
                        i < static_cast<int>(m_dragOrigValues.size())
                            ? m_dragOrigValues[i]
                            : 0.f;
                    auto key = std::make_tuple(ref.channelIdx, ref.type, ref.keyIdx);
                    if (std::find(retimed.begin(), retimed.end(), key) == retimed.end())
                    {
                        setKeyTime(ref,
                                   std::clamp(origTime + dticks, 0.f, m_clipDuration));
                        retimed.push_back(key);
                    }
                    setKeyValue(ref, origVal + dvalue);
                }
                m_curveFitDirty = false;
            }

            // Drag release
            if (m_draggingKeys && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                for (const auto &ref : m_selectedKeys)
                    if (ref.channelIdx >= 0 &&
                        ref.channelIdx < static_cast<int>(clip.channels.size()))
                        SortChannelKeys(clip.channels[ref.channelIdx]);
                m_selectedKeys.clear();
                m_draggingKeys = false;
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }

            // Context menu
            if (ImGui::BeginPopup("##cvKeyMenu"))
            {
                ImGui::TextDisabled("%d key(s)", static_cast<int>(m_selectedKeys.size()));
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del"))
                {
                    DeleteSelectedKeys(clip);
                    ReevaluatePose(scene, anim, clip, currentTimeTicks);
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                    CopySelectedKeys(clip);
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty()))
                {
                    PasteKeys(clip, currentTimeTicks);
                    ReevaluatePose(scene, anim, clip, currentTimeTicks);
                }
                ImGui::EndPopup();
            }

            // Keyboard shortcuts
            if (ImGui::IsWindowFocused())
            {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_selectedKeys.empty())
                {
                    DeleteSelectedKeys(clip);
                    ReevaluatePose(scene, anim, clip, currentTimeTicks);
                }
                if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
                    CopySelectedKeys(clip);
                if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V) && !m_clipboard.empty())
                {
                    PasteKeys(clip, currentTimeTicks);
                    ReevaluatePose(scene, anim, clip, currentTimeTicks);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    m_selectedKeys.clear();
            }

            // ----------------------------------------------------------------
            // Curve legend (top-right corner)
            // ----------------------------------------------------------------
            float legX = curveRight - 32.f;
            float legY = curveTop + 4.f;
            for (int t = 0; t < 3; ++t)
            {
                bool shown = (t == 0 && m_showPosition) || (t == 1 && m_showRotation) ||
                             (t == 2 && m_showScale);
                if (!shown)
                    continue;
                for (int ax = 0; ax < 3; ++ax)
                {
                    ImU32 col = kCurveColors[t][ax];
                    dl->AddRectFilled({legX - 1.f, legY + 4.f}, {legX + 8.f, legY + 6.f},
                                      col);
                    dl->AddText({legX + 10.f, legY}, col, kCurveLabels[t][ax]);
                    legY += 14.f;
                }
            }
        }

        // Playhead
        float phPx = TicksToPixel(currentTimeTicks, curveW);
        if (phPx >= 0.f && phPx <= curveW)
            dl->AddLine({curveLeft + phPx, curveTop}, {curveLeft + phPx, curveBot},
                        IM_COL32(255, 60, 60, 200), 2.f);

        // ----------------------------------------------------------------
        // Ruler overlay (identical to dope sheet)
        // ----------------------------------------------------------------
        {
            float rl = curveLeft - kYAxisWidth;
            float rr = curveRight;
            float rb = rulerBot;

            dl->AddRectFilled({rl, rulerTop}, {rr, rb}, IM_COL32(32, 32, 32, 245));
            dl->AddLine({rl, rb}, {rr, rb}, IM_COL32(255, 255, 255, 45));

            if (gridIntervalTicks > 0.f)
            {
                float startGrid =
                    std::floor(m_visibleStartTicks / gridIntervalTicks) * gridIntervalTicks;
                float halfTicks = gridIntervalTicks * 0.5f;
                for (float t = startGrid - halfTicks;
                     t <= m_visibleStartTicks + visibleDuration; t += halfTicks)
                {
                    float px = TicksToPixel(t, curveW);
                    if (px < 0.f || px > curveW)
                        continue;
                    bool isMajor = (std::fmod(std::abs(t - startGrid), gridIntervalTicks) <
                                    0.001f * gridIntervalTicks);
                    float tickH =
                        isMajor ? kRulerHeight * 0.55f : kRulerHeight * 0.3f;
                    dl->AddLine({curveLeft + px, rb - tickH}, {curveLeft + px, rb},
                                IM_COL32(255, 255, 255, isMajor ? 100 : 50));
                }
                for (float t = startGrid; t <= m_visibleStartTicks + visibleDuration;
                     t += gridIntervalTicks)
                {
                    float px = TicksToPixel(t, curveW);
                    if (px < 0.f || px > curveW)
                        continue;
                    char frameBuf[16], secBuf[16];
                    snprintf(frameBuf, sizeof(frameBuf), "f%d",
                             static_cast<int>(std::round(t)));
                    float secs = (m_ticksPerSecond > 0.f) ? t / m_ticksPerSecond : 0.f;
                    snprintf(secBuf, sizeof(secBuf), "%.2fs", secs);
                    dl->AddText({curveLeft + px + 3.f, rulerTop + 2.f},
                                IM_COL32(220, 220, 220, 230), frameBuf);
                    dl->AddText({curveLeft + px + 3.f, rulerTop + 16.f},
                                IM_COL32(140, 140, 140, 180), secBuf);
                }
            }

            // Playhead indicator in ruler
            if (phPx >= 0.f && phPx <= curveW)
            {
                float tx = curveLeft + phPx;
                dl->AddTriangleFilled({tx - 5.f, rulerTop + 1.f},
                                      {tx + 5.f, rulerTop + 1.f}, {tx, rb - 1.f},
                                      IM_COL32(255, 60, 60, 220));
                char phFrame[16], phSecs[16];
                snprintf(phFrame, sizeof(phFrame), "f%d",
                         static_cast<int>(std::round(currentTimeTicks)));
                float phS =
                    (m_ticksPerSecond > 0.f) ? currentTimeTicks / m_ticksPerSecond : 0.f;
                snprintf(phSecs, sizeof(phSecs), "%.2fs", phS);
                ImVec2 sz1 = ImGui::CalcTextSize(phFrame);
                ImVec2 sz2 = ImGui::CalcTextSize(phSecs);
                float boxW = std::max(sz1.x, sz2.x) + 4.f;
                float lx = std::clamp(tx - boxW * 0.5f, curveLeft, curveRight - boxW);
                dl->AddRectFilled({lx - 1.f, rulerTop + 1.f}, {lx + boxW, rb - 1.f},
                                  IM_COL32(50, 10, 10, 210));
                dl->AddText({lx + (boxW - sz1.x) * 0.5f, rulerTop + 2.f},
                            IM_COL32(255, 140, 140, 255), phFrame);
                dl->AddText({lx + (boxW - sz2.x) * 0.5f, rulerTop + 16.f},
                            IM_COL32(220, 100, 100, 200), phSecs);
            }
        }

        ImGui::Dummy({areaW, areaH});
        ImGui::EndChild();
    }

    // -------------------------------------------------------------------------
    // Status bar
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawStatusBar(const Skeleton &skeleton, const AnimationClip &clip)
    {
        ImGui::Separator();

        // Clip selector
        ImGui::SetNextItemWidth(200.f);

        auto *rs = GetGlobalSystem<RendererSystem>();
        if (!rs)
            return;
        Scene &scene = rs->GetScene();

        NodeId *resolveNode = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        ModelAsset *model = resolveNode ? scene.GetModelForNode(resolveNode) : nullptr;
        if (!model)
            model = scene.FindSkeletonModel();

        if (model)
        {
            const auto &clips = model->GetAnimations();
            if (!clips.empty())
            {
                const char *preview =
                    (m_selectedClip >= 0 && m_selectedClip < static_cast<int>(clips.size()))
                        ? clips[m_selectedClip].name.c_str()
                        : "Select clip...";
                if (ImGui::BeginCombo("##clip", preview))
                {
                    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
                    {
                        bool selected = (m_selectedClip == i);
                        const char *label =
                            clips[i].name.empty() ? "<unnamed>" : clips[i].name.c_str();
                        if (ImGui::Selectable(label, selected))
                            m_selectedClip = i;
                    }
                    ImGui::EndCombo();
                }
            }
        }

        ImGui::SameLine();

        int totalKeys = 0;
        for (const auto &chan : clip.channels)
            totalKeys += static_cast<int>(chan.positionKeys.size() + chan.rotationKeys.size() +
                                          chan.scaleKeys.size());

        ImGui::TextDisabled("Bones: %d  Keys: %d  TPS: %.0f  Sel: %d",
                            static_cast<int>(skeleton.bones.size()), totalKeys,
                            clip.ticksPerSecond, static_cast<int>(m_selectedKeys.size()));

        // Show selected key values in status if exactly one key selected
        if (m_selectedKeys.size() == 1)
        {
            const auto &ref = m_selectedKeys[0];
            if (ref.channelIdx >= 0 && ref.channelIdx < static_cast<int>(clip.channels.size()))
            {
                const auto &chan = clip.channels[ref.channelIdx];
                char valueBuf[128] = {};
                switch (ref.type)
                {
                case KeyType::Position:
                    if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                    {
                        const auto &k = chan.positionKeys[ref.keyIdx];
                        snprintf(valueBuf, sizeof(valueBuf),
                                 "  |  Pos  t=%.3f  (%.3f, %.3f, %.3f)", k.time, k.value.x,
                                 k.value.y, k.value.z);
                    }
                    break;
                case KeyType::Rotation:
                    if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                    {
                        const auto &k = chan.rotationKeys[ref.keyIdx];
                        snprintf(valueBuf, sizeof(valueBuf),
                                 "  |  Rot  t=%.3f  (%.3f, %.3f, %.3f, %.3f)", k.time,
                                 k.value.x, k.value.y, k.value.z, k.value.w);
                    }
                    break;
                case KeyType::Scale:
                    if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                    {
                        const auto &k = chan.scaleKeys[ref.keyIdx];
                        snprintf(valueBuf, sizeof(valueBuf),
                                 "  |  Scl  t=%.3f  (%.3f, %.3f, %.3f)", k.time, k.value.x,
                                 k.value.y, k.value.z);
                    }
                    break;
                }
                if (valueBuf[0])
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", valueBuf);
                }
            }
        }

        // Channel filter toggles (right-aligned)
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.f));
        ImGui::Checkbox("Pos", &m_showPosition);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.8f, 0.35f, 1.f));
        ImGui::Checkbox("Rot", &m_showRotation);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.5f, 0.9f, 1.f));
        ImGui::Checkbox("Scl", &m_showScale);
        ImGui::PopStyleColor();
    }

    // -------------------------------------------------------------------------
    // Main Update
    // -------------------------------------------------------------------------
    void AnimationTimeline::Update()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize({900, 300}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }

        auto *rs = GetGlobalSystem<RendererSystem>();
        auto *anim = GetGlobalSystem<AnimationSystem>();
        if (!rs || !anim)
        {
            ImGui::TextDisabled("Animation system not available.");
            ImGui::End();
            return;
        }

        Scene &scene = rs->GetScene();

        // Track selected node from hierarchy
        auto &selection = SelectionManager::Instance();
        if (selection.HasSelection())
        {
            NodeId *sel = selection.GetSelectedNode();
            if (scene.IsNodeAlive(sel))
                m_targetNode = sel;
            else
                m_targetNode = nullptr;
        }
        else
        {
            m_targetNode = nullptr;
        }

        // Collect animated descendants
        m_animatedNodes.clear();
        if (m_targetNode)
            CollectAnimatedNodes(scene, anim, m_targetNode);

        // If nothing is playing yet but the target node has animations, include it so
        // transport controls can start playback.
        if (m_animatedNodes.empty() && m_targetNode)
        {
            ModelAsset *candidate = scene.GetModelForNode(m_targetNode);
            if (!candidate)
                candidate = scene.FindSkeletonModel();
            if (candidate && candidate->HasSkeleton() && candidate->HasAnimations())
                m_animatedNodes.push_back(m_targetNode);
        }

        NodeId *primaryNode = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];

        // Resolve skeleton + clips from the primary animated node's model
        const Skeleton *skeleton = nullptr;
        m_editModel = nullptr;

        NodeId *resolveNode = primaryNode ? primaryNode : m_targetNode;
        if (resolveNode)
        {
            ModelAsset *model = scene.GetModelForNode(resolveNode);
            if (model && model->HasSkeleton() && model->HasAnimations())
            {
                skeleton = &model->GetSkeleton();
                m_editModel = model;
            }
        }

        // Scene-global fallback
        if (!skeleton)
        {
            m_editModel = scene.FindSkeletonModel();
            if (m_editModel && m_editModel->HasSkeleton() && m_editModel->HasAnimations())
                skeleton = &m_editModel->GetSkeleton();
            else
                m_editModel = nullptr;
        }

        if (!skeleton || !m_editModel || m_editModel->GetAnimations().empty())
        {
            ImGui::TextDisabled("No skeleton or animation clips found. Select an animated node.");
            ImGui::End();
            return;
        }

        if (m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(m_editModel->GetAnimations().size()))
            m_selectedClip = 0;

        AnimationClip &clip = m_editModel->GetMutableAnimations()[m_selectedClip];
        m_clipDuration = clip.duration;
        m_ticksPerSecond = clip.ticksPerSecond > 0.f ? clip.ticksPerSecond : 25.f;

        const auto *state = primaryNode ? anim->GetAnimationState(primaryNode) : nullptr;
        float currentTimeTicks = state ? state->time : 0.f;

        // Keyboard shortcuts (only when this window is focused)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        {
            bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_selectedKeys.empty())
            {
                DeleteSelectedKeys(clip);
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
                CopySelectedKeys(clip);
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V) && !m_clipboard.empty())
            {
                PasteKeys(clip, currentTimeTicks);
                ReevaluatePose(scene, anim, clip, currentTimeTicks);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_selectedKeys.clear();
        }

        // Transport bar
        DrawTransportBar(scene, anim, clip);
        ImGui::Separator();

        // Track view (Phase 1 + Phase 2)
        DrawTrackView(scene, anim, *skeleton, clip, currentTimeTicks);

        // Scrub (handled inside DrawTrackView; apply here)
        if (m_scrubbing && !m_animatedNodes.empty() && m_trackWidth > 0.f)
        {
            float mouseX = ImGui::GetMousePos().x - m_trackOrigin.x;
            float ticks =
                PixelToTicks(std::clamp(mouseX, 0.f, m_trackWidth), m_trackWidth);
            ticks = std::clamp(ticks, 0.f, m_clipDuration);
            ForEachAnimatedNode([&](NodeId *node)
                                { anim->SetPlaybackTime(scene, node, ticks); });
        }

        // Status bar
        DrawStatusBar(*skeleton, clip);

        ImGui::End();
    }
} // namespace pe
