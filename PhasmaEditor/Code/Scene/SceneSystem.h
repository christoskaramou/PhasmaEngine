#pragma once

#include "Scene/SceneGraph.h"

namespace pe
{
    class SceneNodeComponent;

    class SceneSystem : public ISystem
    {
    public:
        SceneSystem() = default;
        ~SceneSystem() override = default;

        SceneNodeHandle RegisterComponent(SceneNodeComponent *component, SceneNodeHandle parent = InvalidSceneNode);
        void UnregisterComponent(SceneNodeComponent *component);

        void SetParent(SceneNodeHandle node, SceneNodeHandle parent);
        void SetLocalTransform(SceneNodeHandle node, const SceneTransform &transform);
        void SetTranslation(SceneNodeHandle node, const vec3 &translation);
        void SetRotation(SceneNodeHandle node, const quat &rotation);
        void SetScale(SceneNodeHandle node, const vec3 &scale);

        const SceneTransform &GetLocalTransform(SceneNodeHandle node) const;
        const mat4 &GetWorldMatrix(SceneNodeHandle node);

        SceneGraph &GetGraph() { return m_graph; }
        const SceneGraph &GetGraph() const { return m_graph; }

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

    private:
        SceneGraph m_graph;
        std::unordered_map<SceneNodeHandle, SceneNodeComponent *> m_componentLookup;
    };
}
