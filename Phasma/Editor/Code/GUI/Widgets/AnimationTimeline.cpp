#include "AnimationTimeline.h"
#include "Animation/AnimationClipTools.h"
#include "Animation/AnimationEvaluator.h"
#include "Animation/AnimationPoseViewport.h"
#include "Animation/AnimationPoseTools.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/Widgets/RigEditor.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

#include <nlohmann/json.hpp>

#include <numeric>

namespace pe
{
    AnimationTimeline::AnimationTimeline() : Widget("Animation Timeline")
    {
        m_open = false;
        m_rigEditor = std::make_unique<RigEditor>();
    }

    AnimationTimeline::~AnimationTimeline() = default;

    void AnimationTimeline::Init(GUI *gui)
    {
        Widget::Init(gui);
        m_rigEditor->Init(gui);
    }

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
        constexpr ImU32 kInterval = IM_COL32(255, 170, 60, 40);
        constexpr ImU32 kIntervalEdge = IM_COL32(255, 190, 90, 215);
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

        // What the evaluator plays for a channel with no Location keys: the bind translation with the
        // intermediate prefix removed, which is not the same as localBindTransform on an imported rig.
        vec3 BindTranslation(const BoneInfo &bone)
        {
            vec3 position, scale;
            quat rotation;
            AnimationEvaluator::BindPose(bone, position, rotation, scale);
            return position;
        }

        bool ChannelKeyed(const AnimationChannel &channel)
        {
            return !channel.positionKeys.empty() || !channel.rotationKeys.empty() || !channel.scaleKeys.empty();
        }

        bool BoneKeyed(const AnimationClip &clip, int bone)
        {
            for (const AnimationChannel &channel : clip.channels)
                if (channel.boneIndex == bone && ChannelKeyed(channel))
                    return true;
            return false;
        }

        std::string BoneNames(const Skeleton &skeleton, std::span<const int> bones)
        {
            std::string text;
            const int shown = std::min(static_cast<int>(bones.size()), 3);
            for (int i = 0; i < shown; ++i)
            {
                if (i)
                    text += ", ";
                const int bone = bones[i];
                text += (bone >= 0 && bone < skeleton.GetBoneCount()) ? skeleton.bones[bone].name : std::to_string(bone);
            }
            if (static_cast<int>(bones.size()) > 3)
                text += ", ...";
            return text;
        }

        std::string TweenEmptyReason(const Skeleton &skeleton, const AnimationClip &clip, std::span<const int> bones)
        {
            if (!bones.empty())
            {
                std::vector<int> missing;
                for (int bone : bones)
                    if (!BoneKeyed(clip, bone))
                        missing.push_back(bone);
                if (!missing.empty() && missing.size() == bones.size())
                    return "Tween wrote nothing: " + BoneNames(skeleton, missing) +
                           (missing.size() == 1 ? " has no keys. Key its ends, or deselect to bake every keyed bone."
                                                : " have no keys. Key their ends, or deselect to bake every keyed bone.");
            }
            return "Tween wrote nothing: no interior frames to bake.";
        }

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
            // Dope sheet and ruler: Ctrl+wheel zooms, and a plain wheel belongs to the window's own
            // scrollbar (Update drives it). The Graph Editor keeps Blender's view2d wheel set.
            if (!graph)
            {
                if (io.KeyCtrl)
                    ZoomRange(m_viewStart, m_viewEnd, PxToFrame(mouse.x), wheel > 0.f ? 0.8f : 1.25f, kMinViewFrames);
            }
            else if (io.KeyCtrl) // scroll frames
            {
                const float df = -wheel * span * 0.1f;
                m_viewStart += df;
                m_viewEnd += df;
            }
            else if (io.KeyShift) // scroll values
            {
                const float dv = wheel * vspan * 0.1f;
                m_curveMin += dv;
                m_curveMax += dv;
            }
            else // zoom around the cursor
            {
                const float factor = wheel > 0.f ? 0.8f : 1.25f;
                ZoomRange(m_viewStart, m_viewEnd, PxToFrame(mouse.x), factor, kMinViewFrames);
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
                if (graph) // the dope sheet shows every row, so a middle-drag only moves time there
                {
                    const float dv = io.MouseDelta.y * vspan / std::max(m_curveHeight, 1.f);
                    m_curveMin += dv;
                    m_curveMax += dv;
                }
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
        // A freshly baked model has a skeleton but no clips yet: the timeline creates its first action.
        ModelAsset *model = scene.GetModelForNode(root);
        if (model && model->HasSkeleton() && scene.NodeHasSkinnedMesh(root))
            m_animatedNodes.push_back(root);
        else if (anim->GetAnimationState(root))
            m_animatedNodes.push_back(root);
        for (NodeId *child : scene.GetChildren(root))
            CollectAnimatedNodes(scene, anim, child);
    }

    // Selecting one part of a character must drive the whole character: climb from the selection to the
    // highest ancestor whose animated nodes all belong to the selection's model (scene loads register
    // rigs as plain group nodes, so the model root cannot be looked up directly).
    namespace
    {
        // True when any node under root draws a model other than `model`: such a group (a prop shelf,
        // the scene root) is a character boundary and is never adopted as the animation root.
        bool SubtreeHasOtherModel(Scene &scene, NodeId *root, ModelAsset *model)
        {
            ModelAsset *owner = scene.GetModelForNode(root);
            if (owner && owner != model)
                return true;
            for (NodeId *child : scene.GetChildren(root))
                if (SubtreeHasOtherModel(scene, child, model))
                    return true;
            return false;
        }
    } // namespace

