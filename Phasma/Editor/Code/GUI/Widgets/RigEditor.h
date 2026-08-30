#pragma once
#include "GUI/Widget.h"
#include "Animation/AnimationTypes.h"
#include "Animation/AnimationReferenceFrames.h"
#include "Animation/RigPresetLibrary.h"
#include "Animation/RigWeightStroke.h"
#include "imgui/imgui.h"

namespace pe
{
    struct NodeId;
    class AnimationTimeline;
    class Scene;
    class Camera;
    class Image;
    class ModelAsset;

    // One authored bone: head/tail in rig space (the model root node's local space) plus a capsule
    // influence shape (radius at each end). rigid = vertices inside belong to this bone only.
    struct RigBone
    {
        std::string name;
        int parent = -1;
        vec3 head = vec3(0.f);
        vec3 tail = vec3(0.f, 0.1f, 0.f);
        float headRadius = 0.05f;
        float tailRadius = 0.05f;
        bool rigid = false;
        bool spline = false; // link of a spline chain: vertices blend 4 chain joints with Catmull-Rom weights
        std::string shell;   // node name of the part this bone owns outright (empty = shapes only)
    };

    // Pins a bone's tail to a point: on another bone (a hand on the shovel) or fixed in rig space (a planted
    // foot). Solved with two-bone IK over the bone and its parent after every pose edit; reach caps how far
    // the pair may extend (1 = fully straight).
    struct RigLock
    {
        std::string bone;
        std::string target;      // empty = anchor is a fixed rig-space point
        vec3 anchor = vec3(0.f); // rig-space rest point on the target (or the fixed point)
        float reach = 1.f;       // ponytail: the only limit is reach (0.3..1); per-joint angle cones are the upgrade
        bool enabled = true;
    };

    // Rig Editor: authors and bakes a skeleton + per-bone influence capsules, poses baked rigs, and
    // adjusts ordinary joint blends directly on a posed mesh. The rig document lives beside the
    // .pemesh; baked weight edits stay in memory until explicitly saved to that .pemesh.
    class RigEditor : public Widget
    {
    public:
        RigEditor() : Widget("Rig Editor") { m_open = false; }
        ~RigEditor() override;
        void Update() override;

        // rig.* editor actions (agent route). Returns a JSON result string.
        std::string HandleAction(const std::string &action, const std::string &argsJson);
        std::string ProjectPresetsJson();

        // Viewport overlay + drag handles; called by SceneView inside the viewport clip rect.
        void DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                          bool &active);
        NodeId *GetRootNode() const { return m_rootNode; }
        bool HasTarget() const { return m_model != nullptr; }

    private:
        enum class Mode
        {
            Edit,
            Pose
        };

        struct ShellInfo
        {
            int nodeIndex = -1;
            int meshIndex = -1;
            std::string name;
            vec3 centroid = vec3(0.f);
            vec3 axis = vec3(0.f, 1.f, 0.f);
            float halfLength = 0.f;
            float radius = 0.f;
            vec3 aabbMin = vec3(0.f), aabbMax = vec3(0.f);
            float volume = 0.f;
            std::vector<vec3> points; // rig-space vertices (auto preset re-measures blobs)
        };

        struct Snapshot
        {
            std::vector<RigBone> bones;
            int selected = -1;
            std::string preset;
            std::vector<RigLock> locks;
            std::vector<std::string> pins;
        };

        // One solved lock: the two-bone chain (root = the bone's parent, mid = the bone) and its rotation edits.
        struct LockSolve
        {
            int lock = -1;
            int root = -1, mid = -1;
            quat rootRotation = quat(1.f, 0.f, 0.f, 0.f);
            quat midRotation = quat(1.f, 0.f, 0.f, 0.f);
            bool clamped = false;
        };

        void ResolveTarget(Scene &scene);
        void ClearDocument();
        void PushUndo(bool keepPreset = false); // call BEFORE mutating the document
        void Restore(const Snapshot &snapshot);
        void Undo();
        void Redo();
        void ImportSkeleton();
        void PresetAuto();
        void ReloadProjectPresets();
        const RigPreset *FindPreset(std::string_view idOrName) const;
        bool ApplyPreset(const RigPreset &preset, std::string &error);
        void CollectShells(std::vector<ShellInfo> &out) const;
        mat4 ModelNodeWorld(int nodeIndex) const;
        int AddBone(const std::string &name, int parent);
        int MakeChain(int index, int count); // subdivide a bone into a spline chain; returns the last link
        void ChainOf(int bone, std::vector<int> &chain) const;
        void ChainWeights(int bone, const vec3 &p, int joints[4], float weights[4]) const;
        void RemoveBone(int index);
        bool SetParent(int index, int parent);
        bool IsAncestor(int ancestor, int index) const;
        int FindBone(const std::string &name) const;
        std::string UniqueName(std::string base) const;
        void MoveTail(int index, const vec3 &tail); // drags connected child heads along
        float ModelHeight() const;

        std::filesystem::path RigJsonPath() const;
        bool SaveJson(std::string *error = nullptr, bool quiet = false);
        bool LoadJson(std::string *error = nullptr);
        std::string DocumentJson() const;

