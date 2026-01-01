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
            outMin = glm::min(outMin, p);
            outMax = glm::max(outMax, p);
        }

        AABB out{};
        out.min = outMin;
        out.max = outMax;
        return out;
    }

    Model::Model() : m_id{ID::NextID()}
    {
        dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_label = "Model_" + std::to_string(m_id);
    }

    Model::~Model()
    {
        for (auto sampler : m_samplers)
        {
            if (!sampler)
                continue;
            if (m_sharedSamplers.find(sampler) != m_sharedSamplers.end())
                continue;
            Sampler::Destroy(sampler);
        }

        for (auto image : m_images)
        {
            if (!image)
                continue;
            if (m_sharedImages.find(image) != m_sharedImages.end())
                continue;
            Image::Destroy(image);
        }
    }

    void Model::AddImage(Image *image, bool owned)
    {
        if (!image)
            return;

        if (std::find(m_images.begin(), m_images.end(), image) == m_images.end())
            m_images.push_back(image);

        if (!owned)
            m_sharedImages.insert(image);
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
        PE_ERROR_IF(!std::filesystem::exists(file), std::string("Model file not found: " + file.string()).c_str());

        // For now we route everything through Assimp (it supports many formats).
        Model *model = ModelAssimp::Load(file);
        PE_ERROR_IF(!model, std::string("Failed to load model: " + file.string()).c_str());

        EventSystem::PushEvent(EventType::ModelLoaded, model);
        return model;
    }

    void Model::DefaultResources::EnsureCreated(CommandBuffer *cmd)
    {
        if (!black)
            black = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/black.png");
        if (!normal)
            normal = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/normal.png");
        if (!white)
            white = Image::LoadRGBA8(cmd, Path::Executable + "Assets/Objects/white.png");

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
        m_sharedImages.clear();
        m_sharedSamplers.clear();
        m_textureCache.clear();

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
        if (!dirtyNodes && !m_previousMatricesIsDirty)
            return;

        for (int i = 0; i < GetNodeCount(); i++)
            UpdateNodeMatrix(i);

        m_previousMatricesIsDirty = dirtyNodes;
        dirtyNodes = false;
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

        // compute world from local * parent chain
        mat4 trans = nodeInfo.localMatrix;
        int parent = nodeInfo.parent;
        while (parent >= 0)
        {
            const NodeInfo &parentInfo = m_nodeInfos[parent];
            trans = parentInfo.localMatrix * trans;
            parent = parentInfo.parent;
        }

        nodeInfo.ubo.worldMatrix = matrix * trans;

        // update mesh world AABB if node owns a mesh
        int mesh = GetNodeMesh(node);
        if (mesh >= 0 && mesh < static_cast<int>(m_meshInfos.size()))
        {
            MeshInfo &meshInfo = m_meshInfos[mesh];
            meshInfo.worldBoundingBox = TransformAabb(meshInfo.boundingBox, nodeInfo.ubo.worldMatrix);
        }

        nodeInfo.dirty = false;

        // mark per possible frame in flight dirty UBO
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            nodeInfo.dirtyUniforms[i] = true;
            dirtyUniforms[i] = true;
        }
    }

    void Model::SetMeshFactors(Buffer *buffer)
    {
        // copy factors in uniform buffer
        BufferRange range{};
        buffer->Map();

        for (int i = 0; i < GetNodeCount(); i++)
        {
            int mesh = GetNodeMesh(i);
            if (mesh < 0)
                continue;

            auto &meshInfo = m_meshInfos[mesh];
            range.data = &meshInfo.materialFactors;
            range.size = sizeof(mat4);
            range.offset = meshInfo.GetMeshFactorsOffset();
            buffer->Copy(1, &range, true);
        }

        buffer->Unmap();
    }

    int Model::GetNodeMesh(int nodeIndex) const
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodeToMesh.size()))
            return -1;
        return m_nodeToMesh[nodeIndex];
    }

    Image *Model::LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath)
    {
        if (texturePath.empty())
            return nullptr;

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(texturePath, ec);
        if (ec)
            normalized = texturePath;

        if (Image *image = GetTextureFromCache(normalized))
            return image;

        if (!std::filesystem::exists(normalized))
            return nullptr;

        Image *image = Image::LoadRGBA8(cmd, normalized.string());
        if (!image)
            return nullptr;

        m_textureCache.emplace(normalized.generic_string(), image);
        AddImage(image, true);
        return image;
    }

    Image *Model::GetTextureFromCache(const std::filesystem::path &texturePath) const
    {
        if (texturePath.empty())
            return nullptr;

        auto it = m_textureCache.find(texturePath.generic_string());
        if (it == m_textureCache.end())
            return nullptr;
        return it->second;
    }

} // namespace pe
