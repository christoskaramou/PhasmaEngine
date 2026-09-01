#include "RigEditor.h"
#include "AnimationTimeline.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include <nlohmann/json.hpp>

namespace pe
{
    namespace
    {
        constexpr ImU32 kBoneCol = IM_COL32(120, 200, 255, 230);
        constexpr ImU32 kBoneSelCol = IM_COL32(255, 200, 80, 255);
        constexpr ImU32 kRigidCol = IM_COL32(255, 140, 120, 230);
        constexpr ImU32 kSplineCol = IM_COL32(120, 255, 190, 230);
        constexpr ImU32 kShapeCol = IM_COL32(120, 200, 255, 80);
        constexpr ImU32 kShapeSelCol = IM_COL32(255, 200, 80, 140);
        constexpr ImU32 kHandleCol = IM_COL32(255, 255, 255, 235);
        constexpr ImU32 kHandleHotCol = IM_COL32(255, 226, 102, 255);
        constexpr ImU32 kLockCol = IM_COL32(255, 120, 255, 255);
        constexpr float kHandleSize = 12.f;
        constexpr float kMinRadius = 0.005f;
        constexpr int kRigJsonVersion = 1;

        // Farmer hand-rig names: the 14-joint contract MoCapAnything's Farmer preprocessing expects.
        vec3 ToVec3(const nlohmann::json &j, const vec3 &fallback)
        {
            if (!j.is_array() || j.size() != 3)
                return fallback;
            return vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
        }

        // Authored rig vectors must be exact: a short or non-finite array is an error, never a
        // silent rig-space origin.
        bool StrictVec3(const nlohmann::json &j, vec3 &out)
        {
            if (!j.is_array() || j.size() != 3 ||
                !std::all_of(j.begin(), j.end(), [](const nlohmann::json &c)
                             { return c.is_number(); }))
                return false;
            const vec3 v(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                return false;
            out = v;
            return true;
        }

        std::vector<mat4> RestBoneTransforms(const Skeleton &skeleton)
        {
            std::vector<mat4> transforms(skeleton.GetBoneCount());
            const mat4 inverseRoot = glm::inverse(skeleton.rootTransform);
            for (int i = 0; i < skeleton.GetBoneCount(); i++)
                transforms[i] = inverseRoot * glm::inverse(skeleton.bones[i].offsetMatrix);
            return transforms;
        }

        vec3 SkinRigPoint(const mat4 &posed, const mat4 &bind, const vec3 &p)
        {
            return vec3(posed * glm::inverse(bind) * vec4(p, 1.f));
        }

        vec3 UnskinRigPoint(const mat4 &posed, const mat4 &bind, const vec3 &p)
        {
            return vec3(bind * glm::inverse(posed) * vec4(p, 1.f));
        }

        nlohmann::ordered_json FromVec3(const vec3 &v)
        {
            return nlohmann::ordered_json::array({v.x, v.y, v.z});
        }

        // Dominant covariance eigenvector by power iteration: good enough for "which way is this shell long".
        vec3 PrincipalAxis(const std::vector<vec3> &points, const vec3 &centroid)
        {
            mat3 cov(0.f);
            for (const vec3 &p : points)
            {
                const vec3 d = p - centroid;
                cov += glm::outerProduct(d, d);
            }
            vec3 axis = glm::normalize(vec3(0.3f, 1.f, 0.2f));
            for (int i = 0; i < 24; i++)
            {
                const vec3 next = cov * axis;
                const float len = glm::length(next);
                if (len < 1e-12f)
                    break;
                axis = next / len;
            }
            return axis;
        }

        bool RayPlane(const vec3 &origin, const vec3 &dir, const vec3 &point, const vec3 &normal, vec3 &hit)
        {
            const float denom = glm::dot(dir, normal);
            if (std::abs(denom) < 1e-6f)
                return false;
            const float t = glm::dot(point - origin, normal) / denom;
            if (t < 0.f)
                return false;
            hit = origin + dir * t;
            return true;
        }

    } // namespace

    // -------------------------------------------------------------------------
    // target + document
    // -------------------------------------------------------------------------
    namespace
    {
        ModelAsset *FindModelInSubtree(Scene &scene, NodeId *node, int depth = 0)
        {
            if (!node || !scene.IsNodeAlive(node) || depth > 64)
                return nullptr;
            if (ModelAsset *model = scene.GetModelForNode(node))
                return model;
            for (NodeId *child : scene.GetChildren(node))
                if (ModelAsset *model = FindModelInSubtree(scene, child, depth + 1))
                    return model;
            return nullptr;
        }
    } // namespace

    void RigEditor::ResolveTarget(Scene &scene)
    {
        // Scene loads and snapshot restores rebuild nodes without re-registering model roots, and they
        // delete the ModelAssets: validate m_model by membership before touching it.
        bool modelAlive = false;
        for (ModelAsset *m : scene.GetModels())
            modelAlive = modelAlive || m == m_model;
        if (m_model && !modelAlive)
        {
            m_model = nullptr;
            m_rootNode = nullptr;
            ClearDocument();
            m_heatBackup.clear(); // the scene was rebuilt: its vertex colours are the originals again
            m_weightUndo.clear();
            m_weightRedo.clear();
            m_weightDirty = false; // unsaved weights belonged to the destroyed in-memory asset
        }

        ModelAsset *model = nullptr;
        NodeId *root = nullptr;
        NodeId *sel = SelectionManager::Instance().GetSelectedNode();
        sel = sel && scene.IsNodeAlive(sel) ? sel : nullptr;
        for (NodeId *n = sel; n && !model; n = scene.GetParent(n))
        {
            for (ModelAsset *m : scene.GetModels())
            {
                for (NodeId *r : scene.GetModelRootNodes(m))
                {
                    if (r == n)
                    {
                        model = m;
                        root = n;
                        break;
                    }
                }
                if (model)
                    break;
            }
        }
        if (!model && sel)
        {
            // Fallback for restored scenes (no registered roots): the model comes from the node or its
            // subtree; the rig root is the highest ancestor that holds no other model's meshes.
            if (ModelAsset *m = FindModelInSubtree(scene, sel))
            {
                NodeId *r = sel;
                for (NodeId *p = scene.GetParent(r); p; p = scene.GetParent(p))
                {
                    ModelAsset *pm = scene.GetModelForNode(p);
                    if (pm && pm != m)
                        break;
                    r = p;
                }
                model = m;
                root = r;
            }
        }

        if (!model)
        {
            // Nothing of a model selected: the editor follows the selection, so drop the target (the
            // document is autosaved, undo/redo stay keyed to the rig file and survive a re-select).
            if (m_model)
            {
                if (m_dirty && m_dragBone < 0)
                    SaveJson(nullptr, true);
                RestoreHeatMap(scene);
                m_model = nullptr;
                m_rootNode = nullptr;
                ClearDocument();
                BuildCaches();
                m_status = "No model selected: select a node of a .pemesh model";
            }
            return;
        }
        if (model == m_model)
        {
            m_rootNode = root;
            return;
        }

        if (m_model && m_dirty)
            SaveJson();        // the rig.json beside the previous model is this document's own file
        RestoreHeatMap(scene); // same scene, other model: put the previous model's colours back
        m_weightDragging = false;
        // Undo/redo belong to the rig file, not the ModelAsset: a reload of the same .pemesh (scene
        // load, play/stop, re-import) keeps them, only another model drops them.
        const std::string docPath = model->GetFilePath().generic_string();
        if (docPath != m_docPath)
        {
            m_undo.clear();
            m_redo.clear();
            m_weightUndo.clear();
            m_weightRedo.clear();
            m_weightDirty = false;
            m_docPath = docPath;
        }
        m_model = model;
        m_rootNode = root;
        ClearDocument();
        BuildCaches();
        ReloadProjectPresets();
        std::string error;
        if (LoadJson(&error))
            m_status = "Loaded " + RigJsonPath().filename().generic_string();
        else if (model->HasSkeleton())
        {
            ImportSkeleton();
            m_status = "Imported the model's skeleton (" + std::to_string(m_bones.size()) + " bones)";
        }
        else
            m_status = "No rig yet: use Auto from Shells, Humanoid Preset or Add Bone";
        m_dirty = false;
    }

    void RigEditor::ClearDocument()
    {
        m_bones.clear();
        m_locks.clear();
        m_pins.clear();
        m_presetName.clear();
        m_selected = -1;
        m_dragBone = -1;
        m_dirty = false;
        m_nameBuf[0] = 0;
    }

    void RigEditor::PushUndo(bool keepPreset)
    {
        m_heatDirty = true;
        m_undo.push_back({m_bones, m_selected, m_presetName, m_locks, m_pins});
        if (!keepPreset)
            m_presetName.clear();
        if (static_cast<int>(m_undo.size()) > kMaxUndo)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
    }

    void RigEditor::Restore(const Snapshot &snapshot)
    {
        m_heatDirty = true;
        m_bones = snapshot.bones;
        m_presetName = snapshot.preset;
        m_locks = snapshot.locks;
        m_pins = snapshot.pins;
        m_selected = std::min(snapshot.selected, static_cast<int>(m_bones.size()) - 1);
        m_dragBone = -1;
        m_dirty = true;
    }

    void RigEditor::Undo()
    {
        if (m_undo.empty())
            return;
        m_redo.push_back({m_bones, m_selected, m_presetName, m_locks, m_pins});
        Restore(m_undo.back());
        m_undo.pop_back();
    }

    void RigEditor::Redo()
    {
        if (m_redo.empty())
            return;
        m_undo.push_back({m_bones, m_selected, m_presetName, m_locks, m_pins});
        Restore(m_redo.back());
        m_redo.pop_back();
    }

    mat4 RigEditor::ModelNodeWorld(int nodeIndex) const
    {
        mat4 m = m_model->GetNodeLocalMatrix(nodeIndex);
        for (int p = m_model->GetNodeParentIndex(nodeIndex); p >= 0; p = m_model->GetNodeParentIndex(p))
            m = m_model->GetNodeLocalMatrix(p) * m;
        // Rig space is the model root node's local frame (what the scene root node's world matrix maps).
        const int root = m_model->GetRootNodeIndex();
        return (root >= 0 ? glm::inverse(m_model->GetNodeLocalMatrix(root)) : mat4(1.f)) * m;
    }

    void RigEditor::CollectShells(std::vector<ShellInfo> &out) const
    {
        out.clear();
        if (!m_model)
            return;
        const std::vector<Vertex> &vertices = m_model->GetVertices();
        std::vector<vec3> points;
        for (int n = 0; n < m_model->GetNodeCount(); n++)
        {
            const int meshIndex = m_model->GetNodeMesh(n);
            const MeshInfo *mesh = meshIndex >= 0 ? m_model->GetMeshInfo(meshIndex) : nullptr;
            if (!mesh || mesh->verticesCount == 0 || mesh->vertexOffset + mesh->verticesCount > vertices.size())
                continue;
            const mat4 toRig = ModelNodeWorld(n);
            points.clear();
            points.reserve(mesh->verticesCount);
            ShellInfo shell;
            shell.nodeIndex = n;
            shell.meshIndex = meshIndex;
            shell.name = m_model->GetNodeName(n);
            shell.aabbMin = vec3(std::numeric_limits<float>::max());
            shell.aabbMax = vec3(-std::numeric_limits<float>::max());
            for (uint32_t v = 0; v < mesh->verticesCount; v++)
            {
                const float *p = vertices[mesh->vertexOffset + v].position;
                const vec3 r = vec3(toRig * vec4(p[0], p[1], p[2], 1.f));
                points.push_back(r);
                shell.centroid += r;
                shell.aabbMin = glm::min(shell.aabbMin, r);
                shell.aabbMax = glm::max(shell.aabbMax, r);
            }
            shell.centroid /= static_cast<float>(points.size());
            shell.axis = PrincipalAxis(points, shell.centroid);
            float maxPerp = 0.f;
            for (const vec3 &p : points)
            {
                const vec3 d = p - shell.centroid;
                const float along = glm::dot(d, shell.axis);
                shell.halfLength = std::max(shell.halfLength, std::abs(along));
                maxPerp = std::max(maxPerp, glm::length(d - shell.axis * along));
            }
            shell.radius = std::max(maxPerp * 0.9f, kMinRadius);
            shell.points = points;
            const vec3 ext = shell.aabbMax - shell.aabbMin;
            shell.volume = ext.x * ext.y * ext.z;
            out.push_back(shell);
        }
    }

    float RigEditor::ModelHeight() const
    {
        std::vector<ShellInfo> shells;
        CollectShells(shells);
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (const ShellInfo &s : shells)
        {
            lo = std::min(lo, s.aabbMin.y);
            hi = std::max(hi, s.aabbMax.y);
        }
        return hi > lo ? hi - lo : 1.f;
    }

    void RigEditor::ImportSkeleton()
    {
        ClearDocument();
        const Skeleton &skeleton = m_model->GetSkeleton();
        const mat4 invRoot = glm::inverse(skeleton.rootTransform);
        const float height = ModelHeight();
        m_bones.resize(skeleton.bones.size());
        for (size_t i = 0; i < skeleton.bones.size(); i++)
        {
            const BoneInfo &bone = skeleton.bones[i];
            m_bones[i].name = bone.name;
            m_bones[i].parent = bone.parentIndex;
            m_bones[i].head = vec3((invRoot * glm::inverse(bone.offsetMatrix))[3]);
            m_bones[i].headRadius = m_bones[i].tailRadius = height * 0.05f;
        }
        for (size_t i = 0; i < m_bones.size(); i++)
        {
            vec3 sum(0.f);
            int count = 0;
            for (const RigBone &child : m_bones)
            {
                if (child.parent == static_cast<int>(i))
                {
                    sum += child.head;
                    count++;
                }
            }
            RigBone &b = m_bones[i];
            if (count > 0)
                b.tail = sum / static_cast<float>(count);
            else if (b.parent >= 0 && glm::length(b.head - m_bones[b.parent].head) > 1e-5f)
                b.tail = b.head + (b.head - m_bones[b.parent].head) * 0.5f;
            else
                b.tail = b.head + vec3(0.f, height * 0.1f, 0.f);
        }
        m_dirty = true;
    }

    // One bone per shell. Hierarchy = Prim's spanning tree grown from the largest shell where the next
    // shell attached is the one with the smallest bounds gap to the tree (ties: larger overlap volume,
    // then nearer centres). A synthetic root sits under everything.
    // ponytail: AABB gaps; switch to capsule distances if a real hero misparents beyond a drag-drop fix.
    void RigEditor::PresetAuto()
    {
        std::vector<ShellInfo> shells;
        CollectShells(shells);
        ClearDocument();
        if (shells.empty())
        {
            m_status = "The model has no meshes to rig";
            return;
        }
        std::sort(shells.begin(), shells.end(), [](const ShellInfo &a, const ShellInfo &b)
                  { return a.volume > b.volume; });

        vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const ShellInfo &s : shells)
        {
            lo = glm::min(lo, s.aabbMin);
            hi = glm::max(hi, s.aabbMax);
        }
        const float height = std::max(hi.y - lo.y, 1e-3f);

        auto gapOverlap = [](const ShellInfo &a, const ShellInfo &b, float &gap, float &overlap)
        {
            gap = 0.f;
            overlap = 1.f;
            for (int k = 0; k < 3; k++)
            {
                const float l = std::max(a.aabbMin[k], b.aabbMin[k]), h = std::min(a.aabbMax[k], b.aabbMax[k]);
                if (h < l)
                {
                    gap += (l - h) * (l - h);
                    overlap = 0.f;
                }
                else
                    overlap *= h - l;
            }
            gap = std::sqrt(gap);
        };
        const size_t count = shells.size();
        std::vector<int> parentShell(count, -1), order{0};
        std::vector<char> inTree(count, 0);
        inTree[0] = 1;
        while (order.size() < count)
        {
            int bestI = -1, bestP = -1;
            float bestGap = std::numeric_limits<float>::max(), bestOverlap = -1.f, bestDist = std::numeric_limits<float>::max();
            for (size_t i = 0; i < count; i++)
            {
                if (inTree[i])
                    continue;
                for (size_t p = 0; p < count; p++)
                {
                    if (!inTree[p])
                        continue;
                    float gap, overlap;
                    gapOverlap(shells[i], shells[p], gap, overlap);
                    const float dist = glm::length(shells[i].centroid - shells[p].centroid);
                    // Overlap relative to the smaller of the two: a hand sitting inside a shovel's bounds
                    // beats a shovel tip grazing an upper arm.
                    overlap /= std::max(std::min(shells[i].volume, shells[p].volume), 1e-9f);
                    const bool better = gap < bestGap - 1e-6f ||
                                        (std::abs(gap - bestGap) <= 1e-6f &&
                                         (overlap > bestOverlap + 1e-6f || (std::abs(overlap - bestOverlap) <= 1e-6f && dist < bestDist)));
                    if (better)
                    {
                        bestGap = gap;
                        bestOverlap = overlap;
                        bestDist = dist;
                        bestI = static_cast<int>(i);
                        bestP = static_cast<int>(p);
                    }
                }
            }
            parentShell[bestI] = bestP;
            inTree[bestI] = 1;
            order.push_back(bestI);
        }

        const int rootBone = AddBone("root", -1);
        m_bones[rootBone].head = vec3(shells[0].centroid.x, lo.y, shells[0].centroid.z);
        m_bones[rootBone].tail = m_bones[rootBone].head + vec3(0.f, height * 0.08f, 0.f);
        m_bones[rootBone].headRadius = m_bones[rootBone].tailRadius = height * 0.03f;

