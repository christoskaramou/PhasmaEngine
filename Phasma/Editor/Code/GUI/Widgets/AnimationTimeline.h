#pragma once
#include "GUI/Widget.h"
#include "Animation/AnimationClipTools.h"
#include "Animation/AnimationTypes.h"
#include "imgui/imgui.h"

#include <array>
#include <memory>
#include <span>

namespace pe
{
    struct NodeId;
    class Scene;
    class Camera;
    class ModelAsset;
    class AnimationSystem;
    class AnimationPoseViewport;
    class RigEditor;

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
        AnimationTimeline();
        ~AnimationTimeline() override;
        void Init(GUI *gui) override;
        void Update() override;
        void SetRigMode(bool rig) { m_rigMode = rig; }
        bool RigMode() const { return m_rigMode; }
        RigEditor &Rig() { return *m_rigEditor; }

        // Programmatic route for editor actions (timeline.*): applied on the next Update.
        void SetGraphMode(bool graph);
        void RequestFrame(float frame) { m_pendingFrame = frame; }
        void RequestBone(const std::string &name) { m_pendingBone = name; }
        void RequestSave() { m_pendingSave = true; }
        void RequestPlay(bool play, bool reverse) { m_pendingPlay = play ? (reverse ? 2 : 1) : 0; }
        void RequestRestPose() { m_pendingRest = true; }
        // Agent actions that read the evaluated pose (timeline.grab, timeline.lock solve) must not run against a
        // playhead or clip that queued requests are about to move.
        bool HasPendingRequests() const
        {
            return m_pendingFrame >= 0.f || !m_pendingPoses.empty() || m_pendingClipSet || !m_pendingBone.empty() ||
                   m_pendingPlay >= 0 || m_pendingSave || m_pendingRest;
        }

