#include "Scene/Components/InstancedStaticMeshComponent.h"

#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pe::scene
{
    InstancedStaticMeshComponent::InstancedStaticMeshComponent()
    {
        EnsureProxy();
    }

    InstancedStaticMeshComponent::InstancedStaticMeshComponent(MeshHandle mesh, MaterialHandle material)
        : m_mesh(mesh), m_material(material)
    {
        EnsureProxy();
    }

    InstancedStaticMeshComponent::~InstancedStaticMeshComponent()
    {
        DestroyProxy();
    }

    int32_t InstancedStaticMeshComponent::AddInstance(const Transform &relativeTransform)
    {
        const int32_t index = static_cast<int32_t>(m_instanceRelativeTransforms.size());
        m_instanceRelativeTransforms.push_back(relativeTransform);
        m_instanceWorldTransforms.emplace_back();
        m_instancesDirty.push_back(true);

        EnsureProxy();
        if (m_proxy)
            m_proxy->SetInstanceCount(m_instanceRelativeTransforms.size());

        UpdateInstanceWorldTransforms();

        return index;
    }

    void InstancedStaticMeshComponent::UpdateInstanceTransform(int32_t index, const Transform &relativeTransform)
    {
        if (index < 0 || static_cast<size_t>(index) >= m_instanceRelativeTransforms.size())
            return;

        m_instanceRelativeTransforms[static_cast<size_t>(index)] = relativeTransform;
        m_instancesDirty[static_cast<size_t>(index)] = true;
    }

    bool InstancedStaticMeshComponent::RemoveInstance(int32_t index)
    {
        if (index < 0 || static_cast<size_t>(index) >= m_instanceRelativeTransforms.size())
            return false;

        size_t idx = static_cast<size_t>(index);
        size_t last = m_instanceRelativeTransforms.size() - 1;

        m_instanceRelativeTransforms[idx] = m_instanceRelativeTransforms[last];
        m_instanceWorldTransforms[idx] = m_instanceWorldTransforms[last];
        m_instancesDirty[idx] = true; // mark swapped entry dirty

        m_instanceRelativeTransforms.pop_back();
        m_instanceWorldTransforms.pop_back();
        m_instancesDirty.pop_back();

        if (m_proxy)
            m_proxy->SetInstanceCount(m_instanceRelativeTransforms.size());

        UpdateInstanceWorldTransforms();

        return true;
    }

    void InstancedStaticMeshComponent::SetMesh(MeshHandle mesh)
    {
        m_mesh = mesh;
        if (m_proxy)
            m_proxy->SetMesh(mesh);
        EnsureProxy();
        MarkAllDirty();
        UpdateInstanceWorldTransforms();
    }

    void InstancedStaticMeshComponent::SetMaterial(MaterialHandle material)
    {
        m_material = material;
        if (m_proxy)
            m_proxy->SetMaterial(material);
    }

    void InstancedStaticMeshComponent::OnWorldTransformUpdated()
    {
        MarkAllDirty();
        UpdateInstanceWorldTransforms();
    }

    void InstancedStaticMeshComponent::UpdateInstanceWorldTransforms()
    {
        EnsureProxy();
        if (!m_proxy)
            return;

        const AABB local = RenderProxyManager::Get().QueryMeshBounds(m_mesh);

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

        for (size_t i = 0; i < m_instanceRelativeTransforms.size(); ++i)
        {
            if (!m_instancesDirty[i])
                continue;

            const Transform worldT = Combine(GetWorldTransform(), m_instanceRelativeTransforms[i]);
            m_instanceWorldTransforms[i] = worldT;
            const glm::mat4 worldMat = worldT.ToMatrix();
            m_proxy->SetInstanceTransform(i, worldMat);

            const AABB worldAabb = TransformAabb(local, worldMat);
            minBounds = glm::min(minBounds, worldAabb.min);
            maxBounds = glm::max(maxBounds, worldAabb.max);

            m_instancesDirty[i] = false;
        }

        if (!m_instanceRelativeTransforms.empty())
        {
            AABB combined;
            combined.min = minBounds;
            combined.max = maxBounds;
            m_proxy->SetBounds(combined);
        }
    }

    void InstancedStaticMeshComponent::EnsureProxy()
    {
        if (m_proxy)
            return;

        m_proxy = RenderProxyManager::Get().CreateInstancedMeshProxy(m_mesh, m_material);
        if (m_proxy)
            m_proxy->SetInstanceCount(m_instanceRelativeTransforms.size());
    }

    void InstancedStaticMeshComponent::DestroyProxy()
    {
        if (!m_proxy)
            return;

        RenderProxyManager::Get().DestroyProxy(m_proxy);
        m_proxy = nullptr;
    }

    void InstancedStaticMeshComponent::MarkAllDirty()
    {
        for (auto &&flag : m_instancesDirty)
            flag = true;
    }

    AABB InstancedStaticMeshComponent::TransformAabb(const AABB &local, const glm::mat4 &world)
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

        glm::vec3 minB(std::numeric_limits<float>::max());
        glm::vec3 maxB(std::numeric_limits<float>::lowest());

        for (const auto &corner : corners)
        {
            glm::vec3 transformed = glm::vec3(world * glm::vec4(corner, 1.0f));
            minB = glm::min(minB, transformed);
            maxB = glm::max(maxB, transformed);
        }

        AABB result;
        result.min = minB;
        result.max = maxB;
        return result;
    }
}