        std::vector<int> shellBone(count, -1);
        for (int si : order)
        {
            const ShellInfo &s = shells[si];
            const int parent = parentShell[si] >= 0 ? shellBone[parentShell[si]] : rootBone;
            const int bone = AddBone(s.name, parent);
            shellBone[si] = bone;
            RigBone &b = m_bones[bone];
            const RigBone &p = m_bones[parent];
            const vec3 anchor = parentShell[si] >= 0 ? (p.head + p.tail) * 0.5f : vec3(s.centroid.x, lo.y - height, s.centroid.z);
            vec3 axis = s.axis;
            float halfLength = s.halfLength, radius = s.radius;
            if (halfLength < radius * 1.3f && glm::length(s.centroid - anchor) > 1e-4f)
            {
                // Blob-like shell: the bone runs from the parent through the centre instead of along a
                // meaningless principal axis.
                axis = glm::normalize(s.centroid - anchor);
                halfLength = 0.f;
                float maxPerp = 0.f;
                for (const vec3 &pt : s.points)
                {
                    const vec3 d = pt - s.centroid;
                    const float along = glm::dot(d, axis);
                    halfLength = std::max(halfLength, std::abs(along));
                    maxPerp = std::max(maxPerp, glm::length(d - axis * along));
                }
                radius = std::max(maxPerp * 0.9f, kMinRadius);
            }
            // The joint sits where the shell meets its parent (a hand gripping a shovel mid-shaft pivots
            // there), the bone runs to the far end.
            const float along = std::clamp(glm::dot(anchor - s.centroid, axis), -halfLength * 0.9f, halfLength * 0.9f);
            b.head = s.centroid + axis * along;
            b.tail = s.centroid + axis * (along >= 0.f ? -halfLength : halfLength);
            b.headRadius = b.tailRadius = radius;
            b.rigid = true;
            b.shell = s.name;
        }
        m_selected = rootBone;
        m_dirty = true;
        m_presetName = "Auto from Shells";
        m_status = "Auto rig: " + std::to_string(shells.size()) + " shells -> " + std::to_string(m_bones.size()) + " bones";
    }

    void RigEditor::ReloadProjectPresets()
    {
        m_projectPresetErrors.clear();
        m_projectPresets = RigPresetLibrary::LoadProjectPresets(&m_projectPresetErrors);
    }

    const RigPreset *RigEditor::FindPreset(std::string_view idOrName) const
    {
        const std::string needle = ToLower(std::string(idOrName));
        for (const RigPreset &preset : RigPresetLibrary::BuiltIn())
            if (ToLower(preset.id) == needle || ToLower(preset.name) == needle)
                return &preset;
        for (const RigPreset &preset : m_projectPresets)
            if (ToLower(preset.id) == needle || ToLower(preset.name) == needle)
                return &preset;
        return nullptr;
    }

    bool RigEditor::ApplyPreset(const RigPreset &preset, std::string &error)
    {
        if (m_shellCache.empty())
        {
            error = "the target model has no mesh bounds to fit";
            return false;
        }

        AABB bounds;
        bounds.min = vec3(std::numeric_limits<float>::max());
        bounds.max = -bounds.min;
        std::vector<std::string> shellNames;
        shellNames.reserve(m_shellCache.size());
        for (const ShellInfo &shell : m_shellCache)
        {
            bounds.min = glm::min(bounds.min, shell.aabbMin);
            bounds.max = glm::max(bounds.max, shell.aabbMax);
            shellNames.push_back(shell.name);
        }

        // Level 3: biped presets fit to the crotch / shoulders / neck found on the mesh instead of the box alone.
        MeasuredLandmarks landmarks;
        const bool useLandmarks = preset.landmarks.valid() && RigPresetLibrary::MeasureBiped(m_rigVerts, bounds, landmarks);
        std::vector<FittedRigPresetBone> fitted;
        std::vector<FittedRigPresetLock> fittedLocks;
        if (!RigPresetLibrary::Fit(preset, bounds, shellNames, fitted, error, useLandmarks ? &landmarks : nullptr,
                                   &fittedLocks))
            return false;

        ClearDocument();
        m_bones.reserve(fitted.size());
        std::vector<std::string> unmatched;
        for (size_t i = 0; i < fitted.size(); ++i)
        {
            const FittedRigPresetBone &source = fitted[i];
            RigBone bone;
            bone.name = source.name;
            bone.parent = source.parent;
            bone.head = source.head;
            bone.tail = source.tail;
            bone.headRadius = source.headRadius;
            bone.tailRadius = source.tailRadius;
            bone.rigid = source.rigid;
            bone.spline = source.spline;
            bone.shell = source.shell;
            bone.swingLimitDegrees = source.swingLimitDegrees;
            bone.twistLimitDegrees = source.twistLimitDegrees;
            m_bones.push_back(std::move(bone));
            if (!preset.bones[i].shellPatterns.empty() && source.shell.empty())
                unmatched.push_back(source.name);
        }
        // Level 2: every joint drops to the middle of the mesh along Z (the drag handles' Volume snap), and a
        // capsule never stays fatter than the limb it sits in. Misses (a hand outside the mesh) keep the fit.
        int snapped = 0, missed = 0;
        if (!m_rigTris.empty())
        {
            const float farZ = bounds.max.z + (bounds.max.z - bounds.min.z) + 1.f;
            auto snapJoint = [&](vec3 &p, float &radius)
            {
                float tEnter, tExit;
                if (!RayModel(vec3(p.x, p.y, farZ), vec3(0.f, 0.f, -1.f), tEnter, tExit))
                {
                    ++missed;
                    return;
                }
                ++snapped;
                if (tExit == std::numeric_limits<float>::max())
                {
                    p.z = farZ - tEnter; // open or flat mesh (an alpha cutout): sit on the surface, keep the radius
                    return;
                }
                p.z = farZ - (tEnter + tExit) * 0.5f;
                radius = std::max(std::min(radius, (tExit - tEnter) * 0.5f), kMinRadius);
            };
            for (RigBone &bone : m_bones)
            {
                snapJoint(bone.head, bone.headRadius);
                snapJoint(bone.tail, bone.tailRadius);
            }
        }
        m_pins = preset.pins;
        for (const FittedRigPresetLock &source : fittedLocks)
        {
            const int bone = FindBone(source.bone);
            if (bone < 0)
                continue;
            RigLock lock;
            lock.bone = source.bone;
            lock.target = source.target;
            lock.anchor = source.hasAnchor ? source.anchor : m_bones[bone].tail;
            lock.reach = source.reach;
            lock.enabled = source.enabled;
            m_locks.push_back(std::move(lock));
        }
        m_selected = m_bones.empty() ? -1 : 0;
        m_dirty = true;
        m_heatDirty = true;
        m_presetName = preset.name;

        m_status = "Applied preset " + preset.name + " (" + std::to_string(m_bones.size()) + " bones)";
        if (useLandmarks)
        {
            char stations[96];
            snprintf(stations, sizeof(stations), "; landmarks hips %.2f shoulders %.2f neck %.2f", landmarks.hips,
                     landmarks.shoulders, landmarks.neck);
            m_status += stations;
            if (landmarks.defaulted.hips || landmarks.defaulted.shoulders || landmarks.defaulted.neck)
            {
                m_status += " (defaulted:";
                if (landmarks.defaulted.hips)
                    m_status += " hips";
                if (landmarks.defaulted.shoulders)
                    m_status += " shoulders";
                if (landmarks.defaulted.neck)
                    m_status += " neck";
                m_status += ")";
            }
        }
        if (snapped + missed > 0)
            m_status += "; " + std::to_string(snapped) + " joints snapped into the mesh" +
                        (missed ? ", " + std::to_string(missed) + " outside it" : std::string());
        if (!unmatched.empty())
        {
            m_status += "; warning: no shell match for ";
            const size_t shown = std::min<size_t>(unmatched.size(), 4);
            for (size_t i = 0; i < shown; ++i)
                m_status += (i == 0 ? "" : ", ") + unmatched[i];
            if (shown < unmatched.size())
                m_status += " +" + std::to_string(unmatched.size() - shown) + " more";
            m_status += ". Each shell can have only one owner.";
        }
        return true;
    }

    int RigEditor::AddBone(const std::string &name, int parent)
    {
        RigBone bone;
        bone.name = UniqueName(name.empty() ? "bone" : name);
        bone.parent = parent >= 0 && parent < static_cast<int>(m_bones.size()) ? parent : -1;
        if (bone.parent >= 0)
        {
            const RigBone &p = m_bones[bone.parent];
            const vec3 dir = glm::length(p.tail - p.head) > 1e-5f ? glm::normalize(p.tail - p.head) : vec3(0.f, 1.f, 0.f);
            bone.head = p.tail;
            bone.tail = p.tail + dir * glm::length(p.tail - p.head) * 0.7f;
            bone.headRadius = p.tailRadius;
            bone.tailRadius = p.tailRadius;
        }
        m_bones.push_back(bone);
        m_dirty = true;
        return static_cast<int>(m_bones.size()) - 1;
    }

