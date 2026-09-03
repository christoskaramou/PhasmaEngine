#include "ClipExchange.h"
#include "Animation/AnimationClipTools.h"
#include "Animation/AnimationEvaluator.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"

#include <nlohmann/json.hpp>

#if defined(PE_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace pe::ClipExchange
{
    namespace
    {
        std::string Lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        void Decompose(const mat4 &m, vec3 &pos, quat &rot, vec3 &scl)
        {
            pos = vec3(m[3]);
            scl = vec3(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
            for (int i = 0; i < 3; i++)
                if (scl[i] < 1e-8f)
                    scl[i] = 1e-8f;
            rot = glm::normalize(glm::quat_cast(mat3(vec3(m[0]) / scl.x, vec3(m[1]) / scl.y, vec3(m[2]) / scl.z)));
        }

        mat4 Compose(const vec3 &pos, const quat &rot, const vec3 &scl)
        {
            return glm::translate(mat4(1.f), pos) * glm::mat4_cast(rot) * glm::scale(mat4(1.f), scl);
        }

        bool MatricesNear(const mat4 &a, const mat4 &b)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(a[c][r] - b[c][r]) > 1e-5f)
                        return false;
            return true;
        }

        // Bones in parent-before-child order (the cooked order usually is, a BVH always is; this makes it certain).
        std::vector<int> TopologicalOrder(const Skeleton &skeleton)
        {
            const int count = skeleton.GetBoneCount();
            std::vector<int> order;
            std::vector<bool> done(count, false);
            order.reserve(count);
            for (int guard = 0; guard < count && static_cast<int>(order.size()) < count; guard++)
            {
                bool progress = false;
                for (int i = 0; i < count; i++)
                {
                    if (done[i])
                        continue;
                    const int parent = skeleton.bones[i].parentIndex;
                    if (parent < -1 || parent >= count || parent == i)
                        return {};
                    if (parent < 0 || done[parent])
                    {
                        done[i] = true;
                        order.push_back(i);
                        progress = true;
                    }
                }
                if (!progress)
                    return {};
            }
            return static_cast<int>(order.size()) == count ? order : std::vector<int>{};
        }

        bool ValidSkeleton(const Skeleton &skeleton)
        {
            return skeleton.GetBoneCount() > 0 &&
                   static_cast<int>(TopologicalOrder(skeleton).size()) == skeleton.GetBoneCount();
        }

        // Bind-pose globals: parent global * localBindTransform.
        void BindGlobals(const Skeleton &skeleton, const std::vector<int> &order, std::vector<mat4> &out)
        {
            out.assign(skeleton.GetBoneCount(), mat4(1.f));
            for (int i : order)
            {
                const int parent = skeleton.bones[i].parentIndex;
                out[i] = (parent >= 0 ? out[parent] : mat4(1.f)) * skeleton.bones[i].localBindTransform;
            }
        }

        // Posed globals at a time: the evaluator's local (prefix * sampled TRS, or the bind local without a channel).
        void PosedGlobals(const Skeleton &skeleton, const AnimationClip &clip, const std::vector<int> &order,
                          const std::vector<int> &boneToChannel, float time, std::vector<mat4> &out)
        {
            out.assign(skeleton.GetBoneCount(), mat4(1.f));
            for (int i : order)
            {
                const BoneInfo &bone = skeleton.bones[i];
                mat4 local = bone.localBindTransform;
                if (boneToChannel[i] >= 0)
                {
                    vec3 pos, scl;
                    quat rot;
                    AnimationEvaluator::SampleChannel(clip.channels[boneToChannel[i]], bone, time, pos, rot, scl);
                    local = bone.intermediatePrefix * Compose(pos, rot, scl);
                }
                const int parent = bone.parentIndex;
                out[i] = (parent >= 0 ? out[parent] : mat4(1.f)) * local;
            }
        }

        std::vector<int> BoneToChannel(const Skeleton &skeleton, const AnimationClip &clip)
        {
            std::vector<int> map(skeleton.GetBoneCount(), -1);
            for (int c = 0; c < static_cast<int>(clip.channels.size()); c++)
            {
                const int bone = clip.channels[c].boneIndex;
                if (bone >= 0 && bone < static_cast<int>(map.size()))
                    map[bone] = c;
            }
            return map;
        }

        float RigHeight(const Skeleton &skeleton, const std::vector<mat4> &bindGlobals)
        {
            float lo = std::numeric_limits<float>::max(), hi = -std::numeric_limits<float>::max();
            for (int i = 0; i < skeleton.GetBoneCount(); i++)
            {
                lo = std::min(lo, bindGlobals[i][3].y);
                hi = std::max(hi, bindGlobals[i][3].y);
            }
            return hi > lo ? hi - lo : 0.f;
        }

        int Depth(const Skeleton &skeleton, int bone)
        {
            int depth = 0;
            for (int guard = 0; bone >= 0 && guard < 1024; guard++, depth++)
                bone = skeleton.bones[bone].parentIndex;
            return depth;
        }

        // The shallowest bone with position keys: the one carrying the body's travel.
        int LocationBone(const Skeleton &skeleton, const AnimationClip &clip)
        {
            int best = -1, bestDepth = std::numeric_limits<int>::max();
            for (const AnimationChannel &chan : clip.channels)
            {
                if (chan.positionKeys.empty() || chan.boneIndex < 0 || chan.boneIndex >= skeleton.GetBoneCount())
                    continue;
                const int depth = Depth(skeleton, chan.boneIndex);
                if (depth < bestDepth)
                {
                    bestDepth = depth;
                    best = chan.boneIndex;
                }
            }
            return best;
        }

        bool FiniteValue(const vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool FiniteValue(const quat &value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && glm::dot(value, value) > 1e-12f;
        }

        template <typename T>
        bool ValidKeys(const std::vector<AnimationKey<T>> &keys)
        {
            float previous = -std::numeric_limits<float>::infinity();
            for (const AnimationKey<T> &key : keys)
            {
                if (!std::isfinite(key.time) || key.time < previous || !FiniteValue(key.value) ||
                    static_cast<uint8_t>(key.interpolation) > static_cast<uint8_t>(AnimationInterpolation::Stepped))
                    return false;
                previous = key.time;
            }
            return true;
        }

        bool ValidClip(const AnimationClip &clip, const Skeleton &skeleton)
        {
            if (!ValidSkeleton(skeleton) || !std::isfinite(clip.duration) || clip.duration < 0.f ||
                !std::isfinite(clip.ticksPerSecond) || clip.ticksPerSecond <= 0.f)
                return false;
            for (const AnimationChannel &channel : clip.channels)
                if (channel.boneIndex < 0 || channel.boneIndex >= skeleton.GetBoneCount() ||
                    !ValidKeys(channel.positionKeys) || !ValidKeys(channel.rotationKeys) ||
                    !ValidKeys(channel.scaleKeys))
                    return false;
            if (clip.rootMotion.boneIndex < 0)
                return clip.rootMotion.positionKeys.empty();
            return clip.rootMotion.boneIndex < skeleton.GetBoneCount() && !clip.rootMotion.positionKeys.empty() &&
                   ValidKeys(clip.rootMotion.positionKeys);
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------------------
    // BVH
    // ---------------------------------------------------------------------------------------------------------
    bool LoadBvh(const std::filesystem::path &path, ImportedRig &out, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = "cannot read " + path.generic_string();
            return false;
        }
        std::vector<std::string> tokens;
        for (std::string line; std::getline(in, line);)
        {
            std::istringstream ls(line);
            for (std::string tok; ls >> tok;)
                tokens.push_back(tok);
        }
        auto parseFloat = [](const std::string &text, float &value)
        {
            char *end = nullptr;
            value = std::strtof(text.c_str(), &end);
            return end == text.c_str() + text.size() && std::isfinite(value);
        };
        auto parseInt = [](const std::string &text, int &value)
        {
            char *end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (end != text.c_str() + text.size() || parsed < 0 || parsed > std::numeric_limits<int>::max())
                return false;
            value = static_cast<int>(parsed);
            return true;
        };
        struct Joint
        {
            std::string name;
            int parent = -1;
            vec3 offset = vec3(0.f);
            std::vector<std::string> channels; // in file order
        };
        std::vector<Joint> joints;
        std::vector<int> stack;
        size_t i = 0;
        auto next = [&](std::string &tok) -> bool
        {
            if (i >= tokens.size())
                return false;
            tok = tokens[i++];
            return true;
        };
        std::string tok;
        bool endSite = false;
        while (next(tok) && tok != "MOTION")
        {
            if (tok == "ROOT" || tok == "JOINT")
            {
                Joint joint;
                if (!next(joint.name))
                {
                    error = "missing joint name in " + path.filename().string();
                    return false;
                }
                joint.parent = stack.empty() ? -1 : stack.back();
                joints.push_back(joint);
            }
            else if (tok == "End")
            {
                next(tok); // Site
                endSite = true;
            }
            else if (tok == "{")
            {
                if (!endSite)
                    stack.push_back(static_cast<int>(joints.size()) - 1);
            }
            else if (tok == "}")
            {
                if (endSite)
                    endSite = false;
                else if (!stack.empty())
                    stack.pop_back();
            }
            else if (tok == "OFFSET")
            {
                vec3 offset;
                for (int k = 0; k < 3; k++)
                {
                    if (!next(tok) || !parseFloat(tok, offset[k]))
                    {
                        error = "invalid joint offset in " + path.filename().string();
                        return false;
                    }
                }
                if (!endSite && !joints.empty())
                    joints.back().offset = offset;
            }
            else if (tok == "CHANNELS")
            {
                int count = 0;
                if (!next(tok) || !parseInt(tok, count) || count > 9)
                {
                    error = "invalid BVH channel count in " + path.filename().string();
                    return false;
                }
                for (int k = 0; k < count; k++)
                {
                    if (!next(tok))
                    {
                        error = "truncated BVH channels in " + path.filename().string();
                        return false;
                    }
                    if (!joints.empty())
                        joints.back().channels.push_back(tok);
                }
            }
        }
        if (tok != "MOTION")
        {
            error = "missing MOTION section in " + path.filename().string();
            return false;
        }
        if (joints.empty())
        {
            error = "no joints in " + path.filename().string();
            return false;
        }
        int frames = 0;
        float frameTime = 1.f / 30.f;
        while (next(tok) && tok != "Time:")
        {
            if (tok == "Frames:")
            {
                if (!next(tok) || !parseInt(tok, frames))
                {
                    error = "invalid frame count in " + path.filename().string();
                    return false;
                }
            }
        }
        if (tok != "Time:" || !next(tok) || !parseFloat(tok, frameTime) || frameTime <= 0.f)
        {
            error = "invalid frame time in " + path.filename().string();
            return false;
        }
        frameTime = std::max(frameTime, 1e-4f);
        size_t channelTotal = 0;
        for (const Joint &joint : joints)
            channelTotal += joint.channels.size();
        if (frames <= 0 || channelTotal == 0 || channelTotal > (tokens.size() - i) / static_cast<size_t>(frames))
        {
            error = "truncated motion data in " + path.filename().string();
            return false;
        }

        Skeleton &skeleton = out.skeleton;
        skeleton = Skeleton{};
        std::vector<mat4> bindGlobal(joints.size());
        for (size_t j = 0; j < joints.size(); j++)
        {
            BoneInfo bone;
            bone.name = joints[j].name;
            bone.parentIndex = joints[j].parent;
            bone.localBindTransform = glm::translate(mat4(1.f), joints[j].offset);
            bindGlobal[j] = (bone.parentIndex >= 0 ? bindGlobal[bone.parentIndex] : mat4(1.f)) * bone.localBindTransform;
            bone.offsetMatrix = glm::inverse(bindGlobal[j]);
            skeleton.boneNameToIndex[bone.name] = static_cast<int>(j);
            skeleton.bones.push_back(bone);
        }

        AnimationClip clip;
        clip.name = path.stem().string();
        clip.ticksPerSecond = 1.f / frameTime;
        clip.duration = static_cast<float>(std::max(frames - 1, 1));
        clip.channels.resize(joints.size());
        for (size_t j = 0; j < joints.size(); j++)
            clip.channels[j].boneIndex = static_cast<int>(j);
        for (int f = 0; f < frames; f++)
        {
            const float t = static_cast<float>(f);
            for (size_t j = 0; j < joints.size(); j++)
            {
                const Joint &joint = joints[j];
                vec3 pos = joint.offset;
                bool hasPos = false;
                bool hasRotation = false;
                quat rot(1.f, 0.f, 0.f, 0.f);
                for (const std::string &ch : joint.channels)
                {
                    float v = 0.f;
                    if (!parseFloat(tokens[i++], v))
                    {
                        error = "invalid motion value in " + path.filename().string();
                        return false;
                    }
                    if (ch == "Xposition")
                    {
                        pos.x += v;
                        hasPos = true;
                    }
                    else if (ch == "Yposition")
                    {
                        pos.y += v;
                        hasPos = true;
                    }
                    else if (ch == "Zposition")
                    {
                        pos.z += v;
                        hasPos = true;
                    }
                    else if (ch == "Xrotation")
                    {
                        rot = rot * glm::angleAxis(glm::radians(v), vec3(1.f, 0.f, 0.f));
                        hasRotation = true;
                    }
                    else if (ch == "Yrotation")
                    {
                        rot = rot * glm::angleAxis(glm::radians(v), vec3(0.f, 1.f, 0.f));
                        hasRotation = true;
                    }
                    else if (ch == "Zrotation")
                    {
                        rot = rot * glm::angleAxis(glm::radians(v), vec3(0.f, 0.f, 1.f));
                        hasRotation = true;
                    }
                }
                AnimationChannel &chan = clip.channels[j];
                if (hasRotation)
                    chan.rotationKeys.push_back({t, glm::normalize(rot), AnimationInterpolation::Linear});
                if (hasPos)
                    chan.positionKeys.push_back({t, pos, AnimationInterpolation::Linear});
            }
        }
        out.clips.clear();
        out.clips.push_back(std::move(clip));
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------
    // cooked files and the cook tool
    // ---------------------------------------------------------------------------------------------------------
    std::filesystem::path CookToTemp(const std::filesystem::path &source, std::string &error)
    {
        const std::filesystem::path exeDir(reinterpret_cast<const char8_t *>(Path::Executable.c_str()));
#if defined(PE_WIN32)
        const std::filesystem::path exe = exeDir / "PhasmaCook.exe";
#else
        const std::filesystem::path exe = exeDir / "PhasmaCook";
#endif
        std::error_code ec;
        if (!std::filesystem::exists(exe, ec))
        {
            error = "PhasmaCook is not beside the executable; build the PhasmaCook target to import source models";
            return {};
        }
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
        if (ec)
        {
            error = "cannot locate the temporary directory";
            return {};
        }
#if defined(PE_WIN32)
        const unsigned long processId = GetCurrentProcessId();
#else
        const unsigned long processId = static_cast<unsigned long>(getpid());
#endif
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path staging =
            tempDir / ("phasma_animator_import_" + std::to_string(processId) + "_" + std::to_string(stamp));
        if (!std::filesystem::create_directory(staging, ec) || ec)
        {
            error = "cannot create a private temporary import directory";
            return {};
        }
        const std::filesystem::path out = staging / "import.pemesh";
        bool ok = false, timedOut = false;
#if defined(PE_WIN32)
        std::wstring cmd = L"\"" + exe.wstring() + L"\" \"" + source.wstring() + L"\" \"" + out.wstring() + L"\"";
        std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
        buffer.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.wstring().c_str(), buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &si, &pi))
        {
            std::filesystem::remove_all(staging, ec);
            error = "could not start PhasmaCook";
            return {};
        }
        const DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);
        timedOut = waitResult == WAIT_TIMEOUT;
        if (timedOut)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        DWORD exitCode = 1;
        if (waitResult == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ok = waitResult == WAIT_OBJECT_0 && exitCode == 0;
#else
        const std::string exeStr = exe.string(), sourceStr = source.string(), outStr = out.string();
        const pid_t pid = fork();
        if (pid < 0)
        {
            std::filesystem::remove_all(staging, ec);
            error = "could not start PhasmaCook";
            return {};
        }
        if (pid == 0)
        {
            execl(exeStr.c_str(), exeStr.c_str(), sourceStr.c_str(), outStr.c_str(), nullptr);
            _exit(127);
        }
        int status = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        pid_t waitResult = 0;
        while ((waitResult = waitpid(pid, &status, WNOHANG)) == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (waitResult == 0)
            waitResult = waitpid(pid, &status, WNOHANG);
        timedOut = waitResult == 0;
        if (timedOut)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        ok = waitResult == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
        if (!ok || !std::filesystem::exists(out, ec))
        {
            std::filesystem::remove_all(staging, ec);
            error = timedOut ? "PhasmaCook timed out while importing " + source.filename().string()
                             : "PhasmaCook could not import " + source.filename().string();
            return {};
        }
        return out;
    }

    bool LoadPemeshRig(const std::filesystem::path &path, ImportedRig &out, std::string &error)
    {
        ModelAsset *model = ModelAssetCooked::Load(path);
        if (!model)
        {
            error = "cannot load " + path.generic_string();
            return false;
        }
        out.skeleton = model->GetSkeleton();
        out.clips = model->GetAnimations();
        delete model; // never added to a scene: nothing else holds it
        return true;
    }

    bool LoadRig(const std::filesystem::path &path, ImportedRig &out, std::string &error)
    {
        const std::string ext = Lower(path.extension().string());
        if (ext == ".bvh")
            return LoadBvh(path, out, error);
        if (ModelAssetCooked::IsCookedPath(path))
            return LoadPemeshRig(path, out, error);
        const std::filesystem::path cooked = CookToTemp(path, error);
        if (cooked.empty())
            return false;
        const bool ok = LoadPemeshRig(cooked, out, error);
        std::error_code ec;
        std::filesystem::remove_all(cooked.parent_path(), ec);
        return ok;
    }

    // ---------------------------------------------------------------------------------------------------------
    // retarget by name
    // ---------------------------------------------------------------------------------------------------------
    void RetargetClip(const AnimationClip &source, const Skeleton &sourceSkeleton, const Skeleton &target,
                      AnimationClip &out, RetargetReport &report, float positionScale)
    {
        report = RetargetReport{};
        out.name = source.name;
        out.duration = source.duration;
        out.ticksPerSecond = source.ticksPerSecond;
        out.channels.clear();
        out.rootMotion = {};

        if (!ValidClip(source, sourceSkeleton) || !ValidSkeleton(target) || !std::isfinite(positionScale))
        {
            report.error = "the source clip or skeleton data is invalid";
            return;
        }

        const std::vector<int> sourceOrder = TopologicalOrder(sourceSkeleton);
        const std::vector<int> targetOrder = TopologicalOrder(target);
        std::vector<mat4> sourceBind, targetBind;
        BindGlobals(sourceSkeleton, sourceOrder, sourceBind);
        BindGlobals(target, targetOrder, targetBind);
        const std::vector<int> sourceChannel = BoneToChannel(sourceSkeleton, source);

        // name matches
        std::vector<int> match(target.GetBoneCount(), -1);
        std::vector<bool> sourceUsed(sourceSkeleton.GetBoneCount(), false);
        for (int t = 0; t < target.GetBoneCount(); t++)
        {
            int s = sourceSkeleton.GetBoneIndex(target.bones[t].name);
            if (s < 0)
            {
                const std::string wanted = Lower(target.bones[t].name);
                for (int k = 0; k < sourceSkeleton.GetBoneCount() && s < 0; k++)
                    if (Lower(sourceSkeleton.bones[k].name) == wanted)
                        s = k;
            }
            match[t] = s;
            if (s >= 0)
            {
                sourceUsed[s] = true;
                report.matchedBones++;
            }
            else
                report.unmatchedTarget.push_back(target.bones[t].name);
        }
        for (int s = 0; s < sourceSkeleton.GetBoneCount(); s++)
            if (!sourceUsed[s])
                report.unmatchedSource.push_back(sourceSkeleton.bones[s].name);
        if (report.matchedBones == 0)
            return;

        // the travel: the source location bone onto the same-named target bone, scaled by the rigs' heights
        const int sourceLocation = LocationBone(sourceSkeleton, source);
        int targetLocation = -1;
        if (sourceLocation >= 0)
            for (int t = 0; t < target.GetBoneCount(); t++)
                if (match[t] == sourceLocation)
                    targetLocation = t;
        float scale = positionScale;
        if (scale <= 0.f)
        {
            const float sourceHeight = RigHeight(sourceSkeleton, sourceBind), targetHeight = RigHeight(target, targetBind);
            scale = sourceHeight > 1e-5f && targetHeight > 1e-5f ? targetHeight / sourceHeight : 1.f;
        }

        // Preserve a true identity retarget byte-for-byte apart from remapped bone indices. This also keeps each
        // key's interpolation instead of unnecessarily resampling an already compatible rig.
        bool sameRig = sourceSkeleton.GetBoneCount() == target.GetBoneCount() && std::abs(scale - 1.f) <= 1e-5f &&
                       MatricesNear(sourceSkeleton.rootTransform, target.rootTransform);
        std::vector<int> sourceToTarget(sourceSkeleton.GetBoneCount(), -1);
        for (int t = 0; sameRig && t < target.GetBoneCount(); ++t)
        {
            const int s = match[t];
            if (s < 0 || sourceToTarget[s] >= 0 || !MatricesNear(sourceSkeleton.bones[s].localBindTransform, target.bones[t].localBindTransform) ||
                !MatricesNear(sourceSkeleton.bones[s].intermediatePrefix, target.bones[t].intermediatePrefix))
            {
                sameRig = false;
                break;
            }
            sourceToTarget[s] = t;
        }
        for (int t = 0; sameRig && t < target.GetBoneCount(); ++t)
        {
            const int sourceParent = sourceSkeleton.bones[match[t]].parentIndex;
            if (target.bones[t].parentIndex != (sourceParent >= 0 ? sourceToTarget[sourceParent] : -1))
                sameRig = false;
        }
        if (sameRig)
        {
            out = source;
            report.keysWritten = 0;
            for (AnimationChannel &channel : out.channels)
            {
                channel.boneIndex = sourceToTarget[channel.boneIndex];
                report.keysWritten +=
                    channel.positionKeys.size() + channel.rotationKeys.size() + channel.scaleKeys.size();
            }
            if (!out.rootMotion.Empty())
            {
                out.rootMotion.boneIndex = sourceToTarget[out.rootMotion.boneIndex];
                report.keysWritten += out.rootMotion.positionKeys.size();
            }
            return;
        }

        mat3 locationMap(1.f);
        if (targetLocation >= 0)
        {
            const int sourceParent = sourceSkeleton.bones[sourceLocation].parentIndex;
            const int targetParent = target.bones[targetLocation].parentIndex;
            const mat3 sourceBasis((sourceParent >= 0 ? sourceBind[sourceParent] : mat4(1.f)) *
                                   sourceSkeleton.bones[sourceLocation].intermediatePrefix);
            const mat3 targetBasis((targetParent >= 0 ? targetBind[targetParent] : mat4(1.f)) *
                                   target.bones[targetLocation].intermediatePrefix);
            if (std::abs(glm::determinant(targetBasis)) <= 1e-8f)
            {
                report.error = "the target Location carrier has a singular bind transform";
                out.channels.clear();
                return;
            }
            locationMap = glm::inverse(targetBasis) * sourceBasis * scale;
        }

        // every key time of the source, in order
        std::vector<float> times;
        for (const AnimationChannel &chan : source.channels)
        {
            for (const auto &key : chan.rotationKeys)
                times.push_back(key.time);
            for (const auto &key : chan.positionKeys)
                times.push_back(key.time);
            for (const auto &key : chan.scaleKeys)
                times.push_back(key.time);
        }
        if (times.empty())
            return;
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](float a, float b)
                                { return std::abs(a - b) < 1e-4f; }),
                    times.end());

        // one channel per matched target bone, in the target's order
        std::vector<int> outChannel(target.GetBoneCount(), -1);
        for (int t : targetOrder)
            if (match[t] >= 0 && sourceChannel[match[t]] >= 0)
            {
                AnimationChannel chan;
                chan.boneIndex = t;
                outChannel[t] = static_cast<int>(out.channels.size());
                out.channels.push_back(chan);
            }

        std::vector<mat4> sourceGlobal, targetGlobal(target.GetBoneCount(), mat4(1.f));
        for (float time : times)
        {
            PosedGlobals(sourceSkeleton, source, sourceOrder, sourceChannel, time, sourceGlobal);
            for (int t : targetOrder)
            {
                const BoneInfo &bone = target.bones[t];
                const int parent = bone.parentIndex;
                const mat4 parentGlobal = parent >= 0 ? targetGlobal[parent] : mat4(1.f);
                const int s = match[t];
                if (s < 0 || sourceChannel[s] < 0)
                {
                    targetGlobal[t] = parentGlobal * bone.localBindTransform;
                    continue;
                }
                AnimationChannel &chan = out.channels[outChannel[t]];
                const AnimationChannel &sourceChan = source.channels[sourceChannel[s]];
                vec3 sourcePos, sourceScale, sourceBindPos, sourceBindScale, targetBindPos, targetBindScale;
                quat sourceLocalRot, sourceBindLocalRot, targetBindLocalRot;
                AnimationEvaluator::SampleChannel(sourceChan, sourceSkeleton.bones[s], time, sourcePos,
                                                  sourceLocalRot, sourceScale);
                AnimationEvaluator::BindPose(sourceSkeleton.bones[s], sourceBindPos, sourceBindLocalRot,
                                             sourceBindScale);
                AnimationEvaluator::BindPose(bone, targetBindPos, targetBindLocalRot, targetBindScale);
                // the source's global rotation delta from its bind, applied to the target's bind global
                vec3 ignoredPosition, ignoredScale;
                quat sourceRot, sourceBindRot, targetBindRot;
                Decompose(sourceGlobal[s], ignoredPosition, sourceRot, ignoredScale);
                Decompose(sourceBind[s], ignoredPosition, sourceBindRot, ignoredScale);
                Decompose(targetBind[t], ignoredPosition, targetBindRot, ignoredScale);
                const quat globalRot = glm::normalize(sourceRot * glm::inverse(sourceBindRot) * targetBindRot);
                vec3 globalPos = vec3(parentGlobal * bone.localBindTransform[3]);
                bool keyPosition = false;
                if (t == targetLocation)
                {
                    const vec3 position = targetBindPos + locationMap * (sourcePos - sourceBindPos);
                    globalPos = vec3(parentGlobal * bone.intermediatePrefix * vec4(position, 1.f));
                    keyPosition = true;
                }
                const mat4 global = glm::translate(mat4(1.f), globalPos) * glm::mat4_cast(globalRot);
                const mat4 local = glm::inverse(bone.intermediatePrefix) * glm::inverse(parentGlobal) * global;
                vec3 pos, scl;
                quat rot;
                Decompose(local, pos, rot, scl);
                if (!sourceChan.scaleKeys.empty())
                {
                    for (int axis = 0; axis < 3; ++axis)
                        scl[axis] = std::abs(sourceBindScale[axis]) > 1e-8f
                                        ? targetBindScale[axis] * sourceScale[axis] / sourceBindScale[axis]
                                        : targetBindScale[axis];
                    chan.scaleKeys.push_back({time, scl, AnimationInterpolation::Linear});
                    report.keysWritten++;
                }
                targetGlobal[t] = parentGlobal * bone.intermediatePrefix * Compose(pos, rot, scl);
                if (!sourceChan.rotationKeys.empty())
                {
                    if (!chan.rotationKeys.empty() && glm::dot(chan.rotationKeys.back().value, rot) < 0.f)
                        rot = -rot; // keep the hemisphere so linear blends never spin the long way
                    chan.rotationKeys.push_back({time, rot, AnimationInterpolation::Linear});
                    report.keysWritten++;
                }
                if (keyPosition)
                {
                    chan.positionKeys.push_back({time, pos, AnimationInterpolation::Linear});
                    report.keysWritten++;
                }
            }
        }

        if (!source.rootMotion.Empty() && source.rootMotion.boneIndex < sourceSkeleton.GetBoneCount())
            for (int t = 0; t < target.GetBoneCount(); ++t)
                if (match[t] == source.rootMotion.boneIndex)
                {
                    out.rootMotion.boneIndex = t;
                    out.rootMotion.positionKeys = source.rootMotion.positionKeys;
                    for (PositionKey &key : out.rootMotion.positionKeys)
                        key.value *= scale;
                    report.keysWritten += out.rootMotion.positionKeys.size();
                    break;
                }
        std::erase_if(out.channels, [](const AnimationChannel &channel)
                      { return channel.positionKeys.empty() && channel.rotationKeys.empty() && channel.scaleKeys.empty(); });
    }

    // ---------------------------------------------------------------------------------------------------------
    // glTF 2.0 out
    // ---------------------------------------------------------------------------------------------------------
    bool WriteGltf(const std::filesystem::path &pathIn, const Skeleton &skeleton, const AnimationClip &clip,
                   std::string &error)
    {
        using nlohmann::json;
        std::filesystem::path path = pathIn;
        if (Lower(path.extension().string()) != ".gltf")
            path.replace_extension(".gltf");
        const std::filesystem::path binPath = std::filesystem::path(path).replace_extension(".bin");
        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "cannot create " + path.parent_path().generic_string();
            return false;
        }
        const int boneCount = skeleton.GetBoneCount();
        if (boneCount == 0 || !ValidClip(clip, skeleton))
        {
            error = boneCount == 0 ? "no skeleton to export" : "the clip or skeleton data is invalid";
            return false;
        }
        AnimationClip exported = clip;
        if (!clip.rootMotion.Empty() && AnimationClipTools::BakeRootMotion(exported) == 0)
        {
            error = "the extracted root motion could not be baked for export";
            return false;
        }

        std::vector<uint8_t> bin;
        json accessors = json::array(), bufferViews = json::array();
        auto align = [&]()
        {
            while (bin.size() % 4)
                bin.push_back(0);
        };
        auto pushFloats = [&](const std::vector<float> &values, const char *type, int components, bool minMax,
                              int target = 0) -> int
        {
            align();
            const size_t offset = bin.size();
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(values.data());
            bin.insert(bin.end(), bytes, bytes + values.size() * sizeof(float));
            json view = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", values.size() * sizeof(float)}};
            if (target)
                view["target"] = target;
            bufferViews.push_back(view);
            json accessor = {{"bufferView", bufferViews.size() - 1},
                             {"componentType", 5126},
                             {"count", values.size() / components},
                             {"type", type}};
            if (minMax && !values.empty())
            {
                std::vector<float> lo(components, std::numeric_limits<float>::max()),
                    hi(components, -std::numeric_limits<float>::max());
                for (size_t k = 0; k < values.size(); k++)
                {
                    lo[k % components] = std::min(lo[k % components], values[k]);
                    hi[k % components] = std::max(hi[k % components], values[k]);
                }
                accessor["min"] = lo;
                accessor["max"] = hi;
            }
            accessors.push_back(accessor);
            return static_cast<int>(accessors.size()) - 1;
        };

        // nodes: the bind pose
        const std::vector<int> order = TopologicalOrder(skeleton);
        std::vector<mat4> bindGlobal;
        BindGlobals(skeleton, order, bindGlobal);
        json nodes = json::array();
        std::vector<std::vector<int>> children(boneCount);
        std::vector<int> roots;
        for (int i = 0; i < boneCount; i++)
        {
            const int parent = skeleton.bones[i].parentIndex;
            if (parent >= 0 && parent < boneCount)
                children[parent].push_back(i);
            else
                roots.push_back(i);
        }
        for (int i = 0; i < boneCount; i++)
        {
            vec3 pos, scl;
            quat rot;
            Decompose(skeleton.bones[i].localBindTransform, pos, rot, scl);
            json node = {{"name", skeleton.bones[i].name},
                         {"translation", {pos.x, pos.y, pos.z}},
                         {"rotation", {rot.x, rot.y, rot.z, rot.w}},
                         {"scale", {scl.x, scl.y, scl.z}}};
            if (!children[i].empty())
                node["children"] = children[i];
            nodes.push_back(node);
        }

        // a one-vertex-per-joint mesh skinned 100% to that joint, so importers rebuild the skeleton as a skin
        std::vector<float> positions, weights, inverseBind;
        std::vector<uint16_t> jointIds;
        std::vector<uint16_t> indices;
        for (int i = 0; i < boneCount; i++)
        {
            const vec3 p = vec3(bindGlobal[i][3]);
            positions.insert(positions.end(), {p.x, p.y, p.z});
            weights.insert(weights.end(), {1.f, 0.f, 0.f, 0.f});
            jointIds.insert(jointIds.end(), {static_cast<uint16_t>(i), 0, 0, 0});
            const mat4 inv = glm::inverse(bindGlobal[i]);
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++)
                    inverseBind.push_back(inv[c][r]);
        }
        for (int i = 0; i + 2 < std::max(boneCount, 3); i++)
            indices.insert(indices.end(), {static_cast<uint16_t>(std::min(i, boneCount - 1)),
                                           static_cast<uint16_t>(std::min(i + 1, boneCount - 1)),
                                           static_cast<uint16_t>(std::min(i + 2, boneCount - 1))});
        const int positionAccessor = pushFloats(positions, "VEC3", 3, true, 34962);
        const int weightAccessor = pushFloats(weights, "VEC4", 4, false, 34962);
        align();
        const size_t jointOffset = bin.size();
        bin.insert(bin.end(), reinterpret_cast<const uint8_t *>(jointIds.data()),
                   reinterpret_cast<const uint8_t *>(jointIds.data()) + jointIds.size() * sizeof(uint16_t));
        bufferViews.push_back({{"buffer", 0}, {"byteOffset", jointOffset}, {"byteLength", jointIds.size() * 2}, {"target", 34962}});
        accessors.push_back({{"bufferView", bufferViews.size() - 1}, {"componentType", 5123}, {"count", boneCount}, {"type", "VEC4"}});
        const int jointAccessor = static_cast<int>(accessors.size()) - 1;
        align();
        const size_t indexOffset = bin.size();
        bin.insert(bin.end(), reinterpret_cast<const uint8_t *>(indices.data()),
                   reinterpret_cast<const uint8_t *>(indices.data()) + indices.size() * sizeof(uint16_t));
        bufferViews.push_back({{"buffer", 0}, {"byteOffset", indexOffset}, {"byteLength", indices.size() * 2}, {"target", 34963}});
        accessors.push_back({{"bufferView", bufferViews.size() - 1}, {"componentType", 5123}, {"count", indices.size()}, {"type", "SCALAR"}});
        const int indexAccessor = static_cast<int>(accessors.size()) - 1;
        const int inverseBindAccessor = pushFloats(inverseBind, "MAT4", 16, false);

        json skinJoints = json::array();
        for (int i = 0; i < boneCount; i++)
            skinJoints.push_back(i);
        const int meshNode = boneCount;
        nodes.push_back({{"name", exported.name + "_skin"}, {"mesh", 0}, {"skin", 0}});

        // The animation: prefix is folded into each local sample. A glTF sampler has one interpolation mode, while
        // Phasma stores it per segment, so mixed/Smooth tracks are baked at 60 Hz; a key one float before a Stepped
        // boundary preserves its hold without turning the whole track into STEP.
        json samplers = json::array(), channels = json::array();
        const float tps = exported.ticksPerSecond > 0.f ? exported.ticksPerSecond : 25.f;
        for (const AnimationChannel &chan : exported.channels)
        {
            const int bone = chan.boneIndex;
            if (bone < 0 || bone >= boneCount)
                continue;
            auto addTrack = [&](const char *path, const auto &keys, const char *type, int components, int track)
            {
                if (keys.empty())
                    return true;
                bool allLinear = true, allStepped = keys.size() > 1;
                for (size_t i = 0; i + 1 < keys.size(); ++i)
                {
                    allLinear = allLinear && keys[i].interpolation == AnimationInterpolation::Linear;
                    allStepped = allStepped && keys[i].interpolation == AnimationInterpolation::Stepped;
                }
                const bool bake = !allLinear && !allStepped;
                std::vector<float> times;
                times.reserve(keys.size());
                for (const auto &key : keys)
                    times.push_back(key.time);
                if (bake)
                {
                    const float sampleStep = std::max(tps / 60.f, 1e-5f);
                    for (size_t i = 0; i + 1 < keys.size(); ++i)
                    {
                        if (keys[i].interpolation == AnimationInterpolation::Smooth)
                            for (float t = keys[i].time + sampleStep; t < keys[i + 1].time; t += sampleStep)
                            {
                                if (times.size() >= 1000000)
                                {
                                    error = "the clip needs too many samples to preserve interpolation";
                                    return false;
                                }
                                times.push_back(t);
                            }
                        else if (keys[i].interpolation == AnimationInterpolation::Stepped &&
                                 keys[i + 1].time > keys[i].time)
                        {
                            const float span = keys[i + 1].time - keys[i].time;
                            times.push_back(keys[i + 1].time - std::min(span * 0.001f, sampleStep * 0.01f));
                        }
                    }
                }
                std::sort(times.begin(), times.end());
                times.erase(std::unique(times.begin(), times.end()), times.end());
                std::vector<float> seconds, values;
                seconds.reserve(times.size());
                values.reserve(times.size() * components);
                for (float t : times)
                {
                    vec3 pos, scl;
                    quat rot;
                    AnimationEvaluator::SampleChannel(chan, skeleton.bones[bone], t, pos, rot, scl);
                    Decompose(skeleton.bones[bone].intermediatePrefix * Compose(pos, rot, scl), pos, rot, scl);
                    seconds.push_back(t / tps);
                    if (track == 0)
                        values.insert(values.end(), {pos.x, pos.y, pos.z});
                    else if (track == 1)
                    {
                        if (!values.empty())
                        {
                            const quat previous(values[values.size() - 1], values[values.size() - 4],
                                                values[values.size() - 3], values[values.size() - 2]);
                            if (glm::dot(previous, rot) < 0.f)
                                rot = -rot;
                        }
                        values.insert(values.end(), {rot.x, rot.y, rot.z, rot.w});
                    }
                    else
                        values.insert(values.end(), {scl.x, scl.y, scl.z});
                }
                const int input = pushFloats(seconds, "SCALAR", 1, true);
                const int output = pushFloats(values, type, components, false);
                samplers.push_back({{"input", input},
                                    {"output", output},
                                    {"interpolation", allStepped ? "STEP" : "LINEAR"}});
                channels.push_back({{"sampler", samplers.size() - 1}, {"target", {{"node", bone}, {"path", path}}}});
                return true;
            };
            if (!addTrack("translation", chan.positionKeys, "VEC3", 3, 0) ||
                !addTrack("rotation", chan.rotationKeys, "VEC4", 4, 1) ||
                !addTrack("scale", chan.scaleKeys, "VEC3", 3, 2))
                return false;
        }

        json doc;
        doc["asset"] = {{"version", "2.0"}, {"generator", "PhasmaAnimator"}};
        doc["scene"] = 0;
        json sceneNodes = roots;
        sceneNodes.push_back(meshNode);
        doc["scenes"] = {{{"name", exported.name}, {"nodes", sceneNodes}}};
        doc["nodes"] = nodes;
        doc["meshes"] = {{{"name", exported.name + "_skin"},
                          {"primitives", {{{"attributes", {{"POSITION", positionAccessor}, {"JOINTS_0", jointAccessor}, {"WEIGHTS_0", weightAccessor}}}, {"indices", indexAccessor}}}}}};
        doc["skins"] = {{{"joints", skinJoints}, {"inverseBindMatrices", inverseBindAccessor}, {"skeleton", roots.empty() ? 0 : roots[0]}}};
        if (!samplers.empty())
            doc["animations"] = {{{"name", exported.name}, {"samplers", samplers}, {"channels", channels}}};
        doc["accessors"] = accessors;
        doc["bufferViews"] = bufferViews;
        doc["buffers"] = {{{"byteLength", bin.size()}, {"uri", binPath.filename().string()}}};

        std::ofstream binOut(binPath, std::ios::binary | std::ios::trunc);
        std::ofstream jsonOut(path, std::ios::binary | std::ios::trunc);
        if (!binOut || !jsonOut)
        {
            error = "cannot write " + path.generic_string();
            return false;
        }
        binOut.write(reinterpret_cast<const char *>(bin.data()), static_cast<std::streamsize>(bin.size()));
        jsonOut << doc.dump(1);
        if (!binOut || !jsonOut)
        {
            error = "failed while writing " + path.generic_string();
            return false;
        }
        return true;
    }
} // namespace pe::ClipExchange
