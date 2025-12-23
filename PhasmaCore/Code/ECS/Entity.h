#pragma once

namespace pe
{
    class Context;
    class IComponent;

    class Entity
    {
    public:
        Entity() : m_context(nullptr), m_id(ID::NextID()), m_enabled(false) {}
        virtual ~Entity() = default;

        [[nodiscard]] size_t GetID() const noexcept { return m_id; }
        [[nodiscard]] Context *GetContext() const noexcept { return m_context; }
        void SetContext(Context *context) noexcept { m_context = context; }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        template <class T>
        inline T *GetComponent();
        auto &GetComponents() noexcept { return m_components; }
        [[nodiscard]] uint32_t GetComponentCount() const noexcept { return static_cast<uint32_t>(m_components.size()); }
        template <class T, class... Params>
        inline T *CreateComponent(Params &&...params);
        template <class T>
        inline void RemoveComponent();

    private:
        size_t m_id;
        Context *m_context;
        std::unordered_map<size_t, std::unique_ptr<IComponent>> m_components;
        bool m_enabled;
    };

    template <class T>
    inline T *Entity::GetComponent()
    {
        ValidateBaseClass<IComponent, T>();

        auto it = m_components.find(ID::GetTypeID<T>());
        if (it != m_components.end())
            return static_cast<T *>(it->second.get());
        else
            return nullptr;
    }

    template <class T, class... Params>
    inline T *Entity::CreateComponent(Params &&...params)
    {
        ValidateBaseClass<IComponent, T>();

        size_t id = ID::GetTypeID<T>();
        auto it = m_components.find(id);

        if (it == m_components.end())
        {
            auto comp = std::make_unique<T>(std::forward<Params>(params)...);
            comp->SetEntity(this);
            comp->SetEnabled(true);
            it = m_components.emplace(id, std::move(comp)).first;
        }

        return static_cast<T *>(it->second.get());
    }

    template <class T>
    inline void Entity::RemoveComponent()
    {
        ValidateBaseClass<IComponent, T>();

        size_t id = ID::GetTypeID<T>();
        auto it = m_components.find(id);

        if (it != m_components.end())
            m_components.erase(it);
    }
} // namespace pe