    void RigEditor::RemoveBone(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_bones.size()))
            return;
        const int parent = m_bones[index].parent;
        for (RigBone &b : m_bones)
        {
            if (b.parent == index)
                b.parent = parent;
            else if (b.parent > index)
                b.parent--;
        }
        m_bones.erase(m_bones.begin() + index);
        m_selected = std::min(m_selected, static_cast<int>(m_bones.size()) - 1);
        m_dragBone = -1;
        m_dirty = true;
    }

    bool RigEditor::IsAncestor(int ancestor, int index) const
    {
        for (int p = index; p >= 0 && p < static_cast<int>(m_bones.size()); p = m_bones[p].parent)
            if (p == ancestor)
                return true;
        return false;
    }

    bool RigEditor::SetParent(int index, int parent)
    {
        if (index < 0 || index >= static_cast<int>(m_bones.size()) || parent == index)
            return false;
        if (parent >= 0 && (parent >= static_cast<int>(m_bones.size()) || IsAncestor(index, parent)))
            return false;
        m_bones[index].parent = parent;
        m_dirty = true;
        return true;
    }

    int RigEditor::FindBone(const std::string &name) const
    {
        for (size_t i = 0; i < m_bones.size(); i++)
            if (m_bones[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    std::string RigEditor::UniqueName(std::string base) const
    {
        std::string name = base;
        for (int n = 1; FindBone(name) >= 0; n++)
            name = base + "." + std::string(n < 100 ? (n < 10 ? "00" : "0") : "") + std::to_string(n);
        return name;
    }

    void RigEditor::MoveTail(int index, const vec3 &tail)
    {
        RigBone &b = m_bones[index];
        const vec3 old = b.tail;
        b.tail = tail;
        for (RigBone &child : m_bones)
            if (child.parent == index && glm::length(child.head - old) < 1e-4f)
                child.head = tail; // connected child follows, Blender-style
        m_dirty = true;
    }

    // -------------------------------------------------------------------------
    // rig.json
    // -------------------------------------------------------------------------
    std::filesystem::path RigEditor::RigJsonPath() const
    {
        if (!m_model || m_model->GetFilePath().empty())
            return {};
        std::filesystem::path p = m_model->GetFilePath();
        return p.replace_extension(".rig.json");
    }

    std::string RigEditor::DocumentJson() const
    {
        nlohmann::ordered_json j;
        j["version"] = kRigJsonVersion;
        j["model"] = m_model ? m_model->GetFilePath().filename().generic_string() : "";
        nlohmann::ordered_json bones = nlohmann::ordered_json::array();
        for (const RigBone &b : m_bones)
        {
            nlohmann::ordered_json bone;
            bone["name"] = b.name;
            bone["parent"] = b.parent >= 0 ? m_bones[b.parent].name : "";
            bone["head"] = FromVec3(b.head);
            bone["tail"] = FromVec3(b.tail);
            bone["radius_head"] = b.headRadius;
            bone["radius_tail"] = b.tailRadius;
            bone["rigid"] = b.rigid;
            bone["spline"] = b.spline;
            bone["shell"] = b.shell;
            if (b.swingLimitDegrees > 0.f || b.twistLimitDegrees > 0.f)
                bone["limit"] = {{"swing_degrees", b.swingLimitDegrees}, {"twist_degrees", b.twistLimitDegrees}};
            bones.push_back(bone);
        }
        j["bones"] = bones;
        nlohmann::ordered_json locks = nlohmann::ordered_json::array();
        for (const RigLock &lock : m_locks)
            if (!lock.automatic) // holds captured while posing belong to the session, not to the rig document
                locks.push_back({{"bone", lock.bone},
                                 {"target", lock.target},
                                 {"anchor", FromVec3(lock.anchor)},
                                 {"reach", lock.reach},
                                 {"enabled", lock.enabled}});
        j["locks"] = locks;
        j["pins"] = m_pins;
        return j.dump(2);
    }

    bool RigEditor::SaveJson(std::string *error, bool quiet)
    {
        const std::filesystem::path path = RigJsonPath();
        if (path.empty())
        {
            if (error)
                *error = "the model has no file path";
            return false;
        }
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            if (error)
                *error = "could not write " + path.generic_string();
            return false;
        }
        out << DocumentJson() << '\n';
        m_dirty = false;
        if (!quiet)
            m_status = "Saved " + path.filename().generic_string();
        return true;
    }

    bool RigEditor::LoadJson(std::string *error)
    {
        const std::filesystem::path path = RigJsonPath();
        std::ifstream in(path.empty() ? std::filesystem::path("") : path, std::ios::binary);
        if (path.empty() || !in)
        {
            if (error)
                *error = "no rig.json beside the model";
            return false;
        }
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("bones") || !j["bones"].is_array())
        {
            if (error)
                *error = "invalid rig.json";
            return false;
        }
        std::vector<RigBone> loadedBones;
        std::vector<std::string> parents;
        bool anyShell = false;
        try
        {
            auto readPoint = [](const nlohmann::json &bone, const char *field, const vec3 &fallback)
            {
                if (!bone.contains(field))
                    return fallback;
                const nlohmann::json &value = bone[field];
                if (!value.is_array() || value.size() != 3 ||
                    !std::all_of(value.begin(), value.end(), [](const nlohmann::json &component)
                                 { return component.is_number(); }))
                    throw std::runtime_error(std::string(field) + " must contain three numbers");
                const vec3 point(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                    throw std::runtime_error(std::string(field) + " must be finite");
                return point;
            };
            auto uniqueLoadedName = [&](std::string base)
            {
                auto exists = [&](const std::string &name)
                {
                    return std::any_of(loadedBones.begin(), loadedBones.end(), [&](const RigBone &bone)
                                       { return bone.name == name; });
                };
                std::string name = base;
                for (int suffix = 1; exists(name); ++suffix)
                    name = base + "." + std::string(suffix < 100 ? (suffix < 10 ? "00" : "0") : "") +
                           std::to_string(suffix);
                return name;
            };

            for (const nlohmann::json &jb : j["bones"])
            {
                if (!jb.is_object())
                    throw std::runtime_error("each bone must be an object");
                RigBone b;
                b.name = uniqueLoadedName(jb.value("name", "bone"));
                b.head = readPoint(jb, "head", vec3(0.f));
                b.tail = readPoint(jb, "tail", b.head + vec3(0.f, 0.1f, 0.f));
                b.headRadius = std::max(jb.value("radius_head", 0.05f), kMinRadius);
                b.tailRadius = std::max(jb.value("radius_tail", 0.05f), kMinRadius);
                if (!std::isfinite(b.headRadius) || !std::isfinite(b.tailRadius))
                    throw std::runtime_error("bone radii must be finite");
                b.rigid = jb.value("rigid", false);
                b.spline = jb.value("spline", false);
                b.shell = jb.value("shell", "");
                if (jb.contains("limit"))
                {
                    const nlohmann::json &limit = jb["limit"];
                    if (!limit.is_object() || !limit.contains("swing_degrees") ||
                        !limit["swing_degrees"].is_number() || !limit.contains("twist_degrees") ||
                        !limit["twist_degrees"].is_number())
                        throw std::runtime_error("limit must contain numeric swing_degrees and twist_degrees");
                    b.swingLimitDegrees = limit["swing_degrees"].get<float>();
                    b.twistLimitDegrees = limit["twist_degrees"].get<float>();
                    if (!std::isfinite(b.swingLimitDegrees) || !std::isfinite(b.twistLimitDegrees) ||
                        b.swingLimitDegrees < 0.f || b.swingLimitDegrees > 180.f ||
                        b.twistLimitDegrees < 0.f || b.twistLimitDegrees > 180.f)
                        throw std::runtime_error("limit degrees must be finite numbers from 0 to 180");
                }
                anyShell |= jb.contains("shell");
                loadedBones.push_back(std::move(b));
                parents.push_back(jb.value("parent", ""));
            }
        }
        catch (const std::exception &exception)
        {
            if (error)
                *error = std::string("invalid rig.json: ") + exception.what();
            return false;
        }

        auto findLoadedBone = [&](const std::string &name)
        {
            for (size_t i = 0; i < loadedBones.size(); ++i)
                if (loadedBones[i].name == name)
                    return static_cast<int>(i);
            return -1;
        };
        for (size_t i = 0; i < loadedBones.size(); ++i)
        {
            if (parents[i].empty())
                continue;
            loadedBones[i].parent = findLoadedBone(parents[i]);
            if (loadedBones[i].parent < 0)
            {
                if (error)
                    *error = "invalid rig.json: unknown parent " + parents[i];
                return false;
            }
        }
        for (size_t i = 0; i < loadedBones.size(); ++i)
        {
            std::vector<bool> visited(loadedBones.size(), false);
            for (int bone = static_cast<int>(i); bone >= 0; bone = loadedBones[bone].parent)
            {
                if (visited[bone])
                {
                    if (error)
                        *error = "invalid rig.json: parent cycle";
                    return false;
                }
                visited[bone] = true;
            }
        }
        // Files written before shell bindings existed: auto-rig bones carry their shell's name.
        if (!anyShell)
            for (RigBone &b : loadedBones)
                for (const ShellInfo &sh : m_shellCache)
                    if (sh.name == b.name)
                        b.shell = sh.name;
        ClearDocument();
        m_bones = std::move(loadedBones);
        try
        {
            if (j.contains("locks") && j["locks"].is_array())
                for (const nlohmann::json &jl : j["locks"])
                {
                    if (!jl.is_object() || !jl.contains("bone") || !jl["bone"].is_string() ||
                        jl["bone"].get<std::string>().empty())
                        continue;
                    RigLock lock;
                    lock.bone = jl["bone"].get<std::string>();
                    lock.target = jl.value("target", "");
                    if (jl.contains("anchor") && !StrictVec3(jl["anchor"], lock.anchor))
                        continue; // malformed anchor: skip this lock, never guess a rig-space origin
                    lock.reach = std::clamp(jl.value("reach", 1.f), 0.3f, 1.f);
                    lock.enabled = jl.value("enabled", true);
                    m_locks.push_back(std::move(lock));
                }
        }
        catch (const std::exception &)
        {
            m_locks.clear(); // a hand-edited locks entry with wrong types drops the section, never the editor
        }
        if (j.contains("pins") && j["pins"].is_array())
            for (const nlohmann::json &pin : j["pins"])
                if (pin.is_string() && !pin.get<std::string>().empty())
                    m_pins.push_back(pin.get<std::string>());
        m_heatDirty = true;
        m_dirty = false;
        return true;
    }

    // -------------------------------------------------------------------------
    // rig.* actions
    // -------------------------------------------------------------------------
    std::string RigEditor::ProjectPresetsJson()
    {
        ReloadProjectPresets();
        nlohmann::json presets = nlohmann::json::array(), builtin = nlohmann::json::array();
        for (const RigPreset &preset : m_projectPresets)
            presets.push_back({{"id", preset.id}, {"name", preset.name}, {"description", preset.description}});
        for (const RigPreset &preset : RigPresetLibrary::BuiltIn())
            builtin.push_back({{"id", preset.id}, {"name", preset.name}, {"description", preset.description}});
        return nlohmann::json{{"directory", RigPresetLibrary::ProjectPresetDirectory().generic_string()},
                              {"presets", std::move(presets)},
                              {"builtin", std::move(builtin)},
                              {"errors", m_projectPresetErrors}}
            .dump();
    }

    std::string RigEditor::HandleAction(const std::string &action, const std::string &argsJson)
    {
        try
        {
            const nlohmann::json args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
            if (args.is_discarded() || !args.is_object())
                return R"({"error":"invalid args json"})";
            if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
                ResolveTarget(renderer->GetScene());
            auto ok = [&](nlohmann::json extra = nlohmann::json::object())
            {
                extra["ok"] = true;
                extra["action"] = action;
                extra["bone_count"] = m_bones.size();
                return extra.dump();
            };
            auto fail = [](const std::string &message)
            { return nlohmann::json{{"error", message}}.dump(); };
            auto boneArg = [&](int &index) -> bool
            {
                const nlohmann::json &b = args.contains("bone") ? args["bone"] : nlohmann::json();
                index = b.is_number_integer() ? b.get<int>() : b.is_string() ? FindBone(b.get<std::string>())
                                                                             : m_selected;
                return index >= 0 && index < static_cast<int>(m_bones.size());
            };

            if (action == "rig.state")
            {
                nlohmann::json state = nlohmann::json::parse(DocumentJson());
                state["selected"] = m_selected;
                state["dirty"] = m_dirty;
                state["snap"] = m_snap;
                state["snap_mode"] = m_snapMode;
                state["mirror_x"] = m_mirrorX;
                state["heat"] = m_heat;
                state["joint_blend"] = m_jointBlend;
                state["weights_dirty"] = m_weightDirty;
                state["weight_undo"] = m_weightUndo.size();
                state["weight_redo"] = m_weightRedo.size();
                state["shells"] = nlohmann::json::parse(ShellsJson());
                state["target"] = m_model ? m_model->GetLabel() : "";
                return ok(state);
            }
            if (action == "rig.mode")
            {
                AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                if (!timeline)
                    return fail("Animation Timeline is not available");
                const std::string mode = args.value("mode", "rig");
                if (mode != "rig" && mode != "edit" && mode != "animate" && mode != "pose")
                    return fail("mode must be rig|edit|animate|pose");
                *timeline->GetOpen() = true;
                timeline->SetRigMode(mode == "rig" || mode == "edit");
                return ok({{"mode", timeline->RigMode() ? "rig" : "animate"}});
            }
            if (action == "rig.shapes")
            {
                m_showShapes = args.value("show", true);
                return ok();
            }
            if (action == "rig.snap")
            {
                m_snap = args.value("enabled", m_snap);
                const std::string mode = args.value("mode", "");
                const char *modes[] = {"joints", "surface", "volume", "increment"};
                for (int i = 0; i < 4; i++)
                    if (mode == modes[i])
                        m_snapMode = i;
                if (!mode.empty() && mode != modes[m_snapMode])
                    return fail("unknown snap mode: " + mode + " (joints|surface|volume|increment)");
                return ok({{"enabled", m_snap}, {"mode", modes[m_snapMode]}});
            }
            if (action == "rig.mirror")
            {
                m_mirrorX = args.value("enabled", m_mirrorX);
                return ok({{"enabled", m_mirrorX}});
            }
            if (action == "rig.heat")
            {
                const std::string mode = args.value("mode", "");
                const char *modes[] = {"off", "selected", "all"};
                for (int i = 0; i < 3; i++)
                    if (mode == modes[i])
                        m_heat = i;
                if (!mode.empty() && mode != modes[m_heat])
                    return fail("unknown heat mode: " + mode + " (off|selected|all)");
                m_heatDirty = true;
                return ok({{"mode", modes[m_heat]}});
            }
            if (action == "rig.reference_load" || action == "rig.reference_clear" || action == "rig.pin" ||
                action == "rig.grab" || action == "rig.lock")
            {
                AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                if (!timeline)
                    return fail("Animation Timeline is not available");
                *timeline->GetOpen() = true;
                timeline->SetRigMode(false);
                return timeline->HandleAction("timeline." + action.substr(4), argsJson);
            }
            if (!m_model)
                return fail("no target model: select a node of a .pemesh model first");

            if (action == "rig.weight_save")
            {
                std::string error;
                return SaveWeights(error) ? ok({{"path", m_model->GetFilePath().generic_string()}}) : fail(error);
            }
            if (action == "rig.weight_undo" || action == "rig.weight_redo")
            {
                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                if (!renderer)
                    return fail("renderer not available");
                const bool redo = action == "rig.weight_redo";
                if (!WeightUndo(renderer->GetScene(), redo))
                    return fail(redo ? "no weight stroke to redo" : "no weight stroke to undo");
                return ok({{"undo", m_weightUndo.size()}, {"redo", m_weightRedo.size()}, {"dirty", m_weightDirty}});
            }
            if (action == "rig.weight_stroke")
            {
                if (!m_model->HasSkeleton())
                    return fail("weight strokes require a baked skeleton");
                AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                AnimationTimeline::ViewportPose pose;
                if (!timeline || !timeline->GetViewportPose(m_model, pose))
                {
                    pose.node = m_rootNode;
                    pose.boneTransforms = RestBoneTransforms(m_model->GetSkeleton());
                }

                const Skeleton &skeleton = m_model->GetSkeleton();
                const nlohmann::json &boneValue = args.contains("bone") ? args["bone"] : nlohmann::json();
                const int bone = boneValue.is_number_integer() ? boneValue.get<int>()
                                 : boneValue.is_string()       ? skeleton.GetBoneIndex(boneValue.get<std::string>())
                                                               : SelectedSkeletonBone();
                if (bone < 0 || bone >= skeleton.GetBoneCount())
                    return fail("unknown weight-stroke bone");
                if (!args.contains("center") || !args["center"].is_array() || args["center"].size() != 3)
                    return fail("center must be [x,y,z] in the posed mesh's rig space");
                for (const nlohmann::json &component : args["center"])
                    if (!component.is_number())
                        return fail("center must contain three finite numbers");
                const std::string space = args.value("space", "posed_rig");
                if (space != "posed_rig" && space != "posed" && space != "rig")
                    return fail("space must be posed_rig (posed and rig are accepted aliases)");

                RigWeightStroke::Stroke stroke;
                stroke.bone = skeleton.bones[bone].name;
                stroke.center = ToVec3(args["center"], vec3(std::numeric_limits<float>::quiet_NaN()));
                stroke.radius = args.value("radius", m_weightRadius);
                stroke.strength = args.value("strength", m_weightStrength);
                stroke.smoothRadius = args.value("smooth_radius", 0.f);
                const std::string mode = args.value("mode", "add");
                if (mode == "add")
                    stroke.mode = RigWeightStroke::Mode::Add;
                else if (mode == "erase")
                    stroke.mode = RigWeightStroke::Mode::Erase;
                else if (mode == "smooth")
                    stroke.mode = RigWeightStroke::Mode::Smooth;
                else
                    return fail("mode must be add|erase|smooth");

                std::vector<vec3> posedVertices;
                std::vector<RigWeightStroke::SkinWeight> base;
                if (!BuildPosedVertices(pose.boneTransforms, posedVertices))
                    return fail("could not build the posed CPU mesh");
                ReadWeights(base);
                const RigWeightStroke::Result result =
                    RigWeightStroke::Apply(m_rigVerts, posedVertices, base, skeleton, stroke, m_weightScratch);
                if (!result)
                    return fail("invalid weight stroke");
                if (result.affectedVertices == 0)
                    return ok({{"affected", 0}, {"skipped_spline", result.skippedSplineVertices}});

                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                if (!renderer)
                    return fail("renderer not available");
                PushWeightUndo();
                if (!WriteWeights(renderer->GetScene(), m_weightScratch))
                {
                    if (!m_weightUndo.empty())
                        m_weightUndo.pop_back();
                    return fail("could not map the model weight streams into the Scene stores");
                }
                UploadWeights(renderer->GetScene());
                const int authored = FindBone(skeleton.bones[bone].name);
                if (authored >= 0)
                    m_selected = authored;
                m_weightDirty = true;
                return ok({{"bone", stroke.bone},
                           {"mode", mode},
                           {"affected", result.affectedVertices},
                           {"skipped_spline", result.skippedSplineVertices},
                           {"dirty", true}});
            }

            if (action == "rig.undo" || action == "rig.redo")
            {
                action == "rig.undo" ? Undo() : Redo();
                return ok({{"undo", m_undo.size()}, {"redo", m_redo.size()}});
            }
            if (action == "rig.preset")
            {
                const std::string preset = args.contains("preset") ? args.value("preset", "auto")
                                           : args.contains("id")   ? args.value("id", "auto")
                                                                   : args.value("name", "auto");
                ReloadProjectPresets();
                const bool builtIn = preset == "auto" || preset == "existing" || preset == "clear";
                const RigPreset *projectPreset = builtIn ? nullptr : FindPreset(preset);
                if (!builtIn && !projectPreset)
                {
                    std::string available = "auto|existing|clear";
                    for (const RigPreset &candidate : RigPresetLibrary::BuiltIn())
                        available += "|" + candidate.id;
                    for (const RigPreset &candidate : m_projectPresets)
                        available += "|" + candidate.id;
                    return fail("unknown preset: " + preset + " (" + available + ")");
                }
                if (preset == "existing" && !m_model->HasSkeleton())
                    return fail("the model has no skeleton to import");
                PushUndo();
                if (preset == "auto")
                    PresetAuto();
                else if (preset == "existing")
                    ImportSkeleton();
                else if (preset == "clear")
                    ClearDocument();
                else
                {
                    std::string error;
                    if (!ApplyPreset(*projectPreset, error))
                    {
                        m_undo.pop_back();
                        return fail("could not apply preset '" + preset + "': " + error);
                    }
                }
                return projectPreset ? ok({{"preset", projectPreset->id}, {"name", projectPreset->name}, {"status", m_status}})
                                     : ok({{"preset", preset}});
            }
            if (action == "rig.transform")
            {
                if (m_bones.empty())
                    return fail("no bones to transform");
                const vec3 move = ToVec3(args.value("move", nlohmann::json()), vec3(0.f));
                const vec3 rotate = ToVec3(args.value("rotate", nlohmann::json()), vec3(0.f));
                const nlohmann::json &sj = args.value("scale", nlohmann::json());
                const vec3 scale = sj.is_number() ? vec3(sj.get<float>()) : ToVec3(sj, vec3(1.f));
                vec3 pivot(0.f);
                if (args.contains("pivot") && !StrictVec3(args["pivot"], pivot))
                    return fail("pivot must be [x, y, z]");
                PushUndo();
                TransformRig(m_undo.back().bones, move, rotate, scale, pivot);
                return ok();
            }
            if (action == "rig.add")
            {
                const nlohmann::json &p = args.contains("parent") ? args["parent"] : nlohmann::json();
                const int parent = p.is_number_integer() ? p.get<int>() : p.is_string() ? FindBone(p.get<std::string>())
                                                                                        : -1;
                PushUndo();
                const int bone = AddBonePair(args.value("name", "bone"), parent);
                RigBone &b = m_bones[bone];
                b.head = ToVec3(args.value("head", nlohmann::json()), b.head);
                b.tail = ToVec3(args.value("tail", nlohmann::json()), b.tail);
                b.headRadius = std::max(args.value("radius_head", b.headRadius), kMinRadius);
                b.tailRadius = std::max(args.value("radius_tail", b.tailRadius), kMinRadius);
                b.rigid = args.value("rigid", b.rigid);
                b.shell = args.value("shell", b.shell);
                m_selected = bone;
                return ok({{"index", bone}, {"name", b.name}});
            }
            int index = -1;
            if (action == "rig.set" || action == "rig.remove" || action == "rig.select")
            {
                if (!boneArg(index))
                    return fail("unknown bone");
            }
            if (action == "rig.select")
            {
                m_selected = index;
                return ok({{"index", index}});
            }
            if (action == "rig.chain")
            {
                if (!boneArg(index))
                    return fail("unknown bone");
                PushUndo();
                const int count = args.value("count", m_chainCount);
                const int last = MakeChain(index, count);
                if (m_mirrorX)
                {
                    const int twin = MirrorCounterpart(index);
                    if (twin >= 0)
                        MakeChain(twin, count);
                }
                std::vector<int> chain;
                ChainOf(index, chain);
                return ok({{"first", index}, {"last", last}, {"count", static_cast<int>(chain.size())}});
            }
            if (action == "rig.remove")
            {
                PushUndo();
                RemoveBone(index);
                return ok();
            }
            if (action == "rig.set")
            {
                PushUndo();
                RigBone &b = m_bones[index];
                if (args.contains("name") && args["name"].is_string() && args["name"] != b.name)
                    b.name = UniqueName(args["name"].get<std::string>());
                if (args.contains("parent"))
                {
                    const nlohmann::json &p = args["parent"];
                    const int parent = p.is_number_integer() ? p.get<int>() : p.is_string() && !p.get<std::string>().empty() ? FindBone(p.get<std::string>())
                                                                                                                             : -1;
                    if (!SetParent(index, parent))
                    {
                        m_undo.pop_back();
                        return fail("invalid parent (unknown, self or descendant)");
                    }
                }
                if (args.contains("head"))
                    b.head = ToVec3(args["head"], b.head);
                if (args.contains("tail"))
                    MoveTail(index, ToVec3(args["tail"], b.tail));
                if (args.contains("radius_head"))
                    b.headRadius = std::max(args.value("radius_head", b.headRadius), kMinRadius);
                if (args.contains("radius_tail"))
                    b.tailRadius = std::max(args.value("radius_tail", b.tailRadius), kMinRadius);
                if (args.contains("rigid"))
                    b.rigid = args.value("rigid", b.rigid);
                if (args.contains("spline"))
                    b.spline = args.value("spline", b.spline);
                if (args.contains("shell"))
                    b.shell = args.value("shell", b.shell);
                if (m_mirrorX)
                    SyncMirror(index);
                m_dirty = true;
                return ok({{"index", index}, {"name", b.name}});
            }
            if (action == "rig.save")
            {
                std::string error;
                return SaveJson(&error) ? ok({{"path", RigJsonPath().generic_string()}}) : fail(error);
            }
            if (action == "rig.load")
            {
                std::string error;
                PushUndo();
                if (LoadJson(&error))
                    return ok({{"path", RigJsonPath().generic_string()}});
                m_undo.pop_back();
                return fail(error);
            }
            if (action == "rig.bake")
            {
                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                if (!renderer)
                    return fail("renderer not available");
                std::string error, outPath;
                if (!Bake(renderer->GetScene(), error, outPath))
                    return fail(error);
                return ok({{"path", outPath}, {"note", m_bakeNote}});
            }
            return fail("unknown rig action");
        }
        catch (const std::exception &exception)
        {
            return nlohmann::json{{"error", std::string("invalid rig action arguments: ") + exception.what()}}.dump();
        }
    }

    // -------------------------------------------------------------------------
    // panel
    // -------------------------------------------------------------------------
    void RigEditor::DrawPanel(Scene &scene)
    {
        if (!m_model)
        {
            ImGui::TextDisabled("Select a node of a loaded model (.pemesh) in the Hierarchy to rig it.");
            return;
        }

        int shellCount = 0;
        for (int n = 0; n < m_model->GetNodeCount(); n++)
            shellCount += m_model->GetNodeMesh(n) >= 0 ? 1 : 0;
        ImGui::Text("%s%s", m_model->GetLabel().c_str(), m_dirty ? " *" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("%d shells   %d bones   %s", shellCount, static_cast<int>(m_bones.size()),
                            m_model->HasSkeleton() ? "has skeleton" : "unrigged");
        ImGui::SameLine(0.f, 18.f);
        ImGui::TextDisabled("Bind + Weights");

        const ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                if (m_jointBlend)
                    WeightUndo(scene, io.KeyShift);
                else
                    io.KeyShift ? Redo() : Undo();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            {
                if (m_jointBlend)
                    WeightUndo(scene, true);
                else
                    Redo();
            }
        }
        DrawToolbar();
        ImGui::Separator();

        const float bottom = ImGui::GetFrameHeightWithSpacing() + 4.f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (m_treeWidth <= 0.f)
            m_treeWidth = std::max(avail.x * 0.4f, 160.f);
        const float paneHeight = avail.y - bottom;
        ImGui::BeginChild("##rig_tree", ImVec2(m_treeWidth, paneHeight), ImGuiChildFlags_Borders);
        ui::PushCompactTree();
        ImGui::TextDisabled("Bones");
        if (ImGui::Selectable("(root level)", false))
            m_selected = -1;
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("RIG_BONE"))
            {
                PushUndo();
                if (!SetParent(*static_cast<const int *>(payload->Data), -1))
                    m_undo.pop_back();
            }
            ImGui::EndDragDropTarget();
        }
        DrawBoneTree(-1, 0);
        ui::PopCompactTree();
        ImGui::EndChild();
        ImGui::SameLine(0.f, 0.f);
        ui::SplitterV("##rig_split", m_treeWidth, 120.f, 200.f, avail.x, paneHeight);
        ImGui::SameLine(0.f, 0.f);
        ImGui::BeginChild("##rig_props", ImVec2(0.f, paneHeight), ImGuiChildFlags_Borders);
        DrawRigTransform();
        DrawBoneProperties();
        DrawJointBlendPanel(scene);
        ImGui::EndChild();

        // Autosave: the rig.json beside the model is the document, so an editor restart or scene
        // reload (which rebuilds the ModelAsset) never loses work. Waits for drags / edits to finish.
        if (m_dirty && m_dragBone < 0 && !ImGui::IsAnyItemActive() && !RigJsonPath().empty())
            SaveJson(nullptr, true);
        if (m_dragBone < 0 && !ImGui::IsAnyItemActive() &&
            (m_heatDirty || (m_heat == 1 && m_heatSelected != m_selected) || (m_heat == 0 && !m_heatBackup.empty())))
            UpdateHeatMap(scene);

        ImGui::TextDisabled("%s", m_status.empty() ? "Viewport: drag joints, Shift-drag a head to move the bone, drag the middle to move the bone; X/Y/Z lock an axis, Shift = precision, Ctrl = snap; Ctrl+Z undo" : m_status.c_str());
    }

    void RigEditor::DrawToolbar()
    {
        const bool hasSkeleton = m_model->HasSkeleton();
        ImGui::BeginDisabled(!hasSkeleton);
        if (ImGui::Button("Load Existing"))
        {
            PushUndo();
            ImportSkeleton();
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(hasSkeleton ? "Import the model's skeleton as editable bones" : "The model has no skeleton");
        // One menu for every way to generate bones: the two built-ins, then the project's rig presets.
        // The label is the preset the rig came from; any hand edit since makes it "Custom".
        const float presetWidth = ImGui::GetFontSize() * 10.f;
        ui::SameLineIfFits(presetWidth);
        ImGui::SetNextItemWidth(presetWidth);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 12.f));
        if (ImGui::BeginCombo("##rig_preset", m_presetName.empty() ? "Custom" : m_presetName.c_str()))
        {
            if (ImGui::Selectable("Auto from Shells", m_presetName == "Auto from Shells"))
            {
                PushUndo();
                PresetAuto();
            }
            ui::ItemTooltip("One bone per mesh shell along its long axis; parents from overlaps; rigid weights");
            auto presetItems = [&](std::span<const RigPreset> presets)
            {
                for (const RigPreset &preset : presets)
                {
                    if (ImGui::Selectable(preset.name.c_str(), m_presetName == preset.name))
                    {
                        PushUndo();
                        std::string error;
                        if (!ApplyPreset(preset, error))
                        {
                            m_undo.pop_back();
                            m_status = "Preset failed: " + error;
                        }
                    }
                    if (!preset.description.empty())
                        ui::ItemTooltip(preset.description.c_str());
                }
            };
            presetItems(RigPresetLibrary::BuiltIn());
            if (!m_projectPresets.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Assets/RigPresets");
                presetItems(m_projectPresets);
            }
            ImGui::EndCombo();
        }
        std::string presetTooltip = "Build the rig from a preset: the built-ins, or project data from Assets/RigPresets fitted to this "
                                    "model's bounds and shells. Each shell can have only one owner; unmatched expected shell "
                                    "patterns are reported in the status line.";
        if (!m_projectPresetErrors.empty())
            presetTooltip += "\nLoad warning: " + m_projectPresetErrors.front();
        ui::ItemTooltip(presetTooltip.c_str());
        ui::SameLineIfFits(ui::ButtonWidth("Add Bone"));
        if (ImGui::Button("Add Bone"))
        {
            PushUndo();
            m_selected = AddBonePair("bone", m_selected);
        }
        ui::ItemTooltip("New bone extruded from the selected bone's tail (or at the origin); with Mirror X on it comes as a .L/.R pair");
        ui::SameLineIfFits(ui::ButtonWidth("Spline Chain"));
        ImGui::BeginDisabled(m_selected < 0);
        if (ImGui::Button("Spline Chain"))
            ImGui::OpenPopup("Spline Chain");
        ImGui::EndDisabled();
        ui::ItemTooltip("Subdivide the selected bone into a chain of spline links: vertices blend the 4 nearest joints with "
                        "Catmull-Rom weights along the chain, so it bends smoothly (tails, capes, ropes, hair) instead of in rigid parts");
        if (ImGui::BeginPopup("Spline Chain"))
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.5f);
            ImGui::InputInt("Links", &m_chainCount);
            m_chainCount = std::clamp(m_chainCount, 2, 32);
            ImGui::SameLine();
            if (ImGui::Button("Convert") && m_selected >= 0)
            {
                PushUndo();
                const int first = m_selected;
                MakeChain(first, m_chainCount);
                if (m_mirrorX)
                {
                    const int twin = MirrorCounterpart(first);
                    if (twin >= 0)
                        MakeChain(twin, m_chainCount);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ui::SameLineIfFits(ui::ButtonWidth("Delete"));
        ImGui::BeginDisabled(m_selected < 0);
        if (ImGui::Button("Delete"))
        {
            PushUndo();
            RemoveBone(m_selected);
        }
        ImGui::EndDisabled();
        ui::SameLineIfFits(ui::ButtonWidth("Undo"));
        ImGui::BeginDisabled(m_undo.empty());
        if (ImGui::Button("Undo"))
            Undo();
        ImGui::EndDisabled();
        ui::ItemTooltip("Ctrl+Z");
        ui::SameLineIfFits(ui::ButtonWidth("Redo"));
        ImGui::BeginDisabled(m_redo.empty());
        if (ImGui::Button("Redo"))
            Redo();
        ImGui::EndDisabled();
        ui::ItemTooltip("Ctrl+Shift+Z / Ctrl+Y");
        ui::SameLineIfFits(ui::ButtonWidth("Save rig.json"));
        if (ImGui::Button("Save rig.json"))
        {
            std::string error;
            if (!SaveJson(&error))
                m_status = error;
        }
        ui::ItemTooltip("Writes <model>.rig.json beside the .pemesh (also the export contract for Blender)");
        ui::SameLineIfFits(ui::ButtonWidth("Load rig.json"));
        if (ImGui::Button("Load rig.json"))
        {
            std::string error;
            PushUndo();
            if (LoadJson(&error))
                m_status = "Loaded " + RigJsonPath().filename().generic_string();
            else
            {
                m_undo.pop_back();
                m_status = error;
            }
        }
        ui::SameLineIfFits(ui::ButtonWidth("Bake"));
        ImGui::BeginDisabled(m_bones.empty() || !m_model);
        if (ImGui::Button("Bake"))
        {
            std::string error, outPath;
            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            if (renderer && Bake(renderer->GetScene(), error, outPath))
                m_status = "Baked " + std::filesystem::path(outPath).filename().generic_string() + m_bakeNote;
            else
                m_status = "Bake failed: " + (renderer ? error : std::string("renderer not available"));
        }
        ImGui::EndDisabled();
        ui::ItemTooltip("Write the rig into the mesh: parts flattened into rig space, skeleton from the bones, joints/weights per vertex "
                        "(owned parts 100%, shapes for the rest) -> <model>_rigged.pemesh beside the source, then swap it into the scene");
        ui::SameLineIfFits(ui::CheckboxWidth("Shapes"));
        ImGui::Checkbox("Shapes", &m_showShapes);
        ui::ItemTooltip("Draw the influence capsules in the viewport");
        ui::SameLineIfFits(ui::CheckboxWidth("Snap"));
        ImGui::Checkbox("Snap", &m_snap);
        ui::ItemTooltip("Magnet for joint drags in the viewport, using the mode next to it (Ctrl while dragging inverts it: Ctrl snaps when this is off, and frees when it is on)");
        ui::SameLineIfFits(ImGui::GetFontSize() * 7.f, 2.f);
        ImGui::BeginDisabled(!m_snap);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
        ImGui::Combo("##snapmode", &m_snapMode, "Joints\0Surface\0Volume\0Increment\0");
        ImGui::EndDisabled();
        ui::ItemTooltip("Snap mode (only while Snap is on, or Ctrl is held while dragging)\nJoints: other bones' heads and tails   Surface: mesh surface under the cursor\nVolume: middle of the mesh under the cursor (Blender)   Increment: 0.01 grid");
        ui::SameLineIfFits(ui::CheckboxWidth("Mirror X"));
        ImGui::Checkbox("Mirror X", &m_mirrorX);
        ui::ItemTooltip("Blender X-Axis Mirror: edits to a bone named *.L / *.R (or _L/_R, .l/.r) mirror to its twin across the rig's X plane, and Add Bone / Extrude Child create the twin");
        ui::SameLineIfFits(ui::LabelledItemWidth(ImGui::GetFontSize() * 7.f, "Heat"));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
        if (ImGui::Combo("Heat", &m_heat, "Off\0Selected\0All\0"))
            m_heatDirty = true;
        ui::ItemTooltip("Weight heat map on the mesh: Selected = grey to orange by the selected bone's weight, All = every bone's colour blended by weight");
    }

    void RigEditor::DrawBoneTree(int parent, int depth)
    {
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            if (m_bones[i].parent != parent)
                continue;
            bool hasChildren = false;
            for (const RigBone &b : m_bones)
                hasChildren = hasChildren || b.parent == i;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (i == m_selected)
                flags |= ImGuiTreeNodeFlags_Selected;
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<intptr_t>(i)), flags, "%s%s", m_bones[i].name.c_str(),
                                                m_bones[i].rigid ? "  [rigid]" : "");
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_selected = i;
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("RIG_BONE", &i, sizeof(int));
                ImGui::TextUnformatted(m_bones[i].name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("RIG_BONE"))
                {
                    PushUndo();
                    if (!SetParent(*static_cast<const int *>(payload->Data), i))
                        m_undo.pop_back();
                }
                ImGui::EndDragDropTarget();
            }
            if (open)
            {
                if (hasChildren && depth < 64)
                    DrawBoneTree(i, depth + 1);
                ImGui::TreePop();
            }
        }
    }

    void RigEditor::TransformRig(std::span<const RigBone> base, const vec3 &move, const vec3 &rotateDegrees,
                                 const vec3 &scale, const vec3 &pivot)
    {
        if (base.size() != m_bones.size())
            return;
        const mat4 rotation = glm::mat4_cast(quat(glm::radians(rotateDegrees)));
        const mat4 m = glm::translate(mat4(1.f), move + pivot) * rotation * glm::scale(mat4(1.f), scale) *
                       glm::translate(mat4(1.f), -pivot);
        const float radiusScale = (std::abs(scale.x) + std::abs(scale.y) + std::abs(scale.z)) / 3.f;
        for (size_t i = 0; i < base.size(); ++i)
        {
            m_bones[i].head = vec3(m * vec4(base[i].head, 1.f));
            m_bones[i].tail = vec3(m * vec4(base[i].tail, 1.f));
            m_bones[i].headRadius = std::max(base[i].headRadius * radiusScale, kMinRadius);
            m_bones[i].tailRadius = std::max(base[i].tailRadius * radiusScale, kMinRadius);
        }
        m_dirty = true;
        m_heatDirty = true;
    }

    // Move / rotate / scale every bone at once. The fields are deltas from the drag's start (one undo step,
    // recomputed from that baseline each frame so nothing drifts) and reset when the drag ends.
    void RigEditor::DrawRigTransform()
    {
        if (m_bones.empty() || !ImGui::CollapsingHeader("Rig Transform", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 10.f);
        ImGui::DragFloat3("Pivot", &m_xformPivot.x, 0.005f, 0.f, 0.f, "%.3f");
        ui::ItemTooltip("Rig-space point to rotate and scale around (X right, Y up, Z forward).");
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            m_xformPivot = vec3(0.f);
        ui::ItemTooltip("Reset the transform pivot to the rig origin.");
        auto field = [&](const char *label, vec3 &value, float speed, const char *format, const char *tip)
        {
            ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.f);
            const bool changed = ImGui::DragFloat3(label, &value.x, speed, 0.f, 0.f, format);
            if (ImGui::IsItemActivated())
            {
                PushUndo();
                m_xformBase = m_undo.back().bones;
            }
            // Apply before the deactivation reset: a typed value changes and deactivates on the same frame.
            if (changed && !m_xformBase.empty())
                TransformRig(m_xformBase, m_xformMove, m_xformRotate, m_xformScale, m_xformPivot);
            if (ImGui::IsItemDeactivated())
            {
                m_xformBase.clear();
                m_xformMove = vec3(0.f);
                m_xformRotate = vec3(0.f);
                m_xformScale = vec3(1.f);
            }
            ui::ItemTooltip(tip);
        };
        field("Move", m_xformMove, 0.005f, "%.3f", "Offset every bone in rig units (X right, Y up, Z forward).");
        field("Rotate", m_xformRotate, 0.5f, "%.1f", "Rotate every bone about the pivot, degrees around X, Y, Z.");
        field("Scale", m_xformScale, 0.01f, "%.3f", "Scale every bone about the pivot; radii follow the average scale.");
        ImGui::Separator();
    }

    void RigEditor::DrawBoneProperties()
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_bones.size()))
        {
            ImGui::TextDisabled("No bone selected");
            return;
        }
        RigBone &b = m_bones[m_selected];
        static int s_nameFor = -1;
        if (s_nameFor != m_selected || !ImGui::IsAnyItemActive())
        {
            snprintf(m_nameBuf, sizeof(m_nameBuf), "%s", b.name.c_str());
            s_nameFor = m_selected;
        }
        const bool nameEdited = ImGui::InputText("Name", m_nameBuf, sizeof(m_nameBuf));
        if (ImGui::IsItemActivated())
            PushUndo();
        if (nameEdited)
        {
            if (m_nameBuf[0] && b.name != m_nameBuf)
            {
                b.name = UniqueName(m_nameBuf);
                m_dirty = true;
            }
        }
        ui::ItemTooltip("Bone name (must be unique in this rig)");

        const char *parentName = b.parent >= 0 ? m_bones[b.parent].name.c_str() : "(none)";
        if (ImGui::BeginCombo("Parent", parentName))
        {
            if (ImGui::Selectable("(none)", b.parent < 0))
            {
                PushUndo();
                SetParent(m_selected, -1);
            }
            for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
            {
                if (i == m_selected || IsAncestor(m_selected, i))
                    continue;
                if (ImGui::Selectable(m_bones[i].name.c_str(), b.parent == i))
                {
                    PushUndo();
                    SetParent(m_selected, i);
                }
            }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Parent bone (drag bones in the tree to reparent too)");

        const float step = std::max(ModelHeight() * 0.002f, 0.0005f);
        // Every edit widget snapshots on activation (before its first change), so one drag = one undo step.
        auto undoOnActivate = [&]()
        {
            if (ImGui::IsItemActivated())
                PushUndo();
        };
        m_dirty |= ImGui::DragFloat3("Head", &b.head.x, step, 0.f, 0.f, "%.3f");
        undoOnActivate();
        vec3 tail = b.tail;
        const bool tailEdited = ImGui::DragFloat3("Tail", &tail.x, step, 0.f, 0.f, "%.3f");
        undoOnActivate();
        if (tailEdited)
            MoveTail(m_selected, tail);
        const bool owned = !b.shell.empty();
        ImGui::BeginDisabled(owned);
        m_dirty |= ImGui::DragFloat("Head radius", &b.headRadius, step, kMinRadius, 100.f, "%.3f");
        undoOnActivate();
        m_dirty |= ImGui::DragFloat("Tail radius", &b.tailRadius, step, kMinRadius, 100.f, "%.3f");
        undoOnActivate();
        m_dirty |= ImGui::Checkbox("Rigid", &b.rigid);
        undoOnActivate();
        ImGui::EndDisabled();
        if (owned)
            ImGui::TextDisabled("Shape unused: this bone owns the part (head = pivot, tail = axis)");
        else
            ui::ItemTooltip("Vertices inside this shape belong to this bone only (parts characters); off = blended with overlapping shapes");
        ImGui::SameLine();
        m_dirty |= ImGui::Checkbox("Spline", &b.spline);
        undoOnActivate();
        ui::ItemTooltip("Link of a spline chain (consecutive spline bones): vertices claimed by the chain blend its 4 nearest joints "
                        "with Catmull-Rom weights, bending smoothly along the chain");
        if (ImGui::BeginCombo("Shell", b.shell.empty() ? "(none)" : b.shell.c_str()))
        {
            if (ImGui::Selectable("(none)", b.shell.empty()) && !b.shell.empty())
            {
                PushUndo();
                b.shell.clear();
                m_dirty = true;
            }
            for (const ShellInfo &sh : m_shellCache)
                if (ImGui::Selectable(sh.name.c_str(), sh.name == b.shell) && sh.name != b.shell)
                {
                    PushUndo();
                    b.shell = sh.name;
                    m_dirty = true;
                }
            ImGui::EndCombo();
        }
        ui::ItemTooltip("Part owned outright by this bone: every vertex of that mesh follows it fully; the shape then only reaches other parts");

        ImGui::SeparatorText("Pose Constraints");
        bool pinned = std::find(m_pins.begin(), m_pins.end(), b.name) != m_pins.end();
        if (ImGui::Checkbox("Puppet Pin", &pinned))
        {
            PushUndo(true);
            if (pinned)
                m_pins.push_back(b.name);
            else
                std::erase(m_pins, b.name);
            m_dirty = true;
        }
        ui::ItemTooltip("Puppet chains stop before this bone. The Animation Timeline uses this bind constraint while posing.");

        m_dirty |= ImGui::DragFloat("Swing limit", &b.swingLimitDegrees, 1.f, 0.f, 180.f, "%.0f deg");
        undoOnActivate();
        b.swingLimitDegrees = std::clamp(b.swingLimitDegrees, 0.f, 180.f);
        ui::ItemTooltip("Bind-frame swing cone around local +Y. Zero is unrestricted.");
        m_dirty |= ImGui::DragFloat("Twist limit", &b.twistLimitDegrees, 1.f, 0.f, 180.f, "%.0f deg");
        undoOnActivate();
        b.twistLimitDegrees = std::clamp(b.twistLimitDegrees, 0.f, 180.f);
        ui::ItemTooltip("Bind-frame twist around local +Y. Zero is unrestricted.");

        const auto fixedLock = std::find_if(m_locks.begin(), m_locks.end(), [&](const RigLock &lock)
                                            { return lock.bone == b.name && lock.target.empty(); });
        const bool hasOtherLock = std::any_of(m_locks.begin(), m_locks.end(), [&](const RigLock &lock)
                                              { return lock.bone == b.name && !lock.target.empty(); });
        bool restLocked = fixedLock != m_locks.end();
        ImGui::BeginDisabled(hasOtherLock);
        if (ImGui::Checkbox("Rest Lock", &restLocked))
        {
            PushUndo(true);
            if (restLocked)
                m_locks.push_back({b.name, "", b.tail, 1.f, true});
            else
                m_locks.erase(std::find_if(m_locks.begin(), m_locks.end(), [&](const RigLock &lock)
                                           { return lock.bone == b.name && lock.target.empty(); }));
            m_dirty = true;
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(hasOtherLock ? "This bone already has a target lock; remove it in the Animation Timeline first."
                                     : "Plant this bone at its authored rest tail. The lock is stored in the rig sidecar.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (m_mirrorX)
            SyncMirror(m_selected);
        ImGui::TextDisabled("Length %.3f", glm::length(b.tail - b.head));
        if (ImGui::Button("Extrude Child"))
        {
            PushUndo();
            const std::string side = MirrorName(b.name);
            const std::string stem = side.empty() ? b.name : b.name.substr(0, b.name.size() - 2);
            m_selected = AddBonePair(side.empty() ? stem + ".child" : stem + ".child" + b.name.substr(b.name.size() - 2), m_selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Bone"))
        {
            PushUndo();
            RemoveBone(m_selected);
        }
        if (b.parent >= 0)
        {
            ImGui::SameLine();
            if (ImGui::Button("Connect to Parent"))
            {
                PushUndo();
                b.head = m_bones[b.parent].tail; // Blender's Connect: head onto the parent's tail, tail stays
                if (m_mirrorX)
                    SyncMirror(m_selected);
                m_dirty = true;
            }
            ui::ItemTooltip("Put this bone's head exactly on its parent's tail (Blender: Connect); from then on the parent's tail drags it along");
        }
    }

    int RigEditor::SelectedSkeletonBone() const
    {
        if (!m_model || !m_model->HasSkeleton() || m_selected < 0 || m_selected >= static_cast<int>(m_bones.size()))
            return -1;
        return m_model->GetSkeleton().GetBoneIndex(m_bones[m_selected].name);
    }

    void RigEditor::DrawJointBlendPanel(Scene &scene)
    {
        if (!m_model->HasSkeleton())
            return;
        ImGui::SeparatorText("Weights");
        if (ImGui::Checkbox("Joint Blend", &m_jointBlend))
        {
            if (!m_jointBlend && m_weightDragging)
            {
                if (m_weightStrokeChanged)
                    UploadWeights(scene);
                m_weightDragging = false;
                m_weightStrokeChanged = false;
                m_weightHasLastCenter = false;
            }
            m_status = m_jointBlend ? "Joint Blend: paint the selected bind bone on the Timeline pose."
                                    : "Joint Blend off.";
        }
        ui::ItemTooltip("Paint actual skin weights on the current Animation Timeline pose. GPU weights upload once when the drag ends.");
        if (!m_jointBlend)
            return;

        ImGui::Indent();
        const int selectedBone = SelectedSkeletonBone();
        const Skeleton &skeleton = m_model->GetSkeleton();
        if (selectedBone >= 0)
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.f), "%s", skeleton.bones[selectedBone].name.c_str());
        else
            ImGui::TextColored(ImVec4(1.f, 0.6f, 0.15f, 1.f), "(select a bind bone)");
        if (ImGui::RadioButton("Add", m_weightMode == RigWeightStroke::Mode::Add))
            m_weightMode = RigWeightStroke::Mode::Add;
        ImGui::SameLine();
        if (ImGui::RadioButton("Erase", m_weightMode == RigWeightStroke::Mode::Erase))
            m_weightMode = RigWeightStroke::Mode::Erase;
        ImGui::SameLine();
        if (ImGui::RadioButton("Smooth", m_weightMode == RigWeightStroke::Mode::Smooth))
            m_weightMode = RigWeightStroke::Mode::Smooth;
        ImGui::DragFloat("Radius##joint_blend", &m_weightRadius, 0.002f, kMinRadius,
                         std::max(ModelHeight(), kMinRadius), "%.3f");
        ImGui::SliderFloat("Strength##joint_blend", &m_weightStrength, 0.f, 1.f, "%.2f");
        ImGui::BeginDisabled(m_weightUndo.empty());
        if (ImGui::Button("Weight Undo"))
            WeightUndo(scene, false);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_weightRedo.empty());
        if (ImGui::Button("Weight Redo"))
            WeightUndo(scene, true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(m_weightDirty ? "Save Weights *" : "Save Weights"))
        {
            std::string error;
            m_status = SaveWeights(error) ? "Saved weights to " + m_model->GetFilePath().filename().generic_string()
                                          : error;
        }
        ui::ItemTooltip("Explicitly overwrite the current .pemesh. Weight strokes never autosave.");
        ImGui::Unindent();
    }

    // -------------------------------------------------------------------------
    // bake
    // -------------------------------------------------------------------------
    // Writes the rig into the mesh: every part is flattened into rig space (node locals become
    // identity), the skeleton is built from the bones, ComputeVertexWeights fills joints/weights, the
    // result is saved as <model>_rigged.pemesh beside the source with the rig.json copied next to it,
    // and the scene swaps the source model for the rigged one.
    // Rest frames are bone-aligned like Blender's: Y runs head -> tail (Rot Y = twist), X is the rig X
    // projected perpendicular to the bone (Rot X = the forward/back bend for upright bones, so a vertical
    // leg keeps its rig-space meaning), Z = X x Y. Pose rotations and locations are expressed in this frame.
    bool RigEditor::Bake(Scene &scene, std::string &error, std::string &outPath)
    {
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer || !m_model || !m_rootNode || !scene.IsNodeAlive(m_rootNode))
        {
            error = "no target model: select a node of a .pemesh model first";
            return false;
        }
        if (m_bones.empty())
        {
            error = "no bones to bake";
            return false;
        }
        if (m_model->GetFilePath().empty())
        {
            error = "the model has no file path to bake beside";
            return false;
        }
        const int nodeCount = m_model->GetNodeCount();
        const int meshCount = m_model->GetMeshInfoCount();
        const int rootIndex = m_model->GetRootNodeIndex();
        const int boneCount = static_cast<int>(m_bones.size());

        // heat colours live in the scene copy that is about to be replaced
        m_heat = 0;
        RestoreHeatMap(scene);

        // rig-space matrix of every node BEFORE any local is reset
        std::vector<mat4> toRig(nodeCount);
        for (int n = 0; n < nodeCount; n++)
            toRig[n] = ModelNodeWorld(n);

        std::vector<int> meshNode(meshCount, -1);
        for (int n = 0; n < nodeCount; n++)
        {
            const int mi = m_model->GetNodeMesh(n);
            if (mi < 0 || mi >= meshCount)
                continue;
            if (meshNode[mi] >= 0)
            {
                error = "mesh shared by nodes '" + m_model->GetNodeName(meshNode[mi]) + "' and '" + m_model->GetNodeName(n) +
                        "' cannot be flattened into one frame";
                return false;
            }
            meshNode[mi] = n;
        }

        std::vector<Vertex> &verts = m_model->GetMutableVertices();
        std::vector<PositionUvVertex> &posUv = m_model->GetMutablePositionUvs();
        std::vector<AabbVertex> &aabbs = m_model->GetMutableAabbVertices();
        for (int mi = 0; mi < meshCount; mi++)
        {
            MeshInfo *mesh = m_model->GetMeshInfo(mi);
            const int n = meshNode[mi];
            if (!mesh || n < 0 || mesh->vertexOffset + mesh->verticesCount > verts.size())
                continue;
            const mat4 &m = toRig[n];
            const mat3 linear(m);
            const mat3 normalM = glm::transpose(glm::inverse(linear));
            const int owner = ShellOwner(m_model->GetNodeName(n));
            vec3 boxMin(std::numeric_limits<float>::max()), boxMax(-std::numeric_limits<float>::max());
            for (uint32_t v = 0; v < mesh->verticesCount; v++)
            {
                Vertex &vert = verts[mesh->vertexOffset + v];
                const vec3 p = vec3(m * vec4(vert.position[0], vert.position[1], vert.position[2], 1.f));
                vec3 nrm = normalM * vec3(vert.normals[0], vert.normals[1], vert.normals[2]);
                if (glm::length(nrm) > 1e-8f)
                    nrm = glm::normalize(nrm);
                vec3 tan = linear * vec3(vert.tangent[0], vert.tangent[1], vert.tangent[2]);
                if (glm::length(tan) > 1e-8f)
                    tan = glm::normalize(tan);
                for (int k = 0; k < 3; k++)
                {
                    vert.position[k] = p[k];
                    vert.normals[k] = nrm[k];
                    vert.tangent[k] = tan[k];
                }
                int joints[4];
                float weights[4];
                ComputeVertexWeights(p, owner, joints, weights);
                float sum = 0.f;
                for (int k = 0; k < 4; k++)
                {
                    joints[k] = std::clamp(joints[k], 0, boneCount - 1);
                    sum += weights[k]; // Catmull-Rom outer weights are negative on purpose (the strip ships them too)
                }
                if (sum <= 1e-6f) // never leave a skinned vertex with no bone: it would collapse to the origin
                {
                    joints[0] = owner >= 0 ? owner : 0;
                    weights[0] = 1.f;
                    weights[1] = weights[2] = weights[3] = 0.f;
                    sum = 1.f;
                }
                for (int k = 0; k < 4; k++)
                {
                    vert.joints[k] = static_cast<uint32_t>(joints[k]);
                    vert.weights[k] = weights[k] / sum;
                }
                // the depth/shadow stream is indexed like the PBR stream (Scene derives positionsOffset)
                const size_t pi = static_cast<size_t>(mesh->vertexOffset) + v;
                if (pi < posUv.size())
                {
                    PositionUvVertex &pv = posUv[pi];
                    for (int k = 0; k < 3; k++)
                        pv.position[k] = p[k];
                    for (int k = 0; k < 4; k++)
                    {
                        pv.joints[k] = vert.joints[k];
                        pv.weights[k] = vert.weights[k];
                    }
                }
                boxMin = glm::min(boxMin, p);
                boxMax = glm::max(boxMax, p);
            }
            if (mesh->verticesCount == 0)
                continue;
            mesh->boundingBox.min = boxMin;
            mesh->boundingBox.max = boxMax;
            mesh->skinned = true;
            if (mesh->aabbVertexOffset + 8 <= aabbs.size())
            {
                const vec3 c[8] = {{boxMin.x, boxMin.y, boxMin.z}, {boxMax.x, boxMin.y, boxMin.z}, {boxMax.x, boxMax.y, boxMin.z}, {boxMin.x, boxMax.y, boxMin.z}, {boxMin.x, boxMin.y, boxMax.z}, {boxMax.x, boxMin.y, boxMax.z}, {boxMax.x, boxMax.y, boxMax.z}, {boxMin.x, boxMax.y, boxMax.z}};
                for (int i = 0; i < 8; i++)
                    for (int k = 0; k < 3; k++)
                        aabbs[mesh->aabbVertexOffset + i].position[k] = c[i][k];
            }
        }
        // the part transforms now live in the vertices; the root keeps its own frame (= rig space)
        for (int n = 0; n < nodeCount; n++)
            if (n != rootIndex)
                m_model->SetNodeLocalMatrix(n, mat4(1.f));

        std::vector<std::string> previousBones;
        for (const BoneInfo &bone : m_model->GetSkeleton().bones)
            previousBones.push_back(bone.name);
        Skeleton &skeleton = m_model->GetMutableSkeleton();
        skeleton = Skeleton{};
        skeleton.bones.reserve(m_bones.size());
        std::vector<mat4> restGlobal(boneCount);
        for (int i = 0; i < boneCount; i++)
        {
            const RigBone &b = m_bones[i];
            vec3 y = b.tail - b.head;
            y = glm::length(y) > 1e-6f ? glm::normalize(y) : vec3(0.f, 1.f, 0.f);
            vec3 x = vec3(1.f, 0.f, 0.f) - y * y.x;
            if (glm::length(x) < 1e-3f)
                x = glm::cross(y, vec3(0.f, 0.f, 1.f));
            x = glm::normalize(x);
            const vec3 z = glm::cross(x, y);
            restGlobal[i] = glm::translate(mat4(1.f), b.head) * mat4(mat3(x, y, z));
        }
        for (int i = 0; i < boneCount; i++)
        {
            const RigBone &b = m_bones[i];
            BoneInfo bone{};
            bone.name = b.name;
            bone.parentIndex = b.parent;
            bone.localBindTransform = (b.parent >= 0 ? glm::inverse(restGlobal[b.parent]) : mat4(1.f)) * restGlobal[i];
            bone.offsetMatrix = glm::inverse(restGlobal[i]);
            bone.intermediatePrefix = mat4(1.f);
            skeleton.boneNameToIndex[bone.name] = i;
            skeleton.bones.push_back(bone);
        }
        skeleton.rootTransform = mat4(1.f);

        // Clip channels reference bones by index: clips authored for a different skeleton would read
        // garbage bones on the new one (exploded poses). The old list staying a prefix of the new one
        // (a re-bake, or appended bones) keeps every old index meaning the same joint, so clips survive.
        std::vector<std::string> newBones;
        for (const BoneInfo &bone : skeleton.bones)
            newBones.push_back(bone.name);
        m_bakeNote.clear();
        const size_t shared = std::min(previousBones.size(), newBones.size());
        const bool sharedSame = std::equal(previousBones.begin(), previousBones.begin() + shared, newBones.begin());
        if (!m_model->GetAnimations().empty() && !sharedSame)
        {
            m_bakeNote = " Dropped " + std::to_string(m_model->GetAnimations().size()) +
                         " clip(s) authored for the previous skeleton.";
            m_model->GetMutableAnimations().clear();
        }
        else if (!m_model->GetAnimations().empty() && newBones.size() < previousBones.size())
        {
            // Shrunk skeleton, same leading bones: only the removed bones' channels are garbage.
            size_t stripped = 0;
            for (AnimationClip &animation : m_model->GetMutableAnimations())
                stripped += std::erase_if(animation.channels, [&](const AnimationChannel &channel)
                                          { return channel.boneIndex < 0 ||
                                                   channel.boneIndex >= static_cast<int>(newBones.size()); });
            if (stripped > 0)
                m_bakeNote = " Stripped " + std::to_string(stripped) + " channel(s) of removed bones.";
        }

        std::filesystem::path out = m_model->GetFilePath();
        std::string stem = out.stem().generic_string();
        const std::string suffix = "_rigged";
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
            stem.resize(stem.size() - suffix.size());
        out.replace_filename(stem + suffix + ".pemesh");
        const std::string document = DocumentJson();
        const bool written = ModelAssetCooked::WriteToFile(m_model, out);
        if (written)
        {
            std::filesystem::path json = out;
            json.replace_extension(".rig.json");
            std::ofstream f(json, std::ios::binary | std::ios::trunc);
            f << document;
        }

        // the in-memory asset is rewritten either way: drop it and let the scene swap decide the target
        ModelAsset *old = m_model;
        NodeId *oldRoot = m_rootNode;
        const mat4 oldLocal = scene.IsNodeAlive(oldRoot) ? scene.GetLocalMatrix(oldRoot) : mat4(1.f);
        m_model = nullptr;
        m_rootNode = nullptr;
        ClearDocument();
        if (AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr)
            timeline->DropTarget(); // the ModelAsset the Timeline resolved is about to be freed
        m_heatBackup.clear();
        BuildCaches();
        renderer->WaitAllFramesCommands();
        if (scene.IsNodeAlive(oldRoot))
            scene.DeleteNode(oldRoot); // the whole rig subtree, including empty group nodes a scene load left unregistered
        scene.RemoveModel(old);
        if (!written)
        {
            scene.UpdateGeometryBuffers();
            error = "failed to write " + out.generic_string();
            return false;
        }
        ModelAsset *rigged = ModelAsset::Load(out);
        if (!rigged)
        {
            scene.UpdateGeometryBuffers();
            error = "wrote " + out.generic_string() + " but could not load it back";
            return false;
        }
        const SceneNodeHandle handle = scene.AddModelDeferred(rigged);
        if (handle.nodeId)
            scene.SetLocalMatrix(handle.nodeId, oldLocal); // keep the placement the old root had in the scene
        scene.UpdateGeometryBuffers();
        scene.MarkDirty();
        renderer->ResetTAAHistory();
        if (handle.nodeId)
            SelectionManager::Instance().Select(handle.nodeId);
        outPath = out.generic_string();
        return true;
    }

    // -------------------------------------------------------------------------
    // caches, weights, mirror, snapping
    // -------------------------------------------------------------------------
    void RigEditor::BuildCaches()
    {
        m_rigVerts.clear();
        m_rigTris.clear();
        m_rigTriMesh.clear();
        m_shellCache.clear();
        if (!m_model)
            return;
        CollectShells(m_shellCache);
        const std::vector<Vertex> &vertices = m_model->GetVertices();
        const std::vector<uint32_t> &indices = m_model->GetIndices();
        m_rigVerts.assign(vertices.size(), vec3(0.f));
        for (int n = 0; n < m_model->GetNodeCount(); n++)
        {
            const int meshIndex = m_model->GetNodeMesh(n);
            const MeshInfo *mesh = meshIndex >= 0 ? m_model->GetMeshInfo(meshIndex) : nullptr;
            if (!mesh || mesh->vertexOffset + mesh->verticesCount > vertices.size() || mesh->indexOffset + mesh->indicesCount > indices.size())
                continue;
            const mat4 toRig = ModelNodeWorld(n);
            for (uint32_t v = 0; v < mesh->verticesCount; v++)
            {
                const float *pos = vertices[mesh->vertexOffset + v].position;
                m_rigVerts[mesh->vertexOffset + v] = vec3(toRig * vec4(pos[0], pos[1], pos[2], 1.f));
            }
            for (uint32_t k = 0; k + 2 < mesh->indicesCount; k += 3)
            {
                for (int c = 0; c < 3; c++)
                    m_rigTris.push_back(mesh->vertexOffset + indices[mesh->indexOffset + k + c]);
                m_rigTriMesh.push_back(meshIndex);
            }
        }
    }

    bool RigEditor::BuildPosedVertices(std::span<const mat4> boneTransforms, std::vector<vec3> &out) const
    {
        if (!m_model || boneTransforms.size() != static_cast<size_t>(m_model->GetSkeleton().GetBoneCount()) ||
            m_rigVerts.size() != m_model->GetVertices().size())
            return false;
        const Skeleton &skeleton = m_model->GetSkeleton();
        const std::vector<Vertex> &vertices = m_model->GetVertices();
        out.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            vec4 posed(0.f);
            float sum = 0.f;
            for (int k = 0; k < 4; k++)
            {
                const uint32_t joint = vertices[i].joints[k];
                const float weight = vertices[i].weights[k];
                if (joint >= boneTransforms.size() || !std::isfinite(weight))
                    continue;
                posed += weight * (boneTransforms[joint] * skeleton.bones[joint].offsetMatrix * vec4(m_rigVerts[i], 1.f));
                sum += weight;
            }
            out[i] = sum > 1e-6f ? vec3(posed) : m_rigVerts[i];
        }
        return true;
    }

    void RigEditor::ReadWeights(std::vector<RigWeightStroke::SkinWeight> &out) const
    {
        out.clear();
        if (!m_model)
            return;
        const std::vector<Vertex> &vertices = m_model->GetVertices();
        out.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
            for (int k = 0; k < 4; k++)
            {
                out[i].joints[k] = vertices[i].joints[k];
                out[i].weights[k] = vertices[i].weights[k];
            }
    }

    // ModelAsset streams are model-local; Scene stores are concatenated. Recover their shared base
    // from the public mesh offsets, then update the CPU copies without touching the GPU mid-stroke.
    bool RigEditor::WriteWeights(Scene &scene, std::span<const RigWeightStroke::SkinWeight> weights)
    {
        if (!m_model || weights.size() != m_model->GetVertices().size() ||
            weights.size() != m_model->GetPositionUvs().size())
            return false;

        std::vector<int> sceneMeshes;
        for (uint32_t n = 0; n < scene.GetNodeCount(); n++)
        {
            NodeId *node = scene.GetNodeId(n);
            if (scene.GetModelForNode(node) != m_model)
                continue;
            for (int ref : scene.GetMeshRefs(node))
                if (ref >= 0 && ref < static_cast<int>(scene.GetMeshes().size()) &&
                    std::find(sceneMeshes.begin(), sceneMeshes.end(), ref) == sceneMeshes.end())
                    sceneMeshes.push_back(ref);
        }

        size_t bestScore = 0;
        uint32_t vertexBase = 0, positionBase = 0;
        for (int ref : sceneMeshes)
        {
            const Mesh &sceneMesh = scene.GetMesh(ref);
            for (const MeshInfo &sourceMesh : m_model->GetMeshInfos())
            {
                if (sceneMesh.vertexCount != sourceMesh.verticesCount || sceneMesh.vertexOffset < sourceMesh.vertexOffset ||
                    sceneMesh.positionsOffset < sourceMesh.vertexOffset)
                    continue;
                const uint32_t candidateVertexBase = sceneMesh.vertexOffset - sourceMesh.vertexOffset;
                const uint32_t candidatePositionBase = sceneMesh.positionsOffset - sourceMesh.vertexOffset;
                size_t score = 0;
                for (const MeshInfo &candidate : m_model->GetMeshInfos())
                    for (int candidateRef : sceneMeshes)
                    {
                        const Mesh &other = scene.GetMesh(candidateRef);
                        if (other.vertexOffset == candidateVertexBase + candidate.vertexOffset &&
                            other.positionsOffset == candidatePositionBase + candidate.vertexOffset &&
                            other.vertexCount == candidate.verticesCount)
                        {
                            score++;
                            break;
                        }
                    }
                if (score > bestScore)
                    bestScore = score, vertexBase = candidateVertexBase, positionBase = candidatePositionBase;
            }
        }

        std::vector<Vertex> &sceneVertices = scene.GetVertexStore();
        std::vector<PositionUvVertex> &scenePositions = scene.GetPositionUvStore();
        if (bestScore == 0 || static_cast<size_t>(vertexBase) + weights.size() > sceneVertices.size() ||
            static_cast<size_t>(positionBase) + weights.size() > scenePositions.size())
            return false;

        std::vector<Vertex> &vertices = m_model->GetMutableVertices();
        std::vector<PositionUvVertex> &positions = m_model->GetMutablePositionUvs();
        for (size_t i = 0; i < weights.size(); i++)
            for (int k = 0; k < 4; k++)
            {
                vertices[i].joints[k] = weights[i].joints[k];
                vertices[i].weights[k] = weights[i].weights[k];
                positions[i].joints[k] = weights[i].joints[k];
                positions[i].weights[k] = weights[i].weights[k];
                sceneVertices[vertexBase + i].joints[k] = weights[i].joints[k];
                sceneVertices[vertexBase + i].weights[k] = weights[i].weights[k];
                scenePositions[positionBase + i].joints[k] = weights[i].joints[k];
                scenePositions[positionBase + i].weights[k] = weights[i].weights[k];
            }
        return true;
    }

    void RigEditor::UploadWeights(Scene &scene)
    {
        if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
        {
            renderer->WaitAllFramesCommands();
            scene.UpdateGeometryBuffers();
            renderer->ResetTAAHistory();
        }
    }

    void RigEditor::PushWeightUndo()
    {
        std::vector<RigWeightStroke::SkinWeight> snapshot;
        ReadWeights(snapshot);
        if (snapshot.empty())
            return;
        m_weightUndo.push_back(std::move(snapshot));
        if (static_cast<int>(m_weightUndo.size()) > kMaxWeightUndo)
            m_weightUndo.erase(m_weightUndo.begin());
        m_weightRedo.clear();
    }

    bool RigEditor::WeightUndo(Scene &scene, bool redo)
    {
        auto &source = redo ? m_weightRedo : m_weightUndo;
        auto &destination = redo ? m_weightUndo : m_weightRedo;
        if (source.empty())
            return false;
        std::vector<RigWeightStroke::SkinWeight> current;
        ReadWeights(current);
        if (!WriteWeights(scene, source.back()))
            return false;
        destination.push_back(std::move(current));
        if (static_cast<int>(destination.size()) > kMaxWeightUndo)
            destination.erase(destination.begin());
        source.pop_back();
        m_weightDirty = true;
        UploadWeights(scene);
        return true;
    }

    bool RigEditor::SaveWeights(std::string &error)
    {
        if (!m_model || !ModelAssetCooked::IsCookedPath(m_model->GetFilePath()))
        {
            error = "the current model is not a .pemesh";
            return false;
        }
        if (!ModelAssetCooked::WriteToFile(m_model, m_model->GetFilePath()))
        {
            error = "failed to write " + m_model->GetFilePath().generic_string();
            return false;
        }
        m_weightDirty = false;
        return true;
    }

    std::string RigEditor::ShellsJson() const
    {
        nlohmann::json shells = nlohmann::json::array();
        for (const ShellInfo &sh : m_shellCache)
        {
            const vec3 a = sh.centroid - sh.axis * sh.halfLength, c = sh.centroid + sh.axis * sh.halfLength;
            shells.push_back({{"name", sh.name},
                              {"centroid", {sh.centroid.x, sh.centroid.y, sh.centroid.z}},
                              {"axis_start", {a.x, a.y, a.z}},
                              {"axis_end", {c.x, c.y, c.z}},
                              {"radius", sh.radius},
                              {"aabb_min", {sh.aabbMin.x, sh.aabbMin.y, sh.aabbMin.z}},
                              {"aabb_max", {sh.aabbMax.x, sh.aabbMax.y, sh.aabbMax.z}},
                              {"vertices", sh.points.size()}});
        }
        return shells.dump();
    }

    // Influence of one capsule at a rig-space point: 1 on the axis, smooth falloff to 0 at the surface.
    // signedDistance < 0 means inside.
    float RigEditor::CapsuleInfluence(const RigBone &b, const vec3 &p, float &signedDistance)
    {
        const vec3 ab = b.tail - b.head;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-12f ? std::clamp(glm::dot(p - b.head, ab) / len2, 0.f, 1.f) : 0.f;
        const float r = std::max(glm::mix(b.headRadius, b.tailRadius, t), kMinRadius);
        const float d = glm::length(p - (b.head + ab * t));
        signedDistance = d - r;
        if (d >= r)
            return 0.f;
        const float x = 1.f - d / r;
        return x * x * (3.f - 2.f * x);
    }

    int RigEditor::ShellOwner(const std::string &nodeName) const
    {
        if (nodeName.empty())
            return -1;
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
            if (m_bones[i].shell == nodeName)
                return i;
        return -1;
    }

    // Joints/weights for one rig-space point (the bake writes exactly this into the vertices): a part
    // owned by a bone (`owner` = ShellOwner of the vertex's node) belongs to it outright; otherwise a
    // rigid capsule claims the point (deepest wins), soft capsules blend and normalise to <= 4
    // influences, and a point outside every capsule follows the nearest one.
    // The maximal run of spline bones through `bone`: up to the first link, then down the first spline child of each link.
    void RigEditor::ChainOf(int bone, std::vector<int> &chain) const
    {
        chain.clear();
        int first = bone;
        while (m_bones[first].parent >= 0 && m_bones[m_bones[first].parent].spline)
            first = m_bones[first].parent;
        for (int cur = first; cur >= 0;)
        {
            chain.push_back(cur);
            int next = -1;
            for (int i = 0; i < static_cast<int>(m_bones.size()) && next < 0; i++)
                if (m_bones[i].parent == cur && m_bones[i].spline)
                    next = i;
            cur = next;
        }
    }

    // Catmull-Rom curve-station weights (the skinned_strip_2d rule): stations are the chain heads plus the last
    // tail; the closest polyline point gives the segment and t, the 4 surrounding joints get the cubic basis.
    // ponytail: joint frames are the plain FK rotations; add 3D tangent-frame smoothing in AnimationSystem
    // (the strip's WriteSmoothStripJointMatrices, lifted to 3D) if a concave bend looks chunky.
    void RigEditor::ChainWeights(int bone, const vec3 &p, int joints[4], float weights[4]) const
    {
        std::vector<int> chain;
        ChainOf(bone, chain);
        const int n = static_cast<int>(chain.size());
        std::vector<vec3> stations(n + 1);
        for (int i = 0; i < n; i++)
            stations[i] = m_bones[chain[i]].head;
        stations[n] = m_bones[chain[n - 1]].tail;
        int seg = 0;
        float t = 0.f, best = std::numeric_limits<float>::max();
        for (int s = 0; s < n; s++)
        {
            const vec3 d = stations[s + 1] - stations[s];
            const float len2 = glm::dot(d, d);
            const float u = len2 > 1e-10f ? std::clamp(glm::dot(p - stations[s], d) / len2, 0.f, 1.f) : 0.f;
            const float dist = glm::length(p - (stations[s] + d * u));
            if (dist < best)
            {
                best = dist;
                seg = s;
                t = u;
            }
        }
        const float t2 = t * t, t3 = t2 * t;
        const int idx[4] = {std::max(seg - 1, 0), seg, std::min(seg + 1, n - 1), std::min(seg + 2, n - 1)};
        const float w[4] = {-0.5f * t + t2 - 0.5f * t3, 1.f - 2.5f * t2 + 1.5f * t3, 0.5f * t + 2.f * t2 - 1.5f * t3,
                            -0.5f * t2 + 0.5f * t3};
        for (int k = 0; k < 4; k++)
        {
            joints[k] = chain[idx[k]];
            weights[k] = w[k];
        }
    }

    int RigEditor::MakeChain(int index, int count)
    {
        count = std::clamp(count, 2, 32);
        const RigBone src = m_bones[index];
        std::vector<int> children;
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
            if (m_bones[i].parent == index)
                children.push_back(i);
        auto at = [&](int k) -> vec3
        { return glm::mix(src.head, src.tail, static_cast<float>(k) / count); };
        auto radiusAt = [&](int k) -> float
        { return glm::mix(src.headRadius, src.tailRadius, static_cast<float>(k) / count); };
        m_bones[index].tail = at(1);
        m_bones[index].tailRadius = radiusAt(1);
        m_bones[index].spline = true;
        m_bones[index].rigid = false;
        int prev = index;
        for (int k = 1; k < count; k++)
        {
            const int nb = AddBone(src.name, prev);
            RigBone &b = m_bones[nb];
            b.head = at(k);
            b.tail = at(k + 1);
            b.headRadius = radiusAt(k);
            b.tailRadius = radiusAt(k + 1);
            b.spline = true;
            b.shell = src.shell;
            prev = nb;
        }
        for (int c : children)
            SetParent(c, prev);
        m_dirty = true;
        return prev;
    }

    void RigEditor::ComputeVertexWeights(const vec3 &p, int owner, int joints[4], float weights[4]) const
    {
        for (int k = 0; k < 4; k++)
        {
            joints[k] = 0;
            weights[k] = 0.f;
        }
        if (m_bones.empty())
            return;
        if (owner >= 0 && owner < static_cast<int>(m_bones.size()))
        {
            if (m_bones[owner].spline)
            {
                ChainWeights(owner, p, joints, weights);
                return;
            }
            joints[0] = owner;
            weights[0] = 1.f;
            return;
        }
        int rigidBest = -1, nearest = 0;
        float rigidDepth = 0.f, nearestDist = std::numeric_limits<float>::max();
        int count = 0;
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            float sd;
            const float w = CapsuleInfluence(m_bones[i], p, sd);
            if (sd < nearestDist)
            {
                nearestDist = sd;
                nearest = i;
            }
            if (w <= 0.f)
                continue;
            if (m_bones[i].rigid)
            {
                if (w > rigidDepth)
                {
                    rigidDepth = w;
                    rigidBest = i;
                }
                continue;
            }
            // keep the 4 strongest soft influences
            int slot = count < 4 ? count++ : -1;
            if (slot < 0)
            {
                slot = 0;
                for (int k = 1; k < 4; k++)
                    if (weights[k] < weights[slot])
                        slot = k;
                if (weights[slot] >= w)
                    continue;
            }
            joints[slot] = i;
            weights[slot] = w;
        }
        if (rigidBest >= 0 || count == 0)
        {
            for (int k = 0; k < 4; k++)
            {
                joints[k] = 0;
                weights[k] = 0.f;
            }
            joints[0] = rigidBest >= 0 ? rigidBest : nearest;
            weights[0] = 1.f;
            if (m_bones[joints[0]].spline)
                ChainWeights(joints[0], p, joints, weights);
            return;
        }
        int strongest = 0;
        float sum = 0.f;
        for (int k = 0; k < 4; k++)
        {
            sum += weights[k];
            if (weights[k] > weights[strongest])
                strongest = k;
        }
        // ponytail: a chain claims the vertex outright; blend the chain root into neighbouring body bones if a
        // tail/cape junction needs a soft seam.
        if (m_bones[joints[strongest]].spline)
        {
            ChainWeights(joints[strongest], p, joints, weights);
            return;
        }
        for (int k = 0; k < 4; k++)
            weights[k] /= sum;
    }

    ImU32 RigEditor::BoneColor(int index)
    {
        // Golden-ratio hue walk: neighbouring bones get clearly different colours.
        const float h = std::fmod(0.12f + static_cast<float>(index) * 0.618034f, 1.f) * 6.f;
        const float x = 1.f - std::abs(std::fmod(h, 2.f) - 1.f);
        float r = 0.f, g = 0.f, b = 0.f;
        switch (static_cast<int>(h))
        {
        case 0:
            r = 1.f, g = x;
            break;
        case 1:
            r = x, g = 1.f;
            break;
        case 2:
            g = 1.f, b = x;
            break;
        case 3:
            g = x, b = 1.f;
            break;
        case 4:
            r = x, b = 1.f;
            break;
        default:
            r = 1.f, b = x;
            break;
        }
        return IM_COL32(static_cast<int>(60 + 195 * r), static_cast<int>(60 + 195 * g), static_cast<int>(60 + 195 * b), 255);
    }

    // Weight heat map: recolours the scene's vertices under the rig root through the GBuffer vertex
    // colour (base colour * vertex colour), keeping the originals so Off restores them.
    // ponytail: full geometry re-upload per refresh (editor-only, debounced to drag end); a vertex
    // sub-range upload is the upgrade if big scenes make it stall.
    void RigEditor::UpdateHeatMap(Scene &scene)
    {
        m_heatDirty = false;
        m_heatSelected = m_selected;
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        std::vector<Vertex> &store = scene.GetVertexStore();
        if (!renderer)
            return;
        if (m_heat == 0 || !m_model || !m_rootNode || !scene.IsNodeAlive(m_rootNode))
        {
            RestoreHeatMap(scene);
            return;
        }

        const bool firstPass = m_heatBackup.empty();
        const mat4 invRoot = glm::inverse(scene.GetWorldMatrix(m_rootNode));
        const vec3 orange(1.f, 0.55f, 0.15f), grey(0.55f);
        std::vector<NodeId *> stack{m_rootNode};
        while (!stack.empty())
        {
            NodeId *node = stack.back();
            stack.pop_back();
            if (!node || !scene.IsNodeAlive(node))
                continue;
            for (NodeId *child : scene.GetChildren(node))
                stack.push_back(child);
            const mat4 toRig = invRoot * scene.GetWorldMatrix(node);
            const int owner = ShellOwner(scene.GetNodeName(node));
            for (int ref : scene.GetMeshRefs(node))
            {
                if (ref < 0 || ref >= static_cast<int>(scene.GetMeshes().size()))
                    continue;
                const Mesh &mesh = scene.GetMesh(ref);
                for (uint32_t v = mesh.vertexOffset; v < mesh.vertexOffset + mesh.vertexCount && v < store.size(); v++)
                {
                    Vertex &vert = store[v];
                    if (firstPass)
                        m_heatBackup.emplace_back(v, vec4(vert.color[0], vert.color[1], vert.color[2], vert.color[3]));
                    const vec3 p = vec3(toRig * vec4(vert.position[0], vert.position[1], vert.position[2], 1.f));
                    int joints[4];
                    float weights[4];
                    ComputeVertexWeights(p, owner, joints, weights);
                    vec3 c(0.f);
                    if (m_heat == 1)
                    {
                        float w = 0.f;
                        for (int k = 0; k < 4; k++)
                            if (joints[k] == m_selected)
                                w += std::max(weights[k], 0.f);
                        c = glm::mix(grey, orange, std::clamp(w, 0.f, 1.f));
                    }
                    else
                    {
                        for (int k = 0; k < 4; k++)
                        {
                            const ImU32 col = BoneColor(joints[k]);
                            c += std::max(weights[k], 0.f) * vec3((col & 0xFF) / 255.f, ((col >> 8) & 0xFF) / 255.f, ((col >> 16) & 0xFF) / 255.f);
                        }
                    }
                    vert.color[0] = c.x;
                    vert.color[1] = c.y;
                    vert.color[2] = c.z;
                }
            }
        }
        renderer->WaitAllFramesCommands();
        scene.UpdateGeometryBuffers();
    }

    void RigEditor::RestoreHeatMap(Scene &scene)
    {
        if (m_heatBackup.empty())
            return;
        std::vector<Vertex> &store = scene.GetVertexStore();
        for (const auto &[index, color] : m_heatBackup)
            if (index < store.size())
                std::memcpy(store[index].color, &color.x, sizeof(float) * 4);
        m_heatBackup.clear();
        if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
        {
            renderer->WaitAllFramesCommands();
            scene.UpdateGeometryBuffers();
        }
    }

    std::string RigEditor::MirrorName(const std::string &name)
    {
        static const char *pairs[][2] = {{".L", ".R"}, {".R", ".L"}, {"_L", "_R"}, {"_R", "_L"}, {".l", ".r"}, {".r", ".l"}};
        for (const auto &pair : pairs)
        {
            const std::string suffix = pair[0];
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                return name.substr(0, name.size() - suffix.size()) + pair[1];
        }
        return {};
    }

    int RigEditor::MirrorCounterpart(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_bones.size()))
            return -1;
        const std::string other = MirrorName(m_bones[index].name);
        const int found = other.empty() ? -1 : FindBone(other);
        return found == index ? -1 : found;
    }

    // Blender's X-Axis Mirror on add/extrude: with Mirror X on, a new bone comes as a .L/.R pair — the
    // side from the head's X (+X = .L on this rig), the twin under the parent's own twin when it has
    // one, placed by SyncMirror. A bone that already has its twin just gets added.
    int RigEditor::AddBonePair(const std::string &name, int parent)
    {
        if (!m_mirrorX)
            return AddBone(name, parent);
        std::string base = name.empty() ? "bone" : name;
        const std::string swapped = MirrorName(base);
        if (swapped.empty())
        {
            // no side suffix yet: pick a free base so both sides get clean names
            const int probe = AddBone(base, parent);
            const bool right = m_bones[probe].head.x < 0.f;
            m_bones.pop_back();
            std::string stem = base;
            for (int n = 1; FindBone(stem + ".L") >= 0 || FindBone(stem + ".R") >= 0; n++)
                stem = base + "." + std::string(n < 100 ? (n < 10 ? "00" : "0") : "") + std::to_string(n);
            base = stem + (right ? ".R" : ".L");
        }
        const int bone = AddBone(base, parent);
        if (MirrorCounterpart(bone) >= 0)
            return bone;
        const int twinParent = MirrorCounterpart(parent) >= 0 ? MirrorCounterpart(parent) : parent;
        AddBone(MirrorName(m_bones[bone].name), twinParent);
        SyncMirror(bone);
        return bone;
    }

    void RigEditor::SyncMirror(int index)
    {
        const int other = MirrorCounterpart(index);
        if (other < 0)
            return;
        const RigBone src = m_bones[index]; // copy: MoveTail may touch other bones
        RigBone &dst = m_bones[other];
        dst.head = vec3(-src.head.x, src.head.y, src.head.z);
        dst.headRadius = src.headRadius;
        dst.tailRadius = src.tailRadius;
        dst.rigid = src.rigid;
        MoveTail(other, vec3(-src.tail.x, src.tail.y, src.tail.z)); // the twin's connected child follows too
    }

    // Ray vs every model triangle in rig space: entry = nearest hit, exit = the next hit of the SAME
    // shell behind it (an arm in front of the body must not pair with the body's front face).
    bool RigEditor::RayModel(const vec3 &origin, const vec3 &dir, float &tEnter, float &tExit) const
    {
        tEnter = tExit = std::numeric_limits<float>::max();
        int enterMesh = -1;
        std::vector<std::pair<float, int>> hits;
        for (size_t k = 0; k + 2 < m_rigTris.size(); k += 3)
        {
            const vec3 &a = m_rigVerts[m_rigTris[k]], &b = m_rigVerts[m_rigTris[k + 1]], &c = m_rigVerts[m_rigTris[k + 2]];
            const vec3 e1 = b - a, e2 = c - a, pv = glm::cross(dir, e2);
            const float det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-9f)
                continue;
            const float inv = 1.f / det;
            const vec3 tv = origin - a;
            const float u = glm::dot(tv, pv) * inv;
            if (u < 0.f || u > 1.f)
                continue;
            const vec3 qv = glm::cross(tv, e1);
            const float v = glm::dot(dir, qv) * inv;
            if (v < 0.f || u + v > 1.f)
                continue;
            const float t = glm::dot(e2, qv) * inv;
            if (t <= 1e-5f)
                continue;
            const int mesh = m_rigTriMesh[k / 3];
            hits.emplace_back(t, mesh);
            if (t < tEnter)
            {
                tEnter = t;
                enterMesh = mesh;
            }
        }
        for (const auto &[t, mesh] : hits)
            if (mesh == enterMesh && t > tEnter && t < tExit)
                tExit = t;
        return tEnter < std::numeric_limits<float>::max();
    }

    // -------------------------------------------------------------------------
    // viewport overlay (bones as head->tail lines, capsule outlines, drag handles)
    // -------------------------------------------------------------------------
    void RigEditor::DrawJointBlendViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                           const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                                           const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered,
                                           bool &active)
    {
        std::vector<vec3> posedVertices;
        std::vector<RigWeightStroke::SkinWeight> weights;
        if (!BuildPosedVertices(boneTransforms, posedVertices))
            return;
        ReadWeights(weights);
        const Skeleton &skeleton = m_model->GetSkeleton();
        const int selectedBone = SelectedSkeletonBone();
        if (selectedBone < 0 || selectedBone >= skeleton.GetBoneCount())
            return;

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const size_t stride = std::max<size_t>(1, posedVertices.size() / 12000);
        const vec3 grey(0.35f), orange(1.f, 0.55f, 0.1f), purple(0.55f, 0.2f, 0.9f);
        for (size_t i = 0; i < posedVertices.size(); i += stride)
        {
            float weight = 0.f;
            for (int k = 0; k < 4; k++)
                if (weights[i].joints[k] == static_cast<uint32_t>(selectedBone))
                    weight += weights[i].weights[k];
            ImVec2 screen;
            if (!project(posedVertices[i], screen))
                continue;
            const vec3 color = weight < 0.f ? glm::mix(grey, purple, std::clamp(-weight, 0.f, 1.f))
                                            : glm::mix(grey, orange, std::clamp(weight, 0.f, 1.f));
            drawList->AddCircleFilled(screen, 1.6f,
                                      IM_COL32(static_cast<int>(color.r * 255.f), static_cast<int>(color.g * 255.f),
                                               static_cast<int>(color.b * 255.f), 190),
                                      6);
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const bool mouseInImage = mouse.x >= imageMin.x && mouse.y >= imageMin.y && mouse.x <= imageMin.x + imageSize.x &&
                                  mouse.y <= imageMin.y + imageSize.y;
        vec3 rayOrigin(0.f), rayDirection(0.f);
        const bool haveRay = mouseInImage && imageSize.x > 0.f && imageSize.y > 0.f &&
                             camera->BuildWorldRayFromNdc((mouse.x - imageMin.x) / imageSize.x * 2.f - 1.f,
                                                          (mouse.y - imageMin.y) / imageSize.y * 2.f - 1.f, rayOrigin,
                                                          rayDirection);
        vec3 hit(0.f);
        bool haveHit = false;
        if (haveRay)
        {
            const mat4 invRootWorld = glm::inverse(rootWorld);
            const vec3 origin = vec3(invRootWorld * vec4(rayOrigin, 1.f));
            const vec3 direction = glm::normalize(vec3(invRootWorld * vec4(rayDirection, 0.f)));
            float nearest = std::numeric_limits<float>::max();
            for (size_t triangle = 0; triangle + 2 < m_rigTris.size(); triangle += 3)
            {
                const uint32_t ia = m_rigTris[triangle], ib = m_rigTris[triangle + 1], ic = m_rigTris[triangle + 2];
                if (ia >= posedVertices.size() || ib >= posedVertices.size() || ic >= posedVertices.size())
                    continue;
                const vec3 &a = posedVertices[ia], &b = posedVertices[ib], &c = posedVertices[ic];
                const vec3 edge1 = b - a, edge2 = c - a, p = glm::cross(direction, edge2);
                const float determinant = glm::dot(edge1, p);
                if (std::abs(determinant) < 1e-9f)
                    continue;
                const float inverse = 1.f / determinant;
                const vec3 fromA = origin - a;
                const float u = glm::dot(fromA, p) * inverse;
                if (u < 0.f || u > 1.f)
                    continue;
                const vec3 q = glm::cross(fromA, edge1);
                const float v = glm::dot(direction, q) * inverse;
                if (v < 0.f || u + v > 1.f)
                    continue;
                const float distance = glm::dot(edge2, q) * inverse;
                if (distance > 1e-5f && distance < nearest)
                    nearest = distance, hit = origin + direction * distance, haveHit = true;
            }
        }

        if (haveHit)
        {
            // Brush feel: wheel resizes the brush under the cursor, Shift+wheel adjusts strength
            // (the editor fly camera does not use the wheel, so it is free here).
            if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.f)
            {
                if (ImGui::GetIO().KeyShift)
                    m_weightStrength = std::clamp(m_weightStrength + wheel * 0.05f, 0.f, 1.f);
                else
                    m_weightRadius = std::clamp(m_weightRadius * std::pow(1.15f, wheel), kMinRadius,
                                                std::max(ModelHeight(), kMinRadius));
                m_status = "Brush radius " + std::to_string(m_weightRadius).substr(0, 5) + ", strength " +
                           std::to_string(m_weightStrength).substr(0, 4);
            }
            ImVec2 center, edge;
            if (project(hit, center))
            {
                const mat4 invRootWorld = glm::inverse(rootWorld);
                const vec3 cameraRight = vec3(glm::inverse(camera->GetView())[0]);
                const vec3 rigRight = glm::normalize(vec3(invRootWorld * vec4(cameraRight, 0.f)));
                const float screenRadius = project(hit + rigRight * m_weightRadius, edge) ? glm::length(vec2(edge.x - center.x, edge.y - center.y)) : 8.f;
                drawList->AddCircle(center, std::max(screenRadius, 3.f), IM_COL32(255, 220, 110, 245), 40, 2.f);
            }
            hovered = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_weightDragging = true;
                m_weightStrokeChanged = false;
                m_weightHasLastCenter = false;
            }
            if (m_weightDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                (!m_weightHasLastCenter || glm::length(hit - m_weightLastCenter) >= m_weightRadius * 0.08f))
            {
                RigWeightStroke::Stroke stroke;
                stroke.bone = skeleton.bones[selectedBone].name;
                stroke.center = hit;
                stroke.radius = m_weightRadius;
                stroke.strength = m_weightStrength;
                stroke.mode = m_weightMode;
                const RigWeightStroke::Result result =
                    RigWeightStroke::Apply(m_rigVerts, posedVertices, weights, skeleton, stroke, m_weightScratch);
                if (result && result.affectedVertices > 0)
                {
                    if (!m_weightStrokeChanged)
                        PushWeightUndo();
                    if (WriteWeights(scene, m_weightScratch))
                    {
                        m_weightStrokeChanged = true;
                        m_weightDirty = true;
                        m_weightLastCenter = hit;
                        m_weightHasLastCenter = true;
                        m_status = "Joint Blend changed " + std::to_string(result.affectedVertices) + " vertices";
                        if (result.skippedSplineVertices > 0)
                            m_status += "; skipped " + std::to_string(result.skippedSplineVertices) + " spline vertices";
                    }
                    else
                    {
                        if (!m_weightStrokeChanged && !m_weightUndo.empty())
                            m_weightUndo.pop_back();
                        m_status = "Could not map model weights into the Scene stores.";
                    }
                }
                else if (result && result.skippedSplineVertices > 0)
                    m_status = "Skipped " + std::to_string(result.skippedSplineVertices) +
                               " spline vertices; their negative Catmull-Rom weights are protected.";
            }
        }

        if (m_weightDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_weightStrokeChanged)
                UploadWeights(scene);
            m_weightDragging = false;
            m_weightStrokeChanged = false;
            m_weightHasLastCenter = false;
        }
        active = m_weightDragging;
    }

    void RigEditor::DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                                 bool &active)
    {
        hovered = false;
        active = false;
        ResolveTarget(scene);
        if (!m_model || !m_rootNode || !camera || !scene.IsNodeAlive(m_rootNode))
            return;
        if (m_bones.empty())
            return;
        // Selecting a bone here (or in the tree) also makes it the Timeline's active bone, so the pose bar
        // edits the bone under the cursor.
        if (m_selected != m_syncedSelected)
        {
            m_syncedSelected = m_selected;
            if (m_selected >= 0 && m_selected < static_cast<int>(m_bones.size()) && m_gui)
                if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                    timeline->RequestBone(m_bones[m_selected].name);
        }

        AnimationTimeline::ViewportPose viewportPose;
        NodeId *viewportRoot = m_rootNode;
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline || !timeline->GetViewportPose(m_model, viewportPose))
        {
            viewportPose.node = m_rootNode;
            if (m_model->HasSkeleton())
                viewportPose.boneTransforms = RestBoneTransforms(m_model->GetSkeleton());
        }
        if (viewportPose.node && scene.IsNodeAlive(viewportPose.node))
            viewportRoot = viewportPose.node;
        const mat4 rootWorld = scene.GetWorldMatrix(viewportRoot);
        const mat4 invRootWorld = glm::inverse(rootWorld);
        const float rigScale = std::max(glm::length(vec3(rootWorld[0])), 1e-5f);
        const mat4 viewProj = camera->GetProjectionNoJitter() * camera->GetView();
        const mat4 invView = glm::inverse(camera->GetView());
        const vec3 camRight = glm::normalize(vec3(invView[0]));
        const vec3 camFront = camera->GetFront();
        const ImGuiIO &io = ImGui::GetIO();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        auto project = [&](const vec3 &rig, ImVec2 &out) -> bool
        {
            const vec3 world = vec3(rootWorld * vec4(rig, 1.f));
            return ProjectWorldToViewportRect(world, viewProj, imageMin.x, imageMin.y, imageSize.x, imageSize.y, out.x, out.y);
        };
        if (m_jointBlend)
        {
            DrawJointBlendViewport(scene, camera, imageMin, imageSize, rootWorld, viewportPose.boneTransforms, project,
                                   hovered, active);
            return;
        }
        const bool haveSkeleton = m_model->HasSkeleton();
        const std::vector<mat4> restXforms = haveSkeleton ? RestBoneTransforms(m_model->GetSkeleton()) : std::vector<mat4>{};
        auto skinned = [&](int bone, const vec3 &bindPt) -> vec3
        {
            if (!haveSkeleton || bone < 0 || bone >= static_cast<int>(m_bones.size()))
                return bindPt;
            const int si = m_model->GetSkeleton().GetBoneIndex(m_bones[bone].name);
            if (si < 0 || si >= static_cast<int>(viewportPose.boneTransforms.size()) || si >= static_cast<int>(restXforms.size()))
                return bindPt;
            return SkinRigPoint(viewportPose.boneTransforms[si], restXforms[si], bindPt);
        };
        auto unskinned = [&](int bone, const vec3 &posedPt) -> vec3
        {
            if (!haveSkeleton || bone < 0 || bone >= static_cast<int>(m_bones.size()))
                return posedPt;
            const int si = m_model->GetSkeleton().GetBoneIndex(m_bones[bone].name);
            if (si < 0 || si >= static_cast<int>(viewportPose.boneTransforms.size()) || si >= static_cast<int>(restXforms.size()))
                return posedPt;
            return UnskinRigPoint(viewportPose.boneTransforms[si], restXforms[si], posedPt);
        };
        const int nBones = static_cast<int>(m_bones.size());
        std::vector<vec3> dispHead(nBones), dispTail(nBones);
        for (int i = 0; i < nBones; i++)
        {
            dispHead[i] = skinned(i, m_bones[i].head);
            dispTail[i] = skinned(i, m_bones[i].tail);
        }
        auto screenRadius = [&](const vec3 &rig, float radius, const ImVec2 &center) -> float
        {
            const vec3 world = vec3(rootWorld * vec4(rig, 1.f)) + camRight * (radius * rigScale);
            ImVec2 edge;
            if (!ProjectWorldToViewportRect(world, viewProj, imageMin.x, imageMin.y, imageSize.x, imageSize.y, edge.x, edge.y))
                return 0.f;
            return std::abs(edge.x - center.x);
        };

        // pass 1: shapes + bones (skinned to the Timeline pose so capsules sit on the posed mesh)
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            const RigBone &b = m_bones[i];
            ImVec2 hs, ts;
            if (!project(dispHead[i], hs) || !project(dispTail[i], ts))
                continue;
            const bool selected = i == m_selected;
            const ImU32 boneCol = m_heat == 2 ? BoneColor(i) : selected ? kBoneSelCol
                                                                        : (b.spline ? kSplineCol : b.rigid ? kRigidCol
                                                                                                           : kBoneCol);
            if (m_showShapes && b.shell.empty())
            {
                const float rh = screenRadius(dispHead[i], b.headRadius, hs), rt = screenRadius(dispTail[i], b.tailRadius, ts);
                const ImU32 col = m_heat == 2 ? (boneCol & 0x00FFFFFF) | (selected ? 0x90000000 : 0x50000000) : selected ? kShapeSelCol
                                                                                                                         : kShapeCol;
                dl->AddCircle(hs, rh, col, 32, selected ? 2.5f : 1.f);
                dl->AddCircle(ts, rt, col, 32, selected ? 2.5f : 1.f);
                const ImVec2 d(ts.x - hs.x, ts.y - hs.y);
                const float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len > 1.f)
                {
                    const ImVec2 n(-d.y / len, d.x / len);
                    dl->AddLine({hs.x + n.x * rh, hs.y + n.y * rh}, {ts.x + n.x * rt, ts.y + n.y * rt}, col, selected ? 2.5f : 1.f);
                    dl->AddLine({hs.x - n.x * rh, hs.y - n.y * rh}, {ts.x - n.x * rt, ts.y - n.y * rt}, col, selected ? 2.5f : 1.f);
                }
            }
            dl->AddLine(hs, ts, IM_COL32(0, 0, 0, 160), selected ? 7.f : 3.f);
            dl->AddLine(hs, ts, boneCol, selected ? 5.f : 1.5f);
            dl->AddCircleFilled(hs, selected ? 6.f : 4.f, boneCol, 16);
            dl->AddCircle(hs, selected ? 5.f : 4.f, selected ? IM_COL32(255, 255, 255, 230) : IM_COL32(0, 0, 0, 200), 16, selected ? 1.5f : 1.f);
            dl->AddCircleFilled(ts, 3.f, boneCol, 12);
            if (selected)
                dl->AddText({hs.x + 8.f, hs.y - 16.f}, kBoneSelCol, b.name.c_str());
        }

        // spline chains: the Catmull-Rom curve through each chain's stations, so the bend the weights follow is visible
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            const RigBone &b = m_bones[i];
            if (!b.spline || (b.parent >= 0 && m_bones[b.parent].spline))
                continue;
            std::vector<int> chain;
            ChainOf(i, chain);
            const int n = static_cast<int>(chain.size());
            std::vector<vec3> st(n + 1);
            for (int k = 0; k < n; k++)
                st[k] = dispHead[chain[k]];
            st[n] = dispTail[chain[n - 1]];
            std::vector<ImVec2> pts;
            for (int s = 0; s < n; s++)
            {
                const vec3 &p0 = st[std::max(s - 1, 0)], &p1 = st[s], &p2 = st[s + 1], &p3 = st[std::min(s + 2, n)];
                for (int j = 0; j <= 8; j++)
                {
                    const float t = j / 8.f, t2 = t * t, t3 = t2 * t;
                    const vec3 c = 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                                           (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
                    ImVec2 q;
                    if (project(c, q))
                        pts.push_back(q);
                }
            }
            if (pts.size() > 1)
                dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), kSplineCol, 0, 1.5f);
        }

        // pass 2: handles (ImGui items so hover/active state is ImGui's, like the strip IK gizmo)
        vec3 rayOrigin(0.f), rayDir(0.f);
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool haveRay = imageSize.x > 0.f && imageSize.y > 0.f &&
                             camera->BuildWorldRayFromNdc((mouse.x - imageMin.x) / imageSize.x * 2.f - 1.f,
                                                          (mouse.y - imageMin.y) / imageSize.y * 2.f - 1.f, rayOrigin, rayDir);
        const bool snapping = m_snap != io.KeyCtrl;
        int axisLock = -1;
        if (ImGui::IsKeyDown(ImGuiKey_X))
            axisLock = 0;
        else if (ImGui::IsKeyDown(ImGuiKey_Y))
            axisLock = 1;
        else if (ImGui::IsKeyDown(ImGuiKey_Z))
            axisLock = 2;
        const ImVec2 cursorRestore = ImGui::GetCursorScreenPos();
        // The selected bone's handles go last so they win ImGui hover where joints coincide (a head
        // snapped onto another bone's tail must still be the one you grab next).
        std::vector<int> order;
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
            if (i != m_selected)
                order.push_back(i);
        if (m_selected >= 0 && m_selected < static_cast<int>(m_bones.size()))
            order.push_back(m_selected);
        for (int i : order)
        {
            RigBone &b = m_bones[i];
            ImVec2 hs, ts;
            if (!project(dispHead[i], hs) || !project(dispTail[i], ts))
                continue;
            const bool selected = i == m_selected;
            const ImVec2 d(ts.x - hs.x, ts.y - hs.y);
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            const ImVec2 n = len > 1.f ? ImVec2(-d.y / len, d.x / len) : ImVec2(1.f, 0.f);
            struct Handle
            {
                int id;
                ImVec2 pos;
                bool drawn;
            };
            Handle handles[5] = {
                {0, hs, true},
                {1, ts, true},
                {2, {hs.x + n.x * screenRadius(dispHead[i], b.headRadius, hs), hs.y + n.y * screenRadius(dispHead[i], b.headRadius, hs)}, selected && m_showShapes && b.shell.empty()},
                {3, {ts.x + n.x * screenRadius(dispTail[i], b.tailRadius, ts), ts.y + n.y * screenRadius(dispTail[i], b.tailRadius, ts)}, selected && m_showShapes && b.shell.empty()},
                {4, {(hs.x + ts.x) * 0.5f, (hs.y + ts.y) * 0.5f}, len > 24.f},
            };
            ImGui::PushID(i);
            for (const Handle &h : handles)
            {
                // off-image handles would extend the Viewport window's content and grow a scrollbar
                if (!h.drawn || h.pos.x < imageMin.x || h.pos.y < imageMin.y || h.pos.x > imageMin.x + imageSize.x ||
                    h.pos.y > imageMin.y + imageSize.y)
                    continue;
                ImGui::PushID(h.id);
                ImGui::SetCursorScreenPos({h.pos.x - kHandleSize * 0.5f, h.pos.y - kHandleSize * 0.5f});
                ImGui::InvisibleButton("##rig_handle", {kHandleSize, kHandleSize});
                const bool itemHovered = ImGui::IsItemHovered();
                const bool itemActive = ImGui::IsItemActive();
                hovered = hovered || itemHovered;
                active = active || itemActive;
                if (ImGui::IsItemActivated())
                {
                    m_selected = i;
                    PushUndo();
                    m_dragBone = i;
                    m_dragHandle = h.id;
                    m_dragHasPrev = false;
                    const vec3 anchor = h.id == 1 || h.id == 3 ? dispTail[i] : h.id == 4 ? (dispHead[i] + dispTail[i]) * 0.5f
                                                                                         : dispHead[i];
                    m_dragPlanePoint = vec3(rootWorld * vec4(anchor, 1.f));
                }
                if (h.id == 2 || h.id == 3)
                {
                    dl->AddRectFilled({h.pos.x - 3.f, h.pos.y - 3.f}, {h.pos.x + 3.f, h.pos.y + 3.f}, (itemHovered || itemActive) ? kHandleHotCol : kHandleCol);
                    if (itemHovered)
                        ui::TooltipText("Drag: shape radius (Shift: fine)");
                }
                else if (h.id < 2 && (itemHovered || itemActive))
                {
                    dl->AddCircle(h.pos, 7.f, kHandleHotCol, 16, 1.5f);
                    if (itemHovered)
                        ui::TooltipText(h.id == 0 ? "Drag: move head (X/Y/Z lock, Shift fine, Ctrl snap)" : "Drag: move tail (X/Y/Z lock, Shift fine, Ctrl snap)");
                }
                else if (h.id == 4 && (itemHovered || itemActive))
                {
                    dl->AddCircle(h.pos, 5.f, kHandleHotCol, 12, 1.5f);
                    if (itemHovered)
                        ui::TooltipText((b.name + "  (drag: move bone)").c_str());
                }

                if (itemActive && m_dragBone == i && m_dragHandle == h.id && haveRay && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    vec3 hit;
                    if (RayPlane(rayOrigin, rayDir, m_dragPlanePoint, camFront, hit))
                    {
                        if (!m_dragHasPrev)
                        {
                            m_dragPrevHit = hit;
                            m_dragHasPrev = true;
                            m_dragFree = h.id == 1 ? dispTail[i] : dispHead[i];
                        }
                        vec3 delta = vec3(invRootWorld * vec4(hit - m_dragPrevHit, 0.f));
                        m_dragPrevHit = hit;
                        if (io.KeyShift)
                            delta *= 0.1f;
                        if (axisLock >= 0)
                        {
                            vec3 axis(0.f);
                            axis[axisLock] = 1.f;
                            delta = axis * glm::dot(delta, axis);
                        }
                        if (h.id == 2 || h.id == 3)
                        {
                            float &radius = h.id == 2 ? b.headRadius : b.tailRadius;
                            const float raw = glm::length(hit - m_dragPlanePoint) / rigScale;
                            float r = io.KeyShift ? radius + (raw - radius) * 0.1f : raw;
                            if (snapping && m_snapMode == 3)
                                r = std::round(r / 0.005f) * 0.005f;
                            radius = std::max(r, kMinRadius);
                        }
                        else
                        {
                            // the joint follows the free cursor position; a snap only overrides where it lands,
                            // so leaving the snap radius releases it under the cursor again (Blender)
                            m_dragFree += delta;
                            vec3 target = m_dragFree;
                            vec3 snapPoint;
                            if (snapping && SnapTarget(target, i, h.id, invRootWorld, rayOrigin, rayDir, project, mouse, snapPoint,
                                                       dispHead, dispTail))
                            {
                                target = snapPoint;
                                ImVec2 sp;
                                if (project(snapPoint, sp))
                                    dl->AddCircle(sp, 9.f, kHandleHotCol, 16, 2.f);
                            }
                            if (h.id == 0)
                                b.head = unskinned(i, target);
                            else if (h.id == 1)
                                MoveTail(i, unskinned(i, target));
                            else
                            {
                                const vec3 newHead = unskinned(i, target);
                                const vec3 move = newHead - b.head;
                                b.head = newHead;
                                MoveTail(i, b.tail + move);
                            }
                        }
                        if (m_mirrorX)
                            SyncMirror(i);
                        m_dirty = true;
                        if (axisLock >= 0)
                        {
                            // axis guide through the dragged point
                            const vec3 anchor = h.id == 1 ? dispTail[i] : dispHead[i];
                            vec3 axis(0.f);
                            axis[axisLock] = 1.f;
                            ImVec2 a0, a1;
                            if (project(anchor - axis * 10.f, a0) && project(anchor + axis * 10.f, a1))
                                dl->AddLine(a0, a1, axisLock == 0 ? IM_COL32(255, 80, 80, 200) : axisLock == 1 ? IM_COL32(120, 255, 120, 200)
                                                                                                               : IM_COL32(100, 140, 255, 200),
                                            1.5f);
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(cursorRestore);
        if (m_dragBone >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_dragBone = -1;
            m_dragHasPrev = false;
        }
        active = active || m_dragBone >= 0;
    }

    // Snap a dragged joint (rig space). Joints: other bones' heads/tails within a few pixels of the
    // cursor. Surface / Volume: the mesh under the cursor (Volume = halfway through it, Blender's
    // armature Volume snap). Increment: 0.01 grid.
    bool RigEditor::SnapTarget(const vec3 &target, int bone, int handle, const mat4 &invRootWorld, const vec3 &rayOrigin,
                               const vec3 &rayDir, const std::function<bool(const vec3 &, ImVec2 &)> &project,
                               const ImVec2 &mouse, vec3 &out, std::span<const vec3> dispHeads,
                               std::span<const vec3> dispTails) const
    {
        if (m_snapMode == 3)
        {
            out = glm::round(target / 0.01f) * 0.01f;
            return true;
        }
        if (m_snapMode == 1 || m_snapMode == 2)
        {
            // ponytail: bind-space mesh; skip while the overlay is posed so a rest hit does not jump a posed joint
            if (dispHeads.size() == m_bones.size())
            {
                for (size_t i = 0; i < dispHeads.size(); i++)
                {
                    const vec3 d = dispHeads[i] - m_bones[i].head;
                    if (glm::dot(d, d) > 1e-6f)
                        return false;
                }
            }
            const vec3 o = vec3(invRootWorld * vec4(rayOrigin, 1.f));
            const vec3 d = glm::normalize(vec3(invRootWorld * vec4(rayDir, 0.f)));
            float tEnter, tExit;
            if (!RayModel(o, d, tEnter, tExit))
                return false;
            const bool volume = m_snapMode == 2 && tExit < std::numeric_limits<float>::max();
            out = o + d * (volume ? (tEnter + tExit) * 0.5f : tEnter);
            return true;
        }
        // joints only: every candidate is a dot you can see
        constexpr float kSnapPx = 24.f;
        float best = kSnapPx;
        bool found = false;
        auto consider = [&](const vec3 &candidate)
        {
            ImVec2 sp;
            if (!project(candidate, sp))
                return;
            const float dist = std::max(std::abs(sp.x - mouse.x), std::abs(sp.y - mouse.y));
            if (dist < best)
            {
                best = dist;
                out = candidate;
                found = true;
            }
        };
        const bool posed = dispHeads.size() == m_bones.size() && dispTails.size() == m_bones.size();
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            if (i == bone)
                continue;
            consider(posed ? dispHeads[i] : m_bones[i].head);
            consider(posed ? dispTails[i] : m_bones[i].tail);
        }
        if (handle == 0 && m_bones[bone].parent >= 0)
        {
            const int parent = m_bones[bone].parent;
            consider(posed ? dispTails[parent] : m_bones[parent].tail);
        }
        return found;
    }
} // namespace pe
