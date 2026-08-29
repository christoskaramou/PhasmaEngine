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
        bool operator==(const KeyRef &o) const
        {
            return channelIdx == o.channelIdx && type == o.type && keyIdx == o.keyIdx;
        }
        bool IsValid() const { return channelIdx >= 0 && keyIdx >= 0; }
    };

    struct ClipboardEntry
    {
        KeyType type = KeyType::Position;
        float relTime = 0.f; // ticks, relative to the earliest copied key
        float absTime = 0.f; // ticks
        int channelIdx = -1;
        vec3 posValue = {};
        quat rotValue = {1.f, 0.f, 0.f, 0.f};
        vec3 sclValue = {1.f, 1.f, 1.f};
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    };

    // Blender-style animation editor: Dope Sheet / Action Editor (+ Graph Editor) with a timeline
    // header. Edits the selected model's AnimationClips in place; Save writes the .pemesh back.
    class AnimationTimeline : public Widget
    {
    public:
        AnimationTimeline() : Widget("Animation Timeline") { m_open = false; }
        void Update() override;

        // Programmatic route for editor actions (timeline.*): applied on the next Update.
        void SetGraphMode(bool graph);
        void RequestFrame(float frame) { m_pendingFrame = frame; }
        void RequestBone(const std::string &name) { m_pendingBone = name; }
        void RequestSave() { m_pendingSave = true; }
        void RequestPlay(bool play, bool reverse) { m_pendingPlay = play ? (reverse ? 2 : 1) : 0; }

        // timeline.pose: key one bone at a frame (<0 = current). loc / rot (degrees) / scale are relative to the
        // bind pose; mask bits 1/2/4 say which of them were given.
        struct PendingPose
        {
            std::string bone;
            float frame = -1.f;
            int mask = 0;
            vec3 loc = {};
            vec3 rot = {};
            vec3 scl = {1.f, 1.f, 1.f};
        };
        void RequestPose(const PendingPose &pose) { m_pendingPoses.push_back(pose); }
        // timeline.clip: select (or create) the action by name and/or set its end frame and fps.
        struct PendingClip
        {
            std::string name;
            float end = -1.f;
            float fps = -1.f;
        };
        void RequestClip(const PendingClip &clip)
        {
            m_pendingClip = clip;
            m_pendingClipSet = true;
        }

    private:
        enum class Mode
        {
            DopeSheet,
            GraphEditor
        };

        enum class Modal
        {
            None,
            Grab,
            Scale
        };

        // One visible line of the channel region. bone < 0 = Summary; type < 0 = bone (group) row,
        // else 0/1/2 = Location/Rotation/Scale sub-channel.
        struct Row
        {
            int bone = -1;
            int type = -1;
        };

        // One drawn diamond; refs into m_glyphRefs (a group glyph covers every key at that frame).
        struct Glyph
        {
            float x = 0.f, y = 0.f, size = 4.f;
            float time = 0.f;
            int row = 0;
            int refBegin = 0, refCount = 0;
            bool selected = false;
        };

        struct UndoEntry
        {
            AnimationClip clip;
            int clipIndex = -1;
        };

        // A visible F-curve: one component of one channel type of one bone.
        struct Curve
        {
            int channelIdx = -1;
            KeyType type = KeyType::Position;
            int axis = 0;
            ImU32 color = 0;
            float mid = 0.f, half = 1.f; // Normalize: value -> (value - mid) / half
        };

        struct CurvePoint
        {
            float x = 0.f, y = 0.f;
            KeyRef ref;
            int axis = 0;
            bool selected = false;
        };

        // --- frame <-> pixel ---
        float FrameToPx(float frame) const;
        float PxToFrame(float px) const;
        float SnapFrame(float frame) const;
        void FrameAll();
        void FrameSelected(const AnimationClip &clip);
        static void ZoomRange(float &lo, float &hi, float around, float factor, float minSpan);
        // Blender view2d navigation shared by the ruler, key and curve areas: wheel zoom, Ctrl/Shift+wheel
        // scroll, MMB pan, Ctrl+MMB zoom-drag. graph = also drive the value axis.
        void NavigateView(const ImVec2 &mouse, bool graph);
        static float DetectFrameTicks(const AnimationClip &clip);
        float ToFrame(float ticks) const;
        float ToTicks(float frame) const;

        // --- animation state plumbing ---
        void CollectAnimatedNodes(Scene &scene, AnimationSystem *anim, NodeId *root);
        void EnsureStates(Scene &scene, AnimationSystem *anim, float keepFrame);
        void SetFrame(Scene &scene, AnimationSystem *anim, float frame);
        void ReevaluatePose(Scene &scene, AnimationSystem *anim);
        void SetPlaying(Scene &scene, AnimationSystem *anim, bool play, bool reverse);
        bool IsPlaying(AnimationSystem *anim) const;

        // --- clip edits (all go through PushUndo) ---
        void PushUndo(AnimationClip &clip);
        void PushUndoSnapshot(const AnimationClip &snapshot);
        void Undo(AnimationClip &clip);
        void Redo(AnimationClip &clip);
        void ResetEditState();
        int ChannelForBone(const AnimationClip &clip, int bone) const;
        int EnsureChannel(AnimationClip &clip, int bone);
        static void SortChannelKeys(AnimationChannel &chan);
        void SortAndRemapSelection(AnimationClip &clip);
        float KeyTime(const AnimationClip &clip, const KeyRef &ref) const;  // ticks
        float KeyFrame(const AnimationClip &clip, const KeyRef &ref) const; // frames
        void SetKeyTime(AnimationClip &clip, const KeyRef &ref, float time);
        AnimationInterpolation KeyInterpolation(const AnimationClip &clip, const KeyRef &ref) const;
        void SetKeyInterpolation(AnimationClip &clip, const KeyRef &ref, AnimationInterpolation interpolation);
        bool DrawInterpolationMenu(AnimationClip &clip);
        static uint64_t PackKey(const KeyRef &ref);
        bool IsKeySelected(const KeyRef &ref) const;
        void SelClear();
        void SelAdd(const KeyRef &ref);
        void SelErase(const KeyRef &ref);
        void SelectKey(const KeyRef &ref, bool additive);
        void SelectAllKeys(const AnimationClip &clip, bool select);
        void DeleteSelectedKeys(AnimationClip &clip);
        void CopySelectedKeys(const AnimationClip &clip);
        void PasteKeys(AnimationClip &clip, float atFrame, bool keepTimes);
        void InsertKeyframe(AnimationClip &clip, const Skeleton &skeleton, int bone, float frame);
        // Writes (or overwrites) the Loc/Rot/Scale keys of a channel at a time in ticks.
        void SetPoseKey(AnimationClip &clip, int channelIdx, float time, const vec3 &pos, const quat &rot, const vec3 &scl);
        void DeleteKeyframesAtFrame(AnimationClip &clip, int bone, float frame);
        void CollectKeyTimes(const AnimationClip &clip, std::vector<float> &out) const;
        bool NextKeyFrame(const AnimationClip &clip, float from, bool forward, float &out) const;

        // --- modal G / S ---
        void BeginModal(Modal modal, const AnimationClip &clip, float anchorFrame);
        void UpdateModal(AnimationClip &clip, float mouseFrame, float playhead);
        void CommitModal(AnimationClip &clip);
        void CancelModal(AnimationClip &clip);

        // --- drawing ---
        // Returns true when the clip list changed (references into it are stale for this frame).
        bool DrawHeader(Scene &scene, AnimationSystem *anim, ModelAsset *model, AnimationClip &clip,
                        float currentFrame);
        void DrawDopeSheet(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                           float currentFrame);
        void DrawChannelRegion(const Skeleton &skeleton, const AnimationClip &clip, const ImVec2 &origin,
                               const ImVec2 &size);
        void DrawRuler(Scene &scene, AnimationSystem *anim, const AnimationClip &clip, float currentFrame,
                       const ImVec2 &origin, const ImVec2 &size);
        void DrawKeyArea(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                         float currentFrame, const ImVec2 &origin, const ImVec2 &size);
        void DrawGraphEditor(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                             float currentFrame);
        void DrawCurveChannels(const Skeleton &skeleton, const AnimationClip &clip, const ImVec2 &origin,
                               const ImVec2 &size);
        void DrawCurveArea(Scene &scene, AnimationSystem *anim, const AnimationClip &clipConst, AnimationClip &clip,
                           float currentFrame, const ImVec2 &origin, const ImVec2 &size);
        float ValueToPx(float value) const;
        float PxToValue(float px) const;
        float CurveHalf(int channelIdx, KeyType type, int axis) const; // 1 unless Normalize is on
        void FitCurveRange(const AnimationClip &clip);
        static float KeyComponent(const AnimationChannel &chan, KeyType type, int keyIdx, int axis);
        static void SetKeyComponent(AnimationChannel &chan, KeyType type, int keyIdx, int axis, float value);
        void CollectCurves(const AnimationClip &clip);
        void DrawStatusBar(const Skeleton &skeleton, const AnimationClip &clip);
        void DrawPoseBar(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                         float currentFrame);
        void HandleHotkeys(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                           float currentFrame);
        void BuildRows(const Skeleton &skeleton);
        void DrawVScrollbar(const ImVec2 &origin, const ImVec2 &size, float contentHeight);
        void DrawHScrollbar(const ImVec2 &origin, const ImVec2 &size, float durationFrames);
        void ScrollWheel(float visibleHeight, float contentHeight);
        void BuildGlyphs(const Skeleton &skeleton, const AnimationClip &clip, float keyLeft, float rowTop,
                         float visibleTop, float visibleBottom);
        void DrawClipPopups(Scene &scene, AnimationSystem *anim, ModelAsset *model);

        // --- state: target ---
        NodeId *m_targetNode = nullptr;
        std::vector<NodeId *> m_animatedNodes;
        ModelAsset *m_editModel = nullptr;
        ModelAsset *m_lastModel = nullptr;
        int m_selectedClip = 0;
        int m_lastClipCount = -1;
        Mode m_mode = Mode::DopeSheet;

        // --- state: view ---
        float m_frameTicks = 0.f; // ticks per displayed frame (0 = detect from the clip)
        float m_viewStart = -2.f; // frames
        float m_viewEnd = 100.f;
        float m_keyLeft = 0.f; // screen x of the key area
        float m_keyWidth = 1.f;
        float m_scrollY = 0.f;
        float m_channelWidth = 210.f;
        bool m_fitPending = true;
        bool m_snap = true;
        bool m_autoKey = false;
        bool m_loop = true;
        float m_speed = 1.f;
        bool m_scrubbing = false;
        bool m_dirty = false;
        bool m_scrollDragging = false;
        int m_hDrag = 0; // bottom scroller: 1 thumb (pan), 2 left grip, 3 right grip (zoom)
        float m_scrollDragOffset = 0.f;
        float m_contentHeight = 0.f; // rows (or curve channel list) height for the scrollbar

        // --- state: channels ---
        std::vector<Row> m_rows;
        std::vector<char> m_boneExpanded;
        std::vector<char> m_boneSelected;
        int m_activeBone = -1;
        int m_hoveredRow = -1;

        // --- state: keys ---
        std::vector<KeyRef> m_selectedKeys;
        std::unordered_set<uint64_t> m_selectedSet; // PackKey(ref) mirror of m_selectedKeys for O(1) lookups
        std::vector<Glyph> m_glyphs;
        std::vector<KeyRef> m_glyphRefs;
        std::vector<ClipboardEntry> m_clipboard;
        std::vector<UndoEntry> m_undo;
        std::vector<UndoEntry> m_redo;
        bool m_boxSelecting = false;
        ImVec2 m_boxStart = {};
        ImVec2 m_boxEnd = {};
        bool m_pressOnKey = false; // LMB went down on a key: may become a tweak-drag
        ImVec2 m_pressPos = {};

        // --- state: graph editor ---
        std::vector<Curve> m_curves;
        std::vector<CurvePoint> m_curvePoints;
        std::vector<char> m_curveHidden; // per (type*4+axis), shared by all bones
        float m_curveMin = -1.f;
        float m_curveMax = 1.f;
        float m_curveTop = 0.f; // screen y of the curve area
        float m_curveHeight = 1.f;
        bool m_curveFitPending = true;
        bool m_normalize = false;
        int m_modalAxis = -1; // graph editor: component whose value follows the mouse
        KeyType m_modalType = KeyType::Position;
        float m_modalValueDelta = 0.f;
        float m_modalAnchorValue = 0.f;
        std::vector<float> m_modalOrigValues;

        // --- state: modal ---
        Modal m_modal = Modal::None;
        bool m_modalTweak = false; // started by dragging a key: commit on release
        float m_modalAnchor = 0.f;
        float m_modalDelta = 0.f;
        float m_modalFactor = 1.f;
        std::vector<float> m_modalOrigTimes;
        AnimationClip m_modalSnapshot;

        // --- state: pending programmatic requests ---
        float m_pendingFrame = -1.f;
        std::vector<PendingPose> m_pendingPoses;
        PendingClip m_pendingClip;
        bool m_pendingClipSet = false;

        // --- state: pose bar ---
        vec3 m_poseEuler = {}; // degrees, held while a pose field is being dragged
        bool m_poseEditing = false;
        std::string m_pendingBone;
        bool m_pendingSave = false;
        int m_pendingPlay = -1; // 0 pause, 1 play, 2 play reverse

        // --- state: popups ---
        char m_nameBuf[128] = {};
        int m_popupClip = -1;

        static constexpr float kRowHeight = 20.f;
        static constexpr float kRulerHeight = 24.f;
        static constexpr float kHeaderHeight = 30.f;
        static constexpr float kStatusHeight = 22.f;
        static constexpr float kKeyHitRadius = 6.f;
        static constexpr float kAxisWidth = 52.f;
        static constexpr float kRightMargin = 16.f; // keeps the last frame's keys and ruler label inside the view
        static constexpr float kScrollbarWidth = 8.f;
        static constexpr float kHScrollHeight = 14.f;
        static constexpr float kMinViewFrames = 4.f;
        static constexpr int kMaxUndo = 64;
    };
} // namespace pe
