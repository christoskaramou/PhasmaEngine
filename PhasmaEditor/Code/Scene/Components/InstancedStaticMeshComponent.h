#pragma once

#include <vector>

#include "Scene/Components/SceneComponent.h"
#include "Render/Proxies/RenderProxyManager.h"

namespace pe::scene
{
    class InstancedStaticMeshComponent : public SceneComponent
    {
    public:
        InstancedStaticMeshComponent();
        InstancedStaticMeshComponent(MeshHandle mesh, MaterialHandle material);
        ~InstancedStaticMeshComponent() override;

        int32_t AddInstance(const Transform &relativeTransform);
        void UpdateInstanceTransform(int32_t index, const Transform &relativeTransform);
        bool RemoveInstance(int32_t index);
        [[nodiscard]] size_t GetInstanceCount() const noexcept { return m_instanceRelativeTransforms.size(); }

        void SetMesh(MeshHandle mesh);
        void SetMaterial(MaterialHandle material);
        [[nodiscard]] MeshHandle GetMesh() const noexcept { return m_mesh; }
        [[nodiscard]] MaterialHandle GetMaterial() const noexcept { return m_material; }

        void UpdateInstanceWorldTransforms();

    protected:
        void OnWorldTransformUpdated() override;

    private:
        void EnsureProxy();
        void DestroyProxy();
        void MarkAllDirty();
        static AABB TransformAabb(const AABB &local, const glm::mat4 &world);

        MeshHandle m_mesh{};
        MaterialHandle m_material{};
        InstancedMeshRenderProxy *m_proxy = nullptr;

        std::vector<Transform> m_instanceRelativeTransforms;
        std::vector<Transform> m_instanceWorldTransforms;
        std::vector<bool> m_instancesDirty;
    };
}
