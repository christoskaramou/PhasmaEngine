#pragma once

#include "Animation/AnimationReferenceFrames.h"
#include "Animation/AnimationTypes.h"
#include "GUI/Widgets/RigEditor.h"
#include "imgui/imgui.h"

#include <array>
#include <filesystem>
#include <functional>
#include <span>

namespace pe
{
    class AnimationTimeline;
    class Camera;
    class Image;
    class Scene;

    // Timeline-owned viewport posing. RigEditor supplies only the bind document whose limits, pins,
    // and rest locks constrain the solve; all interaction state and key writes live here.
    class AnimationPoseViewport
    {
    public:
        AnimationPoseViewport(AnimationTimeline &timeline, RigEditor &rig);
        ~AnimationPoseViewport();

        void DrawControls(Scene &scene);
        // Mass proxy per skeleton bone and its rig-space rest centre for the centre-of-mass ballistic bake.
        void BodyMasses(const Skeleton &skeleton, std::vector<float> &masses, std::vector<vec3> &restCentres) const;
        void DrawViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize, bool &hovered,
                          bool &active);
        std::string HandleAction(Scene &scene, const std::string &action, const std::string &argsJson);
        void Abort();
        // Session lock state for the Timeline's undo: the holds and plants posing created and which authored
        // plants yielded to them. Authored locks themselves belong to the rig document and its own undo.
        std::vector<RigLock> LockSnapshot() const { return m_locks; }
        void RestoreSessionLocks(const std::vector<RigLock> &snapshot);

    private:
        struct LockSolve
        {
            int lock = -1;
            int root = -1, mid = -1;
            quat rootRotation = quat(1.f, 0.f, 0.f, 0.f);
            quat midRotation = quat(1.f, 0.f, 0.f, 0.f);
            bool clamped = false;
        };

        void SyncTarget(Scene &scene);
        void DrawLocksPanel(Scene &scene);
        void DrawIkViewport(Scene &scene, Camera *camera, const ImVec2 &imageMin, const ImVec2 &imageSize,
                            const mat4 &rootWorld, std::span<const mat4> boneTransforms,
                            const std::function<bool(const vec3 &, ImVec2 &)> &project, bool &hovered, bool &active);
        void PoseTails(const Skeleton &skeleton, std::span<const mat4> boneTransforms, std::vector<vec3> &heads,
                       std::vector<vec3> &tails, std::vector<vec3> *restTails = nullptr) const;
        bool LockChain(const RigLock &lock, const Skeleton &skeleton, int &root, int &mid, int &target,
                       std::string *why = nullptr) const;
        bool LockAnchorPosed(const RigLock &lock, const Skeleton &skeleton, std::span<const mat4> boneTransforms,
                             vec3 &out) const;
        int AddLock(const std::string &bone, const std::string &target, float reach, const vec3 *anchor,
                    std::string &error);
        bool CaptureLockAnchor(RigLock &lock);
        void HoldPosedBone(int bone);
        // Auto contact: the floor is the lowest rest tail of the contact bones (feet / toes, authored plants).
        float Ground() const;
        bool IsContactBone(const Skeleton &skeleton, int bone, float ground) const;
        void EraseLock(size_t index);
        void AppendLock(RigLock lock);
        bool Planted(const RigLock &lock, float ground) const;
        void UpdateContacts(const Skeleton &skeleton, std::span<const mat4> boneTransforms, std::span<const int> edited);
        bool CentreOfMass(const Skeleton &skeleton, std::span<const mat4> boneTransforms, vec3 &out) const;
        bool SupportCentre(vec3 &out) const;
        // The heel of a foot bone (under the ankle at the toe's rest height, carried by the bone); foot names only.
        bool HeelPoint(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone,
                       std::span<const vec3> restTails, vec3 &out) const;
        // The point of a contact bone that meets the floor: its tail, or its heel when that is lower.
        vec3 ContactPoint(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone,
                          std::span<const vec3> tails, std::span<const vec3> restTails, bool &heel) const;
        void BeginBalance(int bone = -1);
        // The bone whose rotation counter-leans the upper body: the Location carrier when no foot hangs under it,
        // else its heaviest foot-free child (a mocap spine); -1 when the rig has no such trunk.
        int LeanBone(const Skeleton &skeleton) const;
        bool ApplyBalance(Scene &scene, int draggedBone, int mirrorBone);

