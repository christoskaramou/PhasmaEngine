#include "Voxel/VoxelCollider.h"

namespace pe::voxel
{
    namespace
    {
        constexpr float kInfinity = 3.402823466e+38F;
        constexpr float kCellEpsilon = 0.0001f;

        int FloorToInt(float v)
        {
            return static_cast<int>(std::floor(v));
        }

        float Component(const vec3 &v, int axis)
        {
            if (axis == 0)
                return v.x;
            if (axis == 1)
                return v.y;
            return v.z;
        }

        void SetComponent(vec3 &v, int axis, float value)
        {
            if (axis == 0)
                v.x = value;
            else if (axis == 1)
                v.y = value;
            else
                v.z = value;
        }

        int CellComponent(const BlockPos &cell, int axis)
        {
            if (axis == 0)
                return cell.x;
            if (axis == 1)
                return cell.y;
            return cell.z;
        }

        void SetCellComponent(BlockPos &cell, int axis, int value)
        {
            if (axis == 0)
                cell.x = value;
            else if (axis == 1)
                cell.y = value;
            else
                cell.z = value;
        }

        BlockPos CellFromPoint(const vec3 &p)
        {
            return {FloorToInt(p.x), FloorToInt(p.y), FloorToInt(p.z)};
        }

        int StepFor(float v)
        {
            if (v > 0.0f)
                return 1;
            if (v < 0.0f)
                return -1;
            return 0;
        }

        float InitialTMax(float origin, int cell, float dir, int step)
        {
            if (step == 0)
                return kInfinity;

            const float boundary = step > 0 ? static_cast<float>(cell + 1) : static_cast<float>(cell);
            return (boundary - origin) / dir;
        }

        float TDelta(float dir, int step)
        {
            if (step == 0)
                return kInfinity;

            return 1.0f / std::abs(dir);
        }

        vec3 NormalForStep(int axis, int step)
        {
            if (axis == 0)
                return vec3(static_cast<float>(-step), 0.0f, 0.0f);
            if (axis == 1)
                return vec3(0.0f, static_cast<float>(-step), 0.0f);
            return vec3(0.0f, 0.0f, static_cast<float>(-step));
        }

        struct CellRange
        {
            int minCell;
            int maxCell;
        };

        CellRange OverlapRange(const vec3 &pos, const vec3 &halfExtents, int axis)
        {
            const float minFace = Component(pos, axis) - Component(halfExtents, axis);
            const float maxFace = Component(pos, axis) + Component(halfExtents, axis);
            return {FloorToInt(minFace), FloorToInt(maxFace - kCellEpsilon)};
        }

        bool HasSolidOnFace(
            int axis,
            int axisCell,
            const CellRange &firstRange,
            const CellRange &secondRange,
            const std::function<bool(int, int, int)> &isSolid)
        {
            const int firstAxis = (axis + 1) % 3;
            const int secondAxis = (axis + 2) % 3;

            for (int first = firstRange.minCell; first <= firstRange.maxCell; ++first)
            {
                for (int second = secondRange.minCell; second <= secondRange.maxCell; ++second)
                {
                    int x = 0;
                    int y = 0;
                    int z = 0;

                    if (axis == 0)
                        x = axisCell;
                    else if (firstAxis == 0)
                        x = first;
                    else
                        x = second;

                    if (axis == 1)
                        y = axisCell;
                    else if (firstAxis == 1)
                        y = first;
                    else
                        y = second;

                    if (axis == 2)
                        z = axisCell;
                    else if (firstAxis == 2)
                        z = first;
                    else
                        z = second;

                    if (isSolid(x, y, z))
                        return true;
                }
            }

            return false;
        }

