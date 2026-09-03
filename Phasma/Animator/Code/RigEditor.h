#pragma once
#include "Animation/AnimationTypes.h"
#include "Animation/GeodesicBind.h"
#include "Animation/RigPresetLibrary.h"
#include "Animation/RigWeightStroke.h"
#include "imgui/imgui.h"

namespace pe
{
    struct NodeId;
    class AnimationPoseViewport;
    class AnimationTimeline;
    class AnimatorApp;
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
        bool spline = false;           // link of a spline chain: vertices blend 4 chain joints with Catmull-Rom weights
        std::string shell;             // node name of the part this bone owns outright (empty = shapes only)
        float swingLimitDegrees = 0.f; // zero = unrestricted; relative to the bind/rest bone frame (+Y axis)
        float twistLimitDegrees = 0.f;
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
        bool automatic = false; // captured by posing a bone by hand: session state, never written to rig.json
        bool lifted = false;    // an authored plant whose foot was lifted: yields to the session, never written or solved
        bool heel = false;      // a session plant holding the heel (under the ankle at floor height) instead of the tail
    };

    // Rig Editor: authors and bakes a skeleton + per-bone influence capsules, and adjusts ordinary
    // joint blends directly on a posed mesh. The rig document lives beside the
    // .pemesh; baked weight edits stay in memory until explicitly saved to that .pemesh.
    class RigEditor
    {
    public:
        RigEditor() = default;
        void Init(AnimatorApp *app, AnimationTimeline *timeline)
        {
            m_app = app;
            m_timeline = timeline;
        }
        void ResolveTarget(Scene &scene);
        void DrawPanel(Scene &scene);

        // rig.* editor actions (agent route). Returns a JSON result string.
        std::string HandleAction(const std::string &action, const std::string &argsJson);
        std::string ProjectPresetsJson();

        // Viewport overlay + drag handles; the Timeline calls it inside its viewport clip rect.
        void DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                          bool &active);
        NodeId *GetRootNode() const { return m_rootNode; }
        bool HasTarget() const { return m_model != nullptr; }

    private:
        friend class AnimationPoseViewport;
        AnimatorApp *m_app = nullptr;
        AnimationTimeline *m_timeline = nullptr;

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
            float volume = 0.f;       // bounds volume (auto preset ranks shells by it)
            float meshVolume = 0.f;   // enclosed volume of the part's triangles in rig space (its mass for balance)
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
        void ComputeVertexWeights(const vec3 &p, int owner, int joints[4], float weights[4],
                                  const GeodesicBind::VertexWeights *geodesic = nullptr) const;
        void BuildGeodesic();
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
                        vec3 &out, std::span<const vec3> dispHeads = {}, std::span<const vec3> dispTails = {}) const;
        void DrawBoneTree(int parent, int depth);
        void DrawBoneProperties();
        void DrawJointBlendPanel(Scene &scene);
        void DrawRigTransform();
        void TransformRig(std::span<const RigBone> base, const vec3 &move, const vec3 &rotateDegrees, const vec3 &scale,
                          const vec3 &pivot);
        void DrawJointBlendViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                    const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                                    const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered,
                                    bool &active);
        bool BuildPosedVertices(std::span<const mat4> boneTransforms, std::vector<vec3> &out) const;
        void ReadWeights(std::vector<RigWeightStroke::SkinWeight> &out) const;
        bool WriteWeights(Scene &scene, std::span<const RigWeightStroke::SkinWeight> weights);
        void UploadWeights(Scene &scene);
        void PushWeightUndo();
        bool WeightUndo(Scene &scene, bool redo);
        bool SaveWeights(std::string &error);
        int SelectedSkeletonBone() const;

        ModelAsset *m_model = nullptr;
        NodeId *m_rootNode = nullptr;
        std::vector<RigBone> m_bones;
        int m_selected = -1;
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
        float m_treeWidth = 0.f; // Bones pane width (px); 0 = 40% of the window on first draw
        std::vector<RigLock> m_locks;
        std::vector<std::string> m_pins; // bones a grab pull never bends past
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
        bool m_geodesic = true;     // document: unowned parts bind along the surface (rig.json "geodesic")
        GeodesicBind::Result m_geo; // rebuilt at bake and heat refresh only
        std::string m_geoNote;      // bones whose capsule holds no surface
        std::vector<RigPreset> m_projectPresets;
        std::vector<std::string> m_projectPresetErrors;
        std::string m_presetName; // preset the current bones came from; empty = hand-edited (Custom)
        vec3 m_xformMove = vec3(0.f), m_xformRotate = vec3(0.f), m_xformScale = vec3(1.f);
        vec3 m_xformPivot = vec3(0.f);
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