        // Viewport posing. Bone transforms are in skeleton rig space (rootTransform removed).
        // The Timeline owns the pose tool, key insertion, drag undo snapshot and immediate evaluation.
        struct ViewportPose
        {
            NodeId *node = nullptr;
            std::vector<mat4> boneTransforms;
        };
        bool GetViewportPose(ModelAsset *model, ViewportPose &out) const;
        // Current active-clip playhead in seconds, clamped exactly like viewport pose sampling.
        bool GetViewportTimeSeconds(ModelAsset *model, double &out) const;
        // Samples the active clip at the current playhead plus a displayed-frame offset, clamped to the clip range.
        bool SampleViewportPose(ModelAsset *model, float frameOffset, ViewportPose &out) const;
        // Samples the active clip at an absolute displayed frame, clamped to the clip range.
        bool SampleViewportPoseAtFrame(ModelAsset *model, float frame, ViewportPose &out) const;
        bool GetClipEndFrame(ModelAsset *model, float &endFrame) const;
        struct GlobalBoneRotation
        {
            int bone = -1;
            quat rotation = quat(1.f, 0.f, 0.f, 0.f); // Skeleton rig-space global rotation.
        };
        // Keys every supplied rig-space global rotation at the current frame (frame < 0) or at frame. pushUndo = false
        // when the caller's edit already took the undo snapshot (a drag, PushViewportUndo, a Timeline pose edit).
        // userPose = false for batch writes such as Bake Locks: interval mode must not rebuild the in-betweens
        // around every frame a bake touches.
        bool KeyViewportGlobalRotations(Scene &scene, ModelAsset *model, std::span<const GlobalBoneRotation> rotations,
                                        float frame = -1.f, bool pushUndo = true, bool userPose = true);
        bool PushViewportUndo(ModelAsset *model);
        // Bumps on pose-bar / timeline.pose edits only (never on undo/redo or viewport keying).
        uint32_t PoseEditSerial() const { return m_poseEditSerial; }
        // Frame the last pose edit targeted (< 0 = the playhead): lock re-solves key there, not at the playhead.
        float PoseEditFrame() const { return m_poseEditFrame; }
        bool CanViewportRotate(ModelAsset *model, int bone, bool rotate = true, bool translate = false) const;
        bool BeginViewportRotate(Scene &scene, ModelAsset *model, int bone, bool rotate = true,
                                 bool translate = false);
        // translate also keys the bone's local position from boneTransform (Auto Key, or an existing position key).
        bool UpdateViewportRotate(Scene &scene, ModelAsset *model, int bone, const mat4 &boneTransform,
                                  int mirrorBone = -1, bool rotate = true, bool translate = false);
        void EndViewportRotate();
        bool StepViewportUndo(Scene &scene, bool redo);
        // Forget the resolved model, target nodes, undo stacks and queued requests: the Rig Editor
        // calls this when Bake frees the model the Timeline had resolved.
        void DropTarget();
        bool AutoKey() const { return m_autoKey; }
        void SetAutoKey(bool enabled) { m_autoKey = enabled; }
        std::string HandleAction(const std::string &action, const std::string &argsJson);
        void DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                          bool &hovered, bool &active);
        bool DrawPoseViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                              bool &hovered, bool &active);

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
        friend class AnimationPoseViewport;
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
            // The interval the keys were edited with, so undoing a slide or scale puts the band back too.
            float intervalStart = 0.f;
            float intervalEnd = -1.f;
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
        NodeId *AnimationRootOf(Scene &scene, AnimationSystem *anim, NodeId *selected);
        void EnsureStates(Scene &scene, AnimationSystem *anim, float keepFrame);
        void SetFrame(Scene &scene, AnimationSystem *anim, float frame);
        void ReevaluatePose(Scene &scene, AnimationSystem *anim);
        void SetPlaying(Scene &scene, AnimationSystem *anim, bool play, bool reverse);
        bool IsPlaying(AnimationSystem *anim) const;

        // --- clip edits (all go through PushUndo) ---
        void PushUndo(AnimationClip &clip);
        void PushUndoSnapshot(const AnimationClip &snapshot);
        // Interval slide / scale push the band they started from, not the one the drag left behind.
        void PushUndoSnapshot(const AnimationClip &snapshot, float intervalStart, float intervalEnd);
        void Undo(AnimationClip &clip);
        void Redo(AnimationClip &clip);
        void ResetEditState();
        void DropPendingRequests();
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
        // --- interval: a span of frames the tools treat as one object (Cascadeur-style) ---
        bool HasInterval() const { return m_intervalEnd >= m_intervalStart + 1.f; }
        void ClearInterval();
        std::vector<int> IntervalBones() const; // selected bones, or empty = every keyed bone
        std::vector<int> SpringChain(const Skeleton &skeleton) const;
        int BallisticBone(const Skeleton &skeleton, const AnimationClip &clip, int requested) const;
        size_t BakeBallistic(const Skeleton &skeleton, AnimationClip &clip, int bone, float gravity, bool body);
        size_t TweenBones(AnimationClip &clip, std::span<const int> bones, float startFrame, float endFrame, int everyN,
                          AnimationClipTools::TweenMode mode);
        // Moves every key inside [fromFrame, toFrame]: t' = pivot + (t - pivot) * factor + delta, in frames,
        // clamped to the clip. Returns the keys touched.
        size_t RemapKeyTimes(AnimationClip &clip, float fromFrame, float toFrame, float pivot, float factor, float delta);
        // Interval mode: a pose keyed strictly inside the interval becomes the new extreme and both
        // halves are rebuilt from it. No-op without an interval or on its endpoints.
        size_t RetweenAroundFrame(AnimationClip &clip, std::span<const int> bones, float frame);
        // Writes (or overwrites) the Loc/Rot/Scale keys of a channel at a time in ticks.
        void SetPoseKey(AnimationClip &clip, int channelIdx, float time, const vec3 &pos, const quat &rot, const vec3 &scl);
        void RestPoseAll(Scene &scene, AnimationSystem *anim);
        void EnforcePlaybackOwnership(Scene &scene, AnimationSystem *anim, bool ownsTarget);
        void SetRotationKey(AnimationClip &clip, int channelIdx, float time, const quat &rot);
        void SetPositionKey(AnimationClip &clip, int channelIdx, float time, const vec3 &pos);
        bool SampleViewportPoseTicks(ModelAsset *model, float ticks, ViewportPose &out) const;
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
        void DrawPanelMode();
        bool DrawHeader(Scene &scene, AnimationSystem *anim, ModelAsset *model, AnimationClip &clip,
                        float currentFrame, bool showEditorMode);
        void DrawMotionDoctor();
        void DrawDopeSheet(Scene &scene, AnimationSystem *anim, const Skeleton &skeleton, AnimationClip &clip,
                           float currentFrame);
        void DrawChannelRegion(const Skeleton &skeleton, const AnimationClip &clip, const ImVec2 &origin,
                               const ImVec2 &size);
        void DrawRuler(Scene &scene, AnimationSystem *anim, AnimationClip &clip, float currentFrame,
                       const ImVec2 &origin, const ImVec2 &size);
        // Interval band + its ruler gestures. Returns true while a band drag owns the mouse (no scrub).
        bool DrawInterval(Scene &scene, AnimationSystem *anim, AnimationClip &clip, float currentFrame,
                          const ImVec2 &origin, const ImVec2 &size, bool hovered);
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
        AnimationPoseViewport *PoseViewport(RigEditor &rig);

        // --- state: target ---
        NodeId *m_targetNode = nullptr;
        std::vector<NodeId *> m_animatedNodes;
        ModelAsset *m_editModel = nullptr;
        ModelAsset *m_lastModel = nullptr;
        uint32_t m_ownershipSceneGeneration = ~uint32_t{0};
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
        bool m_autoKey = true;
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

        // --- state: interval ---
        float m_intervalStart = 0.f;
        float m_intervalEnd = -1.f; // < start + 1 frame = no interval
        int m_intervalDrag = 0;     // 1 slide body, 2 left edge, 3 right edge, 4 alt-drag create
        bool m_intervalScale = false;
        float m_intervalGrab = 0.f; // frame under the cursor when the drag began
        float m_intervalDragStart = 0.f;
        float m_intervalDragEnd = 0.f;
        AnimationClip m_intervalSnapshot; // pre-drag key times: slide and scale rebuild from it
        std::string m_tweenStatus;        // what the last Tween or Ballistic bake wrote, beside the button
        float m_gravity = 9.81f;          // rig units per second squared for the ballistic bake
        bool m_ballisticBody = false;     // throw the centre of mass (root corrected per frame) instead of the root

        // --- state: pending programmatic requests ---
        float m_pendingFrame = -1.f;
        std::vector<PendingPose> m_pendingPoses;
        PendingClip m_pendingClip;
        bool m_pendingClipSet = false;

        // --- state: pose bar ---
        vec3 m_poseEuler = {}; // degrees, held while a pose field is being dragged
        bool m_poseEditing = false;
        bool m_viewportRotateActive = false;
        int m_viewportRotateBone = -1;
        float m_viewportRotateTime = 0.f;
        uint32_t m_poseEditSerial = 0;
        float m_poseEditFrame = -1.f;
        std::string m_pendingBone;
        bool m_pendingSave = false;
        int m_pendingPlay = -1; // 0 pause, 1 play, 2 play reverse
        bool m_pendingRest = false;
        bool m_restDisplayed = false; // Rest Pose display active: the playhead viewport pose reads emit the bind pose
        bool m_tabSuspended = false;
        bool m_tabResumePlaying = false;
        bool m_tabResumeRestPose = false;

        // --- state: popups ---
        char m_nameBuf[128] = {};
        int m_popupClip = -1;
        std::array<size_t, 6> m_motionIssueCounts = {};
        std::string m_motionStatus;
        char m_springChainBuf[256] = {};
        int m_motionOffsetFrames = 1;
        float m_breakdownBias = 0.5f;
        std::unique_ptr<AnimationPoseViewport> m_poseViewport;
        std::unique_ptr<RigEditor> m_rigEditor;
        bool m_rigMode = false;
        bool m_visible = false;

        // Layout derived from the current font each frame (Update), so a scaled editor font never clips rows or labels.
        float m_rowHeight = 20.f;
        float m_rulerHeight = 24.f;
        float m_headerHeight = 30.f;
        float m_statusHeight = 22.f;
        float m_axisWidth = 52.f;
        float m_hScrollHeight = 14.f;
        bool m_scrollToActive = false; // scroll the channel list to the active bone on the next draw
        static constexpr float kKeyHitRadius = 6.f;
        static constexpr float kRightMargin = 16.f; // keeps the last frame's keys and ruler label inside the view
        static constexpr float kScrollbarWidth = 8.f;
        static constexpr float kMinViewFrames = 4.f;
        static constexpr float kMinBodyRows = 8.f; // rows the dope sheet / graph keeps before the window scrolls
        static constexpr int kMaxUndo = 64;
    };
} // namespace pe
