#pragma once

#include "Animation/AnimationTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pe::RigWeightStroke
{
    struct SkinWeight
    {
        std::array<uint32_t, 4> joints{};
        std::array<float, 4> weights{};
    };

    enum class Mode : uint8_t
    {
        Add,
        Erase,
        Smooth
    };

    // center/radius use the same world or model space as posedPositions. Smooth samples the target
    // bone across a small rest-space neighborhood, so a posed shoulder/hip seam relaxes without
    // becoming a generic all-bone vertex paint operation.
    struct Stroke
    {
        std::string bone;
        vec3 center = vec3(0.f);
        float radius = 0.1f;
        float strength = 0.5f;    // clamped to 0..1
        float smoothRadius = 0.f; // rest space; <= 0 uses 20% of radius
        Mode mode = Mode::Add;
    };

    enum class Status : uint8_t
    {
        Success,
        SizeMismatch,
        EmptySkeleton,
        UnknownBone,
        InvalidStroke
    };

    struct Result
    {
        Status status = Status::Success;
        int boneIndex = -1;
        size_t affectedVertices = 0;
        size_t skippedSplineVertices = 0;

        explicit operator bool() const { return status == Status::Success; }
    };

    // Touched ordinary vertices are canonicalized: duplicate/invalid influences are removed, the
    // strongest four remain, and weights sum to 1. Vertices carrying negative Catmull-Rom spline
    // weights are skipped and preserved byte-for-byte. On validation failure outWeights is untouched.
    Result Apply(std::span<const vec3> restPositions,
                 std::span<const vec3> posedPositions,
                 std::span<const SkinWeight> baseWeights,
                 const Skeleton &skeleton,
                 const Stroke &stroke,
                 std::vector<SkinWeight> &outWeights);
} // namespace pe::RigWeightStroke
