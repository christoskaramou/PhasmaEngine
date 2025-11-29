#include "Scene/SceneSystem.h"
#include "Scene/SceneNodeComponent.h"

namespace pe
{
    SceneNodeHandle SceneSystem::RegisterComponent(SceneNodeComponent *component, SceneNodeHandle parent)
    {
        PE_ERROR_IF(component == nullptr, "SceneNodeComponent is null");

        SceneNodeHandle handle = m_graph.CreateNode(parent);
        m_componentLookup[handle] = component;
        AttachComponent(component);
        return handle;
    }

    void SceneSystem::UnregisterComponent(SceneNodeComponent *component)
    {
        if (!component)
            return;

        SceneNodeHandle handle = component->GetHandle();
        if (handle == InvalidSceneNode)
            return;

        m_componentLookup.erase(handle);
        m_graph.DestroyNode(handle);
        RemoveComponent<SceneNodeComponent>(component);
    }

    void SceneSystem::SetParent(SceneNodeHandle node, SceneNodeHandle parent)
    {
        m_graph.SetParent(node, parent);
    }

    void SceneSystem::SetLocalTransform(SceneNodeHandle node, const SceneTransform &transform)
    {
        m_graph.SetLocalTransform(node, transform);
    }

    void SceneSystem::SetTranslation(SceneNodeHandle node, const vec3 &translation)
    {
        m_graph.SetLocalTranslation(node, translation);
    }

    void SceneSystem::SetRotation(SceneNodeHandle node, const quat &rotation)
    {
        m_graph.SetLocalRotation(node, rotation);
    }

    void SceneSystem::SetScale(SceneNodeHandle node, const vec3 &scale)
    {
        m_graph.SetLocalScale(node, scale);
    }

    const SceneTransform &SceneSystem::GetLocalTransform(SceneNodeHandle node) const
    {
        return m_graph.GetLocalTransform(node);
    }

    const mat4 &SceneSystem::GetWorldMatrix(SceneNodeHandle node)
    {
        return m_graph.GetWorldMatrix(node);
    }

    void SceneSystem::Init(CommandBuffer *cmd)
    {
        (void)cmd;
    }

    void SceneSystem::Update()
    {
        m_graph.UpdateWorldTransforms();
    }

    void SceneSystem::Destroy()
    {
        for (auto &[handle, component] : m_componentLookup)
        {
            (void)handle;
            if (component)
                component->HandleSystemDestroyed();
        }
        m_componentLookup.clear();
        RemoveAllComponents();
        m_graph = SceneGraph();
    }
}
