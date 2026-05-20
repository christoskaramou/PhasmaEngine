#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Camera/Camera.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        constexpr float kSpriteEmissiveBoost = 4.0f;

        mat4 SetSpriteScaleOnMatrix(const mat4 &m, const vec2 &size)
        {
            vec3 pos(m[3]);
            vec3 oldScale(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
            oldScale.x = oldScale.x == 0.0f ? 1.0f : oldScale.x;
            oldScale.y = oldScale.y == 0.0f ? 1.0f : oldScale.y;
            oldScale.z = oldScale.z == 0.0f ? 1.0f : oldScale.z;
            mat3 rotMat(vec3(m[0]) / oldScale.x, vec3(m[1]) / oldScale.y, vec3(m[2]) / oldScale.z);
            return glm::translate(mat4(1.0f), pos) * glm::mat4_cast(glm::quat_cast(rotMat)) *
                   glm::scale(mat4(1.0f), vec3(size, oldScale.z));
        }

        vec4 FrameToUvRect(int frame, int columns, int rows)
        {
            columns = std::max(columns, 1);
            rows = std::max(rows, 1);
            const int frameCount = columns * rows;
            frame = frameCount > 0 ? std::clamp(frame, 0, frameCount - 1) : 0;
            const int column = frame % columns;
            const int row = frame / columns;
            const float invColumns = 1.0f / static_cast<float>(columns);
            const float invRows = 1.0f / static_cast<float>(rows);
            return vec4(column * invColumns,
                        row * invRows,
                        (column + 1) * invColumns,
                        (row + 1) * invRows);
        }

        struct SpriteNdcFrame
        {
            vec3 right = vec3(1.0f, 0.0f, 0.0f);
            vec3 up = vec3(0.0f, 1.0f, 0.0f);
            vec3 front = vec3(0.0f, 0.0f, -1.0f);
            float halfWidth = 1.0f;
            float halfHeight = 1.0f;
            float depth = 1.0f;
        };

        bool BuildSpriteNdcFrame(Camera *camera, float depth, SpriteNdcFrame &frame)
        {
            if (!camera)
                return false;

            if (glm::dot(camera->GetRight(), camera->GetRight()) < 1e-8f ||
                glm::dot(camera->GetUp(), camera->GetUp()) < 1e-8f ||
                glm::dot(camera->GetFront(), camera->GetFront()) < 1e-8f)
            {
                camera->Update();
            }

            frame.right = -glm::normalize(camera->GetRight());
            frame.up = -glm::normalize(camera->GetUp());
            frame.front = glm::normalize(camera->GetFront());
            frame.depth = std::max(depth, camera->GetNearPlane() + 0.001f);

            const float aspect = std::max(camera->GetAspect(), 0.001f);
            if (camera->IsOrthographic())
            {
                frame.halfHeight = std::max(camera->GetOrthographicSize() * 0.5f, 0.001f);
            }
            else
            {
                frame.halfHeight = std::max(std::tan(camera->Fovy() * 0.5f) * frame.depth, 0.001f);
            }
            frame.halfWidth = std::max(frame.halfHeight * aspect, 0.001f);
            return true;
        }

        bool BuildSpriteNdcWorldMatrix(Camera *camera, const NodeSpriteTag &sprite, mat4 &worldMatrix)
        {
            SpriteNdcFrame frame;
            if (!BuildSpriteNdcFrame(camera, sprite.ndcDepth, frame))
                return false;

            const vec2 size(std::max(sprite.ndcSize.x, 0.001f),
                            std::max(sprite.ndcSize.y, 0.001f));
            const float c = std::cos(sprite.ndcRotation);
            const float s = std::sin(sprite.ndcRotation);
            const vec3 xAxis = frame.right * c + frame.up * s;
            const vec3 yAxis = frame.up * c - frame.right * s;
            const vec3 center = camera->GetPosition() +
                                frame.front * frame.depth +
                                frame.right * (sprite.ndcPosition.x * frame.halfWidth) +
                                frame.up * (sprite.ndcPosition.y * frame.halfHeight);

            worldMatrix = mat4(1.0f);
            worldMatrix[0] = vec4(xAxis * (size.x * frame.halfWidth), 0.0f);
            worldMatrix[1] = vec4(yAxis * (size.y * frame.halfHeight), 0.0f);
            worldMatrix[2] = vec4(-frame.front, 0.0f);
            worldMatrix[3] = vec4(center, 1.0f);
            return true;
        }

        bool CaptureSpriteNdcFromWorld(Camera *camera, const mat4 &worldMatrix, NodeSpriteTag &sprite)
        {
            if (!camera)
                return false;

            const vec3 worldPosition(worldMatrix[3]);
            const vec4 clip = camera->GetProjectionNoJitter() * camera->GetView() * vec4(worldPosition, 1.0f);
            if (std::abs(clip.w) > 1e-6f)
                sprite.ndcPosition = vec2(clip.x, clip.y) / clip.w;

            float depth = glm::dot(worldPosition - camera->GetPosition(), camera->GetFront());
            if (depth <= camera->GetNearPlane())
                depth = sprite.ndcDepth > camera->GetNearPlane() ? sprite.ndcDepth : 1.0f;

            SpriteNdcFrame frame;
            if (!BuildSpriteNdcFrame(camera, depth, frame))
                return false;

            sprite.ndcDepth = frame.depth;
            sprite.ndcSize = vec2(std::max(glm::length(vec3(worldMatrix[0])) / frame.halfWidth, 0.001f),
                                  std::max(glm::length(vec3(worldMatrix[1])) / frame.halfHeight, 0.001f));
            const vec3 xColumn(worldMatrix[0]);
            if (glm::dot(xColumn, xColumn) > 1e-8f)
            {
                const vec3 xAxis = glm::normalize(xColumn);
                sprite.ndcRotation = std::atan2(glm::dot(xAxis, frame.up), glm::dot(xAxis, frame.right));
            }
            return true;
        }

        void ApplySpriteNdcMatrix(Scene &scene, NodeId *node, Camera *camera, const NodeSpriteTag &sprite)
        {
            if (!scene.IsNodeAlive(node))
                return;

            mat4 worldMatrix;
            if (!BuildSpriteNdcWorldMatrix(camera, sprite, worldMatrix))
                return;

            mat4 localMatrix = worldMatrix;
            if (NodeId *parent = scene.GetParent(node))
            {
                const mat4 &parentWorld = scene.GetWorldMatrix(parent);
                if (std::abs(glm::determinant(parentWorld)) > 1e-6f)
                    localMatrix = glm::inverse(parentWorld) * worldMatrix;
            }

            scene.SetLocalMatrix(node, localMatrix, false);
            scene.MarkNodeDirty(node);
        }
    } // namespace

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
        if ((flag & Component_Sprite) && !c.sprite)
            c.sprite = entity->CreateComponent<NodeSpriteTag>();
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
        if ((flag & Component_Sprite) && c.sprite)
        {
            entity->RemoveComponent<NodeSpriteTag>();
            c.sprite = nullptr;
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

        m_nodeComponentCache.push_back({nameComp, hierarchyComp, transformComp, meshRefsComp, scriptComp,
                                        nullptr, nullptr, nullptr, nullptr, nullptr});

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
        if (cache.audio)
            RemoveSceneAudioSource(node);
        RemoveSceneAnimation(node);

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

        SwapAndPopNode(idx);
        node->revision++; // Invalidate all old IDs pointing to this deleted node
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

            // Update the swapped node's identity and bump revision to invalidate old IDs
            m_nodeIds[index]->index = index;
            m_nodeIds[index]->revision++;
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
        m_nodeComponentCache[node->index].transform->localMatrix = m;
        if (markDirty)
        {
            NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
            if (sprite && sprite->coordinateSpace == SpriteCoordinateSpace::NDC && !m_cameras.empty())
            {
                mat4 worldMatrix = m;
                if (NodeId *parent = GetParent(node))
                    worldMatrix = GetWorldMatrix(parent) * m;
                CaptureSpriteNdcFromWorld(m_cameras[0], worldMatrix, *sprite);
            }
            MarkNodeDirty(node);
        }
    }

    bool Scene::IsValidMeshIndex(int meshIndex) const
    {
        return meshIndex >= 0 && meshIndex < static_cast<int>(m_meshes.size()) && m_meshes[meshIndex].live;
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
        cache.script->path = path;
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
            AddMeshRef(node, meshMap[0]);

        // Transfer material ownership from the temporary ModelAsset to the scene
        // before deleting it — mesh.material holds a raw pointer into these.
        for (auto &mat : primitiveModel->GetOwnedMaterials())
            m_ownedMaterials.push_back(std::move(mat));

        MarkNodeDirty(node);
        delete primitiveModel;
    }

    SceneNodeHandle Scene::CreateSpriteNode(const std::string &name,
                                            const vec2 &size,
                                            const vec3 &position,
                                            RenderType renderType,
                                            const vec4 &tint,
                                            NodeId *parent)
    {
        ModelAsset *model = Primitives::CreateQuad(1.0f, 1.0f);
        model->SetLabel(name.empty() ? "Sprite" : name);
        model->SetNodeName(model->GetRootNodeIndex(), name.empty() ? "Sprite" : name);

        SceneNodeHandle handle = AddModelDeferred(model);
        if (!handle.IsValid(*this))
            return handle;

        if (parent)
            ReparentNode(handle.nodeId, parent);

        SetLocalMatrix(handle.nodeId,
                       glm::translate(mat4(1.0f), position) *
                           glm::scale(mat4(1.0f), vec3(size, 1.0f)));
        ConfigureSpriteNode(handle.nodeId, size, renderType, tint);
        return handle;
    }

    void Scene::AttachSpriteToNode(NodeId *node, const vec2 &size, RenderType renderType, const vec4 &tint)
    {
        if (!IsNodeAlive(node))
            return;

        ModelAsset *model = Primitives::CreateQuad(1.0f, 1.0f);
        model->SetLabel("Sprite");
        model->SetNodeName(model->GetRootNodeIndex(), "Sprite");
        AttachPrimitiveToNode(node, model);
        AddComponentFlag(node, Component_Sprite);
        SetSpriteSize(node, size);

        const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        if (!refs.empty())
        {
            const int meshIdx = refs.back();
            if (IsValidMeshIndex(meshIdx))
            {
                Mesh &mesh = GetMesh(meshIdx);
                mesh.renderType = renderType;
                if (mesh.material)
                {
                    MaterialInstance *inst = CreateMaterialInstance(mesh);
                    if (inst)
                    {
                        inst->SetRenderType(renderType);
                        inst->SetBaseColorFactor(tint);
                        inst->SetEmissiveFactor(vec3(tint) * kSpriteEmissiveBoost);
                    }
                }
            }
        }

        m_geometryDirty = true;
        m_materialDirty = true;
        MarkNodeDirty(node);
    }

    void Scene::ConfigureSpriteNode(NodeId *node, const vec2 &size, RenderType renderType, const vec4 &tint)
    {
        if (!IsNodeAlive(node))
            return;

        AddComponentFlag(node, Component_Sprite);
        SetSpriteSize(node, size);
        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (sprite)
        {
            sprite->size = size;
            sprite->uvRect = vec4(0.0f, 0.0f, 1.0f, 1.0f);
            sprite->atlasColumns = 1;
            sprite->atlasRows = 1;
            sprite->frame = 0;
        }

        const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        for (int meshIdx : refs)
        {
            if (!IsValidMeshIndex(meshIdx))
                continue;

            Mesh &mesh = GetMesh(meshIdx);
            mesh.renderType = renderType;
            if (!mesh.material)
                continue;

            MaterialInstance *inst = CreateMaterialInstance(mesh);
            if (!inst)
                continue;

            inst->SetRenderType(renderType);
            inst->SetBaseColorFactor(tint);
            inst->SetEmissiveFactor(vec3(tint) * kSpriteEmissiveBoost);
        }

        m_geometryDirty = true;
        m_materialDirty = true;
        MarkNodeDirty(node);
    }

    void Scene::SetSpriteSize(NodeId *node, const vec2 &size)
    {
        if (!IsNodeAlive(node))
            return;

        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (sprite && sprite->coordinateSpace == SpriteCoordinateSpace::NDC)
        {
            SetSpriteNdcTransform(node, sprite->ndcPosition, size, sprite->ndcDepth, sprite->ndcRotation);
            return;
        }

        SetLocalMatrix(node, SetSpriteScaleOnMatrix(GetLocalMatrix(node), size));
        if (sprite)
            sprite->size = size;
    }

    void Scene::SetSpriteCoordinateSpace(NodeId *node, SpriteCoordinateSpace space)
    {
        if (!IsNodeAlive(node))
            return;

        AddComponentFlag(node, Component_Sprite);
        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (!sprite)
            return;

        if (space == SpriteCoordinateSpace::NDC && sprite->coordinateSpace != SpriteCoordinateSpace::NDC && !m_cameras.empty())
        {
            mat4 worldMatrix = GetLocalMatrix(node);
            if (NodeId *parent = GetParent(node))
                worldMatrix = GetWorldMatrix(parent) * worldMatrix;
            CaptureSpriteNdcFromWorld(m_cameras[0], worldMatrix, *sprite);
        }

        sprite->coordinateSpace = space;
        if (space == SpriteCoordinateSpace::NDC && !m_cameras.empty())
            ApplySpriteNdcMatrix(*this, node, m_cameras[0], *sprite);
        else
            MarkNodeDirty(node);
    }

    void Scene::SetSpriteNdcTransform(NodeId *node, const vec2 &position, const vec2 &size, float depth, float rotation)
    {
        if (!IsNodeAlive(node))
            return;

        AddComponentFlag(node, Component_Sprite);
        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (!sprite)
            return;

        sprite->coordinateSpace = SpriteCoordinateSpace::NDC;
        sprite->ndcPosition = position;
        sprite->ndcSize = vec2(std::max(size.x, 0.001f), std::max(size.y, 0.001f));
        sprite->ndcDepth = std::max(depth, 0.001f);
        sprite->ndcRotation = rotation;

        if (!m_cameras.empty())
            ApplySpriteNdcMatrix(*this, node, m_cameras[0], *sprite);
        else
            MarkNodeDirty(node);
    }

    bool Scene::SetMeshUvRect(int meshIndex, const vec4 &uvRect)
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

        m_geometryDirty = true;
        return true;
    }

    bool Scene::SetSpriteUvRect(NodeId *node, const vec4 &uvRect, int meshSlot)
    {
        if (!IsNodeAlive(node))
            return false;

        const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
            return false;

        if (!SetMeshUvRect(refs[meshSlot], uvRect))
            return false;

        AddComponentFlag(node, Component_Sprite);
        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (sprite)
            sprite->uvRect = uvRect;
        MarkNodeDirty(node);
        return true;
    }

    bool Scene::SetSpriteFrame(NodeId *node, int frame, int columns, int rows, int meshSlot)
    {
        if (!IsNodeAlive(node))
            return false;

        columns = std::max(columns, 1);
        rows = std::max(rows, 1);
        const int frameCount = columns * rows;
        frame = frameCount > 0 ? std::clamp(frame, 0, frameCount - 1) : 0;
        const vec4 uvRect = FrameToUvRect(frame, columns, rows);
        if (!SetSpriteUvRect(node, uvRect, meshSlot))
            return false;

        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (sprite)
        {
            sprite->atlasColumns = columns;
            sprite->atlasRows = rows;
            sprite->frame = frame;
        }
        return true;
    }

    void Scene::SetSpriteAnimation(NodeId *node,
                                   int columns,
                                   int rows,
                                   float fps,
                                   const std::vector<int> &frames,
                                   bool loop)
    {
        if (!IsNodeAlive(node))
            return;

        AddComponentFlag(node, Component_Sprite);
        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (!sprite)
            return;

        sprite->atlasColumns = std::max(columns, 1);
        sprite->atlasRows = std::max(rows, 1);
        sprite->animationFps = std::max(fps, 0.0f);
        sprite->animationFrames = frames;
        sprite->animationLoop = loop;
        sprite->animationPlaying = sprite->animationFps > 0.0f;
        sprite->animationTime = 0.0f;

        const int firstFrame = !sprite->animationFrames.empty() ? sprite->animationFrames.front() : 0;
        SetSpriteFrame(node, firstFrame, sprite->atlasColumns, sprite->atlasRows);
    }

    void Scene::StopSpriteAnimation(NodeId *node)
    {
        if (!IsNodeAlive(node))
            return;

        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (sprite)
            sprite->animationPlaying = false;
    }

    bool Scene::UpdateSpriteAnimation(NodeId *node, float dt)
    {
        if (!IsNodeAlive(node))
            return false;

        NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
        if (!sprite || !sprite->animationPlaying || sprite->animationFps <= 0.0f)
            return false;

        const int frameCount = !sprite->animationFrames.empty()
                                   ? static_cast<int>(sprite->animationFrames.size())
                                   : std::max(sprite->atlasColumns * sprite->atlasRows, 1);
        if (frameCount <= 0)
            return false;

        sprite->animationTime += std::max(dt, 0.0f);
        int animationIndex = static_cast<int>(sprite->animationTime * sprite->animationFps);
        if (sprite->animationLoop)
            animationIndex %= frameCount;
        else if (animationIndex >= frameCount)
        {
            animationIndex = frameCount - 1;
            sprite->animationPlaying = false;
        }

        const int frame = !sprite->animationFrames.empty() ? sprite->animationFrames[animationIndex] : animationIndex;
        if (frame == sprite->frame)
            return false;

        return SetSpriteFrame(node, frame, sprite->atlasColumns, sprite->atlasRows);
    }

    void Scene::UpdateSpriteAnimations(float dt)
    {
        for (NodeId *node : m_nodeIds)
        {
            if (!IsNodeAlive(node))
                continue;
            UpdateSpriteAnimation(node, dt);
        }
    }

    void Scene::UpdateSpriteNdcTransforms()
    {
        if (m_cameras.empty())
            return;

        Camera *camera = m_cameras[0];
        for (NodeId *node : m_nodeIds)
        {
            if (!IsNodeAlive(node))
                continue;

            NodeSpriteTag *sprite = m_nodeComponentCache[node->index].sprite;
            if (!sprite || sprite->coordinateSpace != SpriteCoordinateSpace::NDC)
                continue;

            ApplySpriteNdcMatrix(*this, node, camera, *sprite);
        }
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
