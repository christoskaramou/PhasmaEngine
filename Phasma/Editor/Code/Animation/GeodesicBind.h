#pragma once

#include "Animation/AnimationTypes.h"

#include <array>
#include <span>
#include <unordered_map>
#include <vector>

namespace pe::GeodesicBind
{
    struct Bone
    {
        vec3 head = vec3(0.f);
        vec3 tail = vec3(0.f, 0.1f, 0.f);
        float headRadius = 0.05f;
        float tailRadius = 0.05f;
        int parent = -1;
        bool rigid = false;  // its region never blends; it still bleeds into soft neighbours
        bool spline = false; // reported as the label only: the caller applies its chain weights
    };

    struct VertexWeights
    {
        int label = -1; // geodesically nearest bone; -1 = no bone's surface reaches this vertex
        std::array<int, 4> joints{};
        std::array<float, 4> weights{};
    };

    // Mesh-aware bind weights: each bone seeds the surface its capsule sits inside (rays from the axis
    // that leave the mesh within the capsule radius), distance then travels along the welded surface,
    // so a hand resting against a hip stays a hand. The nearest bone labels a vertex; only bones within
    // two hierarchy steps blend across the seam, in a band as wide as the thinner bone's radius.
    struct Result
    {
        std::vector<VertexWeights> welded; // per welded vertex
        std::vector<uint32_t> weld;        // input vertex -> welded vertex
        std::unordered_map<uint64_t, uint32_t> byKey;
        std::vector<int> seedCount; // per bone; 0 = the capsule holds no surface, the bone gets no region
        float quantum = 0.f;
        size_t unreached = 0;

        const VertexWeights *At(size_t vertex) const;   // by input vertex index
        const VertexWeights *Find(const vec3 &p) const; // by rig-space position (the scene's copy)
    };

    Result Solve(std::span<const vec3> positions, std::span<const uint32_t> triangles, std::span<const Bone> bones,
                 float modelHeight);
} // namespace pe::GeodesicBind
