#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Camera/Camera.h"
#include "API/RHI.h"
#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#endif
#ifdef PE_AUDIO
#include "Systems/AudioSystem.h"
#endif
#include "Systems/AnimationSystem.h"

namespace pe
{
    // --- Component flag management (ECS-backed) ---

    void Scene::AddComponentFlag(NodeId *node, uint32_t flag)
    {
        ValidateNodeId(node);
        auto &c = m_nodeComponentCache[node->index];
        Entity *entity = node->entity;
        if (!entity)
            return;

        if ((flag & Component_Camera) && !c.camera)
            c.camera = entity->CreateComponent<NodeCameraTag>();
        if ((flag & Component_Light) && !c.light)
            c.light = entity->CreateComponent<NodeLightTag>();
        if ((flag & Component_Physics) && !c.physics)
            c.physics = entity->CreateComponent<NodePhysicsTag>();
        if ((flag & Component_Audio) && !c.audio)
            c.audio = entity->CreateComponent<NodeAudioTag>();
    }

    void Scene::RemoveComponentFlag(NodeId *node, uint32_t flag)
    {
        ValidateNodeId(node);
        auto &c = m_nodeComponentCache[node->index];
        Entity *entity = node->entity;
        if (!entity)
            return;

        if ((flag & Component_Camera) && c.camera)
        {
            entity->RemoveComponent<NodeCameraTag>();
            c.camera = nullptr;
        }
        if ((flag & Component_Light) && c.light)
        {
            entity->RemoveComponent<NodeLightTag>();
            c.light = nullptr;
        }
        if ((flag & Component_Physics) && c.physics)
        {
            entity->RemoveComponent<NodePhysicsTag>();
            c.physics = nullptr;
        }
        if ((flag & Component_Audio) && c.audio)
        {
            entity->RemoveComponent<NodeAudioTag>();
            c.audio = nullptr;
        }
    }

    // --- Node lifecycle ---

    NodeId *Scene::CreateNode(const std::string &name, NodeId *parent)
    {
        // Reuse a recycled NodeId or allocate a new one
        NodeId *id;
        if (!m_freeNodeIds.empty())
        {
            id = m_freeNodeIds.back();
            m_freeNodeIds.pop_back();
        }
        else
        {
            id = new NodeId();
        }

        const uint32_t index = static_cast<uint32_t>(m_nodeIds.size());
        id->index = index;
        id->revision++;

        m_nodeIds.push_back(id);

        NodeRuntime runtime{};
        runtime.dirty = true;
        runtime.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_nodeRuntime.push_back(std::move(runtime));

        // Create ECS entity and attach node components
        Entity *entity = Context::Get()->CreateEntity();
        id->entity = entity;

        auto *nameComp = entity->CreateComponent<NodeNameComponent>();
        nameComp->name = name;

        auto *hierarchyComp = entity->CreateComponent<NodeHierarchyComponent>();
        hierarchyComp->parent = parent;
        if (parent)
        {
            m_nodeComponentCache[parent->index].hierarchy->children.push_back(id);
        }

        auto *transformComp = entity->CreateComponent<NodeTransformComponent>();
        auto *meshRefsComp = entity->CreateComponent<NodeMeshRefsComponent>();
        auto *scriptComp = entity->CreateComponent<NodeScriptComponent>();

        m_nodeComponentCache.push_back({nameComp, hierarchyComp, transformComp, meshRefsComp, scriptComp,
                                        nullptr, nullptr, nullptr, nullptr});

        m_nodesDirty = true;

        return id;
    }

