#include "AnimationPoseViewport.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Animation/AnimationEvaluator.h"
#include "Animation/AnimationPoseTools.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/Widgets/AnimationTimeline.h"
#include "GUI/Widgets/FileSelector.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "imgui/ImGuizmo.h"

#include <nlohmann/json.hpp>

namespace pe
{
    namespace
    {
        constexpr ImU32 kBoneCol = IM_COL32(120, 200, 255, 230);
        constexpr ImU32 kBoneSelCol = IM_COL32(255, 200, 80, 255);
        constexpr ImU32 kLockCol = IM_COL32(255, 120, 255, 255);

        bool StrictVec3(const nlohmann::json &json, vec3 &out)
        {
            if (!json.is_array() || json.size() != 3 ||
                !std::all_of(json.begin(), json.end(), [](const nlohmann::json &component)
                             { return component.is_number(); }))
                return false;
            const vec3 value(json[0].get<float>(), json[1].get<float>(), json[2].get<float>());
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                return false;
            out = value;
            return true;
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
                *carry = out;
            return false;
        }

        bool RayPlane(const vec3 &origin, const vec3 &direction, const vec3 &point, const vec3 &normal, vec3 &hit)
        {
            const float denominator = glm::dot(direction, normal);
            if (std::abs(denominator) < 1e-6f)
                return false;
            const float distance = glm::dot(point - origin, normal) / denominator;
            if (distance < 0.f)
                return false;
            hit = origin + direction * distance;
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

    AnimationPoseViewport::AnimationPoseViewport(AnimationTimeline &timeline, RigEditor &rig)
        : m_timeline(timeline), m_rig(rig), m_model(rig.m_model), m_rootNode(rig.m_rootNode), m_bones(rig.m_bones),
          m_locks(rig.m_locks), m_pins(rig.m_pins), m_dirty(rig.m_dirty), m_poseSelected(timeline.m_activeBone)
    {
        m_poseEditSerial = timeline.PoseEditSerial();
    }

    void AnimationPoseViewport::Abort()
    {
        m_grabBone = -1;
        m_grabPushed = false;
        m_ikBone = -1;
        m_ikDragging = false;
        m_ikDirty = false;
        m_reachDragging = false;
        m_reachPushed = false;
        if (m_poseDragging && m_poseDirect)
            m_timeline.EndViewportRotate();
        m_poseDragging = false;
        m_poseDirect = false;
        m_posePushed = false;
    }

    void AnimationPoseViewport::SyncTarget(Scene &scene)
    {
        m_rig.ResolveTarget(scene);
        if (m_model == m_lastModel)
            return;
        Abort();
        m_lockBend.clear();
        m_poseEditSerial = m_timeline.PoseEditSerial();
        m_lastModel = m_model;
        m_status.clear();
    }

    void AnimationPoseViewport::DrawControls(Scene &scene)
    {
        SyncTarget(scene);
        if (!m_model || !m_model->HasSkeleton())
            return;

        // Pose-bar and timeline.pose edits can land while Rig Editor owns the viewport. Locks still
        // belong to this Timeline tool, so consume the edit serial from the controls as well.
        if (m_timeline.PoseEditSerial() != m_poseEditSerial)
        {
            m_poseEditSerial = m_timeline.PoseEditSerial();
            if (m_grabBone < 0 && !m_poseDragging)
                SolveLocks(scene, -1, false, m_timeline.PoseEditFrame());
        }

        const Skeleton &skeleton = m_model->GetSkeleton();
        if (m_poseSelected >= skeleton.GetBoneCount())
            m_poseSelected = -1;

        ImGui::Separator();
        ImGui::TextDisabled("Viewport Pose");
        ImGui::RadioButton("Rotate", &m_poseGizmo, 0);
        ui::ItemTooltip("Rotate recruits the limited puppet chain while keeping the selected tail in place.");
        ImGui::SameLine();
        ImGui::RadioButton("Move", &m_poseGizmo, 1);
        ui::ItemTooltip("Move pulls the selected tail through the same limited puppet solver; child location keys are untouched.");
        ImGui::SameLine();
        ImGui::RadioButton("Both", &m_poseGizmo, 2);
        ui::ItemTooltip("Show move and rotate handles together; both constraints use the puppet solver.");
        ImGui::SameLine();
        ImGui::Checkbox("Mirror X##pose", &m_mirrorX);
        ui::ItemTooltip("Solve the .L/.R counterpart chain in the same Timeline undo step.");

        const float stepperWidth = ImGui::GetFontSize() * 4.f;
        ImGui::Checkbox("Onion Bones", &m_onionBones);
        if (m_onionBones)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(stepperWidth);
            ImGui::DragInt("-##onion_previous", &m_onionPrevious, 0.1f, 0, 12);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(stepperWidth);
            ImGui::DragInt("+##onion_next", &m_onionNext, 0.1f, 0, 12);
        }
        ui::SameLineIfFits(ui::CheckboxWidth("Motion Trail"));
        ImGui::Checkbox("Motion Trail", &m_motionTrail);
        if (m_motionTrail)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(stepperWidth);
            ImGui::DragInt("-##trail_previous", &m_trailPrevious, 0.1f, 0, 60);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(stepperWidth);
            ImGui::DragInt("+##trail_next", &m_trailNext, 0.1f, 0, 60);
        }

