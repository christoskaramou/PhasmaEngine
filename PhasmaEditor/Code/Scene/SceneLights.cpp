#include "Scene/Scene.h"
#include "API/Buffer.h"
#include "API/RHI.h"

namespace pe
{
    NodeId *Scene::CreateLightNode(const std::string &name, const mat4 &localMatrix, NodeId *parent)
    {
        NodeId *node = CreateNode(name, parent);
        AddComponentFlag(node, Component_Light);
        SetLocalMatrix(node, localMatrix);
        MarkNodeDirty(node);
        return node;
    }

    void Scene::InitLightBuffers()
    {
        uint32_t count = RHII.GetSwapchainImageCount();
        m_lightUniforms.resize(count);
        m_lightStorageBuffers.resize(count);

        for (uint32_t i = 0; i < count; i++)
        {
            m_lightUniforms[i] = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(LightsUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "lights_uniform_buffer",
            });
            m_lightUniforms[i]->Map();
            m_lightUniforms[i]->Zero();
            m_lightUniforms[i]->Flush();
            m_lightUniforms[i]->Unmap();

            m_lightStorageBuffers[i] = Buffer::Create({
                .size = 1024,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "lights_storage_buffer",
            });
            m_lightStorageBuffers[i]->Map();
            m_lightStorageBuffers[i]->Zero();
            m_lightStorageBuffers[i]->Flush();
            m_lightStorageBuffers[i]->Unmap();
        }

        // Create default directional light with its scene node
        DirectionalLightEditor l{};
        l.color = {.9765f, .8431f, .9098f, 5.0f};
        l.position = {0.0f, 10.0f, 0.0f, 2.0f};
        quat q_dir = quat(radians(vec3(-90.1f, 0.f, 0.f)));
        l.rotation = {q_dir.x, q_dir.y, q_dir.z, q_dir.w};
        l.name = "Directional Light " + std::to_string(ID::NextID());
        mat4 lightMat = glm::translate(mat4(1.f), vec3(l.position)) * glm::mat4_cast(q_dir);
        l.nodeId = CreateLightNode(l.name, lightMat, nullptr);
        m_directionalLights.push_back(l);

