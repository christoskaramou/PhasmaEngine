#include "Scene/Components/SceneComponent.h"

#include <algorithm>

namespace pe::scene
{
    void SceneComponent::SetRelativeTransform(const Transform &transform)
    {
        m_relative = transform;
        MarkDirtyRecursive();
    }

    const Transform &SceneComponent::GetWorldTransform()
    {
        if (m_dirty)
        {
            if (m_parent)
                m_world = Combine(m_parent->GetWorldTransform(), m_relative);
            else
                m_world = m_relative;

            m_dirty = false;
            OnWorldTransformUpdated();
        }

        return m_world;
    }

    void SceneComponent::AddChild(SceneComponent *child)
    {
        if (!child)
            return;

        if (child->m_parent == this)
            return;

        if (child->m_parent)
            child->m_parent->RemoveChild(child);

        if (std::find(m_children.begin(), m_children.end(), child) != m_children.end())
            return;

        m_children.push_back(child);
        child->m_parent = this;
        child->MarkDirtyRecursive();
    }

    void SceneComponent::RemoveChild(SceneComponent *child)
    {
        if (!child)
            return;

        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it != m_children.end())
        {
            (*it)->m_parent = nullptr;
            (*it)->MarkDirtyRecursive();
            m_children.erase(it);
        }
    }

    void SceneComponent::DetachFromParent()
    {
        if (m_parent)
            m_parent->RemoveChild(this);
    }

    void SceneComponent::MarkDirty()
    {
        m_dirty = true;
    }

    void SceneComponent::MarkDirtyRecursive()
    {
        m_dirty = true;
        for (SceneComponent *child : m_children)
        {
            if (child)
                child->MarkDirtyRecursive();
        }
    }
}
