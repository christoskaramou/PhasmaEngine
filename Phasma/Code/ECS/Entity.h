#pragma once

namespace pe
{
    class Context;

    class Entity
    {
    public:
        Entity() : m_context(nullptr), m_id(NextID()), m_enabled(false)
        {
        }

        virtual ~Entity()
        {
        }

        size_t GetID() const
        {
            return m_id;
        }

        Context *GetContext()
        {
            return m_context;
        }

        void SetContext(Context *context)
        {
            m_context = context;
        }

        bool IsEnabled()
        {
            return m_enabled;
        }

        void SetEnabled(bool enabled)
        {
            m_enabled = enabled;
        }

        template <class T>
        inline bool HasComponent();

        template <class T>
        inline T *GetComponent();

        template <class T, class... Params>
        inline T *CreateComponent(Params &&...params);

        template <class T>
        inline void RemoveComponent();

    private:
        size_t m_id;
        Context *m_context;
        std::unordered_map<size_t, std::shared_ptr<IComponent>> m_components;
        bool m_enabled;
    };

    template <class T>
    inline bool Entity::HasComponent()
    {
        ValidateBaseClass<IComponent, T>();

        if (m_components.find(GetTypeID<T>()) != m_components.end())
            return true;
        else
            return false;
    }

    template <class T>
    inline T *Entity::GetComponent()
    {
        ValidateBaseClass<IComponent, T>();
        
        auto it = m_components.find(GetTypeID<T>());
        if (it != m_components.end())
            return static_cast<T *>(it->second.get());
        else
            return nullptr;
    }

    template <class T, class... Params>
    inline T *Entity::CreateComponent(Params &&...params)
    {
        size_t id = GetTypeID<T>();

        if (!HasComponent<T>())
        {
            m_components[id] = std::make_shared<T>(std::forward<Params>(params)...);
            m_components[id]->SetEntity(this);
            m_components[id]->SetEnabled(true);
        }

        return static_cast<T *>(m_components[id].get());
    }

    template <class T>
    inline void Entity::RemoveComponent()
    {
        if (HasComponent<T>())
            m_components.erase(GetTypeID<T>());
    }
}
