#include "RigWeightStroke.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace pe::RigWeightStroke
{
    namespace
    {
        struct Influence
        {
            uint32_t joint = 0;
            float weight = 0.f;
        };

        using Cell = std::array<int64_t, 3>;

        bool Finite(const vec3 &v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        float Falloff(float distance, float radius)
        {
            const float t = std::clamp(1.f - distance / radius, 0.f, 1.f);
            return t * t * (3.f - 2.f * t);
        }

        std::vector<Influence> Unpack(const SkinWeight &weights, int boneCount)
        {
            std::vector<Influence> result;
            result.reserve(4);
            for (int i = 0; i < 4; i++)
            {
                const uint32_t joint = weights.joints[i];
                const float weight = weights.weights[i];
                if (joint >= static_cast<uint32_t>(boneCount) || !std::isfinite(weight) || weight <= 0.f)
                    continue;
                const auto found = std::find_if(result.begin(), result.end(), [joint](const Influence &influence)
                                                { return influence.joint == joint; });
                if (found == result.end())
                    result.push_back({joint, weight});
                else
                    found->weight += weight;
            }
            return result;
        }

        SkinWeight Pack(std::vector<Influence> influences, uint32_t fallbackJoint)
        {
            influences.erase(std::remove_if(influences.begin(), influences.end(), [](const Influence &influence)
                                            { return !std::isfinite(influence.weight) || influence.weight <= 0.f; }),
                             influences.end());
            std::sort(influences.begin(), influences.end(), [](const Influence &a, const Influence &b)
                      { return a.weight != b.weight ? a.weight > b.weight : a.joint < b.joint; });
            if (influences.size() > 4)
                influences.resize(4);

            float total = 0.f;
            for (const Influence &influence : influences)
                total += influence.weight;
            if (total <= 1e-8f)
                influences = {{fallbackJoint, 1.f}}, total = 1.f;

            SkinWeight result;
            for (int i = 0; i < static_cast<int>(influences.size()); i++)
            {
                result.joints[i] = influences[i].joint;
                result.weights[i] = influences[i].weight / total;
            }
            return result;
        }

        float BoneWeight(const SkinWeight &weights, uint32_t bone)
        {
            for (int i = 0; i < 4; i++)
                if (weights.joints[i] == bone)
                    return weights.weights[i];
            return 0.f;
        }

        bool HasNegativeWeight(const SkinWeight &weights)
        {
            return std::any_of(weights.weights.begin(), weights.weights.end(), [](float weight)
                               { return weight < 0.f; });
        }

        SkinWeight SetBoneWeight(const SkinWeight &base, uint32_t bone, float desired, int boneCount)
        {
            std::vector<Influence> influences = Unpack(base, boneCount);
            desired = std::clamp(desired, 0.f, 1.f);
            influences.erase(std::remove_if(influences.begin(), influences.end(), [bone](const Influence &influence)
                                            { return influence.joint == bone; }),
                             influences.end());
            if (desired > 0.f && influences.size() >= 4)
            {
                const auto weakest = std::min_element(influences.begin(), influences.end(), [](const Influence &a, const Influence &b)
                                                      { return a.weight != b.weight ? a.weight < b.weight : a.joint > b.joint; });
                influences.erase(weakest);
            }
            float otherTotal = 0.f;
            for (const Influence &influence : influences)
                otherTotal += influence.weight;
            if (otherTotal > 1e-8f)
                for (Influence &influence : influences)
                    influence.weight *= (1.f - desired) / otherTotal;
            else if (desired < 1.f)
                desired = 1.f; // a lone influence cannot be erased without a replacement bone
            if (desired > 0.f)
                influences.push_back({bone, desired});
            return Pack(std::move(influences), bone);
        }

        Cell CellFor(const vec3 &position, float cellSize)
        {
            return {static_cast<int64_t>(std::floor(position.x / cellSize)),
                    static_cast<int64_t>(std::floor(position.y / cellSize)),
                    static_cast<int64_t>(std::floor(position.z / cellSize))};
        }
    } // namespace

    Result Apply(std::span<const vec3> restPositions,
                 std::span<const vec3> posedPositions,
                 std::span<const SkinWeight> baseWeights,
                 const Skeleton &skeleton,
                 const Stroke &stroke,
                 std::vector<SkinWeight> &outWeights)
    {
        Result result;
        if (restPositions.size() != posedPositions.size() || restPositions.size() != baseWeights.size())
        {
            result.status = Status::SizeMismatch;
            return result;
        }
        if (skeleton.GetBoneCount() <= 0)
        {
            result.status = Status::EmptySkeleton;
            return result;
        }
        result.boneIndex = skeleton.GetBoneIndex(stroke.bone);
        if (result.boneIndex < 0)
        {
            result.status = Status::UnknownBone;
            return result;
        }
        if (!Finite(stroke.center) || !std::isfinite(stroke.radius) || stroke.radius <= 0.f ||
            !std::isfinite(stroke.strength) || !std::isfinite(stroke.smoothRadius) ||
            (stroke.mode != Mode::Add && stroke.mode != Mode::Erase && stroke.mode != Mode::Smooth))
        {
            result.status = Status::InvalidStroke;
            return result;
        }

        const int boneCount = skeleton.GetBoneCount();
        const uint32_t targetBone = static_cast<uint32_t>(result.boneIndex);
        outWeights.assign(baseWeights.begin(), baseWeights.end());

        std::vector<size_t> affected;
        std::vector<float> falloffs;
        affected.reserve(posedPositions.size() / 8);
        falloffs.reserve(posedPositions.size() / 8);
        for (size_t i = 0; i < posedPositions.size(); i++)
        {
            if (!Finite(posedPositions[i]))
                continue;
            const float distance = glm::length(posedPositions[i] - stroke.center);
            if (distance >= stroke.radius)
                continue;
            if (HasNegativeWeight(baseWeights[i]))
            {
                result.skippedSplineVertices++;
                continue;
            }
            outWeights[i] = Pack(Unpack(baseWeights[i], boneCount), 0);
            affected.push_back(i);
            falloffs.push_back(Falloff(distance, stroke.radius));
        }
        result.affectedVertices = affected.size();
        if (affected.empty())
            return result;

        const float strength = std::clamp(stroke.strength, 0.f, 1.f);
        std::vector<float> smoothTargets;
        if (stroke.mode == Mode::Smooth)
        {
            const float smoothRadius = stroke.smoothRadius > 0.f ? stroke.smoothRadius : stroke.radius * 0.2f;
            std::map<Cell, std::vector<size_t>> cells;
            for (size_t vertex : affected)
                if (Finite(restPositions[vertex]))
                    cells[CellFor(restPositions[vertex], smoothRadius)].push_back(vertex);

            smoothTargets.resize(affected.size());
            const float radiusSq = smoothRadius * smoothRadius;
            for (size_t k = 0; k < affected.size(); k++)
            {
                const size_t vertex = affected[k];
                if (!Finite(restPositions[vertex]))
                {
                    smoothTargets[k] = BoneWeight(outWeights[vertex], targetBone);
                    continue;
                }
                const Cell cell = CellFor(restPositions[vertex], smoothRadius);
                float weighted = 0.f, total = 0.f;
                for (int z = -1; z <= 1; z++)
                    for (int y = -1; y <= 1; y++)
                        for (int x = -1; x <= 1; x++)
                        {
                            const auto found = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
                            if (found == cells.end())
                                continue;
                            for (size_t neighbor : found->second)
                            {
                                const vec3 delta = restPositions[neighbor] - restPositions[vertex];
                                const float distanceSq = glm::dot(delta, delta);
                                if (distanceSq > radiusSq)
                                    continue;
                                const float weight = Falloff(std::sqrt(distanceSq), smoothRadius);
                                weighted += BoneWeight(outWeights[neighbor], targetBone) * weight;
                                total += weight;
                            }
                        }
                smoothTargets[k] = total > 1e-8f ? weighted / total : BoneWeight(outWeights[vertex], targetBone);
            }
        }

        for (size_t k = 0; k < affected.size(); k++)
        {
            const size_t vertex = affected[k];
            const float current = BoneWeight(outWeights[vertex], targetBone);
            const float alpha = strength * falloffs[k];
            float desired = current;
            switch (stroke.mode)
            {
            case Mode::Add:
                desired = current + (1.f - current) * alpha;
                break;
            case Mode::Erase:
                desired = current * (1.f - alpha);
                break;
            case Mode::Smooth:
                desired = glm::mix(current, smoothTargets[k], alpha);
                break;
            }
            outWeights[vertex] = SetBoneWeight(outWeights[vertex], targetBone, desired, boneCount);
        }
        return result;
    }
} // namespace pe::RigWeightStroke
