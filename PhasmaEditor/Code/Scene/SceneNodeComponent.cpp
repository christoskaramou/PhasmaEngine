#include "Scene/SceneNodeComponent.h"
#include "Scene/SceneSystem.h"

namespace pe
{
    SceneNodeComponent::SceneNodeComponent()
        : m_system(nullptr), m_handle(InvalidSceneNode)
    {
    }

    SceneNodeComponent::~SceneNodeComponent()
    {
        Unbind();
    }

    void SceneNodeComponent::Destroy()
    {
        Unbind();
    }

    void SceneNodeComponent::Bind(SceneSystem *system, SceneNodeHandle parent)
    {
        if (m_system == system && m_handle != InvalidSceneNode)
            return;

        Unbind();

        m_system = system;
        if (!m_system)
            return;

        m_handle = m_system->RegisterComponent(this, parent);
    }

    void SceneNodeComponent::Unbind()
    {
        if (!m_system)
            return;

        if (m_handle != InvalidSceneNode)
            m_system->UnregisterComponent(this);

        m_system = nullptr;
        m_handle = InvalidSceneNode;
    }

    const mat4 &SceneNodeComponent::GetWorldMatrix() const
    {
        static mat4 s_identity = mat4(1.0f);
        if (!m_system || m_handle == InvalidSceneNode)
            return s_identity;

        return m_system->GetWorldMatrix(m_handle);
    }

    const SceneTransform &SceneNodeComponent::GetLocalTransform() const
    {
        static SceneTransform s_identity{};
        if (!m_system || m_handle == InvalidSceneNode)
            return s_identity;

        return m_system->GetLocalTransform(m_handle);
    }

    void SceneNodeComponent::SetLocalTransform(const SceneTransform &transform)
    {
        if (!m_system || m_handle == InvalidSceneNode)
            return;

        m_system->SetLocalTransform(m_handle, transform);
    }

    void SceneNodeComponent::SetTranslation(const vec3 &translation)
    {
        if (!m_system || m_handle == InvalidSceneNode)
            return;

        m_system->SetTranslation(m_handle, translation);
    }

    void SceneNodeComponent::SetRotation(const quat &rotation)
    {
        if (!m_system || m_handle == InvalidSceneNode)
            return;

        m_system->SetRotation(m_handle, rotation);
    }

    void SceneNodeComponent::SetScale(const vec3 &scale)
    {
        if (!m_system || m_handle == InvalidSceneNode)
            return;

        m_system->SetScale(m_handle, scale);
    }

    void SceneNodeComponent::SetParent(SceneNodeHandle parent)
    {
        if (!m_system || m_handle == InvalidSceneNode)
            return;

        m_system->SetParent(m_handle, parent);
    }

    void SceneNodeComponent::HandleSystemDestroyed()
    {
        m_system = nullptr;
        m_handle = InvalidSceneNode;
    }
}