        UpdateNodeMatrices();
        UpdateLights();
    }

    void Scene::DestroyLightBuffers()
    {
        for (auto &uniform : m_lightUniforms)
            Buffer::Destroy(uniform);
        for (auto &sb : m_lightStorageBuffers)
            Buffer::Destroy(sb);
        m_lightUniforms.clear();
        m_lightStorageBuffers.clear();
    }

    void Scene::UpdateLights()
    {
        PE_PROFILE_SCOPE("Light Update");
        auto &gSettings = Settings::Get<GlobalSettings>();

        if (gSettings.randomize_lights)
        {
            gSettings.randomize_lights = false;
            for (auto &l : m_pointLights)
            {
                l.color = vec4(rand(0.f, 1.f), rand(0.f, 1.f), rand(0.f, 1.f), 1.0f);
                l.position = vec4(rand(-10.5f, 10.5f), rand(.7f, 6.7f), rand(-4.5f, 4.5f), 10.0f);
            }
        }

        // Directional light day/night toggle
        if (!m_directionalLights.empty())
        {
            static float lastIntensity = m_directionalLights[0].color.w == 0.0f ? 7.0f : m_directionalLights[0].color.w;
            if (m_directionalLights[0].color.w > 0.0f)
                lastIntensity = m_directionalLights[0].color.w;
            m_directionalLights[0].color.w = gSettings.day ? lastIntensity : 0.0f;
        }

        // Sync position/rotation from node world matrices
        auto extractRotation = [](const mat4 &world) -> quat
        {
            mat3 rot3(world);
            rot3[0] = normalize(rot3[0]);
            rot3[1] = normalize(rot3[1]);
            rot3[2] = normalize(rot3[2]);
            return quat_cast(rot3);
        };

        for (auto &l : m_directionalLights)
        {
            if (l.nodeId)
            {
                const mat4 &world = GetWorldMatrix(l.nodeId);
                l.position = vec4(vec3(world[3]), l.position.w);
                quat q = extractRotation(world);
                l.rotation = vec4(q.x, q.y, q.z, q.w);
            }
        }
        for (auto &l : m_pointLights)
        {
            if (l.nodeId)
            {
                const mat4 &world = GetWorldMatrix(l.nodeId);
                l.position = vec4(vec3(world[3]), l.position.w);
            }
        }
        for (auto &l : m_spotLights)
        {
            if (l.nodeId)
            {
                const mat4 &world = GetWorldMatrix(l.nodeId);
                l.position = vec4(vec3(world[3]), l.position.w);
                quat q = extractRotation(world);
                l.rotation = vec4(q.x, q.y, q.z, q.w);
            }
        }
        for (auto &l : m_areaLights)
        {
            if (l.nodeId)
            {
                const mat4 &world = GetWorldMatrix(l.nodeId);
                l.position = vec4(vec3(world[3]), l.position.w);
                quat q = extractRotation(world);
                l.rotation = vec4(q.x, q.y, q.z, q.w);
            }
        }

        // Pack into POD vectors for GPU upload
        m_directionalLightsPOD.resize(m_directionalLights.size());
        for (size_t i = 0; i < m_directionalLights.size(); i++)
            m_directionalLightsPOD[i] = m_directionalLights[i];

        m_pointLightsPOD.resize(m_pointLights.size());
        for (size_t i = 0; i < m_pointLights.size(); i++)
            m_pointLightsPOD[i] = m_pointLights[i];

        m_spotLightsPOD.resize(m_spotLights.size());
        for (size_t i = 0; i < m_spotLights.size(); i++)
            m_spotLightsPOD[i] = m_spotLights[i];

        m_areaLightsPOD.resize(m_areaLights.size());
        for (size_t i = 0; i < m_areaLights.size(); i++)
            m_areaLightsPOD[i] = m_areaLights[i];

        // Calculate aligned offsets and total storage size
        const size_t sizeDirectional = m_directionalLightsPOD.size() * sizeof(DirectionalLight);
        const size_t sizePoint = m_pointLightsPOD.size() * sizeof(PointLight);
        const size_t sizeSpot = m_spotLightsPOD.size() * sizeof(SpotLight);
        const size_t sizeArea = m_areaLightsPOD.size() * sizeof(AreaLight);

        auto align16 = [](uint32_t v)
        { return (v + 15u) & ~15u; };
        const uint32_t offsetDirectional = 0;
        const uint32_t offsetPoint = offsetDirectional + align16(static_cast<uint32_t>(sizeDirectional));
        const uint32_t offsetSpot = offsetPoint + align16(static_cast<uint32_t>(sizePoint));
        const uint32_t offsetArea = offsetSpot + align16(static_cast<uint32_t>(sizeSpot));
        const uint32_t totalSize = offsetArea + static_cast<uint32_t>(sizeArea);

        // Resize storage buffer for current frame if needed
        const int frameIndex = RHII.GetFrameIndex();
        Buffer *sb = m_lightStorageBuffers[frameIndex];

        if (totalSize > 0 && sb->Size() < totalSize)
        {
            Buffer::Destroy(sb);
            m_lightStorageBuffers[frameIndex] = Buffer::Create({
                .size = totalSize * 2,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT,
                .name = "lights_storage_buffer",
            });
            sb = m_lightStorageBuffers[frameIndex];
        }

        // Upload packed light data to storage buffer
        std::vector<BufferRange> uploadRanges;
        uploadRanges.reserve(4);
        if (sizeDirectional > 0)
            uploadRanges.push_back({m_directionalLightsPOD.data(), sizeDirectional, static_cast<size_t>(offsetDirectional)});
        if (sizePoint > 0)
            uploadRanges.push_back({m_pointLightsPOD.data(), sizePoint, static_cast<size_t>(offsetPoint)});
        if (sizeSpot > 0)
            uploadRanges.push_back({m_spotLightsPOD.data(), sizeSpot, static_cast<size_t>(offsetSpot)});
        if (sizeArea > 0)
            uploadRanges.push_back({m_areaLightsPOD.data(), sizeArea, static_cast<size_t>(offsetArea)});

        if (!uploadRanges.empty())
            sb->Copy(static_cast<uint32_t>(uploadRanges.size()), uploadRanges.data(), true);

        // Upload UBO (counts + offsets)
        m_lightsUBO.numDirectionalLights = static_cast<uint32_t>(m_directionalLights.size());
        m_lightsUBO.numPointLights = static_cast<uint32_t>(m_pointLights.size());
        m_lightsUBO.numSpotLights = static_cast<uint32_t>(m_spotLights.size());
        m_lightsUBO.numAreaLights = static_cast<uint32_t>(m_areaLights.size());
        m_lightsUBO.offsetDirectionalLights = offsetDirectional;
        m_lightsUBO.offsetPointLights = offsetPoint;
        m_lightsUBO.offsetSpotLights = offsetSpot;
        m_lightsUBO.offsetAreaLights = offsetArea;

        BufferRange uboRange{&m_lightsUBO, sizeof(LightsUBO), 0};
        m_lightUniforms[frameIndex]->Copy(1, &uboRange, false);
    }

    void Scene::CreateDirectionalLight(NodeId *parent)
    {
        DirectionalLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 10.0f, 0.0f, 2.0f);
        quat q_dir = quat(radians(vec3(-90.1f, 0.f, 0.f)));
        l.rotation = {q_dir.x, q_dir.y, q_dir.z, q_dir.w};
        l.name = "Directional Light " + std::to_string(ID::NextID());
        mat4 lightMat = glm::translate(mat4(1.f), vec3(0.0f, 10.0f, 0.0f)) * glm::mat4_cast(q_dir);
        l.nodeId = CreateLightNode(l.name, lightMat, parent);
        m_directionalLights.push_back(l);
    }

    void Scene::CreatePointLight(NodeId *parent)
    {
        PointLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.name = "Point Light " + std::to_string(ID::NextID());
        l.nodeId = CreateLightNode(l.name, mat4(1.f), parent);
        m_pointLights.push_back(l);
    }

    void Scene::CreateSpotLight(NodeId *parent)
    {
        SpotLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.rotation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        l.params = vec4(15.0f, 5.0f, 0.0f, 0.0f);
        l.name = "Spot Light " + std::to_string(ID::NextID());
        l.nodeId = CreateLightNode(l.name, mat4(1.f), parent);
        m_spotLights.push_back(l);
    }

    void Scene::CreateAreaLight(NodeId *parent)
    {
        AreaLightEditor l{};
        l.color = vec4(1.0f);
        l.position = vec4(0.0f, 0.0f, 0.0f, 10.0f);
        l.rotation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        l.size = vec4(2.0f, 2.0f, 0.0f, 0.0f);
        l.name = "Area Light " + std::to_string(ID::NextID());
        l.nodeId = CreateLightNode(l.name, mat4(1.f), parent);
        m_areaLights.push_back(l);
    }

    void Scene::RemoveLight(LightType type, int index)
    {
        auto getNodeId = [&](auto &vec) -> NodeId *
        {
            if (index < 0 || index >= static_cast<int>(vec.size()))
                return nullptr;
            return vec[index].nodeId;
        };

        NodeId *nodeId = nullptr;
        switch (type)
        {
        case LightType::Directional:
            nodeId = getNodeId(m_directionalLights);
            break;
        case LightType::Point:
            nodeId = getNodeId(m_pointLights);
            break;
        case LightType::Spot:
            nodeId = getNodeId(m_spotLights);
            break;
        case LightType::Area:
            nodeId = getNodeId(m_areaLights);
            break;
        }

        // DeleteNode handles light vector cleanup via Component_Light check
        if (nodeId)
            DeleteNode(nodeId);
    }

    std::pair<LightType, int> Scene::GetLightForNode(const NodeId *node) const
    {
        for (int i = 0; i < static_cast<int>(m_directionalLights.size()); i++)
            if (m_directionalLights[i].nodeId == node)
                return {LightType::Directional, i};
        for (int i = 0; i < static_cast<int>(m_pointLights.size()); i++)
            if (m_pointLights[i].nodeId == node)
                return {LightType::Point, i};
        for (int i = 0; i < static_cast<int>(m_spotLights.size()); i++)
            if (m_spotLights[i].nodeId == node)
                return {LightType::Spot, i};
        for (int i = 0; i < static_cast<int>(m_areaLights.size()); i++)
            if (m_areaLights[i].nodeId == node)
                return {LightType::Area, i};
        return {LightType::Directional, -1};
    }
} // namespace pe