        void DrawToolbar();
        void BuildCaches();
        std::string ShellsJson() const; // JSON text: the header stays free of nlohmann
        static float CapsuleInfluence(const RigBone &b, const vec3 &p, float &signedDistance);
        int ShellOwner(const std::string &nodeName) const;
        bool Bake(Scene &scene, std::string &error, std::string &outPath);
        void ComputeVertexWeights(const vec3 &p, int owner, int joints[4], float weights[4]) const;
        static ImU32 BoneColor(int index);
        void UpdateHeatMap(Scene &scene);
        void RestoreHeatMap(Scene &scene);
        static std::string MirrorName(const std::string &name);
        int AddBonePair(const std::string &name, int parent);
        int MirrorCounterpart(int index) const;
        void SyncMirror(int index);
        bool RayModel(const vec3 &origin, const vec3 &dir, float &tEnter, float &tExit) const;
        bool SnapTarget(const vec3 &target, int bone, int handle, const mat4 &invRootWorld, const vec3 &rayOrigin,
                        const vec3 &rayDir, const std::function<bool(const vec3 &, ImVec2 &)> &project, const ImVec2 &mouse,
                        vec3 &out) const;
        void DrawBoneTree(int parent, int depth);
        void DrawPoseBoneTree(const Skeleton &skeleton, int parent, int depth);
        void DrawBoneProperties();
        void DrawRigTransform();
        void TransformRig(std::span<const RigBone> base, const vec3 &move, const vec3 &rotateDegrees, const vec3 &scale,
                          int pivotMode); // 0 feet, 1 centre, 2 origin
        void DrawPosePanel(Scene &scene);
        void DrawLocksPanel(Scene &scene, AnimationTimeline *timeline);
        // Drop any in-flight grab / IK / reach / gizmo interaction without keying anything.
        void AbortPoseEdits();
        // Posed head / tail of every skeleton bone (tails from the authored rig, else from the children).
        void PoseTails(const Skeleton &skeleton, std::span<const mat4> boneTransforms, std::vector<vec3> &heads,
                       std::vector<vec3> &tails, std::vector<vec3> *restTails = nullptr) const;
        // Lock geometry at a pose; false when the lock names unknown bones or would chase its own chain.
        bool LockChain(const RigLock &lock, const Skeleton &skeleton, int &root, int &mid, int &target,
                       std::string *why = nullptr) const;
        bool LockAnchorPosed(const RigLock &lock, const Skeleton &skeleton, std::span<const mat4> boneTransforms,
                             vec3 &out) const;
        int AddLock(const std::string &bone, const std::string &target, float reach, const vec3 *anchor,
                    std::string &error);
        // Solves every enabled lock at boneTransforms; skipBone = a bone being dragged (its own locks wait).
        void SolveLockRotations(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int skipBone,
                                std::vector<LockSolve> &out);
        // Solves at the playhead (frame < 0) or at an explicit frame; pushUndo = own undo step, else the caller's.
        bool SolveLocks(Scene &scene, int skipBone = -1, bool pushUndo = false, float frame = -1.f);
        bool BakeLocks(Scene &scene, std::string &status);
        // Grab: pull a bone's tail to target; the chain up to the first pinned bone (or the skeleton root) bends,
        // distal bones first (CCD with stiffer parents), so a hand pull moves the arm, then the shoulder, then the torso.
        bool IsPinned(const std::string &bone) const;
        void TogglePin(const std::string &bone);
        void GrabSolve(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone, const vec3 &target,
                       std::vector<std::pair<int, quat>> &out) const;
        // keyedOut reports whether keys actually landed (false for an at-target no-op).
        bool GrabTo(Scene &scene, int bone, const vec3 &target, float *gap = nullptr, bool pushUndo = false,
                    bool *keyedOut = nullptr);
        static void DrawPadlock(ImDrawList *drawList, const ImVec2 &centre, bool closed, ImU32 colour);
        void DrawPoseViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                              bool &hovered, bool &active);
        void DrawJointBlendViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                    const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                                    const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered,
                                    bool &active);
        void DrawIkViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                            const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                            const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered, bool &active);
        void DrawReferenceOverlay(AnimationTimeline *timeline, const ImVec2 &imageMin, const ImVec2 &imageSize);
        bool BuildPosedVertices(std::span<const mat4> boneTransforms, std::vector<vec3> &out) const;
        void ReadWeights(std::vector<RigWeightStroke::SkinWeight> &out) const;
        bool WriteWeights(Scene &scene, std::span<const RigWeightStroke::SkinWeight> weights);
        void UploadWeights(Scene &scene);
        void PushWeightUndo();
        bool WeightUndo(Scene &scene, bool redo);
        bool SaveWeights(std::string &error);
        bool LoadReference(const std::filesystem::path &path, std::string &error);
        void ClearReference();
        bool UpdateReferenceImage(AnimationTimeline *timeline, std::string &error);
        void ReleaseReferenceImage(bool drainRendererFrames = false);
        std::vector<std::string> SelectedSpringChain() const;
        void SetMode(Mode mode);

        ModelAsset *m_model = nullptr;
        NodeId *m_rootNode = nullptr;
        std::vector<RigBone> m_bones;
        int m_selected = -1;
        int m_poseSelected = -1;
        Mode m_mode = Mode::Edit;
        bool m_dirty = false;
        bool m_showShapes = true;
        int m_dragBone = -1; // viewport drag in flight: bone + handle
        int m_dragHandle = -1;
        vec3 m_dragPlanePoint = vec3(0.f);
        vec3 m_dragPrevHit = vec3(0.f); // drags are incremental so precision / axis lock compose
        bool m_dragHasPrev = false;
        vec3 m_dragFree = vec3(0.f); // unsnapped joint position under the cursor during a drag
        bool m_snap = false;
        int m_snapMode = 2; // 0 joints, 1 surface, 2 volume, 3 increment
        bool m_mirrorX = false;
        bool m_poseDragging = false;
        int m_poseGizmo = 0;     // 0 rotate, 1 move, 2 both
        float m_treeWidth = 0.f; // Bones pane width (px); 0 = 40% of the window on first draw
        std::vector<RigLock> m_locks;
        std::vector<std::string> m_pins; // bones a grab pull never bends past
        int m_grabBone = -1;             // viewport grab in flight
        bool m_grabPushed = false;       // this drag's Timeline undo landed with its first key
        vec3 m_grabPlanePoint = vec3(0.f), m_grabOffset = vec3(0.f);
        std::vector<vec3> m_lockBend; // last elbow bend direction per lock: pole continuity through straight arms
        int m_lockTarget = -1;        // Add Lock target combo: -1 = fixed rig-space point
        float m_lockReach = 1.f;
        bool m_reachDragging = false;  // Reach slider: one undo pair per drag, pushed on the first real change
        bool m_reachPushed = false;    // this reach drag's Timeline undo landed with its first solve
        uint32_t m_poseEditSerial = 0; // Timeline pose edits already solved
        bool m_onionBones = false;
        int m_onionPrevious = 2;
        int m_onionNext = 2;
        bool m_motionTrail = false;
        int m_trailPrevious = 8;
        int m_trailNext = 8;
        bool m_twoBoneIk = false;
        int m_ikBone = -1;
        vec3 m_ikTarget = vec3(0.f);
        vec3 m_ikPole = vec3(0.f);
        bool m_ikDragging = false;
        bool m_ikDirty = false;
        bool m_jointBlend = false;
        RigWeightStroke::Mode m_weightMode = RigWeightStroke::Mode::Add;
        float m_weightRadius = 0.1f;
        float m_weightStrength = 0.5f;
        bool m_weightDragging = false;
        bool m_weightStrokeChanged = false;
        bool m_weightDirty = false;
        bool m_weightHasLastCenter = false;
        vec3 m_weightLastCenter = vec3(0.f);
        std::vector<RigWeightStroke::SkinWeight> m_weightScratch;
        std::vector<std::vector<RigWeightStroke::SkinWeight>> m_weightUndo;
        std::vector<std::vector<RigWeightStroke::SkinWeight>> m_weightRedo;
        std::filesystem::path m_referencePath;
        AnimationReferenceFrames::Sequence m_referenceSequence;
        int m_referenceFrameIndex = -1;
        Image *m_referenceImage = nullptr;
        void *m_referenceTexture = nullptr;
        std::array<char, 512> m_referencePathBuffer{};
        int m_chainCount = 6;
        int m_syncedSelected = -2; // last bone selection pushed to the Animation Timeline
        int m_heat = 0;            // 0 off, 1 selected bone, 2 all bones
        bool m_heatDirty = false;
        int m_heatSelected = -1;
        std::vector<std::pair<uint32_t, vec4>> m_heatBackup; // scene vertex index -> original colour
        std::vector<vec3> m_rigVerts;                        // model vertices in rig space (snapping)
        std::vector<uint32_t> m_rigTris;
        std::vector<int> m_rigTriMesh; // model mesh index per triangle (Volume snap pairs hits per shell)
        std::vector<ShellInfo> m_shellCache;
        std::vector<RigPreset> m_projectPresets;
        std::vector<std::string> m_projectPresetErrors;
        std::string m_presetName; // preset the current bones came from; empty = hand-edited (Custom)
        vec3 m_xformMove = vec3(0.f), m_xformRotate = vec3(0.f), m_xformScale = vec3(1.f);
        int m_xformPivot = 0;
        std::vector<RigBone> m_xformBase; // bones at the start of the transform drag
        std::string m_status;
        std::string m_bakeNote; // Bake: dropped-clips notice appended to the callers' status
        char m_nameBuf[96] = {};
        std::string m_docPath; // model file the undo/redo stacks belong to
        std::vector<Snapshot> m_undo;
        std::vector<Snapshot> m_redo;
        static constexpr int kMaxUndo = 64;
        static constexpr int kMaxWeightUndo = 16;
    };
} // namespace pe
