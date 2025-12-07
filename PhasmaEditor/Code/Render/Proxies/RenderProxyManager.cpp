#include "Render/Proxies/RenderProxyManager.h"

#include <algorithm>

namespace
{
    pe::AABB DefaultBounds()
    {
        pe::AABB aabb;
        aabb.min = {0.0f, 0.0f, 0.0f};
        aabb.max = {0.0f, 0.0f, 0.0f};
        return aabb;
    }
}

namespace pe
{
    RenderProxyManager &RenderProxyManager::Get()
    {
        static RenderProxyManager instance;
        return instance;
    }

    MeshRenderProxy *RenderProxyManager::CreateMeshProxy(const MeshHandle &mesh, const MaterialHandle &material)
    {
        auto proxy = std::make_unique<MeshRenderProxy>(mesh, material);
        MeshRenderProxy *ptr = proxy.get();
        m_proxies.emplace_back(std::move(proxy));
        return ptr;
    }

    InstancedMeshRenderProxy *RenderProxyManager::CreateInstancedMeshProxy(const MeshHandle &mesh, const MaterialHandle &material)
    {
        auto proxy = std::make_unique<InstancedMeshRenderProxy>(mesh, material);
        InstancedMeshRenderProxy *ptr = proxy.get();
        m_proxies.emplace_back(std::move(proxy));
        return ptr;
    }

    void RenderProxyManager::DestroyProxy(RenderProxy *proxy)
    {
        if (!proxy)
            return;

        auto it = std::find_if(m_proxies.begin(), m_proxies.end(), [proxy](const std::unique_ptr<RenderProxy> &p) {
            return p.get() == proxy;
        });

        if (it != m_proxies.end())
            m_proxies.erase(it);
    }

    void RenderProxyManager::ForEachMeshProxy(const std::function<void(const MeshRenderProxy &)> &fn) const
    {
        for (const auto &proxy : m_proxies)
        {
            if (!proxy)
                continue;

            if (auto *meshProxy = dynamic_cast<MeshRenderProxy *>(proxy.get()))
                fn(*meshProxy);
        }
    }

    void RenderProxyManager::ForEachInstancedMeshProxy(const std::function<void(const InstancedMeshRenderProxy &)> &fn) const
    {
        for (const auto &proxy : m_proxies)
        {
            if (!proxy)
                continue;

            if (auto *instProxy = dynamic_cast<InstancedMeshRenderProxy *>(proxy.get()))
                fn(*instProxy);
        }
    }

    void RenderProxyManager::SetMeshBoundsResolver(std::function<AABB(const MeshHandle &)> resolver)
    {
        m_boundsResolver = std::move(resolver);
    }

    AABB RenderProxyManager::QueryMeshBounds(const MeshHandle &mesh) const
    {
        if (m_boundsResolver)
            return m_boundsResolver(mesh);
        return DefaultBounds();
    }
}
