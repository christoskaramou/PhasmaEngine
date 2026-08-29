#include "AnimationTimeline.h"
#include "Animation/AnimationEvaluator.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    // Blender (4.x) Dope Sheet / Timeline palette.
    namespace bl
    {
        constexpr ImU32 kHeaderBg = IM_COL32(35, 35, 35, 255);
        constexpr ImU32 kChannelBg = IM_COL32(40, 40, 40, 255);
        constexpr ImU32 kKeyAreaBg = IM_COL32(48, 48, 48, 255);
        constexpr ImU32 kRulerBg = IM_COL32(37, 37, 37, 255);
        constexpr ImU32 kSummaryRow = IM_COL32(66, 66, 66, 255);
        constexpr ImU32 kGroupRow = IM_COL32(56, 56, 56, 255);
        constexpr ImU32 kGroupRowAlt = IM_COL32(52, 52, 52, 255);
        constexpr ImU32 kSubRow = IM_COL32(46, 46, 46, 255);
        constexpr ImU32 kRowSelected = IM_COL32(62, 90, 128, 255);
        constexpr ImU32 kRowActive = IM_COL32(76, 108, 150, 255);
        constexpr ImU32 kRowHover = IM_COL32(255, 255, 255, 10);
        constexpr ImU32 kRowLine = IM_COL32(0, 0, 0, 60);
        constexpr ImU32 kText = IM_COL32(230, 230, 230, 255);
        constexpr ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
        constexpr ImU32 kGridMinor = IM_COL32(255, 255, 255, 10);
        constexpr ImU32 kGridMajor = IM_COL32(255, 255, 255, 26);
        constexpr ImU32 kOutsideRange = IM_COL32(0, 0, 0, 70);
        constexpr ImU32 kPlayhead = IM_COL32(86, 128, 194, 255);
        constexpr ImU32 kPlayheadBadge = IM_COL32(71, 114, 179, 255);
        constexpr ImU32 kKeyFill = IM_COL32(232, 232, 232, 255);
        constexpr ImU32 kKeyFillSel = IM_COL32(255, 190, 51, 255);
        constexpr ImU32 kKeyBorder = IM_COL32(0, 0, 0, 255);
        constexpr ImU32 kKeyBorderSel = IM_COL32(255, 235, 170, 255);
        constexpr ImU32 kKeyHover = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kBoxSelect = IM_COL32(255, 255, 255, 180);
        constexpr ImU32 kBoxSelectFill = IM_COL32(255, 255, 255, 18);
        constexpr ImU32 kIcon = IM_COL32(220, 220, 220, 255);
        constexpr ImU32 kIconHover = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kIconActive = IM_COL32(86, 128, 194, 255);
        constexpr ImU32 kRecord = IM_COL32(230, 60, 60, 255);
        constexpr ImU32 kLoc = IM_COL32(235, 95, 95, 255);
        constexpr ImU32 kRot = IM_COL32(120, 215, 95, 255);
        constexpr ImU32 kScl = IM_COL32(95, 145, 240, 255);
        constexpr ImU32 kDirty = IM_COL32(255, 190, 51, 255);
    } // namespace bl

    namespace
    {
        constexpr float kFrameEps = 1e-3f;

        enum class Icon
        {
            JumpStart,
            PrevKey,
            PlayReverse,
            Play,
            Pause,
            NextKey,
            JumpEnd,
            Record
        };

        void DrawDiamond(ImDrawList *dl, const ImVec2 &c, float s, ImU32 fill, ImU32 border)
        {
            const ImVec2 p0(c.x, c.y - s), p1(c.x + s, c.y), p2(c.x, c.y + s), p3(c.x - s, c.y);
            dl->AddQuadFilled(p0, p1, p2, p3, fill);
            dl->AddQuad(p0, p1, p2, p3, border, 1.f);
        }

        // Blender-style transport glyphs drawn into an invisible button.
        bool IconButton(const char *id, Icon icon, const char *tooltip, bool active = false)
        {
            const ImVec2 size(24.f, 22.f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton(id, size);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && tooltip)
                ui::TooltipText(tooltip);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (hovered || active)
                dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, active ? IM_COL32(86, 128, 194, 90) : IM_COL32(255, 255, 255, 25), 3.f);
            const ImU32 col = active ? bl::kIconActive : (hovered ? bl::kIconHover : bl::kIcon);
            const float cx = p.x + size.x * 0.5f, cy = p.y + size.y * 0.5f;
            const float h = 5.f, w = 5.f;
            auto triRight = [&](float x)
            { dl->AddTriangleFilled({x - w, cy - h}, {x + w, cy}, {x - w, cy + h}, col); };
            auto triLeft = [&](float x)
            { dl->AddTriangleFilled({x + w, cy - h}, {x - w, cy}, {x + w, cy + h}, col); };
            auto bar = [&](float x)
            { dl->AddRectFilled({x - 1.5f, cy - h}, {x + 1.5f, cy + h}, col); };
            auto diamond = [&](float x)
            { DrawDiamond(dl, {x, cy}, 3.5f, col, col); };
            switch (icon)
            {
            case Icon::JumpStart:
                bar(cx - 6.f);
                triLeft(cx + 3.f);
                break;
            case Icon::PrevKey:
                triLeft(cx + 3.f);
                diamond(cx - 6.f);
                break;
            case Icon::PlayReverse:
                triLeft(cx);
                break;
            case Icon::Play:
                triRight(cx + 1.f);
                break;
            case Icon::Pause:
                bar(cx - 3.f);
                bar(cx + 3.f);
                break;
            case Icon::NextKey:
                triRight(cx - 3.f);
                diamond(cx + 6.f);
                break;
            case Icon::JumpEnd:
                triRight(cx - 3.f);
                bar(cx + 6.f);
                break;
            case Icon::Record:
                dl->AddCircleFilled({cx, cy}, 5.f, active ? bl::kRecord : col, 16);
                if (active)
                    dl->AddCircle({cx, cy}, 7.f, bl::kRecord, 16, 1.5f);
                break;
            }
            return pressed;
        }

        float NiceFrameStep(float framesPerPixel, float targetPx)
        {
            const float raw = framesPerPixel * targetPx;
            static constexpr float steps[] = {1.f, 2.f, 5.f, 10.f, 20.f, 25.f, 50.f, 100.f, 200.f, 250.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f};
            for (float s : steps)
                if (s >= raw)
                    return s;
            return steps[sizeof(steps) / sizeof(steps[0]) - 1];
        }

        bool HotkeysAllowed()
        {
            return ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive();
        }

        template <typename K>
        int FindKeyAtTime(const std::vector<K> &keys, float time)
        {
            for (int i = 0; i < static_cast<int>(keys.size()); i++)
                if (std::abs(keys[i].time - time) < kFrameEps)
                    return i;
            return -1;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // frame <-> pixel
    // -------------------------------------------------------------------------
    float AnimationTimeline::FrameToPx(float frame) const
    {
        const float span = std::max(m_viewEnd - m_viewStart, 1e-3f);
        return m_keyLeft + (frame - m_viewStart) / span * m_keyWidth;
    }

    float AnimationTimeline::PxToFrame(float px) const
    {
        const float span = std::max(m_viewEnd - m_viewStart, 1e-3f);
        return m_viewStart + (px - m_keyLeft) / std::max(m_keyWidth, 1.f) * span;
    }

    float AnimationTimeline::SnapFrame(float frame) const
    {
        const bool snap = m_snap != ImGui::GetIO().KeyCtrl;
        return snap ? std::round(frame) : frame;
    }

    void AnimationTimeline::FrameAll()
    {
        m_fitPending = true;
    }

    void AnimationTimeline::ZoomRange(float &lo, float &hi, float around, float factor, float minSpan)
    {
        lo = around + (lo - around) * factor;
        hi = around + (hi - around) * factor;
        if (hi - lo < minSpan)
        {
            const float t = std::clamp((around - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
            lo = around - t * minSpan;
            hi = lo + minSpan;
        }
    }

    void AnimationTimeline::NavigateView(const ImVec2 &mouse, bool graph)
    {
        const ImGuiIO &io = ImGui::GetIO();
        const float span = m_viewEnd - m_viewStart;
        const float vspan = m_curveMax - m_curveMin;
        const float wheel = io.MouseWheel;
        if (wheel != 0.f)
        {
            if (io.KeyCtrl) // scroll frames
            {
                const float df = -wheel * span * 0.1f;
                m_viewStart += df;
                m_viewEnd += df;
            }
            else if (io.KeyShift) // scroll rows / values
            {
                if (graph)
                {
                    const float dv = wheel * vspan * 0.1f;
                    m_curveMin += dv;
                    m_curveMax += dv;
                }
                else
                    m_scrollY -= wheel * kRowHeight * 3.f;
            }
            else // zoom around the cursor
            {
                const float factor = wheel > 0.f ? 0.8f : 1.25f;
                ZoomRange(m_viewStart, m_viewEnd, PxToFrame(mouse.x), factor, kMinViewFrames);
                if (graph)
                    ZoomRange(m_curveMin, m_curveMax, PxToValue(mouse.y), factor, 1e-4f);
            }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            if (io.KeyCtrl) // zoom-drag: right = zoom in on frames, up = zoom in on values
            {
                ZoomRange(m_viewStart, m_viewEnd, (m_viewStart + m_viewEnd) * 0.5f, std::exp(-io.MouseDelta.x * 0.01f), kMinViewFrames);
                if (graph)
                    ZoomRange(m_curveMin, m_curveMax, (m_curveMin + m_curveMax) * 0.5f, std::exp(io.MouseDelta.y * 0.01f), 1e-4f);
            }
            else
            {
                const float df = io.MouseDelta.x * span / std::max(m_keyWidth, 1.f);
                m_viewStart -= df;
                m_viewEnd -= df;
                if (graph)
                {
                    const float dv = io.MouseDelta.y * vspan / std::max(m_curveHeight, 1.f);
                    m_curveMin += dv;
                    m_curveMax += dv;
                }
                else
                    m_scrollY -= io.MouseDelta.y;
            }
        }
    }

    // Blender "View Selected" (Numpad .): fit the selected keys, or everything when nothing is selected.
    void AnimationTimeline::FrameSelected(const AnimationClip &clip)
    {
        if (m_selectedKeys.empty())
        {
            FrameAll();
            m_curveFitPending = true;
            return;
        }
        const float big = std::numeric_limits<float>::max();
        float f0 = big, f1 = -big, v0 = big, v1 = -big;
        for (const KeyRef &ref : m_selectedKeys)
        {
            const float f = KeyFrame(clip, ref);
            f0 = std::min(f0, f);
            f1 = std::max(f1, f);
            if (m_mode != Mode::GraphEditor)
                continue;
            for (const Curve &c : m_curves)
            {
                if (c.channelIdx != ref.channelIdx || c.type != ref.type)
                    continue;
                float v = KeyComponent(clip.channels[ref.channelIdx], ref.type, ref.keyIdx, c.axis);
                if (m_normalize)
                    v = (v - c.mid) / c.half;
                v0 = std::min(v0, v);
                v1 = std::max(v1, v);
            }
        }
        const float fpad = std::max((f1 - f0) * 0.1f, kMinViewFrames * 0.5f);
        m_viewStart = f0 - fpad;
        m_viewEnd = f1 + fpad;
        m_fitPending = false;
        if (v0 <= v1)
        {
            const float vpad = std::max((v1 - v0) * 0.1f, 0.05f);
            m_curveMin = v0 - vpad;
            m_curveMax = v1 + vpad;
            m_curveFitPending = false;
        }
    }

    // Assimp-cooked clips carry ticksPerSecond=1000 (milliseconds), so "frame = tick" would number
    // frames in thousands. Derive the frame grid from the data: 1 tick per frame for frame-based
    // clips, else the median key spacing (a 15 fps mocap clip at 1000 tps -> 66.667 ticks/frame).
    // ponytail: the grid is detected once per clip; a sparse hand-keyed clip at 1000 tps falls back to 24 fps.
    float AnimationTimeline::DetectFrameTicks(const AnimationClip &clip)
    {
        if (clip.ticksPerSecond <= 120.f)
            return 1.f;
        std::vector<float> times;
        for (const AnimationChannel &chan : clip.channels)
        {
            for (const PositionKey &k : chan.positionKeys)
                times.push_back(k.time);
            for (const RotationKey &k : chan.rotationKeys)
                times.push_back(k.time);
            for (const ScaleKey &k : chan.scaleKeys)
                times.push_back(k.time);
        }
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](float a, float b)
                                { return std::abs(a - b) < kFrameEps; }),
                    times.end());
        std::vector<float> deltas;
        for (size_t i = 1; i < times.size(); i++)
            if (times[i] - times[i - 1] > kFrameEps)
                deltas.push_back(times[i] - times[i - 1]);
        if (deltas.size() < 4)
            return clip.ticksPerSecond / 24.f;
        std::nth_element(deltas.begin(), deltas.begin() + deltas.size() / 2, deltas.end());
        return std::max(deltas[deltas.size() / 2], 1e-3f);
    }

    float AnimationTimeline::ToFrame(float ticks) const
    {
        return ticks / m_frameTicks;
    }

    float AnimationTimeline::ToTicks(float frame) const
    {
        return frame * m_frameTicks;
    }

    // -------------------------------------------------------------------------
    // animation state plumbing
    // -------------------------------------------------------------------------
    void AnimationTimeline::CollectAnimatedNodes(Scene &scene, AnimationSystem *anim, NodeId *root)
    {
        if (!scene.IsNodeAlive(root))
            return;
        ModelAsset *model = scene.GetModelForNode(root);
        if (model && model->HasSkeleton() && model->HasAnimations() && scene.NodeHasSkinnedMesh(root))
            m_animatedNodes.push_back(root);
        else if (anim->GetAnimationState(root))
            m_animatedNodes.push_back(root);
        for (NodeId *child : scene.GetChildren(root))
            CollectAnimatedNodes(scene, anim, child);
    }

    void AnimationTimeline::EnsureStates(Scene &scene, AnimationSystem *anim, float keepFrame)
    {
        for (NodeId *node : m_animatedNodes)
        {
            const AnimationNodeState *state = anim->GetAnimationState(node);
            if (state && state->clipIndex == m_selectedClip)
                continue;
            anim->PlayAnimation(scene, node, m_selectedClip, m_loop);
            anim->SetPaused(node, true);
            anim->SetSpeed(node, m_speed);
            anim->SetPlaybackTime(scene, node, ToTicks(keepFrame));
        }
    }

    void AnimationTimeline::SetFrame(Scene &scene, AnimationSystem *anim, float frame)
    {
        EnsureStates(scene, anim, frame);
        for (NodeId *node : m_animatedNodes)
            anim->SetPlaybackTime(scene, node, ToTicks(frame));
    }

    void AnimationTimeline::ReevaluatePose(Scene &scene, AnimationSystem *anim)
    {
        NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
        const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr;
        const bool playing = state && state->playing;
        const float frame = state ? ToFrame(state->time) : 0.f;
        SetFrame(scene, anim, frame);
        if (playing)
            for (NodeId *node : m_animatedNodes)
                anim->SetPaused(node, false);
        if (m_gui)
            m_gui->NotifyChange();
    }

    bool AnimationTimeline::IsPlaying(AnimationSystem *anim) const
    {
        NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
        const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr;
        return state && state->playing;
    }

    void AnimationTimeline::SetPlaying(Scene &scene, AnimationSystem *anim, bool play, bool reverse)
    {
        NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
        const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr;
        const float frame = state ? ToFrame(state->time) : 0.f;
        EnsureStates(scene, anim, frame);
        for (NodeId *node : m_animatedNodes)
        {
            anim->SetSpeed(node, reverse ? -std::abs(m_speed) : std::abs(m_speed));
            anim->SetLoop(node, m_loop);
            anim->SetPaused(node, !play);
        }
    }

    // -------------------------------------------------------------------------
    // undo / selection bookkeeping
    // -------------------------------------------------------------------------
    void AnimationTimeline::PushUndo(AnimationClip &clip)
    {
        PushUndoSnapshot(clip);
    }

    void AnimationTimeline::PushUndoSnapshot(const AnimationClip &snapshot)
    {
        m_undo.push_back({snapshot, m_selectedClip});
        if (static_cast<int>(m_undo.size()) > kMaxUndo)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        m_dirty = true;
    }

    void AnimationTimeline::Undo(AnimationClip &clip)
    {
        if (m_undo.empty() || m_undo.back().clipIndex != m_selectedClip)
            return;
        m_redo.push_back({clip, m_selectedClip});
        clip = m_undo.back().clip;
        m_undo.pop_back();
        SelClear();
        m_dirty = true;
    }

    void AnimationTimeline::Redo(AnimationClip &clip)
    {
        if (m_redo.empty() || m_redo.back().clipIndex != m_selectedClip)
            return;
        m_undo.push_back({clip, m_selectedClip});
        clip = m_redo.back().clip;
        m_redo.pop_back();
        SelClear();
        m_dirty = true;
    }

    void AnimationTimeline::ResetEditState()
    {
        m_undo.clear();
        m_redo.clear();
        SelClear();
        m_clipboard.clear();
        m_modal = Modal::None;
        m_boxSelecting = false;
        m_pressOnKey = false;
        m_dirty = false;
        m_fitPending = true;
        m_frameTicks = 0.f; // re-detect the frame grid for the new clip
        m_boneExpanded.clear();
        m_boneSelected.clear();
        m_activeBone = -1;
    }

    int AnimationTimeline::ChannelForBone(const AnimationClip &clip, int bone) const
    {
        for (int i = 0; i < static_cast<int>(clip.channels.size()); i++)
            if (clip.channels[i].boneIndex == bone)
                return i;
        return -1;
    }

    int AnimationTimeline::EnsureChannel(AnimationClip &clip, int bone)
    {
        int idx = ChannelForBone(clip, bone);
        if (idx >= 0)
            return idx;
        AnimationChannel chan;
        chan.boneIndex = bone;
        clip.channels.push_back(chan);
        return static_cast<int>(clip.channels.size()) - 1;
    }

    void AnimationTimeline::SortChannelKeys(AnimationChannel &chan)
    {
        auto byTime = [](const auto &a, const auto &b)
        { return a.time < b.time; };
        std::sort(chan.positionKeys.begin(), chan.positionKeys.end(), byTime);
        std::sort(chan.rotationKeys.begin(), chan.rotationKeys.end(), byTime);
        std::sort(chan.scaleKeys.begin(), chan.scaleKeys.end(), byTime);
    }

    float AnimationTimeline::KeyTime(const AnimationClip &clip, const KeyRef &ref) const
    {
        if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
            return 0.f;
        const AnimationChannel &chan = clip.channels[ref.channelIdx];
        switch (ref.type)
        {
        case KeyType::Position:
            return ref.keyIdx < static_cast<int>(chan.positionKeys.size()) ? chan.positionKeys[ref.keyIdx].time : 0.f;
        case KeyType::Rotation:
            return ref.keyIdx < static_cast<int>(chan.rotationKeys.size()) ? chan.rotationKeys[ref.keyIdx].time : 0.f;
        case KeyType::Scale:
            return ref.keyIdx < static_cast<int>(chan.scaleKeys.size()) ? chan.scaleKeys[ref.keyIdx].time : 0.f;
        }
        return 0.f;
    }

    float AnimationTimeline::KeyFrame(const AnimationClip &clip, const KeyRef &ref) const
    {
        return ToFrame(KeyTime(clip, ref));
    }

    void AnimationTimeline::SetKeyTime(AnimationClip &clip, const KeyRef &ref, float time)
    {
        if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
            return;
        AnimationChannel &chan = clip.channels[ref.channelIdx];
        switch (ref.type)
        {
        case KeyType::Position:
            if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                chan.positionKeys[ref.keyIdx].time = time;
            break;
        case KeyType::Rotation:
            if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                chan.rotationKeys[ref.keyIdx].time = time;
            break;
        case KeyType::Scale:
            if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                chan.scaleKeys[ref.keyIdx].time = time;
            break;
        }
    }

    // Sorting invalidates key indices; rebuild the selection by (channel, type, time).
    void AnimationTimeline::SortAndRemapSelection(AnimationClip &clip)
    {
        struct Saved
        {
            int channelIdx;
            KeyType type;
            float time;
        };
        std::vector<Saved> saved;
        saved.reserve(m_selectedKeys.size());
        for (const KeyRef &ref : m_selectedKeys)
            saved.push_back({ref.channelIdx, ref.type, KeyTime(clip, ref)});
        for (AnimationChannel &chan : clip.channels)
            SortChannelKeys(chan);
        SelClear();
        for (const Saved &sv : saved)
        {
            const Saved &s = sv;
            if (s.channelIdx < 0 || s.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            const AnimationChannel &chan = clip.channels[s.channelIdx];
            int idx = -1;
            switch (s.type)
            {
            case KeyType::Position:
                idx = FindKeyAtTime(chan.positionKeys, s.time);
                break;
            case KeyType::Rotation:
                idx = FindKeyAtTime(chan.rotationKeys, s.time);
                break;
            case KeyType::Scale:
                idx = FindKeyAtTime(chan.scaleKeys, s.time);
                break;
            }
            if (idx >= 0)
                SelAdd({s.channelIdx, s.type, idx});
        }
    }

    uint64_t AnimationTimeline::PackKey(const KeyRef &ref)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(ref.channelIdx)) << 34) |
               (static_cast<uint64_t>(static_cast<int>(ref.type)) << 32) | static_cast<uint32_t>(ref.keyIdx);
    }

    bool AnimationTimeline::IsKeySelected(const KeyRef &ref) const
    {
        return m_selectedSet.count(PackKey(ref)) != 0;
    }

    void AnimationTimeline::SelClear()
    {
        m_selectedKeys.clear();
        m_selectedSet.clear();
    }

    void AnimationTimeline::SelAdd(const KeyRef &ref)
    {
        if (m_selectedSet.insert(PackKey(ref)).second)
            m_selectedKeys.push_back(ref);
    }

    void AnimationTimeline::SelErase(const KeyRef &ref)
    {
        if (m_selectedSet.erase(PackKey(ref)))
            m_selectedKeys.erase(std::remove(m_selectedKeys.begin(), m_selectedKeys.end(), ref), m_selectedKeys.end());
    }

    void AnimationTimeline::SelectKey(const KeyRef &ref, bool additive)
    {
        if (!additive)
            SelClear();
        SelAdd(ref);
    }

    void AnimationTimeline::SelectAllKeys(const AnimationClip &clip, bool select)
    {
        SelClear();
        if (!select)
            return;
        for (int ci = 0; ci < static_cast<int>(clip.channels.size()); ci++)
        {
            const AnimationChannel &chan = clip.channels[ci];
            for (int k = 0; k < static_cast<int>(chan.positionKeys.size()); k++)
                SelAdd({ci, KeyType::Position, k});
            for (int k = 0; k < static_cast<int>(chan.rotationKeys.size()); k++)
                SelAdd({ci, KeyType::Rotation, k});
            for (int k = 0; k < static_cast<int>(chan.scaleKeys.size()); k++)
                SelAdd({ci, KeyType::Scale, k});
        }
    }

    void AnimationTimeline::DeleteSelectedKeys(AnimationClip &clip)
    {
        if (m_selectedKeys.empty())
            return;
        PushUndo(clip);
        std::vector<KeyRef> dels = m_selectedKeys;
        std::sort(dels.begin(), dels.end(), [](const KeyRef &a, const KeyRef &b)
                  {
                      if (a.channelIdx != b.channelIdx)
                          return a.channelIdx > b.channelIdx;
                      if (a.type != b.type)
                          return a.type > b.type;
                      return a.keyIdx > b.keyIdx; });
        for (const KeyRef &d : dels)
        {
            if (d.channelIdx < 0 || d.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            AnimationChannel &chan = clip.channels[d.channelIdx];
            auto erase = [&](auto &vec)
            {
                if (d.keyIdx >= 0 && d.keyIdx < static_cast<int>(vec.size()))
                    vec.erase(vec.begin() + d.keyIdx);
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
        SelClear();
    }

    void AnimationTimeline::CopySelectedKeys(const AnimationClip &clip)
    {
        m_clipboard.clear();
        if (m_selectedKeys.empty())
            return;
        float earliest = std::numeric_limits<float>::max();
        for (const KeyRef &ref : m_selectedKeys)
            earliest = std::min(earliest, KeyTime(clip, ref));
        for (const KeyRef &ref : m_selectedKeys)
        {
            if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            const AnimationChannel &chan = clip.channels[ref.channelIdx];
            ClipboardEntry e;
            e.type = ref.type;
            e.channelIdx = ref.channelIdx;
            e.absTime = KeyTime(clip, ref);
            e.relTime = e.absTime - earliest;
            switch (ref.type)
            {
            case KeyType::Position:
                if (ref.keyIdx >= static_cast<int>(chan.positionKeys.size()))
                    continue;
                e.posValue = chan.positionKeys[ref.keyIdx].value;
                break;
            case KeyType::Rotation:
                if (ref.keyIdx >= static_cast<int>(chan.rotationKeys.size()))
                    continue;
                e.rotValue = chan.rotationKeys[ref.keyIdx].value;
                break;
            case KeyType::Scale:
                if (ref.keyIdx >= static_cast<int>(chan.scaleKeys.size()))
                    continue;
                e.sclValue = chan.scaleKeys[ref.keyIdx].value;
                break;
            }
            m_clipboard.push_back(e);
        }
    }

    // keepTimes: paste keys at their original frames (Duplicate); else relative to atFrame.
    void AnimationTimeline::PasteKeys(AnimationClip &clip, float atFrame, bool keepTimes)
    {
        if (m_clipboard.empty())
            return;
        PushUndo(clip);
        SelClear();
        const float atTicks = ToTicks(atFrame);
        for (const ClipboardEntry &e : m_clipboard)
        {
            if (e.channelIdx < 0 || e.channelIdx >= static_cast<int>(clip.channels.size()))
                continue;
            AnimationChannel &chan = clip.channels[e.channelIdx];
            const float t = keepTimes ? e.absTime : std::max(0.f, atTicks + e.relTime);
            switch (e.type)
            {
            case KeyType::Position:
                chan.positionKeys.push_back({t, e.posValue});
                SelAdd({e.channelIdx, KeyType::Position, static_cast<int>(chan.positionKeys.size()) - 1});
                break;
            case KeyType::Rotation:
                chan.rotationKeys.push_back({t, e.rotValue});
                SelAdd({e.channelIdx, KeyType::Rotation, static_cast<int>(chan.rotationKeys.size()) - 1});
                break;
            case KeyType::Scale:
                chan.scaleKeys.push_back({t, e.sclValue});
                SelAdd({e.channelIdx, KeyType::Scale, static_cast<int>(chan.scaleKeys.size()) - 1});
                break;
            }
        }
    }

    // Blender "Insert Keyframe" (I): keys the bone's current (evaluated) Loc/Rot/Scl at the frame.
    void AnimationTimeline::InsertKeyframe(AnimationClip &clip, int bone, float frameIn)
    {
        if (bone < 0)
            return;
        const float frame = ToTicks(frameIn);
        const int ci = EnsureChannel(clip, bone);
        AnimationChannel &chan = clip.channels[ci];
        const vec3 pos = AnimationEvaluator::InterpolatePosition(chan.positionKeys, frame);
        const quat rot = AnimationEvaluator::InterpolateRotation(chan.rotationKeys, frame);
        const vec3 scl = AnimationEvaluator::InterpolateScale(chan.scaleKeys, frame);
        if (FindKeyAtTime(chan.positionKeys, frame) < 0)
            chan.positionKeys.push_back({frame, pos});
        if (FindKeyAtTime(chan.rotationKeys, frame) < 0)
            chan.rotationKeys.push_back({frame, rot});
        if (FindKeyAtTime(chan.scaleKeys, frame) < 0)
            chan.scaleKeys.push_back({frame, scl});
        SortChannelKeys(chan);
    }

    void AnimationTimeline::DeleteKeyframesAtFrame(AnimationClip &clip, int bone, float frame)
    {
        const int ci = ChannelForBone(clip, bone);
        if (ci < 0)
            return;
        AnimationChannel &chan = clip.channels[ci];
        const float t = ToTicks(frame), tol = ToTicks(0.5f);
        auto eraseAt = [&](auto &vec)
        {
            for (int i = static_cast<int>(vec.size()) - 1; i >= 0; i--)
                if (std::abs(vec[i].time - t) < tol)
                    vec.erase(vec.begin() + i);
        };
        eraseAt(chan.positionKeys);
        eraseAt(chan.rotationKeys);
        eraseAt(chan.scaleKeys);
    }

    void AnimationTimeline::CollectKeyTimes(const AnimationClip &clip, std::vector<float> &out) const
    {
        out.clear();
        for (const AnimationChannel &chan : clip.channels)
        {
            for (const PositionKey &k : chan.positionKeys)
                out.push_back(ToFrame(k.time));
            for (const RotationKey &k : chan.rotationKeys)
                out.push_back(ToFrame(k.time));
            for (const ScaleKey &k : chan.scaleKeys)
                out.push_back(ToFrame(k.time));
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end(), [](float a, float b)
                              { return std::abs(a - b) < kFrameEps; }),
                  out.end());
    }

    bool AnimationTimeline::NextKeyFrame(const AnimationClip &clip, float from, bool forward, float &out) const
    {
        std::vector<float> times;
        CollectKeyTimes(clip, times);
        if (forward)
        {
            for (float t : times)
                if (t > from + 0.01f)
                {
                    out = t;
                    return true;
                }
        }
        else
        {
            for (auto it = times.rbegin(); it != times.rend(); ++it)
                if (*it < from - 0.01f)
                {
                    out = *it;
                    return true;
                }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // modal G / S (Blender: move with the mouse, LMB/Enter confirm, RMB/Esc cancel)
    // -------------------------------------------------------------------------
    void AnimationTimeline::BeginModal(Modal modal, const AnimationClip &clip, float anchorFrame)
    {
        if (m_selectedKeys.empty())
            return;
        m_modal = modal;
        m_modalTweak = false;
        m_modalAnchor = anchorFrame;
        m_modalDelta = 0.f;
        m_modalFactor = 1.f;
        m_modalValueDelta = 0.f;
        m_modalSnapshot = clip;
        m_modalOrigTimes.clear();
        m_modalOrigValues.clear();
        for (const KeyRef &ref : m_selectedKeys)
        {
            m_modalOrigTimes.push_back(KeyFrame(clip, ref));
            float v = 0.f;
            if (m_modalAxis >= 0 && ref.type == m_modalType && ref.channelIdx >= 0 && ref.channelIdx < static_cast<int>(clip.channels.size()))
                v = KeyComponent(clip.channels[ref.channelIdx], ref.type, ref.keyIdx, m_modalAxis);
            m_modalOrigValues.push_back(v);
        }
        if (m_mode != Mode::GraphEditor)
            m_modalAxis = -1;
        m_modalAnchorValue = m_mode == Mode::GraphEditor ? PxToValue(ImGui::GetMousePos().y) : 0.f;
    }

    void AnimationTimeline::UpdateModal(AnimationClip &clip, float mouseFrame, float playhead)
    {
        if (m_modal == Modal::Grab)
        {
            m_modalDelta = SnapFrame(mouseFrame) - SnapFrame(m_modalAnchor);
            for (size_t i = 0; i < m_selectedKeys.size() && i < m_modalOrigTimes.size(); i++)
                SetKeyTime(clip, m_selectedKeys[i], ToTicks(std::max(0.f, SnapFrame(m_modalOrigTimes[i] + m_modalDelta))));
            if (m_mode == Mode::GraphEditor && m_modalAxis >= 0)
            {
                m_modalValueDelta = PxToValue(ImGui::GetMousePos().y) - m_modalAnchorValue;
                for (size_t i = 0; i < m_selectedKeys.size() && i < m_modalOrigValues.size(); i++)
                {
                    const KeyRef &ref = m_selectedKeys[i];
                    if (ref.type != m_modalType || ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()))
                        continue;
                    SetKeyComponent(clip.channels[ref.channelIdx], ref.type, ref.keyIdx, m_modalAxis,
                                    m_modalOrigValues[i] + m_modalValueDelta * CurveHalf(ref.channelIdx, ref.type, m_modalAxis));
                }
            }
        }
        else if (m_modal == Modal::Scale)
        {
            const float denom = m_modalAnchor - playhead;
            m_modalFactor = std::abs(denom) > 1e-3f ? (mouseFrame - playhead) / denom : 1.f;
            for (size_t i = 0; i < m_selectedKeys.size() && i < m_modalOrigTimes.size(); i++)
                SetKeyTime(clip, m_selectedKeys[i], ToTicks(std::max(0.f, SnapFrame(playhead + (m_modalOrigTimes[i] - playhead) * m_modalFactor))));
        }
    }

    void AnimationTimeline::CommitModal(AnimationClip &clip)
    {
        if (m_modal == Modal::None)
            return;
        PushUndoSnapshot(m_modalSnapshot);
        SortAndRemapSelection(clip);
        m_modal = Modal::None;
        m_modalTweak = false;
    }

    void AnimationTimeline::CancelModal(AnimationClip &clip)
    {
        if (m_modal == Modal::None)
            return;
        clip = m_modalSnapshot;
        m_modal = Modal::None;
        m_modalTweak = false;
    }

    // -------------------------------------------------------------------------
    // channel rows + key glyphs
    // -------------------------------------------------------------------------
    void AnimationTimeline::BuildRows(const Skeleton &skeleton)
    {
        const int boneCount = skeleton.GetBoneCount();
        if (static_cast<int>(m_boneExpanded.size()) != boneCount)
        {
            m_boneExpanded.assign(boneCount, 0);
            m_boneSelected.assign(boneCount, 0);
            m_activeBone = std::min(m_activeBone, boneCount - 1);
        }
        m_rows.clear();
        m_rows.push_back({-1, -1});
        for (int i = 0; i < boneCount; i++)
        {
            m_rows.push_back({i, -1});
            if (m_boneExpanded[i])
                for (int t = 0; t < 3; t++)
                    m_rows.push_back({i, t});
        }
    }

    void AnimationTimeline::BuildGlyphs(const Skeleton &skeleton, const AnimationClip &clip, float keyLeft, float rowTop,
                                        float visibleTop, float visibleBottom)
    {
        (void)skeleton;
        (void)keyLeft;
        m_glyphs.clear();
        m_glyphRefs.clear();

        struct Entry
        {
            float time;
            KeyRef ref;
        };
        std::vector<Entry> entries;
        std::vector<Entry> summary;

        auto emitRow = [&](int rowIdx, std::vector<Entry> &list, float size)
        {
            if (list.empty())
                return;
            std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b)
                      { return a.time < b.time; });
            const float y = rowTop + rowIdx * kRowHeight + kRowHeight * 0.5f;
            size_t i = 0;
            while (i < list.size())
            {
                size_t j = i;
                while (j < list.size() && std::abs(list[j].time - list[i].time) < kFrameEps)
                    j++;
                const float t = list[i].time;
                if (t >= m_viewStart - 1.f && t <= m_viewEnd + 1.f)
                {
                    Glyph g;
                    g.x = FrameToPx(t);
                    g.y = y;
                    g.size = size;
                    g.time = t;
                    g.row = rowIdx;
                    g.refBegin = static_cast<int>(m_glyphRefs.size());
                    for (size_t k = i; k < j; k++)
                    {
                        m_glyphRefs.push_back(list[k].ref);
                        g.selected = g.selected || IsKeySelected(list[k].ref);
                    }
                    g.refCount = static_cast<int>(j - i);
                    m_glyphs.push_back(g);
                }
                i = j;
            }
        };

        for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
        {
            const Row &row = m_rows[r];
            if (row.bone < 0)
                continue;
            const int ci = ChannelForBone(clip, row.bone);
            if (ci < 0)
                continue;
            const AnimationChannel &chan = clip.channels[ci];
            const float rowY = rowTop + r * kRowHeight;
            const bool visible = rowY + kRowHeight >= visibleTop && rowY <= visibleBottom;
            entries.clear();
            if (row.type < 0 || row.type == 0)
                for (int k = 0; k < static_cast<int>(chan.positionKeys.size()); k++)
                    entries.push_back({ToFrame(chan.positionKeys[k].time), {ci, KeyType::Position, k}});
            if (row.type < 0 || row.type == 1)
                for (int k = 0; k < static_cast<int>(chan.rotationKeys.size()); k++)
                    entries.push_back({ToFrame(chan.rotationKeys[k].time), {ci, KeyType::Rotation, k}});
            if (row.type < 0 || row.type == 2)
                for (int k = 0; k < static_cast<int>(chan.scaleKeys.size()); k++)
                    entries.push_back({ToFrame(chan.scaleKeys[k].time), {ci, KeyType::Scale, k}});
            if (row.type < 0)
                summary.insert(summary.end(), entries.begin(), entries.end());
            if (visible)
                emitRow(r, entries, row.type < 0 ? 5.f : 4.f);
        }
        if (rowTop + kRowHeight >= visibleTop && rowTop <= visibleBottom)
            emitRow(0, summary, 6.f);
    }

    void AnimationTimeline::ScrollWheel(float visibleHeight, float contentHeight)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f && !ImGui::GetIO().KeyCtrl)
            m_scrollY -= wheel * kRowHeight * 3.f;
        m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, contentHeight - visibleHeight));
    }

    // Thin Blender-style scrollbar on the right edge of a region; drag the thumb or wheel to scroll.
    void AnimationTimeline::DrawVScrollbar(const ImVec2 &origin, const ImVec2 &size, float contentHeight)
    {
        const float visible = size.y - kRulerHeight;
        if (contentHeight <= visible || visible <= 0.f)
        {
            m_scrollDragging = false;
            return;
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float x0 = origin.x + size.x - kScrollbarWidth - 2.f, x1 = origin.x + size.x - 2.f;
        const float y0 = origin.y + kRulerHeight + 2.f, y1 = origin.y + size.y - 2.f;
        const float trackH = y1 - y0;
        const float thumbH = std::max(trackH * visible / contentHeight, 18.f);
        const float maxScroll = contentHeight - visible;
        const float thumbY = y0 + (trackH - thumbH) * (m_scrollY / maxScroll);
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool onThumb = mouse.x >= x0 && mouse.x <= x1 && mouse.y >= thumbY && mouse.y <= thumbY + thumbH;
        if (onThumb && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            m_scrollDragging = true;
            m_scrollDragOffset = mouse.y - thumbY;
        }
        if (m_scrollDragging)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_scrollDragging = false;
            else
                m_scrollY = std::clamp((mouse.y - m_scrollDragOffset - y0) / std::max(trackH - thumbH, 1.f) * maxScroll, 0.f, maxScroll);
        }
        dl->AddRectFilled({x0, y0}, {x1, y1}, IM_COL32(0, 0, 0, 70), 4.f);
        dl->AddRectFilled({x0, thumbY}, {x1, thumbY + thumbH}, (onThumb || m_scrollDragging) ? IM_COL32(150, 150, 150, 255) : IM_COL32(105, 105, 105, 255), 4.f);
    }

    // Blender's bottom view scroller: the thumb is the visible frame range; drag it to pan, drag either
    // end grip to zoom that side. The track covers the clip range or the view, whichever is wider.
    void AnimationTimeline::DrawHScrollbar(const ImVec2 &origin, const ImVec2 &size, float durationFrames)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float pad = std::max(durationFrames * 0.03f, 1.f);
        const float lo = std::min(-pad, m_viewStart), hi = std::max(durationFrames + pad, m_viewEnd);
        const float x0 = m_keyLeft, x1 = m_keyLeft + m_keyWidth;
        const float y0 = origin.y + 3.f, y1 = origin.y + size.y - 3.f;
        const float framesPerPx = (hi - lo) / std::max(x1 - x0, 1.f);
        constexpr float kGrip = 7.f;
        float tx0 = x0 + (m_viewStart - lo) / framesPerPx, tx1 = x0 + (m_viewEnd - lo) / framesPerPx;
        if (tx1 - tx0 < kGrip * 3.f)
        {
            const float c = (tx0 + tx1) * 0.5f;
            tx0 = c - kGrip * 1.5f;
            tx1 = c + kGrip * 1.5f;
        }
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool inBar = mouse.x >= x0 - kGrip && mouse.x <= x1 + kGrip && mouse.y >= origin.y && mouse.y <= origin.y + size.y &&
                           ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        int hot = 0;
        if (inBar)
            hot = mouse.x < tx0 - 2.f || mouse.x > tx1 + 2.f ? 0 : mouse.x < tx0 + kGrip ? 2
                                                               : mouse.x > tx1 - kGrip   ? 3
                                                                                         : 1;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_hDrag = hot;
        if (m_hDrag)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_hDrag = 0;
            else
            {
                const float df = ImGui::GetIO().MouseDelta.x * framesPerPx;
                if (m_hDrag == 1)
                {
                    m_viewStart += df;
                    m_viewEnd += df;
                }
                else if (m_hDrag == 2)
                    m_viewStart = std::min(m_viewStart + df, m_viewEnd - kMinViewFrames);
                else
                    m_viewEnd = std::max(m_viewEnd + df, m_viewStart + kMinViewFrames);
            }
        }
        const int active = m_hDrag ? m_hDrag : hot;
        if (active >= 2)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        dl->AddRectFilled({x0, y0}, {x1, y1}, IM_COL32(0, 0, 0, 70), 4.f);
        dl->AddRectFilled({tx0, y0}, {tx1, y1}, active == 1 ? IM_COL32(150, 150, 150, 255) : IM_COL32(105, 105, 105, 255), 4.f);
        const ImU32 gripL = active == 2 ? IM_COL32(230, 230, 230, 255) : IM_COL32(60, 60, 60, 255);
        const ImU32 gripR = active == 3 ? IM_COL32(230, 230, 230, 255) : IM_COL32(60, 60, 60, 255);
        dl->AddLine({tx0 + 3.f, y0 + 2.f}, {tx0 + 3.f, y1 - 2.f}, gripL, 1.5f);
        dl->AddLine({tx1 - 3.f, y0 + 2.f}, {tx1 - 3.f, y1 - 2.f}, gripR, 1.5f);
    }

    // -------------------------------------------------------------------------
    // header (Blender timeline header: mode, action, transport, frame, range, options)
    // -------------------------------------------------------------------------
    bool AnimationTimeline::DrawHeader(Scene &scene, AnimationSystem *anim, ModelAsset *model, AnimationClip &clip,
                                       float currentFrame)
    {
        const size_t clipCountBefore = model->GetAnimations().size();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(p, {p.x + w, p.y + kHeaderHeight * 2.f}, bl::kHeaderBg);
        dl->AddLine({p.x, p.y + kHeaderHeight - 0.5f}, {p.x + w, p.y + kHeaderHeight - 0.5f}, IM_COL32(0, 0, 0, 90));
        ImGui::SetCursorScreenPos({p.x + 6.f, p.y + 4.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f, 3.f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.f, 4.f});

        // Editor mode
        ImGui::SetNextItemWidth(118.f);
        const char *modeNames[] = {"Dope Sheet", "Graph Editor"};
        int modeIdx = static_cast<int>(m_mode);
        if (ImGui::Combo("##mode", &modeIdx, modeNames, 2))
        {
            m_mode = static_cast<Mode>(modeIdx);
            m_fitPending = true;
        }
        ui::ItemTooltip("Editor mode: Dope Sheet (keyframes per channel) or Graph Editor (F-curves).");

        // Action (clip) selector + management
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.f);
        const auto &clips = model->GetAnimations();
        const char *clipName = clip.name.empty() ? "<unnamed>" : clip.name.c_str();
        if (ImGui::BeginCombo("##action", clipName))
        {
            for (int i = 0; i < static_cast<int>(clips.size()); i++)
            {
                const bool sel = i == m_selectedClip;
                if (ImGui::Selectable(clips[i].name.empty() ? "<unnamed>" : clips[i].name.c_str(), sel))
                {
                    if (i != m_selectedClip)
                    {
                        m_selectedClip = i;
                        ResetEditState();
                        SetFrame(scene, anim, 0.f);
                    }
                }
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Active action (animation clip) being edited.");
        ImGui::SameLine();
        if (ImGui::Button("+##newclip"))
        {
            snprintf(m_nameBuf, sizeof(m_nameBuf), "Action.%03d", static_cast<int>(clips.size()) + 1);
            ImGui::OpenPopup("New Action");
        }
        ui::ItemTooltip("New action: an empty clip with the same frame rate.");
        ImGui::SameLine();
        if (ImGui::Button("Dup##dupclip"))
        {
            AnimationClip copy = clip;
            copy.name = clip.name + ".001";
            model->GetMutableAnimations().push_back(copy);
            m_selectedClip = static_cast<int>(model->GetAnimations().size()) - 1;
            ResetEditState();
            m_dirty = true;
            SetFrame(scene, anim, currentFrame);
        }
        ui::ItemTooltip("Duplicate the active action.");
        ImGui::SameLine();
        if (ImGui::Button("Ren##renclip"))
        {
            snprintf(m_nameBuf, sizeof(m_nameBuf), "%s", clip.name.c_str());
            ImGui::OpenPopup("Rename Action");
        }
        ui::ItemTooltip("Rename the active action.");
        ImGui::SameLine();
        ImGui::BeginDisabled(clips.size() <= 1);
        if (ImGui::Button("X##delclip"))
            ImGui::OpenPopup("Delete Action");
        ImGui::EndDisabled();
        ui::ItemTooltip("Delete the active action (the model keeps at least one).");
        DrawClipPopups(scene, anim, model);
        if (model->GetAnimations().size() != clipCountBefore)
        {
            // The clip vector was resized: every clip reference this frame is stale.
            ImGui::PopStyleVar(2);
            ImGui::SetCursorScreenPos({p.x, p.y + kHeaderHeight * 2.f});
            return true;
        }

        // Transport
        ImGui::SameLine(0.f, 14.f);
        const bool playing = IsPlaying(anim);
        if (IconButton("##jumpstart", Icon::JumpStart, "Jump to start (Shift+Left)"))
            SetFrame(scene, anim, 0.f);
        ImGui::SameLine();
        if (IconButton("##prevkey", Icon::PrevKey, "Jump to previous keyframe (Down)"))
        {
            float f;
            if (NextKeyFrame(clip, currentFrame, false, f))
                SetFrame(scene, anim, f);
        }
        ImGui::SameLine();
        if (IconButton("##playrev", Icon::PlayReverse, "Play reverse (Shift+Ctrl+Space)", playing && m_speed < 0.f))
        {
            const bool wasReverse = playing && m_speed < 0.f;
            m_speed = -std::abs(m_speed);
            SetPlaying(scene, anim, !wasReverse, true);
        }
        ImGui::SameLine();
        if (IconButton("##play", playing ? Icon::Pause : Icon::Play, playing ? "Pause (Space)" : "Play (Space)", playing && m_speed > 0.f))
        {
            m_speed = std::abs(m_speed);
            SetPlaying(scene, anim, !playing, false);
        }
        ImGui::SameLine();
        if (IconButton("##nextkey", Icon::NextKey, "Jump to next keyframe (Up)"))
        {
            float f;
            if (NextKeyFrame(clip, currentFrame, true, f))
                SetFrame(scene, anim, f);
        }
        ImGui::SameLine();
        if (IconButton("##jumpend", Icon::JumpEnd, "Jump to end (Shift+Right)"))
            SetFrame(scene, anim, ToFrame(clip.duration));

        // Current frame / range / fps (wrap to the second row when the window is narrow)
        const bool narrow = w < 1010.f;
        if (narrow)
            ImGui::SetCursorScreenPos({p.x + 6.f, p.y + kHeaderHeight + 4.f});
        else
            ImGui::SameLine(0.f, 14.f);
        ImGui::SetNextItemWidth(70.f);
        const float durationFrames = ToFrame(clip.duration);
        int frame = static_cast<int>(std::round(currentFrame));
        if (ImGui::DragInt("##frame", &frame, 0.2f, 0, static_cast<int>(durationFrames), "%d"))
            SetFrame(scene, anim, static_cast<float>(frame));
        ui::ItemTooltip("Current frame.");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "Start");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(46.f);
        int start = 0;
        ImGui::BeginDisabled();
        ImGui::DragInt("##start", &start, 1.f, 0, 0);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "End");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.f);
        int end = static_cast<int>(std::round(durationFrames));
        if (ImGui::DragInt("##end", &end, 0.5f, 1, 1000000, "%d"))
        {
            PushUndo(clip);
            clip.duration = ToTicks(static_cast<float>(std::max(end, 1)));
        }
        ui::ItemTooltip("End frame of the action (playback range).");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "FPS");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(52.f);
        float fps = clip.ticksPerSecond / m_frameTicks;
        if (ImGui::DragFloat("##fps", &fps, 0.1f, 1.f, 1000.f, "%.1f"))
        {
            PushUndo(clip);
            clip.ticksPerSecond = std::max(fps, 1.f) * m_frameTicks;
        }
        ui::ItemTooltip("Playback frames per second of this action (frame grid stays fixed).");

        // Options: auto-key, snap, loop, speed (second header row)
        if (narrow)
            ImGui::SameLine(0.f, 14.f);
        else
            ImGui::SetCursorScreenPos({p.x + 6.f, p.y + kHeaderHeight + 4.f});
        // ponytail: auto-key arms viewport bone posing, which lands with the Rig Editor's bone overlay; inert until then.
        IconButton("##autokey", Icon::Record, "Auto keying (needs viewport bone posing - next drop).", false);
        ImGui::SameLine();
        if (ImGui::Checkbox("Snap", &m_snap))
        {
        }
        ui::ItemTooltip("Snap keys to whole frames while moving (hold Ctrl to invert).");
        ImGui::SameLine();
        if (ImGui::Checkbox("Loop", &m_loop))
            for (NodeId *n : m_animatedNodes)
                anim->SetLoop(n, m_loop);
        ui::ItemTooltip("Loop playback.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(56.f);
        float speedAbs = std::abs(m_speed);
        if (ImGui::DragFloat("##speed", &speedAbs, 0.05f, 0.05f, 8.f, "%.2fx"))
        {
            m_speed = m_speed < 0.f ? -speedAbs : speedAbs;
            for (NodeId *n : m_animatedNodes)
                anim->SetSpeed(n, m_speed);
        }
        ui::ItemTooltip("Playback speed.");
        if (m_mode == Mode::GraphEditor)
        {
            ImGui::SameLine();
            if (ImGui::Checkbox("Normalize", &m_normalize))
                m_curveFitPending = true;
            ui::ItemTooltip("Show every F-curve scaled to -1..1 so curves with different units share the view.");
        }

        // Save / undo (right side of the second row)
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 14.f, w - 212.f));
        ImGui::BeginDisabled(m_undo.empty());
        if (ImGui::Button("Undo"))
        {
            Undo(clip);
            ReevaluatePose(scene, anim);
        }
        ImGui::EndDisabled();
        ui::ItemTooltip("Undo the last keyframe edit (Ctrl+Z).");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_redo.empty());
        if (ImGui::Button("Redo"))
        {
            Redo(clip);
            ReevaluatePose(scene, anim);
        }
        ImGui::EndDisabled();
        ui::ItemTooltip("Redo (Ctrl+Shift+Z).");
        ImGui::SameLine();
        const bool canSave = ModelAssetCooked::IsCookedPath(model->GetFilePath());
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button(m_dirty ? "Save *" : "Save"))
        {
            if (ModelAssetCooked::WriteToFile(model, model->GetFilePath()))
                m_dirty = false;
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(canSave ? "Write the clips back into the model's .pemesh (Ctrl+S)."
                                : "Model was not loaded from a .pemesh; cook it first to save clips.");

        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos({p.x, p.y + kHeaderHeight * 2.f});
        return false;
    }

    void AnimationTimeline::DrawClipPopups(Scene &scene, AnimationSystem *anim, ModelAsset *model)
    {
        auto &clips = model->GetMutableAnimations();
        if (ImGui::BeginPopup("New Action"))
        {
            ImGui::SetNextItemWidth(200.f);
            const bool enter = ImGui::InputText("##newname", m_nameBuf, sizeof(m_nameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::SameLine();
            if (ImGui::Button("Create") || enter)
            {
                AnimationClip fresh;
                fresh.name = m_nameBuf;
                fresh.duration = 48.f;
                fresh.ticksPerSecond = clips.empty() ? 24.f : clips[m_selectedClip].ticksPerSecond;
                clips.push_back(fresh);
                m_selectedClip = static_cast<int>(clips.size()) - 1;
                ResetEditState();
                m_dirty = true;
                SetFrame(scene, anim, 0.f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("Rename Action"))
        {
            ImGui::SetNextItemWidth(200.f);
            const bool enter = ImGui::InputText("##rename", m_nameBuf, sizeof(m_nameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::SameLine();
            if (ImGui::Button("Rename") || enter)
            {
                clips[m_selectedClip].name = m_nameBuf;
                m_dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("Delete Action"))
        {
            ImGui::Text("Delete action '%s'?", clips[m_selectedClip].name.c_str());
            if (ImGui::Button("Delete"))
            {
                for (NodeId *n : m_animatedNodes)
                    anim->StopAnimation(n);
                clips.erase(clips.begin() + m_selectedClip);
                m_selectedClip = std::clamp(m_selectedClip, 0, static_cast<int>(clips.size()) - 1);
                ResetEditState();
                m_dirty = true;
                SetFrame(scene, anim, 0.f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // -------------------------------------------------------------------------
    // channel region (left)
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawChannelRegion(const Skeleton &skeleton, const AnimationClip &clip, const ImVec2 &origin,
                                              const ImVec2 &size)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bl::kChannelBg);
        dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);

        const float rowTop = origin.y + kRulerHeight - m_scrollY;
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y}) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool ctrl = ImGui::GetIO().KeyCtrl;

        for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
        {
            const Row &row = m_rows[r];
            const float y0 = rowTop + r * kRowHeight;
            const float y1 = y0 + kRowHeight;
            if (y1 < origin.y + kRulerHeight || y0 > origin.y + size.y)
                continue;
            ImU32 bg;
            if (row.bone < 0)
                bg = bl::kSummaryRow;
            else if (row.type < 0)
                bg = m_boneSelected[row.bone] ? (row.bone == m_activeBone ? bl::kRowActive : bl::kRowSelected) : ((row.bone & 1) ? bl::kGroupRowAlt : bl::kGroupRow);
            else
                bg = bl::kSubRow;
            dl->AddRectFilled({origin.x, y0}, {origin.x + size.x, y1}, bg);
            dl->AddLine({origin.x, y1 - 0.5f}, {origin.x + size.x, y1 - 0.5f}, bl::kRowLine);

            const bool rowHovered = hovered && mouse.y >= y0 && mouse.y < y1 && mouse.y >= origin.y + kRulerHeight;
            if (rowHovered)
                dl->AddRectFilled({origin.x, y0}, {origin.x + size.x, y1}, bl::kRowHover);

            float x = origin.x + 8.f;
            const float cy = y0 + kRowHeight * 0.5f;
            if (row.bone < 0)
            {
                dl->AddText({x, cy - 7.f}, bl::kText, "Summary");
            }
            else if (row.type < 0)
            {
                int depth = 0;
                for (int parent = skeleton.bones[row.bone].parentIndex; parent >= 0 && depth < 12; parent = skeleton.bones[parent].parentIndex)
                    depth++;
                x += depth * 10.f;
                // expand arrow
                const ImVec2 a(x, cy);
                if (m_boneExpanded[row.bone])
                    dl->AddTriangleFilled({a.x - 4.f, a.y - 2.f}, {a.x + 4.f, a.y - 2.f}, {a.x, a.y + 3.f}, bl::kTextDim);
                else
                    dl->AddTriangleFilled({a.x - 2.f, a.y - 4.f}, {a.x + 3.f, a.y}, {a.x - 2.f, a.y + 4.f}, bl::kTextDim);
                x += 12.f;
                const int ci = ChannelForBone(clip, row.bone);
                const bool hasKeys = ci >= 0 && (!clip.channels[ci].positionKeys.empty() || !clip.channels[ci].rotationKeys.empty() || !clip.channels[ci].scaleKeys.empty());
                dl->AddText({x, cy - 7.f}, hasKeys ? bl::kText : bl::kTextDim, skeleton.bones[row.bone].name.c_str());
                if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (mouse.x < x - 2.f)
                        m_boneExpanded[row.bone] = !m_boneExpanded[row.bone];
                    else
                    {
                        if (!ctrl)
                            std::fill(m_boneSelected.begin(), m_boneSelected.end(), 0);
                        m_boneSelected[row.bone] = ctrl ? !m_boneSelected[row.bone] : 1;
                        m_activeBone = row.bone;
                    }
                }
                if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && mouse.x >= x - 2.f)
                    m_boneExpanded[row.bone] = !m_boneExpanded[row.bone];
            }
            else
            {
                static const char *names[] = {"Location", "Rotation", "Scale"};
                static const ImU32 cols[] = {bl::kLoc, bl::kRot, bl::kScl};
                int depth = 0;
                for (int parent = skeleton.bones[row.bone].parentIndex; parent >= 0 && depth < 12; parent = skeleton.bones[parent].parentIndex)
                    depth++;
                x += depth * 10.f + 22.f;
                dl->AddRectFilled({x, cy - 5.f}, {x + 4.f, cy + 5.f}, cols[row.type], 1.f);
                dl->AddText({x + 10.f, cy - 7.f}, bl::kText, names[row.type]);
            }
        }

        if (hovered)
            ScrollWheel(size.y - kRulerHeight, m_contentHeight);

        // Header strip above the rows (aligned with the ruler)
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + kRulerHeight}, bl::kRulerBg);
        dl->AddLine({origin.x, origin.y + kRulerHeight - 0.5f}, {origin.x + size.x, origin.y + kRulerHeight - 0.5f}, bl::kRowLine);
        dl->AddText({origin.x + 8.f, origin.y + 5.f}, bl::kTextDim, "Channels");
        dl->AddLine({origin.x + size.x - 0.5f, origin.y}, {origin.x + size.x - 0.5f, origin.y + size.y}, IM_COL32(0, 0, 0, 120));
        dl->PopClipRect();
    }

    // -------------------------------------------------------------------------
    // ruler (scrub region)
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawRuler(Scene &scene, AnimationSystem *anim, const AnimationClip &clip, float currentFrame,
                                      const ImVec2 &origin, const ImVec2 &size)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bl::kRulerBg);
        dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);

        // range shading
        const float x0 = FrameToPx(0.f), x1 = FrameToPx(ToFrame(clip.duration));
        if (x0 > origin.x)
            dl->AddRectFilled(origin, {x0, origin.y + size.y}, bl::kOutsideRange);
        if (x1 < origin.x + size.x)
            dl->AddRectFilled({x1, origin.y}, {origin.x + size.x, origin.y + size.y}, bl::kOutsideRange);

        const float framesPerPx = (m_viewEnd - m_viewStart) / std::max(m_keyWidth, 1.f);
        const float step = NiceFrameStep(framesPerPx, 70.f);
        const float minor = step / (step >= 10.f ? 5.f : 2.f);
        for (float f = std::floor(m_viewStart / minor) * minor; f <= m_viewEnd; f += minor)
        {
            const float x = FrameToPx(f);
            const bool major = std::abs(std::fmod(f, step)) < 1e-3f || std::abs(std::fmod(f, step) - step) < 1e-3f;
            dl->AddLine({x, origin.y + size.y - (major ? 10.f : 5.f)}, {x, origin.y + size.y}, major ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 50));
            if (major)
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(f)));
                dl->AddText({x + 3.f, origin.y + 3.f}, bl::kTextDim, buf);
            }
        }
        dl->AddLine({origin.x, origin.y + size.y - 0.5f}, {origin.x + size.x, origin.y + size.y - 0.5f}, bl::kRowLine);

        // playhead badge
        {
            const float x = FrameToPx(currentFrame);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(currentFrame)));
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            const float bw = ts.x + 12.f;
            dl->AddRectFilled({x - bw * 0.5f, origin.y + 3.f}, {x + bw * 0.5f, origin.y + size.y - 3.f}, bl::kPlayheadBadge, 3.f);
            dl->AddText({x - ts.x * 0.5f, origin.y + (size.y - ts.y) * 0.5f}, IM_COL32(255, 255, 255, 255), buf);
        }
        dl->PopClipRect();

        // scrub: press in the ruler, drag anywhere
        const bool hovered = ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y}) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_modal == Modal::None)
            m_scrubbing = true;
        if (hovered)
            NavigateView(ImGui::GetMousePos(), false);
        if (m_scrubbing)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_scrubbing = false;
            else
            {
                const float f = std::clamp(SnapFrame(PxToFrame(ImGui::GetMousePos().x)), 0.f, ToFrame(clip.duration));
                if (std::abs(f - currentFrame) > 1e-4f)
                    SetFrame(scene, anim, f);
            }
        }
    }

    // -------------------------------------------------------------------------
    // key area
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawKeyArea(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                                        float currentFrame, const ImVec2 &origin, const ImVec2 &size)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        dl->AddRectFilled(origin, end, bl::kKeyAreaBg);
        dl->PushClipRect(origin, end, true);

        const float rowTop = origin.y - m_scrollY;
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsMouseHoveringRect(origin, end) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const ImGuiIO &io = ImGui::GetIO();

        // row backgrounds
        for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
        {
            const Row &row = m_rows[r];
            const float y0 = rowTop + r * kRowHeight, y1 = y0 + kRowHeight;
            if (y1 < origin.y || y0 > end.y)
                continue;
            ImU32 bg = 0;
            if (row.bone < 0)
                bg = IM_COL32(255, 255, 255, 14);
            else if (row.type < 0)
                bg = m_boneSelected[row.bone] ? IM_COL32(86, 128, 194, 34) : ((row.bone & 1) ? IM_COL32(255, 255, 255, 6) : 0);
            else
                bg = IM_COL32(0, 0, 0, 22);
            if (bg)
                dl->AddRectFilled({origin.x, y0}, {end.x, y1}, bg);
            dl->AddLine({origin.x, y1 - 0.5f}, {end.x, y1 - 0.5f}, bl::kRowLine);
        }

        // range shading + grid
        const float rx0 = FrameToPx(0.f), rx1 = FrameToPx(ToFrame(clip.duration));
        if (rx0 > origin.x)
            dl->AddRectFilled(origin, {rx0, end.y}, bl::kOutsideRange);
        if (rx1 < end.x)
            dl->AddRectFilled({rx1, origin.y}, end, bl::kOutsideRange);
        const float framesPerPx = (m_viewEnd - m_viewStart) / std::max(m_keyWidth, 1.f);
        const float step = NiceFrameStep(framesPerPx, 70.f);
        const float minor = step / (step >= 10.f ? 5.f : 2.f);
        for (float f = std::floor(m_viewStart / minor) * minor; f <= m_viewEnd; f += minor)
        {
            const float x = FrameToPx(f);
            const bool major = std::abs(std::fmod(f, step)) < 1e-3f || std::abs(std::fmod(f, step) - step) < 1e-3f;
            dl->AddLine({x, origin.y}, {x, end.y}, major ? bl::kGridMajor : bl::kGridMinor);
        }

        // glyphs
        BuildGlyphs(skeleton, clip, origin.x, rowTop, origin.y, end.y);
        int hoveredGlyph = -1;
        if (hovered && m_modal == Modal::None)
        {
            float best = kKeyHitRadius + 1.f;
            for (int i = 0; i < static_cast<int>(m_glyphs.size()); i++)
            {
                const float d = std::max(std::abs(mouse.x - m_glyphs[i].x), std::abs(mouse.y - m_glyphs[i].y));
                if (d <= kKeyHitRadius && d < best)
                {
                    best = d;
                    hoveredGlyph = i;
                }
            }
        }
        for (int i = 0; i < static_cast<int>(m_glyphs.size()); i++)
        {
            const Glyph &g = m_glyphs[i];
            if (g.x < origin.x - 8.f || g.x > end.x + 8.f)
                continue;
            const ImU32 fill = g.selected ? bl::kKeyFillSel : bl::kKeyFill;
            const ImU32 border = i == hoveredGlyph ? bl::kKeyHover : (g.selected ? bl::kKeyBorderSel : bl::kKeyBorder);
            DrawDiamond(dl, {g.x, g.y}, g.size + (i == hoveredGlyph ? 1.f : 0.f), fill, border);
        }

        // playhead (draggable: hovering within a few pixels grabs it instead of selecting keys)
        const float playheadX = FrameToPx(currentFrame);
        const bool onPlayhead = hovered && m_modal == Modal::None && !m_boxSelecting && !m_pressOnKey &&
                                std::abs(mouse.x - playheadX) <= 5.f;
        if (onPlayhead)
            hoveredGlyph = -1; // the playhead band wins: dense rows have a diamond under every pixel
        dl->AddLine({playheadX, origin.y}, {playheadX, end.y}, bl::kPlayhead, (onPlayhead || m_scrubbing) ? 3.f : 2.f);
        if (onPlayhead || m_scrubbing)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (onPlayhead && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_scrubbing = true;

        // box select rectangle
        if (m_boxSelecting)
        {
            const ImVec2 a(std::min(m_boxStart.x, m_boxEnd.x), std::min(m_boxStart.y, m_boxEnd.y));
            const ImVec2 b(std::max(m_boxStart.x, m_boxEnd.x), std::max(m_boxStart.y, m_boxEnd.y));
            dl->AddRectFilled(a, b, bl::kBoxSelectFill);
            dl->AddRect(a, b, bl::kBoxSelect, 0.f, 0, 1.f);
        }
        dl->PopClipRect();

        // ---- interaction ----
        auto selectGlyph = [&](int gi, bool additive, bool toggle)
        {
            const Glyph &g = m_glyphs[gi];
            if (!additive)
                SelClear();
            for (int k = 0; k < g.refCount; k++)
            {
                const KeyRef &ref = m_glyphRefs[g.refBegin + k];
                if (toggle && g.selected)
                    SelErase(ref);
                else
                    SelAdd(ref);
            }
        };

        const float mouseFrame = PxToFrame(mouse.x);

        if (m_modal != Modal::None)
        {
            UpdateModal(clip, mouseFrame, currentFrame);
            const bool confirm = m_modalTweak ? !ImGui::IsMouseDown(ImGuiMouseButton_Left)
                                              : (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter));
            const bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (cancel)
            {
                CancelModal(clip);
                ReevaluatePose(scene, anim);
            }
            else if (confirm)
            {
                CommitModal(clip);
                ReevaluatePose(scene, anim);
            }
            else
                ReevaluatePose(scene, anim);
        }
        else if (hovered && !m_scrubbing)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (hoveredGlyph >= 0)
                {
                    const bool wasSelected = m_glyphs[hoveredGlyph].selected;
                    if (io.KeyShift)
                        selectGlyph(hoveredGlyph, true, true);
                    else if (!wasSelected)
                        selectGlyph(hoveredGlyph, false, false);
                    m_pressOnKey = true;
                    m_pressPos = mouse;
                }
                else
                {
                    if (!io.KeyShift)
                        SelClear();
                    m_boxSelecting = true;
                    m_boxStart = mouse;
                    m_boxEnd = mouse;
                }
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredGlyph < 0)
            {
                const int r = static_cast<int>((mouse.y - rowTop) / kRowHeight);
                if (r >= 0 && r < static_cast<int>(m_rows.size()) && m_rows[r].bone >= 0)
                {
                    PushUndo(clip);
                    InsertKeyframe(clip, m_rows[r].bone, std::clamp(SnapFrame(mouseFrame), 0.f, ToFrame(clip.duration)));
                    ReevaluatePose(scene, anim);
                }
                m_boxSelecting = false;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                if (hoveredGlyph >= 0 && !m_glyphs[hoveredGlyph].selected)
                    selectGlyph(hoveredGlyph, false, false);
                ImGui::OpenPopup("##keyContext");
            }
        }

        // tweak: dragging a pressed key starts a Grab that commits on release
        if (m_pressOnKey)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_pressOnKey = false;
            else if (std::abs(mouse.x - m_pressPos.x) > 3.f && m_modal == Modal::None && !m_selectedKeys.empty())
            {
                BeginModal(Modal::Grab, clip, PxToFrame(m_pressPos.x));
                m_modalTweak = true;
                m_pressOnKey = false;
            }
        }

        // box select
        if (m_boxSelecting)
        {
            m_boxEnd = mouse;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float x0 = std::min(m_boxStart.x, m_boxEnd.x), x1 = std::max(m_boxStart.x, m_boxEnd.x);
                const float y0 = std::min(m_boxStart.y, m_boxEnd.y), y1 = std::max(m_boxStart.y, m_boxEnd.y);
                if (x1 - x0 > 2.f || y1 - y0 > 2.f)
                    for (int i = 0; i < static_cast<int>(m_glyphs.size()); i++)
                    {
                        const Glyph &g = m_glyphs[i];
                        if (g.x >= x0 && g.x <= x1 && g.y >= y0 && g.y <= y1)
                            selectGlyph(i, true, false);
                    }
                m_boxSelecting = false;
            }
        }

        // context menu
        if (ImGui::BeginPopup("##keyContext"))
        {
            const int selCount = static_cast<int>(m_selectedKeys.size());
            ImGui::TextDisabled("%d key(s) selected", selCount);
            ImGui::Separator();
            if (ImGui::MenuItem("Insert Keyframe", "I", false, m_activeBone >= 0))
            {
                PushUndo(clip);
                for (int b = 0; b < static_cast<int>(m_boneSelected.size()); b++)
                    if (m_boneSelected[b])
                        InsertKeyframe(clip, b, std::round(currentFrame));
                ReevaluatePose(scene, anim);
            }
            if (ImGui::MenuItem("Delete Keyframes", "X", false, selCount > 0))
            {
                DeleteSelectedKeys(clip);
                ReevaluatePose(scene, anim);
            }
            if (ImGui::MenuItem("Duplicate", "Shift+D", false, selCount > 0))
            {
                CopySelectedKeys(clip);
                PasteKeys(clip, 0.f, true);
                BeginModal(Modal::Grab, clip, mouseFrame);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, selCount > 0))
                CopySelectedKeys(clip);
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty()))
            {
                PasteKeys(clip, std::round(currentFrame), false);
                SortAndRemapSelection(clip);
                ReevaluatePose(scene, anim);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "A"))
                SelectAllKeys(clip, true);
            if (ImGui::MenuItem("Deselect All", "Alt+A"))
                SelectAllKeys(clip, false);
            if (ImGui::MenuItem("Frame All", "Home"))
                FrameAll();
            if (ImGui::MenuItem("Frame Selected", "Numpad ."))
                FrameSelected(clip);
            ImGui::EndPopup();
        }

        if (hovered)
            NavigateView(mouse, false);
        m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, m_contentHeight - size.y));
    }

    // -------------------------------------------------------------------------
    // hotkeys (Blender defaults)
    // -------------------------------------------------------------------------
    void AnimationTimeline::HandleHotkeys(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                                          float currentFrame)
    {
        (void)skeleton;
        if (!HotkeysAllowed() || m_modal != Modal::None)
            return;
        const ImGuiIO &io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
        const float mouseFrame = PxToFrame(ImGui::GetMousePos().x);

        if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            if (ctrl && shift)
            {
                m_speed = -std::abs(m_speed);
                SetPlaying(scene, anim, !IsPlaying(anim), true);
            }
            else
            {
                m_speed = std::abs(m_speed);
                SetPlaying(scene, anim, !IsPlaying(anim), false);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            SetFrame(scene, anim, shift ? 0.f : std::max(0.f, std::round(currentFrame) - 1.f));
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            SetFrame(scene, anim, shift ? ToFrame(clip.duration) : std::min(ToFrame(clip.duration), std::round(currentFrame) + 1.f));
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            float f;
            if (NextKeyFrame(clip, currentFrame, ImGui::IsKeyPressed(ImGuiKey_UpArrow), f))
                SetFrame(scene, anim, f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        {
            FrameAll();
            m_curveFitPending = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false))
            FrameSelected(clip);
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        {
            const float factor = ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ? 0.8f : 1.25f;
            ZoomRange(m_viewStart, m_viewEnd, (m_viewStart + m_viewEnd) * 0.5f, factor, kMinViewFrames);
            if (m_mode == Mode::GraphEditor)
                ZoomRange(m_curveMin, m_curveMax, (m_curveMin + m_curveMax) * 0.5f, factor, 1e-4f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false))
            SelectAllKeys(clip, !alt);
        if (ImGui::IsKeyPressed(ImGuiKey_G, false) && !m_selectedKeys.empty())
            BeginModal(Modal::Grab, clip, mouseFrame);
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !ctrl && !m_selectedKeys.empty())
            BeginModal(Modal::Scale, clip, mouseFrame);
        if ((ImGui::IsKeyPressed(ImGuiKey_X, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) && !m_selectedKeys.empty())
        {
            DeleteSelectedKeys(clip);
            ReevaluatePose(scene, anim);
        }
        if (shift && ImGui::IsKeyPressed(ImGuiKey_D, false) && !m_selectedKeys.empty())
        {
            CopySelectedKeys(clip);
            PasteKeys(clip, 0.f, true);
            BeginModal(Modal::Grab, clip, mouseFrame);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopySelectedKeys(clip);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_clipboard.empty())
        {
            PasteKeys(clip, std::round(currentFrame), false);
            SortAndRemapSelection(clip);
            ReevaluatePose(scene, anim);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_I, false))
        {
            bool any = false;
            for (int b = 0; b < static_cast<int>(m_boneSelected.size()); b++)
                any = any || m_boneSelected[b];
            if (any || m_activeBone >= 0)
            {
                PushUndo(clip);
                if (any)
                {
                    for (int b = 0; b < static_cast<int>(m_boneSelected.size()); b++)
                        if (m_boneSelected[b])
                            alt ? DeleteKeyframesAtFrame(clip, b, std::round(currentFrame)) : InsertKeyframe(clip, b, std::round(currentFrame));
                }
                else
                    alt ? DeleteKeyframesAtFrame(clip, m_activeBone, std::round(currentFrame)) : InsertKeyframe(clip, m_activeBone, std::round(currentFrame));
                SelClear();
                ReevaluatePose(scene, anim);
            }
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            shift ? Redo(clip) : Undo(clip);
            ReevaluatePose(scene, anim);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            Redo(clip);
            ReevaluatePose(scene, anim);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && m_editModel && ModelAssetCooked::IsCookedPath(m_editModel->GetFilePath()))
        {
            if (ModelAssetCooked::WriteToFile(m_editModel, m_editModel->GetFilePath()))
                m_dirty = false;
        }
    }

    // -------------------------------------------------------------------------
    // dope sheet layout
    // -------------------------------------------------------------------------
    void AnimationTimeline::DrawDopeSheet(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                                          float currentFrame)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 size(avail.x, std::max(avail.y - kStatusHeight, 40.f));

        BuildRows(skeleton);
        m_keyLeft = origin.x + m_channelWidth;
        m_keyWidth = std::max(size.x - m_channelWidth - kRightMargin, 10.f);
        m_contentHeight = m_rows.size() * kRowHeight;
        if (m_fitPending)
        {
            const float durationFrames = ToFrame(clip.duration);
            const float pad = std::max(durationFrames * 0.03f, 1.f);
            m_viewStart = -pad;
            m_viewEnd = durationFrames + pad;
            m_fitPending = false;
        }

        ImGui::InvisibleButton("##dopesheet", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool focusClick = ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        if (focusClick)
            ImGui::SetWindowFocus();

        const ImVec2 body(size.x, size.y - kHScrollHeight);
        DrawRuler(scene, anim, clip, currentFrame, {m_keyLeft, origin.y}, {body.x - m_channelWidth, kRulerHeight});
        DrawKeyArea(scene, anim, skeleton, clip, currentFrame, {m_keyLeft, origin.y + kRulerHeight}, {body.x - m_channelWidth, body.y - kRulerHeight});
        DrawChannelRegion(skeleton, clip, origin, {m_channelWidth, body.y});
        DrawVScrollbar(origin, {m_channelWidth, body.y}, m_contentHeight);
        DrawHScrollbar({origin.x, origin.y + body.y}, {size.x, kHScrollHeight}, ToFrame(clip.duration));
        ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    }

    // -------------------------------------------------------------------------
    // graph editor (Blender F-curves: one curve per channel component, linear like the evaluator)
    // -------------------------------------------------------------------------
    float AnimationTimeline::ValueToPx(float value) const
    {
        const float span = std::max(m_curveMax - m_curveMin, 1e-6f);
        return m_curveTop + m_curveHeight - (value - m_curveMin) / span * m_curveHeight;
    }

    float AnimationTimeline::PxToValue(float px) const
    {
        const float span = std::max(m_curveMax - m_curveMin, 1e-6f);
        return m_curveMin + (m_curveTop + m_curveHeight - px) / std::max(m_curveHeight, 1.f) * span;
    }

    float AnimationTimeline::KeyComponent(const AnimationChannel &chan, KeyType type, int keyIdx, int axis)
    {
        switch (type)
        {
        case KeyType::Position:
            return keyIdx < static_cast<int>(chan.positionKeys.size()) ? chan.positionKeys[keyIdx].value[axis] : 0.f;
        case KeyType::Rotation:
        {
            if (keyIdx >= static_cast<int>(chan.rotationKeys.size()))
                return 0.f;
            const quat &q = chan.rotationKeys[keyIdx].value;
            return axis == 0 ? q.w : axis == 1 ? q.x
                                 : axis == 2   ? q.y
                                               : q.z;
        }
        case KeyType::Scale:
            return keyIdx < static_cast<int>(chan.scaleKeys.size()) ? chan.scaleKeys[keyIdx].value[axis] : 0.f;
        }
        return 0.f;
    }

    void AnimationTimeline::SetKeyComponent(AnimationChannel &chan, KeyType type, int keyIdx, int axis, float value)
    {
        switch (type)
        {
        case KeyType::Position:
            if (keyIdx < static_cast<int>(chan.positionKeys.size()))
                chan.positionKeys[keyIdx].value[axis] = value;
            break;
        case KeyType::Rotation:
            if (keyIdx < static_cast<int>(chan.rotationKeys.size()))
            {
                quat &q = chan.rotationKeys[keyIdx].value;
                (axis == 0 ? q.w : axis == 1 ? q.x
                               : axis == 2   ? q.y
                                             : q.z) = value;
                q = glm::normalize(q); // the evaluator slerps unit quaternions
            }
            break;
        case KeyType::Scale:
            if (keyIdx < static_cast<int>(chan.scaleKeys.size()))
                chan.scaleKeys[keyIdx].value[axis] = value;
            break;
        }
    }

    // Visible curves = every component of the selected bones (active bone when none is selected).
    void AnimationTimeline::CollectCurves(const AnimationClip &clip)
    {
        m_curves.clear();
        if (m_curveHidden.size() != 12)
            m_curveHidden.assign(12, 0);
        static const ImU32 axisColors[4] = {IM_COL32(240, 200, 80, 255), IM_COL32(235, 95, 95, 255), IM_COL32(120, 215, 95, 255), IM_COL32(95, 145, 240, 255)};
        bool any = false;
        for (char sel : m_boneSelected)
            any = any || sel;
        for (int b = 0; b < static_cast<int>(m_boneSelected.size()); b++)
        {
            if (any ? !m_boneSelected[b] : b != m_activeBone)
                continue;
            const int ci = ChannelForBone(clip, b);
            if (ci < 0)
                continue;
            const AnimationChannel &chan = clip.channels[ci];
            auto add = [&](KeyType type, int count, bool has)
            {
                if (!has)
                    return;
                for (int a = 0; a < count; a++)
                {
                    const int slot = static_cast<int>(type) * 4 + a;
                    if (m_curveHidden[slot])
                        continue;
                    // W X Y Z for rotation, X Y Z for the rest (colors: X red, Y green, Z blue, W yellow)
                    const ImU32 col = type == KeyType::Rotation ? axisColors[a] : axisColors[a + 1];
                    Curve curve{ci, type, a, col};
                    if (m_normalize)
                    {
                        float lo = std::numeric_limits<float>::max(), hi = -std::numeric_limits<float>::max();
                        const int n = type == KeyType::Position   ? static_cast<int>(chan.positionKeys.size())
                                      : type == KeyType::Rotation ? static_cast<int>(chan.rotationKeys.size())
                                                                  : static_cast<int>(chan.scaleKeys.size());
                        for (int k = 0; k < n; k++)
                        {
                            const float v = KeyComponent(chan, type, k, a);
                            lo = std::min(lo, v);
                            hi = std::max(hi, v);
                        }
                        curve.mid = (lo + hi) * 0.5f;
                        curve.half = std::max((hi - lo) * 0.5f, 1e-4f);
                    }
                    m_curves.push_back(curve);
                }
            };
            add(KeyType::Position, 3, !chan.positionKeys.empty());
            add(KeyType::Rotation, 4, !chan.rotationKeys.empty());
            add(KeyType::Scale, 3, !chan.scaleKeys.empty());
        }
    }

    float AnimationTimeline::CurveHalf(int channelIdx, KeyType type, int axis) const
    {
        if (!m_normalize)
            return 1.f;
        for (const Curve &c : m_curves)
            if (c.channelIdx == channelIdx && c.type == type && c.axis == axis)
                return c.half;
        return 1.f;
    }

    void AnimationTimeline::FitCurveRange(const AnimationClip &clip)
    {
        if (m_normalize)
        {
            m_curveMin = -1.15f;
            m_curveMax = 1.15f;
            m_curveFitPending = false;
            return;
        }
        float lo = std::numeric_limits<float>::max(), hi = -std::numeric_limits<float>::max();
        for (const Curve &c : m_curves)
        {
            const AnimationChannel &chan = clip.channels[c.channelIdx];
            const int n = c.type == KeyType::Position   ? static_cast<int>(chan.positionKeys.size())
                          : c.type == KeyType::Rotation ? static_cast<int>(chan.rotationKeys.size())
                                                        : static_cast<int>(chan.scaleKeys.size());
            for (int k = 0; k < n; k++)
            {
                const float v = KeyComponent(chan, c.type, k, c.axis);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        if (lo > hi)
        {
            lo = -1.f;
            hi = 1.f;
        }
        if (hi - lo < 1e-4f)
        {
            lo -= 0.5f;
            hi += 0.5f;
        }
        const float pad = (hi - lo) * 0.1f;
        m_curveMin = lo - pad;
        m_curveMax = hi + pad;
        m_curveFitPending = false;
    }

    void AnimationTimeline::DrawCurveChannels(const Skeleton &skeleton, const AnimationClip &clip, const ImVec2 &origin,
                                              const ImVec2 &size)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, bl::kChannelBg);
        dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y}) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        float y = origin.y + kRulerHeight - m_scrollY;

        // bones
        for (int b = 0; b < skeleton.GetBoneCount(); b++, y += kRowHeight)
        {
            if (y + kRowHeight < origin.y + kRulerHeight || y > origin.y + size.y)
                continue;
            const bool sel = m_boneSelected[b];
            dl->AddRectFilled({origin.x, y}, {origin.x + size.x, y + kRowHeight}, sel ? (b == m_activeBone ? bl::kRowActive : bl::kRowSelected) : ((b & 1) ? bl::kGroupRowAlt : bl::kGroupRow));
            dl->AddLine({origin.x, y + kRowHeight - 0.5f}, {origin.x + size.x, y + kRowHeight - 0.5f}, bl::kRowLine);
            int depth = 0;
            for (int parent = skeleton.bones[b].parentIndex; parent >= 0 && depth < 12; parent = skeleton.bones[parent].parentIndex)
                depth++;
            const bool hasKeys = ChannelForBone(clip, b) >= 0;
            dl->AddText({origin.x + 10.f + depth * 10.f, y + 3.f}, hasKeys ? bl::kText : bl::kTextDim, skeleton.bones[b].name.c_str());
            if (hovered && mouse.y >= y && mouse.y < y + kRowHeight && mouse.y >= origin.y + kRulerHeight && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (!ctrl)
                    std::fill(m_boneSelected.begin(), m_boneSelected.end(), 0);
                m_boneSelected[b] = ctrl ? !m_boneSelected[b] : 1;
                m_activeBone = b;
                m_curveFitPending = true;
            }
        }

        // component visibility toggles (shared by all shown bones)
        y += 6.f;
        static const char *labels[12] = {"X Location", "Y Location", "Z Location", "", "W Rotation", "X Rotation", "Y Rotation", "Z Rotation", "X Scale", "Y Scale", "Z Scale", ""};
        static const ImU32 cols[12] = {IM_COL32(235, 95, 95, 255), IM_COL32(120, 215, 95, 255), IM_COL32(95, 145, 240, 255), 0, IM_COL32(240, 200, 80, 255), IM_COL32(235, 95, 95, 255), IM_COL32(120, 215, 95, 255), IM_COL32(95, 145, 240, 255), IM_COL32(235, 95, 95, 255), IM_COL32(120, 215, 95, 255), IM_COL32(95, 145, 240, 255), 0};
        if (m_curveHidden.size() != 12)
            m_curveHidden.assign(12, 0);
        for (int slot = 0; slot < 12; slot++)
        {
            if (!labels[slot][0])
                continue;
            if (y + kRowHeight >= origin.y + kRulerHeight && y <= origin.y + size.y)
            {
                const bool hidden = m_curveHidden[slot] != 0;
                dl->AddRectFilled({origin.x, y}, {origin.x + size.x, y + kRowHeight}, bl::kSubRow);
                dl->AddRectFilled({origin.x + 12.f, y + 5.f}, {origin.x + 16.f, y + kRowHeight - 5.f}, hidden ? bl::kTextDim : cols[slot], 1.f);
                dl->AddText({origin.x + 24.f, y + 3.f}, hidden ? bl::kTextDim : bl::kText, labels[slot]);
                // eye toggle
                const ImVec2 e(origin.x + size.x - 16.f, y + kRowHeight * 0.5f);
                dl->AddCircle(e, 4.f, hidden ? bl::kTextDim : bl::kText, 12, 1.2f);
                if (!hidden)
                    dl->AddCircleFilled(e, 1.6f, bl::kText, 8);
                if (hovered && mouse.y >= y && mouse.y < y + kRowHeight && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m_curveHidden[slot] = hidden ? 0 : 1;
                    m_curveFitPending = true;
                }
            }
            y += kRowHeight;
        }

        if (hovered)
            ScrollWheel(size.y - kRulerHeight, m_contentHeight);
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + kRulerHeight}, bl::kRulerBg);
        dl->AddText({origin.x + 8.f, origin.y + 5.f}, bl::kTextDim, "F-Curves");
        dl->AddLine({origin.x + size.x - 0.5f, origin.y}, {origin.x + size.x - 0.5f, origin.y + size.y}, IM_COL32(0, 0, 0, 120));
        dl->PopClipRect();
    }

    void AnimationTimeline::DrawCurveArea(Scene &scene, AnimationSystem *anim, const AnimationClip &clipConst, AnimationClip &clip,
                                          float currentFrame, const ImVec2 &origin, const ImVec2 &size)
    {
        (void)clipConst;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        const ImGuiIO &io = ImGui::GetIO();
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsMouseHoveringRect(origin, end) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        m_curveTop = origin.y;
        m_curveHeight = std::max(size.y, 1.f);
        CollectCurves(clip);
        if (m_curveFitPending)
            FitCurveRange(clip);

        dl->AddRectFilled(origin, end, bl::kKeyAreaBg);
        dl->PushClipRect(origin, end, true);

        // frame range + grid (X)
        const float rx0 = FrameToPx(0.f), rx1 = FrameToPx(ToFrame(clip.duration));
        if (rx0 > origin.x)
            dl->AddRectFilled(origin, {rx0, end.y}, bl::kOutsideRange);
        if (rx1 < end.x)
            dl->AddRectFilled({rx1, origin.y}, end, bl::kOutsideRange);
        const float framesPerPx = (m_viewEnd - m_viewStart) / std::max(m_keyWidth, 1.f);
        const float step = NiceFrameStep(framesPerPx, 70.f);
        for (float f = std::floor(m_viewStart / step) * step; f <= m_viewEnd; f += step)
            dl->AddLine({FrameToPx(f), origin.y}, {FrameToPx(f), end.y}, bl::kGridMajor);

        // value grid (Y) + axis labels in the left strip
        const float vspan = std::max(m_curveMax - m_curveMin, 1e-6f);
        float vstep = std::pow(10.f, std::floor(std::log10(vspan / 4.f)));
        while (vspan / vstep > 8.f)
            vstep *= 2.f;
        for (float v = std::floor(m_curveMin / vstep) * vstep; v <= m_curveMax; v += vstep)
        {
            const float y = ValueToPx(v);
            dl->AddLine({origin.x + kAxisWidth, y}, {end.x, y}, std::abs(v) < 1e-6f ? IM_COL32(255, 255, 255, 60) : bl::kGridMajor);
            char buf[24];
            snprintf(buf, sizeof(buf), "%.3g", std::abs(v) < 1e-6f ? 0.f : v);
            dl->AddText({origin.x + 4.f, y - 7.f}, bl::kTextDim, buf);
        }
        dl->AddRectFilled(origin, {origin.x + kAxisWidth, end.y}, IM_COL32(0, 0, 0, 40));

        // curves + points (points are thinned when keys sit closer than a few pixels, Blender-style density)
        m_curvePoints.clear();
        const bool drawAllPoints = m_keyWidth / std::max(m_viewEnd - m_viewStart, 1.f) >= 5.f;
        for (const Curve &c : m_curves)
        {
            const AnimationChannel &chan = clip.channels[c.channelIdx];
            const int n = c.type == KeyType::Position   ? static_cast<int>(chan.positionKeys.size())
                          : c.type == KeyType::Rotation ? static_cast<int>(chan.rotationKeys.size())
                                                        : static_cast<int>(chan.scaleKeys.size());
            ImVec2 prev;
            for (int k = 0; k < n; k++)
            {
                const float time = c.type == KeyType::Position ? chan.positionKeys[k].time : c.type == KeyType::Rotation ? chan.rotationKeys[k].time
                                                                                                                         : chan.scaleKeys[k].time;
                const float value = m_normalize ? (KeyComponent(chan, c.type, k, c.axis) - c.mid) / c.half : KeyComponent(chan, c.type, k, c.axis);
                const ImVec2 pt(FrameToPx(ToFrame(time)), ValueToPx(value));
                if (k > 0)
                    dl->AddLine(prev, pt, c.color, 1.5f);
                prev = pt;
                if (pt.x >= origin.x - 8.f && pt.x <= end.x + 8.f)
                {
                    CurvePoint cp;
                    cp.x = pt.x;
                    cp.y = pt.y;
                    cp.ref = {c.channelIdx, c.type, k};
                    cp.axis = c.axis;
                    cp.selected = IsKeySelected(cp.ref);
                    m_curvePoints.push_back(cp);
                }
            }
        }
        int hoveredPoint = -1;
        if (hovered && m_modal == Modal::None)
        {
            float best = kKeyHitRadius + 1.f;
            for (int i = 0; i < static_cast<int>(m_curvePoints.size()); i++)
            {
                const float d = std::max(std::abs(mouse.x - m_curvePoints[i].x), std::abs(mouse.y - m_curvePoints[i].y));
                if (d <= kKeyHitRadius && d < best)
                {
                    best = d;
                    hoveredPoint = i;
                }
            }
        }
        for (int i = 0; i < static_cast<int>(m_curvePoints.size()); i++)
        {
            const CurvePoint &cp = m_curvePoints[i];
            if (!drawAllPoints && !cp.selected && i != hoveredPoint)
                continue;
            const float r = cp.selected ? 3.5f : 2.5f;
            dl->AddCircleFilled({cp.x, cp.y}, r + (i == hoveredPoint ? 1.f : 0.f), cp.selected ? bl::kKeyFillSel : bl::kKeyFill, 10);
            dl->AddCircle({cp.x, cp.y}, r + (i == hoveredPoint ? 1.f : 0.f), i == hoveredPoint ? bl::kKeyHover : bl::kKeyBorder, 10, 1.f);
        }

        // playhead (draggable, like the dope sheet)
        const float playheadX = FrameToPx(currentFrame);
        const bool onPlayhead = hovered && m_modal == Modal::None && !m_boxSelecting && !m_pressOnKey &&
                                std::abs(mouse.x - playheadX) <= 5.f;
        if (onPlayhead)
            hoveredPoint = -1;
        dl->AddLine({playheadX, origin.y}, {playheadX, end.y}, bl::kPlayhead, (onPlayhead || m_scrubbing) ? 3.f : 2.f);
        if (onPlayhead || m_scrubbing)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (onPlayhead && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_scrubbing = true;
        if (m_boxSelecting)
        {
            const ImVec2 a(std::min(m_boxStart.x, m_boxEnd.x), std::min(m_boxStart.y, m_boxEnd.y));
            const ImVec2 b(std::max(m_boxStart.x, m_boxEnd.x), std::max(m_boxStart.y, m_boxEnd.y));
            dl->AddRectFilled(a, b, bl::kBoxSelectFill);
            dl->AddRect(a, b, bl::kBoxSelect, 0.f, 0, 1.f);
        }
        dl->PopClipRect();

        // ---- interaction ----
        const float mouseFrame = PxToFrame(mouse.x);
        if (m_modal != Modal::None)
        {
            UpdateModal(clip, mouseFrame, currentFrame);
            const bool confirm = m_modalTweak ? !ImGui::IsMouseDown(ImGuiMouseButton_Left)
                                              : (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter));
            const bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (cancel)
                CancelModal(clip);
            else if (confirm)
                CommitModal(clip);
            ReevaluatePose(scene, anim);
        }
        else if (hovered && !m_scrubbing)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (hoveredPoint >= 0)
                {
                    const CurvePoint &cp = m_curvePoints[hoveredPoint];
                    if (io.KeyShift)
                    {
                        if (cp.selected)
                            SelErase(cp.ref);
                        else
                            SelAdd(cp.ref);
                    }
                    else if (!cp.selected)
                        SelectKey(cp.ref, false);
                    m_modalAxis = cp.axis;
                    m_modalType = cp.ref.type;
                    m_pressOnKey = true;
                    m_pressPos = mouse;
                }
                else
                {
                    if (!io.KeyShift)
                        SelClear();
                    m_boxSelecting = true;
                    m_boxStart = mouse;
                    m_boxEnd = mouse;
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                if (hoveredPoint >= 0 && !m_curvePoints[hoveredPoint].selected)
                    SelectKey(m_curvePoints[hoveredPoint].ref, false);
                ImGui::OpenPopup("##keyContext");
            }
        }
        if (m_pressOnKey)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_pressOnKey = false;
            else if ((std::abs(mouse.x - m_pressPos.x) > 3.f || std::abs(mouse.y - m_pressPos.y) > 3.f) && m_modal == Modal::None && !m_selectedKeys.empty())
            {
                BeginModal(Modal::Grab, clip, PxToFrame(m_pressPos.x));
                m_modalAnchorValue = PxToValue(m_pressPos.y);
                m_modalTweak = true;
                m_pressOnKey = false;
            }
        }
        if (m_boxSelecting)
        {
            m_boxEnd = mouse;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float x0 = std::min(m_boxStart.x, m_boxEnd.x), x1 = std::max(m_boxStart.x, m_boxEnd.x);
                const float y0 = std::min(m_boxStart.y, m_boxEnd.y), y1 = std::max(m_boxStart.y, m_boxEnd.y);
                if (x1 - x0 > 2.f || y1 - y0 > 2.f)
                    for (const CurvePoint &cp : m_curvePoints)
                        if (cp.x >= x0 && cp.x <= x1 && cp.y >= y0 && cp.y <= y1)
                            SelAdd(cp.ref);
                m_boxSelecting = false;
            }
        }
        if (ImGui::BeginPopup("##keyContext"))
        {
            const int selCount = static_cast<int>(m_selectedKeys.size());
            ImGui::TextDisabled("%d key(s) selected", selCount);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Keyframes", "X", false, selCount > 0))
            {
                DeleteSelectedKeys(clip);
                ReevaluatePose(scene, anim);
            }
            if (ImGui::MenuItem("Select All", "A"))
                SelectAllKeys(clip, true);
            if (ImGui::MenuItem("Deselect All", "Alt+A"))
                SelectAllKeys(clip, false);
            if (ImGui::MenuItem("Frame All", "Home"))
            {
                FrameAll();
                m_curveFitPending = true;
            }
            if (ImGui::MenuItem("Frame Selected", "Numpad ."))
                FrameSelected(clip);
            if (ImGui::Checkbox("Normalize", &m_normalize))
                m_curveFitPending = true;
            ImGui::EndPopup();
        }

        if (hovered)
            NavigateView(mouse, true);
        m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, m_contentHeight - size.y));
    }

    void AnimationTimeline::DrawGraphEditor(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                                            float currentFrame)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 size(avail.x, std::max(avail.y - kStatusHeight, 40.f));

        BuildRows(skeleton);
        m_keyLeft = origin.x + m_channelWidth + kAxisWidth;
        m_keyWidth = std::max(size.x - m_channelWidth - kAxisWidth - kRightMargin, 10.f);
        m_contentHeight = (skeleton.GetBoneCount() + 10) * kRowHeight + 6.f; // bones + 10 component toggles
        if (m_fitPending)
        {
            const float durationFrames = ToFrame(clip.duration);
            const float pad = std::max(durationFrames * 0.03f, 1.f);
            m_viewStart = -pad;
            m_viewEnd = durationFrames + pad;
            m_fitPending = false;
        }

        ImGui::InvisibleButton("##grapheditor", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            ImGui::SetWindowFocus();

        const ImVec2 body(size.x, size.y - kHScrollHeight);
        DrawRuler(scene, anim, clip, currentFrame, {origin.x + m_channelWidth, origin.y}, {body.x - m_channelWidth, kRulerHeight});
        DrawCurveArea(scene, anim, clip, clip, currentFrame, {origin.x + m_channelWidth, origin.y + kRulerHeight}, {body.x - m_channelWidth, body.y - kRulerHeight});
        DrawCurveChannels(skeleton, clip, origin, {m_channelWidth, body.y});
        DrawVScrollbar(origin, {m_channelWidth, body.y}, m_contentHeight);
        DrawHScrollbar({origin.x, origin.y + body.y}, {size.x, kHScrollHeight}, ToFrame(clip.duration));
        ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    }

    void AnimationTimeline::DrawStatusBar(const Skeleton &skeleton, const AnimationClip &clip)
    {
        int totalKeys = 0;
        for (const AnimationChannel &chan : clip.channels)
            totalKeys += static_cast<int>(chan.positionKeys.size() + chan.rotationKeys.size() + chan.scaleKeys.size());
        ImGui::TextDisabled("Bones %d   Keys %d   Selected %d", skeleton.GetBoneCount(), totalKeys, static_cast<int>(m_selectedKeys.size()));
        ImGui::SameLine(0.f, 24.f);
        if (m_modal == Modal::Grab && m_mode == Mode::GraphEditor && m_modalAxis >= 0)
            ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Move  D: %+.0f frames  %+.4f value   (LMB/Enter confirm, RMB/Esc cancel, Ctrl toggles snap)", m_modalDelta, m_modalValueDelta);
        else if (m_modal == Modal::Grab)
            ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Move  D: %+.0f frames   (LMB/Enter confirm, RMB/Esc cancel, Ctrl toggles snap)", m_modalDelta);
        else if (m_modal == Modal::Scale)
            ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "Scale  %.3f  around frame   (LMB/Enter confirm, RMB/Esc cancel)", m_modalFactor);
        else
            ImGui::TextDisabled("G move  S scale  X delete  Shift+D duplicate  I insert key  A/Alt+A select  Home frame all  Numpad . frame selected  Space play  Wheel zoom  Ctrl/Shift+wheel scroll  MMB pan  Ctrl+MMB zoom");
    }

    void AnimationTimeline::SetGraphMode(bool graph)
    {
        m_mode = graph ? Mode::GraphEditor : Mode::DopeSheet;
        m_fitPending = true;
        m_curveFitPending = true;
    }

    // -------------------------------------------------------------------------
    // main
    // -------------------------------------------------------------------------
    void AnimationTimeline::Update()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize({1360, 360}, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        const bool visible = ImGui::Begin(m_name.c_str(), &m_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto *rs = GetGlobalSystem<RendererSystem>();
        auto *anim = GetGlobalSystem<AnimationSystem>();
        if (!rs || !anim)
        {
            ImGui::TextDisabled("  Animation system not available.");
            ImGui::End();
            return;
        }
        Scene &scene = rs->GetScene();

        // Target = hierarchy selection (root or any node under an animated model).
        auto &selection = SelectionManager::Instance();
        m_targetNode = nullptr;
        if (selection.HasSelection())
        {
            NodeId *sel = selection.GetSelectedNode();
            if (scene.IsNodeAlive(sel))
                m_targetNode = sel;
        }
        m_animatedNodes.clear();
        if (m_targetNode)
            CollectAnimatedNodes(scene, anim, m_targetNode);

        ModelAsset *model = nullptr;
        if (!m_animatedNodes.empty())
            model = scene.GetModelForNode(m_animatedNodes[0]);
        if (!model && m_targetNode)
            model = scene.GetModelForNode(m_targetNode);
        if (!model)
            model = scene.FindSkeletonModel();
        if (!model || !model->HasSkeleton())
        {
            ImGui::TextDisabled("  No skeleton found. Select an animated model in the hierarchy.");
            ImGui::End();
            return;
        }
        if (model != m_lastModel || static_cast<int>(model->GetAnimations().size()) != m_lastClipCount)
        {
            ResetEditState();
            m_lastModel = model;
            m_lastClipCount = static_cast<int>(model->GetAnimations().size());
            m_selectedClip = 0;
        }
        m_editModel = model;
        auto &clips = model->GetMutableAnimations();
        if (clips.empty())
        {
            AnimationClip fresh;
            fresh.name = "Action";
            fresh.duration = 48.f;
            fresh.ticksPerSecond = 24.f;
            clips.push_back(fresh);
            m_lastClipCount = 1;
            m_dirty = true;
        }
        // The animation state is the source of truth for the active clip.
        NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
        const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr;
        if (state && state->clipIndex >= 0 && state->clipIndex < static_cast<int>(clips.size()) && state->clipIndex != m_selectedClip)
        {
            m_selectedClip = state->clipIndex;
            ResetEditState();
        }
        m_selectedClip = std::clamp(m_selectedClip, 0, static_cast<int>(clips.size()) - 1);
        AnimationClip &clip = clips[m_selectedClip];
        const Skeleton &skeleton = model->GetSkeleton();
        if (m_frameTicks <= 0.f)
            m_frameTicks = DetectFrameTicks(clip);
        const float currentFrame = state ? ToFrame(state->time) : 0.f;
        if (state)
        {
            m_loop = state->loop;
            if (state->playing)
                m_speed = state->speed;
        }

        // programmatic requests (editor actions timeline.*)
        if (!m_pendingBone.empty())
        {
            BuildRows(skeleton);
            const int b = skeleton.GetBoneIndex(m_pendingBone);
            m_pendingBone.clear();
            if (b >= 0)
            {
                std::fill(m_boneSelected.begin(), m_boneSelected.end(), 0);
                m_boneSelected[b] = 1;
                m_activeBone = b;
                m_curveFitPending = true;
            }
        }
        if (m_pendingFrame >= 0.f)
        {
            SetFrame(scene, anim, std::clamp(m_pendingFrame, 0.f, ToFrame(clip.duration)));
            m_pendingFrame = -1.f;
        }
        if (m_pendingSave)
        {
            m_pendingSave = false;
            if (ModelAssetCooked::IsCookedPath(model->GetFilePath()) && ModelAssetCooked::WriteToFile(model, model->GetFilePath()))
                m_dirty = false;
        }

        if (DrawHeader(scene, anim, model, clip, currentFrame))
        {
            ImGui::End();
            return;
        }
        if (m_mode == Mode::DopeSheet)
            DrawDopeSheet(scene, anim, skeleton, clip, currentFrame);
        else
            DrawGraphEditor(scene, anim, skeleton, clip, currentFrame);
        ImGui::SetCursorPosX(8.f);
        DrawStatusBar(skeleton, clip);
        HandleHotkeys(scene, anim, skeleton, clip, currentFrame);

        ImGui::End();
    }
} // namespace pe
