#pragma once
#include "GUI/Widget.h"
#include "Animation/AnimationTypes.h"
#include "imgui/imgui.h"

namespace pe
{
    struct NodeId;
    class Scene;
    class Camera;
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
        std::string shell; // node name of the part this bone owns outright (empty = shapes only)
    };

    // Rig Editor: authors a skeleton + per-bone influence capsules over the selected model (load
    // existing / presets / manual), drawn and manipulated in the viewport. The document lives in the
    // widget and in <model>.rig.json beside the .pemesh; baking weights into the model is the next drop.
    class RigEditor : public Widget
    {
    public:
        RigEditor() : Widget("Rig Editor") { m_open = false; }
        void Update() override;

        // rig.* editor actions (agent route). Returns a JSON result string.
        std::string HandleAction(const std::string &action, const std::string &argsJson);

        // Viewport overlay + drag handles; called by SceneView inside the viewport clip rect.
        void DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                          bool &active);
        NodeId *GetRootNode() const { return m_rootNode; }
        bool HasTarget() const { return m_model != nullptr; }

    private:
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
        };

        void ResolveTarget(Scene &scene);
        void ClearDocument();
        void PushUndo(); // call BEFORE mutating the document
        void Restore(const Snapshot &snapshot);
        void Undo();
        void Redo();
        void ImportSkeleton();
        void PresetAuto();
        void PresetHumanoid();
        void CollectShells(std::vector<ShellInfo> &out) const;
        mat4 ModelNodeWorld(int nodeIndex) const;
        int AddBone(const std::string &name, int parent);
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
        void DrawBoneProperties();

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
        int m_heat = 0; // 0 off, 1 selected bone, 2 all bones
        bool m_heatDirty = false;
        int m_heatSelected = -1;
        std::vector<std::pair<uint32_t, vec4>> m_heatBackup; // scene vertex index -> original colour
        std::vector<vec3> m_rigVerts;                        // model vertices in rig space (snapping)
        std::vector<uint32_t> m_rigTris;
        std::vector<int> m_rigTriMesh; // model mesh index per triangle (Volume snap pairs hits per shell)
        std::vector<ShellInfo> m_shellCache;
        std::string m_status;
        char m_nameBuf[96] = {};
        std::string m_docPath; // model file the undo/redo stacks belong to
        std::vector<Snapshot> m_undo;
        std::vector<Snapshot> m_redo;
        static constexpr int kMaxUndo = 64;
    };
} // namespace pe
