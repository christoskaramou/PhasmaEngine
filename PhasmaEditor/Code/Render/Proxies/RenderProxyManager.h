#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Render/Proxies/RenderProxy.h"

namespace pe
{
    class RenderProxyManager
    {
    public:
        RenderProxyManager() = default;
        ~RenderProxyManager() = default;

        static RenderProxyManager &Get();

        MeshRenderProxy *CreateMeshProxy(const MeshHandle &mesh, const MaterialHandle &material);
        InstancedMeshRenderProxy *CreateInstancedMeshProxy(const MeshHandle &mesh, const MaterialHandle &material);
        void DestroyProxy(RenderProxy *proxy);

        void ForEachMeshProxy(const std::function<void(const MeshRenderProxy &)> &fn) const;
        void ForEachInstancedMeshProxy(const std::function<void(const InstancedMeshRenderProxy &)> &fn) const;

        void SetMeshBoundsResolver(std::function<AABB(const MeshHandle &)> resolver);
        [[nodiscard]] AABB QueryMeshBounds(const MeshHandle &mesh) const;

    private:
        std::vector<std::unique_ptr<RenderProxy>> m_proxies;
        std::function<AABB(const MeshHandle &)> m_boundsResolver;
    };
}