        void ResolveAxis(
            vec3 &pos,
            const vec3 &halfExtents,
            int axis,
            float amount,
            const std::function<bool(int, int, int)> &isSolid)
        {
            if (amount == 0.0f)
                return;

            const float oldCenter = Component(pos, axis);
            const float half = Component(halfExtents, axis);
            const float oldFace = oldCenter + (amount > 0.0f ? half : -half);

            SetComponent(pos, axis, oldCenter + amount);

            const float newCenter = Component(pos, axis);
            const float newFace = newCenter + (amount > 0.0f ? half : -half);
            const int startCell = FloorToInt(oldFace);
            const int endCell = FloorToInt(newFace);
            const int firstAxis = (axis + 1) % 3;
            const int secondAxis = (axis + 2) % 3;
            const CellRange firstRange = OverlapRange(pos, halfExtents, firstAxis);
            const CellRange secondRange = OverlapRange(pos, halfExtents, secondAxis);

            if (amount > 0.0f)
            {
                for (int cell = startCell; cell <= endCell; ++cell)
                {
                    if (HasSolidOnFace(axis, cell, firstRange, secondRange, isSolid))
                    {
                        SetComponent(pos, axis, static_cast<float>(cell) - half);
                        return;
                    }
                }
            }
            else
            {
                for (int cell = startCell; cell >= endCell; --cell)
                {
                    if (HasSolidOnFace(axis, cell, firstRange, secondRange, isSolid))
                    {
                        SetComponent(pos, axis, static_cast<float>(cell + 1) + half);
                        return;
                    }
                }
            }
        }
    } // namespace

    VoxelRayHit RaycastVoxels(
        const vec3 &origin,
        const vec3 &dir,
        float maxDist,
        const std::function<bool(int, int, int)> &isSolid)
    {
        BlockPos cell = CellFromPoint(origin);

        if (isSolid(cell.x, cell.y, cell.z))
            return {true, cell, cell, vec3(0.0f, 0.0f, 0.0f), 0.0f};

        const float dirLenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        if (dirLenSq == 0.0f || maxDist < 0.0f)
            return {false, cell, cell, vec3(0.0f, 0.0f, 0.0f), 0.0f};

        const float invLen = 1.0f / std::sqrt(dirLenSq);
        const vec3 rayDir(dir.x * invLen, dir.y * invLen, dir.z * invLen);

        const int stepX = StepFor(rayDir.x);
        const int stepY = StepFor(rayDir.y);
        const int stepZ = StepFor(rayDir.z);

        float tMaxX = InitialTMax(origin.x, cell.x, rayDir.x, stepX);
        float tMaxY = InitialTMax(origin.y, cell.y, rayDir.y, stepY);
        float tMaxZ = InitialTMax(origin.z, cell.z, rayDir.z, stepZ);
        const float tDeltaX = TDelta(rayDir.x, stepX);
        const float tDeltaY = TDelta(rayDir.y, stepY);
        const float tDeltaZ = TDelta(rayDir.z, stepZ);

        while (true)
        {
            int axis = 0;
            float nextT = tMaxX;
            if (tMaxY < nextT)
            {
                axis = 1;
                nextT = tMaxY;
            }
            if (tMaxZ < nextT)
            {
                axis = 2;
                nextT = tMaxZ;
            }

            if (nextT > maxDist || nextT == kInfinity)
                return {false, cell, cell, vec3(0.0f, 0.0f, 0.0f), maxDist};

            const BlockPos adjacent = cell;
            const int step = axis == 0 ? stepX : (axis == 1 ? stepY : stepZ);
            SetCellComponent(cell, axis, CellComponent(cell, axis) + step);

            const vec3 normal = NormalForStep(axis, step);
            if (isSolid(cell.x, cell.y, cell.z))
                return {true, cell, adjacent, normal, nextT};

            if (axis == 0)
                tMaxX += tDeltaX;
            else if (axis == 1)
                tMaxY += tDeltaY;
            else
                tMaxZ += tDeltaZ;
        }
    }

    vec3 MoveAabb(
        const vec3 &pos,
        const vec3 &halfExtents,
        const vec3 &delta,
        const std::function<bool(int, int, int)> &isSolid)
    {
        vec3 resolved = pos;

        // Resolve vertical motion first, then horizontal X and Z for predictable sliding.
        ResolveAxis(resolved, halfExtents, 1, delta.y, isSolid);
        ResolveAxis(resolved, halfExtents, 0, delta.x, isSolid);
        ResolveAxis(resolved, halfExtents, 2, delta.z, isSolid);

        return resolved;
    }
} // namespace pe::voxel
