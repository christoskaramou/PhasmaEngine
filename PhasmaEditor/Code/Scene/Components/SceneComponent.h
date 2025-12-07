#pragma once

#include <vector>
#include "ECS/Component.h"
#include "Scene/Components/Transform.h"

namespace pe::scene
{
    class SceneComponent : public IComponent
    {
    public:
        SceneComponent() = default;
        ~SceneComponent() override = default;

        [[nodiscard]] const Transform &GetRelativeTransform() const noexcept { return m_relative; }
        void SetRelativeTransform(const Transform &transform);

        [[nodiscard]] const Transform &GetWorldTransform();

        [[nodiscard]] SceneComponent *GetParent() const noexcept { return m_parent; }
        [[nodiscard]] const std::vector<SceneComponent *> &GetChildren() const noexcept { return m_children; }

        void AddChild(SceneComponent *child);
        void RemoveChild(SceneComponent *child);
        void DetachFromParent();

    protected:
        virtual void OnWorldTransformUpdated() {}
        void MarkDirty();

    private:
        void MarkDirtyRecursive();

        Transform m_relative{};
        Transform m_world{};
        SceneComponent *m_parent = nullptr;
        std::vector<SceneComponent *> m_children{};
        bool m_dirty = true;
    };
}