    NodeId *AnimationTimeline::AnimationRootOf(Scene &scene, AnimationSystem *anim, NodeId *selected)
    {
        std::vector<NodeId *> keep = std::move(m_animatedNodes);
        ModelAsset *model = nullptr;
        NodeId *root = selected;
        for (NodeId *node = selected; node && scene.IsNodeAlive(node); node = scene.GetParent(node))
        {
            m_animatedNodes.clear();
            CollectAnimatedNodes(scene, anim, node);
            if (m_animatedNodes.empty())
            {
                // An empty bone / group node: keep climbing inside the character, but a top-level empty
                // (camera, light, the scene root) is not part of any character.
                if (model || !scene.GetParent(node) || !scene.GetParent(scene.GetParent(node)))
                    break;
                continue;
            }
            ModelAsset *first = scene.GetModelForNode(m_animatedNodes[0]);
            if (!model)
                model = first;
            const bool sameModel = std::all_of(m_animatedNodes.begin(), m_animatedNodes.end(), [&](NodeId *n)
                                               { return scene.GetModelForNode(n) == model; });
            if (!sameModel || SubtreeHasOtherModel(scene, node, model))
                break;
            root = node;
        }
        m_animatedNodes = std::move(keep);
        return model ? root : selected;
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
        m_restDisplayed = false;
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

    bool AnimationTimeline::GetViewportPose(ModelAsset *model, ViewportPose &out) const
    {
        return SampleViewportPose(model, 0.f, out);
    }

    bool AnimationTimeline::GetViewportTimeSeconds(ModelAsset *model, double &out) const
    {
        out = 0.0;
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;

        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        const float time = state ? state->time : 0.f;
        if (!std::isfinite(time) || !std::isfinite(clip.duration) || clip.duration < 0.f ||
            !std::isfinite(clip.ticksPerSecond) || clip.ticksPerSecond <= 0.f)
            return false;

        out = static_cast<double>(std::clamp(time, 0.f, clip.duration)) /
              static_cast<double>(clip.ticksPerSecond);
        return std::isfinite(out);
    }

    bool AnimationTimeline::SampleViewportPose(ModelAsset *model, float frameOffset, ViewportPose &out) const
    {
        out = {};
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        // The mesh skins identity whenever the node carries no evaluated joint matrices: Rest Pose display,
        // or a freshly loaded scene nobody has scrubbed yet (no animation state exists). Mirror the Scene
        // upload fallback here or the overlay points sit in a clip pose the mesh is not in.
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        const bool unposed = !node || !renderer || !renderer->GetScene().IsNodeAlive(node) ||
                             renderer->GetScene().GetNodeRuntime(node).jointMatrices.empty();
        if ((m_restDisplayed || unposed) && frameOffset == 0.f)
        {
            // Rest Pose display: the mesh skins identity, so hand the overlay the matching rest transforms.
            const Skeleton &skeleton = model->GetSkeleton();
            const mat4 invRoot = glm::inverse(skeleton.rootTransform);
            out.node = node;
            out.boneTransforms.resize(skeleton.bones.size());
            for (int i = 0; i < static_cast<int>(skeleton.bones.size()); i++)
                out.boneTransforms[i] = invRoot * glm::inverse(skeleton.bones[i].offsetMatrix);
            return !out.boneTransforms.empty();
        }
        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const float frameTicks = m_frameTicks > 0.f ? m_frameTicks : DetectFrameTicks(clip);
        return SampleViewportPoseTicks(model, (state ? state->time : 0.f) + frameOffset * frameTicks, out);
    }

    bool AnimationTimeline::SampleViewportPoseAtFrame(ModelAsset *model, float frame, ViewportPose &out) const
    {
        out = {};
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;
        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const float frameTicks = m_frameTicks > 0.f ? m_frameTicks : DetectFrameTicks(clip);
        return SampleViewportPoseTicks(model, frame * frameTicks, out);
    }

    bool AnimationTimeline::GetClipEndFrame(ModelAsset *model, float &endFrame) const
    {
        endFrame = 0.f;
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;
        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const float frameTicks = m_frameTicks > 0.f ? m_frameTicks : DetectFrameTicks(clip);
        if (frameTicks <= 0.f || !std::isfinite(clip.duration))
            return false;
        endFrame = std::max(clip.duration, 0.f) / frameTicks;
        return true;
    }

    bool AnimationTimeline::SampleViewportPoseTicks(ModelAsset *model, float ticks, ViewportPose &out) const
    {
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const float time = std::clamp(ticks, 0.f, std::max(clip.duration, 0.f));
        const Skeleton &skeleton = model->GetSkeleton();
        std::vector<mat4> jointMatrices;
        AnimationEvaluator::EvaluatePose(clip, skeleton, time, jointMatrices);

        const mat4 invRoot = glm::inverse(skeleton.rootTransform);
        out.node = node;
        out.boneTransforms.resize(jointMatrices.size());
        for (int i = 0; i < static_cast<int>(jointMatrices.size()); i++)
            out.boneTransforms[i] = invRoot * jointMatrices[i] * glm::inverse(skeleton.bones[i].offsetMatrix);
        return !out.boneTransforms.empty();
    }

    bool AnimationTimeline::PushViewportUndo(ModelAsset *model)
    {
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;
        PushUndo(model->GetMutableAnimations()[m_selectedClip]);
        return true;
    }

    bool AnimationTimeline::KeyViewportGlobalRotations(Scene &scene, ModelAsset *model,
                                                       std::span<const GlobalBoneRotation> rotations, float frame,
                                                       bool pushUndo, bool userPose)
    {
        if (!model || model != m_editModel || rotations.empty() || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;

        const Skeleton &skeleton = model->GetSkeleton();
        std::vector<quat> desiredRotations(skeleton.GetBoneCount(), quat(1.f, 0.f, 0.f, 0.f));
        std::vector<char> requested(skeleton.GetBoneCount(), 0);
        for (const GlobalBoneRotation &edit : rotations)
        {
            const float lengthSquared = glm::dot(edit.rotation, edit.rotation);
            if (edit.bone < 0 || edit.bone >= skeleton.GetBoneCount() || !std::isfinite(lengthSquared) ||
                lengthSquared <= 1e-8f)
                return false;
            desiredRotations[edit.bone] = glm::normalize(edit.rotation);
            requested[edit.bone] = 1;
        }

        std::vector<int> bones;
        for (int bone = 0; bone < skeleton.GetBoneCount(); ++bone)
            if (requested[bone])
                bones.push_back(bone);
        if (bones.empty())
            return false;
        auto depth = [&](int bone)
        {
            int result = 0;
            for (int guard = 0; guard < skeleton.GetBoneCount() && bone >= 0; ++guard)
            {
                bone = skeleton.bones[bone].parentIndex;
                ++result;
            }
            return result;
        };
        std::stable_sort(bones.begin(), bones.end(), [&](int left, int right)
                         { return depth(left) < depth(right); });

        AnimationClip &clip = model->GetMutableAnimations()[m_selectedClip];
        if (m_frameTicks <= 0.f)
            m_frameTicks = DetectFrameTicks(clip);
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        const float time = ToTicks(std::round(frame >= 0.f ? frame : state ? ToFrame(state->time)
                                                                           : 0.f));

        std::vector<mat4> joints;
        AnimationEvaluator::EvaluatePose(clip, skeleton, time, joints);
        if (joints.size() != skeleton.bones.size())
            return false;
        std::vector<mat4> sourceGlobals(joints.size());
        std::vector<mat4> finalGlobals(joints.size());
        const mat4 inverseRoot = glm::inverse(skeleton.rootTransform);
        for (size_t bone = 0; bone < joints.size(); ++bone)
            sourceGlobals[bone] = joints[bone] * glm::inverse(skeleton.bones[bone].offsetMatrix);

        auto rotationOf = [](const mat4 &transform)
        {
            const vec3 scale(glm::length(vec3(transform[0])),
                             glm::length(vec3(transform[1])),
                             glm::length(vec3(transform[2])));
            if (scale.x <= 1e-8f || scale.y <= 1e-8f || scale.z <= 1e-8f)
                return quat(1.f, 0.f, 0.f, 0.f);
            return glm::normalize(glm::quat_cast(mat3(vec3(transform[0]) / scale.x,
                                                      vec3(transform[1]) / scale.y,
                                                      vec3(transform[2]) / scale.z)));
        };

        struct LocalRotation
        {
            int bone = -1;
            quat value = quat(1.f, 0.f, 0.f, 0.f);
        };
        std::vector<LocalRotation> solved;
        solved.reserve(bones.size());
        std::vector<int> hierarchy(skeleton.GetBoneCount());
        std::iota(hierarchy.begin(), hierarchy.end(), 0);
        std::stable_sort(hierarchy.begin(), hierarchy.end(), [&](int left, int right)
                         { return depth(left) < depth(right); });
        for (int boneIndex : hierarchy)
        {
            const BoneInfo &bone = skeleton.bones[boneIndex];
            const mat4 parentGlobal = bone.parentIndex >= 0 ? finalGlobals[bone.parentIndex] : mat4(1.f);

            vec3 localPosition, localScale;
            quat localRotation;
            const int channel = ChannelForBone(clip, boneIndex);
            if (channel >= 0)
                AnimationEvaluator::SampleChannel(clip.channels[channel],
                                                  bone,
                                                  time,
                                                  localPosition,
                                                  localRotation,
                                                  localScale);
            else
                AnimationEvaluator::BindPose(bone, localPosition, localRotation, localScale);
            if (requested[boneIndex])
            {
                const mat4 currentRig = inverseRoot * sourceGlobals[boneIndex];
                const vec3 scale(glm::length(vec3(currentRig[0])),
                                 glm::length(vec3(currentRig[1])),
                                 glm::length(vec3(currentRig[2])));
                const mat4 desiredRig = glm::translate(mat4(1.f), vec3(currentRig[3])) *
                                        glm::mat4_cast(desiredRotations[boneIndex]) * glm::scale(mat4(1.f), scale);
                const mat4 desiredGlobal = skeleton.rootTransform * desiredRig;
                localRotation = rotationOf(glm::inverse(bone.intermediatePrefix) *
                                           glm::inverse(parentGlobal) * desiredGlobal);
                solved.push_back({boneIndex, localRotation});
            }
            finalGlobals[boneIndex] = parentGlobal * bone.intermediatePrefix *
                                      glm::translate(mat4(1.f), localPosition) * glm::mat4_cast(localRotation) *
                                      glm::scale(mat4(1.f), localScale);
        }

        const AnimationClip before = clip;
        for (LocalRotation &rotation : solved)
        {
            const int channel = EnsureChannel(clip, rotation.bone);
            const auto &keys = clip.channels[channel].rotationKeys;
            const auto next = std::lower_bound(keys.begin(), keys.end(), time,
                                               [](const RotationKey &key, float keyTime)
                                               { return key.time < keyTime; });
            if (next != keys.begin() && glm::dot(std::prev(next)->value, rotation.value) < 0.f)
                rotation.value = quat(-rotation.value.w, -rotation.value.x, -rotation.value.y, -rotation.value.z);
            SetRotationKey(clip, channel, time, rotation.value);
        }
        if (userPose)
            RetweenAroundFrame(clip, bones, ToFrame(time));
        if (pushUndo)
            PushUndoSnapshot(before);
        m_dirty = true;
        SelClear();
        if (anim)
            ReevaluatePose(scene, anim);
        return true;
    }

    bool AnimationTimeline::CanViewportRotate(ModelAsset *model, int bone, bool rotate, bool translate) const
    {
        if (!model || model != m_editModel || bone < 0 || bone >= model->GetSkeleton().GetBoneCount() ||
            m_selectedClip < 0 || m_selectedClip >= static_cast<int>(model->GetAnimations().size()) || m_frameTicks <= 0.f)
            return false;
        if (m_autoKey)
            return true;

        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const int channel = ChannelForBone(clip, bone);
        if (channel < 0)
            return false;
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        const float time = ToTicks(std::round(state ? ToFrame(state->time) : 0.f));
        return (rotate && FindKeyAtTime(clip.channels[channel].rotationKeys, time) >= 0) ||
               (translate && FindKeyAtTime(clip.channels[channel].positionKeys, time) >= 0);
    }

    bool AnimationTimeline::BeginViewportRotate(Scene &scene, ModelAsset *model, int bone, bool rotate, bool translate)
    {
        if (!CanViewportRotate(model, bone, rotate, translate))
            return false;
        if (m_viewportRotateActive)
            return m_viewportRotateBone == bone;

        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        const float frame = std::round(state ? ToFrame(state->time) : 0.f);
        m_viewportRotateTime = ToTicks(frame);
        PushUndo(model->GetMutableAnimations()[m_selectedClip]);
        m_viewportRotateBone = bone;
        m_viewportRotateActive = true;
        if (anim)
            SetFrame(scene, anim, frame);
        return true;
    }

    bool AnimationTimeline::UpdateViewportRotate(Scene &scene, ModelAsset *model, int bone, const mat4 &boneTransform,
                                                 int mirrorBone, bool rotate, bool translate)
    {
        if (!m_viewportRotateActive || m_viewportRotateBone != bone || model != m_editModel ||
            m_selectedClip < 0 || m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return false;

        AnimationClip &clip = model->GetMutableAnimations()[m_selectedClip];
        const Skeleton &skeleton = model->GetSkeleton();
        std::vector<mat4> jointMatrices;
        AnimationEvaluator::EvaluatePose(clip, skeleton, m_viewportRotateTime, jointMatrices);
        std::vector<mat4> globalTransforms(jointMatrices.size());
        std::vector<mat4> rigTransforms(jointMatrices.size());
        const mat4 invRoot = glm::inverse(skeleton.rootTransform);
        for (int i = 0; i < static_cast<int>(jointMatrices.size()); i++)
        {
            globalTransforms[i] = jointMatrices[i] * glm::inverse(skeleton.bones[i].offsetMatrix);
            rigTransforms[i] = invRoot * globalTransforms[i];
        }

        auto rotationOf = [](const mat4 &m)
        {
            const vec3 scale(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
            if (scale.x <= 1e-8f || scale.y <= 1e-8f || scale.z <= 1e-8f)
                return quat(1.f, 0.f, 0.f, 0.f);
            return glm::normalize(glm::quat_cast(mat3(vec3(m[0]) / scale.x, vec3(m[1]) / scale.y, vec3(m[2]) / scale.z)));
        };
        // Returns whether the bone was actually keyed: without Auto Key a bone with no key here is left
        // alone, and interval mode must not rebuild the in-betweens of a bone nobody posed.
        auto writeGlobalRotation = [&](int targetBone, const mat4 &rigTransform) -> bool
        {
            if (targetBone < 0 || targetBone >= skeleton.GetBoneCount())
                return false;
            const BoneInfo &info = skeleton.bones[targetBone];
            const mat4 desiredGlobal = skeleton.rootTransform * rigTransform;
            const mat4 local = info.parentIndex >= 0 && info.parentIndex < static_cast<int>(globalTransforms.size())
                                   ? glm::inverse(globalTransforms[info.parentIndex]) * desiredGlobal
                                   : desiredGlobal;
            const mat4 localBone = glm::inverse(info.intermediatePrefix) * local;
            const quat rotation = rotationOf(localBone);
            int channel = ChannelForBone(clip, targetBone);
            if (channel < 0)
            {
                if (!m_autoKey)
                    return false;
                channel = EnsureChannel(clip, targetBone);
            }
            bool wrote = false;
            if (rotate && (m_autoKey || FindKeyAtTime(clip.channels[channel].rotationKeys, m_viewportRotateTime) >= 0))
            {
                SetRotationKey(clip, channel, m_viewportRotateTime, rotation);
                wrote = true;
            }
            if (translate &&
                (m_autoKey || FindKeyAtTime(clip.channels[channel].positionKeys, m_viewportRotateTime) >= 0))
            {
                SetPositionKey(clip, channel, m_viewportRotateTime, vec3(localBone[3]));
                wrote = true;
            }
            return wrote;
        };

        std::vector<int> keyed;
        if (writeGlobalRotation(bone, boneTransform))
            keyed.push_back(bone);
        if (mirrorBone >= 0 && mirrorBone < skeleton.GetBoneCount() && mirrorBone != bone)
        {
            mat4 reflection(1.f);
            reflection[0][0] = -1.f;
            const quat mirroredRotation = rotationOf(reflection * glm::mat4_cast(rotationOf(boneTransform)) * reflection);
            const mat4 &current = rigTransforms[mirrorBone];
            const vec3 scale(glm::length(vec3(current[0])), glm::length(vec3(current[1])), glm::length(vec3(current[2])));
            const vec3 mirroredPosition = translate ? vec3(-boneTransform[3].x, boneTransform[3].y, boneTransform[3].z)
                                                    : vec3(current[3]);
            const mat4 mirroredTransform = glm::translate(mat4(1.f), mirroredPosition) * glm::mat4_cast(mirroredRotation) *
                                           glm::scale(mat4(1.f), scale);
            if (writeGlobalRotation(mirrorBone, mirroredTransform))
                keyed.push_back(mirrorBone);
        }

        RetweenAroundFrame(clip, keyed, ToFrame(m_viewportRotateTime));
        m_dirty = true;
        if (AnimationSystem *anim = GetGlobalSystem<AnimationSystem>())
            ReevaluatePose(scene, anim);
        return true;
    }

    void AnimationTimeline::EndViewportRotate()
    {
        m_viewportRotateActive = false;
        m_viewportRotateBone = -1;
    }

    bool AnimationTimeline::KeyViewportPosition(Scene &scene, ModelAsset *model, int bone, const vec3 &rigPosition, float frame,
                                                bool userPose)
    {
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()) || bone < 0 ||
            bone >= model->GetSkeleton().GetBoneCount())
            return false;
        const Skeleton &skeleton = model->GetSkeleton();
        AnimationClip &clip = model->GetMutableAnimations()[m_selectedClip];
        if (m_frameTicks <= 0.f)
            m_frameTicks = DetectFrameTicks(clip);
        NodeId *node = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
        const AnimationNodeState *state = anim && node ? anim->GetAnimationState(node) : nullptr;
        const float time = ToTicks(std::round(frame >= 0.f ? frame : state ? ToFrame(state->time)
                                                                           : 0.f));
        int channel = ChannelForBone(clip, bone);
        if (channel < 0 ? !m_autoKey : !m_autoKey && FindKeyAtTime(clip.channels[channel].positionKeys, time) < 0)
            return false; // the Move gizmo's rule: a Location key needs Auto Key or a key at this frame
        std::vector<mat4> joints;
        AnimationEvaluator::EvaluatePose(clip, skeleton, time, joints);
        if (joints.size() != skeleton.bones.size())
            return false;
        const BoneInfo &info = skeleton.bones[bone];
        mat4 desiredGlobal = joints[bone] * glm::inverse(info.offsetMatrix);
        desiredGlobal[3] = skeleton.rootTransform * vec4(rigPosition, 1.f);
        const mat4 parentGlobal = info.parentIndex >= 0
                                      ? joints[info.parentIndex] * glm::inverse(skeleton.bones[info.parentIndex].offsetMatrix)
                                      : mat4(1.f);
        const mat4 localBone = glm::inverse(info.intermediatePrefix) * glm::inverse(parentGlobal) * desiredGlobal;
        if (channel < 0)
            channel = EnsureChannel(clip, bone);
        SetPositionKey(clip, channel, time, vec3(localBone[3]));
        if (userPose)
            RetweenAroundFrame(clip, std::vector<int>{bone}, ToFrame(time));
        m_dirty = true;
        if (anim)
            ReevaluatePose(scene, anim);
        return true;
    }

    int AnimationTimeline::LocationBone(ModelAsset *model) const
    {
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return -1;
        const Skeleton &skeleton = model->GetSkeleton();
        auto depth = [&](int bone)
        {
            int result = 0;
            for (int guard = 0; guard < skeleton.GetBoneCount() && bone >= 0; ++guard, ++result)
                bone = skeleton.bones[bone].parentIndex;
            return result;
        };
        int best = -1, bestDepth = std::numeric_limits<int>::max();
        for (const AnimationChannel &channel : model->GetAnimations()[m_selectedClip].channels)
            if (!channel.positionKeys.empty() && channel.boneIndex >= 0 && channel.boneIndex < skeleton.GetBoneCount() &&
                depth(channel.boneIndex) < bestDepth)
                bestDepth = depth(channel.boneIndex), best = channel.boneIndex;
        for (int i = 0; best < 0 && i < skeleton.GetBoneCount(); i++)
            if (skeleton.bones[i].parentIndex < 0)
                best = i;
        return best;
    }

    float AnimationTimeline::FrameSeconds(ModelAsset *model) const
    {
        if (!model || model != m_editModel || m_selectedClip < 0 ||
            m_selectedClip >= static_cast<int>(model->GetAnimations().size()))
            return 0.f;
        const AnimationClip &clip = model->GetAnimations()[m_selectedClip];
        const float frameTicks = m_frameTicks > 0.f ? m_frameTicks : DetectFrameTicks(clip);
        return clip.ticksPerSecond > 0.f && frameTicks > 0.f ? frameTicks / clip.ticksPerSecond : 0.f;
    }

    bool AnimationTimeline::StepViewportUndo(Scene &scene, bool redo)
    {
        // rig.bake can free m_editModel before the next Update re-resolves it: only trust the pointer
        // while the resolved target chain is still alive and still maps to it.
        NodeId *carrier = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
        if (!m_editModel || !scene.IsNodeAlive(carrier) || scene.GetModelForNode(carrier) != m_editModel ||
            m_selectedClip < 0 || m_selectedClip >= static_cast<int>(m_editModel->GetAnimations().size()))
            return false;
        AnimationClip &clip = m_editModel->GetMutableAnimations()[m_selectedClip];
        const size_t before = redo ? m_redo.size() : m_undo.size();
        redo ? Redo(clip) : Undo(clip);
        if (before == (redo ? m_redo.size() : m_undo.size()))
            return false;
        if (AnimationSystem *anim = GetGlobalSystem<AnimationSystem>())
            ReevaluatePose(scene, anim);
        return true;
    }

    std::string AnimationTimeline::HandleAction(const std::string &action, const std::string &argsJson)
    {
        using namespace AnimationClipTools;
        const nlohmann::json args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"ok":false,"error":"invalid args json"})";
        auto fail = [&](const std::string &message)
        { return nlohmann::json{{"ok", false}, {"action", action}, {"error", message}}.dump(); };
        auto ok = [&](nlohmann::json result = nlohmann::json::object())
        {
            result["ok"] = true;
            result["action"] = action;
            return result.dump();
        };

        try
        {
            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
            if (!renderer || !anim)
                return fail("animation system not available");
            Scene &scene = renderer->GetScene();
            if (m_targetNode && !scene.IsNodeAlive(m_targetNode))
                m_targetNode = nullptr;
            std::erase_if(m_animatedNodes, [&](NodeId *node)
                          { return !scene.IsNodeAlive(node); });

            ModelAsset *model = nullptr;
            auto &selection = SelectionManager::Instance();
            if (selection.HasSelection())
            {
                NodeId *selected = selection.GetSelectedNode();
                if (scene.IsNodeAlive(selected))
                {
                    m_targetNode = AnimationRootOf(scene, anim, selected);
                    m_animatedNodes.clear();
                    CollectAnimatedNodes(scene, anim, m_targetNode);
                    model = !m_animatedNodes.empty() ? scene.GetModelForNode(m_animatedNodes[0])
                                                     : scene.GetModelForNode(selected);
                }
            }
            if (!model)
            {
                const auto &sceneModels = scene.GetModels();
                const bool editModelAlive =
                    std::find(sceneModels.begin(), sceneModels.end(), m_editModel) != sceneModels.end();
                if (editModelAlive)
                    model = m_editModel;
                else if (m_editModel)
                {
                    ResetEditState();
                    m_editModel = nullptr;
                    m_lastModel = nullptr;
                    m_targetNode = nullptr;
                    m_animatedNodes.clear();
                }
            }
            if (!model || !model->HasSkeleton())
                return fail("no rigged animation target selected");
            if (model != m_editModel)
            {
                ResetEditState();
                m_editModel = model;
                m_lastModel = model;
                m_lastClipCount = static_cast<int>(model->GetAnimations().size());
                m_selectedClip = 0;
            }

            if (action == "timeline.grab" || action == "timeline.pin" || action == "timeline.lock" ||
                action == "timeline.balance" || action == "timeline.reference_load" || action == "timeline.reference_clear")
                return PoseViewport(*m_rigEditor)->HandleAction(scene, action, argsJson);
            if (model->GetAnimations().empty())
                return fail("the selected rig has no animation clip");

            NodeId *primary = m_animatedNodes.empty() ? m_targetNode : m_animatedNodes[0];
            const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr;
            if (state && state->clipIndex >= 0 && state->clipIndex < static_cast<int>(model->GetAnimations().size()))
                m_selectedClip = state->clipIndex;
            m_selectedClip = std::clamp(m_selectedClip, 0, static_cast<int>(model->GetAnimations().size()) - 1);
            AnimationClip &clip = model->GetMutableAnimations()[m_selectedClip];
            const Skeleton &skeleton = model->GetSkeleton();
            if (!std::isfinite(clip.duration) || !std::isfinite(clip.ticksPerSecond) || clip.duration < 0.0f ||
                clip.ticksPerSecond <= 0.0f)
                return fail("clip timing is invalid");
            if (!std::isfinite(m_frameTicks) || m_frameTicks <= 0.f)
                m_frameTicks = DetectFrameTicks(clip);
            if (!std::isfinite(m_frameTicks) || m_frameTicks <= 0.f)
                return fail("clip frame grid is invalid");
            const float sampledFrame = state ? ToFrame(state->time) : 0.f;
            const float currentFrame = std::isfinite(sampledFrame) ? sampledFrame : 0.f;
            const float durationFrames = ToFrame(clip.duration);

            auto parseBone = [&](const nlohmann::json &value) -> int
            {
                if (value.is_number_integer())
                    return value.get<int>();
                if (value.is_string())
                {
                    const std::string name = value.get<std::string>();
                    int bone = skeleton.GetBoneIndex(name);
                    if (bone >= 0)
                        return bone;
                    for (int index = 0; index < skeleton.GetBoneCount(); ++index)
                        if (skeleton.bones[index].name == name)
                            return index;
                }
                return -1;
            };
            auto appendChain = [&](const std::string &text, std::vector<int> &bones)
            {
                size_t begin = 0;
                while (begin <= text.size())
                {
                    const size_t end = text.find_first_of(",;", begin);
                    std::string name = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                    const size_t first = name.find_first_not_of(" \t\r\n");
                    const size_t last = name.find_last_not_of(" \t\r\n");
                    if (first != std::string::npos)
                    {
                        name = name.substr(first, last - first + 1);
                        bones.push_back(parseBone(name));
                    }
                    if (end == std::string::npos)
                        break;
                    begin = end + 1;
                }
            };
            auto bonesArg = [&](bool ordered)
            {
                std::vector<int> bones;
                const nlohmann::json *value = args.contains("bones")   ? &args["bones"]
                                              : args.contains("chain") ? &args["chain"]
                                                                       : nullptr;
                const bool explicitlySupplied = value != nullptr;
                if (value && value->is_array())
                    for (const nlohmann::json &bone : *value)
                        bones.push_back(parseBone(bone));
                else if (value && value->is_string())
                    appendChain(value->get<std::string>(), bones);
                else if (value)
                    throw std::invalid_argument("bones/chain must be an array or comma-separated string");
                else
                {
                    for (int bone = 0; bone < static_cast<int>(m_boneSelected.size()); ++bone)
                        if (m_boneSelected[bone])
                            bones.push_back(bone);
                    if (bones.empty() && m_activeBone >= 0)
                        bones.push_back(m_activeBone);
                }
                if (std::any_of(bones.begin(), bones.end(), [&](int bone)
                                { return bone < 0 || bone >= skeleton.GetBoneCount(); }))
                    throw std::invalid_argument("bones/chain contains an unknown bone");
                std::sort(bones.begin(), bones.end());
                bones.erase(std::unique(bones.begin(), bones.end()), bones.end());
                if (explicitlySupplied && bones.empty())
                    throw std::invalid_argument("bones/chain must not be empty");
                if (ordered)
                {
                    auto depth = [&](int bone)
                    {
                        int result = 0;
                        while (bone >= 0 && result <= skeleton.GetBoneCount())
                        {
                            bone = skeleton.bones[bone].parentIndex;
                            ++result;
                        }
                        return result;
                    };
                    std::stable_sort(bones.begin(), bones.end(), [&](int left, int right)
                                     { return depth(left) < depth(right); });
                }
                return bones;
            };
            auto boneArg = [&]()
            {
                if (args.contains("bone"))
                    return parseBone(args["bone"]);
                return m_activeBone;
            };
            auto channelMask = [&]()
            {
                if (!args.contains("channels"))
                    return ChannelMask::All;
                ChannelMask result = ChannelMask::None;
                auto add = [&](const std::string &name)
                {
                    if (name == "all" || name == "trs")
                        result = ChannelMask::All;
                    else if (name == "position" || name == "location")
                        result = result | ChannelMask::Position;
                    else if (name == "rotation")
                        result = result | ChannelMask::Rotation;
                    else if (name == "scale")
                        result = result | ChannelMask::Scale;
                    else
                        throw std::invalid_argument("unknown channel: " + name);
                };
                if (args["channels"].is_string())
                    add(args["channels"].get<std::string>());
                else if (args["channels"].is_array())
                {
                    for (const auto &name : args["channels"])
                    {
                        if (!name.is_string())
                            throw std::invalid_argument("channels entries must be strings");
                        add(name.get<std::string>());
                    }
                }
                else
                    throw std::invalid_argument("channels must be a string or array");
                if (result == ChannelMask::None)
                    throw std::invalid_argument("channels must not be empty");
                return result;
            };
            auto commit = [&](const AnimationClip &before, size_t changed)
            {
                if (changed == 0)
                    return;
                PushUndoSnapshot(before);
                SelClear();
                m_fitPending = true;
                m_curveFitPending = true;
                ReevaluatePose(scene, anim);
            };

            if (action == "timeline.interval")
            {
                const float start = args.value("start", 0.f);
                const float end = args.value("end", -1.f);
                if (!std::isfinite(start) || !std::isfinite(end))
                    return fail("start and end must be finite frames");
                if (end < start + 1.f)
                {
                    ClearInterval();
                    return ok({{"interval", false}});
                }
                m_intervalStart = std::clamp(start, 0.f, std::max(durationFrames - 1.f, 0.f));
                m_intervalEnd = std::clamp(end, m_intervalStart + 1.f, std::max(durationFrames, m_intervalStart + 1.f));
                return ok({{"interval", true}, {"start", m_intervalStart}, {"end", m_intervalEnd}});
            }
            if (action == "timeline.interval_slide" || action == "timeline.interval_scale")
            {
                if (!HasInterval())
                    return fail("no interval: set one with timeline.interval");
                const bool slide = action == "timeline.interval_slide";
                const float factor = slide ? 1.f : args.value("factor", 1.f);
                const float pivot = slide ? 0.f : args.value("pivot_frame", m_intervalStart);
                const float requested = slide ? args.value("delta_frames", 0.f) : 0.f;
                if (!std::isfinite(requested) || !std::isfinite(factor) || !std::isfinite(pivot) || factor <= 0.f ||
                    factor > 100.f)
                    return fail("delta_frames and pivot_frame must be finite, factor must be in (0, 100]");
                // Clamp the delta, never the two ends on their own: clamping each end deforms the interval
                // while its keys keep the full delta. The ruler drag clamps the same way.
                const float delta =
                    slide ? std::clamp(requested, -m_intervalStart, durationFrames - m_intervalEnd) : 0.f;
                // Keys clamp into the clip range, so the band does too - a scale that reaches past frame
                // zero is the specified case, not an error.
                const float mappedStart = std::clamp(pivot + (m_intervalStart - pivot) * factor + delta, 0.f, durationFrames);
                const float mappedEnd = std::clamp(pivot + (m_intervalEnd - pivot) * factor + delta, 0.f, durationFrames);
                if (mappedEnd < mappedStart + 1.f)
                    return fail("the interval would collapse below one frame");
                const AnimationClip before = clip;
                const size_t changed = RemapKeyTimes(clip, m_intervalStart, m_intervalEnd, pivot, factor, delta);
                for (AnimationChannel &channel : clip.channels)
                    SortChannelKeys(channel);
                if (changed > 0)
                    commit(before, changed); // pushes the undo snapshot while the band is still the old one
                else if (std::abs(mappedStart - m_intervalStart) > kFrameEps ||
                         std::abs(mappedEnd - m_intervalEnd) > kFrameEps)
                    PushUndoSnapshot(before, m_intervalStart, m_intervalEnd); // an empty band that moved is undoable
                m_intervalStart = mappedStart;
                m_intervalEnd = mappedEnd;
                return ok({{"keys_moved", changed},
                           {"delta_frames", delta},
                           {"start", m_intervalStart},
                           {"end", m_intervalEnd}});
            }
            if (action == "timeline.tween")
            {
                if (m_intervalEnd < m_intervalStart + 2.f)
                    return fail("the interval must span at least 2 frames: set one with timeline.interval");
                const int everyN = args.value("every_n", 1);
                if (everyN < 1 || everyN > 64)
                    return fail("every_n must be between 1 and 64");
                const std::vector<int> bones =
                    args.contains("bones") || args.contains("chain") ? bonesArg(false) : IntervalBones();
                const AnimationClip before = clip;
                const size_t changed = TweenBones(clip, bones, m_intervalStart, m_intervalEnd, everyN,
                                                  AnimationClipTools::TweenMode::SampleClip);
                commit(before, changed);
                m_tweenStatus = changed > 0 ? "Tween wrote " + std::to_string(changed) + " keys."
                                            : TweenEmptyReason(skeleton, clip, bones);
                return ok({{"keys_written", changed},
                           {"start", m_intervalStart},
                           {"end", m_intervalEnd},
                           {"status", m_tweenStatus}});
            }
            if (action == "timeline.motion.analyze")
            {
                const Analysis analysis = Analyze(clip, skeleton);
                for (size_t issue = 0; issue < m_motionIssueCounts.size(); ++issue)
                    m_motionIssueCounts[issue] = analysis.Count(static_cast<IssueType>(issue));
                nlohmann::json result = {{"total", analysis.issues.size()},
                                         {"quaternion_flips", m_motionIssueCounts[0]},
                                         {"loop_pose_seams", m_motionIssueCounts[1]},
                                         {"loop_velocity_seams", m_motionIssueCounts[2]},
                                         {"root_drift", m_motionIssueCounts[3]},
                                         {"jitter", m_motionIssueCounts[4]},
                                         {"redundant_keys", m_motionIssueCounts[5]}};
                const int bone = boneArg();
                if (args.contains("bone") && (bone < 0 || bone >= skeleton.GetBoneCount()))
                    return fail("unknown bone");
                if (bone >= 0 && bone < skeleton.GetBoneCount() && clip.duration > 0.f)
                {
                    const WorldDriftResult drift = AnalyzeWorldPositionDrift(clip,
                                                                             skeleton,
                                                                             bone,
                                                                             0.f,
                                                                             clip.duration,
                                                                             std::max(m_frameTicks, 0.00001f));
                    if (drift)
                        result["selected_bone_world_drift"] = drift.maxDrift;
                }
                return ok(result);
            }
            if (action == "timeline.motion.fix_quaternions")
            {
                const AnimationClip before = clip;
                const size_t changed = FixQuaternionHemisphereFlips(clip);
                commit(before, changed);
                return ok({{"keys_fixed", changed}});
            }
            if (action == "timeline.motion.smooth")
            {
                const float startFrame = args.value("start_frame", 0.f);
                const float endFrame = args.value("end_frame", durationFrames);
                const float strength = args.value("strength", 0.5f);
                const int passes = args.value("passes", 1);
                if (!std::isfinite(startFrame) || !std::isfinite(endFrame) || startFrame < 0.0f ||
                    endFrame > durationFrames || startFrame > endFrame)
                    return fail("smooth range must be finite and inside the clip");
                if (!std::isfinite(strength) || strength < 0.0f || strength > 1.0f)
                    return fail("smooth strength must be between 0 and 1");
                if (passes < 1 || passes > MaxSmoothPasses)
                    return fail("smooth passes must be between 1 and " + std::to_string(MaxSmoothPasses));
                SmoothSettings settings;
                settings.startTime = ToTicks(startFrame);
                settings.endTime = ToTicks(endFrame);
                settings.strength = strength;
                settings.passes = passes;
                settings.channels = channelMask();
                const std::vector<int> bones = bonesArg(false);
                const AnimationClip before = clip;
                const size_t changed = Smooth(clip, settings, bones);
                commit(before, changed);
                return ok({{"keys_smoothed", changed}});
            }
            if (action == "timeline.motion.simplify")
            {
                SimplifySettings settings;
                settings.positionTolerance = args.value("position_tolerance", settings.positionTolerance);
                settings.rotationToleranceDegrees = args.value("rotation_tolerance_degrees",
                                                               settings.rotationToleranceDegrees);
                settings.scaleTolerance = args.value("scale_tolerance", settings.scaleTolerance);
                if (!std::isfinite(settings.positionTolerance) ||
                    !std::isfinite(settings.rotationToleranceDegrees) ||
                    !std::isfinite(settings.scaleTolerance) || settings.positionTolerance < 0.0f ||
                    settings.rotationToleranceDegrees < 0.0f || settings.scaleTolerance < 0.0f)
                    return fail("simplify tolerances must be finite and non-negative");
                settings.channels = channelMask();
                const std::vector<int> bones = bonesArg(false);
                const AnimationClip before = clip;
                const size_t changed = Simplify(clip, settings, bones);
                commit(before, changed);
                return ok({{"keys_removed", changed}});
            }
            if (action == "timeline.motion.make_cyclic")
            {
                const AnimationClip before = clip;
                const size_t changed = MakeCyclic(clip, channelMask());
                commit(before, changed);
                return ok({{"keys_written", changed}});
            }
            if (action == "timeline.motion.mirror_pose")
            {
                const float sourceFrame = args.value("source_frame", currentFrame);
                const float targetFrame = args.value("target_frame", currentFrame);
                if (!std::isfinite(sourceFrame) || !std::isfinite(targetFrame))
                    return fail("source_frame and target_frame must be finite");
                const std::vector<int> bones = bonesArg(false);
                const AnimationClip before = clip;
                const size_t changed = PasteMirroredPose(clip,
                                                         skeleton,
                                                         ToTicks(sourceFrame),
                                                         ToTicks(targetFrame),
                                                         bones,
                                                         args.value("include_center", true),
                                                         channelMask());
                commit(before, changed);
                return ok({{"keys_written", changed}});
            }
            if (action == "timeline.motion.breakdown")
            {
                const int bone = boneArg();
                if (bone < 0 || bone >= skeleton.GetBoneCount())
                    return fail("select a bone or pass bone");
                const float frame = args.value("frame", currentFrame);
                const float bias = args.value("bias", 0.5f);
                if (!std::isfinite(frame) || frame < 0.f || frame > durationFrames)
                    return fail("frame must be inside the clip range");
                if (!std::isfinite(bias) || bias < 0.f || bias > 1.f)
                    return fail("bias must be between 0 and 1");
                const AnimationClip before = clip;
                const AnimationPoseTools::BreakdownResult breakdown =
                    AnimationPoseTools::InsertBreakdown(clip, skeleton, bone, ToTicks(frame), bias);
                if (!breakdown)
                    return fail("breakdown insertion failed with status " +
                                std::to_string(static_cast<int>(breakdown.status)));
                commit(before, breakdown.keysWritten);
                return ok({{"bone", bone},
                           {"frame", frame},
                           {"bias", breakdown.appliedBias},
                           {"previous_frame", ToFrame(breakdown.previousTime)},
                           {"next_frame", ToFrame(breakdown.nextTime)},
                           {"channel", breakdown.channelIndex},
                           {"keys_written", breakdown.keysWritten}});
            }
            if (action == "timeline.motion.offset_bone")
            {
                const std::vector<int> bones = bonesArg(false);
                if (bones.empty())
                    return fail("select a bone or pass bones");
                const float deltaFrames = args.value("delta_frames", 1.f);
                if (!std::isfinite(deltaFrames) || !std::isfinite(ToTicks(deltaFrames)))
                    return fail("delta_frames must be finite");
                const AnimationClip before = clip;
                size_t changed = 0;
                for (int bone : bones)
                    changed += OffsetBoneKeyTimes(clip,
                                                  bone,
                                                  ToTicks(deltaFrames),
                                                  args.value("wrap", false),
                                                  channelMask());
                commit(before, changed);
                return ok({{"bones", bones.size()}, {"keys_touched", changed}});
            }
            if (action == "timeline.ballistic")
            {
                if (!HasInterval())
                    return fail("mark an interval first (timeline.interval)");
                // The button falls back to the root, but a bone named in the request is honoured or refused -
                // a typo must never quietly bake the arc onto a different curve.
                int bone = m_activeBone;
                if (args.contains("bone"))
                {
                    bone = boneArg();
                    if (bone < 0 || bone != BallisticBone(skeleton, clip, bone))
                        return fail("bone must be a root bone or already carry position keys");
                }
                else
                    bone = BallisticBone(skeleton, clip, bone);
                if (bone < 0)
                    return fail("ballistic needs a root bone, or one that already has position keys");
                const float gravity = args.value("gravity", m_gravity);
                if (!std::isfinite(gravity) || gravity < 0.f)
                    return fail("gravity must be finite and not negative");
                const bool body = args.value("body", m_ballisticBody);
                const AnimationClip before = clip;
                const size_t written = BakeBallistic(skeleton, clip, bone, gravity, body);
                commit(before, written);
                return ok({{"bone", skeleton.bones[bone].name}, {"keys_written", written}, {"gravity", gravity}, {"body", body}});
            }
            if (action == "timeline.balance_bake")
            {
                float start = m_intervalStart, end = m_intervalEnd;
                if (args.contains("start_frame") || args.contains("end_frame"))
                {
                    start = args.value("start_frame", 0.f);
                    end = args.value("end_frame", ToFrame(clip.duration));
                    if (!std::isfinite(start) || !std::isfinite(end) || start < 0.f || end > ToFrame(clip.duration) + kFrameEps ||
                        end < start + 1.f)
                        return fail("start_frame / end_frame must lie inside the clip, at least one frame apart");
                }
                else if (!HasInterval())
                    return fail("mark an interval first (timeline.interval), or pass start_frame / end_frame");
                std::string status, report;
                if (!PoseViewport(*m_rigEditor)->BakeBalance(scene, start, end, status, &report))
                    return fail(status);
                nlohmann::json result = nlohmann::json::parse(report);
                result["status"] = status;
                return ok(result);
            }
            if (action == "timeline.motion.spring_bake")
            {
                const std::vector<int> bones = bonesArg(true);
                if (bones.empty())
                    return fail("pass an ordered contiguous chain in bones or chain");
                SpringBakeSettings settings;
                settings.framesPerSecond = args.value("fps", clip.ticksPerSecond / m_frameTicks);
                settings.frameStep = args.value("frame_step", settings.frameStep);
                settings.stiffness = args.value("stiffness", settings.stiffness);
                settings.damping = args.value("damping", settings.damping);
                settings.response = args.value("response", settings.response);
                settings.drag = args.value("drag", settings.drag);
                settings.cyclicWarmupCycles = args.value("warmup_cycles", settings.cyclicWarmupCycles);
                const std::string endpoint = args.value("endpoint", "preserve");
                if (endpoint == "free")
                    settings.endpointMode = SpringEndpointMode::Free;
                else if (endpoint == "cyclic")
                    settings.endpointMode = SpringEndpointMode::Cyclic;
                else if (endpoint != "preserve")
                    return fail("unknown endpoint (free|preserve|cyclic)");
                if (args.contains("start_frame") || args.contains("end_frame"))
                {
                    const float startFrame = args.value("start_frame", 0.f);
                    const float endFrame = args.value("end_frame", ToFrame(clip.duration));
                    if (!std::isfinite(startFrame) || !std::isfinite(endFrame) || startFrame < 0.f ||
                        endFrame > ToFrame(clip.duration) + kFrameEps || endFrame < startFrame + 1.f)
                        return fail("start_frame/end_frame must span at least one frame inside the clip");
                    settings.startTime = ToTicks(startFrame);
                    settings.endTime = ToTicks(endFrame);
                }
                const AnimationClip before = clip;
                const SpringBakeResult baked = BakeSecondarySpring(clip, skeleton, bones, settings);
                if (!baked)
                {
                    static const char *reasons[] = {"", "the chain is empty", "the clip has no usable duration",
                                                    "the spring settings or frame range are out of bounds (Cyclic needs the whole clip)",
                                                    "the chain names an unknown bone", "the bones must be a directly parented chain, root to tip"};
                    return fail(std::string("spring bake failed: ") + reasons[static_cast<int>(baked.status)]);
                }
                commit(before, baked.keysWritten);
                return ok({{"bones_baked", baked.bonesBaked},
                           {"keys_written", baked.keysWritten},
                           {"samples", baked.sampleCount},
                           {"sample_step_ticks", baked.sampleStepTicks},
                           {"max_angular_lag_degrees", baked.maxAngularLagDegrees}});
            }
            if (action == "timeline.motion.world_drift" || action == "timeline.motion.stabilize_world")
            {
                const int bone = boneArg();
                if (bone < 0 || bone >= skeleton.GetBoneCount())
                    return fail("select a bone or pass bone");
                const float start = ToTicks(args.value("start_frame", currentFrame));
                const float end = ToTicks(args.value("end_frame", durationFrames));
                const float step = ToTicks(args.value("sample_step_frames", 1.f));
                if (action == "timeline.motion.world_drift")
                {
                    const WorldDriftResult drift = AnalyzeWorldPositionDrift(clip, skeleton, bone, start, end, step);
                    if (!drift)
                        return fail("world drift analysis failed with status " +
                                    std::to_string(static_cast<int>(drift.status)));
                    return ok({{"bone", bone}, {"samples", drift.sampleCount}, {"max_world_drift", drift.maxDrift}});
                }
                int compensation = -1;
                if (args.contains("compensation_bone"))
                {
                    compensation = parseBone(args["compensation_bone"]);
                    if (compensation < 0 || compensation >= skeleton.GetBoneCount())
                        return fail("unknown compensation_bone");
                }
                const AnimationClip before = clip;
                const WorldDriftResult stabilized = StabilizeWorldPosition(clip,
                                                                           skeleton,
                                                                           bone,
                                                                           start,
                                                                           end,
                                                                           step,
                                                                           compensation);
                if (!stabilized)
                    return fail("world stabilization failed with status " +
                                std::to_string(static_cast<int>(stabilized.status)));
                commit(before, stabilized.keysWritten);
                return ok({{"bone", bone},
                           {"compensation_bone", stabilized.compensationBoneIndex},
                           {"samples", stabilized.sampleCount},
                           {"keys_written", stabilized.keysWritten},
                           {"max_world_drift_before", stabilized.maxDrift},
                           {"max_world_drift_after", stabilized.maxRemainingDrift}});
            }
            return fail("unknown timeline motion action");
        }
        catch (const std::exception &error)
        {
            return fail(std::string("invalid action arguments: ") + error.what());
        }
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
        if (play)
            m_restDisplayed = false;
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
        PushUndoSnapshot(snapshot, m_intervalStart, m_intervalEnd);
    }

