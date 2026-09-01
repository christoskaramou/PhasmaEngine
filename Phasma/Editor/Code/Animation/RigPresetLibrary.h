#pragma once

#include "Base/Math.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pe
{
    struct RigPresetBone
    {
        std::string name;
        std::string parentName;
        int parent = -1;
        vec3 head = vec3(0.f);
        vec3 tail = vec3(0.f, 0.1f, 0.f);
        float headRadius = 0.05f;
        float tailRadius = 0.05f;
        bool rigid = false;
        bool spline = false;
        float swingLimitDegrees = 0.f;
        float twistLimitDegrees = 0.f;
        std::vector<std::string> shellPatterns;
    };

    struct RigPresetLock
    {
        std::string bone;
        std::string target;
        vec3 anchor = vec3(0.f);
        bool hasAnchor = false;
        float reach = 1.f;
        bool enabled = true;
    };

    // Normalized Y stations (and hip / shoulder half-widths in X) of a biped template. Presets carrying them are
    // fitted to landmarks measured on the mesh instead of the bounding box alone; all zero = bounds fit only.
    struct RigPresetLandmarks
    {
        float feet = 0.f, hips = 0.f, shoulders = 0.f, neck = 0.f, top = 0.f;
        float hipHalfWidth = 0.f, shoulderHalfWidth = 0.f;
        bool valid() const { return top > feet; }
    };

    // The same stations found on a model, in rig units. defaulted.* says which fell back to a proportion.
    struct MeasuredLandmarks
    {
        float feet = 0.f, hips = 0.f, shoulders = 0.f, neck = 0.f, top = 0.f;
        float hipHalfWidth = 0.f, shoulderHalfWidth = 0.f;
        struct
        {
            bool hips = false, shoulders = false, neck = false;
        } defaulted;
    };

    struct RigPreset
    {
        std::string id;
        std::string name;
        std::string description;
        std::filesystem::path sourcePath;
        std::vector<RigPresetBone> bones;
        std::vector<std::string> pins;
        std::vector<RigPresetLock> locks;
        RigPresetLandmarks landmarks;
    };

    // A RigPresetBone fitted to one model. Coordinates are in the target model's rig space and
    // shell is the first unclaimed target shell matching the preset's ordered glob patterns.
    struct FittedRigPresetBone
    {
        std::string name;
        int parent = -1;
        vec3 head = vec3(0.f);
        vec3 tail = vec3(0.f, 0.1f, 0.f);
        float headRadius = 0.05f;
        float tailRadius = 0.05f;
        bool rigid = false;
        bool spline = false;
        float swingLimitDegrees = 0.f;
        float twistLimitDegrees = 0.f;
        std::string shell;
    };

    struct FittedRigPresetLock
    {
        std::string bone;
        std::string target;
        vec3 anchor = vec3(0.f);
        bool hasAnchor = false;
        float reach = 1.f;
        bool enabled = true;
    };

    class RigPresetLibrary
    {
    public:
        // Generic Blender-named templates (humanoid, quadruped) in the same normalized space as project files.
        static std::span<const RigPreset> BuiltIn();
        static std::filesystem::path ProjectPresetDirectory();
        static std::vector<RigPreset> LoadProjectPresets(std::vector<std::string> *errors = nullptr);
        static std::vector<RigPreset> LoadDirectory(const std::filesystem::path &directory,
                                                    std::vector<std::string> *errors = nullptr);
        static bool LoadFile(const std::filesystem::path &path, RigPreset &preset, std::string &error);

        // Slices the point cloud in Y to find the crotch, shoulders and neck of a standing biped.
        static bool MeasureBiped(std::span<const vec3> points, const AABB &bounds, MeasuredLandmarks &out);

        // Preset positions are normalized to Y-up bounds: X/Z use -1..1 across each half-extent,
        // while Y uses 0..1 from bounds.min.y to bounds.max.y. Radii are fractions of bounds height.
        // With landmarks (and a preset that carries template stations) Y and X are remapped onto them.
        static bool Fit(const RigPreset &preset, const AABB &bounds, std::span<const std::string> shellNames,
                        std::vector<FittedRigPresetBone> &bones, std::string &error,
                        const MeasuredLandmarks *landmarks = nullptr,
                        std::vector<FittedRigPresetLock> *locks = nullptr);
    };
} // namespace pe
