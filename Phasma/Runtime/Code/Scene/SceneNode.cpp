#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/SceneRuntimeHooks.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "Camera/Camera.h"
#include "API/RHI.h"

namespace pe
{
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
        if ((flag & Component_Physics2D) && !c.physics2d)
            c.physics2d = entity->CreateComponent<NodePhysics2DTag>();
        if ((flag & Component_Audio) && !c.audio)
            c.audio = entity->CreateComponent<NodeAudioTag>();
        if ((flag & Component_Skybox) && !c.skybox)
        {
            c.skybox = entity->CreateComponent<NodeSkyboxTag>();
            const auto &settings = Settings::Get<SceneSettings>();
            c.skybox->path = settings.skybox_path;
        }
        if ((flag & Component_RuntimeUi) && !c.runtimeUi)
            c.runtimeUi = entity->CreateComponent<NodeRuntimeUiTag>();
        if ((flag & Component_Prefab) && !c.prefab)
            c.prefab = entity->CreateComponent<NodePrefabComponent>();
        if ((flag & Component_Sprite) && !c.sprite)
            c.sprite = entity->CreateComponent<NodeSpriteComponent>();
        if ((flag & Component_SceneSettings) && !c.sceneSettings)
            c.sceneSettings = entity->CreateComponent<NodeSceneSettingsTag>();
        if ((flag & Component_TriggerZone) && !c.triggerZone)
            c.triggerZone = entity->CreateComponent<NodeTriggerZoneTag>();
        if ((flag & Component_VoxelWorld) && !c.voxelWorld)
            c.voxelWorld = entity->CreateComponent<NodeVoxelWorldTag>();
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
        if ((flag & Component_Physics2D) && c.physics2d)
        {
            entity->RemoveComponent<NodePhysics2DTag>();
            c.physics2d = nullptr;
        }
        if ((flag & Component_Audio) && c.audio)
        {
            entity->RemoveComponent<NodeAudioTag>();
            c.audio = nullptr;
        }
        if ((flag & Component_Skybox) && c.skybox)
        {
            entity->RemoveComponent<NodeSkyboxTag>();
            c.skybox = nullptr;
            ApplySkyboxSettingsFromNode();
        }
        if ((flag & Component_RuntimeUi) && c.runtimeUi)
        {
            entity->RemoveComponent<NodeRuntimeUiTag>();
            c.runtimeUi = nullptr;
        }
        if ((flag & Component_Prefab) && c.prefab)
        {
            entity->RemoveComponent<NodePrefabComponent>();
            c.prefab = nullptr;
        }
        if ((flag & Component_Sprite) && c.sprite)
        {
            entity->RemoveComponent<NodeSpriteComponent>();
            c.sprite = nullptr;
        }
        if ((flag & Component_SceneSettings) && c.sceneSettings)
        {
            entity->RemoveComponent<NodeSceneSettingsTag>();
            c.sceneSettings = nullptr;
        }
        if ((flag & Component_TriggerZone) && c.triggerZone)
        {
            entity->RemoveComponent<NodeTriggerZoneTag>();
            c.triggerZone = nullptr;
        }
        if ((flag & Component_VoxelWorld) && c.voxelWorld)
        {
            entity->RemoveComponent<NodeVoxelWorldTag>();
            c.voxelWorld = nullptr;
        }
    }

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
        m_nodeRuntime.push_back(std::move(runtime));

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

        m_nodeComponentCache.push_back({nameComp, hierarchyComp, transformComp, meshRefsComp, scriptComp, nullptr,
                                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});

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
        const bool hasMeshRefs = !cache.meshRefs->meshRefs.empty();

        // Null out material pointers on meshes so stale entries in m_meshes
        // don't dereference freed Materials after the owning model is deleted.
        // Only null if no other node still references each mesh.
        for (int meshRef : cache.meshRefs->meshRefs)
        {
            if (meshRef < 0 || meshRef >= static_cast<int>(m_meshes.size()))
                continue;

            bool otherRef = false;
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); i++)
            {
                if (i == idx)
                    continue;
                for (int mr : m_nodeComponentCache[i].meshRefs->meshRefs)
                {
                    if (mr == meshRef)
                    {
                        otherRef = true;
                        break;
                    }
                }
                if (otherRef)
                    break;
            }
            if (!otherRef)
            {
                m_meshes[meshRef].material = nullptr;
                m_meshes[meshRef].materialInstance = nullptr;
                m_meshes[meshRef].live = false;
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
        if (cache.physics)
            RemoveScenePhysicsBody(node);
#ifdef PE_PHYSICS2D
        if (cache.physics2d)
            RemoveScenePhysics2DBody(node);
#endif
        if (cache.audio)
            RemoveSceneAudioSource(node);
        if (cache.skybox)
        {
            auto &settings = Settings::Get<SceneSettings>();
            settings.skybox_path.clear();
            RefreshSceneSky();
        }
        RemoveSceneAnimation(node);

        for (auto modelRootIt = m_modelRootNodes.begin(); modelRootIt != m_modelRootNodes.end();)
        {
            auto &roots = modelRootIt->second;
            roots.erase(std::remove(roots.begin(), roots.end(), node), roots.end());
            if (!roots.empty())
            {
                ++modelRootIt;
                continue;
            }

            const size_t modelId = modelRootIt->first;
            modelRootIt = m_modelRootNodes.erase(modelRootIt);
            auto modelIt = m_models.find(modelId);
            if (modelIt != m_models.end())
            {
                ModelAsset *model = *modelIt;
                m_models.erase(modelId);
                ResetSkeletonCache();
                delete model;
            }
        }

        NodeId *parent = cache.hierarchy->parent;
        if (parent)
        {
            auto &siblings = m_nodeComponentCache[parent->index].hierarchy->children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
        }

        if (node->entity)
        {
            Context::Get()->RemoveEntity(node->entity->GetID());
            node->entity = nullptr;
        }

        m_nodesMoved.erase(std::remove(m_nodesMoved.begin(), m_nodesMoved.end(), node), m_nodesMoved.end());

        SwapAndPopNode(idx);
        node->revision++; // Invalidate all old IDs pointing to this deleted node
        node->index = UINT32_MAX;
        m_freeNodeIds.push_back(node);

        m_nodesDirty = true;
        if (hasMeshRefs)
        {
            m_instancesDirty = true;
            m_tlasDirty = true;
        }
        m_dirty = true;
    }

    void Scene::SwapAndPopNode(uint32_t index)
    {
        const uint32_t last = static_cast<uint32_t>(m_nodeIds.size()) - 1;

        if (index != last)
        {
            std::swap(m_nodeIds[index], m_nodeIds[last]);
            std::swap(m_nodeComponentCache[index], m_nodeComponentCache[last]);
            std::swap(m_nodeRuntime[index], m_nodeRuntime[last]);

            // The relocated node keeps its identity — only its array slot changes, so update index
            // in place and DO NOT bump revision (per the NodeId contract in SceneNode.h). Bumping it
            // here would silently orphan every cached (nodeId, revision) pair for a still-live node —
            // physics/physics2d/animation/script state and Lua handles — whenever a sibling is deleted.
            m_nodeIds[index]->index = index;
        }

        m_nodeIds.pop_back();
        m_nodeComponentCache.pop_back();
        m_nodeRuntime.pop_back();
    }

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
        mat4 &localMatrix = m_nodeComponentCache[node->index].transform->localMatrix;
        // Transform early-out: a redundant write (same matrix) must not re-dirty the node or
        // recurse the subtree. set_position/set_rotation/set_scale all round-trip through here,
        // so per-frame drivers that re-assert an unchanged transform (pooled props, HUD nodes,
        // idle units) cost nothing. Material edits dirty via their own MarkNodeDirty path, so
        // skipping here only elides redundant transform work.
        if (localMatrix == m)
            return;

        localMatrix = m;
        if (markDirty)
            MarkNodeDirty(node);
    }

    void Scene::SetNodeEnabled(NodeId *node, bool enabled)
    {
        if (!IsNodeAlive(node))
            return;

        NodeHierarchyComponent *hierarchy = m_nodeComponentCache[node->index].hierarchy;
        if (!hierarchy || hierarchy->enabled == enabled)
            return;

        hierarchy->enabled = enabled;
        // Only a node whose subtree contains renderable geometry changes the raster instance set
        // or the RT TLAS. Toggling a geometry-less node (UI widget, empty group, light/camera) must
        // not force UpdateRasterInstances + RebuildTLASOnly every flip — those gathers are mesh-only
        // (SceneBuffers.cpp), and lights re-gather per frame, so m_dirty alone covers them. This is
        // the cheap-visibility guard for frequent enable/disable; mesh-bearing pools still rebuild
        // (see the per-instance render-active path TODO).
        if (SubtreeHasMeshRefs(node))
        {
            m_instancesDirty = true;
            m_tlasDirty = true;
        }
        m_dirty = true;
        if (GetSkyboxNode())
            ApplySkyboxSettingsFromNode();
    }

    bool Scene::SubtreeHasMeshRefs(const NodeId *node) const
    {
        if (!node || node->index == UINT32_MAX)
            return false;

        const uint32_t idx = node->index;
        if (const NodeMeshRefsComponent *refs = m_nodeComponentCache[idx].meshRefs;
            refs && !refs->meshRefs.empty())
            return true;

        for (const NodeId *child : m_nodeComponentCache[idx].hierarchy->children)
            if (SubtreeHasMeshRefs(child))
                return true;

        return false;
    }

    void Scene::SetNodeRenderVisible(NodeId *node, bool visible)
    {
        if (!IsNodeAlive(node))
            return;

        NodeRuntime &rt = m_nodeRuntime[node->index];
        const uint32_t v = visible ? 1u : 0u;
        if (rt.gpuData.renderVisible == v)
            return;

        // Flip the per-instance cull flag and re-upload this node's NodeGpuData through the
        // existing per-frame dirtyUniforms path. CullingCS skips the draw when 0 — no
        // RebuildRasterInstances and no TLAS rebuild, unlike SetNodeEnabled.
        rt.gpuData.renderVisible = v;
        rt.dirtyUniforms = 0xFF;
    }

    bool Scene::IsNodeRenderVisible(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return false;
        return m_nodeRuntime[node->index].gpuData.renderVisible != 0u;
    }

    bool Scene::IsNodeEnabled(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return false;

        const NodeHierarchyComponent *hierarchy = m_nodeComponentCache[node->index].hierarchy;
        return !hierarchy || hierarchy->enabled;
    }

    bool Scene::IsNodeHierarchyEnabled(const NodeId *node) const
    {
        for (const NodeId *current = node; current;)
        {
            if (!IsNodeAlive(current))
                return false;

            const NodeHierarchyComponent *hierarchy = m_nodeComponentCache[current->index].hierarchy;
            if (hierarchy && !hierarchy->enabled)
                return false;
            current = hierarchy ? hierarchy->parent : nullptr;
        }
        return true;
    }

    bool Scene::IsValidMeshIndex(int meshIndex) const
    {
        return meshIndex >= 0 && meshIndex < static_cast<int>(m_meshes.size()) && m_meshes[meshIndex].live;
    }

    NodeId *Scene::CreateSkyboxNode(NodeId *parent, bool markDirty)
    {
        if (NodeId *existing = GetSkyboxNode())
            return existing;

        NodeId *node = CreateNode("Skybox", parent);
        AddComponentFlag(node, Component_Skybox);

        const auto &settings = Settings::Get<SceneSettings>();
        SetSkyboxPath(node, settings.skybox_path, markDirty);
        return node;
    }

    NodeId *Scene::GetSkyboxNode() const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
            if (m_nodeComponentCache[i].skybox)
                return m_nodeIds[i];
        return nullptr;
    }

    NodeSkyboxTag *Scene::GetSkyboxForNode(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].skybox;
    }

    void Scene::SetSkyboxPath(NodeId *node, std::string path, bool markDirty)
    {
        if (!IsNodeAlive(node))
            return;

        if (!m_nodeComponentCache[node->index].skybox)
            AddComponentFlag(node, Component_Skybox);

        NodeSkyboxTag *skybox = m_nodeComponentCache[node->index].skybox;
        if (!skybox)
            return;

        const bool changed = skybox->path != path;
        skybox->path = std::move(path);
        ApplySkyboxSettingsFromNode(node);

        if (changed && markDirty)
            MarkDirty();
    }

    void Scene::ApplySkyboxSettingsFromNode(NodeId *node)
    {
        NodeId *skyboxNode = node ? node : GetSkyboxNode();
        std::string path;

        if (skyboxNode && IsNodeAlive(skyboxNode) && IsNodeHierarchyEnabled(skyboxNode))
        {
            if (NodeSkyboxTag *skybox = m_nodeComponentCache[skyboxNode->index].skybox)
                path = skybox->path;
        }

        auto &settings = Settings::Get<SceneSettings>();
        if (settings.skybox_path == path)
            return;

        settings.skybox_path = std::move(path);
        RefreshSceneSky();
    }

    void Scene::EnsureSkyboxNodeFromSettings(bool markDirty)
    {
        if (GetSkyboxNode())
            return;

        const auto &settings = Settings::Get<SceneSettings>();
        if (settings.skybox_path.empty())
            return;

        CreateSkyboxNode(nullptr, markDirty);
    }

    float Scene::VolumeDistanceOutside(const NodeId *node, const vec3 &p, bool global) const
    {
        if (global)
            return 0.0f; // unbounded -> always inside
        // AABB from world translation +/- (world basis length * 0.5).
        // ponytail: ignores rotation; upgrade to OBB (inverse-world transform) if rotated volumes are needed.
        const mat4 &w = GetWorldMatrix(node);
        const vec3 center(w[3].x, w[3].y, w[3].z);
        const vec3 he(length(vec3(w[0].x, w[0].y, w[0].z)) * 0.5f,
                      length(vec3(w[1].x, w[1].y, w[1].z)) * 0.5f,
                      length(vec3(w[2].x, w[2].y, w[2].z)) * 0.5f);
        const NodeTriggerZoneTag *z = GetTriggerZoneForNode(node);
        if (z && z->shape == ZoneShape::Sphere)
        {
            // Sphere radius = largest half-extent so it encloses the same scale (matches the physics
            // sphere, whose radius = 0.5 local * max world scale).
            const float radius = std::max({he.x, he.y, he.z});
            return std::max(0.0f, length(p - center) - radius);
        }
        const vec3 q = abs(p - center) - he;
        return length(glm::max(q, vec3(0.0f))); // 0 inside, world-unit distance to surface outside
    }

    NodeId *Scene::CreateTriggerZoneNode(NodeId *parent, bool markDirty)
    {
        NodeId *node = CreateNode("Trigger Zone", parent);
        AddComponentFlag(node, Component_TriggerZone);
        if (markDirty)
            MarkDirty();
        return node;
    }

    NodeTriggerZoneTag *Scene::GetTriggerZoneForNode(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].triggerZone;
    }

    NodeId *Scene::CreateVoxelWorldNode(NodeId *parent, bool markDirty)
    {
        if (NodeId *existing = GetVoxelWorldNode())
            return existing; // one voxel world per scene (VoxelSystem holds a single world)
        NodeId *node = CreateNode("Voxel World", parent);
        AddComponentFlag(node, Component_VoxelWorld);
        if (markDirty)
            MarkDirty();
        return node;
    }

    NodeId *Scene::GetVoxelWorldNode() const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
            if (m_nodeComponentCache[i].voxelWorld && IsNodeAlive(m_nodeIds[i]))
                return m_nodeIds[i];
        return nullptr;
    }

    NodeVoxelWorldTag *Scene::GetVoxelWorldForNode(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].voxelWorld;
    }

    // Blend src into dst by weight t (0..1): floats lerp, ints round, bools snap at t>=0.5. Field
    // lists mirror PostProcessProfile (keep in sync with the struct / serializer / set_pp binding).
    static void BlendPostProcessInto(PostProcessProfile &dst, const PostProcessProfile &src, float t)
    {
        if (t <= 0.0f)
            return;
        if (t >= 1.0f)
        {
            dst = src;
            return;
        }
        static constexpr float PostProcessProfile::*kF[] = {
            &PostProcessProfile::ssao_radius, &PostProcessProfile::ssao_bias,
            &PostProcessProfile::ssao_intensity, &PostProcessProfile::ssao_power,
            &PostProcessProfile::cas_sharpness, &PostProcessProfile::color_grading_lift_r,
            &PostProcessProfile::color_grading_lift_g, &PostProcessProfile::color_grading_lift_b,
            &PostProcessProfile::color_grading_gamma_r, &PostProcessProfile::color_grading_gamma_g,
            &PostProcessProfile::color_grading_gamma_b, &PostProcessProfile::color_grading_gain_r,
            &PostProcessProfile::color_grading_gain_g, &PostProcessProfile::color_grading_gain_b,
            &PostProcessProfile::color_grading_saturation, &PostProcessProfile::color_grading_contrast,
            &PostProcessProfile::color_grading_intensity, &PostProcessProfile::dof_focus_scale,
            &PostProcessProfile::dof_blur_range, &PostProcessProfile::bloom_strength,
            &PostProcessProfile::bloom_range, &PostProcessProfile::motion_blur_strength,
            &PostProcessProfile::IBL_intensity};
        for (auto m : kF)
            dst.*m = dst.*m + (src.*m - dst.*m) * t;
        static constexpr int PostProcessProfile::*kI[] = {&PostProcessProfile::ssao_samples,
                                                          &PostProcessProfile::motion_blur_samples};
        for (auto m : kI)
            dst.*m = static_cast<int>(std::lround(dst.*m + (src.*m - dst.*m) * t));
        // Bools mid-blend: ON if EITHER side wants it (the t>=1 early-return already snapped to the
        // target). So an effect off->on turns on at the START of the blend (and its params ramp in),
        // while on->off stays on through the blend and only cuts off once fully blended (at the end).
        static constexpr bool PostProcessProfile::*kB[] = {
            &PostProcessProfile::ssao, &PostProcessProfile::fxaa,
            &PostProcessProfile::taa, &PostProcessProfile::cas_sharpening,
            &PostProcessProfile::ssr, &PostProcessProfile::tonemapping,
            &PostProcessProfile::color_grading, &PostProcessProfile::dof,
            &PostProcessProfile::bloom, &PostProcessProfile::motion_blur,
            &PostProcessProfile::IBL};
        for (auto m : kB)
            dst.*m = dst.*m || src.*m;
    }

    // Per-effect blend factor (0..1): how visible each effect is this frame. Seeded from the base
    // profile's on/off, then lerped toward each volume's on/off by its weight — so off->on fades in
    // from 0 and on->off fades out to 0 (passes pass this to their shaders to avoid the on/off snap).
    static void SeedBlendFactors(PostProcessBlend &b, const PostProcessProfile &p)
    {
        b.ssao = p.ssao ? 1.0f : 0.0f;
        b.fxaa = p.fxaa ? 1.0f : 0.0f;
        b.taa = p.taa ? 1.0f : 0.0f;
        b.cas_sharpening = p.cas_sharpening ? 1.0f : 0.0f;
        b.ssr = p.ssr ? 1.0f : 0.0f;
        b.tonemapping = p.tonemapping ? 1.0f : 0.0f;
        b.color_grading = p.color_grading ? 1.0f : 0.0f;
        b.dof = p.dof ? 1.0f : 0.0f;
        b.bloom = p.bloom ? 1.0f : 0.0f;
        b.motion_blur = p.motion_blur ? 1.0f : 0.0f;
        b.IBL = p.IBL ? 1.0f : 0.0f;
    }

    static void BlendFactorsToward(PostProcessBlend &b, const PostProcessProfile &target, float t)
    {
        auto L = [t](float &f, bool on)
        { f += ((on ? 1.0f : 0.0f) - f) * t; };
        L(b.ssao, target.ssao);
        L(b.fxaa, target.fxaa);
        L(b.taa, target.taa);
        L(b.cas_sharpening, target.cas_sharpening);
        L(b.ssr, target.ssr);
        L(b.tonemapping, target.tonemapping);
        L(b.color_grading, target.color_grading);
        L(b.dof, target.dof);
        L(b.bloom, target.bloom);
        L(b.motion_blur, target.motion_blur);
        L(b.IBL, target.IBL);
    }

    PostProcessProfile *Scene::ResolvePostProcessProfile(const vec3 &cameraPos)
    {
        // Gather applicable volumes with a blend weight, then composite low->high priority over the
        // scene default. Returns null when nothing applies (caller falls back to the scene default).
        struct Layer
        {
            float priority;
            float weight;
            const PostProcessProfile *profile;
        };
        std::vector<Layer> layers; // ponytail: a handful of zones per scene; plain vector is fine
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
        {
            NodeTriggerZoneTag *z = m_nodeComponentCache[i].triggerZone;
            if (!z || !z->postProcessEnabled)
                continue;
            NodeId *node = m_nodeIds[i];
            if (!IsNodeAlive(node) || !IsNodeHierarchyEnabled(node))
                continue;
            float weight = std::clamp(z->blend, 0.0f, 1.0f);
            // Real world-unit distance from the camera to the box surface: 0 inside, >0 outside.
            const float distOutside = VolumeDistanceOutside(node, cameraPos, false);
            if (z->blend_distance > 0.0f)
            {
                // Full effect everywhere inside the box; fade to 0 over blend_distance WORLD UNITS
                // outside the surface — so blend_distance is a real metres-from-the-zone distance
                // and the effect reaches full exactly at the box wall.
                if (distOutside >= z->blend_distance)
                    continue; // beyond the fade band
                weight *= 1.0f - distOutside / z->blend_distance;
            }
            else if (distOutside > 0.0f)
            {
                continue; // hard edge: only inside the box
            }
            if (weight <= 0.0f)
                continue;
            layers.push_back({z->priority, weight, &z->postProcess});
        }
        if (layers.empty())
        {
            // No volume in effect: passes run per their own bools and show full strength.
            SetActivePostProcessBlend(PostProcessBlend{});
            return nullptr;
        }

        std::stable_sort(layers.begin(), layers.end(),
                         [](const Layer &a, const Layer &b)
                         { return a.priority < b.priority; });
        // Base to blend over: the scene default when the Scene Settings node is active, otherwise the
        // all-off profile — so a volume turns its own effects ON even with no/disabled Scene Settings.
        m_resolvedPostProcessProfile = SceneSettingsActive()
                                           ? static_cast<const PostProcessProfile &>(Settings::Get<SceneSettings>())
                                           : DisabledPostProcessProfile();
        PostProcessBlend blend;
        SeedBlendFactors(blend, m_resolvedPostProcessProfile);
        for (const Layer &l : layers)
        {
            BlendFactorsToward(blend, *l.profile, l.weight);
            BlendPostProcessInto(m_resolvedPostProcessProfile, *l.profile, l.weight);
        }
        SetActivePostProcessBlend(blend);
        return &m_resolvedPostProcessProfile;
    }

    NodeId *Scene::GetSceneSettingsNode() const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
            if (m_nodeComponentCache[i].sceneSettings)
                return m_nodeIds[i];
        return nullptr;
    }

    void Scene::EnsureSceneSettingsNodeFromSettings(bool markDirty)
    {
        if (GetSceneSettingsNode())
            return;

        NodeId *node = CreateNode("Scene Settings");
        AddComponentFlag(node, Component_SceneSettings);
        if (markDirty)
            MarkDirty();
    }

    void Scene::SetMeshRef(NodeId *node, int meshIndex)
    {
        auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        bool changed = !refs.empty() || meshIndex >= 0;
        refs.clear();
        if (IsValidMeshIndex(meshIndex))
            refs.push_back(meshIndex);
        m_nodeRuntime[node->index].hasUniformData =
            IsValidMeshIndex(meshIndex) && m_meshes[meshIndex].indexCount > 0;
        if (changed)
        {
            m_instancesDirty = true;
            m_tlasDirty = true;
            MarkNodeDirty(node);
        }
    }

    void Scene::AddMeshRef(NodeId *node, int meshIndex)
    {
        if (!IsValidMeshIndex(meshIndex))
            return;
        m_nodeComponentCache[node->index].meshRefs->meshRefs.push_back(meshIndex);
        if (m_meshes[meshIndex].indexCount > 0)
            m_nodeRuntime[node->index].hasUniformData = true;
        m_instancesDirty = true;
        m_tlasDirty = true;
        MarkNodeDirty(node);
    }

    void Scene::RemoveMeshRef(NodeId *node, int meshIndex)
    {
        auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        auto it = std::remove(refs.begin(), refs.end(), meshIndex);
        if (it != refs.end())
        {
            refs.erase(it, refs.end());
            // Recompute drawable flag after removal
            bool hasDrawable = false;
            for (int mr : refs)
                if (mr >= 0 && m_meshes[mr].indexCount > 0)
                {
                    hasDrawable = true;
                    break;
                }
            m_nodeRuntime[node->index].hasUniformData = hasDrawable;
            m_instancesDirty = true;
            m_tlasDirty = true;
            MarkNodeDirty(node);
        }
    }

    static std::string NormalizeNodeScriptPath(const std::string &path)
    {
        if (path.empty())
            return path;

        std::filesystem::path p(path);
        if (p.is_relative())
            return p.generic_string(); // already relative; just normalize separators

        std::filesystem::path projectRoot(Path::Root);
        if (!Path::Assets.empty())
        {
            std::filesystem::path assets = std::filesystem::path(Path::Assets).lexically_normal();
            if (assets.filename().empty()) // trailing slash leaves an empty leaf component
                assets = assets.parent_path();
            if (assets.has_parent_path())
                projectRoot = assets.parent_path();
        }

        const std::string rel =
            p.lexically_normal().lexically_relative(projectRoot.lexically_normal()).generic_string();
        if (rel.empty() || rel.compare(0, 2, "..") == 0)
            return p.lexically_normal().generic_string(); // outside the project: keep absolute
        return rel;
    }

    void Scene::SetNodeScript(NodeId *node, const std::string &path)
    {
        if (!node || node->index >= (int)m_nodeComponentCache.size())
        {
            PE_WARN("[Scene] SetNodeScript: invalid node index %d", node ? node->index : -1);
            return;
        }
        auto &cache = m_nodeComponentCache[node->index];
        if (!cache.script)
        {
            PE_WARN("[Scene] SetNodeScript: node %d has no script component", node->index);
            return;
        }
        cache.script->path = NormalizeNodeScriptPath(path);
    }

    void Scene::SetNodePrefabPath(NodeId *node, const std::string &path, bool markDirty)
    {
        if (!IsNodeAlive(node))
            return;

        if (path.empty())
        {
            ClearNodePrefab(node);
            return;
        }

        const std::string normalizedPath = std::filesystem::path(path).generic_string();
        AddComponentFlag(node, Component_Prefab);
        if (NodePrefabComponent *prefab = m_nodeComponentCache[node->index].prefab)
        {
            if (prefab->path == normalizedPath)
                return;

            prefab->path = normalizedPath;
            if (markDirty)
                m_dirty = true;
        }
    }

    void Scene::ClearNodePrefab(NodeId *node)
    {
        if (!IsNodeAlive(node))
            return;

        if (m_nodeComponentCache[node->index].prefab)
        {
            RemoveComponentFlag(node, Component_Prefab);
            m_dirty = true;
        }
    }

    const std::string &Scene::GetNodePrefabPath(const NodeId *node) const
    {
        static const std::string empty;
        if (!IsNodeAlive(node))
            return empty;

        const NodePrefabComponent *prefab = m_nodeComponentCache[node->index].prefab;
        return prefab ? prefab->path : empty;
    }

    NodeRuntimeUiTag *Scene::GetRuntimeUiComponent(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].runtimeUi;
    }

    const NodeRuntimeUiTag *Scene::GetRuntimeUiComponent(const NodeId *node) const
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].runtimeUi;
    }

    NodeRuntimeUiTag &Scene::GetOrCreateRuntimeUiComponent(NodeId *node)
    {
        ValidateNodeId(node);
        PE_ERROR_IF(!node->entity, "Scene::GetOrCreateRuntimeUiComponent requires a node entity");
        NodeRuntimeUiTag *&runtimeUi = m_nodeComponentCache[node->index].runtimeUi;
        if (!runtimeUi)
        {
            AddComponentFlag(node, Component_RuntimeUi);
            runtimeUi = m_nodeComponentCache[node->index].runtimeUi;
        }
        return *runtimeUi;
    }

    void Scene::ClearRuntimeUiComponent(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return;
        RemoveComponentFlag(node, Component_RuntimeUi);
        m_dirty = true;
    }

    NodeSpriteComponent *Scene::GetSpriteComponent(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].sprite;
    }

    const NodeSpriteComponent *Scene::GetSpriteComponent(const NodeId *node) const
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].sprite;
    }

    NodeSpriteComponent &Scene::GetOrCreateSpriteComponent(NodeId *node)
    {
        ValidateNodeId(node);
        PE_ERROR_IF(!node->entity, "Scene::GetOrCreateSpriteComponent requires a node entity");
        NodeSpriteComponent *&sprite = m_nodeComponentCache[node->index].sprite;
        if (!sprite)
        {
            AddComponentFlag(node, Component_Sprite);
            sprite = m_nodeComponentCache[node->index].sprite;
        }
        return *sprite;
    }

    void Scene::ClearSpriteComponent(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return;
        RemoveComponentFlag(node, Component_Sprite);
        m_dirty = true;
    }

    NodeSkinnedStrip2DComponent *Scene::GetSkinnedStrip2DState(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].skinnedStrip2D;
    }

    const NodeSkinnedStrip2DComponent *Scene::GetSkinnedStrip2DState(const NodeId *node) const
    {
        if (!node || !IsNodeAlive(node))
            return nullptr;
        return m_nodeComponentCache[node->index].skinnedStrip2D;
    }

    NodeSkinnedStrip2DComponent &Scene::GetOrCreateSkinnedStrip2DState(NodeId *node)
    {
        ValidateNodeId(node);
        PE_ERROR_IF(!node->entity, "Scene::GetOrCreateSkinnedStrip2DState requires a node entity");
        NodeSkinnedStrip2DComponent *&state = m_nodeComponentCache[node->index].skinnedStrip2D;
        if (!state)
            state = node->entity->CreateComponent<NodeSkinnedStrip2DComponent>();
        return *state;
    }

    void Scene::ClearSkinnedStrip2DState(NodeId *node)
    {
        if (!node || !IsNodeAlive(node))
            return;

        NodeSkinnedStrip2DComponent *&state = m_nodeComponentCache[node->index].skinnedStrip2D;
        if (!state || !node->entity)
            return;

        node->entity->RemoveComponent<NodeSkinnedStrip2DComponent>();
        state = nullptr;
        m_dirty = true;
    }

    void Scene::AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel)
    {
        const bool keepModel = primitiveModel->HasSkeleton() || primitiveModel->HasAnimations();

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = primitiveModel->GetFilePath();
        source.primitiveType = primitiveModel->GetPrimitiveType();
        source.primitiveParams = primitiveModel->GetPrimitiveParams();
        source.primitiveParamCount = primitiveModel->GetPrimitiveParamCount();
        if (keepModel)
            source.modelId = primitiveModel->GetId();
        m_sources.push_back(std::move(source));

        std::vector<int> meshMap = AddModelGeometry(primitiveModel, sourceIndex);
        if (!meshMap.empty() && meshMap[0] >= 0)
            AddMeshRef(node, meshMap[0]);

        if (keepModel)
        {
            m_models.insert(primitiveModel->GetId(), primitiveModel);
            m_modelRootNodes[primitiveModel->GetId()].push_back(node);
            ResetSkeletonCache();
        }
        else
        {
            // Transfer material ownership from the temporary ModelAsset to the scene
            // before deleting it — mesh.material holds a raw pointer into these.
            for (auto &mat : primitiveModel->GetOwnedMaterials())
                m_ownedMaterials.push_back(std::move(mat));
            delete primitiveModel;
        }

        MarkNodeDirty(node);
    }

    bool Scene::ApplyMeshUvRect(int meshIndex, const vec4 &uvRect, bool markGeometryDirty, bool uploadGpu)
    {
        if (!IsValidMeshIndex(meshIndex))
            return false;

        Mesh &mesh = m_meshes[meshIndex];
        if (mesh.vertexCount == 0)
            return false;

        if (mesh.vertexOffset + mesh.vertexCount > m_vertexStore.size() ||
            mesh.positionsOffset + mesh.vertexCount > m_positionUvStore.size())
            return false;

        if (mesh.vertexCount != 4)
            return false;

        const vec2 uvs[4] = {
            vec2(uvRect.x, uvRect.y),
            vec2(uvRect.x, uvRect.w),
            vec2(uvRect.z, uvRect.w),
            vec2(uvRect.z, uvRect.y),
        };

        for (uint32_t i = 0; i < 4; i++)
        {
            Vertex &vertex = m_vertexStore[mesh.vertexOffset + i];
            PositionUvVertex &positionUv = m_positionUvStore[mesh.positionsOffset + i];
            FillVertexUV(vertex, uvs[i].x, uvs[i].y);
            FillVertexUV(positionUv, uvs[i].x, uvs[i].y);
        }

        if (uploadGpu && m_buffer && !m_geometryDirty)
        {
            Queue *queue = RHII.GetMainQueue();
            const size_t vertexBytes = mesh.vertexCount * sizeof(Vertex);
            const size_t positionUvBytes = mesh.vertexCount * sizeof(PositionUvVertex);
            const size_t vertexDstOffset = m_verticesOffset + mesh.vertexOffset * sizeof(Vertex);
            const size_t positionUvDstOffset = m_positionsOffset + mesh.positionsOffset * sizeof(PositionUvVertex);
            const bool rangesFit = vertexDstOffset + vertexBytes <= m_buffer->Size() &&
                                   positionUvDstOffset + positionUvBytes <= m_buffer->Size();
            if (queue && rangesFit)
            {
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                cmd->CopyBufferStaged(m_buffer, &m_vertexStore[mesh.vertexOffset], vertexBytes, vertexDstOffset);
                cmd->CopyBufferStaged(m_buffer, &m_positionUvStore[mesh.positionsOffset], positionUvBytes, positionUvDstOffset);

                BufferBarrierInfo vertexBarrier{};
                vertexBarrier.buffer = m_buffer;
                vertexBarrier.stageMask = PE_STAGE_VERTEX_INPUT;
                vertexBarrier.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
                vertexBarrier.offset = vertexDstOffset;
                vertexBarrier.size = vertexBytes;
                cmd->BufferBarrier(vertexBarrier);

                BufferBarrierInfo positionUvBarrier{};
                positionUvBarrier.buffer = m_buffer;
                positionUvBarrier.stageMask = PE_STAGE_VERTEX_INPUT;
                positionUvBarrier.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
                positionUvBarrier.offset = positionUvDstOffset;
                positionUvBarrier.size = positionUvBytes;
                cmd->BufferBarrier(positionUvBarrier);

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
                return true;
            }
        }

        if (markGeometryDirty || uploadGpu)
            m_geometryDirty = true;
        return true;
    }

    bool Scene::SetMeshUvRect(int meshIndex, const vec4 &uvRect)
    {
        return ApplyMeshUvRect(meshIndex, uvRect, true, false);
    }

    bool Scene::SetMeshUvRectTransient(int meshIndex, const vec4 &uvRect)
    {
        return ApplyMeshUvRect(meshIndex, uvRect, false, true);
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
        rt.dirtyUniforms = 0xFF;

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

        // Update world AABB — union of all mesh bounding boxes
        const auto &refs = m_nodeComponentCache[idx].meshRefs->meshRefs;
        bool aabbInit = false;
        for (int meshIdx : refs)
        {
            if (meshIdx < 0)
                continue;
            AABB meshAABB = TransformAabb(m_meshes[meshIdx].boundingBox, rt.gpuData.worldMatrix);
            if (!aabbInit)
            {
                rt.worldAABB = meshAABB;
                aabbInit = true;
            }
            else
            {
                rt.worldAABB.min = min(rt.worldAABB.min, meshAABB.min);
                rt.worldAABB.max = max(rt.worldAABB.max, meshAABB.max);
            }
        }

        rt.dirty = false;

        // Mark uniforms dirty for all frames
        rt.dirtyUniforms = 0xFF;

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

    void Scene::RetireAllNodeIds()
    {
        auto retire = [this](NodeId *id)
        {
            if (!id)
                return;
            id->entity = nullptr;
            id->index = UINT32_MAX;
            id->revision++;
            m_retiredNodeIds.push_back(id);
        };

        for (NodeId *id : m_nodeIds)
            retire(id);
        for (NodeId *id : m_freeNodeIds)
            retire(id);

        m_nodeIds.clear();
        m_freeNodeIds.clear();
    }
} // namespace pe