    void AnimationTimeline::PushUndoSnapshot(const AnimationClip &snapshot, float intervalStart, float intervalEnd)
    {
        m_undo.push_back({snapshot, m_selectedClip, intervalStart, intervalEnd,
                          m_poseViewport ? m_poseViewport->LockSnapshot() : std::vector<RigLock>{}});
        if (static_cast<int>(m_undo.size()) > kMaxUndo)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        m_dirty = true;
    }

    void AnimationTimeline::Undo(AnimationClip &clip)
    {
        if (m_undo.empty() || m_undo.back().clipIndex != m_selectedClip)
            return;
        m_redo.push_back({clip, m_selectedClip, m_intervalStart, m_intervalEnd,
                          m_poseViewport ? m_poseViewport->LockSnapshot() : std::vector<RigLock>{}});
        clip = m_undo.back().clip;
        m_intervalStart = m_undo.back().intervalStart;
        m_intervalEnd = m_undo.back().intervalEnd;
        if (m_poseViewport)
            m_poseViewport->RestoreSessionLocks(m_undo.back().locks);
        m_undo.pop_back();
        SelClear();
        m_dirty = true;
    }

    void AnimationTimeline::Redo(AnimationClip &clip)
    {
        if (m_redo.empty() || m_redo.back().clipIndex != m_selectedClip)
            return;
        m_undo.push_back({clip, m_selectedClip, m_intervalStart, m_intervalEnd,
                          m_poseViewport ? m_poseViewport->LockSnapshot() : std::vector<RigLock>{}});
        clip = m_redo.back().clip;
        m_intervalStart = m_redo.back().intervalStart;
        m_intervalEnd = m_redo.back().intervalEnd;
        if (m_poseViewport)
            m_poseViewport->RestoreSessionLocks(m_redo.back().locks);
        m_redo.pop_back();
        SelClear();
        m_dirty = true;
    }

