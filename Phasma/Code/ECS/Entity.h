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
        inline T *GetComponent();

        auto &GetComponents() { return m_components; }

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
        ValidateBaseClass<IComponent, T>();

        size_t id = GetTypeID<T>();
        auto it = m_components.find(id);

        if (it == m_components.end())
        {
            auto comp = std::make_shared<T>(std::forward<Params>(params)...);
            comp->SetEntity(this);
            comp->SetEnabled(true);
            it = m_components.emplace(id, comp).first;
        }

        return static_cast<T *>(it->second.get());
    }

    template <class T>
    inline void Entity::RemoveComponent()
    {
        ValidateBaseClass<IComponent, T>();

        size_t id = GetTypeID<T>();
        auto it = m_components.find(id);

        if (it != m_components.end())
            m_components.erase(it);
    }
}
