#pragma once

#include <vector>

#include <glm/glm.hpp>
#include "Base/Math.h"

namespace pe
{
    struct MeshHandle
    {
        uint32_t id = ~0u;
    };

    struct MaterialHandle
    {
        uint32_t id = ~0u;
    };

    class RenderProxy
    {
    public:
        virtual ~RenderProxy() = default;

        void SetWorldMatrix(const glm::mat4 &world) { m_worldMatrix = world; }
        [[nodiscard]] const glm::mat4 &GetWorldMatrix() const noexcept { return m_worldMatrix; }

        void SetBounds(const AABB &bounds) { m_bounds = bounds; }
        [[nodiscard]] const AABB &GetBounds() const noexcept { return m_bounds; }

    protected:
        glm::mat4 m_worldMatrix{1.0f};
        AABB m_bounds{};
    };

    class MeshRenderProxy : public RenderProxy
    {
    public:
        MeshRenderProxy() = default;
        MeshRenderProxy(const MeshHandle &mesh, const MaterialHandle &material)
            : m_mesh(mesh), m_material(material) {}

        void SetMesh(const MeshHandle &mesh) { m_mesh = mesh; }
        void SetMaterial(const MaterialHandle &material) { m_material = material; }
        [[nodiscard]] const MeshHandle &GetMesh() const noexcept { return m_mesh; }
        [[nodiscard]] const MaterialHandle &GetMaterial() const noexcept { return m_material; }

    private:
        MeshHandle m_mesh{};
        MaterialHandle m_material{};
    };

    class InstancedMeshRenderProxy : public RenderProxy
    {
    public:
        InstancedMeshRenderProxy() = default;
        InstancedMeshRenderProxy(const MeshHandle &mesh, const MaterialHandle &material)
            : m_mesh(mesh), m_material(material) {}

        void SetMesh(const MeshHandle &mesh) { m_mesh = mesh; }
        void SetMaterial(const MaterialHandle &material) { m_material = material; }
        [[nodiscard]] const MeshHandle &GetMesh() const noexcept { return m_mesh; }
        [[nodiscard]] const MaterialHandle &GetMaterial() const noexcept { return m_material; }

        void SetInstanceCount(size_t count) { m_worldMatrices.resize(count, glm::mat4(1.0f)); }
        void SetInstanceTransform(size_t index, const glm::mat4 &world)
        {
            if (index >= m_worldMatrices.size())
                m_worldMatrices.resize(index + 1, glm::mat4(1.0f));
            m_worldMatrices[index] = world;
        }
        [[nodiscard]] const std::vector<glm::mat4> &GetInstanceTransforms() const noexcept { return m_worldMatrices; }

    private:
        MeshHandle m_mesh{};
        MaterialHandle m_material{};
        std::vector<glm::mat4> m_worldMatrices{};
    };
}
