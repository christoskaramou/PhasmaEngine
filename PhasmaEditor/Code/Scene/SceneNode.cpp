#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Camera/Camera.h"
#include "API/RHI.h"
#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#endif

namespace pe
{
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
        m_nodeNames.push_back(name);
        m_localMatrices.push_back(mat4(1.f));
        m_nodeParents.push_back(parent);
        m_nodeChildren.emplace_back();
        m_componentFlags.push_back(Component_None);
        m_meshRefs.push_back(-1);
        m_nodeScriptPaths.push_back("");

        NodeRuntime runtime{};
        runtime.dirty = true;
        runtime.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_nodeRuntime.push_back(std::move(runtime));

        // Add to parent's children
        if (parent)
            m_nodeChildren[parent->index].push_back(id);

        m_nodesDirty = true;

        return id;
    }

    void Scene::DeleteNode(NodeId *node)
    {
        if (!node)
            return;

        // Recursively delete children first (collect to avoid modifying while iterating)
        // Use node->index live — it may change during child deletions due to swap-and-pop
        std::vector<NodeId *> childrenCopy = m_nodeChildren[node->index];
        for (NodeId *child : childrenCopy)
            DeleteNode(child);

        // Re-read index after child deletions (swap-and-pop may have moved this node)
        const uint32_t idx = node->index;

        // Null out material pointers on the mesh so stale entries in m_meshes
        // don't dereference freed Materials after the owning model is deleted.
        int meshRef = m_meshRefs[idx];
        if (meshRef >= 0 && meshRef < static_cast<int>(m_meshes.size()))
        {
            m_meshes[meshRef].material = nullptr;
            m_meshes[meshRef].materialInstance = nullptr;
        }

        // Remove component entries so they don't persist with dangling nodeIds
        uint32_t compFlags = m_componentFlags[idx];
        if (compFlags & Component_Light)
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
        if (compFlags & Component_Camera)
        {
            Camera *cam = GetCameraForNode(node);
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
        if (compFlags & Component_Physics)
        {
            if (auto *ps = GetGlobalSystem<PhysicsSystem>())
                ps->RemoveBody(node);
        }
#endif

        // Remove from parent's children list
        NodeId *parent = m_nodeParents[idx];
        if (parent)
        {
            auto &siblings = m_nodeChildren[parent->index];
            siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
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
            // Swap all SoA arrays
            std::swap(m_nodeIds[index], m_nodeIds[last]);
            std::swap(m_nodeNames[index], m_nodeNames[last]);
            std::swap(m_localMatrices[index], m_localMatrices[last]);
            std::swap(m_nodeParents[index], m_nodeParents[last]);
            std::swap(m_nodeChildren[index], m_nodeChildren[last]);
            std::swap(m_componentFlags[index], m_componentFlags[last]);
            std::swap(m_meshRefs[index], m_meshRefs[last]);
            std::swap(m_nodeScriptPaths[index], m_nodeScriptPaths[last]);
            std::swap(m_nodeRuntime[index], m_nodeRuntime[last]);

            // Update the swapped node's identity — the one place
            m_nodeIds[index]->index = index;
        }

        // Pop the last element from all arrays
        m_nodeIds.pop_back();
        m_nodeNames.pop_back();
        m_localMatrices.pop_back();
        m_nodeParents.pop_back();
        m_nodeChildren.pop_back();
        m_componentFlags.pop_back();
        m_meshRefs.pop_back();
        m_nodeScriptPaths.pop_back();
        m_nodeRuntime.pop_back();
    }

    void Scene::ReparentNode(NodeId *node, NodeId *newParent)
    {
        if (!node || node == newParent)
            return;

        // Prevent reparenting to own descendant
        for (NodeId *p = newParent; p; p = m_nodeParents[p->index])
        {
            if (p == node)
                return;
        }

        const uint32_t idx = node->index;

        // Remove from old parent's children
        NodeId *oldParent = m_nodeParents[idx];
        if (oldParent)
        {
            auto &siblings = m_nodeChildren[oldParent->index];
            siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
        }

        // Set new parent
        m_nodeParents[idx] = newParent;

        // Add to new parent's children
        if (newParent)
            m_nodeChildren[newParent->index].push_back(node);

        MarkNodeDirty(node);
    }

    void Scene::SetLocalMatrix(NodeId *node, const mat4 &m, bool markDirty)
    {
        m_localMatrices[node->index] = m;
        if (markDirty)
            MarkNodeDirty(node);
    }

    void Scene::SetMeshRef(NodeId *node, int meshIndex)
    {
        const uint32_t idx = node->index;
        m_meshRefs[idx] = meshIndex;

        if (meshIndex >= 0)
            m_componentFlags[idx] |= Component_Mesh;
        else
            m_componentFlags[idx] &= ~Component_Mesh;
    }

    void Scene::SetNodeScript(NodeId *node, const std::string &path)
    {
        const uint32_t idx = node->index;
        m_nodeScriptPaths[idx] = path;

        if (!path.empty())
            m_componentFlags[idx] |= Component_Script;
        else
            m_componentFlags[idx] &= ~Component_Script;
    }

    void Scene::AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel)
    {
        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = primitiveModel->GetFilePath();
        source.primitiveType = primitiveModel->GetPrimitiveType();
        m_sources.push_back(std::move(source));

        std::vector<int> meshMap = AddModelGeometry(primitiveModel, sourceIndex);
        if (!meshMap.empty() && meshMap[0] >= 0)
            SetMeshRef(node, meshMap[0]);

        // Transfer material ownership from the temporary ModelAsset to the scene
        // before deleting it — mesh.material holds a raw pointer into these.
        for (auto &mat : primitiveModel->m_materials)
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
        for (NodeId *child : m_nodeChildren[idx])
            MarkNodeDirty(child);
    }

    void Scene::UpdateNodeMatrix(NodeId *node)
    {
        const uint32_t idx = node->index;
        NodeRuntime &rt = m_nodeRuntime[idx];

        if (!rt.dirty)
            return;

        NodeId *parent = m_nodeParents[idx];
        const mat4 prevWorld = rt.gpuData.worldMatrix;
        if (parent)
            rt.gpuData.worldMatrix = m_nodeRuntime[parent->index].gpuData.worldMatrix * m_localMatrices[idx];
        else
            rt.gpuData.worldMatrix = m_localMatrices[idx];

        // On first compute, seed previousWorldMatrix so shaders see zero motion on spawn
        if (prevWorld == mat4(1.f) && rt.gpuData.previousWorldMatrix == mat4(1.f))
            rt.gpuData.previousWorldMatrix = rt.gpuData.worldMatrix;

        // Update world AABB from mesh bounding box
        const int meshIdx = m_meshRefs[idx];
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
        for (NodeId *child : m_nodeChildren[idx])
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
            NodeId *parent = m_nodeParents[i];
            if (parent && m_nodeRuntime[parent->index].dirty)
                continue; // parent will recurse here
            UpdateNodeMatrix(m_nodeIds[i]);
        }

        m_nodesDirty = false;
    }
} // namespace pe
