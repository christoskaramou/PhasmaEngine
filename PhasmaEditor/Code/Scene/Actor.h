#pragma once

#include <string>
#include "ECS/Entity.h"
#include "Scene/Components/SceneComponent.h"

namespace pe
{
    class Actor : public Entity
    {
    public:
        Actor() = default;
        explicit Actor(const std::string &name) : m_name(name) {}
        ~Actor() override = default;

        [[nodiscard]] const std::string &GetName() const noexcept { return m_name; }
        void SetName(const std::string &name) { m_name = name; }

        [[nodiscard]] scene::SceneComponent *GetRootComponent() const noexcept { return m_rootComponent; }
        void SetRootComponent(scene::SceneComponent *root);

    private:
        std::string m_name;
        scene::SceneComponent *m_rootComponent = nullptr;
    };
}
