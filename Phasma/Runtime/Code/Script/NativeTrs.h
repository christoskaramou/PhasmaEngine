#pragma once
#include <cmath>
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"

namespace pe
{
    struct LocalTrs
    {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    // Signed-scale decomposition of an affine matrix. A reflection (negative determinant) is carried
    // by a negative x scale whichever axis was originally mirrored (a matrix cannot tell); zero-scale
    // axes keep a valid rotation instead of producing NaN.
    inline LocalTrs DecomposeTrs(const glm::mat4 &m)
    {
        constexpr float epsilon = 1e-12f;
        LocalTrs trs;
        trs.translation = glm::vec3(m[3]);
        glm::vec3 axes[3] = {glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2])};
        bool zero[3];
        int zeroCount = 0;
        for (int i = 0; i < 3; ++i)
        {
            trs.scale[i] = glm::length(axes[i]);
            zero[i] = !(trs.scale[i] > epsilon) || !std::isfinite(trs.scale[i]);
            if (zero[i])
            {
                trs.scale[i] = 0.0f;
                axes[i] = glm::vec3(0.0f);
                ++zeroCount;
            }
            else
                axes[i] /= trs.scale[i];
        }
        if (zeroCount == 0 && glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f)
        {
            trs.scale.x = -trs.scale.x;
            axes[0] = -axes[0];
        }
        if (zeroCount == 1)
        {
            const int missing = zero[0] ? 0 : (zero[1] ? 1 : 2);
            const glm::vec3 axis = glm::cross(axes[(missing + 1) % 3], axes[(missing + 2) % 3]);
            const float length = glm::length(axis);
            if (length > epsilon)
                axes[missing] = axis / length;
            else // parallel survivors: rebuild from one
            {
                zero[(missing + 2) % 3] = true;
                zeroCount = 2;
            }
        }
        if (zeroCount == 3)
            return trs;
        if (zeroCount == 2)
        {
            // Complete the surviving axis to a right-handed basis.
            const int keep = zero[0] ? (zero[1] ? 2 : 1) : 0;
            const glm::vec3 seed = std::abs(axes[keep].x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            const int a = (keep + 1) % 3, b = (keep + 2) % 3;
            axes[a] = glm::normalize(glm::cross(axes[keep], seed));
            axes[b] = glm::cross(axes[keep], axes[a]);
        }
        trs.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
        return trs;
    }

    inline glm::mat4 ComposeTrs(const LocalTrs &trs)
    {
        return glm::translate(glm::mat4(1.0f), trs.translation) * glm::mat4_cast(trs.rotation) * glm::scale(glm::mat4(1.0f), trs.scale);
    }

    // Replaces rotation (Euler degrees) and/or scale, keeping the other components.
    inline glm::mat4 ReplaceTrs(const glm::mat4 &m, const glm::vec3 *rotationDegrees, const glm::vec3 *scale)
    {
        LocalTrs trs = DecomposeTrs(m);
        if (rotationDegrees)
            trs.rotation = glm::quat(glm::radians(*rotationDegrees));
        if (scale)
            trs.scale = *scale;
        return ComposeTrs(trs);
    }
} // namespace pe