    void AnimationTimeline::DropPendingRequests()
    {
        m_pendingPoses.clear();
        m_pendingClipSet = false;
        m_pendingBone.clear();
        m_pendingFrame = -1.f;
        m_pendingPlay = -1;
        m_pendingSave = false;
        m_pendingRest = false;
    }

    void AnimationTimeline::DropTarget()
    {
        m_restDisplayed = false;
        m_tabSuspended = false;
        m_tabResumePlaying = false;
        m_tabResumeRestPose = false;
        m_ownershipSceneGeneration = ~uint32_t{0};
        ResetEditState();
        DropPendingRequests();
        m_editModel = nullptr;
        m_targetNode = nullptr;
        m_lastModel = nullptr;
        m_animatedNodes.clear();
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
        EndViewportRotate();
        m_dirty = false;
        m_fitPending = true;
        m_frameTicks = 0.f; // re-detect the frame grid for the new clip
        m_boneExpanded.clear();
        m_boneSelected.clear();
        m_activeBone = -1;
        ClearInterval(); // frames from the previous clip mean nothing in this one
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

    AnimationInterpolation AnimationTimeline::KeyInterpolation(const AnimationClip &clip, const KeyRef &ref) const
    {
        if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()) || ref.keyIdx < 0)
            return AnimationInterpolation::Linear;
        const AnimationChannel &chan = clip.channels[ref.channelIdx];
        switch (ref.type)
        {
        case KeyType::Position:
            return ref.keyIdx < static_cast<int>(chan.positionKeys.size()) ? chan.positionKeys[ref.keyIdx].interpolation : AnimationInterpolation::Linear;
        case KeyType::Rotation:
            return ref.keyIdx < static_cast<int>(chan.rotationKeys.size()) ? chan.rotationKeys[ref.keyIdx].interpolation : AnimationInterpolation::Linear;
        case KeyType::Scale:
            return ref.keyIdx < static_cast<int>(chan.scaleKeys.size()) ? chan.scaleKeys[ref.keyIdx].interpolation : AnimationInterpolation::Linear;
        }
        return AnimationInterpolation::Linear;
    }

    void AnimationTimeline::SetKeyInterpolation(AnimationClip &clip, const KeyRef &ref, AnimationInterpolation interpolation)
    {
        if (ref.channelIdx < 0 || ref.channelIdx >= static_cast<int>(clip.channels.size()) || ref.keyIdx < 0)
            return;
        AnimationChannel &chan = clip.channels[ref.channelIdx];
        switch (ref.type)
        {
        case KeyType::Position:
            if (ref.keyIdx < static_cast<int>(chan.positionKeys.size()))
                chan.positionKeys[ref.keyIdx].interpolation = interpolation;
            break;
        case KeyType::Rotation:
            if (ref.keyIdx < static_cast<int>(chan.rotationKeys.size()))
                chan.rotationKeys[ref.keyIdx].interpolation = interpolation;
            break;
        case KeyType::Scale:
            if (ref.keyIdx < static_cast<int>(chan.scaleKeys.size()))
                chan.scaleKeys[ref.keyIdx].interpolation = interpolation;
            break;
        }
    }

    bool AnimationTimeline::DrawInterpolationMenu(AnimationClip &clip)
    {
        if (!ImGui::BeginMenu("Interpolation", !m_selectedKeys.empty()))
            return false;

        AnimationInterpolation common = AnimationInterpolation::Linear;
        bool first = true;
        bool mixed = false;
        for (const KeyRef &ref : m_selectedKeys)
        {
            const AnimationInterpolation interpolation = KeyInterpolation(clip, ref);
            if (first)
            {
                common = interpolation;
                first = false;
            }
            else
                mixed = mixed || interpolation != common;
        }

        AnimationInterpolation chosen = common;
        bool picked = false;
        if (ImGui::MenuItem("Linear", nullptr, !mixed && common == AnimationInterpolation::Linear))
        {
            chosen = AnimationInterpolation::Linear;
            picked = true;
        }
        if (ImGui::MenuItem("Smooth", nullptr, !mixed && common == AnimationInterpolation::Smooth))
        {
            chosen = AnimationInterpolation::Smooth;
            picked = true;
        }
        if (ImGui::MenuItem("Stepped", nullptr, !mixed && common == AnimationInterpolation::Stepped))
        {
            chosen = AnimationInterpolation::Stepped;
            picked = true;
        }
        ImGui::EndMenu();

        if (!picked || (!mixed && chosen == common))
            return false;
        PushUndo(clip);
        for (const KeyRef &ref : m_selectedKeys)
            SetKeyInterpolation(clip, ref, chosen);
        return true;
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
            e.interpolation = KeyInterpolation(clip, ref);
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
                chan.positionKeys.push_back({t, e.posValue, e.interpolation});
                SelAdd({e.channelIdx, KeyType::Position, static_cast<int>(chan.positionKeys.size()) - 1});
                break;
            case KeyType::Rotation:
                chan.rotationKeys.push_back({t, e.rotValue, e.interpolation});
                SelAdd({e.channelIdx, KeyType::Rotation, static_cast<int>(chan.rotationKeys.size()) - 1});
                break;
            case KeyType::Scale:
                chan.scaleKeys.push_back({t, e.sclValue, e.interpolation});
                SelAdd({e.channelIdx, KeyType::Scale, static_cast<int>(chan.scaleKeys.size()) - 1});
                break;
            }
        }
    }

    // Blender "Insert Keyframe" (I): keys the bone's current (evaluated) Loc/Rot/Scl at the frame.
    void AnimationTimeline::InsertKeyframe(AnimationClip &clip, const Skeleton &skeleton, int bone, float frameIn)
    {
        if (bone < 0 || bone >= skeleton.GetBoneCount())
            return;
        const float time = ToTicks(frameIn);
        const int ci = EnsureChannel(clip, bone);
        vec3 pos, scl;
        quat rot;
        AnimationEvaluator::SampleChannel(clip.channels[ci], skeleton.bones[bone], time, pos, rot, scl);
        SetPoseKey(clip, ci, time, pos, rot, scl);
        // One choke point for every user insert (I, dope double-click, the context menu, the pose bar):
        // a key inside the interval is a new extreme, so both halves follow it.
        const int keyed[1] = {bone};
        RetweenAroundFrame(clip, keyed, frameIn);
    }

    namespace
    {
        // Overwrite-or-insert one key; a new key inherits the interpolation of the key before it.
        template <class K, class V>
        void SetKeyAt(std::vector<K> &keys, float time, const V &value)
        {
            const int at = FindKeyAtTime(keys, time);
            if (at >= 0)
            {
                keys[at].value = value;
                return;
            }
            AnimationInterpolation interpolation = AnimationInterpolation::Linear;
            const auto next = std::lower_bound(keys.begin(), keys.end(), time,
                                               [](const K &key, float t)
                                               { return key.time < t; });
            if (next != keys.begin())
                interpolation = std::prev(next)->interpolation;
            keys.push_back({time, value, interpolation});
            std::sort(keys.begin(), keys.end(), [](const K &a, const K &b)
                      { return a.time < b.time; });
        }
    } // namespace

    void AnimationTimeline::SetPoseKey(AnimationClip &clip, int channelIdx, float time, const vec3 &pos, const quat &rot,
                                       const vec3 &scl)
    {
        AnimationChannel &chan = clip.channels[channelIdx];
        SetKeyAt(chan.positionKeys, time, pos);
        SetKeyAt(chan.rotationKeys, time, rot);
        SetKeyAt(chan.scaleKeys, time, scl);
    }

    void AnimationTimeline::SetRotationKey(AnimationClip &clip, int channelIdx, float time, const quat &rot)
    {
        SetKeyAt(clip.channels[channelIdx].rotationKeys, time, glm::normalize(rot));
    }

    void AnimationTimeline::SetPositionKey(AnimationClip &clip, int channelIdx, float time, const vec3 &pos)
    {
        SetKeyAt(clip.channels[channelIdx].positionKeys, time, pos);
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
            const float y = rowTop + rowIdx * m_rowHeight + m_rowHeight * 0.5f;
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
            const float rowY = rowTop + r * m_rowHeight;
            const bool visible = rowY + m_rowHeight >= visibleTop && rowY <= visibleBottom;
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
        if (rowTop + m_rowHeight >= visibleTop && rowTop <= visibleBottom)
            emitRow(0, summary, 6.f);
    }

    void AnimationTimeline::ScrollWheel(float visibleHeight, float contentHeight)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f && !ImGui::GetIO().KeyCtrl)
            m_scrollY -= wheel * m_rowHeight * 3.f;
        m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, contentHeight - visibleHeight));
    }

    // Thin Blender-style scrollbar on the right edge of a region; drag the thumb or wheel to scroll.
    void AnimationTimeline::DrawVScrollbar(const ImVec2 &origin, const ImVec2 &size, float contentHeight)
    {
        const float visible = size.y - m_rulerHeight;
        if (contentHeight <= visible || visible <= 0.f)
        {
            m_scrollDragging = false;
            return;
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float x0 = origin.x + size.x - kScrollbarWidth - 2.f, x1 = origin.x + size.x - 2.f;
        const float y0 = origin.y + m_rulerHeight + 2.f, y1 = origin.y + size.y - 2.f;
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
    void AnimationTimeline::DrawPanelMode()
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        drawList->AddRectFilled(start, {start.x + width, start.y + m_headerHeight}, bl::kHeaderBg);
        drawList->AddLine({start.x, start.y + m_headerHeight - 0.5f},
                          {start.x + width, start.y + m_headerHeight - 0.5f}, IM_COL32(0, 0, 0, 90));
        // SameLine after SetCursorScreenPos keeps the previous line's Y, so Animate sat higher than Rig.
        const float y = start.y + std::floor((m_headerHeight - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::SetCursorScreenPos({start.x + 8.f, y});
        if (ImGui::RadioButton("Rig", m_rigMode))
            m_rigMode = true;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, y});
        if (ImGui::RadioButton("Animate", !m_rigMode))
            m_rigMode = false;
        ImGui::SetCursorScreenPos({start.x, start.y + m_headerHeight});
    }

    bool AnimationTimeline::DrawHeader(Scene &scene, AnimationSystem *anim, ModelAsset *model, AnimationClip &clip,
                                       float currentFrame, bool showEditorMode)
    {
        const size_t clipCountBefore = model->GetAnimations().size();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(p, {p.x + w, p.y + m_headerHeight * 2.f}, bl::kHeaderBg);
        dl->AddLine({p.x, p.y + m_headerHeight - 0.5f}, {p.x + w, p.y + m_headerHeight - 0.5f}, IM_COL32(0, 0, 0, 90));
        ImGui::SetCursorScreenPos({p.x + 6.f, p.y + 4.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f, 3.f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.f, 4.f});

        if (showEditorMode)
        {
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("Graph Editor").x + ImGui::GetStyle().FramePadding.x * 2.f + ImGui::GetFrameHeight());
            const char *modeNames[] = {"Dope Sheet", "Graph Editor"};
            int modeIdx = static_cast<int>(m_mode);
            if (ImGui::Combo("##mode", &modeIdx, modeNames, 2))
            {
                m_mode = static_cast<Mode>(modeIdx);
                m_fitPending = true;
            }
            ui::ItemTooltip("Editor mode: Dope Sheet (keyframes per channel) or Graph Editor (F-curves).");
            ImGui::SameLine();
        }

        // Action (clip) selector + management
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
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
                        m_frameTicks = DetectFrameTicks(clips[i]);
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
            ImGui::SetCursorScreenPos({p.x, p.y + m_headerHeight * 2.f});
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
            ImGui::SetCursorScreenPos({p.x + 6.f, p.y + m_headerHeight + 4.f});
        else
            ImGui::SameLine(0.f, 14.f);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.f);
        const float durationFrames = ToFrame(clip.duration);
        int frame = static_cast<int>(std::round(currentFrame));
        if (ImGui::DragInt("##frame", &frame, 0.2f, 0, static_cast<int>(durationFrames), "%d"))
            SetFrame(scene, anim, static_cast<float>(frame));
        ui::ItemTooltip("Current frame.");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "Start");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
        int start = 0;
        ImGui::BeginDisabled();
        ImGui::DragInt("##start", &start, 1.f, 0, 0);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "End");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
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
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.f);
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
            ImGui::SetCursorScreenPos({p.x + 6.f, p.y + m_headerHeight + 4.f});
        if (IconButton("##autokey", Icon::Record,
                       "Auto Key: viewport bone rotation inserts a key at the current frame. When off, only an existing rotation key can be edited.",
                       m_autoKey))
            m_autoKey = !m_autoKey;
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
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.f);
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
        ImGui::SameLine();
        if (ImGui::Button("Motion Doctor"))
            ImGui::OpenPopup("Motion Doctor##timeline");
        ui::ItemTooltip("Analyze and clean animation curves, mirror poses, bake follow-through, and stabilize a bone in world position.");
        DrawMotionDoctor();

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
        ImGui::SetCursorScreenPos({p.x, p.y + m_headerHeight * 2.f});
        return false;
    }

    void AnimationTimeline::DrawMotionDoctor()
    {
        ImGui::SetNextWindowSize({430.f, 0.f}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("Motion Doctor##timeline"))
            return;

        auto run = [&](const char *action, const nlohmann::json &args = nlohmann::json::object())
        {
            m_motionStatus = HandleAction(action, args.dump());
        };
        if (ImGui::Button("Analyze Clip"))
            run("timeline.motion.analyze");
        ui::ItemTooltip("Scan the action for quaternion flips, loop seams, root drift, jitter spikes and redundant keys. Changes nothing.");
        ImGui::SameLine();
        ImGui::TextDisabled("Q %zu  Pose %zu  Vel %zu  Drift %zu  Jitter %zu  Extra %zu",
                            m_motionIssueCounts[0],
                            m_motionIssueCounts[1],
                            m_motionIssueCounts[2],
                            m_motionIssueCounts[3],
                            m_motionIssueCounts[4],
                            m_motionIssueCounts[5]);
        ui::ItemTooltip("Issues found by the last Analyze: Q quaternion hemisphere flips, Pose and Vel loop seams between the last and first frame, Drift root drift, Jitter velocity spikes, Extra redundant keys.");

        if (ImGui::Button("Fix Quats"))
            run("timeline.motion.fix_quaternions");
        ui::ItemTooltip("Negate rotation keys into the previous key's hemisphere so interpolation stops taking the long way round.");
        ImGui::SameLine();
        if (ImGui::Button("Smooth Selected"))
            run("timeline.motion.smooth");
        ui::ItemTooltip("Average the interior keys of the selected bones; the first and last key of every curve stay put.");
        ImGui::SameLine();
        if (ImGui::Button("Simplify Selected"))
            run("timeline.motion.simplify");
        ui::ItemTooltip("Drop the selected bones' keys that the curve through their kept neighbours already reproduces within tolerance.");
        if (ImGui::Button("Make Cyclic"))
            run("timeline.motion.make_cyclic");
        ui::ItemTooltip("Write the frame-zero pose onto the end of the action so it loops without a pop.");
        ImGui::SameLine();
        if (ImGui::Button("Paste Mirrored Pose"))
            run("timeline.motion.mirror_pose");
        ui::ItemTooltip("Mirror the current pose across the rig X plane onto the current frame, swapping .L and .R bones.");

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.f);
        ImGui::SliderFloat("Breakdown bias", &m_breakdownBias, 0.f, 1.f, "%.2f");
        ui::ItemTooltip("Where the breakdown sits between the surrounding keys: 0 is the previous pose, 1 the next.");
        ImGui::SameLine();
        if (ImGui::Button("Insert Breakdown"))
            run("timeline.motion.breakdown", {{"bias", m_breakdownBias}});
        ui::ItemTooltip("Write one complete pose for the active bone at the current frame, biased between its surrounding keys.");

        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
        ImGui::InputInt("Frame offset", &m_motionOffsetFrames);
        ui::ItemTooltip("Frames to shift keys by; negative values shift them earlier.");
        ImGui::SameLine();
        if (ImGui::Button("Offset Selected Bones"))
            run("timeline.motion.offset_bone", {{"delta_frames", m_motionOffsetFrames}});
        ui::ItemTooltip("Slide the selected bones' keys in time, so a chain stops moving in mechanical lockstep.");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputTextWithHint("##springchain",
                                 "ordered chain: scarf.01, scarf.02, scarf.03",
                                 m_springChainBuf,
                                 sizeof(m_springChainBuf));
        ui::ItemTooltip("Directly parented bones from root to tip, comma separated.");
        if (ImGui::Button("Bake Spring Chain"))
            run("timeline.motion.spring_bake", {{"chain", m_springChainBuf}});
        ui::ItemTooltip("Simulate the chain above as a damped spring and bake the result as ordinary rotation keys.");
        ImGui::SameLine();
        if (ImGui::Button("Measure World Drift"))
            run("timeline.motion.world_drift");
        ui::ItemTooltip("Report how far the active bone travels in world space over the action. Changes nothing.");
        ImGui::SameLine();
        if (ImGui::Button("Stabilize World Position"))
            run("timeline.motion.stabilize_world");
        ui::ItemTooltip("Pin the active bone at its start world position by baking compensation into an ancestor - this is how a sliding foot gets planted.");

        if (!m_motionStatus.empty())
        {
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 410.f);
            ImGui::TextDisabled("%s", m_motionStatus.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndPopup();
    }

    void AnimationTimeline::DrawClipPopups(Scene &scene, AnimationSystem *anim, ModelAsset *model)
    {
        auto &clips = model->GetMutableAnimations();
        if (ImGui::BeginPopup("New Action"))
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.5f);
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
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.5f);
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

        const float rowTop = origin.y + m_rulerHeight - m_scrollY;
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y}) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        // The panel is as tall as its content now and the window scrolls it, so the widget rect reaches well
        // past the screen: rows are culled against what is actually visible (PushClipRect already intersected
        // this with the window), not against the rect, or every row of a big rig is emitted every frame.
        const float viewTop = std::max(dl->GetClipRectMin().y, origin.y + m_rulerHeight);
        const float viewBottom = dl->GetClipRectMax().y;

        for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
        {
            const Row &row = m_rows[r];
            const float y0 = rowTop + r * m_rowHeight;
            const float y1 = y0 + m_rowHeight;
            if (y1 < viewTop || y0 > viewBottom)
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

            const bool rowHovered = hovered && mouse.y >= y0 && mouse.y < y1 && mouse.y >= origin.y + m_rulerHeight;
            if (rowHovered)
                dl->AddRectFilled({origin.x, y0}, {origin.x + size.x, y1}, bl::kRowHover);

            float x = origin.x + 8.f;
            const float cy = y0 + m_rowHeight * 0.5f;
            if (row.bone < 0)
            {
                dl->AddText({x, cy - ImGui::GetTextLineHeight() * 0.5f}, bl::kText, "Summary");
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
                dl->AddText({x, cy - ImGui::GetTextLineHeight() * 0.5f}, hasKeys ? bl::kText : bl::kTextDim, skeleton.bones[row.bone].name.c_str());
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
                dl->AddText({x + 10.f, cy - ImGui::GetTextLineHeight() * 0.5f}, bl::kText, names[row.type]);
            }
        }

        if (m_scrollToActive)
        {
            m_scrollToActive = false;
            for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
            {
                if (m_rows[r].bone != m_activeBone || m_rows[r].type >= 0)
                    continue;
                const float visible = size.y - m_rulerHeight;
                const float top = r * m_rowHeight;
                if (top < m_scrollY)
                    m_scrollY = top;
                else if (top + m_rowHeight > m_scrollY + visible)
                    m_scrollY = top + m_rowHeight - visible;
                m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, m_contentHeight - visible));
                break;
            }
        }
        if (hovered)
            ScrollWheel(size.y - m_rulerHeight, m_contentHeight);

        // Header strip above the rows (aligned with the ruler)
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + m_rulerHeight}, bl::kRulerBg);
        dl->AddLine({origin.x, origin.y + m_rulerHeight - 0.5f}, {origin.x + size.x, origin.y + m_rulerHeight - 0.5f}, bl::kRowLine);
        dl->AddText({origin.x + 8.f, origin.y + (m_rulerHeight - ImGui::GetTextLineHeight()) * 0.5f}, bl::kTextDim, "Channels");
        dl->AddLine({origin.x + size.x - 0.5f, origin.y}, {origin.x + size.x - 0.5f, origin.y + size.y}, IM_COL32(0, 0, 0, 120));
        dl->PopClipRect();
    }

    // -------------------------------------------------------------------------
    // ruler (scrub region)
    // -------------------------------------------------------------------------
    void AnimationTimeline::ClearInterval()
    {
        m_intervalStart = 0.f;
        m_intervalEnd = -1.f;
        m_intervalDrag = 0;
        m_intervalScale = false;
    }

    std::vector<int> AnimationTimeline::IntervalBones() const
    {
        std::vector<int> bones;
        for (int bone = 0; bone < static_cast<int>(m_boneSelected.size()); bone++)
            if (m_boneSelected[bone])
                bones.push_back(bone);
        return bones; // empty = every keyed bone, like the Motion Doctor tools
    }

    // One entry for the button and the action: the root's own arc, or the centre-of-mass arc with the root
    // corrected per frame from the rig's mass model.
    size_t AnimationTimeline::BakeBallistic(const Skeleton &skeleton, AnimationClip &clip, int bone, float gravity, bool body)
    {
        const vec3 rest = BindTranslation(skeleton.bones[bone]);
        const float start = ToTicks(m_intervalStart), end = ToTicks(m_intervalEnd), step = ToTicks(1.f);
        if (!body)
            return AnimationClipTools::BallisticInterval(clip, bone, rest, start, end, step, gravity);
        std::vector<float> masses;
        std::vector<vec3> centres;
        PoseViewport(*m_rigEditor)->BodyMasses(skeleton, masses, centres);
        return AnimationClipTools::BallisticBodyInterval(clip, skeleton, bone, rest, masses, centres, start, end, step, gravity);
    }

    // The chain the pose-bar Spring bakes: the selected bones (the action orders them root to tip), or the active
    // bone alone. One bone extends down through single-child descendants, so the root of a tail takes the whole tail.
    std::vector<int> AnimationTimeline::SpringChain(const Skeleton &skeleton) const
    {
        std::vector<int> chain = IntervalBones();
        if (chain.empty() && m_activeBone >= 0 && m_activeBone < skeleton.GetBoneCount())
            chain.push_back(m_activeBone);
        if (chain.size() == 1)
            while (static_cast<int>(chain.size()) < skeleton.GetBoneCount())
            {
                int only = -1, count = 0;
                for (int i = 0; i < skeleton.GetBoneCount(); i++)
                    if (skeleton.bones[i].parentIndex == chain.back())
                        only = i, count++;
                if (count != 1)
                    break;
                chain.push_back(only);
            }
        return chain; // the bake refuses a chain that is not directly parented
    }

    // The arc is root translation. A requested bone qualifies only when it is a root itself or already owns a
    // Location curve (mocap rigs keep translation on Hips under a keyless control root); posing never gives a
    // child one, so baking an arc there would invent the curve the pose tools refuse to create.
    int AnimationTimeline::BallisticBone(const Skeleton &skeleton, const AnimationClip &clip, int requested) const
    {
        const int boneCount = skeleton.GetBoneCount();
        if (requested >= 0 && requested < boneCount &&
            (skeleton.bones[requested].parentIndex < 0 ||
             std::any_of(clip.channels.begin(), clip.channels.end(), [requested](const AnimationChannel &channel)
                         { return channel.boneIndex == requested && !channel.positionKeys.empty(); })))
            return requested;
        // A selected arm or hand carries no translation, so the arc belongs to the root instead of failing:
        // in the Timeline you almost always have a child bone active.
        for (int i = 0; i < boneCount; i++)
            if (skeleton.bones[i].parentIndex < 0)
                return i;
        return -1;
    }

    size_t AnimationTimeline::TweenBones(AnimationClip &clip, std::span<const int> bones, float startFrame,
                                         float endFrame, int everyN, AnimationClipTools::TweenMode mode)
    {
        if (m_frameTicks <= 0.f)
            return 0;
        return AnimationClipTools::TweenInterval(clip, ToTicks(startFrame), ToTicks(endFrame),
                                                 ToTicks(static_cast<float>(std::max(everyN, 1))), mode, bones);
    }

    size_t AnimationTimeline::RemapKeyTimes(AnimationClip &clip, float fromFrame, float toFrame, float pivot,
                                            float factor, float delta)
    {
        const float from = ToTicks(fromFrame), to = ToTicks(toFrame);
        const float pivotTicks = ToTicks(pivot), deltaTicks = ToTicks(delta);
        const float tolerance = ToTicks(kFrameEps); // inclusive ends, in the clip's own tick scale
        size_t touched = 0;
        auto remap = [&](auto &keys)
        {
            std::vector<char> moved(keys.size(), 0);
            for (size_t i = 0; i < keys.size(); i++)
                if (keys[i].time >= from - tolerance && keys[i].time <= to + tolerance)
                {
                    keys[i].time =
                        std::clamp(pivotTicks + (keys[i].time - pivotTicks) * factor + deltaTicks, 0.f, clip.duration);
                    moved[i] = 1;
                    ++touched;
                }
            // A moved key overwrites the key it lands on, like dragging one key onto another; two moved
            // keys stacked by the clamp keep the later source key, like OffsetBoneKeyTimes.
            for (int i = static_cast<int>(keys.size()) - 1; i >= 0; i--)
                for (size_t j = 0; j < keys.size(); j++)
                    if (static_cast<int>(j) != i && std::abs(keys[j].time - keys[i].time) <= tolerance &&
                        moved[j] && (!moved[i] || static_cast<int>(j) > i))
                    {
                        keys.erase(keys.begin() + i);
                        moved.erase(moved.begin() + i);
                        // Selected keys are held by index: dropping one here would leave the selection
                        // pointing at a different key, so it goes.
                        SelClear();
                        break;
                    }
        };
        for (AnimationChannel &channel : clip.channels)
        {
            remap(channel.positionKeys);
            remap(channel.rotationKeys);
            remap(channel.scaleKeys);
        }
        return touched;
    }

    size_t AnimationTimeline::RetweenAroundFrame(AnimationClip &clip, std::span<const int> bones, float frame)
    {
        if (!HasInterval() || frame <= m_intervalStart + kFrameEps || frame >= m_intervalEnd - kFrameEps)
            return 0; // no interval, or the pose IS one of the extremes
        // The pose at this frame is the new extreme, so both halves are rebuilt from it.
        return TweenBones(clip, bones, m_intervalStart, frame, 1, AnimationClipTools::TweenMode::RebuildFromEnds) +
               TweenBones(clip, bones, frame, m_intervalEnd, 1, AnimationClipTools::TweenMode::RebuildFromEnds);
    }

    // Cascadeur-style interval: Alt-drag the ruler marks a span of frames the tools treat as one object.
    // Dragging the band slides every key inside it, an edge resizes, Shift+edge scales those keys from the
    // far edge. The playhead band keeps the click it would have had, so scrubbing is unchanged.
    bool AnimationTimeline::DrawInterval(Scene &scene, AnimationSystem *anim, AnimationClip &clip, float currentFrame,
                                         const ImVec2 &origin, const ImVec2 &size, bool hovered)
    {
        const float durationFrames = ToFrame(clip.duration);
        if (HasInterval())
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const float xs = FrameToPx(m_intervalStart), xe = FrameToPx(m_intervalEnd);
            dl->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            dl->AddRectFilled({xs, origin.y}, {xe, origin.y + size.y}, bl::kInterval);
            dl->AddLine({xs, origin.y}, {xs, origin.y + size.y}, bl::kIntervalEdge, 2.f);
            dl->AddLine({xe, origin.y}, {xe, origin.y + size.y}, bl::kIntervalEdge, 2.f);
            dl->PopClipRect();
        }
        if (m_modal != Modal::None)
            return false;

        const float mouseX = ImGui::GetMousePos().x;
        const float mouseFrame = std::clamp(SnapFrame(PxToFrame(mouseX)), 0.f, durationFrames);
        if (hovered && m_intervalDrag == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (ImGui::GetIO().KeyAlt)
            {
                m_intervalDrag = 4;
                m_intervalGrab = mouseFrame;
                m_intervalStart = m_intervalEnd = mouseFrame;
            }
            else if (HasInterval() && std::abs(mouseX - FrameToPx(currentFrame)) > 5.f)
            {
                const float xs = FrameToPx(m_intervalStart), xe = FrameToPx(m_intervalEnd);
                const int part = std::abs(mouseX - xs) <= 4.f   ? 2
                                 : std::abs(mouseX - xe) <= 4.f ? 3
                                 : (mouseX > xs && mouseX < xe) ? 1
                                                                : 0;
                if (part == 0)
                    ClearInterval(); // clicking the ruler outside the band drops it (and still scrubs)
                else
                {
                    m_intervalDrag = part;
                    m_intervalScale = part != 1 && ImGui::GetIO().KeyShift;
                    m_intervalGrab = mouseFrame;
                    m_intervalDragStart = m_intervalStart;
                    m_intervalDragEnd = m_intervalEnd;
                    m_intervalSnapshot = clip;
                    // The interval is a time span, not the key selection: every drag update restores the
                    // snapshot, so selected indices would drift against a re-sorted clip. Drop them once.
                    SelClear();
                }
            }
        }
        if (m_intervalDrag == 0)
            return false;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const bool movedKeys = (m_intervalDrag == 1 || m_intervalScale) &&
                                   (std::abs(m_intervalStart - m_intervalDragStart) > kFrameEps ||
                                    std::abs(m_intervalEnd - m_intervalDragEnd) > kFrameEps);
            if (m_intervalDrag == 4 && !HasInterval())
                ClearInterval(); // an Alt-click, or a drag too short to hold a frame
            else if (movedKeys)
                PushUndoSnapshot(m_intervalSnapshot, m_intervalDragStart, m_intervalDragEnd);
            m_intervalDrag = 0;
            m_intervalScale = false;
            m_intervalSnapshot = {};
            return true;
        }

        // Slide and scale rebuild from the pre-drag clip, so dragging back and forth never compounds.
        auto remapKeys = [&](float pivot, float factor, float delta)
        {
            clip = m_intervalSnapshot;
            RemapKeyTimes(clip, m_intervalDragStart, m_intervalDragEnd, pivot, factor, delta);
            SortAndRemapSelection(clip);
            ReevaluatePose(scene, anim);
        };

        if (m_intervalDrag == 4)
        {
            m_intervalStart = std::min(m_intervalGrab, mouseFrame);
            m_intervalEnd = std::max(m_intervalGrab, mouseFrame);
        }
        else if (m_intervalDrag == 1)
        {
            const float delta =
                std::clamp(mouseFrame - m_intervalGrab, -m_intervalDragStart, durationFrames - m_intervalDragEnd);
            m_intervalStart = m_intervalDragStart + delta;
            m_intervalEnd = m_intervalDragEnd + delta;
            remapKeys(0.f, 1.f, delta);
        }
        else
        {
            const bool left = m_intervalDrag == 2;
            const float pivot = left ? m_intervalDragEnd : m_intervalDragStart;
            const float edge = left ? std::min(mouseFrame, pivot - 1.f) : std::max(mouseFrame, pivot + 1.f);
            m_intervalStart = left ? edge : m_intervalDragStart;
            m_intervalEnd = left ? m_intervalDragEnd : edge;
            if (m_intervalScale && std::abs((left ? m_intervalDragStart : m_intervalDragEnd) - pivot) > kFrameEps)
                remapKeys(pivot, (edge - pivot) / ((left ? m_intervalDragStart : m_intervalDragEnd) - pivot), 0.f);
        }
        return true;
    }

    void AnimationTimeline::DrawRuler(Scene &scene, AnimationSystem *anim, AnimationClip &clip, float currentFrame,
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
        const bool intervalDrag = DrawInterval(scene, anim, clip, currentFrame, origin, size, hovered);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_modal == Modal::None && !intervalDrag)
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

        // Same as the channel region: cull against the visible band, not the full-height widget rect.
        const float viewTop = dl->GetClipRectMin().y, viewBottom = dl->GetClipRectMax().y;

        // row backgrounds
        for (int r = 0; r < static_cast<int>(m_rows.size()); r++)
        {
            const Row &row = m_rows[r];
            const float y0 = rowTop + r * m_rowHeight, y1 = y0 + m_rowHeight;
            if (y1 < viewTop || y0 > viewBottom)
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
        BuildGlyphs(skeleton, clip, origin.x, rowTop, viewTop, viewBottom);
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
                const int r = static_cast<int>((mouse.y - rowTop) / m_rowHeight);
                if (r >= 0 && r < static_cast<int>(m_rows.size()) && m_rows[r].bone >= 0)
                {
                    PushUndo(clip);
                    InsertKeyframe(clip, skeleton, m_rows[r].bone, std::clamp(SnapFrame(mouseFrame), 0.f, ToFrame(clip.duration)));
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
                        InsertKeyframe(clip, skeleton, b, std::round(currentFrame));
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
            if (DrawInterpolationMenu(clip))
                ReevaluatePose(scene, anim);
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

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && HasInterval())
            ClearInterval();

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
                            alt ? DeleteKeyframesAtFrame(clip, b, std::round(currentFrame)) : InsertKeyframe(clip, skeleton, b, std::round(currentFrame));
                }
                else
                    alt ? DeleteKeyframesAtFrame(clip, m_activeBone, std::round(currentFrame)) : InsertKeyframe(clip, skeleton, m_activeBone, std::round(currentFrame));
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
        BuildRows(skeleton);
        m_contentHeight = m_rows.size() * m_rowHeight;
        // Every channel row is drawn: the window's own scrollbar carries the whole panel, so the sheet has no
        // inner vertical scroller and never hides a bone. The pose bar and the Pose Locks stack above it must
        // not squash it either, hence the row floor.
        m_scrollY = 0.f;
        const float minBody = m_rulerHeight + m_rowHeight * kMinBodyRows + m_hScrollHeight;
        const ImVec2 size(avail.x, std::max({avail.y - m_statusHeight, minBody,
                                             m_rulerHeight + m_contentHeight + m_hScrollHeight}));

        m_keyLeft = origin.x + m_channelWidth;
        m_keyWidth = std::max(size.x - m_channelWidth - kRightMargin, 10.f);
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

        const ImVec2 body(size.x, size.y - m_hScrollHeight);
        DrawRuler(scene, anim, clip, currentFrame, {m_keyLeft, origin.y}, {body.x - m_channelWidth, m_rulerHeight});
        DrawKeyArea(scene, anim, skeleton, clip, currentFrame, {m_keyLeft, origin.y + m_rulerHeight}, {body.x - m_channelWidth, body.y - m_rulerHeight});
        DrawChannelRegion(skeleton, clip, origin, {m_channelWidth, body.y});
        DrawHScrollbar({origin.x, origin.y + body.y}, {size.x, m_hScrollHeight}, ToFrame(clip.duration));
        ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    }

    // -------------------------------------------------------------------------
    // graph editor (Blender F-curves: one curve per channel component, matching the evaluator)
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
        float y = origin.y + m_rulerHeight - m_scrollY;
        const float viewTop = std::max(dl->GetClipRectMin().y, origin.y + m_rulerHeight);
        const float viewBottom = dl->GetClipRectMax().y;

        // bones
        for (int b = 0; b < skeleton.GetBoneCount(); b++, y += m_rowHeight)
        {
            if (y + m_rowHeight < viewTop || y > viewBottom)
                continue;
            const bool sel = m_boneSelected[b];
            dl->AddRectFilled({origin.x, y}, {origin.x + size.x, y + m_rowHeight}, sel ? (b == m_activeBone ? bl::kRowActive : bl::kRowSelected) : ((b & 1) ? bl::kGroupRowAlt : bl::kGroupRow));
            dl->AddLine({origin.x, y + m_rowHeight - 0.5f}, {origin.x + size.x, y + m_rowHeight - 0.5f}, bl::kRowLine);
            int depth = 0;
            for (int parent = skeleton.bones[b].parentIndex; parent >= 0 && depth < 12; parent = skeleton.bones[parent].parentIndex)
                depth++;
            const bool hasKeys = ChannelForBone(clip, b) >= 0;
            dl->AddText({origin.x + 10.f + depth * 10.f, y + (m_rowHeight - ImGui::GetTextLineHeight()) * 0.5f}, hasKeys ? bl::kText : bl::kTextDim, skeleton.bones[b].name.c_str());
            if (hovered && mouse.y >= y && mouse.y < y + m_rowHeight && mouse.y >= origin.y + m_rulerHeight && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
            if (y + m_rowHeight >= origin.y + m_rulerHeight && y <= origin.y + size.y)
            {
                const bool hidden = m_curveHidden[slot] != 0;
                dl->AddRectFilled({origin.x, y}, {origin.x + size.x, y + m_rowHeight}, bl::kSubRow);
                dl->AddRectFilled({origin.x + 12.f, y + 5.f}, {origin.x + 16.f, y + m_rowHeight - 5.f}, hidden ? bl::kTextDim : cols[slot], 1.f);
                dl->AddText({origin.x + 24.f, y + (m_rowHeight - ImGui::GetTextLineHeight()) * 0.5f}, hidden ? bl::kTextDim : bl::kText, labels[slot]);
                // eye toggle
                const ImVec2 e(origin.x + size.x - 16.f, y + m_rowHeight * 0.5f);
                dl->AddCircle(e, 4.f, hidden ? bl::kTextDim : bl::kText, 12, 1.2f);
                if (!hidden)
                    dl->AddCircleFilled(e, 1.6f, bl::kText, 8);
                if (hovered && mouse.y >= y && mouse.y < y + m_rowHeight && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m_curveHidden[slot] = hidden ? 0 : 1;
                    m_curveFitPending = true;
                }
            }
            y += m_rowHeight;
        }

        if (hovered)
            ScrollWheel(size.y - m_rulerHeight, m_contentHeight);
        dl->AddRectFilled(origin, {origin.x + size.x, origin.y + m_rulerHeight}, bl::kRulerBg);
        dl->AddText({origin.x + 8.f, origin.y + (m_rulerHeight - ImGui::GetTextLineHeight()) * 0.5f}, bl::kTextDim, "F-Curves");
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
            dl->AddLine({origin.x + m_axisWidth, y}, {end.x, y}, std::abs(v) < 1e-6f ? IM_COL32(255, 255, 255, 60) : bl::kGridMajor);
            char buf[24];
            snprintf(buf, sizeof(buf), "%.3g", std::abs(v) < 1e-6f ? 0.f : v);
            dl->AddText({origin.x + 4.f, y - ImGui::GetTextLineHeight() * 0.5f}, bl::kTextDim, buf);
        }
        dl->AddRectFilled(origin, {origin.x + m_axisWidth, end.y}, IM_COL32(0, 0, 0, 40));

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
                {
                    const AnimationInterpolation interpolation = KeyInterpolation(clip, {c.channelIdx, c.type, k - 1});
                    if (interpolation == AnimationInterpolation::Stepped)
                    {
                        const ImVec2 corner(pt.x, prev.y);
                        dl->AddLine(prev, corner, c.color, 1.5f);
                        dl->AddLine(corner, pt, c.color, 1.5f);
                    }
                    else if (interpolation == AnimationInterpolation::Smooth || c.type == KeyType::Rotation)
                    {
                        const int samples = std::clamp(static_cast<int>(std::abs(pt.x - prev.x) / 12.f), 4, 24);
                        ImVec2 samplePrev = prev;
                        for (int sample = 1; sample <= samples; sample++)
                        {
                            const float u = static_cast<float>(sample) / static_cast<float>(samples);
                            const float factor = AnimationEvaluator::ApplyInterpolation(interpolation, u);
                            float sampleValue = 0.f;
                            switch (c.type)
                            {
                            case KeyType::Position:
                                sampleValue = glm::mix(chan.positionKeys[k - 1].value, chan.positionKeys[k].value, factor)[c.axis];
                                break;
                            case KeyType::Rotation:
                            {
                                const quat q = glm::slerp(chan.rotationKeys[k - 1].value, chan.rotationKeys[k].value, factor);
                                sampleValue = c.axis == 0 ? q.w : c.axis == 1 ? q.x
                                                              : c.axis == 2   ? q.y
                                                                              : q.z;
                                break;
                            }
                            case KeyType::Scale:
                                sampleValue = glm::mix(chan.scaleKeys[k - 1].value, chan.scaleKeys[k].value, factor)[c.axis];
                                break;
                            }
                            if (m_normalize)
                                sampleValue = (sampleValue - c.mid) / c.half;
                            const ImVec2 samplePoint(prev.x + (pt.x - prev.x) * u, ValueToPx(sampleValue));
                            dl->AddLine(samplePrev, samplePoint, c.color, 1.5f);
                            samplePrev = samplePoint;
                        }
                    }
                    else
                        dl->AddLine(prev, pt, c.color, 1.5f);
                }
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
            if (DrawInterpolationMenu(clip))
                ReevaluatePose(scene, anim);
            ImGui::Separator();
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
        const float minBody = m_rulerHeight + m_rowHeight * kMinBodyRows + m_hScrollHeight;
        const ImVec2 size(avail.x, std::max(avail.y - m_statusHeight, minBody));

        BuildRows(skeleton);
        m_keyLeft = origin.x + m_channelWidth + m_axisWidth;
        m_keyWidth = std::max(size.x - m_channelWidth - m_axisWidth - kRightMargin, 10.f);
        m_contentHeight = (skeleton.GetBoneCount() + 10) * m_rowHeight + 6.f; // bones + 10 component toggles
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

        const ImVec2 body(size.x, size.y - m_hScrollHeight);
        DrawRuler(scene, anim, clip, currentFrame, {origin.x + m_channelWidth, origin.y}, {body.x - m_channelWidth, m_rulerHeight});
        DrawCurveArea(scene, anim, clip, clip, currentFrame, {origin.x + m_channelWidth, origin.y + m_rulerHeight}, {body.x - m_channelWidth, body.y - m_rulerHeight});
        DrawCurveChannels(skeleton, clip, origin, {m_channelWidth, body.y});
        DrawVScrollbar({origin.x + m_channelWidth, origin.y}, {body.x - m_channelWidth, body.y}, m_contentHeight);
        DrawHScrollbar({origin.x, origin.y + body.y}, {size.x, m_hScrollHeight}, ToFrame(clip.duration));
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

    // Blender's N-panel Transform for the active bone: Loc / Rot / Scale relative to the bind pose at the
    // current frame; every edit writes the keys of that frame.
    void AnimationTimeline::DrawPoseBar(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                                        float currentFrame)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(p, {p.x + w, p.y + m_headerHeight}, bl::kHeaderBg);
        dl->AddLine({p.x, p.y + 0.5f}, {p.x + w, p.y + 0.5f}, IM_COL32(0, 0, 0, 90));
        ImGui::SetCursorScreenPos({p.x + 6.f, p.y + 4.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f, 3.f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.f, 4.f});
        const int bone = m_activeBone >= 0 && m_activeBone < skeleton.GetBoneCount() ? m_activeBone : -1;
        if (bone < 0)
        {
            ImGui::TextDisabled("Pose: click a bone channel, then edit its Location / Rotation / Scale here (every edit keys the current frame).");
        }
        else
        {
            const float time = ToTicks(std::round(currentFrame));
            const int poseBones[1] = {bone}; // interval mode re-tweens the halves around a pose keyed here
            const int ci = ChannelForBone(clip, bone);
            const BoneInfo &info = skeleton.bones[bone];
            vec3 pos, scl, bindPos, bindScl;
            quat rot, bindRot;
            AnimationEvaluator::BindPose(info, bindPos, bindRot, bindScl);
            if (ci >= 0)
                AnimationEvaluator::SampleChannel(clip.channels[ci], info, time, pos, rot, scl);
            else
                pos = bindPos, rot = bindRot, scl = bindScl;
            vec3 loc = glm::inverse(bindRot) * (pos - bindPos); // Blender: pose location lives in the bone's rest frame
            vec3 sclRel = scl / bindScl;
            const quat delta = glm::normalize(glm::inverse(bindRot) * rot);
            // keep the typed Euler triplet while it still describes the key (eulerAngles picks other, equivalent triplets)
            if (!m_poseEditing && std::abs(glm::dot(glm::normalize(quat(glm::radians(m_poseEuler))), delta)) < 0.99999f)
                m_poseEuler = glm::degrees(glm::eulerAngles(delta));

            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "%s", info.name.c_str());
            ui::ItemTooltip("Active bone (click a bone channel to change). Values are relative to the bind pose.");
            bool changed = false, activated = false, active = false;
            auto field = [&](const char *label, const char *id, float *v, float speed, float lo, float hi, const char *fmt, const char *tip)
            {
                ImGui::SameLine(0.f, 12.f);
                ImGui::TextDisabled("%s", label);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 15.5f);
                changed |= ImGui::DragFloat3(id, v, speed, lo, hi, fmt);
                activated |= ImGui::IsItemActivated();
                active |= ImGui::IsItemActive();
                ui::ItemTooltip(tip);
            };
            field("Loc", "##poseloc", &loc.x, 0.002f, 0.f, 0.f, "%.3f", "Location offset from the bind pose, in the bone's own rest frame (Y along the bone).");
            field("Rot", "##poserot", &m_poseEuler.x, 0.5f, 0.f, 0.f, "%.1f", "Rotation (XYZ Euler, degrees) in the bone's own rest frame: Y = twist along the bone, X / Z = bends.");
            field("Scale", "##posescl", &sclRel.x, 0.01f, 0.001f, 100.f, "%.3f", "Scale relative to the bind pose.");
            m_poseEditing = active;
            if (activated)
                PushUndo(clip);
            if (changed)
            {
                const int c = EnsureChannel(clip, bone);
                SetPoseKey(clip, c, time, bindPos + bindRot * loc, glm::normalize(bindRot * quat(glm::radians(m_poseEuler))), bindScl * sclRel);
                m_dirty = true;
                ++m_poseEditSerial;
                m_poseEdits = {{-1.f, {bone}}};
                RetweenAroundFrame(clip, poseBones, std::round(currentFrame));
                ReevaluatePose(scene, anim);
            }
            ImGui::SameLine(0.f, 12.f);
            if (ImGui::Button("Key"))
            {
                PushUndo(clip);
                InsertKeyframe(clip, skeleton, bone, std::round(currentFrame)); // retweens the interval itself
                ++m_poseEditSerial;
                m_poseEdits = {{-1.f, {bone}}};
                ReevaluatePose(scene, anim);
            }
            ui::ItemTooltip("Key the bone's current pose at this frame (I).");
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
            {
                PushUndo(clip);
                SetPoseKey(clip, EnsureChannel(clip, bone), time, bindPos, bindRot, bindScl);
                m_poseEuler = vec3(0.f);
                ++m_poseEditSerial;
                m_poseEdits = {{-1.f, {bone}}};
                RetweenAroundFrame(clip, poseBones, std::round(currentFrame));
                ReevaluatePose(scene, anim);
            }
            ui::ItemTooltip("Key the bind pose at this frame (Blender Alt+G / Alt+R / Alt+S).");
        }
        ImGui::SameLine(0.f, 16.f);
        if (ImGui::Button("Rest Pose"))
            RestPoseAll(scene, anim);
        ui::ItemTooltip("Show the bind pose: pauses playback and clears the evaluated pose. No keys are written - "
                        "scrub, play or edit a pose to return to the clip.");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_intervalEnd < m_intervalStart + 2.f);
        if (ImGui::Button("Tween"))
        {
            const AnimationClip before = clip;
            const std::vector<int> bones = IntervalBones();
            const size_t written =
                TweenBones(clip, bones, m_intervalStart, m_intervalEnd, 1, AnimationClipTools::TweenMode::SampleClip);
            if (written > 0)
            {
                PushUndoSnapshot(before);
                ReevaluatePose(scene, anim);
            }
            m_tweenStatus = written > 0 ? "Tween wrote " + std::to_string(written) + " keys."
                                        : TweenEmptyReason(skeleton, clip, bones);
        }
        ui::ItemTooltip("Bake the in-between frames of the interval (Alt-drag the ruler to mark one) for the "
                        "selected bones, or every keyed bone when none are selected. The selected bone must already "
                        "have keys; Tween will not invent a curve. Keys what the clip already plays; the ends stay put.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        if (ImGui::Button("Ballistic"))
        {
            const int bone = BallisticBone(skeleton, clip, m_activeBone);
            const AnimationClip before = clip;
            const size_t written = bone < 0 ? 0 : BakeBallistic(skeleton, clip, bone, m_gravity, m_ballisticBody);
            if (written > 0)
            {
                PushUndoSnapshot(before);
                ReevaluatePose(scene, anim);
                m_tweenStatus = "Ballistic wrote " + std::to_string(written) + " keys on " +
                                skeleton.bones[bone].name + (m_ballisticBody ? " (centre of mass)." : ".");
            }
            else
                m_tweenStatus = bone < 0
                                    ? "Ballistic needs the root bone, or one that already has Location keys."
                                    : "Ballistic wrote nothing: this interval and gravity make no usable arc.";
        }
        ui::ItemTooltip("Throw the root through the interval instead of sliding it: the in-between frames follow "
                        "gravity from the launch speed the two ends imply, while the horizontal path stays straight. "
                        "The ends themselves are kept. Acts on the selected bone when it carries the translation, "
                        "otherwise on the skeleton root. Posing this bone again inside the same interval rebuilds "
                        "its curve from the ends, which flattens the arc - bake it last.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
        ImGui::DragFloat("##ballistic_gravity", &m_gravity, 0.05f, 0.f, 200.f, "g %.2f");
        ui::ItemTooltip("Gravity for the ballistic bake, in rig units per second squared. 9.81 is life-sized; "
                        "raise it for a snappier, more animated fall.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        ImGui::Checkbox("Body", &m_ballisticBody);
        ui::ItemTooltip("Throw the body's centre of mass instead of the root: on every frame the root is moved so the "
                        "mass of the posed limbs and carried props follows the arc (tucking the legs drops the hips, "
                        "swinging the shovel forward pulls the body after it), and the root turns against the limbs so "
                        "the body's angular momentum stays what it was - an arm thrown forward tips the body back where "
                        "the throw happens; both ends keep their rotation, so a swing spread evenly over the whole "
                        "flight shows nothing. Masses come from the rig's capsules and owned parts. Airborne spans "
                        "only - on the ground the feet would move with the root.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        if (ImGui::Button("Spring"))
        {
            nlohmann::json chain = nlohmann::json::array();
            for (int b : SpringChain(skeleton))
                chain.push_back(skeleton.bones[b].name);
            const nlohmann::json result = nlohmann::json::parse(HandleAction(
                "timeline.motion.spring_bake",
                nlohmann::json{{"bones", chain}, {"start_frame", m_intervalStart}, {"end_frame", m_intervalEnd}}.dump()));
            m_tweenStatus = result.value("ok", false)
                                ? "Spring wrote " + std::to_string(result.value("keys_written", 0)) + " keys on " +
                                      std::to_string(result.value("bones_baked", 0)) + " bones."
                                : "Spring wrote nothing: " + result.value("error", std::string("unknown error")) + ".";
        }
        ui::ItemTooltip("Follow-through: simulate the selected bones as a damped spring chain that lags and overshoots "
                        "what they are keyed to do, and bake the interval's interior frames from it (a scarf, a tail, a "
                        "loose arm). Select the chain root to tip, or one bone to take its whole tail; the ends keep "
                        "their keys. Bake it last - posing a chain bone inside the interval rebuilds its curve from the "
                        "ends. timeline.motion.spring_bake has the stiffness, damping and drag knobs.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine();
        if (ImGui::Button("Balance"))
        {
            std::string status;
            PoseViewport(*m_rigEditor)->BakeBalance(scene, m_intervalStart, m_intervalEnd, status);
            m_tweenStatus = status;
        }
        ui::ItemTooltip("Stand the body over its feet on every grounded frame of the interval: the root shifts so the "
                        "zero-moment point - the centre of mass, led by its acceleration (h/g) - sits over the feet "
                        "touching the ground, and each planted foot is bent back to where the frame had it - the hip "
                        "sway over the stance foot a walk needs; a body that stops short settles onto its heels. Feet "
                        "are the bones named foot / toe or holding a planted lock; contact is read from the clip; g is "
                        "the Ballistic knob. The interval ends keep their keys, airborne frames follow their grounded "
                        "neighbours (throw those with Ballistic or Body). Bake it after the poses.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();
        if (!m_tweenStatus.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_tweenStatus.c_str());
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos({p.x, p.y + m_headerHeight});
    }

    // Rest Pose: SHOW the bind pose (Blender's Rest Position). Pauses playback and clears the evaluated
    // joint matrices so skinning falls back to identity, which IS the bind pose. Nothing is keyed and the
    // clip is untouched; the next scrub, play or pose edit re-evaluates the clip and takes over again.
    void AnimationTimeline::RestPoseAll(Scene &scene, AnimationSystem *anim)
    {
        if (m_animatedNodes.empty())
            return;
        SetPlaying(scene, anim, false, false);
        for (NodeId *node : m_animatedNodes)
        {
            if (!scene.IsNodeAlive(node))
                continue;
            auto &rt = scene.GetNodeRuntime(node);
            rt.jointMatrices.clear();
            scene.MarkNodeDirty(node);
        }
        m_restDisplayed = true;
        if (m_gui)
            m_gui->NotifyChange();
    }

    void AnimationTimeline::EnforcePlaybackOwnership(Scene &scene, AnimationSystem *anim, bool ownsTarget)
    {
        for (uint32_t i = 0; i < scene.GetNodeCount(); i++)
        {
            NodeId *node = scene.GetNodeId(i);
            if (!anim->GetAnimationState(node) ||
                (ownsTarget && std::find(m_animatedNodes.begin(), m_animatedNodes.end(), node) != m_animatedNodes.end()))
                continue;
            anim->SetPaused(node, true);
            auto &joints = scene.GetNodeRuntime(node).jointMatrices;
            if (!joints.empty())
            {
                joints.clear();
                scene.MarkNodeDirty(node);
            }
        }
    }

    void AnimationTimeline::SetGraphMode(bool graph)
    {
        m_mode = graph ? Mode::GraphEditor : Mode::DopeSheet;
        m_fitPending = true;
        m_curveFitPending = true;
    }

    AnimationPoseViewport *AnimationTimeline::PoseViewport(RigEditor &rig)
    {
        if (!m_poseViewport)
            m_poseViewport = std::make_unique<AnimationPoseViewport>(*this, rig);
        return m_poseViewport.get();
    }

    void AnimationTimeline::DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin,
                                         const ImVec2 &imageSize, bool &hovered, bool &active)
    {
        hovered = false;
        active = false;
        if (!m_visible)
            return;
        if (m_rigMode)
            m_rigEditor->DrawViewport(scene, camera, imageMin, imageSize, hovered, active);
        else
            DrawPoseViewport(scene, camera, imageMin, imageSize, hovered, active);
    }

    bool AnimationTimeline::DrawPoseViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin,
                                             const ImVec2 &imageSize, bool &hovered, bool &active)
    {
        hovered = false;
        active = false;
        if (!m_open || !m_gui)
            return false;
        if (!m_editModel || !m_editModel->HasSkeleton())
            return false;
        ViewportPose pose;
        if (!GetViewportPose(m_editModel, pose))
            return false;
        PoseViewport(*m_rigEditor)->DrawViewport(scene, camera, imageMin, imageSize, hovered, active);
        return true;
    }

    // -------------------------------------------------------------------------
    // main
    // -------------------------------------------------------------------------
    void AnimationTimeline::Update()
    {
        if (!m_open)
        {
            m_visible = false;
            auto *rs = GetGlobalSystem<RendererSystem>();
            auto *anim = GetGlobalSystem<AnimationSystem>();
            if (rs && anim)
            {
                Scene &scene = rs->GetScene();
                if (!m_tabSuspended)
                {
                    m_tabSuspended = true;
                    m_tabResumePlaying = IsPlaying(anim);
                    m_tabResumeRestPose = m_restDisplayed;
                    RestPoseAll(scene, anim);
                    EnforcePlaybackOwnership(scene, anim, false);
                }
                else if (m_ownershipSceneGeneration != scene.GetGeneration())
                    EnforcePlaybackOwnership(scene, anim, false);
                m_ownershipSceneGeneration = scene.GetGeneration();
            }
            return;
        }

        ImGui::SetNextWindowSize({1360, 360}, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        // Target resolution and the timeline.* request drains below run even while this window is a
        // hidden docked tab: agent actions depend on m_editModel. Playback does not retain ownership.
        // The window keeps its own right-edge scrollbar (the whole panel scrolls, header to status bar) but
        // never takes the wheel: the wheel belongs to the key / curve areas for zoom.
        const bool visible = ImGui::Begin(m_name.c_str(), &m_open, ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        m_visible = visible;

        // The wheel over the panel scrolls the panel. NoScrollWithMouse keeps ImGui from doing this on its
        // own, so Ctrl+wheel reaches the timeline as zoom instead of scrolling the window at the same time.
        if (visible && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.f && ImGui::IsWindowHovered())
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() - ImGui::GetIO().MouseWheel * m_rowHeight * 3.f, 0.f,
                                         ImGui::GetScrollMaxY()));

        const float lineHeight = ImGui::GetTextLineHeight();
        m_rowHeight = std::round(lineHeight + 6.f);
        m_rulerHeight = std::round(lineHeight + 10.f);
        m_headerHeight = std::round(lineHeight + 16.f);
        m_statusHeight = std::round(lineHeight + 8.f);
        m_hScrollHeight = std::round(std::max(14.f, lineHeight));
        m_axisWidth = std::round(ImGui::CalcTextSize("-0.000").x + 12.f);
        m_channelWidth = std::round(std::max(210.f, ImGui::CalcTextSize("ForearmHand.R.001").x + 90.f));
        if (visible)
            DrawPanelMode();

        auto *rs = GetGlobalSystem<RendererSystem>();
        auto *anim = GetGlobalSystem<AnimationSystem>();
        if (!rs || !anim)
        {
            if (visible)
                ImGui::TextDisabled("  Animation system not available.");
            ImGui::End();
            return;
        }
        Scene &scene = rs->GetScene();
        m_rigEditor->ResolveTarget(scene);
        auto drawRigPanel = [&]()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.f, 8.f});
            ImGui::BeginChild("##rig_panel", {}, ImGuiChildFlags_AlwaysUseWindowPadding);
            m_rigEditor->DrawPanel(scene);
            ImGui::EndChild();
            ImGui::PopStyleVar();
        };

        NodeId *previousTarget = m_targetNode;
        if (!visible && !m_tabSuspended)
        {
            m_tabSuspended = true;
            m_tabResumePlaying = IsPlaying(anim);
            m_tabResumeRestPose = m_restDisplayed;
            NodeId *primary = m_animatedNodes.empty() ? nullptr : m_animatedNodes[0];
            if (const AnimationNodeState *state = primary ? anim->GetAnimationState(primary) : nullptr)
            {
                m_loop = state->loop;
                if (state->playing)
                    m_speed = state->speed;
            }
            RestPoseAll(scene, anim);
        }

        // Target = hierarchy selection (root or any node under an animated model).
        auto &selection = SelectionManager::Instance();
        m_targetNode = nullptr;
        if (selection.HasSelection())
        {
            NodeId *sel = selection.GetSelectedNode();
            if (scene.IsNodeAlive(sel))
                m_targetNode = AnimationRootOf(scene, anim, sel);
        }
        m_animatedNodes.clear();
        if (m_targetNode)
            CollectAnimatedNodes(scene, anim, m_targetNode);
        if (m_tabSuspended && m_targetNode != previousTarget)
        {
            m_tabResumePlaying = false;
            m_tabResumeRestPose = false;
            m_restDisplayed = false;
        }

        ModelAsset *model = nullptr;
        if (!m_animatedNodes.empty())
            model = scene.GetModelForNode(m_animatedNodes[0]);
        if (!model && m_targetNode)
            model = scene.GetModelForNode(m_targetNode);
        if (m_ownershipSceneGeneration != scene.GetGeneration() || m_targetNode != previousTarget)
            EnforcePlaybackOwnership(scene, anim, visible && model && model->HasSkeleton());
        m_ownershipSceneGeneration = scene.GetGeneration();
        // Follows the selection like the Rig Editor: nothing selected = no target.
        if (!model || !model->HasSkeleton())
        {
            m_editModel = nullptr;
            DropPendingRequests(); // a request queued with no target must not ambush the next selection
            if (visible)
            {
                if (m_rigMode)
                    drawRigPanel();
                else
                    ImGui::TextDisabled(m_targetNode ? "  The selected model has no skeleton (bake a rig first)."
                                                     : "  No model selected: select a node of a rigged model in the hierarchy.");
            }
            ImGui::End();
            return;
        }
        if (model != m_lastModel)
            DropPendingRequests(); // requests were aimed at the previous character
        if (model != m_lastModel || static_cast<int>(model->GetAnimations().size()) != m_lastClipCount)
        {
            ResetEditState();
            m_lastModel = model;
            m_lastClipCount = static_cast<int>(model->GetAnimations().size());
            m_selectedClip = 0;
        }
        m_editModel = model;
        auto &clips = model->GetMutableAnimations();
        // Requests naming unknown bones are dropped here, before they can force a placeholder action
        // into the asset or burn an undo step in the drain below.
        std::erase_if(m_pendingPoses, [&](const PendingPose &pp)
                      { return model->GetSkeleton().GetBoneIndex(pp.bone) < 0; });
        // A hidden tab must not mutate the asset: the placeholder Action appears only when the window
        // is shown or an agent request needs a clip to land in; requests that need a clip that does
        // not exist are dropped instead of ambushing the user when the tab is next shown.
        if (clips.empty() && !visible && !m_pendingClipSet && m_pendingPoses.empty())
        {
            DropPendingRequests();
            ImGui::End();
            return;
        }
        if (clips.empty())
        {
            AnimationClip fresh;
            fresh.name = m_pendingClipSet && !m_pendingClip.name.empty() ? m_pendingClip.name : "Action";
            fresh.duration = 48.f;
            fresh.ticksPerSecond = 24.f;
            clips.push_back(fresh);
            m_lastClipCount = 1;
            m_dirty = true;
        }
        // timeline.clip: select / create by name, then set the range (before the clip reference below is bound).
        if (m_pendingClipSet)
        {
            m_pendingClipSet = false;
            if (!m_pendingClip.name.empty())
            {
                int idx = -1;
                for (int i = 0; i < static_cast<int>(clips.size()); i++)
                    if (clips[i].name == m_pendingClip.name)
                        idx = i;
                if (idx < 0 && clips.size() == 1 && clips[0].channels.empty())
                {
                    clips[0].name = m_pendingClip.name; // the placeholder action created above, still unkeyed
                    idx = 0;
                }
                if (idx < 0)
                {
                    AnimationClip fresh;
                    fresh.name = m_pendingClip.name;
                    fresh.duration = 48.f;
                    fresh.ticksPerSecond = 24.f;
                    clips.push_back(fresh);
                    idx = static_cast<int>(clips.size()) - 1;
                    m_dirty = true;
                }
                if (idx != m_selectedClip)
                {
                    m_selectedClip = idx;
                    ResetEditState();
                }
                m_lastClipCount = static_cast<int>(clips.size());
                m_frameTicks = DetectFrameTicks(clips[idx]);
                SetFrame(scene, anim, 0.f);
            }
            AnimationClip &target = clips[std::clamp(m_selectedClip, 0, static_cast<int>(clips.size()) - 1)];
            if (m_frameTicks <= 0.f)
                m_frameTicks = DetectFrameTicks(target);
            if (m_pendingClip.end > 0.f)
                target.duration = ToTicks(m_pendingClip.end);
            if (m_pendingClip.fps > 0.f)
                target.ticksPerSecond = m_pendingClip.fps * m_frameTicks;
            if (m_pendingClip.end > 0.f || m_pendingClip.fps > 0.f)
                m_dirty = true;
            m_fitPending = true;
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
        float currentFrame = state ? ToFrame(state->time) : 0.f;
        if (state)
        {
            m_loop = state->loop;
            if (state->playing)
                m_speed = state->speed;
        }

        if (visible && m_tabSuspended)
        {
            const bool resumePlaying = m_tabResumePlaying;
            const bool resumeRestPose = m_tabResumeRestPose;
            m_tabSuspended = false;
            m_tabResumePlaying = false;
            m_tabResumeRestPose = false;
            if (!resumeRestPose)
            {
                SetFrame(scene, anim, currentFrame);
                if (resumePlaying)
                    SetPlaying(scene, anim, true, m_speed < 0.f);
            }
        }

        if (!visible)
        {
            if (m_pendingClipSet || m_pendingFrame >= 0.f || !m_pendingPoses.empty())
                m_tabResumeRestPose = false;
            if (m_pendingRest)
            {
                m_tabResumePlaying = false;
                m_tabResumeRestPose = true;
            }
            if (m_pendingPlay >= 0)
            {
                m_tabResumePlaying = m_pendingPlay != 0;
                if (m_pendingPlay != 0)
                    m_tabResumeRestPose = false;
            }
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
                m_scrollToActive = true;
            }
        }
        if (m_pendingFrame >= 0.f)
        {
            SetFrame(scene, anim, std::clamp(m_pendingFrame, 0.f, ToFrame(clip.duration)));
            // Poses queued in the same action pump land at the frame just requested, not the stale playhead.
            currentFrame = std::clamp(m_pendingFrame, 0.f, ToFrame(clip.duration));
            m_pendingFrame = -1.f;
        }
        if (!m_pendingPoses.empty())
        {
            PushUndo(clip);
            std::vector<PoseEdit> edits; // one group per frame (< 0 = playhead); the lock re-solve keys there
            std::vector<int> posedBones;
            float posedFrame = std::round(currentFrame);
            for (const PendingPose &pp : m_pendingPoses)
            {
                const int b = skeleton.GetBoneIndex(pp.bone);
                if (b < 0)
                    continue;
                const float time = ToTicks(pp.frame >= 0.f ? pp.frame : std::round(currentFrame));
                const int ci = EnsureChannel(clip, b);
                vec3 pos, scl, bindPos, bindScl;
                quat rot, bindRot;
                AnimationEvaluator::BindPose(skeleton.bones[b], bindPos, bindRot, bindScl);
                AnimationEvaluator::SampleChannel(clip.channels[ci], skeleton.bones[b], time, pos, rot, scl);
                if (pp.mask & 1)
                    pos = bindPos + bindRot * pp.loc;
                if (pp.mask & 2)
                    rot = glm::normalize(bindRot * quat(glm::radians(pp.rot)));
                if (pp.mask & 4)
                    scl = bindScl * pp.scl;
                SetPoseKey(clip, ci, time, pos, rot, scl);
                posedBones.push_back(b);
                posedFrame = pp.frame >= 0.f ? pp.frame : std::round(currentFrame);
                const float group = pp.frame >= 0.f ? pp.frame : -1.f;
                auto edit = std::find_if(edits.begin(), edits.end(), [&](const PoseEdit &e)
                                         { return e.frame == group; });
                if (edit == edits.end())
                    edit = edits.insert(edits.end(), {group, {}});
                edit->bones.push_back(b);
            }
            m_pendingPoses.clear();
            ++m_poseEditSerial;
            m_poseEdits = edits;
            RetweenAroundFrame(clip, posedBones, posedFrame);
            ReevaluatePose(scene, anim);
        }
        if (m_pendingRest)
        {
            m_pendingRest = false;
            RestPoseAll(scene, anim);
        }
        if (m_pendingPlay >= 0)
        {
            if (m_pendingPlay == 2)
                m_speed = -std::abs(m_speed);
            else if (m_pendingPlay == 1)
                m_speed = std::abs(m_speed);
            SetPlaying(scene, anim, visible && m_pendingPlay != 0, m_pendingPlay == 2);
            m_pendingPlay = -1;
        }
        if (m_pendingSave)
        {
            m_pendingSave = false;
            if (ModelAssetCooked::IsCookedPath(model->GetFilePath()) && ModelAssetCooked::WriteToFile(model, model->GetFilePath()))
                m_dirty = false;
        }

        if (!visible)
        {
            // Pose/agent requests may have reevaluated or started the newly resolved target above.
            if (!m_restDisplayed || IsPlaying(anim))
                RestPoseAll(scene, anim);
            ImGui::End();
            return;
        }

        if (DrawHeader(scene, anim, model, clip, currentFrame, !m_rigMode))
        {
            ImGui::End();
            return;
        }
        if (m_rigMode)
        {
            // The Rig panel scrubs a posed mesh for Joint Blend, so it gets the same frame ruler: the
            // interval band, its gestures and scrubbing are on both radios, not just Animate.
            const ImVec2 rulerOrigin = ImGui::GetCursorScreenPos();
            const float rulerWidth = ImGui::GetContentRegionAvail().x;
            m_keyLeft = rulerOrigin.x;
            m_keyWidth = std::max(rulerWidth - kRightMargin, 10.f);
            if (m_fitPending)
            {
                const float durationFrames = ToFrame(clip.duration);
                const float pad = std::max(durationFrames * 0.03f, 1.f);
                m_viewStart = -pad;
                m_viewEnd = durationFrames + pad;
                m_fitPending = false;
            }
            DrawRuler(scene, anim, clip, currentFrame, rulerOrigin, {rulerWidth, m_rulerHeight});
            if (HotkeysAllowed() && m_modal == Modal::None && ImGui::IsKeyPressed(ImGuiKey_Escape) && HasInterval())
                ClearInterval(); // the Animate hotkey path never runs here, and the ruler is shared
            ImGui::SetCursorScreenPos({rulerOrigin.x, rulerOrigin.y + m_rulerHeight});
            drawRigPanel();
            ImGui::End();
            return;
        }
        DrawPoseBar(scene, anim, skeleton, clip, currentFrame);
        PoseViewport(*m_rigEditor)->DrawControls(scene);
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
