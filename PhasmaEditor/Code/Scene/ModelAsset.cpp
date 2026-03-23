#include "Scene/ModelAsset.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Scene/ModelAssetAssimp.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{

    ModelAsset::ModelAsset() : m_id{ID::NextID()}
    {
        m_dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_label = "Model_" + std::to_string(m_id);
    }

    ModelAsset::~ModelAsset()
    {
        Unload();
    }

    void ModelAsset::Unload()
    {
        for (auto sampler : m_samplers)
        {
            if (!sampler)
                continue;
            if (m_sharedSamplers.find(sampler) != m_sharedSamplers.end())
                continue;
            RHII.AddToDeletionQueue([sampler]()
                                    { Sampler *s = sampler; Sampler::Destroy(s); });
        }

        m_images.clear();
    }

    void ModelAsset::AddImage(Image *image, bool owned)
    {
        if (!image)
            return;
        (void)owned;

        auto it = std::find_if(m_images.begin(), m_images.end(),
                               [image](const ResourceHandle<Image> &handle)
                               {
                                   return handle.get() == image;
                               });
        if (it == m_images.end())
            m_images.push_back(ResourceHandle<Image>::FromRaw(image));
    }

    void ModelAsset::AddSampler(Sampler *sampler, bool owned)
    {
        if (!sampler)
            return;

        if (std::find(m_samplers.begin(), m_samplers.end(), sampler) == m_samplers.end())
            m_samplers.push_back(sampler);

        if (!owned)
            m_sharedSamplers.insert(sampler);
    }

    ModelAsset *ModelAsset::Load(const std::filesystem::path &file)
    {
        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));
        if (!std::filesystem::exists(file))
        {
            PE_WARN("[ModelAsset] File not found: %s", fileStr.c_str());
            return nullptr;
        }

        // For now we route everything through Assimp (it supports many formats).
        ModelAsset *model = ModelAssetAssimp::Load(file);
        if (!model)
        {
            PE_WARN("[ModelAsset] Failed to load: %s", fileStr.c_str());
            return nullptr;
        }

        PE_INFO("Loaded model: %s", fileStr.c_str());
        return model;
    }

    void ModelAsset::DefaultResources::EnsureCreated(CommandBuffer *cmd)
    {
        if (!black)
            black = Image::LoadRGBA8(cmd, Path::Assets + "Objects/black.png");
        if (!normal)
            normal = Image::LoadRGBA8(cmd, Path::Assets + "Objects/normal.png");
        if (!white)
            white = Image::LoadRGBA8(cmd, Path::Assets + "Objects/white.png");

        if (!sampler)
        {
            vk::SamplerCreateInfo info = Sampler::CreateInfoInit();
            sampler = Sampler::Create(info, "Default Sampler");
        }
    }

    ModelAsset::DefaultResources &ModelAsset::GetDefaultResources(CommandBuffer *cmd)
    {
        DefaultResources &defaults = Defaults();
        defaults.EnsureCreated(cmd);
        return defaults;
    }

    const ModelAsset::DefaultResources &ModelAsset::GetDefaultResources()
    {
        return Defaults();
    }

    ModelAsset::DefaultResources &ModelAsset::Defaults()
    {
        static DefaultResources defaults;
        return defaults;
    }

    void ModelAsset::ResetResources(CommandBuffer *cmd)
    {
        m_images.clear();
        m_samplers.clear();
        m_sharedSamplers.clear();

        auto &defaults = GetDefaultResources(cmd);
        AddImage(defaults.black, false);
        AddImage(defaults.normal, false);
        AddImage(defaults.white, false);
        AddSampler(defaults.sampler, false);
    }

    void ModelAsset::DestroyDefaults()
    {
        DefaultResources &defaults = Defaults();

        if (defaults.black)
        {
            Image::Destroy(defaults.black);
            defaults.black = nullptr;
        }

        if (defaults.normal)
        {
            Image::Destroy(defaults.normal);
            defaults.normal = nullptr;
        }

        if (defaults.white)
        {
            Image::Destroy(defaults.white);
            defaults.white = nullptr;
        }

        if (defaults.sampler)
        {
            Sampler::Destroy(defaults.sampler);
            defaults.sampler = nullptr;
        }
    }

    void ModelAsset::SetMatrix(const mat4 &matrix, bool markDirty)
    {
        m_matrix = matrix;
        if (markDirty)
        {
            for (int i = 0; i < GetNodeCount(); i++)
            {
                if (m_nodeInfos[i].parent < 0)
                    MarkDirty(i);
            }
        }
    }

    void ModelAsset::UpdateNodeMatrices()
    {
        if (!m_dirtyNodes && !m_previousMatricesIsDirty)
            return;

        for (int i = 0; i < GetNodeCount(); i++)
            UpdateNodeMatrix(i);

        m_previousMatricesIsDirty = m_dirtyNodes;
        m_dirtyNodes = false;
    }

    void ModelAsset::UpdateNodeMatrix(int node)
    {
        if (node < 0 || node >= static_cast<int>(m_nodeInfos.size()))
            return;

        const NodeInfo &nodeInfo = m_nodeInfos[node];
        NodeRuntimeInfo &runtime = m_nodeRuntimeInfos[node];

        // keep previous (for motion vectors, etc.)
        bool previousMatrixChanged = false;
        if (runtime.gpuData.previousWorldMatrix != runtime.gpuData.worldMatrix)
        {
            runtime.gpuData.previousWorldMatrix = runtime.gpuData.worldMatrix;
            previousMatrixChanged = true;
        }

        if (!runtime.dirty && !previousMatrixChanged)
            return;

        int parent = nodeInfo.parent;
        if (parent >= 0)
        {
            runtime.gpuData.worldMatrix = m_nodeRuntimeInfos[parent].gpuData.worldMatrix * nodeInfo.localMatrix;
        }
        else
        {
            runtime.gpuData.worldMatrix = m_matrix * nodeInfo.localMatrix;
        }

        // update mesh world AABB if node owns a mesh
        int mesh = GetNodeMesh(node);
        if (mesh >= 0 && mesh < static_cast<int>(m_meshInfos.size()))
        {
            const MeshInfo &meshInfo = m_meshInfos[mesh];
            runtime.worldBoundingBox = TransformAabb(meshInfo.boundingBox, runtime.gpuData.worldMatrix);
        }

        runtime.dirty = false;
        m_nodesMoved.push_back(node);

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            runtime.dirtyUniforms[i] = true;
            m_dirtyUniforms[i] = true;
        }
    }

    void ModelAsset::MarkDirty(int node)
    {
        if (node < 0 || node >= static_cast<int>(m_nodeInfos.size()))
            return;

        std::vector<int> toProcess;
        toProcess.reserve(64);
        toProcess.push_back(node);

        size_t head = 0;
        while (head < toProcess.size())
        {
            int currentCtx = toProcess[head++];
            NodeRuntimeInfo &ri = m_nodeRuntimeInfos[currentCtx];

            ri.dirty = true;
            for (size_t k = 0; k < ri.dirtyUniforms.size(); k++)
                ri.dirtyUniforms[k] = true;

            for (int child : m_nodeInfos[currentCtx].children)
                toProcess.push_back(child);
        }

        m_dirtyNodes = true;
        for (size_t i = 0; i < m_dirtyUniforms.size(); i++)
            m_dirtyUniforms[i] = true;
    }

    int ModelAsset::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return -1;
        return m_nodeToMesh[nodeIndex];
    }

    void ModelAsset::RemoveMesh(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return;

        int meshIdx = m_nodeToMesh[nodeIndex];
        if (meshIdx < 0)
            return;

        // Detach mesh from this node
        m_nodeToMesh[nodeIndex] = -1;

        // Check if any other node still references this mesh
        bool meshStillUsed = false;
        for (int nm : m_nodeToMesh)
        {
            if (nm == meshIdx)
            {
                meshStillUsed = true;
                break;
            }
        }

        if (!meshStillUsed)
        {
            // Build mesh remap (skip the orphaned mesh)
            std::vector<int> meshRemap(m_meshInfos.size(), -1);
            int newMeshCount = 0;
            for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
            {
                if (i != meshIdx)
                    meshRemap[i] = newMeshCount++;
            }

            // Rebuild flat vertex/index arrays without the orphaned mesh
            std::vector<Vertex> newVertices;
            std::vector<PositionUvVertex> newPosUvs;
            std::vector<uint32_t> newIndices;
            std::vector<AabbVertex> newAabbVerts;

            uint32_t vOff = 0, iOff = 0;
            size_t aOff = 0;
            for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
            {
                const MeshInfo &mi = m_meshInfos[i];
                if (i != meshIdx)
                {
                    newVertices.insert(newVertices.end(),
                                       m_vertices.begin() + vOff, m_vertices.begin() + vOff + mi.verticesCount);
                    newPosUvs.insert(newPosUvs.end(),
                                     m_positionUvs.begin() + vOff, m_positionUvs.begin() + vOff + mi.verticesCount);
                    newIndices.insert(newIndices.end(),
                                      m_indices.begin() + iOff, m_indices.begin() + iOff + mi.indicesCount);
                    if (aOff + 8 <= m_aabbVertices.size())
                        newAabbVerts.insert(newAabbVerts.end(),
                                            m_aabbVertices.begin() + aOff, m_aabbVertices.begin() + aOff + 8);
                }
                vOff += mi.verticesCount;
                iOff += mi.indicesCount;
                aOff += 8;
            }

            m_vertices = std::move(newVertices);
            m_positionUvs = std::move(newPosUvs);
            m_indices = std::move(newIndices);
            m_aabbVertices = std::move(newAabbVerts);

            // Rebuild meshInfos and meshRuntimeInfos without the orphaned mesh
            std::vector<MeshInfo> newMeshInfos;
            std::vector<MeshRuntimeInfo> newMeshRuntimeInfos;
            newMeshInfos.reserve(m_meshInfos.size() - 1);
            newMeshRuntimeInfos.reserve(m_meshRuntimeInfos.size() > 0 ? m_meshRuntimeInfos.size() - 1 : 0);
            for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
            {
                if (i != meshIdx)
                {
                    newMeshInfos.push_back(std::move(m_meshInfos[i]));
                    if (i < static_cast<int>(m_meshRuntimeInfos.size()))
                        newMeshRuntimeInfos.push_back(std::move(m_meshRuntimeInfos[i]));
                }
            }
            m_meshInfos = std::move(newMeshInfos);
            m_meshRuntimeInfos = std::move(newMeshRuntimeInfos);

            // Remap all nodeToMesh references
            for (int &nm : m_nodeToMesh)
            {
                if (nm >= 0 && nm < static_cast<int>(meshRemap.size()))
                    nm = meshRemap[nm];
            }

            // Update counts
            m_meshCount = static_cast<uint32_t>(m_meshInfos.size());
            m_verticesCount = static_cast<uint32_t>(m_vertices.size());
            m_indicesCount = static_cast<uint32_t>(m_indices.size());
        }

        // Mark dirty
        m_dirtyNodes = true;
        for (size_t i = 0; i < m_dirtyUniforms.size(); i++)
            m_dirtyUniforms[i] = true;
    }

    ResourceHandle<Image> ModelAsset::LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath)
    {
        if (texturePath.empty())
            return ResourceHandle<Image>();

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(texturePath, ec);
        if (ec)
            normalized = texturePath;

        std::string normalizedStr(reinterpret_cast<const char *>(normalized.u8string().c_str()));

        ResourceHandle<Image> handle = ResourceManager::Get().Find<Image>(normalizedStr);
        if (!handle)
        {
            Image *rawImg = Image::LoadRGBA8(cmd, normalizedStr);
            if (!rawImg)
                return ResourceHandle<Image>();

            std::shared_ptr<Image> sharedImage(rawImg, [](Image *img)
                                               { Image::Destroy(img); });
            ResourceManager::Get().Register<Image>(normalizedStr, sharedImage);
            handle = ResourceHandle<Image>(sharedImage);
        }

        auto it = std::find_if(m_images.begin(), m_images.end(),
                               [&handle](const ResourceHandle<Image> &existing)
                               {
                                   return existing.get() == handle.get();
                               });
        if (it == m_images.end())
            m_images.push_back(handle);
        return handle;
    }

    bool ModelAsset::RemoveNode(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return false;

        // 1. Collect subtree via BFS
        std::vector<int> subtree;
        subtree.push_back(nodeIndex);
        size_t head = 0;
        while (head < subtree.size())
        {
            for (int child : m_nodeInfos[subtree[head++]].children)
                subtree.push_back(child);
        }
        std::set<int> removedNodes(subtree.begin(), subtree.end());

        // If removing all nodes, the model is empty
        if (removedNodes.size() == m_nodeInfos.size())
            return true;

        // 2. Detach from parent
        int parent = m_nodeInfos[nodeIndex].parent;
        if (parent >= 0)
        {
            auto &siblings = m_nodeInfos[parent].children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), nodeIndex), siblings.end());
        }

        // 3. Find orphaned mesh indices (only referenced by removed nodes)
        std::set<int> orphanedMeshes;
        for (int ni : subtree)
        {
            int mi = GetNodeMesh(ni);
            if (mi >= 0)
                orphanedMeshes.insert(mi);
        }
        for (int ni = 0; ni < static_cast<int>(m_nodeInfos.size()); ni++)
        {
            if (removedNodes.count(ni))
                continue;
            orphanedMeshes.erase(GetNodeMesh(ni));
        }

        // 4. Build index remaps
        std::vector<int> meshRemap(m_meshInfos.size(), -1);
        int newMeshCount = 0;
        for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
        {
            if (!orphanedMeshes.count(i))
                meshRemap[i] = newMeshCount++;
        }

        std::vector<int> nodeRemap(m_nodeInfos.size(), -1);
        int newNodeCount = 0;
        for (int i = 0; i < static_cast<int>(m_nodeInfos.size()); i++)
        {
            if (!removedNodes.count(i))
                nodeRemap[i] = newNodeCount++;
        }

        // 5. Rebuild flat vertex/index arrays, skipping orphaned mesh data
        std::vector<Vertex> newVertices;
        std::vector<PositionUvVertex> newPosUvs;
        std::vector<uint32_t> newIndices;
        std::vector<AabbVertex> newAabbVerts;

        newVertices.reserve(m_vertices.size());
        newPosUvs.reserve(m_positionUvs.size());
        newIndices.reserve(m_indices.size());
        newAabbVerts.reserve(m_aabbVertices.size());

        uint32_t vOff = 0, iOff = 0;
        size_t aOff = 0;
        for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
        {
            const MeshInfo &mi = m_meshInfos[i];
            if (!orphanedMeshes.count(i))
            {
                newVertices.insert(newVertices.end(),
                                   m_vertices.begin() + vOff, m_vertices.begin() + vOff + mi.verticesCount);
                newPosUvs.insert(newPosUvs.end(),
                                 m_positionUvs.begin() + vOff, m_positionUvs.begin() + vOff + mi.verticesCount);
                newIndices.insert(newIndices.end(),
                                  m_indices.begin() + iOff, m_indices.begin() + iOff + mi.indicesCount);
                if (aOff + 8 <= m_aabbVertices.size())
                    newAabbVerts.insert(newAabbVerts.end(),
                                        m_aabbVertices.begin() + aOff, m_aabbVertices.begin() + aOff + 8);
            }
            vOff += mi.verticesCount;
            iOff += mi.indicesCount;
            aOff += 8;
        }

        m_vertices = std::move(newVertices);
        m_positionUvs = std::move(newPosUvs);
        m_indices = std::move(newIndices);
        m_aabbVertices = std::move(newAabbVerts);

        // 6. Rebuild meshInfos and meshRuntimeInfos
        std::vector<MeshInfo> newMeshInfos;
        std::vector<MeshRuntimeInfo> newMeshRuntimeInfos;
        newMeshInfos.reserve(newMeshCount);
        newMeshRuntimeInfos.reserve(newMeshCount);
        for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
        {
            if (!orphanedMeshes.count(i))
            {
                newMeshInfos.push_back(std::move(m_meshInfos[i]));
                if (i < static_cast<int>(m_meshRuntimeInfos.size()))
                    newMeshRuntimeInfos.push_back(std::move(m_meshRuntimeInfos[i]));
                else
                    newMeshRuntimeInfos.push_back(MeshRuntimeInfo{});
            }
        }
        m_meshInfos = std::move(newMeshInfos);
        m_meshRuntimeInfos = std::move(newMeshRuntimeInfos);

        // 7. Rebuild nodeInfos, nodeRuntimeInfos, and nodeToMesh with remapped indices
        std::vector<NodeInfo> newNodeInfos;
        std::vector<NodeRuntimeInfo> newNodeRuntimeInfos;
        std::vector<int> newNodeToMesh;
        newNodeInfos.reserve(newNodeCount);
        newNodeRuntimeInfos.reserve(newNodeCount);
        newNodeToMesh.reserve(newNodeCount);

        for (int i = 0; i < static_cast<int>(m_nodeInfos.size()); i++)
        {
            if (removedNodes.count(i))
                continue;

            NodeInfo ni = std::move(m_nodeInfos[i]);
            ni.parent = (ni.parent >= 0) ? nodeRemap[ni.parent] : -1;

            std::vector<int> newChildren;
            for (int c : ni.children)
            {
                if (nodeRemap[c] >= 0)
                    newChildren.push_back(nodeRemap[c]);
            }
            ni.children = std::move(newChildren);

            newNodeInfos.push_back(std::move(ni));

            if (i < static_cast<int>(m_nodeRuntimeInfos.size()))
                newNodeRuntimeInfos.push_back(std::move(m_nodeRuntimeInfos[i]));
            else
                newNodeRuntimeInfos.push_back(NodeRuntimeInfo{});

            int oldMesh = (i < static_cast<int>(m_nodeToMesh.size())) ? m_nodeToMesh[i] : -1;
            int newMesh = (oldMesh >= 0 && oldMesh < static_cast<int>(meshRemap.size())) ? meshRemap[oldMesh] : -1;
            newNodeToMesh.push_back(newMesh);
        }

        m_nodeInfos = std::move(newNodeInfos);
        m_nodeRuntimeInfos = std::move(newNodeRuntimeInfos);
        m_nodeToMesh = std::move(newNodeToMesh);

        // 8. Update counts
        m_meshCount = static_cast<uint32_t>(m_meshInfos.size());
        m_verticesCount = static_cast<uint32_t>(m_vertices.size());
        m_indicesCount = static_cast<uint32_t>(m_indices.size());

        // 9. Mark everything dirty
        m_dirtyNodes = true;
        m_nodesMoved.clear();
        for (size_t i = 0; i < m_dirtyUniforms.size(); i++)
            m_dirtyUniforms[i] = true;
        for (auto &ri : m_nodeRuntimeInfos)
        {
            ri.dirty = true;
            for (size_t k = 0; k < ri.dirtyUniforms.size(); k++)
                ri.dirtyUniforms[k] = true;
        }

        return false;
    }

    void ModelAsset::ReparentNode(int nodeIndex, int newParentIndex)
    {
        PE_INFO("ReparentNode: Request to move node %d to parent %d", nodeIndex, newParentIndex);

        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
        {
            PE_INFO("ReparentNode: Invalid node index");
            return;
        }

        // Check for self parenting
        if (nodeIndex == newParentIndex)
        {
            PE_INFO("ReparentNode: Self parenting");
            return;
        }

        // check if newParentIndex is a child of nodeIndex (circular dependency)
        // Also check if newParentIndex is valid or -1 (root)
        if (newParentIndex >= 0)
        {
            if (newParentIndex >= static_cast<int>(m_nodeInfos.size()))
                return;

            int check = newParentIndex;
            while (check >= 0)
            {
                if (check == nodeIndex)
                {
                    PE_INFO("ReparentNode: Circular dependency detected");
                    return;
                }
                check = m_nodeInfos[check].parent;
            }
        }

        NodeInfo &node = m_nodeInfos[nodeIndex];
        int oldParentIndex = node.parent;

        if (oldParentIndex == newParentIndex)
        {
            PE_INFO("ReparentNode: New parent is same as old parent");
            return;
        }

        // Cache current world matrix to preserve transform
        mat4 currentWorldMatrix = m_nodeRuntimeInfos[nodeIndex].gpuData.worldMatrix;

        // Remove from old parent
        if (oldParentIndex >= 0)
        {
            auto &children = m_nodeInfos[oldParentIndex].children;
            children.erase(std::remove(children.begin(), children.end(), nodeIndex), children.end());
        }

        // Add to new parent
        if (newParentIndex >= 0)
        {
            m_nodeInfos[newParentIndex].children.push_back(nodeIndex);
        }

        // Update parent
        node.parent = newParentIndex;

        // Calculate new local matrix to preserve world transform
        mat4 newParentWorldMatrix = (newParentIndex >= 0) ? m_nodeRuntimeInfos[newParentIndex].gpuData.worldMatrix : m_matrix;
        node.localMatrix = inverse(newParentWorldMatrix) * currentWorldMatrix;

        // Update flags
        MarkDirty(nodeIndex);
        UpdateNodeMatrices();
    }

    int ModelAsset::CreateNode(const std::string &name, int parentIndex, const mat4 &localMatrix, int meshIndex)
    {
        const int idx = static_cast<int>(m_nodeInfos.size());

        NodeInfo ni{};
        ni.name = name;
        ni.parent = parentIndex;
        ni.localMatrix = localMatrix;
        m_nodeInfos.push_back(std::move(ni));

        NodeRuntimeInfo ri{};
        ri.dirty = true;
        ri.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_nodeRuntimeInfos.push_back(std::move(ri));

        if (m_nodeToMesh.size() <= static_cast<size_t>(idx))
            m_nodeToMesh.resize(static_cast<size_t>(idx) + 1, -1);
        m_nodeToMesh[idx] = meshIndex;

        if (parentIndex >= 0 && parentIndex < static_cast<int>(m_nodeInfos.size()))
            m_nodeInfos[parentIndex].children.push_back(idx);

        MarkDirty(idx);
        return idx;
    }

    int ModelAsset::GetRootNodeIndex() const
    {
        for (int i = 0; i < static_cast<int>(m_nodeInfos.size()); i++)
        {
            if (m_nodeInfos[i].parent < 0)
                return i;
        }
        return -1;
    }

    void ModelAsset::SetNodeName(int nodeIndex, const std::string &name)
    {
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeInfos.size()))
            m_nodeInfos[nodeIndex].name = name;
    }

    const std::string &ModelAsset::GetNodeName(int nodeIndex) const
    {
        static const std::string empty;
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return empty;
        return m_nodeInfos[nodeIndex].name;
    }

    NodeInfo *ModelAsset::GetNodeInfo(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return nullptr;
        return &m_nodeInfos[nodeIndex];
    }

    const NodeInfo *ModelAsset::GetNodeInfo(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return nullptr;
        return &m_nodeInfos[nodeIndex];
    }

    const ModelAsset::NodeGpuData *ModelAsset::GetNodeGpuData(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return nullptr;
        return &m_nodeRuntimeInfos[nodeIndex].gpuData;
    }

    void ModelAsset::SyncNodeGpuDataFromMesh(int nodeIndex)
    {
        int mesh = GetNodeMesh(nodeIndex);
        if (mesh < 0 || mesh >= static_cast<int>(m_meshInfos.size()))
            return;
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return;
        m_nodeRuntimeInfos[nodeIndex].gpuData.materialFactors[0] = m_meshInfos[mesh].materialFactors[0];
        m_nodeRuntimeInfos[nodeIndex].gpuData.materialFactors[1] = m_meshInfos[mesh].materialFactors[1];
    }

    const mat4 &ModelAsset::GetNodeLocalMatrix(int nodeIndex) const
    {
        static const mat4 identity(1.f);
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return identity;
        return m_nodeInfos[nodeIndex].localMatrix;
    }

    void ModelAsset::SetNodeLocalMatrix(int nodeIndex, const mat4 &localMatrix, bool markDirty)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return;
        m_nodeInfos[nodeIndex].localMatrix = localMatrix;
        if (markDirty)
            MarkDirty(nodeIndex);
    }

    const mat4 &ModelAsset::GetNodeWorldMatrix(int nodeIndex) const
    {
        static const mat4 identity(1.f);
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return identity;
        return m_nodeRuntimeInfos[nodeIndex].gpuData.worldMatrix;
    }

    int ModelAsset::GetNodeParentIndex(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return -1;
        return m_nodeInfos[nodeIndex].parent;
    }

    void ModelAsset::SetNodeParentIndex(int nodeIndex, int parentIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return;
        m_nodeInfos[nodeIndex].parent = parentIndex;
    }

    const std::vector<int> &ModelAsset::GetNodeChildren(int nodeIndex) const
    {
        static const std::vector<int> empty;
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return empty;
        return m_nodeInfos[nodeIndex].children;
    }

    void ModelAsset::RebuildNodeChildrenFromParents()
    {
        for (auto &ni : m_nodeInfos)
            ni.children.clear();

        for (int i = 0; i < static_cast<int>(m_nodeInfos.size()); i++)
        {
            int p = m_nodeInfos[i].parent;
            if (p >= 0 && p < static_cast<int>(m_nodeInfos.size()))
                m_nodeInfos[p].children.push_back(i);
        }
    }

    const AABB &ModelAsset::GetNodeWorldBoundingBox(int nodeIndex) const
    {
        static const AABB empty{};
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return empty;
        return m_nodeRuntimeInfos[nodeIndex].worldBoundingBox;
    }

    MeshInfo *ModelAsset::GetMeshInfo(int meshIndex)
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshInfos.size()))
            return nullptr;
        return &m_meshInfos[meshIndex];
    }

    const MeshInfo *ModelAsset::GetMeshInfo(int meshIndex) const
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshInfos.size()))
            return nullptr;
        return &m_meshInfos[meshIndex];
    }

    size_t ModelAsset::GetNodeDataOffset(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return static_cast<size_t>(-1);
        return m_nodeRuntimeInfos[nodeIndex].dataOffset;
    }

    void ModelAsset::SetNodeDataOffset(int nodeIndex, size_t offset)
    {
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeRuntimeInfos.size()))
            m_nodeRuntimeInfos[nodeIndex].dataOffset = offset;
    }

    uint32_t ModelAsset::GetNodeIndirectIndex(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return 0;
        return m_nodeRuntimeInfos[nodeIndex].indirectIndex;
    }

    void ModelAsset::SetNodeIndirectIndex(int nodeIndex, uint32_t index)
    {
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeRuntimeInfos.size()))
            m_nodeRuntimeInfos[nodeIndex].indirectIndex = index;
    }

    int ModelAsset::GetNodeInstanceIndex(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return -1;
        return m_nodeRuntimeInfos[nodeIndex].instanceIndex;
    }

    void ModelAsset::SetNodeInstanceIndex(int nodeIndex, int instanceIndex)
    {
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeRuntimeInfos.size()))
            m_nodeRuntimeInfos[nodeIndex].instanceIndex = instanceIndex;
    }

    bool ModelAsset::IsNodeDirty(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return false;
        return m_nodeRuntimeInfos[nodeIndex].dirty;
    }

    void ModelAsset::SetNodeDirty(int nodeIndex, bool dirty)
    {
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeRuntimeInfos.size()))
            m_nodeRuntimeInfos[nodeIndex].dirty = dirty;
    }

    void ModelAsset::MarkNodeUniformsDirty(int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return;
        for (size_t i = 0; i < m_nodeRuntimeInfos[nodeIndex].dirtyUniforms.size(); i++)
            m_nodeRuntimeInfos[nodeIndex].dirtyUniforms[i] = true;
    }

    bool ModelAsset::IsNodeUniformDirty(int nodeIndex, uint32_t frameIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return false;
        if (frameIndex >= m_nodeRuntimeInfos[nodeIndex].dirtyUniforms.size())
            return false;
        return m_nodeRuntimeInfos[nodeIndex].dirtyUniforms[frameIndex];
    }

    void ModelAsset::SetNodeUniformDirty(int nodeIndex, uint32_t frameIndex, bool dirty)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeRuntimeInfos.size()))
            return;
        if (frameIndex >= m_nodeRuntimeInfos[nodeIndex].dirtyUniforms.size())
            return;
        m_nodeRuntimeInfos[nodeIndex].dirtyUniforms[frameIndex] = dirty;
    }

    uint32_t ModelAsset::GetMeshImageViewIndex(int meshIndex, int slot) const
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshRuntimeInfos.size()))
            return 0xFFFFFFFF;
        if (slot < 0 || slot >= 5)
            return 0xFFFFFFFF;
        return m_meshRuntimeInfos[meshIndex].imageViewIndices[slot];
    }

    void ModelAsset::SetMeshImageViewIndex(int meshIndex, int slot, uint32_t imageViewIndex)
    {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshRuntimeInfos.size()))
            return;
        if (slot < 0 || slot >= 5)
            return;
        m_meshRuntimeInfos[meshIndex].imageViewIndices[slot] = imageViewIndex;
    }

    void ModelAsset::MarkAllUniformsDirty()
    {
        for (size_t i = 0; i < m_dirtyUniforms.size(); i++)
            m_dirtyUniforms[i] = true;
        for (auto &ri : m_nodeRuntimeInfos)
        {
            for (size_t k = 0; k < ri.dirtyUniforms.size(); k++)
                ri.dirtyUniforms[k] = true;
        }
    }

    bool ModelAsset::IsUniformDirty(uint32_t frameIndex) const
    {
        if (frameIndex >= m_dirtyUniforms.size())
            return false;
        return m_dirtyUniforms[frameIndex];
    }

    void ModelAsset::SetUniformDirty(uint32_t frameIndex, bool dirty)
    {
        if (frameIndex < m_dirtyUniforms.size())
            m_dirtyUniforms[frameIndex] = dirty;
    }

    void ModelAsset::ResetRuntimeState()
    {
        m_nodeRuntimeInfos.resize(m_nodeInfos.size());
        m_meshRuntimeInfos.resize(m_meshInfos.size());

        for (auto &ri : m_nodeRuntimeInfos)
        {
            ri = NodeRuntimeInfo{};
            ri.dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        }

        for (auto &mi : m_meshRuntimeInfos)
            mi = MeshRuntimeInfo{};
    }
} // namespace pe
