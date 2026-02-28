#include "Scene/Model.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Scene/ModelAssimp.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static inline AABB TransformAabb(const AABB &local, const mat4 &m)
    {
        // Correct AABB transform (works with rotation/scale):
        // transform 8 corners and rebuild min/max.
        const vec3 &mn = local.min;
        const vec3 &mx = local.max;

        const vec3 corners[8] = {
            {mn.x, mn.y, mn.z},
            {mx.x, mn.y, mn.z},
            {mx.x, mx.y, mn.z},
            {mn.x, mx.y, mn.z},
            {mn.x, mn.y, mx.z},
            {mx.x, mn.y, mx.z},
            {mx.x, mx.y, mx.z},
            {mn.x, mx.y, mx.z},
        };

        vec3 outMin(std::numeric_limits<float>::max());
        vec3 outMax(-std::numeric_limits<float>::max());

        for (const vec3 &c : corners)
        {
            vec4 t = m * vec4(c, 1.f);
            vec3 p(t.x, t.y, t.z);
            outMin = min(outMin, p);
            outMax = max(outMax, p);
        }

        AABB out{};
        out.min = outMin;
        out.max = outMax;
        return out;
    }

    Model::Model() : m_id{ID::NextID()}
    {
        m_dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_label = "Model_" + std::to_string(m_id);
    }

    Model::~Model()
    {
        Unload();
    }

    void Model::Unload()
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

    void Model::AddImage(Image *image, bool owned)
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

    void Model::AddSampler(Sampler *sampler, bool owned)
    {
        if (!sampler)
            return;

        if (std::find(m_samplers.begin(), m_samplers.end(), sampler) == m_samplers.end())
            m_samplers.push_back(sampler);

        if (!owned)
            m_sharedSamplers.insert(sampler);
    }

    Model *Model::Load(const std::filesystem::path &file)
    {
        auto fileU8 = file.u8string();
        std::string fileStr(reinterpret_cast<const char *>(fileU8.c_str()));
        if (!std::filesystem::exists(file))
        {
            PE_WARN("Model file not found: %s", fileStr.c_str());
            return nullptr;
        }

        // For now we route everything through Assimp (it supports many formats).
        Model *model = ModelAssimp::Load(file);
        if (!model)
        {
            PE_WARN("Failed to load model: %s", fileStr.c_str());
            return nullptr;
        }

        EventSystem::PushEvent(EventType::ModelLoaded, model);
        PE_INFO("Loaded model: %s", fileStr.c_str());
        return model;
    }

    void Model::DefaultResources::EnsureCreated(CommandBuffer *cmd)
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

    Model::DefaultResources &Model::GetDefaultResources(CommandBuffer *cmd)
    {
        DefaultResources &defaults = Defaults();
        defaults.EnsureCreated(cmd);
        return defaults;
    }

    const Model::DefaultResources &Model::GetDefaultResources()
    {
        return Defaults();
    }

    Model::DefaultResources &Model::Defaults()
    {
        static DefaultResources defaults;
        return defaults;
    }

    void Model::ResetResources(CommandBuffer *cmd)
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

    void Model::DestroyDefaults()
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

    void Model::UpdateNodeMatrices()
    {
        if (!m_dirtyNodes && !m_previousMatricesIsDirty)
            return;

        for (int i = 0; i < GetNodeCount(); i++)
            UpdateNodeMatrix(i);

        m_previousMatricesIsDirty = m_dirtyNodes;
        m_dirtyNodes = false;
    }

    void Model::UpdateNodeMatrix(int node)
    {
        if (node < 0 || node >= static_cast<int>(m_nodeInfos.size()))
            return;

        NodeInfo &nodeInfo = m_nodeInfos[node];

        // keep previous (for motion vectors, etc.)
        bool previousMatrixChanged = false;
        if (nodeInfo.ubo.previousWorldMatrix != nodeInfo.ubo.worldMatrix)
        {
            nodeInfo.ubo.previousWorldMatrix = nodeInfo.ubo.worldMatrix;
            previousMatrixChanged = true;
        }

        if (!nodeInfo.dirty && !previousMatrixChanged)
            return;

        int parent = nodeInfo.parent;
        if (parent >= 0)
        {
            // Parent is already updated
            nodeInfo.ubo.worldMatrix = m_nodeInfos[parent].ubo.worldMatrix * nodeInfo.localMatrix;
        }
        else
        {
            nodeInfo.ubo.worldMatrix = m_matrix * nodeInfo.localMatrix;
        }

        // update mesh world AABB if node owns a mesh
        int mesh = GetNodeMesh(node);
        if (mesh >= 0 && mesh < static_cast<int>(m_meshInfos.size()))
        {
            const MeshInfo &meshInfo = m_meshInfos[mesh];
            nodeInfo.worldBoundingBox = TransformAabb(meshInfo.boundingBox, nodeInfo.ubo.worldMatrix);
        }

        nodeInfo.dirty = false;
        m_nodesMoved.push_back(node);

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            nodeInfo.dirtyUniforms[i] = true;
            m_dirtyUniforms[i] = true;
        }
    }

    void Model::MarkDirty(int node)
    {
        if (node < 0 || node >= static_cast<int>(m_nodeInfos.size()))
            return;

        // Use cached children list
        std::vector<int> toProcess;
        toProcess.reserve(64);
        toProcess.push_back(node);

        // BFS traversal
        size_t head = 0;
        while (head < toProcess.size())
        {
            int currentCtx = toProcess[head++];
            NodeInfo &ni = m_nodeInfos[currentCtx];

            // Mark dirty
            ni.dirty = true;
            for (size_t k = 0; k < ni.dirtyUniforms.size(); k++)
                ni.dirtyUniforms[k] = true;

            // Enqueue children
            for (int child : ni.children)
                toProcess.push_back(child);
        }

        m_dirtyNodes = true;
        for (size_t i = 0; i < m_dirtyUniforms.size(); i++)
            m_dirtyUniforms[i] = true;
    }

    int Model::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return -1;
        return m_nodeToMesh[nodeIndex];
    }

    ResourceHandle<Image> Model::LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath)
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

            std::shared_ptr<Image> sharedImage(rawImg);
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

    void Model::ReparentNode(int nodeIndex, int newParentIndex)
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
        mat4 currentWorldMatrix = node.ubo.worldMatrix;

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
        mat4 newParentWorldMatrix = (newParentIndex >= 0) ? m_nodeInfos[newParentIndex].ubo.worldMatrix : m_matrix;
        node.localMatrix = inverse(newParentWorldMatrix) * currentWorldMatrix;

        // Update flags
        MarkDirty(nodeIndex);
        UpdateNodeMatrices();
    }
} // namespace pe
