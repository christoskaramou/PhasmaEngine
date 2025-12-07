#include "Scene/Actor.h"

namespace pe
{
    void Actor::SetRootComponent(scene::SceneComponent *root)
    {
        if (m_rootComponent == root)
            return;

        if (m_rootComponent)
            m_rootComponent->DetachFromParent();

        m_rootComponent = root;
        if (m_rootComponent)
        {
            // Ensure new root is detached from any previous parent and marked dirty.
            m_rootComponent->DetachFromParent();
            m_rootComponent->SetRelativeTransform(m_rootComponent->GetRelativeTransform());
        }
    }
}
