#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pe::scene
{
    struct Transform
    {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};

        Transform() = default;
        Transform(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s) : translation(t), rotation(r), scale(s) {}

        [[nodiscard]] glm::mat4 ToMatrix() const
        {
            const glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
            const glm::mat4 r = glm::mat4_cast(rotation);
            const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
            return t * r * s;
        }

        static Transform FromMatrix(const glm::mat4 &m)
        {
            glm::vec3 skew{};
            glm::vec4 perspective{};
            Transform result{};
            glm::decompose(m, result.scale, result.rotation, result.translation, skew, perspective);
            result.rotation = glm::conjugate(result.rotation); // glm::decompose returns inverse rotation
            return result;
        }
    };

    inline Transform Combine(const Transform &parent, const Transform &local)
    {
        Transform combined{};
        combined.rotation = parent.rotation * local.rotation;
        combined.scale = parent.scale * local.scale;
        combined.translation = parent.translation + parent.rotation * (parent.scale * local.translation);
        return combined;
    }
}
