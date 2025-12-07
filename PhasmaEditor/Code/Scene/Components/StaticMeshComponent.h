#pragma once

#include "Scene/Components/SceneComponent.h"
#include "Render/Proxies/RenderProxyManager.h"

namespace pe::scene
{
    class StaticMeshComponent : public SceneComponent
    {
    public:
        StaticMeshComponent();
        StaticMeshComponent(MeshHandle mesh, MaterialHandle material);
        ~StaticMeshComponent() override;

        void SetMesh(MeshHandle mesh);
        void SetMaterial(MaterialHandle material);
        [[nodiscard]] MeshHandle GetMesh() const noexcept { return m_mesh; }
        [[nodiscard]] MaterialHandle GetMaterial() const noexcept { return m_material; }

    protected:
        void OnWorldTransformUpdated() override;

    private:
        void EnsureProxy();
        void DestroyProxy();
        static AABB TransformAabb(const AABB &local, const glm::mat4 &world);

        MeshHandle m_mesh{};
        MaterialHandle m_material{};
        MeshRenderProxy *m_proxy = nullptr;
    };
}
