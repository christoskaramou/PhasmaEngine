#pragma once

#include "Scene/SceneGraph.h"

namespace pe
{
    class SceneSystem;

    class SceneNodeComponent : public IComponent
    {
    public:
        SceneNodeComponent();
        ~SceneNodeComponent() override;

        void Destroy() override;

        void Bind(SceneSystem *system, SceneNodeHandle parent = InvalidSceneNode);
        void Unbind();

        SceneNodeHandle GetHandle() const { return m_handle; }
        SceneSystem *GetSystem() const { return m_system; }

        const mat4 &GetWorldMatrix() const;
        const SceneTransform &GetLocalTransform() const;

        void SetLocalTransform(const SceneTransform &transform);
        void SetTranslation(const vec3 &translation);
        void SetRotation(const quat &rotation);
        void SetScale(const vec3 &scale);
        void SetParent(SceneNodeHandle parent);

    private:
        friend class SceneSystem;

        SceneSystem *m_system;
        SceneNodeHandle m_handle;

        void HandleSystemDestroyed();
    };
}
