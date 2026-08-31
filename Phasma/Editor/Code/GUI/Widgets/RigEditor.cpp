#include "RigEditor.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Animation/AnimationPoseTools.h"
#include "AnimationTimeline.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/Widgets/FileSelector.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/RendererSystem.h"
#include "imgui/ImGuizmo.h"
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

        // Vectors that key poses must be exact: a short or non-finite array is an error, never a
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

        nlohmann::ordered_json FromVec3(const vec3 &v)
        {
            return nlohmann::ordered_json::array({v.x, v.y, v.z});
        }

        quat RotationOf(const mat4 &transform)
        {
            const vec3 scale(glm::length(vec3(transform[0])), glm::length(vec3(transform[1])),
                             glm::length(vec3(transform[2])));
            if (scale.x <= 1e-8f || scale.y <= 1e-8f || scale.z <= 1e-8f)
                return quat(1.f, 0.f, 0.f, 0.f);
            return glm::normalize(glm::quat_cast(
                mat3(vec3(transform[0]) / scale.x, vec3(transform[1]) / scale.y, vec3(transform[2]) / scale.z)));
        }

        // Elbow bend of a two-bone chain: mid pushed off the root->tip line. carry keeps the last good direction
        // so a straight arm does not flip its pole; returns false when neither exists (any perpendicular then).
        bool BendDirection(const vec3 &root, const vec3 &mid, const vec3 &tip, vec3 *carry, vec3 &out)
        {
            const vec3 chain = tip - root;
            const float chainLength = glm::length(chain);
            const vec3 chainDirection = chainLength > 1e-5f ? chain / chainLength : vec3(0.f, 1.f, 0.f);
            const vec3 bend = (mid - root) - chainDirection * glm::dot(mid - root, chainDirection);
            if (glm::dot(bend, bend) > 1e-8f)
            {
                out = glm::normalize(bend);
                if (carry)
                    *carry = out;
                return true;
            }
            if (carry && glm::dot(*carry, *carry) > 0.5f)
            {
                // Re-project the carried pole onto the current chain-perpendicular plane so a rotated
                // straight limb keeps its side instead of drifting off-plane.
                const vec3 projected = *carry - chainDirection * glm::dot(*carry, chainDirection);
                if (glm::dot(projected, projected) > 1e-8f)
                {
                    out = glm::normalize(projected);
                    *carry = out;
                    return true;
                }
            }
            const vec3 axis = std::abs(chainDirection.x) <= std::abs(chainDirection.y) &&
                                      std::abs(chainDirection.x) <= std::abs(chainDirection.z)
                                  ? vec3(1.f, 0.f, 0.f)
                              : std::abs(chainDirection.y) <= std::abs(chainDirection.z) ? vec3(0.f, 1.f, 0.f)
                                                                                         : vec3(0.f, 0.f, 1.f);
            out = glm::normalize(glm::cross(chainDirection, axis));
            if (carry)
                *carry = out; // seed the pole on a straight start so the axis pick cannot flip mid-clip
            return false;
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

        std::string Utf8Path(const std::filesystem::path &path)
        {
            const std::u8string value = path.u8string();
            return std::string(reinterpret_cast<const char *>(value.c_str()), value.size());
        }

        std::filesystem::path PathFromUtf8(std::string_view value)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
        }
    } // namespace

    RigEditor::~RigEditor()
    {
        ReleaseReferenceImage(true);
    }

    void RigEditor::ReleaseReferenceImage(bool drainRendererFrames)
    {
        if (!m_referenceImage && !m_referenceTexture)
        {
            m_referenceFrameIndex = -1;
            return;
        }
        if (drainRendererFrames)
            if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
                renderer->WaitAllFramesCommands();
        if (m_referenceTexture && m_gui)
            m_gui->ReleaseImageTexture(m_referenceTexture);
        m_referenceTexture = nullptr;
        Image::Destroy(m_referenceImage);
        m_referenceFrameIndex = -1;
    }

    void RigEditor::ClearReference()
    {
        ReleaseReferenceImage(true);
        m_referencePath.clear();
        m_referenceSequence = {};
        m_referencePathBuffer.fill(0);
    }

    bool RigEditor::LoadReference(const std::filesystem::path &requestedPath, std::string &error)
    {
        error.clear();
        std::filesystem::path path = requestedPath;
        if (path.is_relative())
        {
            std::error_code directError;
            if (!std::filesystem::is_regular_file(path, directError))
                path = std::filesystem::path(Path::Assets) / path;
        }
        path = path.lexically_normal();
        std::string filename = path.filename().generic_string();
        std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (!filename.ends_with(".reference.json"))
        {
            error = "reference manifest must end in .reference.json";
            return false;
        }
        AnimationReferenceFrames::Config config;
        AnimationReferenceFrames::Sequence sequence;
        if (!AnimationReferenceFrames::LoadConfig(path, config, error) ||
            !AnimationReferenceFrames::BuildSequence(config, sequence, error))
            return false;

        ClearReference();
        m_referencePath = path;
        m_referenceSequence = std::move(sequence);
        std::snprintf(m_referencePathBuffer.data(), m_referencePathBuffer.size(), "%s", Utf8Path(path).c_str());
        return true;
    }

    bool RigEditor::UpdateReferenceImage(AnimationTimeline *timeline, std::string &error)
    {
        if (!timeline || m_referenceSequence.frames.empty())
            return false;
        double timeSeconds = 0.0;
        if (!timeline->GetViewportTimeSeconds(m_model, timeSeconds))
            return false;
        const AnimationReferenceFrames::Lookup lookup = AnimationReferenceFrames::Resolve(m_referenceSequence, timeSeconds);
        if (!lookup)
        {
            error = lookup.error;
            return false;
        }
        const int frame = static_cast<int>(lookup.sequenceIndex);
        if (frame == m_referenceFrameIndex)
            return m_referenceImage && m_referenceTexture;

        Queue *queue = RHII.GetMainQueue();
        if (!queue || !m_gui)
        {
            error = "reference image upload is unavailable";
            return false;
        }
        CommandBuffer *command = queue->AcquireCommandBuffer();
        command->Begin();
        Image *image = Image::LoadRGBA8(command, Utf8Path(lookup.path));
        command->End();
        queue->Submit(1, &command, nullptr, nullptr);
        command->Wait();
        command->Return();
        if (!image)
        {
            ReleaseReferenceImage();
            error = "failed to load reference frame " + lookup.path.filename().generic_string();
            return false;
        }
        void *texture = m_gui->RegisterImageTexture(image);
        if (!texture)
        {
            Image::Destroy(image);
            ReleaseReferenceImage();
            error = "failed to register the reference frame for ImGui";
            return false;
        }

        ReleaseReferenceImage();
        m_referenceImage = image;
        m_referenceTexture = texture;
        m_referenceFrameIndex = frame;
        return true;
    }

    void RigEditor::DrawReferenceOverlay(AnimationTimeline *timeline, const ImVec2 &imageMin, const ImVec2 &imageSize)
    {
        std::string error;
        if (!UpdateReferenceImage(timeline, error))
        {
            if (!error.empty())
                m_status = error;
            return;
        }
        const float imageWidth = static_cast<float>(m_referenceImage->GetWidth());
        const float imageHeight = static_cast<float>(m_referenceImage->GetHeight());
        if (imageWidth <= 0.f || imageHeight <= 0.f)
            return;
        const AnimationReferenceFrames::Config &config = m_referenceSequence.config;
        const float fit = std::min(imageSize.x / imageWidth, imageSize.y / imageHeight) * config.scale;
        const ImVec2 size(imageWidth * fit, imageHeight * fit);
        const ImVec2 center(imageMin.x + imageSize.x * 0.5f + config.offset.x,
                            imageMin.y + imageSize.y * 0.5f + config.offset.y);
        const ImVec2 min(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
        const ImVec2 max(center.x + size.x * 0.5f, center.y + size.y * 0.5f);
        const ImVec2 uv0(config.flipX ? 1.f : 0.f, config.flipY ? 1.f : 0.f);
        const ImVec2 uv1(config.flipX ? 0.f : 1.f, config.flipY ? 0.f : 1.f);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)m_referenceTexture, min, max, uv0, uv1,
                                             IM_COL32(255, 255, 255, static_cast<int>(config.opacity * 255.f)));
    }

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
            if (m_poseDragging && m_gui)
                if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                    timeline->EndViewportRotate();
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
        if (m_poseDragging && m_gui)
            if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                timeline->EndViewportRotate();
        m_poseDragging = false;
        m_weightDragging = false;
        if (!model->HasSkeleton())
            m_mode = Mode::Edit;
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
        m_lockBend.clear();
        m_pins.clear();
        AbortPoseEdits();
        if (AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr)
            m_poseEditSerial = timeline->PoseEditSerial(); // a new target must not solve older pose edits
        m_presetName.clear();
        m_selected = -1;
        m_poseSelected = -1;
        m_dragBone = -1;
        m_poseDragging = false;
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
        m_lockBend.clear();
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
        if (!RigPresetLibrary::Fit(preset, bounds, shellNames, fitted, error, useLandmarks ? &landmarks : nullptr))
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
            bones.push_back(bone);
        }
        j["bones"] = bones;
        nlohmann::ordered_json locks = nlohmann::ordered_json::array();
        for (const RigLock &lock : m_locks)
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
                state["mode"] = m_mode == Mode::Pose ? "pose" : "edit";
                state["pose_selected"] = m_poseSelected;
                state["joint_blend"] = m_jointBlend;
                state["weights_dirty"] = m_weightDirty;
                state["weight_undo"] = m_weightUndo.size();
                state["weight_redo"] = m_weightRedo.size();
                state["onion_bones"] = m_onionBones;
                state["onion_previous"] = m_onionPrevious;
                state["onion_next"] = m_onionNext;
                state["motion_trail"] = m_motionTrail;
                state["motion_trail_previous"] = m_trailPrevious;
                state["motion_trail_next"] = m_trailNext;
                state["two_bone_ik"] = m_twoBoneIk;
                state["ik_bone"] = m_ikBone;
                state["ik_target"] = FromVec3(m_ikTarget);
                state["ik_pole"] = FromVec3(m_ikPole);
                const AnimationReferenceFrames::Config &reference = m_referenceSequence.config;
                const char *referenceSource = reference.source == AnimationReferenceFrames::Source::Directory ? "directory"
                                              : reference.source == AnimationReferenceFrames::Source::Pattern ? "pattern"
                                                                                                              : "files";
                state["reference"] = {{"loaded", !m_referenceSequence.frames.empty()},
                                      {"path", m_referencePath.generic_string()},
                                      {"frames", m_referenceSequence.frames.size()},
                                      {"source", referenceSource},
                                      {"source_fps", reference.sourceFps},
                                      {"timeline_offset_seconds", reference.timelineOffsetSeconds},
                                      {"playback", reference.playback == AnimationReferenceFrames::Playback::Loop ? "loop" : "clamp"},
                                      {"opacity", reference.opacity},
                                      {"scale", reference.scale},
                                      {"offset", {reference.offset.x, reference.offset.y}},
                                      {"flip_x", reference.flipX},
                                      {"flip_y", reference.flipY},
                                      {"current_frame", m_referenceFrameIndex}};
                if (m_gui)
                    if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                        state["auto_key"] = timeline->AutoKey();
                state["shells"] = nlohmann::json::parse(ShellsJson());
                state["target"] = m_model ? m_model->GetLabel() : "";
                return ok(state);
            }
            if (action == "rig.mode")
            {
                const std::string mode = args.value("mode", "edit");
                if (mode != "edit" && mode != "pose")
                    return fail("unknown rig mode: " + mode + " (edit|pose)");
                if (mode == "pose" && (!m_model || !m_model->HasSkeleton()))
                    return fail("pose mode requires a baked skeleton");
                SetMode(mode == "pose" ? Mode::Pose : Mode::Edit);
                return ok({{"mode", mode}});
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
            if (action == "rig.reference_load")
            {
                const std::string path = args.value("path", "");
                if (path.empty())
                    return fail("path to a .reference.json manifest is required");
                std::string error;
                if (!LoadReference(PathFromUtf8(path), error))
                    return fail(error);
                return ok({{"path", m_referencePath.generic_string()}, {"frames", m_referenceSequence.frames.size()}});
            }
            if (action == "rig.reference_clear")
            {
                ClearReference();
                return ok();
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
                    return fail("weight strokes require the model's current Animation Timeline pose");

                const Skeleton &skeleton = m_model->GetSkeleton();
                const nlohmann::json &boneValue = args.contains("bone") ? args["bone"] : nlohmann::json();
                const int bone = boneValue.is_number_integer() ? boneValue.get<int>()
                                 : boneValue.is_string()       ? skeleton.GetBoneIndex(boneValue.get<std::string>())
                                                               : m_poseSelected;
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
                m_poseSelected = bone;
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
                const std::string pivot = args.value("pivot", "feet");
                const int pivotMode = pivot == "centre" || pivot == "center" ? 1 : pivot == "origin" ? 2
                                                                                                     : 0;
                PushUndo();
                TransformRig(m_undo.back().bones, move, rotate, scale, pivotMode);
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
            if (action == "rig.pin" || action == "rig.grab")
            {
                if (!m_model || !m_model->HasSkeleton())
                    return fail("the model has no skeleton");
                const Skeleton &skeleton = m_model->GetSkeleton();
                const std::string name = args.value("bone", "");
                const int bone = skeleton.GetBoneIndex(name);
                if (bone < 0)
                    return fail("unknown bone: " + name);
                if (action == "rig.pin")
                {
                    const bool want = args.value("pinned", !IsPinned(name));
                    if (want != IsPinned(name))
                        TogglePin(name);
                    return ok({{"bone", name}, {"pinned", IsPinned(name)}, {"pins", m_pins}});
                }
                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                if (!renderer || !timeline || !args.contains("target"))
                    return fail("rig.grab needs target[3], the renderer and the Animation Timeline");
                *timeline->GetOpen() = true; // a closed Timeline resolves next frame; the retry then has a pose
                if (timeline->HasPendingRequests())
                    return fail("the Timeline has queued requests (frame/pose/clip); retry after the next frame");
                vec3 target;
                if (!StrictVec3(args["target"], target))
                    return fail("target must be three finite numbers");
                float gap = 0.f;
                bool keyed = false;
                if (!GrabTo(renderer->GetScene(), bone, target, &gap, true, &keyed))
                    return fail("nothing grabbed: the clip must be active in the Timeline and the bone needs a parent");
                // A one-shot grab commits like a mouse release: the grabbed bone's own lock reapplies
                // (locks always win) and the reported gap is measured on the final pose. An at-target
                // no-op keyed nothing, so there is nothing to re-solve either.
                if (keyed && SolveLocks(renderer->GetScene()))
                {
                    AnimationTimeline::ViewportPose pose;
                    std::vector<vec3> heads, tails;
                    if (timeline->GetViewportPose(m_model, pose) &&
                        static_cast<int>(pose.boneTransforms.size()) == skeleton.GetBoneCount())
                    {
                        PoseTails(skeleton, pose.boneTransforms, heads, tails);
                        gap = glm::distance(tails[bone], target);
                    }
                }
                nlohmann::json chain = nlohmann::json::array();
                for (int b = bone; b >= 0 && skeleton.bones[b].parentIndex >= 0; b = skeleton.bones[b].parentIndex)
                {
                    if (b != bone && IsPinned(skeleton.bones[b].name))
                        break;
                    chain.push_back(skeleton.bones[b].name);
                }
                return ok({{"bone", name}, {"gap", gap}, {"chain", chain}});
            }
            if (action == "rig.lock")
            {
                const std::string op = args.value("op", "list");
                // gap = tail-to-anchor distance at the Timeline's current pose (agents assert "held" on it).
                AnimationTimeline::ViewportPose currentPose;
                std::vector<vec3> poseHeads, poseTails;
                if (AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                    timeline && m_model && m_model->HasSkeleton() && timeline->GetViewportPose(m_model, currentPose) &&
                    static_cast<int>(currentPose.boneTransforms.size()) == m_model->GetSkeleton().GetBoneCount())
                    PoseTails(m_model->GetSkeleton(), currentPose.boneTransforms, poseHeads, poseTails);
                auto lockJson = [&](int i)
                {
                    const RigLock &lock = m_locks[i];
                    int root, mid, target;
                    std::string why;
                    vec3 anchor;
                    const bool valid = m_model && m_model->HasSkeleton() &&
                                       LockChain(lock, m_model->GetSkeleton(), root, mid, target, &why);
                    nlohmann::json j{{"index", i},
                                     {"bone", lock.bone},
                                     {"target", lock.target},
                                     {"anchor", {lock.anchor.x, lock.anchor.y, lock.anchor.z}},
                                     {"reach", lock.reach},
                                     {"enabled", lock.enabled},
                                     {"valid", valid},
                                     {"why", why}};
                    if (valid && !poseTails.empty() &&
                        LockAnchorPosed(lock, m_model->GetSkeleton(), currentPose.boneTransforms, anchor))
                    {
                        j["gap"] = glm::distance(anchor, poseTails[mid]);
                        j["tail"] = {poseTails[mid].x, poseTails[mid].y, poseTails[mid].z};
                        j["anchor_posed"] = {anchor.x, anchor.y, anchor.z};
                        j["root"] = {poseHeads[root].x, poseHeads[root].y, poseHeads[root].z};
                        j["mid"] = {poseHeads[mid].x, poseHeads[mid].y, poseHeads[mid].z};
                    }
                    return j;
                };
                auto lockIndex = [&](int &index)
                {
                    index = args.value("index", -1);
                    if (index < 0 && args.contains("bone") && args["bone"].is_string())
                        for (int i = 0; i < static_cast<int>(m_locks.size()); i++)
                            if (m_locks[i].bone == args["bone"].get<std::string>())
                                index = i;
                    return index >= 0 && index < static_cast<int>(m_locks.size());
                };
                if (op == "list")
                {
                    nlohmann::json locks = nlohmann::json::array();
                    for (int i = 0; i < static_cast<int>(m_locks.size()); i++)
                        locks.push_back(lockJson(i));
                    return ok({{"locks", locks}});
                }
                if (op == "add")
                {
                    std::string bone = args.value("bone", "");
                    if (bone.empty() && m_model && m_model->HasSkeleton() && m_poseSelected >= 0 &&
                        m_poseSelected < m_model->GetSkeleton().GetBoneCount())
                        bone = m_model->GetSkeleton().bones[m_poseSelected].name;
                    const bool hasAnchor = args.contains("anchor");
                    vec3 anchor(0.f);
                    if (hasAnchor && !StrictVec3(args["anchor"], anchor))
                        return fail("anchor must be three finite numbers");
                    std::string error;
                    const int added = AddLock(bone, args.value("target", ""), args.value("reach", 1.f),
                                              hasAnchor ? &anchor : nullptr, error);
                    return added < 0 ? fail(error) : ok({{"lock", lockJson(added)}});
                }
                int index;
                if (op == "remove" || op == "set")
                {
                    if (!lockIndex(index))
                        return fail("unknown lock: give index or bone");
                    PushUndo(true);
                    m_lockBend.clear();
                    m_dirty = true;
                    if (op == "remove")
                    {
                        m_locks.erase(m_locks.begin() + index);
                        return ok({{"locks", m_locks.size()}});
                    }
                    RigLock &lock = m_locks[index];
                    if (args.contains("target"))
                        lock.target = args.value("target", "");
                    if (args.contains("anchor") && !StrictVec3(args["anchor"], lock.anchor))
                        return fail("anchor must be three finite numbers");
                    lock.reach = std::clamp(args.value("reach", lock.reach), 0.3f, 1.f);
                    lock.enabled = args.value("enabled", lock.enabled);
                    return ok({{"lock", lockJson(index)}});
                }
                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
                if (!renderer || !timeline)
                    return fail("renderer or Animation Timeline not available");
                *timeline->GetOpen() = true; // a closed Timeline resolves next frame; the retry then has a pose
                if (timeline->HasPendingRequests())
                    return fail("the Timeline has queued requests (frame/pose/clip); retry after the next frame");
                if (op == "solve")
                {
                    if (!SolveLocks(renderer->GetScene(), -1, true))
                        return fail("nothing solved: locks need the clip active in the Timeline and valid chains");
                    return ok({{"status", m_status}});
                }
                if (op == "bake")
                {
                    std::string status;
                    const bool done = BakeLocks(renderer->GetScene(), status);
                    m_status = status;
                    return done ? ok({{"status", status}}) : fail(status);
                }
                return fail("unknown lock op: list|add|remove|set|solve|bake");
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
    // Drop any in-flight pose interaction (grab, gizmo drag, IK gizmos, reach drag) without keying:
    // mode/tool switches and target changes must never leave a half-done edit armed.
    void RigEditor::AbortPoseEdits()
    {
        m_grabBone = -1;
        m_grabPushed = false;
        m_ikBone = -1;
        m_ikDragging = false;
        m_ikDirty = false;
        m_reachDragging = false;
        m_reachPushed = false;
        if (m_poseDragging && m_gui)
            if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                timeline->EndViewportRotate();
        m_poseDragging = false;
    }

    void RigEditor::SetMode(Mode mode)
    {
        if (mode == Mode::Pose && (!m_model || !m_model->HasSkeleton()))
            return;
        if (m_mode == mode)
            return;
        AbortPoseEdits();
        m_mode = mode;
        // Edit Rig edits rest data: show the rest pose so the document bones sit on the mesh they describe.
        if (mode != Mode::Pose && m_gui && m_model && m_model->HasSkeleton())
            if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                timeline->RequestRestPose();
        // Pose mode needs the Timeline resolved (it owns the evaluated pose and key writes).
        if (mode == Mode::Pose && m_gui)
            if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
            {
                *timeline->GetOpen() = true;
                m_poseEditSerial = timeline->PoseEditSerial(); // older pose edits are not ours to solve
            }
        m_status.clear();
        if (mode != Mode::Pose || !m_gui)
            return;

        AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>();
        if (!timeline)
            return;
        *timeline->GetOpen() = true;
        const Skeleton &skeleton = m_model->GetSkeleton();
        if (m_selected >= 0 && m_selected < static_cast<int>(m_bones.size()))
            m_poseSelected = skeleton.GetBoneIndex(m_bones[m_selected].name);
        if (m_poseSelected < 0 && skeleton.GetBoneCount() > 0)
            m_poseSelected = 0;
        if (m_poseSelected >= 0)
            timeline->RequestBone(skeleton.bones[m_poseSelected].name);
    }

    void RigEditor::Update()
    {
        if (!m_open)
            return;
        ImGui::SetNextWindowSize(ImVec2(560.f, 440.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.f, 320.f), ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
        if (!ImGui::Begin(m_name.c_str(), &m_open))
        {
            ImGui::End();
            return;
        }
        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
        {
            ImGui::End();
            return;
        }
        Scene &scene = renderer->GetScene();
        ResolveTarget(scene);
        if (!m_model)
        {
            ImGui::TextDisabled("Select a node of a loaded model (.pemesh) in the Hierarchy to rig it.");
            ImGui::End();
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
        if (ImGui::RadioButton("Edit Rig", m_mode == Mode::Edit))
            SetMode(Mode::Edit);
        ui::ItemTooltip("Author bones and influence shapes over the model, then Bake them into a skeleton.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_model->HasSkeleton());
        if (ImGui::RadioButton("Pose", m_mode == Mode::Pose))
            SetMode(Mode::Pose);
        ImGui::EndDisabled();
        ui::ItemTooltip(m_model->HasSkeleton() ? "Pose the baked skeleton directly in the viewport."
                                               : "Bake or load a model with a skeleton first.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        const ImGuiIO &io = ImGui::GetIO();
        if (m_mode == Mode::Pose)
        {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput && m_gui)
            {
                if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                    {
                        if (m_jointBlend)
                            WeightUndo(scene, io.KeyShift);
                        else
                            timeline->StepViewportUndo(scene, io.KeyShift);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                    {
                        if (m_jointBlend)
                            WeightUndo(scene, true);
                        else
                            timeline->StepViewportUndo(scene, true);
                    }
                }
            }
            ImGui::Separator();
            DrawPosePanel(scene);
            ImGui::TextDisabled("%s", m_status.empty() ? "Viewport: drag a tail dot to pull the limb (padlocks pin where the pull stops), or click a bone and drag its rings. Each drag is one Timeline undo step." : m_status.c_str());
            // Lock / pin / reach edits are document edits, and Pose mode returns before the Edit-mode
            // autosave below: save here too, or closing / hot-reloading in Pose mode loses them.
            if (m_dirty && !ImGui::IsAnyItemActive() && !RigJsonPath().empty())
                SaveJson(nullptr, true);
            ImGui::End();
            return;
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                io.KeyShift ? Redo() : Undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                Redo();
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
        ImGui::EndChild();

        // Autosave: the rig.json beside the model is the document, so an editor restart or scene
        // reload (which rebuilds the ModelAsset) never loses work. Waits for drags / edits to finish.
        if (m_dirty && m_dragBone < 0 && !ImGui::IsAnyItemActive() && !RigJsonPath().empty())
            SaveJson(nullptr, true);
        if (m_dragBone < 0 && !ImGui::IsAnyItemActive() &&
            (m_heatDirty || (m_heat == 1 && m_heatSelected != m_selected) || (m_heat == 0 && !m_heatBackup.empty())))
            UpdateHeatMap(scene);

        ImGui::TextDisabled("%s", m_status.empty() ? "Viewport: drag joints, Shift-drag a head to move the bone, drag the middle to move the bone; X/Y/Z lock an axis, Shift = precision, Ctrl = snap; Ctrl+Z undo" : m_status.c_str());
        ImGui::End();
    }

    void RigEditor::DrawPosePanel(Scene &scene)
    {
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline)
        {
            ImGui::TextDisabled("Animation Timeline is not available.");
            return;
        }

        bool autoKey = timeline->AutoKey();
        if (ImGui::Checkbox("Auto Key", &autoKey))
            timeline->SetAutoKey(autoKey);
        ui::ItemTooltip("On: insert or overwrite the current-frame rotation key. Off: only overwrite an existing rotation key.");
        ui::SameLineIfFits(ui::CheckboxWidth("Mirror X"));
        ImGui::Checkbox("Mirror X", &m_mirrorX);
        ui::ItemTooltip("Rotate the .L/.R counterpart geometrically across the rig X plane in the same undo step.");
        ui::SameLineIfFits(ui::ButtonWidth("Pose Undo"), 18.f);
        if (ImGui::Button("Pose Undo"))
            timeline->StepViewportUndo(scene, false);
        ui::ItemTooltip("Undo the last viewport pose drag; one drag is one step.");
        ui::SameLineIfFits(ui::ButtonWidth("Pose Redo"));
        if (ImGui::Button("Pose Redo"))
            timeline->StepViewportUndo(scene, true);
        ui::ItemTooltip("Redo the last undone viewport pose drag.");
        ui::SameLineIfFits(ui::CheckboxWidth("Rotate") + ui::CheckboxWidth("Move") + ui::CheckboxWidth("Both"), 18.f);
        ImGui::RadioButton("Rotate", &m_poseGizmo, 0);
        ui::ItemTooltip("Viewport gizmo rotates the selected bone.");
        ImGui::SameLine();
        ImGui::RadioButton("Move", &m_poseGizmo, 1);
        ui::ItemTooltip("Viewport gizmo moves the selected bone (keys its location; locked hands follow).");
        ImGui::SameLine();
        ImGui::RadioButton("Both", &m_poseGizmo, 2);
        ui::ItemTooltip("Move and rotate handles together.");

        const Skeleton &skeleton = m_model->GetSkeleton();
        const float kStepperWidth = ImGui::GetFontSize() * 4.f;
        if (m_poseSelected >= skeleton.GetBoneCount())
            m_poseSelected = -1;
        ImGui::Separator();
        ImGui::Checkbox("Onion Bones", &m_onionBones);
        ui::ItemTooltip("Draw the skeleton at the surrounding displayed frames as faded ghosts.");
        if (m_onionBones)
        {
            ui::SameLineIfFits(kStepperWidth);
            ImGui::SetNextItemWidth(kStepperWidth);
            ImGui::DragInt("-##onion_previous", &m_onionPrevious, 0.1f, 0, 12);
            ui::ItemTooltip("Ghost frames drawn before the playhead.");
            ui::SameLineIfFits(kStepperWidth);
            ImGui::SetNextItemWidth(kStepperWidth);
            ImGui::DragInt("+##onion_next", &m_onionNext, 0.1f, 0, 12);
            ui::ItemTooltip("Ghost frames drawn after the playhead.");
        }
        ui::SameLineIfFits(ui::CheckboxWidth("Motion Trail"), 18.f);
        ImGui::Checkbox("Motion Trail", &m_motionTrail);
        ui::ItemTooltip("Draw the selected bone's origin across the surrounding frames as a path.");
        if (m_motionTrail)
        {
            ui::SameLineIfFits(kStepperWidth);
            ImGui::SetNextItemWidth(kStepperWidth);
            ImGui::DragInt("-##trail_previous", &m_trailPrevious, 0.1f, 0, 60);
            ui::ItemTooltip("Trail samples taken before the playhead.");
            ui::SameLineIfFits(kStepperWidth);
            ImGui::SetNextItemWidth(kStepperWidth);
            ImGui::DragInt("+##trail_next", &m_trailNext, 0.1f, 0, 60);
            ui::ItemTooltip("Trail samples taken after the playhead.");
        }
        const int ikMid = m_poseSelected >= 0 && m_poseSelected < skeleton.GetBoneCount()
                              ? skeleton.bones[m_poseSelected].parentIndex
                              : -1;
        const bool hasIkChain = ikMid >= 0 && ikMid < skeleton.GetBoneCount() &&
                                skeleton.bones[ikMid].parentIndex >= 0;
        ImGui::BeginDisabled(!hasIkChain);
        if (ImGui::Checkbox("Two-Bone IK", &m_twoBoneIk))
        {
            m_ikBone = -1;
            m_ikDragging = false;
            m_ikDirty = false;
            m_grabBone = -1; // the grab block is skipped while a tool owns the mouse: never leave one armed
            m_grabPushed = false;
            if (m_twoBoneIk)
            {
                m_jointBlend = false;
                if (m_poseDragging)
                {
                    timeline->EndViewportRotate();
                    m_poseDragging = false;
                }
                if (m_weightDragging)
                {
                    if (m_weightStrokeChanged)
                        UploadWeights(scene);
                    m_weightDragging = false;
                    m_weightStrokeChanged = false;
                    m_weightHasLastCenter = false;
                }
            }
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(hasIkChain ? "Selected bone is the tip; its parent and grandparent form the two solved links."
                                   : "Select a tip bone with both a parent and grandparent.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!hasIkChain)
            m_twoBoneIk = false;
        if (m_twoBoneIk)
        {
            ui::SameLineIfFits(ui::ButtonWidth("Reset IK Target"));
            if (ImGui::SmallButton("Reset IK Target"))
                m_ikBone = -1;
            ui::ItemTooltip("Snap the target and pole gizmos back onto the current pose.");
        }

        auto showMotionResult = [&](const char *label, const std::string &result)
        {
            const nlohmann::json response = nlohmann::json::parse(result, nullptr, false);
            if (response.is_discarded())
                m_status = std::string(label) + " failed: invalid response";
            else if (response.contains("error"))
                m_status = std::string(label) + " failed: " + response.value("error", "unknown error");
            else
                m_status = std::string(label) + " applied in memory; use Timeline Save to persist.";
        };
        ImGui::BeginDisabled(m_poseSelected < 0 || m_poseSelected >= skeleton.GetBoneCount());
        if (ImGui::Button("Plant From Here"))
            showMotionResult("Plant From Here",
                             timeline->HandleAction("timeline.motion.stabilize_world",
                                                    nlohmann::json{{"bone", skeleton.bones[m_poseSelected].name}}.dump()));
        ui::ItemTooltip("Stabilize the selected bone's world position from the current frame through clip end.");
        ui::SameLineIfFits(ui::ButtonWidth("Spring Selected Chain"));
        if (ImGui::Button("Spring Selected Chain"))
        {
            const std::vector<std::string> chain = SelectedSpringChain();
            if (chain.size() < 2)
                m_status = "Spring Selected Chain needs a contiguous selected/direct-child chain of at least two bones.";
            else
                showMotionResult("Spring Selected Chain",
                                 timeline->HandleAction("timeline.motion.spring_bake",
                                                        nlohmann::json{{"bones", chain}}.dump()));
        }
        ui::ItemTooltip("Bake follow-through on the selected chain; a unique spline child is preferred at forks.");
        ui::SameLineIfFits(ui::ButtonWidth("Breakdown 50%"));
        if (ImGui::Button("Breakdown 50%"))
            showMotionResult("Breakdown",
                             timeline->HandleAction("timeline.motion.breakdown",
                                                    nlohmann::json{{"bone", skeleton.bones[m_poseSelected].name},
                                                                   {"bias", 0.5f}}
                                                        .dump()));
        ImGui::EndDisabled();
        ui::ItemTooltip("Write one complete pose for the selected bone halfway between its surrounding keys. Needs a selected bone.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

        if (ImGui::CollapsingHeader("Reference Sequence"))
        {
            ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 11.f,
                                             ImGui::GetContentRegionAvail().x - ui::ButtonWidth("Browse Load Clear")));
            ImGui::InputText("##reference_path", m_referencePathBuffer.data(), m_referencePathBuffer.size());
            ui::ItemTooltip("Manifest v1: source_fps, timeline_offset_seconds, playback clamp|loop, opacity, scale, offset, flip_x, flip_y, and exactly one source: files, directory, or pattern.");
            ui::SameLineIfFits(ui::ButtonWidth("Browse##reference"));
            if (ImGui::Button("Browse##reference") && m_gui)
                if (FileSelector *selector = m_gui->GetWidget<FileSelector>())
                    selector->OpenSelection([this](const std::string &path)
                                            {
                                                std::string error;
                                                m_status = LoadReference(PathFromUtf8(path), error)
                                                               ? "Loaded reference sequence."
                                                               : error;
                                                return error.empty(); },
                                            {".json"}, Path::Assets);
            ui::ItemTooltip("Pick a .reference.json manifest under Assets.");
            ui::SameLineIfFits(ui::ButtonWidth("Load##reference"));
            if (ImGui::Button("Load##reference"))
            {
                std::string error;
                m_status = LoadReference(PathFromUtf8(m_referencePathBuffer.data()), error)
                               ? "Loaded reference sequence."
                               : error;
            }
            ui::ItemTooltip("Load the manifest named in the path field.");
            ui::SameLineIfFits(ui::ButtonWidth("Clear##reference"));
            ImGui::BeginDisabled(m_referenceSequence.frames.empty());
            if (ImGui::Button("Clear##reference"))
            {
                ClearReference();
                m_status = "Reference sequence cleared.";
            }
            ImGui::EndDisabled();
            ui::ItemTooltip("Drop the sequence and release its cached frame texture.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            if (!m_referenceSequence.frames.empty())
            {
                AnimationReferenceFrames::Config &reference = m_referenceSequence.config;
                ImGui::SliderFloat("Opacity##reference", &reference.opacity, 0.f, 1.f, "%.2f");
                ui::ItemTooltip("How strongly the reference image is blended over the rendered scene.");
                ImGui::DragFloat("Scale##reference", &reference.scale, 0.01f, 0.05f, 5.f, "%.2f");
                ui::ItemTooltip("Uniform scale of the reference image in the viewport.");
                ImGui::DragFloat2("Offset px##reference", &reference.offset.x, 1.f, -4096.f, 4096.f, "%.0f");
                ui::ItemTooltip("Pixel offset of the reference image in the viewport.");
                ImGui::Checkbox("Flip X##reference", &reference.flipX);
                ui::ItemTooltip("Mirror the reference image horizontally.");
                ui::SameLineIfFits(ui::CheckboxWidth("Flip Y##reference"));
                ImGui::Checkbox("Flip Y##reference", &reference.flipY);
                ui::ItemTooltip("Mirror the reference image vertically.");
                ImGui::SameLine();
                ImGui::TextDisabled("%d frames @ %.2f fps", static_cast<int>(m_referenceSequence.frames.size()),
                                    reference.sourceFps);
                ui::ItemTooltip("Frames resolved from the manifest, and the source rate they are sampled at.");
            }
        }
        ImGui::Separator();
        if (ImGui::Checkbox("Joint Blend", &m_jointBlend))
        {
            if (m_jointBlend)
            {
                m_twoBoneIk = false;
                m_ikBone = -1;
                m_ikDragging = false;
                m_ikDirty = false;
            }
            m_grabBone = -1; // the grab block is skipped while a tool owns the mouse: never leave one armed
            m_grabPushed = false;
            if (m_jointBlend && m_poseDragging)
            {
                timeline->EndViewportRotate();
                m_poseDragging = false;
            }
            if (!m_jointBlend && m_weightDragging)
            {
                if (m_weightStrokeChanged)
                    UploadWeights(scene);
                m_weightDragging = false;
                m_weightStrokeChanged = false;
                m_weightHasLastCenter = false;
            }
            m_status = m_jointBlend ? "Joint Blend: paint the selected bone on the posed mesh." : "Joint Blend off.";
        }
        ui::ItemTooltip("Paint actual skin weights at the current pose. GPU weights upload once when the drag ends.");
        if (m_jointBlend)
        {
            const char *selectedName = m_poseSelected >= 0 && m_poseSelected < skeleton.GetBoneCount()
                                           ? skeleton.bones[m_poseSelected].name.c_str()
                                           : "(select a bone)";
            ImGui::SameLine();
            ImGui::TextDisabled("%s", selectedName);
            if (ImGui::RadioButton("Add", m_weightMode == RigWeightStroke::Mode::Add))
                m_weightMode = RigWeightStroke::Mode::Add;
            ui::ItemTooltip("Raise the selected bone's weight under the brush; the other influences give way.");
            ui::SameLineIfFits(ui::CheckboxWidth("Erase"));
            if (ImGui::RadioButton("Erase", m_weightMode == RigWeightStroke::Mode::Erase))
                m_weightMode = RigWeightStroke::Mode::Erase;
            ui::ItemTooltip("Lower the selected bone's weight under the brush and hand it back to its neighbours.");
            ui::SameLineIfFits(ui::CheckboxWidth("Smooth"));
            if (ImGui::RadioButton("Smooth", m_weightMode == RigWeightStroke::Mode::Smooth))
                m_weightMode = RigWeightStroke::Mode::Smooth;
            ui::ItemTooltip("Average the selected bone's weight with its rest-space neighbours to relax a hard seam.");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
            ImGui::DragFloat("Radius", &m_weightRadius, 0.002f, kMinRadius, std::max(ModelHeight(), kMinRadius), "%.3f");
            ui::ItemTooltip("Brush radius in model units, measured on the posed mesh; the mouse wheel resizes it over the mesh.");
            ui::SameLineIfFits(ui::LabelledItemWidth(ImGui::GetFontSize() * 9.f, "Strength"));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
            ImGui::SliderFloat("Strength", &m_weightStrength, 0.f, 1.f, "%.2f");
            ui::ItemTooltip("How much weight one stroke sample moves at the brush centre; Shift+wheel adjusts it over the mesh.");
            ImGui::BeginDisabled(m_weightUndo.empty());
            if (ImGui::Button("Weight Undo"))
                WeightUndo(scene, false);
            ImGui::EndDisabled();
            ui::ItemTooltip("Undo one Joint Blend drag.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            ui::SameLineIfFits(ui::ButtonWidth("Weight Redo"));
            ImGui::BeginDisabled(m_weightRedo.empty());
            if (ImGui::Button("Weight Redo"))
                WeightUndo(scene, true);
            ImGui::EndDisabled();
            ui::ItemTooltip("Redo one Joint Blend drag.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            ui::SameLineIfFits(ui::ButtonWidth("Save Weights *"));
            if (ImGui::Button(m_weightDirty ? "Save Weights *" : "Save Weights"))
            {
                std::string error;
                m_status = SaveWeights(error) ? "Saved weights to " + m_model->GetFilePath().filename().generic_string()
                                              : error;
            }
            ui::ItemTooltip("Explicitly overwrite the current .pemesh. Weight strokes never autosave.");
            ImGui::Separator();
        }
        DrawLocksPanel(scene, timeline);
        ImGui::TextDisabled("Pose Bones");
        // Never squashed by the controls above: at least 12 rows, else the window scrolls; leaves the status line.
        const float bonesMin = ImGui::GetTextLineHeightWithSpacing() * 12.f;
        const float bonesAvail = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##pose_bones", ImVec2(0.f, std::max(bonesAvail, bonesMin)), ImGuiChildFlags_Borders);
        ui::PushCompactTree();
        DrawPoseBoneTree(skeleton, -1, 0);
        ui::PopCompactTree();
        ImGui::EndChild();
    }

    void RigEditor::DrawPoseBoneTree(const Skeleton &skeleton, int parent, int depth)
    {
        for (int i = 0; i < skeleton.GetBoneCount(); i++)
        {
            if (skeleton.bones[i].parentIndex != parent)
                continue;
            bool hasChildren = false;
            for (int child = 0; child < skeleton.GetBoneCount(); child++)
                hasChildren = hasChildren || skeleton.bones[child].parentIndex == i;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (i == m_poseSelected)
                flags |= ImGuiTreeNodeFlags_Selected;
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<intptr_t>(i)), flags, "%s",
                                                skeleton.bones[i].name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                m_poseSelected = i;
                if (m_gui)
                    if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                        timeline->RequestBone(skeleton.bones[i].name);
            }
            if (open)
            {
                if (hasChildren && depth < 64)
                    DrawPoseBoneTree(skeleton, i, depth + 1);
                ImGui::TreePop();
            }
        }
    }

    std::vector<std::string> RigEditor::SelectedSpringChain() const
    {
        std::vector<std::string> result;
        if (!m_model || m_poseSelected < 0 || m_poseSelected >= m_model->GetSkeleton().GetBoneCount())
            return result;
        const Skeleton &skeleton = m_model->GetSkeleton();
        auto isSpline = [&](int bone)
        {
            const int authored = bone >= 0 && bone < skeleton.GetBoneCount() ? FindBone(skeleton.bones[bone].name) : -1;
            return authored >= 0 && m_bones[authored].spline;
        };
        auto children = [&](int parent)
        {
            std::vector<int> found;
            for (int bone = 0; bone < skeleton.GetBoneCount(); bone++)
                if (skeleton.bones[bone].parentIndex == parent)
                    found.push_back(bone);
            return found;
        };

        int current = m_poseSelected;
        const std::vector<int> direct = children(current);
        if (!isSpline(current))
        {
            std::vector<int> splineChildren;
            for (int child : direct)
                if (isSpline(child))
                    splineChildren.push_back(child);
            if (splineChildren.size() == 1)
                current = splineChildren[0];
        }

        for (int guard = 0; guard < skeleton.GetBoneCount(); guard++)
        {
            result.push_back(skeleton.bones[current].name);
            const std::vector<int> next = children(current);
            if (next.empty())
                break;
            int child = next.size() == 1 ? next[0] : -1;
            if (child < 0)
            {
                for (int candidate : next)
                    if (isSpline(candidate))
                    {
                        if (child >= 0)
                        {
                            child = -1;
                            break;
                        }
                        child = candidate;
                    }
            }
            if (child < 0)
                break;
            current = child;
        }
        return result;
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
                                 const vec3 &scale, int pivotMode)
    {
        if (base.size() != m_bones.size())
            return;
        vec3 lo(std::numeric_limits<float>::max()), hi = -lo;
        for (const RigBone &b : base)
        {
            lo = glm::min(lo, glm::min(b.head, b.tail));
            hi = glm::max(hi, glm::max(b.head, b.tail));
        }
        const vec3 centre = (lo + hi) * 0.5f;
        const vec3 pivot = pivotMode == 0 ? vec3(centre.x, lo.y, centre.z) : pivotMode == 1 ? centre
                                                                                            : vec3(0.f);
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
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
        ImGui::Combo("Pivot", &m_xformPivot, "Feet\0Centre\0Origin\0");
        ui::ItemTooltip("Point the rig rotates and scales around: the bottom centre of all bones, their centre, or the rig origin.");
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
        ImGui::SetNextItemWidth(-1.f);
        const bool nameEdited = ImGui::InputText("##name", m_nameBuf, sizeof(m_nameBuf));
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

        const char *parentName = b.parent >= 0 ? m_bones[b.parent].name.c_str() : "(none)";
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##parent", parentName))
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
        if (m_poseSelected < 0 || m_poseSelected >= skeleton.GetBoneCount())
            return;

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const size_t stride = std::max<size_t>(1, posedVertices.size() / 12000);
        const vec3 grey(0.35f), orange(1.f, 0.55f, 0.1f), purple(0.55f, 0.2f, 0.9f);
        for (size_t i = 0; i < posedVertices.size(); i += stride)
        {
            float weight = 0.f;
            for (int k = 0; k < 4; k++)
                if (weights[i].joints[k] == static_cast<uint32_t>(m_poseSelected))
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
                stroke.bone = skeleton.bones[m_poseSelected].name;
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

    void RigEditor::DrawIkViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                   const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                                   const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered,
                                   bool &active)
    {
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline || !camera || !m_model)
            return;
        const Skeleton &skeleton = m_model->GetSkeleton();
        const int tip = m_poseSelected;
        const int mid = tip >= 0 && tip < skeleton.GetBoneCount() ? skeleton.bones[tip].parentIndex : -1;
        const int root = mid >= 0 && mid < skeleton.GetBoneCount() ? skeleton.bones[mid].parentIndex : -1;
        if (root < 0 || root >= skeleton.GetBoneCount() || static_cast<int>(boneTransforms.size()) != skeleton.GetBoneCount())
        {
            m_twoBoneIk = false;
            m_status = "Two-Bone IK needs a selected tip with a parent and grandparent.";
            return;
        }

        const vec3 rootPosition = vec3(boneTransforms[root][3]);
        const vec3 midPosition = vec3(boneTransforms[mid][3]);
        const vec3 tipPosition = vec3(boneTransforms[tip][3]);
        if (m_ikBone != tip)
        {
            m_ikBone = tip;
            m_ikTarget = tipPosition;
            vec3 bend;
            BendDirection(rootPosition, midPosition, tipPosition, nullptr, bend);
            m_ikPole = midPosition + bend * std::max(glm::length(tipPosition - rootPosition) * 0.5f, 0.05f);
            m_ikDragging = false;
            m_ikDirty = false;
        }

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImGuizmo::SetOrthographic(camera->IsOrthographic());
        ImGuizmo::SetDrawlist(drawList);
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
        mat4 handedness(1.f);
        handedness[2][2] = -1.f;
        const mat4 viewRH = handedness * camera->GetView() * handedness;
        const float nearPlane = std::max(camera->GetNearPlane(), 0.001f), farPlane = 1000.f;
        mat4 projectionRH;
        if (camera->IsOrthographic())
        {
            const float halfHeight = std::max(camera->GetOrthographicSize(), 0.001f) * 0.5f;
            const float halfWidth = halfHeight * camera->GetAspect();
            projectionRH = glm::orthoRH_NO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        }
        else
            projectionRH = glm::perspectiveRH_NO(camera->Fovy(), camera->GetAspect(), nearPlane, farPlane);
        projectionRH[1][1] *= -1.f;

        const bool rightMouse = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(!rightMouse);
        mat4 targetWorldRH = handedness * (rootWorld * glm::translate(mat4(1.f), m_ikTarget)) * handedness;
        ImGuizmo::PushID("RigIkTarget");
        const bool targetChanged = ImGuizmo::Manipulate(glm::value_ptr(viewRH), glm::value_ptr(projectionRH),
                                                        ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                                                        glm::value_ptr(targetWorldRH));
        const bool targetHovered = ImGuizmo::IsOver();
        const bool targetActive = ImGuizmo::IsUsing();
        ImGuizmo::PopID();
        if (targetChanged)
        {
            const mat4 world = handedness * targetWorldRH * handedness;
            m_ikTarget = vec3(glm::inverse(rootWorld) * world[3]);
        }

        mat4 poleWorldRH = handedness * (rootWorld * glm::translate(mat4(1.f), m_ikPole)) * handedness;
        ImGuizmo::PushID("RigIkPole");
        const bool poleChanged = ImGuizmo::Manipulate(glm::value_ptr(viewRH), glm::value_ptr(projectionRH),
                                                      ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                                                      glm::value_ptr(poleWorldRH));
        const bool poleHovered = ImGuizmo::IsOver();
        const bool poleActive = ImGuizmo::IsUsing();
        ImGuizmo::PopID();
        if (poleChanged)
        {
            const mat4 world = handedness * poleWorldRH * handedness;
            m_ikPole = vec3(glm::inverse(rootWorld) * world[3]);
        }
        ImGuizmo::Enable(true);

        const bool gizmoActive = targetActive || poleActive;
        if (gizmoActive)
            m_ikDragging = true;
        m_ikDirty = m_ikDirty || targetChanged || poleChanged;
        hovered = hovered || targetHovered || poleHovered;
        active = gizmoActive || m_ikDragging;

        const AnimationPoseTools::TwoBoneIkInput input{rootPosition, midPosition, tipPosition, m_ikTarget, m_ikPole};
        const AnimationPoseTools::TwoBoneIkResult result = AnimationPoseTools::SolveTwoBoneIk(input);
        if (result)
        {
            ImVec2 rootPoint, midPoint, tipPoint, targetPoint, polePoint;
            const bool rootVisible = project(rootPosition, rootPoint);
            const bool midVisible = project(result.solvedMidPosition, midPoint);
            const bool tipVisible = project(result.solvedTipPosition, tipPoint);
            const bool targetVisible = project(m_ikTarget, targetPoint);
            const bool poleVisible = project(m_ikPole, polePoint);
            if (rootVisible && midVisible)
                drawList->AddLine(rootPoint, midPoint, IM_COL32(255, 214, 70, 255), 4.f);
            if (midVisible && tipVisible)
                drawList->AddLine(midPoint, tipPoint, IM_COL32(255, 214, 70, 255), 4.f);
            if (rootVisible)
                drawList->AddCircleFilled(rootPoint, 4.f, IM_COL32(255, 214, 70, 255), 12);
            if (midVisible)
                drawList->AddCircleFilled(midPoint, 4.f, IM_COL32(255, 214, 70, 255), 12);
            if (tipVisible)
                drawList->AddCircleFilled(tipPoint, 5.f, IM_COL32(255, 214, 70, 255), 12);
            if (midVisible && poleVisible)
                drawList->AddLine(midPoint, polePoint, IM_COL32(150, 255, 190, 155), 1.5f);
            if (targetVisible)
                drawList->AddText({targetPoint.x + 8.f, targetPoint.y + 8.f}, IM_COL32(255, 214, 70, 255),
                                  result.targetClamped ? "IK Target (clamped)" : "IK Target");
            if (poleVisible)
                drawList->AddText({polePoint.x + 8.f, polePoint.y + 8.f}, IM_COL32(150, 255, 190, 255), "Pole");
            if (result.targetClamped && tipVisible && targetVisible)
                drawList->AddLine(tipPoint, targetPoint, IM_COL32(255, 90, 90, 220), 2.f);
        }

        if (m_ikDragging && !gizmoActive)
        {
            if (m_ikDirty)
            {
                if (!result)
                {
                    const char *reason = result.status == AnimationPoseTools::TwoBoneIkStatus::DegenerateUpperBone
                                             ? "upper link has zero length"
                                         : result.status == AnimationPoseTools::TwoBoneIkStatus::DegenerateLowerBone
                                             ? "lower link has zero length"
                                             : "input is not finite";
                    m_status = std::string("Two-Bone IK failed: ") + reason + ".";
                }
                else
                {
                    const quat rootRotation = result.rootGlobalDelta * RotationOf(boneTransforms[root]);
                    const quat midRotation = result.midGlobalDelta * result.rootGlobalDelta * RotationOf(boneTransforms[mid]);
                    const std::array<AnimationTimeline::GlobalBoneRotation, 2> rotations = {
                        AnimationTimeline::GlobalBoneRotation{root, rootRotation},
                        AnimationTimeline::GlobalBoneRotation{mid, midRotation},
                    };
                    if (timeline->KeyViewportGlobalRotations(scene, m_model, rotations))
                    {
                        m_status = result.targetClamped
                                       ? "Two-Bone IK keyed as one edit; target was outside chain reach and was clamped."
                                       : "Two-Bone IK keyed as one edit at the current frame.";
                        SolveLocks(scene); // the tip's own lock reapplies too: locks always win, like a grab release
                    }
                    else
                        m_status = "Two-Bone IK keys were rejected; select a valid active clip target.";
                }
            }
            m_ikDragging = false;
            m_ikDirty = false;
            active = false;
        }
    }

    // -------------------------------------------------------------------------
    // locks
    // -------------------------------------------------------------------------
    void RigEditor::PoseTails(const Skeleton &skeleton, std::span<const mat4> boneTransforms, std::vector<vec3> &heads,
                              std::vector<vec3> &tails, std::vector<vec3> *restTailsOut) const
    {
        const int boneCount = skeleton.GetBoneCount();
        const mat4 invSkeletonRoot = glm::inverse(skeleton.rootTransform);
        std::vector<vec3> restHeads(boneCount), restTails(boneCount);
        heads.resize(boneCount);
        tails.resize(boneCount);
        for (int i = 0; i < boneCount; i++)
        {
            restHeads[i] = vec3((invSkeletonRoot * glm::inverse(skeleton.bones[i].offsetMatrix))[3]);
            heads[i] = vec3(boneTransforms[i][3]);
        }
        for (int i = 0; i < boneCount; i++)
        {
            const int authored = FindBone(skeleton.bones[i].name);
            if (authored >= 0)
                restTails[i] = m_bones[authored].tail;
            else
            {
                vec3 children(0.f);
                int childCount = 0;
                for (int child = 0; child < boneCount; child++)
                    if (skeleton.bones[child].parentIndex == i)
                        children += restHeads[child], childCount++;
                if (childCount > 0)
                    restTails[i] = children / static_cast<float>(childCount);
                else if (skeleton.bones[i].parentIndex >= 0)
                    restTails[i] = restHeads[i] + (restHeads[i] - restHeads[skeleton.bones[i].parentIndex]) * 0.5f;
                else
                    restTails[i] = restHeads[i] + vec3(0.f, ModelHeight() * 0.1f, 0.f);
            }
            const mat4 bind = invSkeletonRoot * glm::inverse(skeleton.bones[i].offsetMatrix);
            tails[i] = vec3(boneTransforms[i] * glm::inverse(bind) * vec4(restTails[i], 1.f));
        }
        if (restTailsOut)
            *restTailsOut = std::move(restTails);
    }

    bool RigEditor::LockChain(const RigLock &lock, const Skeleton &skeleton, int &root, int &mid, int &target,
                              std::string *why) const
    {
        mid = skeleton.GetBoneIndex(lock.bone);
        root = mid >= 0 ? skeleton.bones[mid].parentIndex : -1;
        target = lock.target.empty() ? -1 : skeleton.GetBoneIndex(lock.target);
        const char *reason = mid < 0                              ? "unknown bone"
                             : root < 0                           ? "the bone has no parent to bend"
                             : !lock.target.empty() && target < 0 ? "unknown target"
                                                                  : nullptr;
        for (int b = target; !reason && b >= 0; b = skeleton.bones[b].parentIndex)
            if (b == mid || b == root)
                reason = "the target moves with this chain";
        if (reason && why)
            *why = reason;
        return !reason;
    }

    bool RigEditor::LockAnchorPosed(const RigLock &lock, const Skeleton &skeleton, std::span<const mat4> boneTransforms,
                                    vec3 &out) const
    {
        if (lock.target.empty())
        {
            out = lock.anchor;
            return true;
        }
        const int target = skeleton.GetBoneIndex(lock.target);
        if (target < 0 || target >= static_cast<int>(boneTransforms.size()))
            return false;
        const mat4 bind = glm::inverse(skeleton.rootTransform) * glm::inverse(skeleton.bones[target].offsetMatrix);
        out = vec3(boneTransforms[target] * glm::inverse(bind) * vec4(lock.anchor, 1.f));
        return true;
    }

    int RigEditor::AddLock(const std::string &bone, const std::string &target, float reach, const vec3 *anchor,
                           std::string &error)
    {
        if (!m_model || !m_model->HasSkeleton())
        {
            error = "the model has no skeleton to lock";
            return -1;
        }
        const Skeleton &skeleton = m_model->GetSkeleton();
        const int boneCount = skeleton.GetBoneCount();
        RigLock lock;
        lock.bone = bone;
        lock.target = target;
        lock.reach = std::clamp(reach, 0.3f, 1.f);
        int root, mid, targetIndex;
        if (!LockChain(lock, skeleton, root, mid, targetIndex, &error))
            return -1;
        for (const RigLock &existing : m_locks)
            if (existing.bone == bone)
            {
                error = "the bone already has a lock; edit or remove it first";
                return -1;
            }
        if (anchor)
            lock.anchor = *anchor;
        else
        {
            // Pin the tail where it is now: the Timeline pose when there is one, else the rest pose.
            AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
            AnimationTimeline::ViewportPose pose;
            if (!timeline || !timeline->GetViewportPose(m_model, pose) ||
                static_cast<int>(pose.boneTransforms.size()) != boneCount)
            {
                pose.boneTransforms.resize(boneCount);
                for (int i = 0; i < boneCount; i++)
                    pose.boneTransforms[i] = glm::inverse(skeleton.rootTransform) * glm::inverse(skeleton.bones[i].offsetMatrix);
            }
            std::vector<vec3> heads, tails;
            PoseTails(skeleton, pose.boneTransforms, heads, tails);
            if (targetIndex >= 0)
            {
                const mat4 bind = glm::inverse(skeleton.rootTransform) * glm::inverse(skeleton.bones[targetIndex].offsetMatrix);
                lock.anchor = vec3(bind * glm::inverse(pose.boneTransforms[targetIndex]) * vec4(tails[mid], 1.f));
            }
            else
                lock.anchor = tails[mid];
        }
        PushUndo(true);
        m_locks.push_back(std::move(lock));
        m_lockBend.clear();
        m_dirty = true;
        return static_cast<int>(m_locks.size()) - 1;
    }

    void RigEditor::SolveLockRotations(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int skipBone,
                                       std::vector<LockSolve> &out)
    {
        out.clear();
        if (m_lockBend.size() != m_locks.size())
            m_lockBend.assign(m_locks.size(), vec3(0.f));
        std::vector<vec3> heads, tails;
        PoseTails(skeleton, boneTransforms, heads, tails);
        std::vector<char> used(skeleton.GetBoneCount(), 0);
        for (int i = 0; i < static_cast<int>(m_locks.size()); i++)
        {
            const RigLock &lock = m_locks[i];
            int root, mid, target;
            vec3 anchor;
            if (!lock.enabled || !LockChain(lock, skeleton, root, mid, target) || mid == skipBone || root == skipBone ||
                !LockAnchorPosed(lock, skeleton, boneTransforms, anchor))
                continue;
            // ponytail: locks sharing a bone solve from the same pose and would overwrite each other in
            // the keyed result; the first lock wins deterministically. Sequential re-posed solves are the upgrade.
            if (used[root] || used[mid])
                continue;
            const vec3 rootPos = heads[root], midPos = heads[mid], tipPos = tails[mid];
            const float reach = std::clamp(lock.reach, 0.3f, 1.f) *
                                (glm::distance(rootPos, midPos) + glm::distance(midPos, tipPos));
            LockSolve solve;
            solve.lock = i;
            solve.root = root;
            solve.mid = mid;
            vec3 goal = anchor;
            const vec3 toAnchor = anchor - rootPos;
            const float distance = glm::length(toAnchor);
            if (distance > reach && distance > 1e-6f)
            {
                goal = rootPos + toAnchor * (reach / distance);
                solve.clamped = true;
            }
            vec3 bend;
            BendDirection(rootPos, midPos, tipPos, &m_lockBend[i], bend);
            const vec3 pole = midPos + bend * std::max(glm::distance(rootPos, tipPos) * 0.5f, 0.05f);
            const AnimationPoseTools::TwoBoneIkResult result =
                AnimationPoseTools::SolveTwoBoneIk({rootPos, midPos, tipPos, goal, pole});
            if (!result)
                continue;
            used[root] = used[mid] = 1; // claimed only by a solve that actually lands
            solve.clamped = solve.clamped || result.targetClamped;
            solve.rootRotation = result.rootGlobalDelta * RotationOf(boneTransforms[root]);
            solve.midRotation = result.midGlobalDelta * result.rootGlobalDelta * RotationOf(boneTransforms[mid]);
            out.push_back(solve);
        }
    }

    bool RigEditor::SolveLocks(Scene &scene, int skipBone, bool pushUndo, float frame)
    {
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline || !m_model || !m_model->HasSkeleton() || m_locks.empty())
            return false;
        const Skeleton &skeleton = m_model->GetSkeleton();
        AnimationTimeline::ViewportPose pose;
        const bool sampled = frame >= 0.f ? timeline->SampleViewportPoseAtFrame(m_model, frame, pose)
                                          : timeline->GetViewportPose(m_model, pose);
        if (!sampled || static_cast<int>(pose.boneTransforms.size()) != skeleton.GetBoneCount())
            return false;
        std::vector<LockSolve> solves;
        SolveLockRotations(skeleton, pose.boneTransforms, skipBone, solves);
        if (solves.empty())
            return false;
        std::vector<AnimationTimeline::GlobalBoneRotation> rotations;
        std::string outOfReach;
        for (const LockSolve &solve : solves)
        {
            rotations.push_back({solve.root, solve.rootRotation});
            rotations.push_back({solve.mid, solve.midRotation});
            if (solve.clamped)
                outOfReach += (outOfReach.empty() ? "" : ", ") + m_locks[solve.lock].bone;
        }
        // pushUndo rides Key's own success path: a rejected batch costs no undo step and no snapshot.
        if (!timeline->KeyViewportGlobalRotations(scene, m_model, rotations, frame, pushUndo))
            return false;
        m_status = outOfReach.empty() ? "Locks held (" + std::to_string(solves.size()) + ")."
                                      : "Locks solved; out of reach: " + outOfReach + ".";
        return true;
    }

    bool RigEditor::BakeLocks(Scene &scene, std::string &status)
    {
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        float endFrame = 0.f;
        if (!timeline || !m_model || !m_model->HasSkeleton() || !timeline->GetClipEndFrame(m_model, endFrame))
        {
            status = "Bake Locks needs the model's clip active in the Animation Timeline.";
            return false;
        }
        if (m_locks.empty())
        {
            status = "No locks to bake.";
            return false;
        }
        const Skeleton &skeleton = m_model->GetSkeleton();
        m_lockBend.clear();
        // Sample every frame from the clip as it is now: keying frame N must not feed the poses solved
        // for the frames after it (sparse clips would interpolate from the fresh lock keys and drift).
        std::vector<AnimationTimeline::ViewportPose> poses;
        // <= endFrame with float fuzz only: a 48.6-frame clip must not get a key at frame 49; capped so a
        // runaway End field cannot pre-sample poses for a million frames.
        const int lastBakeFrame = std::min(static_cast<int>(endFrame + 1e-3f), 20000);
        for (int frame = 0; frame <= lastBakeFrame; frame++)
        {
            AnimationTimeline::ViewportPose pose;
            if (!timeline->SampleViewportPoseAtFrame(m_model, static_cast<float>(frame), pose) ||
                static_cast<int>(pose.boneTransforms.size()) != skeleton.GetBoneCount())
                break;
            poses.push_back(std::move(pose));
        }
        bool pushed = false;
        int frames = 0, clamped = 0;
        std::vector<LockSolve> solves;
        std::vector<AnimationTimeline::GlobalBoneRotation> rotations;
        for (int frame = 0; frame < static_cast<int>(poses.size()); frame++)
        {
            SolveLockRotations(skeleton, poses[frame].boneTransforms, -1, solves);
            if (solves.empty())
                continue;
            rotations.clear();
            for (const LockSolve &solve : solves)
            {
                rotations.push_back({solve.root, solve.rootRotation});
                rotations.push_back({solve.mid, solve.midRotation});
                clamped += solve.clamped ? 1 : 0;
            }
            if (timeline->KeyViewportGlobalRotations(scene, m_model, rotations, static_cast<float>(frame), !pushed))
            {
                pushed = true; // the first landed frame carries the whole bake's undo snapshot
                frames++;
            }
        }
        status = "Baked locks on " + std::to_string(frames) + " frames (" + std::to_string(clamped) +
                 " out of reach); Timeline Save persists them.";
        if (static_cast<int>(endFrame + 1e-3f) > lastBakeFrame)
            status += " Capped at 20000 frames.";
        return frames > 0;
    }

    void RigEditor::DrawLocksPanel(Scene &scene, AnimationTimeline *timeline)
    {
        const Skeleton &skeleton = m_model->GetSkeleton();
        const float kReachWidth = ImGui::GetFontSize() * 5.f;
        ImGui::Separator();
        ImGui::TextDisabled("Locks");
        ui::ItemTooltip("Pin a bone's tail to a point on another bone (a hand on the shovel) or to a fixed rig-space point (a "
                        "planted foot). The bone and its parent bend to keep the pin after every pose edit; Reach caps how "
                        "far they may straighten. Lock keys are written even with Auto Key off.");
        for (int i = 0; i < static_cast<int>(m_locks.size()); i++)
        {
            RigLock &lock = m_locks[i];
            ImGui::PushID(i);
            bool enabled = lock.enabled;
            if (ImGui::Checkbox("##lock_enabled", &enabled))
            {
                PushUndo(true);
                lock.enabled = enabled;
                m_dirty = true;
            }
            ui::ItemTooltip("Solve this lock.");
            int root, mid, target;
            std::string why;
            const bool valid = LockChain(lock, skeleton, root, mid, target, &why);
            ImGui::SameLine();
            ImGui::TextDisabled("%s -> %s%s%s", lock.bone.c_str(), lock.target.empty() ? "rig space" : lock.target.c_str(),
                                valid ? "" : " (", valid ? "" : (why + ")").c_str());
            ui::SameLineIfFits(ui::LabelledItemWidth(kReachWidth, "Reach") + ui::ButtonWidth("X"));
            ImGui::SetNextItemWidth(kReachWidth);
            float reach = lock.reach;
            const bool reachChanged = ImGui::SliderFloat("Reach", &reach, 0.3f, 1.f, "%.2f");
            const bool reachReleased = ImGui::IsItemDeactivated();
            ui::ItemTooltip("Fraction of the straight bone pair the tail may reach; below 1 keeps the joint from locking out.");
            if (reachChanged)
            {
                if (!m_reachDragging) // one undo pair per drag, and only when the value really changes
                {
                    PushUndo(true);
                    m_reachDragging = true;
                    m_reachPushed = false;
                }
                lock.reach = reach;
                m_dirty = true;
                if (SolveLocks(scene, -1, !m_reachPushed)) // the Timeline undo lands with the first real solve
                    m_reachPushed = true;
            }
            if (reachReleased)
                m_reachDragging = false;
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                PushUndo(true);
                m_locks.erase(m_locks.begin() + i);
                m_lockBend.clear();
                m_dirty = true;
                ImGui::PopID();
                break;
            }
            ui::ItemTooltip("Remove this lock (keys already written stay).");
            ImGui::PopID();
        }

        const bool haveBone = m_poseSelected >= 0 && m_poseSelected < skeleton.GetBoneCount();
        if (m_lockTarget >= skeleton.GetBoneCount())
            m_lockTarget = -1;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
        if (ImGui::BeginCombo("##lock_target", m_lockTarget < 0 ? "Rig space" : skeleton.bones[m_lockTarget].name.c_str()))
        {
            if (ImGui::Selectable("Rig space", m_lockTarget < 0))
                m_lockTarget = -1;
            for (int i = 0; i < skeleton.GetBoneCount(); i++)
                if (i != m_poseSelected && ImGui::Selectable(skeleton.bones[i].name.c_str(), m_lockTarget == i))
                    m_lockTarget = i;
            ImGui::EndCombo();
        }
        ui::ItemTooltip("What the selected bone's tail is pinned to: a bone it rides on, or a fixed point in rig space.");
        ui::SameLineIfFits(kReachWidth);
        ImGui::SetNextItemWidth(kReachWidth);
        ImGui::SliderFloat("##lock_reach", &m_lockReach, 0.3f, 1.f, "Reach %.2f");
        ui::ItemTooltip("Reach limit for the new lock.");
        ui::SameLineIfFits(ui::ButtonWidth("Add Lock"));
        ImGui::BeginDisabled(!haveBone);
        if (ImGui::Button("Add Lock"))
        {
            std::string error;
            const int added = AddLock(skeleton.bones[m_poseSelected].name,
                                      m_lockTarget < 0 ? "" : skeleton.bones[m_lockTarget].name, m_lockReach, nullptr, error);
            m_status = added >= 0 ? "Locked " + m_locks[added].bone + " where its tail is now." : "Add Lock failed: " + error;
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(haveBone ? "Pin the selected bone's tail where it is now." : "Select a bone in the Pose Bones tree first.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::BeginDisabled(m_locks.empty());
        if (ImGui::Button("Solve Locks"))
        {
            if (!SolveLocks(scene, -1, true))
                m_status = "Nothing to solve: locks need the clip active in the Timeline and a valid chain.";
        }
        ui::ItemTooltip("Re-solve every enabled lock at the current frame (after scrubbing to a frame without lock keys).",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ui::SameLineIfFits(ui::ButtonWidth("Bake Locks To Clip"));
        if (ImGui::Button("Bake Locks To Clip"))
            BakeLocks(scene, m_status);
        ui::ItemTooltip("Solve every enabled lock on every frame of the active clip (mocap hands onto the shovel, feet onto the "
                        "floor) as one undo step.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Rig Undo"))
            Undo();
        ui::ItemTooltip("Undo the last rig document edit: a lock, pin or reach change (bone geometry is untouched in Pose mode).");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rig Redo"))
            Redo();
        ui::ItemTooltip("Redo the last undone rig document edit.");
        ImGui::Separator();
    }

    // -------------------------------------------------------------------------
    // grab + pins
    // -------------------------------------------------------------------------
    namespace
    {
        quat RotationBetween(const vec3 &from, const vec3 &to)
        {
            const float lengths = glm::length(from) * glm::length(to);
            if (lengths <= 1e-12f)
                return quat(1.f, 0.f, 0.f, 0.f);
            const float cosine = std::clamp(glm::dot(from, to) / lengths, -1.f, 1.f);
            if (cosine > 1.f - 1e-6f)
                return quat(1.f, 0.f, 0.f, 0.f);
            if (cosine < -1.f + 1e-6f)
            {
                const vec3 f = glm::normalize(from);
                const vec3 axis = std::abs(f.x) < 0.9f ? glm::normalize(glm::cross(f, vec3(1.f, 0.f, 0.f)))
                                                       : glm::normalize(glm::cross(f, vec3(0.f, 1.f, 0.f)));
                return glm::angleAxis(glm::pi<float>(), axis);
            }
            return glm::normalize(glm::angleAxis(std::acos(cosine), glm::normalize(glm::cross(from, to))));
        }
    } // namespace

    bool RigEditor::IsPinned(const std::string &bone) const
    {
        return std::find(m_pins.begin(), m_pins.end(), bone) != m_pins.end();
    }

    void RigEditor::TogglePin(const std::string &bone)
    {
        PushUndo(true);
        const auto at = std::find(m_pins.begin(), m_pins.end(), bone);
        if (at == m_pins.end())
            m_pins.push_back(bone);
        else
            m_pins.erase(at);
        m_dirty = true;
    }

    void RigEditor::GrabSolve(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone, const vec3 &target,
                              std::vector<std::pair<int, quat>> &out) const
    {
        out.clear();
        if (bone < 0 || bone >= skeleton.GetBoneCount() || static_cast<int>(boneTransforms.size()) != skeleton.GetBoneCount())
            return;
        std::vector<vec3> heads, tails;
        PoseTails(skeleton, boneTransforms, heads, tails);
        // chain[0] = the grabbed bone, then its ancestors up to (excluding) the first pin or the skeleton root
        std::vector<int> chain;
        for (int b = bone; b >= 0 && skeleton.bones[b].parentIndex >= 0; b = skeleton.bones[b].parentIndex)
        {
            if (b != bone && IsPinned(skeleton.bones[b].name))
                break;
            chain.push_back(b);
        }
        if (chain.empty())
            return;
        std::vector<vec3> pivots(chain.size());
        std::vector<quat> deltas(chain.size(), quat(1.f, 0.f, 0.f, 0.f));
        for (size_t k = 0; k < chain.size(); k++)
            pivots[k] = heads[chain[k]];
        vec3 effector = tails[bone];
        // ponytail: CCD with stiffer parents (hand and forearm free, shoulder 0.35, torso and beyond 0.2 per pass) gives
        // "arm first, then shoulder, then torso" without joint limits; add per-bone limits if poses start folding wrong.
        auto weight = [](size_t k)
        { return k < 2 ? 1.f : k == 2 ? 0.35f
                                      : 0.2f; };
        for (int pass = 0; pass < 16; pass++)
        {
            if (glm::distance(effector, target) < 1e-4f)
                break;
            for (size_t k = 0; k < chain.size(); k++)
            {
                const vec3 pivot = pivots[k];
                const vec3 have = effector - pivot, want = target - pivot;
                if (glm::dot(have, have) < 1e-10f || glm::dot(want, want) < 1e-10f)
                    continue;
                quat q = RotationBetween(have, want);
                const float w = weight(k);
                if (w < 1.f)
                    q = glm::normalize(glm::slerp(quat(1.f, 0.f, 0.f, 0.f), q, w));
                for (size_t j = 0; j <= k; j++)
                {
                    deltas[j] = q * deltas[j];
                    if (j < k)
                        pivots[j] = pivot + q * (pivots[j] - pivot);
                }
                effector = pivot + q * (effector - pivot);
            }
        }
        for (size_t k = 0; k < chain.size(); k++)
            if (std::abs(deltas[k].w) < 1.f - 1e-6f) // identity deltas key nothing: a still click must not freeze the chain
                out.emplace_back(chain[k], glm::normalize(deltas[k] * RotationOf(boneTransforms[chain[k]])));
    }

    bool RigEditor::GrabTo(Scene &scene, int bone, const vec3 &target, float *gap, bool pushUndo, bool *keyedOut)
    {
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline || !m_model || !m_model->HasSkeleton())
            return false;
        const Skeleton &skeleton = m_model->GetSkeleton();
        AnimationTimeline::ViewportPose pose;
        if (!timeline->GetViewportPose(m_model, pose) ||
            static_cast<int>(pose.boneTransforms.size()) != skeleton.GetBoneCount())
            return false;
        std::vector<std::pair<int, quat>> solved;
        GrabSolve(skeleton, pose.boneTransforms, bone, target, solved);
        if (solved.empty())
        {
            // Every delta was identity: the tail already sits on the target (a still click, or an agent
            // re-grabbing the same point). Nothing to key, but the grab itself is satisfied.
            std::vector<vec3> heads, tails;
            PoseTails(skeleton, pose.boneTransforms, heads, tails);
            const bool atTarget = bone >= 0 && bone < static_cast<int>(tails.size()) &&
                                  glm::distance(tails[bone], target) < std::max(ModelHeight(), 0.1f) * 0.01f;
            if (atTarget && gap)
                *gap = glm::distance(tails[bone], target);
            return atTarget;
        }
        std::vector<AnimationTimeline::GlobalBoneRotation> rotations;
        for (const auto &[b, rotation] : solved)
            rotations.push_back({b, rotation});
        // pushUndo rides Key's own success path: a rejected batch costs no undo step and no snapshot.
        if (!timeline->KeyViewportGlobalRotations(scene, m_model, rotations, -1.f, pushUndo))
            return false;
        if (keyedOut)
            *keyedOut = true;
        SolveLocks(scene, bone);
        if (gap && timeline->GetViewportPose(m_model, pose))
        {
            std::vector<vec3> heads, tails;
            PoseTails(skeleton, pose.boneTransforms, heads, tails);
            *gap = glm::distance(tails[bone], target);
        }
        return true;
    }

    void RigEditor::DrawPadlock(ImDrawList *drawList, const ImVec2 &centre, bool closed, ImU32 colour)
    {
        const float s = 4.f; // half width of the body
        drawList->AddRectFilled({centre.x - s, centre.y}, {centre.x + s, centre.y + s * 1.5f}, colour, 1.f);
        const ImVec2 arcCentre = closed ? ImVec2(centre.x, centre.y) : ImVec2(centre.x + s * 0.6f, centre.y - s * 0.4f);
        drawList->PathArcTo(arcCentre, s * 0.65f, glm::pi<float>(), glm::two_pi<float>(), 10);
        drawList->PathStroke(colour, 0, 1.5f);
    }

    void RigEditor::DrawPoseViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                     bool &hovered, bool &active)
    {
        hovered = false;
        active = false;
        AnimationTimeline *timeline = m_gui ? m_gui->GetWidget<AnimationTimeline>() : nullptr;
        if (!timeline || !m_model || !camera || !m_model->HasSkeleton())
            return;

        // Pose-bar / timeline.pose edits land in the Timeline; their locks are solved here, once per edit.
        if (timeline->PoseEditSerial() != m_poseEditSerial)
        {
            m_poseEditSerial = timeline->PoseEditSerial();
            if (m_grabBone < 0 && !m_poseDragging)
                // Keys the frame the pose edit targeted (timeline.pose frame=N), not the playhead; an
                // in-flight drag re-solves on release instead, never fighting the mouse mid-frame.
                SolveLocks(scene, -1, false, timeline->PoseEditFrame());
        }
        AnimationTimeline::ViewportPose pose;
        if (!timeline->GetViewportPose(m_model, pose))
        {
            m_status = "Pose is waiting for the Animation Timeline target.";
            return;
        }
        const Skeleton &skeleton = m_model->GetSkeleton();
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount <= 0 || static_cast<int>(pose.boneTransforms.size()) != boneCount)
            return;
        if (m_poseSelected < 0 || m_poseSelected >= boneCount)
            m_poseSelected = 0;

        NodeId *poseNode = pose.node && scene.IsNodeAlive(pose.node) ? pose.node : m_rootNode;
        if (!poseNode || !scene.IsNodeAlive(poseNode))
            return;
        const mat4 rootWorld = scene.GetWorldMatrix(poseNode);
        const mat4 invRootWorld = glm::inverse(rootWorld);
        const mat4 viewProj = camera->GetProjectionNoJitter() * camera->GetView();
        const mat4 invSkeletonRoot = glm::inverse(skeleton.rootTransform);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetMousePos();

        auto project = [&](const vec3 &rig, ImVec2 &out)
        {
            const vec3 world = vec3(rootWorld * vec4(rig, 1.f));
            return ProjectWorldToViewportRect(world, viewProj, imageMin.x, imageMin.y, imageSize.x, imageSize.y, out.x, out.y);
        };
        auto pointSegmentDistance = [](const ImVec2 &p, const ImVec2 &a, const ImVec2 &b)
        {
            const ImVec2 ab(b.x - a.x, b.y - a.y);
            const float lengthSq = ab.x * ab.x + ab.y * ab.y;
            const float t = lengthSq > 1e-6f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSq, 0.f, 1.f) : 0.f;
            const float dx = p.x - (a.x + ab.x * t), dy = p.y - (a.y + ab.y * t);
            return std::sqrt(dx * dx + dy * dy);
        };

        std::vector<vec3> restTails, poseHeads, poseTails;
        std::vector<ImVec2> headScreen(boneCount), tailScreen(boneCount);
        std::vector<char> visible(boneCount, 0);
        PoseTails(skeleton, pose.boneTransforms, poseHeads, poseTails, &restTails);
        for (int i = 0; i < boneCount; i++)
            visible[i] = project(poseHeads[i], headScreen[i]) && project(poseTails[i], tailScreen[i]);

        // The reference belongs above the rendered scene but below every rig aid, so bones and gizmos stay legible.
        DrawReferenceOverlay(timeline, imageMin, imageSize);

        if (m_onionBones)
        {
            for (int frameOffset = -m_onionPrevious; frameOffset <= m_onionNext; ++frameOffset)
            {
                if (frameOffset == 0)
                    continue;
                AnimationTimeline::ViewportPose ghost;
                if (!timeline->SampleViewportPose(m_model, static_cast<float>(frameOffset), ghost) ||
                    static_cast<int>(ghost.boneTransforms.size()) != boneCount)
                    continue;
                const int range = frameOffset < 0 ? std::max(m_onionPrevious, 1) : std::max(m_onionNext, 1);
                const float fade = 1.f - 0.65f * static_cast<float>(std::abs(frameOffset) - 1) /
                                             static_cast<float>(range);
                const int alpha = static_cast<int>(std::clamp(fade, 0.2f, 1.f) * 105.f);
                const ImU32 color = frameOffset < 0 ? IM_COL32(80, 170, 255, alpha)
                                                    : IM_COL32(255, 100, 110, alpha);
                for (int bone = 0; bone < boneCount; ++bone)
                {
                    const mat4 bind = invSkeletonRoot * glm::inverse(skeleton.bones[bone].offsetMatrix);
                    const vec3 head = vec3(ghost.boneTransforms[bone][3]);
                    const vec3 tail = vec3(ghost.boneTransforms[bone] * glm::inverse(bind) * vec4(restTails[bone], 1.f));
                    ImVec2 headPoint, tailPoint;
                    if (!project(head, headPoint) || !project(tail, tailPoint))
                        continue;
                    drawList->AddLine(headPoint, tailPoint, color, bone == m_poseSelected ? 2.f : 1.f);
                    drawList->AddCircleFilled(headPoint, bone == m_poseSelected ? 2.5f : 1.5f, color, 8);
                }
            }
        }

        if (m_motionTrail && m_poseSelected >= 0 && m_poseSelected < boneCount)
        {
            bool havePrevious = false;
            ImVec2 previous;
            for (int frameOffset = -m_trailPrevious; frameOffset <= m_trailNext; ++frameOffset)
            {
                AnimationTimeline::ViewportPose trailPose;
                ImVec2 point;
                if (!timeline->SampleViewportPose(m_model, static_cast<float>(frameOffset), trailPose) ||
                    static_cast<int>(trailPose.boneTransforms.size()) != boneCount ||
                    !project(vec3(trailPose.boneTransforms[m_poseSelected][3]), point))
                {
                    havePrevious = false;
                    continue;
                }
                const ImU32 color = frameOffset == 0 ? kBoneSelCol : IM_COL32(255, 255, 255, 145);
                if (havePrevious)
                    drawList->AddLine(previous, point, IM_COL32(255, 255, 255, 120), 1.5f);
                drawList->AddCircleFilled(point, frameOffset == 0 ? 4.f : 2.5f, color, 10);
                previous = point;
                havePrevious = true;
            }
        }

        int hoveredBone = -1;
        float nearest = 8.f;
        const bool mouseInImage = mouse.x >= imageMin.x && mouse.y >= imageMin.y && mouse.x <= imageMin.x + imageSize.x &&
                                  mouse.y <= imageMin.y + imageSize.y;
        for (int i = 0; i < boneCount; i++)
        {
            if (!visible[i])
                continue;
            if (mouseInImage)
            {
                const float distance = pointSegmentDistance(mouse, headScreen[i], tailScreen[i]);
                if (distance < nearest)
                    nearest = distance, hoveredBone = i;
            }
            const bool selected = i == m_poseSelected;
            const ImU32 color = selected ? kBoneSelCol : kBoneCol;
            drawList->AddLine(headScreen[i], tailScreen[i], IM_COL32(0, 0, 0, 180), selected ? 7.f : 4.f);
            drawList->AddLine(headScreen[i], tailScreen[i], color, selected ? 4.f : 2.f);
            drawList->AddCircleFilled(headScreen[i], selected ? 5.f : 3.f, color, 12);
            if (selected)
                drawList->AddText({headScreen[i].x + 8.f, headScreen[i].y - 18.f}, kBoneSelCol, skeleton.bones[i].name.c_str());
        }
        for (const RigLock &lock : m_locks)
        {
            int root, mid, target;
            vec3 anchor;
            ImVec2 anchorScreen;
            if (!lock.enabled || !LockChain(lock, skeleton, root, mid, target) ||
                !LockAnchorPosed(lock, skeleton, pose.boneTransforms, anchor) || !project(anchor, anchorScreen))
                continue;
            const bool held = glm::distance(anchor, poseTails[mid]) <= std::max(ModelHeight(), 0.1f) * 0.01f;
            const ImU32 color = held ? kLockCol : IM_COL32(255, 90, 90, 230);
            if (!held && visible[mid])
                drawList->AddLine(tailScreen[mid], anchorScreen, color, 2.f);
            drawList->AddNgon(anchorScreen, 7.f, IM_COL32(0, 0, 0, 200), 4, 4.f);
            drawList->AddNgon(anchorScreen, 7.f, color, 4, 2.f);
            if (!held)
                drawList->AddText({anchorScreen.x + 9.f, anchorScreen.y + 6.f}, color, "out of reach");
        }

        // Grab handles (tail dots) and pin padlocks beside every head: pull a tail and the chain bends up to
        // the first pinned bone; click a padlock to pin / unpin. Plain Pose mode only (IK / Joint Blend own the mouse).
        if (!m_jointBlend && !m_twoBoneIk)
        {
            const bool gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            int hoveredTail = -1, hoveredPin = -1;
            float nearestTail = 9.f, nearestPin = 8.f;
            auto pinPos = [&](int i)
            { return ImVec2(headScreen[i].x - 12.f, headScreen[i].y - 12.f); };
            for (int i = 0; i < boneCount && mouseInImage && m_grabBone < 0 && !gizmoBusy; i++)
            {
                // Only what is drawn is pickable: root bones show no tail dot or padlock.
                if (!visible[i] || skeleton.bones[i].parentIndex < 0)
                    continue;
                const float dTail = std::hypot(mouse.x - tailScreen[i].x, mouse.y - tailScreen[i].y);
                if (dTail < nearestTail)
                    nearestTail = dTail, hoveredTail = i;
                const ImVec2 pin = pinPos(i);
                const float dPin = std::hypot(mouse.x - pin.x, mouse.y - pin.y);
                if (dPin < nearestPin)
                    nearestPin = dPin, hoveredPin = i;
            }
            for (int i = 0; i < boneCount; i++)
            {
                if (!visible[i] || skeleton.bones[i].parentIndex < 0)
                    continue;
                const bool pinned = IsPinned(skeleton.bones[i].name);
                DrawPadlock(drawList, pinPos(i), pinned,
                            pinned            ? IM_COL32(255, 170, 40, 255)
                            : i == hoveredPin ? IM_COL32(255, 255, 255, 230)
                                              : IM_COL32(255, 255, 255, 70));
                const bool grabbing = i == m_grabBone;
                drawList->AddCircleFilled(tailScreen[i], grabbing || i == hoveredTail ? 5.f : 3.f,
                                          grabbing ? IM_COL32(255, 170, 40, 255) : i == hoveredTail ? IM_COL32(255, 255, 255, 255)
                                                                                                    : IM_COL32(255, 255, 255, 120),
                                          12);
            }
            if (hoveredPin >= 0)
                ui::TooltipText(IsPinned(skeleton.bones[hoveredPin].name) ? "Pinned: pulls from its children stop here. Click to unpin."
                                                                          : "Click to pin: pulls from its children stop here.");
            else if (hoveredTail >= 0)
                ui::TooltipText("Drag to pull the limb; the chain bends up to the first pinned bone.");
            if (hoveredPin >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                TogglePin(skeleton.bones[hoveredPin].name);
                hovered = true;
                return;
            }

            vec3 rayOrigin(0.f), rayDir(0.f);
            const bool haveRay = imageSize.x > 0.f && imageSize.y > 0.f &&
                                 camera->BuildWorldRayFromNdc((mouse.x - imageMin.x) / imageSize.x * 2.f - 1.f,
                                                              (mouse.y - imageMin.y) / imageSize.y * 2.f - 1.f, rayOrigin, rayDir);
            if (m_grabBone < 0 && hoveredTail >= 0 && haveRay && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                vec3 hit;
                m_grabPlanePoint = vec3(rootWorld * vec4(poseTails[hoveredTail], 1.f));
                if (RayPlane(rayOrigin, rayDir, m_grabPlanePoint, camera->GetFront(), hit))
                {
                    m_grabBone = hoveredTail;
                    m_grabOffset = poseTails[hoveredTail] - vec3(invRootWorld * vec4(hit, 1.f));
                    m_poseSelected = hoveredTail;
                    m_grabPushed = false; // the Timeline undo lands with the first real grab key
                    timeline->RequestBone(skeleton.bones[hoveredTail].name);
                }
            }
            if (m_grabBone >= 0)
            {
                hovered = true;
                active = true;
                vec3 hit;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_grabBone >= boneCount)
                {
                    if (m_grabPushed)
                        SolveLocks(scene); // the grabbed bone's own lock waited for the release
                    m_grabBone = -1;
                    active = false;
                }
                else if (haveRay && RayPlane(rayOrigin, rayDir, m_grabPlanePoint, camera->GetFront(), hit))
                {
                    float gap = 0.f;
                    bool keyed = false;
                    if (GrabTo(scene, m_grabBone, vec3(invRootWorld * vec4(hit, 1.f)) + m_grabOffset, &gap, !m_grabPushed,
                               &keyed))
                    {
                        m_grabPushed = m_grabPushed || keyed;
                        m_status = gap > std::max(ModelHeight(), 0.1f) * 0.01f ? "Grab: chain cannot reach (pin further up, or pull less)."
                                                                               : "Grab keyed at the current frame.";
                    }
                }
                return;
            }
            hovered = hovered || hoveredTail >= 0 || hoveredPin >= 0;
            if (hoveredTail >= 0 || hoveredPin >= 0)
                hoveredBone = -1; // the click belongs to the handle, not the bone under it
        }

        if (m_jointBlend)
        {
            DrawJointBlendViewport(scene, camera, imageMin, imageSize, rootWorld, pose.boneTransforms, project, hovered,
                                   active);
            return;
        }
        if (m_twoBoneIk)
        {
            hovered = hoveredBone >= 0;
            DrawIkViewport(scene, camera, imageMin, imageSize, rootWorld, pose.boneTransforms, project, hovered, active);
            return;
        }

        ImGuizmo::SetOrthographic(camera->IsOrthographic());
        ImGuizmo::SetDrawlist(drawList);
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
        mat4 handedness(1.f);
        handedness[2][2] = -1.f;
        const mat4 viewRH = handedness * camera->GetView() * handedness;
        const float nearPlane = std::max(camera->GetNearPlane(), 0.001f), farPlane = 1000.f;
        mat4 projectionRH;
        if (camera->IsOrthographic())
        {
            const float halfHeight = std::max(camera->GetOrthographicSize(), 0.001f) * 0.5f;
            const float halfWidth = halfHeight * camera->GetAspect();
            projectionRH = glm::orthoRH_NO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        }
        else
            projectionRH = glm::perspectiveRH_NO(camera->Fovy(), camera->GetAspect(), nearPlane, farPlane);
        projectionRH[1][1] *= -1.f;

        const bool canRotate = timeline->CanViewportRotate(m_model, m_poseSelected, m_poseGizmo != 1, m_poseGizmo != 0);
        const bool rightMouse = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(canRotate && !rightMouse);
        mat4 worldRH = handedness * (rootWorld * pose.boneTransforms[m_poseSelected]) * handedness;
        const ImGuizmo::OPERATION operation = m_poseGizmo == 1   ? ImGuizmo::TRANSLATE
                                              : m_poseGizmo == 2 ? ImGuizmo::OPERATION(ImGuizmo::TRANSLATE | ImGuizmo::ROTATE)
                                                                 : ImGuizmo::ROTATE;
        const bool changed = ImGuizmo::Manipulate(glm::value_ptr(viewRH), glm::value_ptr(projectionRH), operation,
                                                  ImGuizmo::LOCAL, glm::value_ptr(worldRH));
        const bool gizmoHovered = ImGuizmo::IsOver();
        const bool gizmoActive = ImGuizmo::IsUsing();
        ImGuizmo::Enable(true);
        hovered = hoveredBone >= 0 || gizmoHovered;
        active = gizmoActive || m_poseDragging;

        if (changed)
        {
            if (!m_poseDragging)
                m_poseDragging =
                    timeline->BeginViewportRotate(scene, m_model, m_poseSelected, m_poseGizmo != 1, m_poseGizmo != 0);
            if (m_poseDragging)
            {
                const mat4 world = handedness * worldRH * handedness;
                const mat4 rigTransform = invRootWorld * world;
                int mirrorBone = -1;
                if (m_mirrorX)
                {
                    const std::string mirrorName = MirrorName(skeleton.bones[m_poseSelected].name);
                    if (!mirrorName.empty())
                        mirrorBone = skeleton.GetBoneIndex(mirrorName);
                }
                timeline->UpdateViewportRotate(scene, m_model, m_poseSelected, rigTransform, mirrorBone,
                                               m_poseGizmo != 1, m_poseGizmo != 0);
                m_status = m_mirrorX && mirrorBone >= 0 ? "Pose rotation keyed with mirrored counterpart."
                                                        : "Pose rotation keyed at the current frame.";
                SolveLocks(scene, m_poseSelected);
            }
        }
        if (m_poseDragging && !gizmoActive)
        {
            SolveLocks(scene); // the dragged bone's own locks, skipped during the drag
            timeline->EndViewportRotate();
            m_poseDragging = false;
            active = false;
        }

        if (hoveredBone >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoHovered && !gizmoActive)
        {
            m_poseSelected = hoveredBone;
            timeline->RequestBone(skeleton.bones[hoveredBone].name);
            m_status.clear();
        }
        if (!canRotate && gizmoHovered)
            ui::TooltipText("Enable Auto Key, or add a rotation key at this frame first.");
    }

    void RigEditor::DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                                 bool &active)
    {
        hovered = false;
        active = false;
        if (!m_open)
            return;
        ResolveTarget(scene);
        if (!m_model || !m_rootNode || !camera || !scene.IsNodeAlive(m_rootNode))
            return;
        if (m_mode == Mode::Pose)
        {
            DrawPoseViewport(scene, camera, imageMin, imageSize, hovered, active);
            return;
        }
        if (m_bones.empty())
            return;
        // Selecting a bone here (or in the tree) also makes it the Timeline's active bone, so the pose bar
        // edits the bone under the cursor. Runs here because this draws even when the widget's tab is hidden.
        if (m_selected != m_syncedSelected)
        {
            m_syncedSelected = m_selected;
            if (m_selected >= 0 && m_selected < static_cast<int>(m_bones.size()) && m_gui)
                if (AnimationTimeline *timeline = m_gui->GetWidget<AnimationTimeline>())
                    timeline->RequestBone(m_bones[m_selected].name);
        }

        const mat4 rootWorld = scene.GetWorldMatrix(m_rootNode);
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
        auto screenRadius = [&](const vec3 &rig, float radius, const ImVec2 &center) -> float
        {
            const vec3 world = vec3(rootWorld * vec4(rig, 1.f)) + camRight * (radius * rigScale);
            ImVec2 edge;
            if (!ProjectWorldToViewportRect(world, viewProj, imageMin.x, imageMin.y, imageSize.x, imageSize.y, edge.x, edge.y))
                return 0.f;
            return std::abs(edge.x - center.x);
        };

        // pass 1: shapes + bones
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            const RigBone &b = m_bones[i];
            ImVec2 hs, ts;
            if (!project(b.head, hs) || !project(b.tail, ts))
                continue;
            const bool selected = i == m_selected;
            const ImU32 boneCol = m_heat == 2 ? BoneColor(i) : selected ? kBoneSelCol
                                                                        : (b.spline ? kSplineCol : b.rigid ? kRigidCol
                                                                                                           : kBoneCol);
            if (m_showShapes && b.shell.empty())
            {
                const float rh = screenRadius(b.head, b.headRadius, hs), rt = screenRadius(b.tail, b.tailRadius, ts);
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
                st[k] = m_bones[chain[k]].head;
            st[n] = m_bones[chain[n - 1]].tail;
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
            if (!project(b.head, hs) || !project(b.tail, ts))
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
                {2, {hs.x + n.x * screenRadius(b.head, b.headRadius, hs), hs.y + n.y * screenRadius(b.head, b.headRadius, hs)}, selected && m_showShapes && b.shell.empty()},
                {3, {ts.x + n.x * screenRadius(b.tail, b.tailRadius, ts), ts.y + n.y * screenRadius(b.tail, b.tailRadius, ts)}, selected && m_showShapes && b.shell.empty()},
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
                    const vec3 anchor = h.id == 1 || h.id == 3 ? b.tail : h.id == 4 ? (b.head + b.tail) * 0.5f
                                                                                    : b.head;
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
                            m_dragFree = h.id == 1 ? b.tail : b.head;
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
                            if (snapping && SnapTarget(target, i, h.id, invRootWorld, rayOrigin, rayDir, project, mouse, snapPoint))
                            {
                                target = snapPoint;
                                ImVec2 sp;
                                if (project(snapPoint, sp))
                                    dl->AddCircle(sp, 9.f, kHandleHotCol, 16, 2.f);
                            }
                            if (h.id == 0)
                                b.head = target;
                            else if (h.id == 1)
                                MoveTail(i, target);
                            else
                            {
                                const vec3 move = target - b.head;
                                b.head = target;
                                MoveTail(i, b.tail + move);
                            }
                        }
                        if (m_mirrorX)
                            SyncMirror(i);
                        m_dirty = true;
                        if (axisLock >= 0)
                        {
                            // axis guide through the dragged point
                            const vec3 anchor = h.id == 1 ? b.tail : b.head;
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
                               const ImVec2 &mouse, vec3 &out) const
    {
        if (m_snapMode == 3)
        {
            out = glm::round(target / 0.01f) * 0.01f;
            return true;
        }
        if (m_snapMode == 1 || m_snapMode == 2)
        {
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
        for (int i = 0; i < static_cast<int>(m_bones.size()); i++)
        {
            if (i == bone)
                continue;
            consider(m_bones[i].head);
            consider(m_bones[i].tail);
        }
        if (handle == 0 && m_bones[bone].parent >= 0)
            consider(m_bones[m_bones[bone].parent].tail);
        return found;
    }
} // namespace pe
