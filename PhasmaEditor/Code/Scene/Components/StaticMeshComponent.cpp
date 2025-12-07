#include "Scene/Components/StaticMeshComponent.h"

#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pe::scene
{
    StaticMeshComponent::StaticMeshComponent()
    {
        EnsureProxy();
    }

    StaticMeshComponent::StaticMeshComponent(MeshHandle mesh, MaterialHandle material)
        : m_mesh(mesh), m_material(material)
    {
        EnsureProxy();
    }

    StaticMeshComponent::~StaticMeshComponent()
    {
        DestroyProxy();
    }

    void StaticMeshComponent::SetMesh(MeshHandle mesh)
    {
        m_mesh = mesh;
        if (m_proxy)
            m_proxy->SetMesh(mesh);
        EnsureProxy();
        OnWorldTransformUpdated();
    }

    void StaticMeshComponent::SetMaterial(MaterialHandle material)
    {
        m_material = material;
        if (m_proxy)
            m_proxy->SetMaterial(material);
    }

    void StaticMeshComponent::OnWorldTransformUpdated()
    {
        EnsureProxy();
        if (!m_proxy)
            return;

        const glm::mat4 world = GetWorldTransform().ToMatrix();
        m_proxy->SetWorldMatrix(world);

        const AABB local = RenderProxyManager::Get().QueryMeshBounds(m_mesh);
        const AABB worldBounds = TransformAabb(local, world);
        m_proxy->SetBounds(worldBounds);
    }

    void StaticMeshComponent::EnsureProxy()
    {
        if (m_proxy)
            return;

        m_proxy = RenderProxyManager::Get().CreateMeshProxy(m_mesh, m_material);
    }

    void StaticMeshComponent::DestroyProxy()
    {
        if (!m_proxy)
            return;

        RenderProxyManager::Get().DestroyProxy(m_proxy);
        m_proxy = nullptr;
    }

    AABB StaticMeshComponent::TransformAabb(const AABB &local, const glm::mat4 &world)
    {
        glm::vec3 corners[8] = {
            {local.min.x, local.min.y, local.min.z},
            {local.max.x, local.min.y, local.min.z},
            {local.min.x, local.max.y, local.min.z},
            {local.max.x, local.max.y, local.min.z},
            {local.min.x, local.min.y, local.max.z},
            {local.max.x, local.min.y, local.max.z},
            {local.min.x, local.max.y, local.max.z},
            {local.max.x, local.max.y, local.max.z},
        };

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

        for (const auto &corner : corners)
        {
            glm::vec3 transformed = glm::vec3(world * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, transformed);
            maxBounds = glm::max(maxBounds, transformed);
        }

        AABB result;
        result.min = minBounds;
        result.max = maxBounds;
        return result;
    }
}