    void Scene::DeleteNode(NodeId *node)
    {
        if (!node)
            return;

        // Recursively delete children first (collect to avoid modifying while iterating)
        // Use node->index live — it may change during child deletions due to swap-and-pop
        std::vector<NodeId *> childrenCopy = m_nodeComponentCache[node->index].hierarchy->children;
        for (NodeId *child : childrenCopy)
            DeleteNode(child);

        // Re-read index after child deletions (swap-and-pop may have moved this node)
        const uint32_t idx = node->index;
        const auto &cache = m_nodeComponentCache[idx];

        // Null out material pointers on the mesh so stale entries in m_meshes
        // don't dereference freed Materials after the owning model is deleted.
        // Only null if no other node still references this mesh.
        int meshRef = MeshRefAt(idx);
        if (meshRef >= 0 && meshRef < static_cast<int>(m_meshes.size()))
        {
            bool otherRef = false;
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); i++)
            {
                if (i != idx && MeshRefAt(i) == meshRef)
                {
                    otherRef = true;
                    break;
                }
            }
            if (!otherRef)
            {
                m_meshes[meshRef].material = nullptr;
                m_meshes[meshRef].materialInstance = nullptr;
            }
        }

        // Remove component entries so they don't persist with dangling nodeIds
        if (cache.light)
        {
            auto [lt, lightIdx] = GetLightForNode(node);
            if (lightIdx >= 0)
            {
                switch (lt)
                {
                case LightType::Directional:
                    m_directionalLights.erase(m_directionalLights.begin() + lightIdx);
                    break;
                case LightType::Point:
                    m_pointLights.erase(m_pointLights.begin() + lightIdx);
                    break;
                case LightType::Spot:
                    m_spotLights.erase(m_spotLights.begin() + lightIdx);
                    break;
                case LightType::Area:
                    m_areaLights.erase(m_areaLights.begin() + lightIdx);
                    break;
                }
            }
        }
        if (cache.camera)
        {
            Camera *cam = cache.camera->camera;
            if (cam)
            {
                auto it = std::find(m_cameras.begin(), m_cameras.end(), cam);
                if (it != m_cameras.end())
                {
                    delete *it;
                    m_cameras.erase(it);
                }
            }
        }
#ifdef PE_PHYSICS
        if (cache.physics)
        {
            if (auto *ps = GetGlobalSystem<PhysicsSystem>())
                ps->RemoveBody(node);
        }
#endif
#ifdef PE_AUDIO
        if (cache.audio)
        {
            if (auto *as = GetGlobalSystem<AudioSystem>())
                as->RemoveSource(node);
        }
