#include "Scene/ModelAsset.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAssetCooked.h"
#if defined(PE_ENABLE_ASSIMP)
#include "Scene/ModelAssetAssimp.h"
#endif
#include "Scene/SceneNode.h"

namespace pe
{

    ModelAsset::ModelAsset() : m_id{ID::NextID()}
    {
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

        // Cooked meshes (".pemesh") are the player's only mesh source and load on every platform with
        // no Assimp dependency. Source models (glTF/FBX/OBJ) are imported and cooked editor-side.
        if (ModelAssetCooked::IsCookedPath(file))
            return ModelAssetCooked::Load(file);

#if defined(PE_ENABLE_ASSIMP)
        // Editor-only: import a source model via Assimp (used by the import/cook path).
        ModelAsset *model = ModelAssetAssimp::Load(file);
        if (!model)
        {
            PE_WARN("[ModelAsset] Failed to load: %s", fileStr.c_str());
            return nullptr;
        }

        PE_INFO("Loaded model: %s", fileStr.c_str());
        return model;
#else
        PE_WARN("[ModelAsset] Source model loading is disabled in the player; only .pemesh is supported: %s", fileStr.c_str());
        return nullptr;
#endif
    }

    void ModelAsset::DefaultResources::EnsureCreated(CommandBuffer *cmd)
    {
        if (!black)
            black = Image::LoadRGBA8(cmd, Path::RuntimeAssets + "Objects/black.png");
        if (!normal)
            normal = Image::LoadRGBA8(cmd, Path::RuntimeAssets + "Objects/normal.png");
        if (!white)
            white = Image::LoadRGBA8(cmd, Path::RuntimeAssets + "Objects/white.png");

        if (!sampler)
        {
            SamplerDesc info = Sampler::CreateInfoInit();
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

    ResourceHandle<Image> ModelAsset::DefaultTextureForSlot(TextureType slot)
    {
        const auto &defaults = GetDefaultResources();
        switch (slot)
        {
        case TextureType::Normal:
            return ResourceHandle<Image>::FromRaw(defaults.normal);
        case TextureType::Emissive:
            return ResourceHandle<Image>::FromRaw(defaults.black);
        default:
            return ResourceHandle<Image>::FromRaw(defaults.white);
        }
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

    int ModelAsset::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return -1;
        return m_nodeToMesh[nodeIndex];
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

            // The last handle may drop while in-flight bindless descriptors still reference the image.
            std::shared_ptr<Image> sharedImage(rawImg, [](Image *img)
                                               { RHII.AddToDeletionQueue([img]()
                                                                         { Image *i = img; Image::Destroy(i); }); });
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

        // 6. Rebuild meshInfos
        std::vector<MeshInfo> newMeshInfos;
        newMeshInfos.reserve(newMeshCount);
        for (int i = 0; i < static_cast<int>(m_meshInfos.size()); i++)
        {
            if (!orphanedMeshes.count(i))
                newMeshInfos.push_back(std::move(m_meshInfos[i]));
        }
        m_meshInfos = std::move(newMeshInfos);

        // 7. Rebuild nodeInfos and nodeToMesh with remapped indices
        std::vector<NodeInfo> newNodeInfos;
        std::vector<int> newNodeToMesh;
        newNodeInfos.reserve(newNodeCount);
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

            int oldMesh = (i < static_cast<int>(m_nodeToMesh.size())) ? m_nodeToMesh[i] : -1;
            int newMesh = (oldMesh >= 0 && oldMesh < static_cast<int>(meshRemap.size())) ? meshRemap[oldMesh] : -1;
            newNodeToMesh.push_back(newMesh);
        }

        m_nodeInfos = std::move(newNodeInfos);
        m_nodeToMesh = std::move(newNodeToMesh);

        // 8. Update counts
        m_meshCount = static_cast<uint32_t>(m_meshInfos.size());
        m_verticesCount = static_cast<uint32_t>(m_vertices.size());
        m_indicesCount = static_cast<uint32_t>(m_indices.size());

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
        mat4 currentWorldMatrix = ComputeNodeWorldMatrix(nodeIndex);

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
        mat4 newParentWorldMatrix = (newParentIndex >= 0) ? ComputeNodeWorldMatrix(newParentIndex) : m_matrix;
        node.localMatrix = inverse(newParentWorldMatrix) * currentWorldMatrix;
    }

    int ModelAsset::CreateNode(const std::string &name, int parentIndex, const mat4 &localMatrix, int meshIndex)
    {
        const int idx = static_cast<int>(m_nodeInfos.size());

        NodeInfo ni{};
        ni.name = name;
        ni.parent = parentIndex;
        ni.localMatrix = localMatrix;
        m_nodeInfos.push_back(std::move(ni));

        if (m_nodeToMesh.size() <= static_cast<size_t>(idx))
            m_nodeToMesh.resize(static_cast<size_t>(idx) + 1, -1);
        m_nodeToMesh[idx] = meshIndex;

        if (parentIndex >= 0 && parentIndex < static_cast<int>(m_nodeInfos.size()))
            m_nodeInfos[parentIndex].children.push_back(idx);

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

    const mat4 &ModelAsset::GetNodeLocalMatrix(int nodeIndex) const
    {
        static const mat4 identity(1.f);
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return identity;
        return m_nodeInfos[nodeIndex].localMatrix;
    }

    void ModelAsset::SetNodeLocalMatrix(int nodeIndex, const mat4 &localMatrix)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return;
        m_nodeInfos[nodeIndex].localMatrix = localMatrix;
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

    mat4 ModelAsset::ComputeNodeWorldMatrix(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return mat4(1.f);

        mat4 worldMatrix = m_nodeInfos[nodeIndex].localMatrix;
        int parentIndex = m_nodeInfos[nodeIndex].parent;
        while (parentIndex >= 0 && parentIndex < static_cast<int>(m_nodeInfos.size()))
        {
            worldMatrix = m_nodeInfos[parentIndex].localMatrix * worldMatrix;
            parentIndex = m_nodeInfos[parentIndex].parent;
        }

        return m_matrix * worldMatrix;
    }

    AABB ModelAsset::GetNodeWorldBoundingBox(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeInfos.size()))
            return {};

        int meshIndex = GetNodeMesh(nodeIndex);
        if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshInfos.size()))
            return {};

        const MeshInfo &meshInfo = m_meshInfos[meshIndex];
        return TransformAabb(meshInfo.boundingBox, ComputeNodeWorldMatrix(nodeIndex));
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

} // namespace pe