        const int mid = m_poseSelected >= 0 && m_poseSelected < skeleton.GetBoneCount()
                            ? skeleton.bones[m_poseSelected].parentIndex
                            : -1;
        const bool hasIkChain = mid >= 0 && skeleton.bones[mid].parentIndex >= 0;
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasIkChain);
        bool twoBoneIk = m_twoBoneIk;
        if (ImGui::Checkbox("Two-Bone IK", &twoBoneIk))
        {
            Abort();
            m_twoBoneIk = twoBoneIk;
        }
        ImGui::EndDisabled();
        ui::ItemTooltip(hasIkChain ? "Selected Timeline bone is the tip; its parent and grandparent form the solved links."
                                   : "Select a tip bone with a parent and grandparent in the Timeline.",
                        ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
        if (!hasIkChain)
            m_twoBoneIk = false;
        if (m_twoBoneIk)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset IK Target"))
                m_ikBone = -1;
        }

        if (ImGui::CollapsingHeader("Reference Sequence##timeline_pose"))
        {
            ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 11.f,
                                             ImGui::GetContentRegionAvail().x - ui::ButtonWidth("Browse Load Clear")));
            ImGui::InputText("##timeline_reference_path", m_referencePathBuffer.data(), m_referencePathBuffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse##timeline_reference") && m_timeline.m_gui)
                if (FileSelector *selector = m_timeline.m_gui->GetWidget<FileSelector>())
                    selector->OpenSelection([this](const std::string &path)
                                            {
                                                std::string error;
                                                m_status = LoadReference(PathFromUtf8(path), error)
                                                               ? "Loaded reference sequence."
                                                               : error;
                                                return error.empty(); },
                                            {".json"}, Path::Assets);
            ImGui::SameLine();
            if (ImGui::Button("Load##timeline_reference"))
            {
                std::string error;
                m_status = LoadReference(PathFromUtf8(m_referencePathBuffer.data()), error)
                               ? "Loaded reference sequence."
                               : error;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_referenceSequence.frames.empty());
            if (ImGui::Button("Clear##timeline_reference"))
            {
                ClearReference();
                m_status = "Reference sequence cleared.";
            }
            ImGui::EndDisabled();
            if (!m_referenceSequence.frames.empty())
            {
                AnimationReferenceFrames::Config &reference = m_referenceSequence.config;
                ImGui::SliderFloat("Opacity##timeline_reference", &reference.opacity, 0.f, 1.f, "%.2f");
                ImGui::DragFloat("Scale##timeline_reference", &reference.scale, 0.01f, 0.05f, 5.f, "%.2f");
                ImGui::DragFloat2("Offset px##timeline_reference", &reference.offset.x, 1.f, -4096.f, 4096.f, "%.0f");
                ImGui::Checkbox("Flip X##timeline_reference", &reference.flipX);
                ImGui::SameLine();
                ImGui::Checkbox("Flip Y##timeline_reference", &reference.flipY);
                ImGui::SameLine();
                ImGui::TextDisabled("%d frames @ %.2f fps", static_cast<int>(m_referenceSequence.frames.size()),
                                    reference.sourceFps);
            }
        }
        if (ImGui::CollapsingHeader("Pose Locks##timeline_pose"))
            DrawLocksPanel(scene);

        if (!m_status.empty())
            ImGui::TextDisabled("%s", m_status.c_str());
        if (m_dirty && !ImGui::IsAnyItemActive() && !m_rig.RigJsonPath().empty())
            m_rig.SaveJson(nullptr, true);
    }

    std::string AnimationPoseViewport::HandleAction(Scene &scene, const std::string &action,
                                                    const std::string &argsJson)
    {
        try
        {
            const nlohmann::json args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
            if (args.is_discarded() || !args.is_object())
                return R"({"error":"invalid args json"})";
            SyncTarget(scene);
            auto ok = [&](nlohmann::json extra = nlohmann::json::object())
            {
                extra["ok"] = true;
                extra["action"] = action;
                extra["bone_count"] = m_bones.size();
                return extra.dump();
            };
            auto fail = [](const std::string &message)
            { return nlohmann::json{{"error", message}}.dump(); };

            if (action == "timeline.reference_load")
            {
                const std::string path = args.value("path", "");
                if (path.empty())
                    return fail("path to a .reference.json manifest is required");
                std::string error;
                if (!LoadReference(PathFromUtf8(path), error))
                    return fail(error);
                return ok({{"path", m_referencePath.generic_string()}, {"frames", m_referenceSequence.frames.size()}});
            }
            if (action == "timeline.reference_clear")
            {
                ClearReference();
                return ok();
            }
            if (!m_model)
                return fail("no target model: select a node of a .pemesh model first");

            if (action == "timeline.pin" || action == "timeline.grab")
            {
                if (!m_model || !m_model->HasSkeleton())
                    return fail("the model has no skeleton");
                const Skeleton &skeleton = m_model->GetSkeleton();
                const std::string name = args.value("bone", "");
                const int bone = skeleton.GetBoneIndex(name);
                if (bone < 0)
                    return fail("unknown bone: " + name);
                if (action == "timeline.pin")
                {
                    const bool want = args.value("pinned", !IsPinned(name));
                    if (want != IsPinned(name))
                        TogglePin(name);
                    return ok({{"bone", name}, {"pinned", IsPinned(name)}, {"pins", m_pins}});
                }
                RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
                AnimationTimeline *timeline = &m_timeline;
                if (!renderer || !timeline || !args.contains("target"))
                    return fail("timeline.grab needs target[3], the renderer and the Animation Timeline");
                *timeline->GetOpen() = true; // a closed Timeline resolves next frame; the retry then has a pose
                if (timeline->HasPendingRequests())
                    return fail("the Timeline has queued requests (frame/pose/clip); retry after the next frame");
                vec3 target;
                if (!StrictVec3(args["target"], target))
                    return fail("target must be three finite numbers");
                float gap = 0.f;
                bool keyed = false;
                bool limited = false;
                if (!PuppetTo(renderer->GetScene(), bone, &target, nullptr, &gap, true, &keyed, &limited))
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
                return ok({{"bone", name}, {"gap", gap}, {"limited", limited}, {"chain", chain}});
            }
            if (action == "timeline.lock")
            {
                const std::string op = args.value("op", "list");
                // gap = tail-to-anchor distance at the Timeline's current pose (agents assert "held" on it).
                AnimationTimeline::ViewportPose currentPose;
                std::vector<vec3> poseHeads, poseTails;
                if (AnimationTimeline *timeline = &m_timeline;
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
                AnimationTimeline *timeline = &m_timeline;
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
            return fail("unknown Timeline pose action");
        }
        catch (const std::exception &exception)
        {
            return nlohmann::json{{"error", std::string("invalid Timeline pose arguments: ") + exception.what()}}.dump();
        }
    }

    AnimationPoseViewport::~AnimationPoseViewport()
    {
        ReleaseReferenceImage(true);
    }

    void AnimationPoseViewport::ReleaseReferenceImage(bool drainRendererFrames)
    {
        if (!m_referenceImage && !m_referenceTexture)
        {
            m_referenceFrameIndex = -1;
            return;
        }
        if (drainRendererFrames)
            if (RendererSystem *renderer = GetGlobalSystem<RendererSystem>())
                renderer->WaitAllFramesCommands();
        if (m_referenceTexture && m_timeline.m_gui)
            m_timeline.m_gui->ReleaseImageTexture(m_referenceTexture);
        m_referenceTexture = nullptr;
        Image::Destroy(m_referenceImage);
        m_referenceFrameIndex = -1;
    }

    void AnimationPoseViewport::ClearReference()
    {
        ReleaseReferenceImage(true);
        m_referencePath.clear();
        m_referenceSequence = {};
        m_referencePathBuffer.fill(0);
    }

    bool AnimationPoseViewport::LoadReference(const std::filesystem::path &requestedPath, std::string &error)
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

    bool AnimationPoseViewport::UpdateReferenceImage(AnimationTimeline *timeline, std::string &error)
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
        if (!queue || !m_timeline.m_gui)
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
        void *texture = m_timeline.m_gui->RegisterImageTexture(image);
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

    void AnimationPoseViewport::DrawReferenceOverlay(AnimationTimeline *timeline, const ImVec2 &imageMin, const ImVec2 &imageSize)
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

    void AnimationPoseViewport::DrawIkViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                               const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                                               const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered,
                                               bool &active)
    {
        AnimationTimeline *timeline = &m_timeline;
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

    // -------------------------------------------------------------------------
    // locks
    // -------------------------------------------------------------------------
    void AnimationPoseViewport::PoseTails(const Skeleton &skeleton, std::span<const mat4> boneTransforms, std::vector<vec3> &heads,
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

    bool AnimationPoseViewport::LockChain(const RigLock &lock, const Skeleton &skeleton, int &root, int &mid, int &target,
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

    bool AnimationPoseViewport::LockAnchorPosed(const RigLock &lock, const Skeleton &skeleton, std::span<const mat4> boneTransforms,
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

    int AnimationPoseViewport::AddLock(const std::string &bone, const std::string &target, float reach, const vec3 *anchor,
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
            AnimationTimeline *timeline = &m_timeline;
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

    void AnimationPoseViewport::SolveLockRotations(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int skipBone,
                                                   int skipMirrorBone, std::vector<LockSolve> &out)
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
                mid == skipMirrorBone || root == skipMirrorBone ||
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

    bool AnimationPoseViewport::SolveLocks(Scene &scene, int skipBone, bool pushUndo, float frame, int skipMirrorBone)
    {
        AnimationTimeline *timeline = &m_timeline;
        if (!timeline || !m_model || !m_model->HasSkeleton() || m_locks.empty())
            return false;
        const Skeleton &skeleton = m_model->GetSkeleton();
        AnimationTimeline::ViewportPose pose;
        const bool sampled = frame >= 0.f ? timeline->SampleViewportPoseAtFrame(m_model, frame, pose)
                                          : timeline->GetViewportPose(m_model, pose);
        if (!sampled || static_cast<int>(pose.boneTransforms.size()) != skeleton.GetBoneCount())
            return false;
        std::vector<LockSolve> solves;
        SolveLockRotations(skeleton, pose.boneTransforms, skipBone, skipMirrorBone, solves);
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

    bool AnimationPoseViewport::BakeLocks(Scene &scene, std::string &status)
    {
        AnimationTimeline *timeline = &m_timeline;
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
            SolveLockRotations(skeleton, poses[frame].boneTransforms, -1, -1, solves);
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

    void AnimationPoseViewport::DrawLocksPanel(Scene &scene)
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
        ui::ItemTooltip(haveBone ? "Pin the selected bone's tail where it is now." : "Select a bone in the Timeline channel list first.",
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
    }

    // -------------------------------------------------------------------------
    // puppet drag + pins
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

    bool AnimationPoseViewport::IsPinned(const std::string &bone) const
    {
        return std::find(m_pins.begin(), m_pins.end(), bone) != m_pins.end();
    }

    void AnimationPoseViewport::TogglePin(const std::string &bone)
    {
        PushUndo(true);
        const auto at = std::find(m_pins.begin(), m_pins.end(), bone);
        if (at == m_pins.end())
            m_pins.push_back(bone);
        else
            m_pins.erase(at);
        m_dirty = true;
    }

    quat AnimationPoseViewport::ClampPuppetRotation(const Skeleton &skeleton, std::span<const quat> rotations, int bone,
                                                    const quat &rotation, bool &limited) const
    {
        const int authored = bone >= 0 && bone < skeleton.GetBoneCount() ? FindBone(skeleton.bones[bone].name) : -1;
        if (authored < 0 || (m_bones[authored].swingLimitDegrees <= 0.f && m_bones[authored].twistLimitDegrees <= 0.f))
            return glm::normalize(rotation);

        const BoneInfo &info = skeleton.bones[bone];
        const quat parentRotation = info.parentIndex >= 0 ? rotations[info.parentIndex] : quat(1.f, 0.f, 0.f, 0.f);
        const quat prefixRotation = RotationOf(info.intermediatePrefix);
        const quat channelRotation = glm::normalize(glm::conjugate(prefixRotation) * glm::conjugate(parentRotation) * rotation);
        vec3 bindPosition, bindScale;
        quat bindRotation;
        AnimationEvaluator::BindPose(info, bindPosition, bindRotation, bindScale);
        quat relative = glm::normalize(glm::conjugate(bindRotation) * channelRotation);

        // Swing-twist decomposition around the rest bone's +Y axis. Limits are symmetric cones in bind space.
        const vec3 axis(0.f, 1.f, 0.f);
        const vec3 projected = axis * relative.y;
        quat twist(relative.w, projected.x, projected.y, projected.z);
        twist = glm::dot(twist, twist) > 1e-10f ? glm::normalize(twist) : quat(1.f, 0.f, 0.f, 0.f);
        quat swing = glm::normalize(relative * glm::conjugate(twist));
        if (swing.w < 0.f)
            swing = quat(-swing.w, -swing.x, -swing.y, -swing.z);
        if (twist.w < 0.f)
            twist = quat(-twist.w, -twist.x, -twist.y, -twist.z);

        const RigBone &limit = m_bones[authored];
        if (limit.swingLimitDegrees > 0.f)
        {
            const float angle = 2.f * std::acos(std::clamp(swing.w, -1.f, 1.f));
            const float maximum = glm::radians(limit.swingLimitDegrees);
            if (angle > maximum)
            {
                const vec3 vector(swing.x, swing.y, swing.z);
                swing = glm::dot(vector, vector) > 1e-10f
                            ? glm::angleAxis(maximum, glm::normalize(vector))
                            : quat(1.f, 0.f, 0.f, 0.f);
                limited = true;
            }
        }
        if (limit.twistLimitDegrees > 0.f)
        {
            float angle = 2.f * std::atan2(twist.y, twist.w);
            angle = std::remainder(angle, glm::two_pi<float>());
            const float clamped = std::clamp(angle, -glm::radians(limit.twistLimitDegrees),
                                             glm::radians(limit.twistLimitDegrees));
            if (std::abs(angle - clamped) > 1e-6f)
                limited = true;
            twist = glm::angleAxis(clamped, axis);
        }
        return glm::normalize(parentRotation * prefixRotation * bindRotation * swing * twist);
    }

    void AnimationPoseViewport::PuppetSolve(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone,
                                            const vec3 *targetTail, const quat *targetRotation,
                                            std::vector<std::pair<int, quat>> &out, bool &limited) const
    {
        out.clear();
        limited = false;
        if (bone < 0 || bone >= skeleton.GetBoneCount() || static_cast<int>(boneTransforms.size()) != skeleton.GetBoneCount() ||
            (!targetTail && !targetRotation))
            return;
        std::vector<vec3> heads, tails;
        PoseTails(skeleton, boneTransforms, heads, tails);
        // chain[0] = the handle bone, then ancestors up to (excluding) the first pin or skeleton root.
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
        std::vector<quat> rotations(skeleton.GetBoneCount());
        std::vector<quat> original(skeleton.GetBoneCount());
        for (int i = 0; i < skeleton.GetBoneCount(); ++i)
            rotations[i] = original[i] = RotationOf(boneTransforms[i]);
        for (size_t k = 0; k < chain.size(); k++)
            pivots[k] = heads[chain[k]];
        vec3 effector = tails[bone];
        const vec3 target = targetTail ? *targetTail : effector;
        auto weight = [](size_t k)
        { return k < 2 ? 1.f : k == 2 ? 0.35f
                                      : 0.2f; };
        auto rotateJoint = [&](size_t k, const quat &requested)
        {
            const int joint = chain[k];
            const quat before = rotations[joint];
            const quat after = ClampPuppetRotation(skeleton, rotations, joint, requested, limited);
            const quat delta = glm::normalize(after * glm::conjugate(before));
            if (std::abs(delta.w) >= 1.f - 1e-8f)
                return;
            for (size_t j = 0; j <= k; ++j)
            {
                rotations[chain[j]] = glm::normalize(delta * rotations[chain[j]]);
                if (j < k)
                    pivots[j] = pivots[k] + delta * (pivots[j] - pivots[k]);
            }
            effector = pivots[k] + delta * (effector - pivots[k]);
        };

        for (int pass = 0; pass < 24; ++pass)
        {
            if (targetRotation)
                rotateJoint(0, glm::normalize(*targetRotation));
            if (glm::distance(effector, target) < 1e-4f)
                break;
            for (size_t k = targetRotation ? 1 : 0; k < chain.size(); ++k)
            {
                const vec3 have = effector - pivots[k], want = target - pivots[k];
                if (glm::dot(have, have) < 1e-10f || glm::dot(want, want) < 1e-10f)
                    continue;
                quat delta = RotationBetween(have, want);
                if (weight(k) < 1.f)
                    delta = glm::normalize(glm::slerp(quat(1.f, 0.f, 0.f, 0.f), delta, weight(k)));
                rotateJoint(k, glm::normalize(delta * rotations[chain[k]]));
            }
        }
        if (targetRotation)
            rotateJoint(0, glm::normalize(*targetRotation)); // selected bone absorbs the last orientation residual
        for (size_t k = 0; k < chain.size(); k++)
            if (std::abs(glm::dot(rotations[chain[k]], original[chain[k]])) < 1.f - 1e-6f)
                out.emplace_back(chain[k], rotations[chain[k]]);
    }

    bool AnimationPoseViewport::PuppetTo(Scene &scene, int bone, const vec3 *targetTail, const quat *targetRotation, float *gap,
                                         bool pushUndo, bool *keyedOut, bool *limitedOut)
    {
        if (keyedOut)
            *keyedOut = false;
        if (limitedOut)
            *limitedOut = false;
        AnimationTimeline *timeline = &m_timeline;
        if (!timeline || !m_model || !m_model->HasSkeleton())
            return false;
        const Skeleton &skeleton = m_model->GetSkeleton();
        AnimationTimeline::ViewportPose pose;
        if (!timeline->GetViewportPose(m_model, pose) ||
            static_cast<int>(pose.boneTransforms.size()) != skeleton.GetBoneCount())
            return false;
        std::vector<std::pair<int, quat>> solved;
        bool limited = false;
        PuppetSolve(skeleton, pose.boneTransforms, bone, targetTail, targetRotation, solved, limited);

        int mirrorBone = -1;
        if (m_mirrorX)
        {
            const std::string mirrorName = MirrorName(skeleton.bones[bone].name);
            if (!mirrorName.empty())
                mirrorBone = skeleton.GetBoneIndex(mirrorName);
        }
        if (mirrorBone >= 0 && mirrorBone != bone)
        {
            vec3 mirroredTail;
            quat mirroredRotation;
            const vec3 *mirrorTailPtr = nullptr;
            const quat *mirrorRotationPtr = nullptr;
            if (targetTail)
            {
                mirroredTail = *targetTail;
                mirroredTail.x = -mirroredTail.x;
                mirrorTailPtr = &mirroredTail;
            }
            if (targetRotation)
            {
                mat4 reflection(1.f);

                reflection[0][0] = -1.f;
                mirroredRotation = RotationOf(reflection * glm::mat4_cast(*targetRotation) * reflection);
                mirrorRotationPtr = &mirroredRotation;
            }
            std::vector<std::pair<int, quat>> mirrored;
            bool mirrorLimited = false;
            PuppetSolve(skeleton, pose.boneTransforms, mirrorBone, mirrorTailPtr, mirrorRotationPtr, mirrored,
                        mirrorLimited);
            limited = limited || mirrorLimited;
            for (const auto &[mirrorIndex, rotation] : mirrored)
            {
                const auto duplicate = std::find_if(solved.begin(), solved.end(), [&](const auto &edit)
                                                    { return edit.first == mirrorIndex; });
                if (duplicate == solved.end())
                    solved.emplace_back(mirrorIndex, rotation);
                else
                    duplicate->second = glm::normalize(glm::slerp(duplicate->second, rotation, 0.5f));
            }
        }
        std::vector<AnimationTimeline::GlobalBoneRotation> rotations;
        for (const auto &[b, rotation] : solved)
            if (timeline->CanViewportRotate(m_model, b, true, false))
                rotations.push_back({b, rotation});
        if (limitedOut)
            *limitedOut = limited;
        if (rotations.empty())
        {
            std::vector<vec3> heads, tails;
            PoseTails(skeleton, pose.boneTransforms, heads, tails);
            const bool tailHeld = !targetTail || glm::distance(tails[bone], *targetTail) < 1e-4f;
            const bool rotationHeld = !targetRotation ||
                                      std::abs(glm::dot(RotationOf(pose.boneTransforms[bone]), *targetRotation)) > 1.f - 1e-6f;
            if (gap && targetTail)
                *gap = glm::distance(tails[bone], *targetTail);
            return tailHeld && rotationHeld;
        }
        // pushUndo rides Key's own success path: a rejected batch costs no undo step and no snapshot.
        if (!timeline->KeyViewportGlobalRotations(scene, m_model, rotations, -1.f, pushUndo))
            return false;
        if (keyedOut)
            *keyedOut = true;
        SolveLocks(scene, bone, false, -1.f, mirrorBone);
        if (gap && targetTail && timeline->GetViewportPose(m_model, pose))
        {
            std::vector<vec3> heads, tails;
            PoseTails(skeleton, pose.boneTransforms, heads, tails);
            *gap = glm::distance(tails[bone], *targetTail);
        }
        return true;
    }

    void AnimationPoseViewport::DrawPadlock(ImDrawList *drawList, const ImVec2 &centre, bool closed, ImU32 colour)
    {
        const float s = 4.f; // half width of the body
        drawList->AddRectFilled({centre.x - s, centre.y}, {centre.x + s, centre.y + s * 1.5f}, colour, 1.f);
        const ImVec2 arcCentre = closed ? ImVec2(centre.x, centre.y) : ImVec2(centre.x + s * 0.6f, centre.y - s * 0.4f);
        drawList->PathArcTo(arcCentre, s * 0.65f, glm::pi<float>(), glm::two_pi<float>(), 10);
        drawList->PathStroke(colour, 0, 1.5f);
    }

    void AnimationPoseViewport::DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                                             bool &hovered, bool &active)
    {
        hovered = false;
        active = false;
        SyncTarget(scene);
        AnimationTimeline *timeline = &m_timeline;
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

        // Puppet handles (tail dots) and pin padlocks beside every head: pull a tail and the chain bends up to
        // the first pinned bone; click a padlock to pin / unpin. The IK tool owns the mouse while enabled.
        if (!m_twoBoneIk)
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
                ui::TooltipText("Puppet Drag: pull the tail and recruit the chain up to the first pinned bone.");
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
                    bool limited = false;
                    const vec3 target = vec3(invRootWorld * vec4(hit, 1.f)) + m_grabOffset;
                    if (PuppetTo(scene, m_grabBone, &target, nullptr, &gap, !m_grabPushed, &keyed, &limited))
                    {
                        m_grabPushed = m_grabPushed || keyed;
                        m_status = limited ? "Puppet Drag keyed; joint limit reached."
                                   : gap > std::max(ModelHeight(), 0.1f) * 0.01f
                                       ? "Puppet Drag: chain cannot reach (pin further up, or pull less)."
                                       : "Puppet Drag keyed at the current frame.";
                    }
                }
                return;
            }
            hovered = hovered || hoveredTail >= 0 || hoveredPin >= 0;
            if (hoveredTail >= 0 || hoveredPin >= 0)
                hoveredBone = -1; // the click belongs to the handle, not the bone under it
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

        const bool rootHandle = skeleton.bones[m_poseSelected].parentIndex < 0;
        const bool canRotate = rootHandle
                                   ? timeline->CanViewportRotate(m_model, m_poseSelected, m_poseGizmo != 1, m_poseGizmo != 0)
                                   : timeline->CanViewportRotate(m_model, m_poseSelected, true, false);
        const bool rightMouse = ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsWindowFocused();
        ImGuizmo::Enable(canRotate && !rightMouse);
        const mat4 handleRig = glm::translate(mat4(1.f), poseTails[m_poseSelected]) *
                               glm::mat4_cast(RotationOf(pose.boneTransforms[m_poseSelected]));
        mat4 worldRH = handedness * (rootWorld * handleRig) * handedness;
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
            const mat4 world = handedness * worldRH * handedness;
            const mat4 requested = invRootWorld * world;
            int mirrorBone = -1;
            if (m_mirrorX)
            {
                const std::string mirrorName = MirrorName(skeleton.bones[m_poseSelected].name);
                if (!mirrorName.empty())
                    mirrorBone = skeleton.GetBoneIndex(mirrorName);
            }
            if (rootHandle)
            {
                if (!m_poseDragging)
                {
                    m_poseDragging =
                        timeline->BeginViewportRotate(scene, m_model, m_poseSelected, m_poseGizmo != 1, m_poseGizmo != 0);
                    m_poseDirect = m_poseDragging;
                }
                if (m_poseDragging)
                {
                    const mat4 current = pose.boneTransforms[m_poseSelected];
                    const vec3 scale(glm::length(vec3(current[0])), glm::length(vec3(current[1])),
                                     glm::length(vec3(current[2])));
                    const vec3 position = vec3(current[3]) +
                                          (m_poseGizmo != 0 ? vec3(requested[3]) - poseTails[m_poseSelected] : vec3(0.f));
                    const quat rotation = m_poseGizmo != 1 ? RotationOf(requested) : RotationOf(current);
                    const mat4 transform = glm::translate(mat4(1.f), position) * glm::mat4_cast(rotation) *
                                           glm::scale(mat4(1.f), scale);
                    timeline->UpdateViewportRotate(scene, m_model, m_poseSelected, transform, mirrorBone,
                                                   m_poseGizmo != 1, m_poseGizmo != 0);
                    SolveLocks(scene, m_poseSelected, false, -1.f, mirrorBone);
                }
            }
            else
            {
                if (!m_poseDragging)
                {
                    m_poseDragging = true;
                    m_poseDirect = false;
                    m_posePushed = false;
                }
                const vec3 targetTail = m_poseGizmo == 0 ? poseTails[m_poseSelected] : vec3(requested[3]);
                const quat targetRotation = RotationOf(requested);
                const quat *rotationTarget = m_poseGizmo == 1 ? nullptr : &targetRotation;
                float gap = 0.f;
                bool keyed = false, limited = false;
                if (PuppetTo(scene, m_poseSelected, &targetTail, rotationTarget, &gap, !m_posePushed, &keyed, &limited))
                {
                    m_posePushed = m_posePushed || keyed;
                    const std::string mirror = m_mirrorX && mirrorBone >= 0 ? " with mirrored counterpart" : "";
                    m_status = limited ? "Puppet Drag keyed" + mirror + "; joint limit reached."
                               : gap > std::max(ModelHeight(), 0.1f) * 0.01f
                                   ? "Puppet Drag keyed" + mirror + "; chain cannot satisfy the handle."
                                   : "Puppet Drag keyed" + mirror + " at the current frame.";
                }
            }
            if (rootHandle && m_poseDragging)
            {
                m_status = m_mirrorX && mirrorBone >= 0 ? "Pose rotation keyed with mirrored counterpart."
                                                        : "Pose rotation keyed at the current frame.";
            }
        }
        if (m_poseDragging && !gizmoActive)
        {
            if (m_poseDirect || m_posePushed)
                SolveLocks(scene); // the dragged bone's own locks, skipped during the drag
            if (m_poseDirect)
                timeline->EndViewportRotate();
            m_poseDragging = false;
            m_poseDirect = false;
            m_posePushed = false;
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

} // namespace pe