#endif
        if (auto *animSys = GetGlobalSystem<AnimationSystem>())
            animSys->RemoveAnimation(node);

        // Remove from parent's children list (ECS hierarchy)
        NodeId *parent = cache.hierarchy->parent;
        if (parent)
        {
            auto &siblings = m_nodeComponentCache[parent->index].hierarchy->children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
        }

        // Destroy ECS entity
        if (node->entity)
        {
            Context::Get()->RemoveEntity(node->entity->GetID());
            node->entity = nullptr;
        }

        // Swap-and-pop
        SwapAndPopNode(idx);

        // Recycle the NodeId (set sentinel to catch use-after-free)
        node->index = UINT32_MAX;
        m_freeNodeIds.push_back(node);

        m_nodesDirty = true;
    }

    void Scene::SwapAndPopNode(uint32_t index)
    {
        const uint32_t last = static_cast<uint32_t>(m_nodeIds.size()) - 1;

        if (index != last)
        {
            std::swap(m_nodeIds[index], m_nodeIds[last]);
            std::swap(m_nodeComponentCache[index], m_nodeComponentCache[last]);
            std::swap(m_nodeRuntime[index], m_nodeRuntime[last]);

            // Update the swapped node's identity — the one place
            m_nodeIds[index]->index = index;
        }

        m_nodeIds.pop_back();
        m_nodeComponentCache.pop_back();
        m_nodeRuntime.pop_back();
    }

    // --- Node operations ---

    void Scene::ReparentNode(NodeId *node, NodeId *newParent)
    {
        if (!node || node == newParent)
            return;

        // Prevent reparenting to own descendant
        for (NodeId *p = newParent; p; p = m_nodeComponentCache[p->index].hierarchy->parent)
        {
            if (p == node)
                return;
        }

        const uint32_t idx = node->index;

        // Remove from old parent's children
        NodeId *oldParent = m_nodeComponentCache[idx].hierarchy->parent;
        if (oldParent)
        {
            auto &siblings = m_nodeComponentCache[oldParent->index].hierarchy->children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
        }

        // Set new parent
        m_nodeComponentCache[idx].hierarchy->parent = newParent;

        // Add to new parent's children
        if (newParent)
            m_nodeComponentCache[newParent->index].hierarchy->children.push_back(node);

        MarkNodeDirty(node);
    }

    void Scene::SetLocalMatrix(NodeId *node, const mat4 &m, bool markDirty)
    {
        m_nodeComponentCache[node->index].transform->localMatrix = m;
        if (markDirty)
            MarkNodeDirty(node);
    }

    void Scene::SetMeshRef(NodeId *node, int meshIndex)
    {
        auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        refs.clear();
        if (meshIndex >= 0)
            refs.push_back(meshIndex);
    }

    void Scene::SetNodeScript(NodeId *node, const std::string &path)
    {
        m_nodeComponentCache[node->index].script->path = path;
    }

    void Scene::AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel)
    {
        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = primitiveModel->GetFilePath();
        source.primitiveType = primitiveModel->GetPrimitiveType();
        source.primitiveParams = primitiveModel->GetPrimitiveParams();
        source.primitiveParamCount = primitiveModel->GetPrimitiveParamCount();
        m_sources.push_back(std::move(source));

        std::vector<int> meshMap = AddModelGeometry(primitiveModel, sourceIndex);
        if (!meshMap.empty() && meshMap[0] >= 0)
            SetMeshRef(node, meshMap[0]);

        // Transfer material ownership from the temporary ModelAsset to the scene
        // before deleting it — mesh.material holds a raw pointer into these.
        for (auto &mat : primitiveModel->GetOwnedMaterials())
            m_ownedMaterials.push_back(std::move(mat));

        MarkNodeDirty(node);
        delete primitiveModel;
    }

    int Scene::AddMesh(Mesh &&mesh)
    {
        const int index = static_cast<int>(m_meshes.size());
        m_meshes.push_back(std::move(mesh));
        m_meshRuntimes.emplace_back();
        return index;
    }

    // --- Dirty tracking and matrix updates ---

    void Scene::MarkNodeDirty(NodeId *node)
    {
        if (!node)
            return;

        const uint32_t idx = node->index;
        NodeRuntime &rt = m_nodeRuntime[idx];

        // Always mark uniforms dirty so material changes are caught even
        // if the node was already dirty (e.g., material edit on a moved node).
        for (size_t i = 0; i < rt.dirtyUniforms.size(); i++)
            rt.dirtyUniforms[i] = true;

        if (rt.dirty)
            return;

        rt.dirty = true;
        m_nodesDirty = true;

        // Mark all children dirty recursively
        for (NodeId *child : m_nodeComponentCache[idx].hierarchy->children)
            MarkNodeDirty(child);
    }

    void Scene::UpdateNodeMatrix(NodeId *node)
    {
        const uint32_t idx = node->index;
        NodeRuntime &rt = m_nodeRuntime[idx];

        if (!rt.dirty)
            return;

        NodeId *parent = m_nodeComponentCache[idx].hierarchy->parent;
        const mat4 &localMatrix = m_nodeComponentCache[idx].transform->localMatrix;
        const mat4 prevWorld = rt.gpuData.worldMatrix;
        if (parent)
            rt.gpuData.worldMatrix = m_nodeRuntime[parent->index].gpuData.worldMatrix * localMatrix;
        else
            rt.gpuData.worldMatrix = localMatrix;

        // On first compute, seed previousWorldMatrix so shaders see zero motion on spawn
        if (prevWorld == mat4(1.f) && rt.gpuData.previousWorldMatrix == mat4(1.f))
            rt.gpuData.previousWorldMatrix = rt.gpuData.worldMatrix;

        // Update world AABB from mesh bounding box
        const int meshIdx = MeshRefAt(idx);
        if (meshIdx >= 0)
        {
            const AABB &localAABB = m_meshes[meshIdx].boundingBox;
            rt.worldAABB = TransformAabb(localAABB, rt.gpuData.worldMatrix);
        }

        rt.dirty = false;

        // Mark uniforms dirty for all frames
        for (size_t i = 0; i < rt.dirtyUniforms.size(); i++)
            rt.dirtyUniforms[i] = true;

        m_nodesMoved.push_back(node);

        // Recurse into children
        for (NodeId *child : m_nodeComponentCache[idx].hierarchy->children)
            UpdateNodeMatrix(child);
    }

    void Scene::UpdateNodeMatrices()
    {
        if (!m_nodesDirty)
            return;

        // Update from the shallowest dirty ancestor in each dirty subtree.
        // A node is an entry point if it is dirty and its parent is either absent (root)
        // or clean (parent world matrix is already current).
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); i++)
        {
            if (!m_nodeRuntime[i].dirty)
                continue;
            NodeId *parent = m_nodeComponentCache[i].hierarchy->parent;
            if (parent && m_nodeRuntime[parent->index].dirty)
                continue; // parent will recurse here
            UpdateNodeMatrix(m_nodeIds[i]);
        }

        m_nodesDirty = false;
    }

    void Scene::DestroyAllNodeEntities()
    {
        Context *ctx = Context::Get();
        for (NodeId *id : m_nodeIds)
        {
            if (id && id->entity)
            {
                ctx->RemoveEntity(id->entity->GetID());
                id->entity = nullptr;
            }
        }
        for (NodeId *id : m_freeNodeIds)
        {
            if (id)
                id->entity = nullptr;
        }
        m_nodeComponentCache.clear();
    }
} // namespace pe