    public:
        // Interval bake of the same prior: on every grounded frame the centre of mass is brought over the feet in
        // contact. report receives a JSON summary. Airborne spans belong to Ballistic / Body.
        bool BakeBalance(Scene &scene, float startFrame, float endFrame, std::string &status, std::string *report = nullptr);

    private:
        void SolveLockRotations(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int skipBone,
                                int skipMirrorBone, std::vector<LockSolve> &out);
        bool SolveLocks(Scene &scene, int skipBone = -1, bool pushUndo = false, float frame = -1.f,
                        int skipMirrorBone = -1, std::span<const int> editedBones = {});
        bool BakeLocks(Scene &scene, std::string &status);
        bool IsPinned(const std::string &bone) const;
        void TogglePin(const std::string &bone);
        quat ClampPuppetRotation(const Skeleton &skeleton, std::span<const quat> rotations, int bone,
                                 const quat &rotation, bool &limited) const;
        void PuppetSolve(const Skeleton &skeleton, std::span<const mat4> boneTransforms, int bone,
                         const vec3 *targetTail, const quat *targetRotation,
                         std::vector<std::pair<int, quat>> &out, bool &limited) const;
        bool PuppetTo(Scene &scene, int bone, const vec3 *targetTail, const quat *targetRotation, float *gap = nullptr,
                      bool pushUndo = false, bool *keyedOut = nullptr, bool *limitedOut = nullptr);
        static void DrawPadlock(ImDrawList *drawList, const ImVec2 &centre, bool closed, ImU32 colour);

        bool LoadReference(const std::filesystem::path &path, std::string &error);
        void ClearReference();
        bool UpdateReferenceImage(AnimationTimeline *timeline, std::string &error);
        void ReleaseReferenceImage(bool drainRendererFrames = false);
        void DrawReferenceOverlay(AnimationTimeline *timeline, const ImVec2 &imageMin, const ImVec2 &imageSize);

        int FindBone(const std::string &name) const { return m_rig.FindBone(name); }
        float ModelHeight() const { return m_rig.ModelHeight(); }
        void PushUndo(bool keepPreset = false) { m_rig.PushUndo(keepPreset); }
        std::string MirrorName(const std::string &name) const { return m_rig.MirrorName(name); }

        AnimationTimeline &m_timeline;
        RigEditor &m_rig;
        ModelAsset *&m_model;
        NodeId *&m_rootNode;
        std::vector<RigBone> &m_bones;
        std::vector<RigLock> &m_locks;
        std::vector<std::string> &m_pins;
        bool &m_dirty;
        int &m_poseSelected;
        ModelAsset *m_lastModel = nullptr;
        std::string m_status;

        bool m_mirrorX = false;
        bool m_poseDragging = false;
        bool m_poseDirect = false;
        bool m_posePushed = false;
        int m_poseGizmo = 0;
        int m_grabBone = -1;
        bool m_grabPushed = false;
        vec3 m_grabPlanePoint = vec3(0.f), m_grabOffset = vec3(0.f);
        std::vector<vec3> m_lockBend;
        bool m_autoLock = true; // hold every bone posed by hand where it was left
        bool m_balance = true;  // a drag keeps the centre of mass over the ground point it started on
        bool m_balanceHaveReference = false;
        vec3 m_balanceReference = vec3(0.f);      // rig-space centre of mass when the drag began
        std::string m_balanceNote;                // why the last drag could not balance
        int m_balanceLeanBone = -1;               // trunk bone the drag counter-leans, chosen when the drag begins
        glm::vec2 m_balanceLean = glm::vec2(0.f); // counter-lean applied by this drag: rotation vector about the floor axes, radians
        int m_lockTarget = -1;
        float m_lockReach = 1.f;
        bool m_reachDragging = false;
        bool m_reachPushed = false;
        uint32_t m_poseEditSerial = 0;
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

        std::filesystem::path m_referencePath;
        AnimationReferenceFrames::Sequence m_referenceSequence;
        int m_referenceFrameIndex = -1;
        Image *m_referenceImage = nullptr;
        void *m_referenceTexture = nullptr;
        std::array<char, 512> m_referencePathBuffer{};
    };
} // namespace pe
