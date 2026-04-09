#pragma once
#include "GUI/Widget.h"
#include "Animation/AnimationTypes.h"
#include "imgui/imgui.h"

namespace pe
{
    struct NodeId;
    class Scene;
    class ModelAsset;
    class AnimationSystem;

    // -----------------------------------------------------------------------
    // Phase 2 keyframe editing types
    // -----------------------------------------------------------------------
    enum class KeyType
    {
        Position,
        Rotation,
        Scale
    };

    struct KeyRef
    {
        int channelIdx = -1;
        KeyType type = KeyType::Position;
        int keyIdx = -1;
        int axis = -1; // -1 = all axes (dope sheet); 0/1/2 = component (curve editor)
        bool operator==(const KeyRef &o) const
        {
            return channelIdx == o.channelIdx && type == o.type && keyIdx == o.keyIdx &&
                   axis == o.axis;
        }
        bool IsValid() const { return channelIdx >= 0 && keyIdx >= 0; }
    };

    struct ClipboardEntry
    {
        KeyType type = KeyType::Position;
        float relTime = 0.f; // relative to earliest key in the copied selection
        int channelIdx = -1; // original channel index for paste-back
        vec3 posValue = {};
        quat rotValue = {1.f, 0.f, 0.f, 0.f};
        vec3 sclValue = {1.f, 1.f, 1.f};
    };

    // -----------------------------------------------------------------------

    class AnimationTimeline : public Widget
    {
    public:
        AnimationTimeline() : Widget("Animation Timeline") { m_open = false; }
        void Update() override;

    private:
        void DrawTransportBar(Scene &scene, AnimationSystem *anim, const AnimationClip &clip);
        void DrawTrackView(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton,
                           AnimationClip &clip, float currentTimeTicks);
        void DrawStatusBar(const Skeleton &skeleton, const AnimationClip &clip);

        // Time conversion helpers
        static std::string FormatTime(float ticks, float ticksPerSecond);
        float TicksToPixel(float ticks, float trackWidth) const;
        float PixelToTicks(float px, float trackWidth) const;

        // Broadcast transport commands to all animated children of the selected node
        template <typename Fn>
        void ForEachAnimatedNode(Fn &&fn)
        {
            for (NodeId *node : m_animatedNodes)
                fn(node);
        }

        // Collect all animated descendants recursively
        void CollectAnimatedNodes(Scene &scene, AnimationSystem *anim, NodeId *root);

        // Phase 3a: curve view
        void DrawCurveView(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton,
                           AnimationClip &clip, float currentTimeTicks);

        // ----------------------------------------------------------------
        // Phase 2: keyframe editing helpers
        // ----------------------------------------------------------------
        bool IsKeySelected(const KeyRef &ref) const;
        void SelectKey(const KeyRef &ref, bool additive);
        void DeleteSelectedKeys(AnimationClip &clip);
        void CopySelectedKeys(const AnimationClip &clip);
        void PasteKeys(AnimationClip &clip, float insertAtTicks);
        void InsertKeyframesAtTime(AnimationClip &clip, const Skeleton &skeleton, float timeTicks);
        void SortChannelKeys(AnimationChannel &chan);
        void ReevaluatePose(Scene &scene, AnimationSystem *anim, const AnimationClip &clip,
                            float currentTimeTicks);

        // ----------------------------------------------------------------
        // State — playback / view
        // ----------------------------------------------------------------
        NodeId *m_targetNode = nullptr;        // selected parent (model root)
        std::vector<NodeId *> m_animatedNodes; // cached animated children (rebuilt each frame)
        int m_selectedClip = 0;
        float m_visibleStartTicks = 0.f;
        float m_zoomLevel = 1.f; // 1.0 = full clip visible
        int m_selectedBone = -1;
        bool m_scrubbing = false;
        bool m_showPosition = true;
        bool m_showRotation = true;
        bool m_showScale = true;

        // Cached per-frame
        float m_clipDuration = 0.f;
        float m_ticksPerSecond = 25.f;
        ImVec2 m_trackOrigin = {}; // screen-space origin of track area (from DrawTrackView)
        float m_trackWidth = 0.f;  // width of track area in pixels
        float m_boneScrollY = 0.f; // synchronized scroll for bone list + track area

        // ----------------------------------------------------------------
        // State — Phase 3: curve editor
        // ----------------------------------------------------------------
        bool m_curveMode = false;
        bool m_curveFitDirty = true; // triggers Y-range auto-fit on next draw
        float m_curveYMin = -1.f;
        float m_curveYMax = 1.f;

        // ----------------------------------------------------------------
        // State — Phase 2: keyframe editing
        // ----------------------------------------------------------------
        std::vector<KeyRef> m_selectedKeys;
        bool m_draggingKeys = false;
        float m_dragAnchorTicks = 0.f;
        float m_dragAnchorValue = 0.f;       // curve-editor: value at drag start
        std::vector<float> m_dragOrigTimes;  // original times per selected key
        std::vector<float> m_dragOrigValues; // original values per selected key (curve mode)
        bool m_boxSelecting = false;
        ImVec2 m_boxSelectStart = {};
        ImVec2 m_boxSelectEnd = {};
        std::vector<ClipboardEntry> m_clipboard;
        ModelAsset *m_editModel = nullptr; // model for in-place clip editing

        static constexpr float kTrackRowHeight = 20.f;
        static constexpr float kBoneListWidth = 160.f;
        static constexpr float kTransportHeight = 32.f;
        static constexpr float kStatusHeight = 24.f;
        static constexpr float kRulerHeight = 30.f; // 2 text lines: frame# + seconds
        static constexpr float kYAxisWidth = 48.f;  // curve editor Y axis strip
        static constexpr float kMinZoom = 1.f;
        static constexpr float kMaxZoom = 50.f;
        static constexpr float kKeyHitRadius = 6.f;
    };
} // namespace pe
