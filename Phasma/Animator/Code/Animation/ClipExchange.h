#pragma once

#include "Animation/AnimationTypes.h"

namespace pe::ClipExchange
{
    // A rig read from a file: the skeleton the clips are keyed on and the clips themselves.
    struct ImportedRig
    {
        Skeleton skeleton;
        std::vector<AnimationClip> clips;
    };

    // Reads a BVH (offsets become the bind locals, no prefix; one clip named after the file, ticks = frames).
    bool LoadBvh(const std::filesystem::path &path, ImportedRig &out, std::string &error);
    // Runs the cook tool beside the Animator without opening a window. output is a durable .pemesh chosen by the caller.
    bool CookModel(const std::filesystem::path &source, const std::filesystem::path &output, std::string &error);
    // Cooks a source model (glTF / GLB / FBX / OBJ ...) through PhasmaCook.exe beside the executable into a temp
    // .pemesh and returns that path; empty with the error when the tool is missing or fails.
    std::filesystem::path CookToTemp(const std::filesystem::path &source, std::string &error);
    // Reads the skeleton and clips of a cooked .pemesh without touching the scene (the model is freed here).
    bool LoadPemeshRig(const std::filesystem::path &path, ImportedRig &out, std::string &error);
    // Any of the above by extension.
    bool LoadRig(const std::filesystem::path &path, ImportedRig &out, std::string &error);

    struct RetargetReport
    {
        size_t matchedBones = 0;
        size_t keysWritten = 0;
        std::vector<std::string> unmatchedTarget; // target bones the source does not have
        std::vector<std::string> unmatchedSource; // source bones the target does not have
        std::string error;
    };
    // Copies `source` onto `target` by bone name. Rotations carry the source's global delta from its bind pose onto
    // the target's bind pose, then solve the target's local (the same rig reduces to a plain copy). The source's
    // location bone (the shallowest with position keys) drives the target's bone of the same name, its offset from
    // the bind scaled by the rigs' heights (positionScale 0 = that automatic ratio).
    void RetargetClip(const AnimationClip &source, const Skeleton &sourceSkeleton, const Skeleton &target,
                      AnimationClip &out, RetargetReport &report, float positionScale = 0.f);

    // Writes the skeleton (bind-pose nodes, a one-vertex-per-joint skinned mesh so importers see a skin) and the
    // clip as glTF 2.0: <path>.gltf with a .bin beside it.
    bool WriteGltf(const std::filesystem::path &path, const Skeleton &skeleton, const AnimationClip &clip,
                   std::string &error);
} // namespace pe::ClipExchange
